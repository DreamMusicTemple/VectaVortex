// MIXER (web #mixerPanel): per-track pan / MOD / fader (-60...+12 dB) / meter. SMP2-16 collapse behind EXPAND SAMPLES.
// Meters read vv_engine_mixer_level(). NOT COMPILED YET - see docs/MAC_DAY_ONE.md.
import SwiftUI

struct MixerView: View {
    @ObservedObject var s: PresetStore
    @State private var expanded = false
    @State private var levels = [Float](repeating: 0, count: 24)
    @State private var tick = 0
    private let timer = Timer.publish(every: 0.05, on: .main, in: .common).autoconnect()
    static let keys = ["WT1", "WT2", "FMA", "FMB", "FM6OP"] + (1...16).map { "SMP\($0)" } + ["PRC1", "PRC2", "SID"]
    static func label(_ k: String) -> String {
        switch k { case "WT1": return "WT-1"; case "WT2": return "WT-2"; case "FMA": return "FM-A"; case "FMB": return "FM-B"; case "FM6OP": return "FM-6OP"
        case "PRC1": return "PRC-1"; case "PRC2": return "PRC-2"; default: return k.hasPrefix("SMP") ? "SMP " + k.dropFirst(3) : k }
    }
    static func panPath(_ k: String) -> String {
        let n = Int(k.filter(\.isNumber)) ?? 1
        switch k { case "WT1", "WT2": return "layers.WT.oscs.\(n - 1).pan"; case "FMA": return "layers.FM.enginePan.0"; case "FMB": return "layers.FM.enginePan.1"
        case "FM6OP": return "layers.FM.dx7Pan"; case "PRC1", "PRC2": return "layers.PRO.oscs.\(n - 1).pan"; case "SID": return "layers.SID.pan"
        default: return "layers.SMP.oscs.\(n - 1).pan" }
    }
    var body: some View {
        ScrollView(.horizontal) {
            HStack(alignment: .top, spacing: 4) {
                ForEach(Array(Self.keys.enumerated()), id: \.offset) { i, k in
                    if k == "SMP1" {
                        Button(expanded ? "COLLAPSE\nSAMPLES" : "EXPAND\nSAMPLES") { expanded.toggle() }
                            .buttonStyle(.bordered).multilineTextAlignment(.center).frame(width: 70).frame(maxHeight: .infinity)
                    }
                    if !(k.hasPrefix("SMP") && k != "SMP1" && !expanded) { Strip(s: s, key: k, level: levels[i]) }
                }
            }.padding(8)
        }
        .onReceive(timer) { _ in tick += 1; if tick % max(1, BatterySaving.shared.uiDivider) == 0 { for i in 0..<24 { levels[i] = s.mixerLevel(i) } } }
        .navigationTitle("Mixer").navigationBarTitleDisplayMode(.inline).toolbar(.hidden, for: .navigationBar)
    }

