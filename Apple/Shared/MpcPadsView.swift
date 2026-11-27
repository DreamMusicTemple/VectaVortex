// MPC pads (web: MPC PADS): 16 pads. Sample mode: every pad plays one sampler slot once; bottom row can switch to WT / FM synth pads. LOCK freezes the assignments.
import SwiftUI

struct MpcPadsView: View {
    @ObservedObject var s: PresetStore
    @Environment(\.dismiss) private var dismiss
    @AppStorage("vv.mpc.synth") private var synthMode = false
    @AppStorage("vv.mpc.locked") private var locked = false
    @AppStorage("vv.mpc.assign") private var assignRaw = (0..<16).map(String.init).joined(separator: ",")
    @State private var down = Set<Int>()
    @State private var lit = Set<Int>()   // highlight outlives a quick tap by ~110 ms so every hit is visible

    private var assign: [Int] { let a = assignRaw.split(separator: ",").compactMap { Int($0) }; return a.count == 16 ? a : Array(0..<16) }
    private func setAssign(_ pad: Int, _ slot: Int) { var a = assign; a[pad] = slot; assignRaw = a.map(String.init).joined(separator: ",") }
    private func isSynth(_ i: Int) -> Bool { i >= 12 && synthMode }
    private func slotName(_ slot: Int) -> String { let n = s.string("layers.SMP.oscs.\(slot).name"); return n.isEmpty ? "- empty -" : n }
    private func tint(_ i: Int) -> Color { !isSynth(i) ? Color(white: 0.2) : i < 14 ? Color(red: 0.29, green: 0.56, blue: 0.89) : i == 14 ? Color(red: 0.89, green: 0.44, blue: 0.29) : Color(red: 0.89, green: 0.31, blue: 0.29) }
    private static let synthNotes = [60, 72, 60, 67]

    private func press(_ i: Int) {
        if !isSynth(i) {
            // "Send Transients to MPC": extra.mpcPadSlice.<pad> = { osc, ti } -> this pad plays one transient of that slot
            if let ti = s.value("extra.mpcPadSlice.\(i).ti") as? Int, let osc = s.value("extra.mpcPadSlice.\(i).osc") as? Int, ti >= 0, ti < s.count("layers.SMP.oscs.\(osc).transients") { s.audioUnit.padSliceOn(slot: osc, slice: ti + 1, pad: i) }
            else { s.audioUnit.padOn(layer: 2, slot: assign[i], pad: i, pitchNote: 60) }
        }
        else { s.audioUnit.padOn(layer: i >= 14 ? 1 : 0, slot: -1, pad: i, pitchNote: Self.synthNotes[i - 12]) }
    }
    private func release(_ i: Int) { s.audioUnit.padOff(layer: isSynth(i) ? (i >= 14 ? 1 : 0) : 2, pad: i) }

    var body: some View {
        NavigationStack {
            VStack(spacing: 10) {
                HStack {
                    Button(synthMode ? "BOTTOM ROW: WT / FM" : "BOTTOM ROW: SAMPLES") { if !locked { synthMode.toggle() } }.disabled(locked)
                    Spacer()
                    Button(locked ? "UNLOCK" : "LOCK") { locked.toggle() }.tint(locked ? vvCyan : nil)
                }.buttonStyle(.bordered)
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 10), count: 4), spacing: 10) {
                    ForEach(0..<16, id: \.self) { i in
                        VStack(spacing: 4) {
                            ZStack {
                                RoundedRectangle(cornerRadius: 8).fill(lit.contains(i) ? vvCyan.opacity(0.42) : Color(white: 0.11)).shadow(color: lit.contains(i) ? vvCyan.opacity(0.9) : .clear, radius: 8)
                                RoundedRectangle(cornerRadius: 8).stroke(tint(i), lineWidth: 3)
                                VStack(spacing: 2) {
                                    Text("PAD \(i + 1)").font(.system(size: 9, design: .monospaced)).foregroundStyle(.secondary)
                                    if isSynth(i) { Text(i < 14 ? "WAVETABLE" : i == 14 ? "FM A" : "FM B").font(.caption.monospaced()); Text(i == 12 ? "root" : i == 13 ? "+octave" : i == 14 ? "root" : "+5th").font(.system(size: 10)).foregroundStyle(tint(i)) }
                                    else { Text(slotName(assign[i])).font(.caption.monospaced()).lineLimit(2).multilineTextAlignment(.center) }
                                }.padding(4)
                            }
                            .frame(height: 96)
                            .gesture(DragGesture(minimumDistance: 0).onChanged { _ in if !down.contains(i) { var tx = Transaction(); tx.disablesAnimations = true; withTransaction(tx) { lit.insert(i) }; down.insert(i); press(i) } }.onEnded { _ in down.remove(i); release(i); DispatchQueue.main.asyncAfter(deadline: .now() + 0.11) { if !down.contains(i) { var tx = Transaction(); tx.disablesAnimations = true; withTransaction(tx) { lit.remove(i) } } } })
                            if !isSynth(i) {
                                Picker("", selection: Binding(get: { assign[i] }, set: { setAssign(i, $0) })) { ForEach(0..<16, id: \.self) { Text("\($0 + 1): \(slotName($0))").tag($0) } }
                                    .pickerStyle(.menu).labelsHidden().disabled(locked).font(.caption2)
                            }
                        }
                    }
                }
                Spacer()
            }.padding()
            .navigationTitle("MPC PADS")
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Close") { dismiss() } } }
        }
    }
}
