// XFX MODE editor (SwiftUI): a different sound on every key. Opened from the XFX button in the Sound tab.
// State is extra.xfx of the preset (the web app's own JSON, so presets travel both ways); every edit goes through PresetStore.xfxAction.
// Covers: on/off, 8 banks (rename / clear), key picker, per-key layer switches, per-key parameter overrides (curated list + any registry path).
// NOTE: written without a Mac - NOT compiled yet. The engine side is tested (Core/tests/xfx_env_tests.cpp).
import SwiftUI

struct XfxView: View {
    @ObservedObject var s: PresetStore
    @Environment(\.dismiss) private var dismiss
    @State private var key = 60
    @State private var path = "WT.env.rep"
    @State private var valueText = "3"
    @State private var renaming = false
    @State private var newName = ""
    @State private var xfxPresets: [PresetEntry] = []   // Round 6: every preset of the XFX category (factory + user), loadable from inside XFX MODE
    private let layerKeys = ["WT", "FM", "SMP", "PRO", "SID", "LUA"], layerNames = ["Wavetable", "FM", "Samples", "Procedural", "SID", "LUA"]
    private let common = ["WT.env.a", "WT.env.d", "WT.env.s", "WT.env.r", "WT.env.rep", "WT.oscs.0.level", "WT.oscs.0.oct", "WT.oscs.0.pan", "WT.oscs.0.hold",
                          "WT.oscs.0.filter.cutoff", "WT.oscs.0.filter.reso", "FM.env.a", "FM.env.d", "FM.env.s", "FM.env.r", "FM.env.rep", "FM.oscs.0.level",
                          "SMP.oscs.0.level", "SMP.oscs.0.env.rep", "LUA.oscs.0.level", "LUA.oscs.0.pan", "LUA.oscs.0.oct", "LUA.oscs.0.semitone", "LUA.oscs.0.fineTune", "LUA.oscs.0.velSens", "LUA.oscs.0.velLow", "LUA.oscs.0.velHigh", "LUA.oscs.0.filterRoute", "LUA.oscs.0.fxRoute", "LUA.oscs.0.lua.params.<script param id>", "SMP.globalAdsr.rep", "PRO.env.a", "PRO.env.r", "PRO.env.rep", "SID.oscs.0.env.rep", "SID.oscs.0.level"]
    private static let noteNames = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    private func name(_ n: Int) -> String { Self.noteNames[n % 12] + String(n / 12 - 1) }

    private var x: [String: Any] { s.revision >= 0 ? (s.xfx() ?? [:]) : [:] }
    private var enabled: Bool { (x["enabled"] as? Bool) ?? (x.isEmpty ? false : true) }
    private var activeBank: Int { min(7, max(0, (x["active"] as? Int) ?? 0)) }
    private var bank: [String: Any] { ((x["banks"] as? [[String: Any]]) ?? []).indices.contains(activeBank) ? ((x["banks"] as? [[String: Any]]) ?? [])[activeBank] : [:] }
    private var keysDict: [String: Any] { bank["keys"] as? [String: Any] ?? [:] }
    private var rec: [String: Any] { keysDict[String(key)] as? [String: Any] ?? [:] }
    private var overrides: [(String, String)] { ((rec["o"] as? [String: Any]) ?? [:]).map { ($0.key, "\($0.value)") }.sorted { $0.0 < $1.0 } }
    private func layerState(_ k: String) -> Int { guard let v = (rec["en"] as? [String: Any])?[k] else { return 0 }; return ((v as? Bool) ?? ((v as? NSNumber)?.boolValue ?? true)) ? 1 : 2 }

    private func presetBadge(_ p: PresetEntry) -> String {
        guard let d = try? Data(contentsOf: p.url), let o = try? JSONSerialization.jsonObject(with: d) as? [String: Any] else { return "" }
        let inner = (o["data"] as? [String: Any]) ?? o; let ex = inner["extra"] as? [String: Any]
        var b = ""; if let dr = ex?["drumSeq"] as? [String: Any], (dr["on"] as? Bool) == true { b += "  · drum seq" }
        if let cl = (ex?["clips"] as? [String: Any])?["banks"] as? [[String: Any]], let sl = cl.first?["slots"] as? [Any], sl.contains(where: { ($0 as? [String: Any])?["layer"] as? String == "XFX" }) { b += "  · 8 clips" }
        return b
    }
    private func loadXfxPreset(_ p: PresetEntry) {
        guard let raw = try? String(contentsOf: p.url, encoding: .utf8) else { return }
        if s.load(json: raw) { s.xfxAction("bank", ["index": 0]) }   // Bank 1
    }
    var body: some View {
        NavigationStack {
            Form {
                presetsSection
                modeSection
                keySection
                layersSection
                overridesSection
            }
            .navigationTitle("XFX MODE").onAppear { xfxPresets = PresetLibrary.scan().filter { $0.category == "XFX" } }
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
            .alert("Rename bank", isPresented: $renaming) { TextField("Name", text: $newName); Button("OK") { s.xfxAction("rename", ["name": newName]) }; Button("Cancel", role: .cancel) {} }
        }
    }

