import SwiftUI

/// Piano-roll RECORD (web: the roll's REC button). The engine captures the user's keyboard / MIDI notes (pitch + velocity), pitch bend and mod wheel on the audio thread into a
/// lock-free ring (Core roll_rec.hpp); this object drains it ~20 Hz on the main thread and writes finished notes and the bend / mod lanes (roll.lanes) into the pattern's roll.
/// Overdub adds to the roll, Replace clears it when recording starts. Starts the transport when it is stopped. Output latency is already removed by the engine.
@MainActor final class RollRecorder: ObservableObject {
    @Published var armed = false
    @Published var replace = false
    @Published var quantise = false
    @Published var gridBeats = 0.25
    @Published var status = ""
    private var timer: Timer?
    private var buf = [CChar](repeating: 0, count: 65536)
    private weak var store: PresetStore?
    private var pPath = ""

    func toggle(store: PresetStore, patternPath: String) { if armed { stop() } else { start(store: store, patternPath: patternPath) } }

    func start(store: PresetStore, patternPath: String) {
        guard let e = store.audioUnit.enginePointer else { status = "The audio engine is not ready yet."; return }
        self.store = store; pPath = patternPath; ensureRoll()
        if replace { store.set(pPath + ".roll.notes", [Any]()); store.set(pPath + ".roll.lanes", RollRecorder.emptyLanes()) }
        vv_engine_roll_rec_set(e, 1)
        if vv_engine_seq_running(e) == 0 { vv_engine_start_sequencer(e, 1) }
        armed = true; status = "Recording: play the keyboard or MIDI."
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 20.0, repeats: true) { [weak self] _ in Task { @MainActor in self?.drain(final: false) } }
    }

    func stop() {
        guard armed, let e = store?.audioUnit.enginePointer else { armed = false; timer?.invalidate(); timer = nil; return }
        vv_engine_roll_rec_set(e, 0); drain(final: true); timer?.invalidate(); timer = nil; armed = false
    }

    static func emptyLanes() -> [String: Any] { ["bend": [Any](), "mod": [Any]()] }

    /// Web rollOf(): a step pattern turns into roll notes the first time it is recorded into (each "on" step becomes a 1/16 note at 60 + pitch).
    private func ensureRoll() {
        guard let store else { return }
        if store.value(pPath + ".roll") is [String: Any] { if !(store.value(pPath + ".roll.lanes") is [String: Any]) { store.set(pPath + ".roll.lanes", RollRecorder.emptyLanes()) }; return }
        var notes = [[String: Any]](); let len = max(1, store.int(pPath + ".length", 16))
        for (i, st) in store.array(pPath + ".steps").enumerated() where i < len {
            guard let d = st as? [String: Any], (d["on"] as? NSNumber)?.boolValue == true else { continue }
            notes.append(["t": Double(i) * 0.25, "l": 0.25, "p": 60 + ((d["pitch"] as? NSNumber)?.intValue ?? 0), "v": 0.9, "prob": (d["prob"] as? NSNumber)?.doubleValue ?? 100,
                          "rat": max(1, (d["ratchet"] as? NSNumber)?.intValue ?? 1), "pl": (d["plocks"] as? [String: Any]) ?? [String: Any](), "tr": (d["transientIdx"] as? NSNumber)?.intValue ?? 0])
        }
        let bars = notes.isEmpty ? 4 : max(1, Int((Double(len) / 16).rounded(.up)))
        store.set(pPath + ".roll", ["bars": bars, "loopOn": true, "loopStart": 0, "loopEnd": bars, "notes": notes, "lanes": RollRecorder.emptyLanes()] as [String: Any])
    }

    private func drain(final: Bool) {
        guard let store, let e = store.audioUnit.enginePointer else { return }
        let bars = max(1, store.int(pPath + ".roll.bars", 4)), lo = store.bool(pPath + ".roll.loopOn", true)
        let lStart = store.double(pPath + ".roll.loopStart", 0) * 4, lEnd = store.double(pPath + ".roll.loopEnd", Double(bars)) * 4
        let ls = lo ? lStart : 0, le = lo ? max(ls + 4, lEnd) : Double(bars) * 4
        let g = quantise ? gridBeats : 0, fin: Int32 = final ? 1 : 0
        let n = buf.withUnsafeMutableBufferPointer { vv_engine_roll_rec_drain_json(e, ls, le, Double(bars) * 4, g, fin, $0.baseAddress, $0.count) }
        guard n > 0 else { return }
        let data = buf.withUnsafeBytes { Data(bytes: $0.baseAddress!, count: Int(n)) }
        guard let j = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return }
        let fresh = (j["notes"] as? [[String: Any]]) ?? []
        if !fresh.isEmpty {
            var notes = store.array(pPath + ".roll.notes")
            for q in fresh { notes.append(["t": q["t"] ?? 0, "l": q["l"] ?? 0.25, "p": q["p"] ?? 60, "v": q["v"] ?? 0.9, "prob": 100, "rat": 1, "pl": [String: Any](), "tr": 0] as [String: Any]) }
            store.set(pPath + ".roll.notes", notes)
        }
        merge("bend", j["bend"], j["bendCut"]); merge("mod", j["mod"], j["modCut"])
        if final || !fresh.isEmpty { status = final ? "Recorded \((j["count"] as? NSNumber)?.intValue ?? 0) notes. Bend / mod lanes replay with the roll." : status }
    }

    /// New lane points replace the old ones inside every [a, b] span they cover (overdub); the lane stays sorted by beat.
    private func merge(_ kind: String, _ pts: Any?, _ cuts: Any?) {
        guard let store else { return }
        let add = (pts as? [[Double]]) ?? [], cut = (cuts as? [[Double]]) ?? []; if add.isEmpty && cut.isEmpty { return }
        var keep = [[Double]]()
        for q in store.array(pPath + ".roll.lanes." + kind) {
            guard let a = q as? [Any], a.count >= 2, let t = (a[0] as? NSNumber)?.doubleValue, let v = (a[1] as? NSNumber)?.doubleValue else { continue }
            if cut.contains(where: { $0.count == 2 && t >= $0[0] - 1e-9 && t <= $0[1] + 1e-9 }) { continue }
            keep.append([t, v])
        }
        keep.append(contentsOf: add); keep.sort { $0[0] < $1[0] }
        store.set(pPath + ".roll.lanes." + kind, keep)
    }
}

