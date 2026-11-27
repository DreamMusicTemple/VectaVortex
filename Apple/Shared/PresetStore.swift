// PresetStore: the SwiftUI side's mirror of the engine's preset JSON. Every control reads from the mirror and writes through
// vv_engine_set_value (dot path + JSON value), which the engine applies live without resetting the sound.
// NOTE: written without a Mac. Not compiled yet.
import Foundation
import SwiftUI

final class PresetStore: ObservableObject {
    @Published private(set) var revision = 0
    private var doc: [String: Any] = [:]
    let audioUnit: VectaAudioUnit
    private var engine: OpaquePointer? { audioUnit.enginePointer }

    init(audioUnit: VectaAudioUnit) {
        self.audioUnit = audioUnit
        if let l = audioUnit.loader {
            l.onLoaded = { [weak self] in self?.reload() }
            if l.isLoaded { reload() }
        }
        // host automation / preset loads change the engine's preset behind our back: refresh the mirror (debounced)
        observer = NotificationCenter.default.addObserver(forName: .vvAutomationApplied, object: audioUnit, queue: .main) { [weak self] _ in self?.scheduleReload() }
    }
    deinit { if let o = observer { NotificationCenter.default.removeObserver(o) } }
    private var observer: NSObjectProtocol?
    private var reloadPending = false
    private func scheduleReload() {
        if reloadPending { return }
        reloadPending = true
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { [weak self] in self?.reloadPending = false; self?.reload() }
    }

    // MARK: undo / redo / current preset (the top bar). Edits within 0.7 s form one undo step; a preset load is always its own step.
    @Published private(set) var canUndo = false
    @Published private(set) var canRedo = false
    @Published var presetName = ""
    @Published var requestImport = false     // LOAD in the top bar → the Presets tab opens its importer
    @Published var requestSaveAs = false
    private var undoStack: [String] = [], redoStack: [String] = []
    private var lastEdit = Date.distantPast
    private var replaying = false
    private func captureUndo(force: Bool = false) {
        guard engine != nil, !replaying else { return }
        let now = Date()
        if !force && now.timeIntervalSince(lastEdit) < 0.7 && !undoStack.isEmpty { lastEdit = now; return }
        lastEdit = now
        let snap = exportJSON(); guard !snap.isEmpty else { return }
        undoStack.append(snap); if undoStack.count > 60 { undoStack.removeFirst() }
        redoStack.removeAll(); canUndo = true; canRedo = false
    }
    private func replay(_ text: String) {
        replaying = true; _ = audioUnit.loader?.applyPreset(text); replaying = false
        lastEdit = .distantPast; reload(); canUndo = !undoStack.isEmpty; canRedo = !redoStack.isEmpty
    }
    @discardableResult func undo() -> Bool { guard let t = undoStack.popLast() else { return false }; redoStack.append(exportJSON()); replay(t); return true }
    @discardableResult func redo() -> Bool { guard let t = redoStack.popLast() else { return false }; undoStack.append(exportJSON()); replay(t); return true }
    func clearHistory() { undoStack = []; redoStack = []; canUndo = false; canRedo = false }

