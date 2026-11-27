import Foundation

/// Loads the bundled wavetables, built-in samples and a starting preset into the C++ engine, and swaps presets later.
/// Loading is NOT realtime safe, so it always runs on a background queue; the engine stages everything and picks it up in the next render.
/// NOTE: written without a Mac. Not compiled yet. See docs/MAC_DAY_ONE.md.
public final class InstrumentLoader {
    private let queue = DispatchQueue(label: "vv.loader", qos: .userInitiated)
    private let engine: OpaquePointer
    private let bundle: Bundle
    public private(set) var currentPresetJSON: String = ""
    public private(set) var isLoaded = false
    /// Called on the main thread when the bundled assets and the starting preset are in.
    public var onLoaded: (() -> Void)?
    /// Called on the main thread after any preset is applied and the host parameters have been re-synced from it.
    public var onPresetApplied: (() -> Void)?

    public init(engine: OpaquePointer, bundle: Bundle) { self.engine = engine; self.bundle = bundle }

    /// Wavetables first (a preset refers to them by name), then samples, then the preset.
    public func loadEverything(presetJSON: String? = nil, completion: (() -> Void)? = nil) {
        queue.async { [self] in
            if let dir = bundle.url(forResource: "Wavetables", withExtension: nil),
               let files = try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil) {
                for f in files where f.pathExtension == "vvwt" {
                    if let d = try? Data(contentsOf: f) {
                        let name = f.deletingPathExtension().lastPathComponent
                        d.withUnsafeBytes { raw in _ = vv_engine_load_wavetable(engine, name, raw.bindMemory(to: UInt8.self).baseAddress, d.count) }
                    }
                }
            }
            loadBuiltInSamples()
            let json = presetJSON ?? defaultPresetJSON()
            applyPreset(json)
            DispatchQueue.main.async { [self] in isLoaded = true; onLoaded?() }
            completion?()
        }
    }

    /// Library entries are named by their display name (the web app looks samples up by that name).
    private func loadBuiltInSamples() {
        guard let listURL = bundle.url(forResource: "builtin_samples", withExtension: "json"),
              let data = try? Data(contentsOf: listURL),
              let arr = try? JSONSerialization.jsonObject(with: data) as? [[String: String]],
              let dir = bundle.url(forResource: "Samples", withExtension: nil) else { return }
        for item in arr {
            guard let name = item["name"], let file = item["file"], let d = try? Data(contentsOf: dir.appendingPathComponent(file)) else { continue }
            d.withUnsafeBytes { raw in _ = vv_engine_load_sample_wav(engine, name, raw.bindMemory(to: UInt8.self).baseAddress, d.count) }
        }
    }

    private func defaultPresetJSON() -> String {
        if let u = bundle.url(forResource: "default_preset", withExtension: "json"), let s = try? String(contentsOf: u) { return s }
        return "{}"
    }

    /// Accepts the web app's snapshotState() JSON (export a preset from the web app and load it here).
    @discardableResult public func applyPreset(_ json: String) -> Bool {
        let ok = json.withCString { vv_engine_load_preset_json(engine, $0, json.utf8.count) } == 1
        if ok {
            currentPresetJSON = json
            _ = vv_engine_sync_params_from_preset(engine)
            DispatchQueue.main.async { [self] in onPresetApplied?() }
        }
        return ok
    }

    /// The live preset (including edits and automation) as JSON, for saving. Falls back to the last loaded text.
    public func exportJSON() -> String {
        let n = vv_engine_get_value(engine, "", nil, 0)
        guard n > 2 else { return currentPresetJSON }
        var buf = [CChar](repeating: 0, count: n + 1)
        _ = vv_engine_get_value(engine, "", &buf, n + 1)
        return String(cString: buf)
    }
    public func loadPreset(_ json: String, completion: ((Bool) -> Void)? = nil) { queue.async { [self] in completion?(applyPreset(json)) } }
}
