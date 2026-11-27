// Keyboard zones (web: ZONES): every oscillator of every layer with Hold, Range Low/High (LEARN LO / LEARN HI take the next note played), Glide and Key track.
// FM has one zone per engine (FM A = operators 1-4, FM B = 5-8): edits apply to all four operators, like the web.
import SwiftUI

struct ZonesView: View {
    @ObservedObject var s: PresetStore
    @Environment(\.dismiss) private var dismiss
    @State private var learn: (group: Int, high: Bool)?
    @State private var baseline = -1
    private let timer = Timer.publish(every: 0.1, on: .main, in: .common).autoconnect()

    struct ZoneGroup: Identifiable { let id: Int; let layer: String; let label: String; let first: Int; let count: Int; let color: Color }
    private var groups: [ZoneGroup] {
        var g: [ZoneGroup] = []; var i = 0
        func add(_ layer: String, _ label: String, _ first: Int, _ count: Int, _ c: Color) { g.append(ZoneGroup(id: i, layer: layer, label: label, first: first, count: count, color: c)); i += 1 }
        let wt = LayerID.color(0), fm = LayerID.color(1), smp = LayerID.color(2), pro = LayerID.color(3)
        for n in 0..<max(s.count("layers.WT.oscs"), 2) { add("WT", "WT \(n + 1)", n, 1, wt) }
        add("FM", "FM A", 0, 4, fm); add("FM", "FM B", 4, 4, fm)
        for n in 0..<max(s.count("layers.SMP.oscs"), 16) { add("SMP", "SMP \(n + 1)", n, 1, smp) }
        for n in 0..<max(s.count("layers.PRO.oscs"), 2) { add("PRO", "PRO \(n + 1)", n, 1, pro) }
        let lua = LayerID.color(5)
        for n in 0..<4 { add("LUA", "LUA \(n + 1)", n, 1, lua) }
        return g
    }
    private func path(_ g: ZoneGroup, _ k: Int = 0) -> String { "layers.\(g.layer).oscs.\(g.first + k)" }
    private func setAll(_ g: ZoneGroup, _ key: String, _ v: Any) { for k in 0..<g.count { s.set(path(g, k) + "." + key, v) } }

    var body: some View {
        NavigationStack {
            List(groups) { g in
                VStack(alignment: .leading, spacing: 6) {
                    HStack {
                        Text(g.label).font(.caption.monospaced().bold()).foregroundStyle(g.color).frame(width: 52, alignment: .leading)
                        Toggle("Hold", isOn: Binding(get: { s.bool(path(g) + ".hold") }, set: { setAll(g, "hold", $0) })).font(.caption).fixedSize()
                        Spacer()
                    }
                    HStack {
                        Stepper("Low \(Int(s.double(path(g) + ".rangeLow", 0)))", value: Binding(get: { Int(s.double(path(g) + ".rangeLow", 0)) }, set: { setAll(g, "rangeLow", $0) }), in: 0...127).font(.caption)
                        Button(learn?.group == g.id && learn?.high == false ? "PLAY…" : "LEARN LO") { arm(g.id, false) }.buttonStyle(.bordered).controlSize(.mini)
                    }
                    HStack {
                        Stepper("High \(Int(s.double(path(g) + ".rangeHigh", 127)))", value: Binding(get: { Int(s.double(path(g) + ".rangeHigh", 127)) }, set: { setAll(g, "rangeHigh", $0) }), in: 0...127).font(.caption)
                        Button(learn?.group == g.id && learn?.high == true ? "PLAY…" : "LEARN HI") { arm(g.id, true) }.buttonStyle(.bordered).controlSize(.mini)
                    }
                    GeometryReader { geo in
                        let lo = CGFloat(s.double(path(g) + ".rangeLow", 0)) / 127, hi = CGFloat(s.double(path(g) + ".rangeHigh", 127)) / 127
                        ZStack(alignment: .leading) { Rectangle().fill(Color.black); Rectangle().fill(g.color.opacity(0.75)).frame(width: max(1, (hi - lo) * geo.size.width)).offset(x: lo * geo.size.width) }
                    }.frame(height: 10)
                    SliderRowSet(s: s, g: g, path: path(g), setAll: { k, v in setAll(g, k, v) })
                }
            }
            .navigationTitle("Keyboard zones")
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
            .onReceive(timer) { _ in
                guard let l = learn else { return }
                let n = s.audioUnit.lastNoteOn
                if n >= 0 && n != baseline, let g = groups.first(where: { $0.id == l.group }) { setAll(g, l.high ? "rangeHigh" : "rangeLow", n); learn = nil }
            }
        }
    }
    private func arm(_ id: Int, _ high: Bool) { baseline = s.audioUnit.lastNoteOn; learn = (id, high) }
}

private struct SliderRowSet: View {
    @ObservedObject var s: PresetStore
    let g: ZonesView.ZoneGroup; let path: String; let setAll: (String, Double) -> Void
    var body: some View {
        VStack(spacing: 2) {
            HStack { Text("Glide").font(.caption2); Slider(value: Binding(get: { s.double(path + ".glideTime", 0) }, set: { setAll("glideTime", $0) }), in: 0...2); Text(String(format: "%.3fs", s.double(path + ".glideTime", 0))).font(.caption2.monospacedDigit()) }
            HStack { Text("Key track").font(.caption2); Slider(value: Binding(get: { s.double(path + ".pitchKeytrack", 100) }, set: { setAll("pitchKeytrack", $0.rounded()) }), in: 0...200); Text("\(Int(s.double(path + ".pitchKeytrack", 100)))%").font(.caption2.monospacedDigit()) }
        }
    }
}