    struct Strip: View {
        @ObservedObject var s: PresetStore
        let key: String
        let level: Float
        private var gainPath: String { "mixerGains.\(key)" }
        private var hasMod: Bool { (0..<s.count("connections")).contains { s.string("connections.\($0).dest") == "MIX:\(key)" } }
        private let sources = ["lfo1", "lfo2", "lfo3", "lfo4", "lfo5", "lfo6", "velocity", "modwheel", "random", "envWT", "envFM", "envSMP", "envFollow", "macro1", "macro2", "macro3", "macro4"]
        // Meter fractions on the fader scale (peak linear -> dB -> 0...1 over -60...+12). Peak falls 20 dB/s with a 1.5 s hold.
        static func frac(_ lin: Float) -> CGFloat { CGFloat(min(1, max(0, (20 * log10(Double(max(lin, 1e-6))) + 60) / 72))) }
        var peakFrac: CGFloat { Strip.frac(level) }
        var avgFrac: CGFloat { Strip.frac(level * 0.7) }   // the engine reports per-voice block peaks, so this is an averaged-level estimate, not true RMS
        var holdFrac: CGFloat { Strip.frac(level) }
        var body: some View {
            let db = s.double(gainPath, 0)
            VStack(spacing: 6) {
                Slider(value: Binding(get: { s.double(MixerView.panPath(key), 0) }, set: { s.set(MixerView.panPath(key), $0) }), in: -1...1).frame(width: 52)
                Menu {
                    ForEach(sources, id: \.self) { src in Button(src) { s.append("connections", ["source": src, "dest": "MIX:\(key)", "amount": 30]) } }
                    if hasMod { Button("Remove routes", role: .destructive) { for i in (0..<s.count("connections")).reversed() where s.string("connections.\(i).dest") == "MIX:\(key)" { s.remove("connections", at: i) } } }
                } label: { Text("MOD").font(.caption2).padding(4).background(hasMod ? vvCyan.opacity(0.4) : .gray.opacity(0.2)).cornerRadius(4) }
                // fader and meter share one row, one height and the -60...+12 dB scale; ticks are positioned from the same mapping
                HStack(spacing: 2) {
                GeometryReader { g in
                    let f = CGFloat(min(1, max(0, (db + 60) / 72)))
                    ZStack(alignment: .bottom) {
                        RoundedRectangle(cornerRadius: 3).fill(.black.opacity(0.6)).frame(width: 6)
                        RoundedRectangle(cornerRadius: 3).fill(vvCyan.opacity(0.6)).frame(width: 6, height: g.size.height * f)
                        RoundedRectangle(cornerRadius: 2).fill(.white).frame(width: 24, height: 8).offset(y: -(g.size.height * f) + 4)
                    }.frame(maxWidth: .infinity).contentShape(Rectangle())
                    .gesture(DragGesture(minimumDistance: 0).onChanged { v in
                        let t = 1 - min(1, max(0, v.location.y / g.size.height)); s.set(gainPath, ((-60 + Double(t) * 72) * 10).rounded() / 10)
                    })
                }.frame(height: 240)
                    MixerMeter(peak: peakFrac, avg: avgFrac, hold: holdFrac).frame(width: 30, height: 240)
                }
                Text(db <= -60 ? "-∞" : String(format: "%@%.1f", db > 0 ? "+" : "", db)).font(.system(size: 10, design: .monospaced)).foregroundStyle(.secondary)
                Text(MixerView.label(key)).font(.system(size: 10, design: .monospaced))
            }
            .padding(4).frame(width: 62).background(RoundedRectangle(cornerRadius: 5).fill(.white.opacity(0.05)))
        }
    }
}

/// Calibrated dB meter: two bars (peak left, average right) plus ticks 0 / -6 / -12 labelled and +6 / -3 / -18 / -24 / -36 / -inf subtle.
struct MixerMeter: View {
    let peak: CGFloat, avg: CGFloat, hold: CGFloat
    private static func frac(_ db: Double) -> CGFloat { CGFloat(min(1, max(0, (db + 60) / 72))) }
    private func colour(_ f: CGFloat) -> Color { let db = Double(f) * 72 - 60; return db > 0 ? .red : (db > -12 ? .orange : .green) }
    var body: some View {
        GeometryReader { g in
            let h = g.size.height
            ZStack(alignment: .bottomLeading) {
                Rectangle().fill(.black.opacity(0.6)).frame(width: 12).offset(x: 18)
                Rectangle().fill(colour(peak)).frame(width: 6, height: h * peak).offset(x: 18)
                Rectangle().fill(colour(avg).opacity(0.7)).frame(width: 6, height: h * avg).offset(x: 24)
                Rectangle().fill(.white).frame(width: 6, height: 1).offset(x: 18, y: -(h * hold))
                ForEach([(12.0, false), (6.0, false), (0.0, true), (-3.0, false), (-6.0, true), (-12.0, true), (-18.0, false), (-24.0, false), (-36.0, false), (-60.0, false)], id: \.0) { t in
                    let y = h * MixerMeter.frac(t.0)
                    Rectangle().fill(Color.gray.opacity(t.1 ? 0.9 : 0.4)).frame(width: 4, height: 1).offset(x: 13, y: -y)
                    if t.1 { Text(t.0 == 0 ? "0" : String(Int(t.0))).font(.system(size: 8, design: .monospaced)).foregroundStyle(.secondary).frame(width: 12, alignment: .trailing).offset(x: 0, y: -y + 5) }
                }
                Text("-∞").font(.system(size: 8, design: .monospaced)).foregroundStyle(.secondary).frame(width: 14, alignment: .trailing).offset(y: 5)
            }
        }
    }
}