    // Keep the Form's sections and their bindings separate to reduce SwiftUI type-checker complexity.
    private func bankTitle(_ i: Int) -> String {
        let banks = x["banks"] as? [[String: Any]]
        return (banks?[safe: i]?["name"] as? String) ?? "Bank \(i + 1)"
    }
    private var keyTitle: String { "Key \(key)  (\(name(key)))  " + (rec.isEmpty ? "plays the normal sound" : "programmed") }
    private var programmedKeysText: String { keysDict.keys.compactMap { Int($0) }.sorted().map(name).joined(separator: " ") }
    private func layerBinding(_ i: Int) -> Binding<Int> {
        Binding(get: { layerState(layerKeys[i]) }, set: { v in
            let state: Any = v == 0 ? NSNull() : (v == 1)
            s.xfxAction("layer", ["note": key, "layer": layerKeys[i], "state": state])
        })
    }

    private var presetsSection: some View {
        Section("XFX PRESETS (loads into Bank 1)") {
            Menu {
                ForEach(xfxPresets) { p in Button(p.name + presetBadge(p)) { loadXfxPreset(p) } }
            } label: { Label(xfxPresets.isEmpty ? "No XFX presets found" : "LOAD XFX PRESET (\(xfxPresets.count)) ▾", systemImage: "square.and.arrow.down") }
            .disabled(xfxPresets.isEmpty)
            Text("Replaces the sound with the chosen XFX MODE preset (programmed keys in Bank 1). Drum-sequence presets switch the drum sequencer to XFX output; lead presets bring 8 XFX clips in the Clip launcher.").font(.caption2).foregroundStyle(.secondary)
        }
    }

    private var modeSection: some View {
        Section("XFX MODE") {
            Toggle("XFX MODE ON", isOn: Binding(get: { enabled }, set: { s.xfxAction("enable", ["on": $0]) }))
            Picker("Bank", selection: Binding(get: { activeBank }, set: { s.xfxAction("bank", ["index": $0]) })) {
                ForEach(0..<8, id: \.self) { i in Text(bankTitle(i)).tag(i) }
            }
            HStack {
                Button("Rename") { newName = (bank["name"] as? String) ?? ""; renaming = true }
                Spacer(); Button("Clear bank", role: .destructive) { s.xfxAction("clearBank") }
                Spacer(); Button("Reset XFX", role: .destructive) { s.xfxAction("reset") }
            }.buttonStyle(.bordered)
        }
    }

    private var keySection: some View {
        Section("Key") {
            Stepper(value: $key, in: 0...127) { Text(keyTitle).font(.callout) }
            HStack {
                Button("Audition") { s.auditionNote(key, on: true); DispatchQueue.main.asyncAfter(deadline: .now() + 0.8) { s.auditionNote(key, on: false) } }
                Spacer(); Button("Copy to next key") { if key < 127 { s.xfxAction("copyKey", ["from": key, "to": key + 1]); key += 1 } }
                Spacer(); Button("Clear key", role: .destructive) { s.xfxAction("clearKey", ["note": key]) }
            }.buttonStyle(.bordered)
            let prog = programmedKeysText
            Text(prog.isEmpty ? "No programmed keys in this bank." : "Programmed keys: " + prog).font(.caption).foregroundStyle(.secondary)
        }
    }

    private var layersSection: some View {
        Section("Layers on this key") {
            ForEach(0..<5, id: \.self) { i in
                Picker(layerNames[i], selection: layerBinding(i)) {
                    Text("Inherit").tag(0); Text("On").tag(1); Text("Off").tag(2)
                }.pickerStyle(.segmented)
            }
        }
    }

    private var overridesSection: some View {
        Section("Parameter overrides on this key") {
            HStack {
                TextField("LAYER.param.path", text: $path).textInputAutocapitalization(.never).autocorrectionDisabled().font(.callout.monospaced())
                Menu("List") { ForEach(common, id: \.self) { p in Button(p) { path = p } }
                }
            }
            HStack {
                TextField("value", text: $valueText).keyboardType(.numbersAndPunctuation).frame(width: 90)
                Spacer(); Button("Set override") { if let v = Double(valueText) { s.xfxAction("setOverride", ["note": key, "path": path, "value": v]) } }.buttonStyle(.borderedProminent)
            }
            if overrides.isEmpty { Text("None - this key follows the normal layer settings.").font(.caption).foregroundStyle(.secondary) }
            ForEach(overrides, id: \.0) { o in
                HStack { Text(o.0).font(.caption.monospaced()); Spacer(); Text(o.1).font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                    Button(role: .destructive) { s.xfxAction("clearOverride", ["note": key, "path": o.0]) } label: { Image(systemName: "trash") }.buttonStyle(.borderless) }
            }
        }
    }
}

private extension Array { subscript(safe i: Int) -> Element? { indices.contains(i) ? self[i] : nil } }