/// REC row of the pattern editor (the roll's own record button).
struct RollRecBar: View {
    @ObservedObject var s: PresetStore
    let pPath: String
    @StateObject private var rec = RollRecorder()
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Button { rec.toggle(store: s, patternPath: pPath) } label: { Label(rec.armed ? "STOP" : "REC", systemImage: rec.armed ? "stop.circle.fill" : "record.circle") }
                    .buttonStyle(.borderedProminent).tint(rec.armed ? .red : .gray)
                Picker("", selection: $rec.replace) { Text("Overdub").tag(false); Text("Replace").tag(true) }.pickerStyle(.segmented).frame(maxWidth: 180).disabled(rec.armed)
                Toggle("REC Q", isOn: $rec.quantise).toggleStyle(.button)
                if rec.quantise { Picker("", selection: $rec.gridBeats) { Text("1/4").tag(1.0); Text("1/8").tag(0.5); Text("1/16").tag(0.25); Text("1/32").tag(0.125) }.pickerStyle(.menu) }
                Button("CLR BEND/MOD") { s.set(pPath + ".roll.lanes", RollRecorder.emptyLanes()) }.disabled(rec.armed)
            }.font(.caption2).controlSize(.small).buttonStyle(.bordered)
            Text(rec.status.isEmpty ? "REC writes the notes you play (pitch, velocity, length) and pitch-bend / mod-wheel moves into this pattern's piano roll. Starts the transport." : rec.status)
                .font(.caption2).foregroundStyle(.secondary)
        }
        .onDisappear { if rec.armed { rec.stop() } }
    }
}
