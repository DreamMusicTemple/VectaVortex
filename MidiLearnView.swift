// MIDI LEARN (web: four profiles of CC -> parameter mappings). The engine holds the active profile's map (vv_engine_cc_learn_*);
// the four profiles are kept in UserDefaults. Incoming CCs are only recorded on the audio thread: MidiLearnModel polls them from a
// normal timer: VectaAudioUnit.startPump calls vv_engine_poll_cc (~30 Hz) and posts .vvAutomationApplied; this model only watches for a finished learn.
import SwiftUI

struct LearnTarget: Identifiable, Hashable { let id: String; let label: String; let lo: Double; let hi: Double }

enum LearnTargets {
    static func layerTargets(_ layer: String, _ name: String) -> [LearnTarget] { [
        LearnTarget(id: "layers.\(layer).filter.cutoff", label: "\(name) filter cutoff", lo: 20, hi: 18000),
        LearnTarget(id: "layers.\(layer).filter.reso", label: "\(name) filter resonance", lo: 0, hi: 130),
        LearnTarget(id: "layers.\(layer).filter.keytrack", label: "\(name) filter keytrack", lo: 0, hi: 200),
        LearnTarget(id: "layers.\(layer).env.a", label: "\(name) attack", lo: 0.001, hi: 3),
        LearnTarget(id: "layers.\(layer).env.d", label: "\(name) decay", lo: 0.001, hi: 3),
        LearnTarget(id: "layers.\(layer).env.s", label: "\(name) sustain", lo: 0, hi: 1),
        LearnTarget(id: "layers.\(layer).env.r", label: "\(name) release", lo: 0.001, hi: 3),
    ] }
    static let all: [LearnTarget] =
        layerTargets("WT", "Wavetable") + layerTargets("FM", "FM") + layerTargets("SMP", "Sampler")
        + (1...4).map { LearnTarget(id: "lfo.lfo\($0).hz", label: "LFO \($0) rate", lo: 0.1, hi: 20) }
        + [LearnTarget(id: "wtFmMix", label: "WT / FM mix", lo: 0, hi: 1), LearnTarget(id: "masterTone", label: "Master tone", lo: 0, hi: 1)]
    static let ccTable = """
    CC 1 Mod wheel | CC 7 Master volume | CC 74 / 71 Wavetable filter cutoff / resonance | CC 20 / 21 FM filter cutoff / resonance | CC 22 / 23 Sampler filter cutoff / resonance
    CC 24 / 25 / 26 filter keytrack (WT / FM / SMP) | CC 73 / 75 / 79 / 72 Wavetable A / D / S / R | CC 28 / 29 / 30 / 31 FM A / D / S / R
    CC 33 / 34 / 35 / 36 Sampler A / D / S / R | CC 102-105 Macros 1-4 | CC 106 / 107 / 108 / 112 / 113 / 114 LFO 1-6 rate | CC 109 / 110 / 111 Reverb / Delay / Saturation mix
    CC 115 / 116 / 117 Procedural filter cutoff / resonance / keytrack | CC 118 / 119 / 85 / 86 Procedural A / D / S / R | CC 87 Master tone | CC 88 WT / FM mix | Pitch bend, Program change, MPE
    """
}

final class MidiLearnModel: ObservableObject {
    let s: PresetStore
    @Published var status = ""
    @Published var learning = false
    @Published var mappings: [(cc: String, path: String)] = []
    @Published var profile: Int { didSet { UserDefaults.standard.set(profile, forKey: "vv.midiProfile"); applyActive() } }
    @Published var names: [String]
    private var maps: [String]
    private var timer: Timer?

    init(s: PresetStore) {
        self.s = s
        let d = UserDefaults.standard
        var n = d.stringArray(forKey: "vv.midiNames") ?? []
        var m = d.stringArray(forKey: "vv.midiMaps") ?? []
        while n.count < 4 { n.append("Profile \(n.count + 1)") }
        while m.count < 4 { m.append("{}") }
        self.names = Array(n.prefix(4))
        self.maps = Array(m.prefix(4))
        self.profile = min(3, max(0, d.integer(forKey: "vv.midiProfile")))
    }
    func start() {
        applyActive(); stop()
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in self?.tick() }
    }
    func stop() { timer?.invalidate(); timer = nil }

    private var engine: OpaquePointer? { s.audioUnit.enginePointer }

    private func tick() {
        guard let e = engine else { return }
        if learning { let c = vv_engine_cc_learned(e); if c >= 0 { learning = false; saveActive(); status = "Mapped CC \(c)." } }
    }
    func arm(_ t: LearnTarget) {
        guard let e = engine else { return }
        vv_engine_cc_learn_arm(e, t.id, t.lo, t.hi); learning = true
        status = "Move a hardware control now to map \(t.label)."
    }
    func cancel() { if let e = engine { vv_engine_cc_learn_cancel(e) }; learning = false; status = "" }
    func clearProfile() { if let e = engine { vv_engine_cc_learn_clear(e) }; saveActive(); status = "Profile mappings cleared." }
    func rename(_ n: String) { names[profile] = n; UserDefaults.standard.set(names, forKey: "vv.midiNames") }

    func applyActive() {
        guard let e = engine else { return }
        _ = maps[profile].withCString { vv_engine_cc_learn_set_json(e, $0) }
        refreshList()
    }
    private func saveActive() {
        guard let e = engine else { return }
        let n = vv_engine_cc_learn_json(e, nil, 0)
        if n > 0 { var buf = [CChar](repeating: 0, count: n + 1); _ = vv_engine_cc_learn_json(e, &buf, n + 1); maps[profile] = String(cString: buf) } else { maps[profile] = "{}" }
        UserDefaults.standard.set(maps, forKey: "vv.midiMaps"); refreshList()
    }
    private func refreshList() {
        var out: [(cc: String, path: String)] = []
        if let data = maps[profile].data(using: .utf8), let o = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
            for (k, v) in o { if let d = v as? [String: Any], let p = d["path"] as? String { out.append((cc: k, path: p)) } }
        }
        out.sort { (Int($0.cc) ?? 0) < (Int($1.cc) ?? 0) }
        mappings = out
    }
}

struct MidiLearnSection: View {
    @StateObject private var m: MidiLearnModel
    @State private var target = LearnTargets.all[0]
    @State private var name = ""
    init(s: PresetStore) { _m = StateObject(wrappedValue: MidiLearnModel(s: s)) }
    var body: some View {
        Section("MIDI learn") {
            Picker("Profile", selection: $m.profile) { ForEach(0..<4, id: \.self) { Text(m.names[$0]).tag($0) } }
            TextField("Profile name", text: Binding(get: { m.names[m.profile] }, set: { m.rename($0) }))
            Picker("Parameter", selection: $target) { ForEach(LearnTargets.all) { Text($0.label).tag($0) } }
            if m.learning { Button("Cancel learn", role: .cancel) { m.cancel() } }
            else { Button("Learn: map next CC to this parameter") { m.arm(target) } }
            if !m.status.isEmpty { Text(m.status).font(.caption).foregroundStyle(vvCyan) }
            ForEach(m.mappings, id: \.cc) { Text("CC \($0.cc)  ->  \($0.path)").font(.system(.caption, design: .monospaced)) }
            Button("Clear profile mappings", role: .destructive) { m.clearProfile() }
            Text(LearnTargets.ccTable).font(.system(size: 10, design: .monospaced)).foregroundStyle(.secondary)
        }
        .onAppear { m.start() }
        .onDisappear { m.stop() }
    }
}