    static var userFolder: URL {
        let u = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("Presets", isDirectory: true)
        try? FileManager.default.createDirectory(at: u, withIntermediateDirectories: true)
        return u
    }
    /// Factory presets (bundled) followed by the user's, each sorted by name: the order the top bar's ◀ ▶ walk through.
    func allPresetFiles() -> [(name: String, url: URL)] {
        let b = Bundle(for: VectaAudioUnit.self)
        let fac = (b.url(forResource: "Presets", withExtension: nil)).flatMap { try? FileManager.default.contentsOfDirectory(at: $0, includingPropertiesForKeys: nil) } ?? []
        let mine = (try? FileManager.default.contentsOfDirectory(at: PresetStore.userFolder, includingPropertiesForKeys: nil)) ?? []
        func list(_ a: [URL]) -> [(name: String, url: URL)] { a.filter { $0.pathExtension == "json" }.map { ($0.deletingPathExtension().lastPathComponent, $0) }.sorted { $0.0.localizedCaseInsensitiveCompare($1.0) == .orderedAscending } }
        return list(fac) + list(mine)
    }
    @discardableResult func stepPreset(_ dir: Int) -> String {
        let files = allPresetFiles(); guard !files.isEmpty else { return "No presets" }
        let cur = files.firstIndex { $0.name == presetName }
        let n = files.count
        let next = cur.map { (($0 + dir) % n + n) % n } ?? (dir > 0 ? 0 : n - 1)
        guard let t = try? String(contentsOf: files[next].url), load(json: t) else { return "Could not load \(files[next].name)" }
        presetName = files[next].name; return "Loaded \(presetName)"
    }
    @discardableResult func quickSave() -> String {
        var name = presetName
        if name.isEmpty { let f = DateFormatter(); f.dateFormat = "yyyy-MM-dd HHmmss"; name = "Quick save " + f.string(from: Date()) }
        let json = exportJSON(); guard !json.isEmpty else { return "Nothing to save yet" }
        do { try json.write(to: PresetStore.userFolder.appendingPathComponent(name + ".json"), atomically: true, encoding: .utf8); presetName = name; return "Saved \(name)" }
        catch { return "Could not save: \(error.localizedDescription)" }
    }
    static let generatableCategories = ["Arp", "Bass", "Key", "Lead", "Pad", "Drum", "FX", "XFX"]
    /// Web "magic wand" GENERATE: the engine builds a category-appropriate patch; it is filed in the user library as "<Category> Gen N".
    @discardableResult func generate(_ category: String) -> String {
        guard let e = engine else { return "" }
        // Follows the keyboard scale when a scale is chosen and Scale lock is on (same @AppStorage keys as KeyboardView).
        let d = UserDefaults.standard, idx = d.integer(forKey: "vv.kb.scale"), root = d.integer(forKey: "vv.kb.root"), lock = d.bool(forKey: "vv.kb.lock")
        let scaleOn = lock && idx > 0 && idx < kbScales.count
        var mask: UInt32 = 0; if scaleOn { for i in kbScales[idx].iv { mask |= UInt32(1) << UInt32(((i % 12) + 12) % 12) } }
        var nm = [CChar](repeating: 0, count: 96), vw = [CChar](repeating: 0, count: 8)
        captureUndo(force: true); replaying = true
        let ok = vv_engine_preset_generate(e, category, 0, scaleOn ? 1 : 0, Int32(root), mask, scaleOn ? kbScales[idx].id : "", &nm, 96, &vw, 8)
        replaying = false
        guard ok == 1 else { return "Generate failed" }
        afterEngineEdit()
        let variant = String(cString: nm)
        var n = 1; while FileManager.default.fileExists(atPath: PresetStore.userFolder.appendingPathComponent("\(category) Gen \(n) - \(variant).json").path) { n += 1 }
        let name = "\(category) Gen \(n) - \(variant)"
        try? exportJSON().write(to: PresetStore.userFolder.appendingPathComponent(name + ".json"), atomically: true, encoding: .utf8)
        presetName = name; return "Generated \(name)"
    }
    /// Web MODIFY: nudges the loaded sound by about 10 % and files the result as "<name> (mod)".
    @discardableResult func modify() -> String {
        guard let e = engine else { return "" }
        captureUndo(force: true); replaying = true; let ok = vv_engine_preset_action(e, "modify", "", 0); replaying = false
        guard ok == 1 else { return "Modify failed" }
        afterEngineEdit()
        let base = presetName.isEmpty ? "Preset" : (presetName.range(of: " (mod", options: .backwards).map { String(presetName[..<$0.lowerBound]) } ?? presetName)
        var name = base + " (mod)", n = 2
        while FileManager.default.fileExists(atPath: PresetStore.userFolder.appendingPathComponent(name + ".json").path) { name = base + " (mod \(n))"; n += 1 }
        try? exportJSON().write(to: PresetStore.userFolder.appendingPathComponent(name + ".json"), atomically: true, encoding: .utf8)
        presetName = name; return "Saved \(name)"
    }
    private func afterEngineEdit() {
        if let e = engine { _ = vv_engine_sync_params_from_preset(e) }
        audioUnit.refreshParametersFromEngine(notify: false); reload()
    }
    @discardableResult func initPreset() -> String {
        let b = Bundle(for: VectaAudioUnit.self)
        guard let u = b.url(forResource: "default_preset", withExtension: "json"), let t = try? String(contentsOf: u), load(json: t) else { return "Could not initialise" }
        presetName = ""; return "Initialised"
    }

