// Round 11: per-oscillator PITCH ENVELOPE editor (WT OSC1/OSC2, FM engine A/B, Procedural A/B) and the display-rate LFO preview.
// Both use the shared CurveCanvas (EQ-style direct manipulation, STRAIGHT / BEZIER) and the same evaluator as the audio engine.
// Preset fields (per oscillator): pitchEnv { on, amount (semitones, +-48), time (s), bipolar, retrig, pts [{x,y,cx,cy,lin}] }.
// NOTE: written without a Mac. Not compiled yet.
import SwiftUI

struct PitchEnvEditor: View {
    @ObservedObject var s: PresetStore
    let prefix: String            // e.g. "layers.WT.oscs.0" - the pitchEnv object lives under prefix + ".pitchEnv"
    let title: String
    @State private var pts: [CurvePt] = []
    @State private var born: Date = .distantPast
    private var p: String { prefix + ".pitchEnv" }
    private var bip: Bool { s.bool(p + ".bipolar", true) }
    private var amount: Double { s.double(p + ".amount", 12) }
    static var defaultPts: [CurvePt] { var q = [CurvePt(x: 0, y: 1), CurvePt(x: 0.22, y: 0.66), CurvePt(x: 1, y: 0.5)]; Curve.ensure(&q); return q }

    /// Semitone <-> curve-y: bipolar 0.5 = no shift, 1 = +amount, 0 = -amount; unipolar 0..1 = 0..+amount.
    private func semis(_ y: Double) -> Double { bip ? (y - 0.5) * 2 * amount : y * amount }
    private func yFor(semis st: Double) -> Double { let a = max(1e-6, abs(amount)) * (amount < 0 ? -1 : 1); return bip ? 0.5 + st / (2 * a) : st / a }

    var body: some View {
        DisclosureGroup(title) {
            ToggleRow(s: s, label: "Pitch envelope on", path: p + ".on")
            SliderRow(s: s, label: "Amount", path: p + ".amount", range: -48...48, def: 12, fmt: "%.1f", unit: " st")
            SliderRow(s: s, label: "Time", path: p + ".time", range: 0.01...10, def: 0.35, fmt: "%.2f", log: true, unit: " s")
            ToggleRow(s: s, label: "Bipolar (centre = no shift)", path: p + ".bipolar", def: true)
            ToggleRow(s: s, label: "Retrigger on legato (off = join at resting pitch)", path: p + ".retrig", def: true)
            CurveCanvas(pts: $pts, onChange: { Curve.write(s, p + ".pts", pts) },
                        yGrid: grid, snapY: { y in yFor(semis: semis(y).rounded()) }, playhead: nil)
                .frame(height: 170)
            Text(String(format: "Start %+.1f st  →  end %+.1f st over %.2f s", semis(pts.first?.y ?? 0), semis(pts.last?.y ?? 0), s.double(p + ".time", 0.35))).font(.caption2.monospacedDigit()).foregroundStyle(.secondary)
            Button("RESET") { pts = Self.defaultPts; Curve.write(s, p + ".pts", pts) }.buttonStyle(.bordered).controlSize(.small)
        }
        .onAppear { pts = Curve.read(s, p + ".pts"); if pts.count < 2 { pts = Self.defaultPts }; Curve.ensure(&pts) }
    }
    private var grid: [(y: Double, label: String)] {
        let a = abs(amount); if a < 0.5 { return [] }
        let step = a > 24 ? 12.0 : (a > 8 ? 6.0 : (a > 3 ? 2.0 : 1.0)); var out: [(Double, String)] = []
        var st = bip ? -a : 0.0; while st <= a + 1e-6 { out.append((yFor(semis: st * (amount < 0 ? -1 : 1)), String(format: "%+.0f", st))); st += step }
        return out.map { (y: $0.0, label: $0.1) }
    }
}

/// LFO shape preview drawn at display rate (TimelineView(.animation) = CADisplayLink: 60 Hz, 120 Hz on ProMotion). It reads the engine's lock-free phase
/// snapshot (an atomic float written by the control tick) and extrapolates with the snapshot rate between ticks, so the playhead moves every frame
/// without any lock, allocation or UI work on the audio thread.
struct LfoPreview: View {
    @ObservedObject var s: PresetStore
    let index: Int                // 0...5
    @State private var lastPhase: Double = 0
    @State private var lastSeen: TimeInterval = 0
    @State private var shown: Double = 0
    var body: some View {
        TimelineView(.animation(minimumInterval: BatterySaving.shared.frameInterval())) { tl in
            Canvas { ctx, size in
                let now = tl.date.timeIntervalSinceReferenceDate
                var ph = Double(s.lfoPhase(index)); let hz = Double(s.lfoRateHz(index))
                if ph != lastPhase { DispatchQueue.main.async { lastPhase = ph; lastSeen = now } } else if lastSeen > 0 { ph = (ph + hz * min(0.25, now - lastSeen)).truncatingRemainder(dividingBy: 1) }
                let pts = Curve.read(s, "lfo.lfo\(index + 1).shape")
                var path = Path()
                for x in 0..<Int(size.width) {
                    let u = Double(x) / Double(size.width); let v = pts.count >= 2 ? Curve.eval(pts, u) : 0.5 + 0.5 * sin(2 * .pi * u)
                    let q = CGPoint(x: CGFloat(x), y: size.height * (1 - CGFloat(v)) * 0.9 + size.height * 0.05); if x == 0 { path.move(to: q) } else { path.addLine(to: q) }
                }
                ctx.stroke(path, with: .color(Color(red: 0.61, green: 0.42, blue: 1)), lineWidth: 2)
                var l = Path(); l.move(to: CGPoint(x: size.width * CGFloat(ph), y: 0)); l.addLine(to: CGPoint(x: size.width * CGFloat(ph), y: size.height)); ctx.stroke(l, with: .color(.green), lineWidth: 1.5)
            }
        }
        .frame(height: 70).background(Color.black).clipShape(RoundedRectangle(cornerRadius: 6))
    }
}
