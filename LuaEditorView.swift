// LUA oscillator layer editor (web: buildLuaOscUI). Four script slots; each slot's source, seed and parameter values live in the preset
// document at layers.LUA.oscs.N.lua.*  and are pushed to the audio engine through PresetStore.set (the same path as every other control).
import SwiftUI

struct LuaEditorView: View {
    @ObservedObject var s: PresetStore
    @State private var slot = 0
    @State private var report: [String] = []
    @State private var info: [String: Any] = [:]
    @State private var draft = ""
    @State private var showLib = false
    @State private var stats: [String: Any] = [:]
    private let timer = Timer.publish(every: 0.5, on: .main, in: .common).autoconnect()
    private var base: String { "layers.LUA.oscs.\(slot)" }

    private func refresh() {
        draft = s.string("\(base).lua.src", "")
        info = s.luaInfo(slot)
    }
    private func apply(_ src: String) {
        let seed = Int(s.double("\(base).lua.seed", 1))
        let r = s.luaValidate(src, seed: seed)
        var lines: [String] = []
        if (r["ok"] as? Bool) == true {
            lines.append("ok: compiled – \(r["params"].flatMap { $0 as? [Any] }?.count ?? 0) parameter(s), setup \(r["setupInstr"] as? Int ?? 0) instructions")
            s.set("\(base).lua.src", src)
            s.set("\(base).enabled", 1)
        } else {
            let line = r["line"] as? Int ?? 0, col = r["col"] as? Int ?? 0
            lines.append("error: line \(line), col \(col): \(r["error"] as? String ?? "failed")  (the previous script keeps playing)")
        }
        report = lines
        info = s.luaInfo(slot)
    }

    var body: some View {
        Group {
            Section("LUA oscillator – slot \(slot + 1)") {
                Picker("Slot", selection: $slot) { ForEach(0..<4, id: \.self) { Text("LUA \($0 + 1)").tag($0) } }
                    .pickerStyle(.segmented).onChange(of: slot) { _ in refresh() }
                ToggleRow(s: s, label: "Enabled", path: "\(base).enabled")
                HStack {
                    Button("RUN") { apply(draft) }.buttonStyle(.borderedProminent)
                    Button("VALIDATE") {
                        let r = s.luaValidate(draft, seed: Int(s.double("\(base).lua.seed", 1)))
                        report = [(r["ok"] as? Bool) == true ? "ok: syntax OK, setup ran (\(r["setupInstr"] as? Int ?? 0) instructions)"
                                  : "error: line \(r["line"] as? Int ?? 0): \(r["error"] as? String ?? "failed")"]
                    }
                    Button("LIBRARY") { showLib = true }
                }.buttonStyle(.bordered)
                TextEditor(text: $draft)
                    .font(.system(size: 12, design: .monospaced)).frame(minHeight: 220)
                    .autocorrectionDisabled().textInputAutocapitalization(.never)
                ForEach(Array(report.enumerated()), id: \.offset) { _, l in
                    Text(l).font(.caption.monospaced()).foregroundColor(l.hasPrefix("error") ? .red : .green)
                }
                if let e = stats["error"] as? String, !e.isEmpty { Text("runtime: \(e)").font(.caption.monospaced()).foregroundColor(.orange) }
                Text("voices \(stats["voices"] as? Int ?? 0) · block \(stats["instr"] as? Int ?? 0)/15000 instr · faults \(stats["faults"] as? Int ?? 0)")
                    .font(.caption2.monospaced()).foregroundColor(.secondary)
                if let log = stats["log"] as? [String], !log.isEmpty { Text(log.suffix(4).joined(separator: "\n")).font(.caption2.monospaced()) }
            }
            if let name = info["name"] as? String, !name.isEmpty {
                Section("\(name)\((info["author"] as? String).map { $0.isEmpty ? "" : " – \($0)" } ?? "")") {
                    if let d = info["desc"] as? String, !d.isEmpty { Text(d).font(.caption) }
                    ForEach(Array((info["params"] as? [[String: Any]] ?? []).enumerated()), id: \.offset) { _, p in paramRow(p) }
                }
            }
            Section("Slot output") {
                SliderRow(s: s, label: "Level", path: "\(base).level", range: 0...1, def: 0.8)
                SliderRow(s: s, label: "Width", path: "\(base).width", range: 0...2, def: 1)
                ToggleRow(s: s, label: "Invert polarity", path: "\(base).polarity")
                ToggleRow(s: s, label: "Mono", path: "\(base).mono")
                SliderRow(s: s, label: "Velocity low", path: "\(base).velLow", range: 1...127, def: 1, fmt: "%.0f")
                SliderRow(s: s, label: "Velocity high", path: "\(base).velHigh", range: 1...127, def: 127, fmt: "%.0f")
                SliderRow(s: s, label: "Velocity curve", path: "\(base).velCurve", range: -1...1, def: 0)
                SliderRow(s: s, label: "Velocity sensitivity", path: "\(base).velSens", range: 0...1, def: 1)
                SliderRow(s: s, label: "Seed", path: "\(base).lua.seed", range: 1...9999, def: 1, fmt: "%.0f")
                ToggleRow(s: s, label: "Lock seed (same sound every note)", path: "\(base).lua.seedLock", def: true)
                OscCommonEditor(s: s, prefix: base)
            }
        }
        .onAppear { refresh() }
        .onReceive(timer) { _ in stats = s.luaStats(slot) }
        .sheet(isPresented: $showLib) {
            NavigationStack {
                List(s.luaFactory, id: \.id) { f in
                    Button(f.id.replacingOccurrences(of: "_", with: " ").capitalized) {
                        draft = f.src; showLib = false; apply(f.src)
                    }
                }
                .navigationTitle("Factory scripts")
                .toolbar { ToolbarItem(placement: .cancellationAction) { Button("Close") { showLib = false } } }
            }
        }
    }

    @ViewBuilder private func paramRow(_ p: [String: Any]) -> some View {
        let id = p["id"] as? String ?? "", label = p["label"] as? String ?? id
        let lo = (p["min"] as? Double) ?? 0, hi = max((p["max"] as? Double) ?? 1, lo + 1e-6), def = (p["def"] as? Double) ?? lo
        let path = "\(base).lua.params.\(id)"
        if let opts = p["options"] as? [String], !opts.isEmpty {
            Picker(label, selection: Binding(get: { Int(s.double(path, def)) }, set: { s.set(path, Double($0)) })) {
                ForEach(Array(opts.enumerated()), id: \.offset) { Text($1).tag($0) }
            }
        } else if (p["kind"] as? String) == "toggle" || (p["kind"] as? String) == "bool" {
            ToggleRow(s: s, label: label, path: path, def: def != 0)
        } else {
            SliderRow(s: s, label: label, path: path, range: lo...hi, def: def, fmt: hi - lo >= 20 ? "%.0f" : "%.2f")
        }
    }
}