    // MARK: reading
    func reload() {
        guard let e = engine else { return }
        let n = vv_engine_get_value(e, "", nil, 0)
        guard n > 0 else { return }
        var buf = [CChar](repeating: 0, count: n + 1)
        _ = vv_engine_get_value(e, "", &buf, n + 1)
        let data = Data(bytes: buf, count: n)
        if let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any] { doc = obj; revision += 1 }
    }
    func value(_ path: String) -> Any? {
        var cur: Any? = doc
        for key in path.split(separator: ".") {
            if let a = cur as? [Any], let i = Int(key) { cur = i < a.count ? a[i] : nil }
            else if let d = cur as? [String: Any] { cur = d[String(key)] }
            else { return nil }
        }
        return cur
    }
    /// Peak level of one Mixer track (index = web MIXER_TRACKS order), linear 0...~4; 0 without an engine.
    /// Web SOLO / MUTE: bit l = layer l (0 WT, 1 FM, 2 SMP, 3 PRO, 4 SID). View state only, as on the web (not saved in presets).
    @Published var viewLayer = 0      // web footer VIEW buttons: which layer the Sound tab edits
    @Published var transportRunning = false
    @Published var soloMask = 0
    @Published var muteMask = 0
    func setSolo(_ layer: Int, _ on: Bool) { soloMask = on ? soloMask | (1 << layer) : soloMask & ~(1 << layer); pushSoloMute() }
    func setMute(_ layer: Int, _ on: Bool) { muteMask = on ? muteMask | (1 << layer) : muteMask & ~(1 << layer); pushSoloMute() }
    private func pushSoloMute() { if let e = engine { vv_engine_set_solo_mute(e, Int32(soloMask), Int32(muteMask)) } }
    /// Chord analyzer readout (engine keeps the last recognised chord). Call from the UI thread.
    func chordLabel() -> String {
        guard let e = engine else { return "" }
        var buf = [CChar](repeating: 0, count: 64); _ = vv_engine_chord_label(e, &buf, 64); return String(cString: buf)
    }
    // MARK: sample recorder (vv_engine_rec_*): lock-free on the audio thread, allocation happens in recPrepare
    var engineSampleRate: Double { engine.map { vv_engine_sample_rate($0) } ?? 48000 }
    func recPrepare(external: Bool, arm: Bool) { if let e = engine { _ = vv_engine_rec_prepare(e, Int32(engineSampleRate), 120, external ? 1 : 0, arm ? 1 : 0, 0.02) } }
    func recStart() { if let e = engine { vv_engine_rec_start(e) } }
    func recStop() { if let e = engine { vv_engine_rec_stop(e) } }
    var recState: Int { engine.map { Int(vv_engine_rec_state($0)) } ?? 0 }
    var recFrames: Int { engine.map { Int(vv_engine_rec_frames($0)) } ?? 0 }
    var recPeak: Float { engine.map { vv_engine_rec_peak($0) } ?? 0 }
    func recTake() -> AudioHandle? { engine.flatMap { AudioHandle(vv_engine_rec_take($0, Int32(engineSampleRate))) } }

    // MARK: DAW-style recorder + click (vv_daw_*): records the master output; the click is heard but never recorded
    func dawStart() { if let e = engine { vv_daw_start(e) } }
    func dawStop() { if let e = engine { vv_daw_stop(e) } }
    func dawDrain() -> Double { engine.map { vv_daw_drain($0) } ?? 0 }
    var dawState: Int { engine.map { Int(vv_daw_state($0)) } ?? 0 }
    var dawDropped: Int { engine.map { Int(vv_daw_dropped($0)) } ?? 0 }
    func dawSetClick(_ mode: Int) { if let e = engine { vv_daw_set_click(e, Int32(mode)) } }
    /// Safe to call from a background thread while no new recording is started (resampling a long take takes a moment).
    func dawTakeWav(bits: Int, rate: Int) -> Data {
        guard let e = engine else { return Data() }
        let n = vv_daw_take_wav(e, Int32(bits), Int32(rate), nil, 0); guard n > 0 else { return Data() }
        var d = Data(count: n); d.withUnsafeMutableBytes { _ = vv_daw_take_wav(e, Int32(bits), Int32(rate), $0.bindMemory(to: UInt8.self).baseAddress, n) }; return d
    }

    func setTransport(_ on: Bool) { transportRunning = on; if let e = engine { vv_engine_start_sequencer(e, on ? 1 : 0) } }
    func allNotesOff() { if let e = engine { vv_engine_all_notes_off(e) } }
    // sample editor: audition a slot / one of its transient slices (slice 0 = whole start..end), and the live playhead (fraction of the source, -1 idle)
    func auditionSlice(slot: Int, slice: Int) { if let e = engine { vv_engine_audition_slice(e, Int32(slot), Int32(slice), 0.9) } }
    func auditionOff() { if let e = engine { vv_engine_audition_off(e) } }
    func sampleHead(_ slot: Int) -> Float { engine.map { vv_engine_sample_head($0, Int32(slot)) } ?? -1 }
    func mixerLevel(_ i: Int) -> Float { engine.map { vv_engine_mixer_level($0, Int32(i)) } ?? 0 }
    /// R11: lock-free snapshots for the display-rate LFO previews (phase 0..1, rate in Hz; 0 while held).
    func lfoPhase(_ k: Int) -> Float { engine.map { vv_engine_lfo_phase($0, Int32(k)) } ?? 0 }
    func lfoRateHz(_ k: Int) -> Float { engine.map { vv_engine_lfo_rate_hz($0, Int32(k)) } ?? 0 }
    func double(_ path: String, _ def: Double = 0) -> Double { (value(path) as? NSNumber)?.doubleValue ?? def }
    func int(_ path: String, _ def: Int = 0) -> Int { Int((value(path) as? NSNumber)?.doubleValue.rounded() ?? Double(def)) }
    func bool(_ path: String, _ def: Bool = false) -> Bool { (value(path) as? NSNumber)?.boolValue ?? def }
    func string(_ path: String, _ def: String = "") -> String { (value(path) as? String) ?? def }
    func count(_ path: String) -> Int { (value(path) as? [Any])?.count ?? 0 }
    func array(_ path: String) -> [Any] { (value(path) as? [Any]) ?? [] }
    /// Round 6: the XFX MODE pseudo layer keeps its sequencer patterns in extra.xfxSeq (web state.xfxSeq). Creates 8 empty roll patterns the first time it is edited.
    func ensureXfxSeq() {
        if count("extra.xfxSeq.patterns") > 0 { return }
        let roll: [String: Any] = ["bars": 4, "loopOn": true, "loopStart": 0, "loopEnd": 4, "notes": [Any]()]
        let pats: [[String: Any]] = (0..<8).map { _ in ["length": 16, "steps": [Any](), "roll": roll] }
        set("extra.xfxSeq", ["currentSlot": 0, "chainMode": "off", "chainOrder": [Any](), "gate": 0.7, "patterns": pats] as [String: Any])
    }
    func dict(_ path: String) -> [String: Any] { (value(path) as? [String: Any]) ?? [:] }

    // MARK: XFX MODE (a different sound on every key). State is extra.xfx in the preset (the web app's own JSON).
    /// action: enable{on} bank{index} rename{name} setOverride{note,path,value} clearOverride{note,path} layer{note,layer,state} clearKey{note} copyKey{from,to} clearBank{} reset{}
    @discardableResult func xfxAction(_ action: String, _ args: [String: Any] = [:]) -> Bool {
        guard let e = engine, let data = try? JSONSerialization.data(withJSONObject: args), let json = String(data: data, encoding: .utf8) else { return false }
        captureUndo()
        let ok = vv_engine_xfx_action(e, action, json) != 0
        reload(); return ok
    }
    /// extra.xfx as a dictionary (nil when the preset has no XFX programming).
    func xfx() -> [String: Any]? {
        guard let e = engine else { return nil }
        let n = vv_engine_xfx_export(e, nil, 0); guard n > 0 else { return nil }
        var buf = [CChar](repeating: 0, count: n + 1); _ = vv_engine_xfx_export(e, &buf, n + 1)
        return (try? JSONSerialization.jsonObject(with: Data(bytes: buf, count: n)) as? [String: Any]) ?? nil
    }
    func auditionNote(_ note: Int, on: Bool) { guard let e = engine else { return }; if on { vv_engine_note_on(e, Int32(note), 0.8) } else { vv_engine_note_off(e, Int32(note)) } }

    // MARK: writing
    // MARK: LUA layer bridge (JSON from the C ABI; non-audio thread only)
    private func luaJSON(_ call: (UnsafeMutablePointer<CChar>, Int32) -> Int32) -> [String: Any] {
        var buf = [CChar](repeating: 0, count: 65536)
        let n = buf.withUnsafeMutableBufferPointer { call($0.baseAddress!, 65536) }
        guard n > 0 else { return [:] }
        let str = String(cString: buf)
        return (try? JSONSerialization.jsonObject(with: Data(str.utf8))) as? [String: Any] ?? [:]
    }
    func luaInfo(_ slot: Int) -> [String: Any] { guard let e = engine else { return [:] }; return luaJSON { vv_engine_lua_info(e, Int32(slot), $0, $1) } }
    func luaValidate(_ src: String, seed: Int) -> [String: Any] { guard let e = engine else { return [:] }; return luaJSON { vv_engine_lua_validate(e, src, Int32(seed), $0, $1) } }
    func luaStats(_ slot: Int) -> [String: Any] { guard let e = engine else { return [:] }; return luaJSON { vv_engine_lua_stats(e, Int32(slot), $0, $1) } }
    var luaFactory: [(id: String, src: String)] {
        (0..<Int(vv_engine_lua_factory_count())).map { i in
            (String(cString: vv_engine_lua_factory_id(Int32(i))), String(cString: vv_engine_lua_factory_src(Int32(i))))
        }
    }

    func set(_ path: String, _ v: Any) {
        if let d = v as? Double, !d.isFinite { return }
        captureUndo()
        doc = (assign(doc, path.split(separator: ".")[...], v) as? [String: Any]) ?? doc
        revision += 1
        if let e = engine, let data = try? JSONSerialization.data(withJSONObject: v, options: [.fragmentsAllowed]), let json = String(data: data, encoding: .utf8) {
            _ = vv_engine_set_value(e, path, json)
        }
        scheduleParamSync()
    }
    /// UI edits change preset-backed values that the host also sees as parameters; refresh those (debounced) so host knobs follow.
    private var syncPending = false
    private func scheduleParamSync() {
        if syncPending { return }
        syncPending = true
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { [weak self] in
            guard let self = self else { return }
            self.syncPending = false
            if let e = self.engine { _ = vv_engine_sync_params_from_preset(e) }
            self.audioUnit.refreshParametersFromEngine(notify: false)
        }
    }
    private func assign(_ cur: Any?, _ keys: ArraySlice<Substring>, _ v: Any) -> Any {
        guard let k = keys.first else { return v }
        let rest = keys.dropFirst()
        if let i = Int(k) {
            var a = (cur as? [Any]) ?? []
            while a.count <= i { a.append(NSNull()) }
            a[i] = assign(a[i], rest, v); return a
        }
        var d = (cur as? [String: Any]) ?? [:]
        d[String(k)] = assign(d[String(k)], rest, v); return d
    }
    /// Appends to an array at `path` (creating it).
    func append(_ path: String, _ item: Any) { var a = array(path); a.append(item); set(path, a) }
    func remove(_ path: String, at index: Int) { var a = array(path); guard index >= 0 && index < a.count else { return }; a.remove(at: index); set(path, a) }
    func move(_ path: String, from: Int, to: Int) { var a = array(path); guard from >= 0, to >= 0, from < a.count, to < a.count else { return }; let x = a.remove(at: from); a.insert(x, at: to); set(path, a) }

    // MARK: whole presets
    func exportJSON() -> String {
        guard let e = engine else { return "" }
        let n = vv_engine_get_value(e, "", nil, 0); if n == 0 { return "" }
        var buf = [CChar](repeating: 0, count: n + 1); _ = vv_engine_get_value(e, "", &buf, n + 1)
        return String(cString: buf)
    }
    /// The web app's single-preset file: {vectaVortexPreset:1, name, category, data}  (.vvpreset.json)
    func exportSinglePresetFile(name: String, category: String = "Misc") -> String {
        guard let d = exportJSON().data(using: .utf8), let obj = try? JSONSerialization.jsonObject(with: d),
              let out = try? JSONSerialization.data(withJSONObject: ["vectaVortexPreset": 1, "name": name, "category": category, "data": obj], options: [.sortedKeys]) else { return "" }
        return String(data: out, encoding: .utf8) ?? ""
    }
    @discardableResult func load(json raw: String) -> Bool {
        var json = raw
        if let d = raw.data(using: .utf8), let o = try? JSONSerialization.jsonObject(with: d) as? [String: Any], o["vectaVortexPreset"] != nil, let inner = o["data"],
           let dd = try? JSONSerialization.data(withJSONObject: inner), let t = String(data: dd, encoding: .utf8) { json = t }
        captureUndo(force: true)
        let ok = audioUnit.loader?.applyPreset(json) ?? false
        if ok { reload() }
        return ok
    }

    // MARK: bindings
    func binding(_ path: String, _ def: Double = 0) -> Binding<Double> { Binding(get: { self.double(path, def) }, set: { self.set(path, $0) }) }
    func binding(_ path: String, bool def: Bool) -> Binding<Bool> { Binding(get: { self.bool(path, def) }, set: { self.set(path, $0) }) }
    func binding(_ path: String, string def: String) -> Binding<String> { Binding(get: { self.string(path, def) }, set: { self.set(path, $0) }) }
    func binding(_ path: String, int def: Int) -> Binding<Int> { Binding(get: { self.int(path, def) }, set: { self.set(path, $0) }) }
}
