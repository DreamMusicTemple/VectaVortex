// Draggable-band EQ graph (replaces the eight EQ sliders, as in Vektur Vocal): LOW shelf / BELL / BELL / HIGH shelf.
// X = frequency 40...16000 Hz (log), Y = gain +-15 dB in 0.5 steps. Bound to `<prefix>.band1...4.{freq,gain}`; prefix = "layers.WT.eq".
// Magnitude uses the same RBJ coefficients as the engine EQ (Core/include/vv/fx_basic.hpp). NOT COMPILED YET - see docs/MAC_DAY_ONE.md.
import SwiftUI

enum EQMath {
    static let defaultFreq: [Double] = [120, 600, 2500, 8000]
    static let lo = 40.0, hi = 16000.0
    private struct Coef { var b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0 }
    private static func norm(_ B0: Double, _ B1: Double, _ B2: Double, _ A0: Double, _ A1: Double, _ A2: Double) -> Coef { Coef(b0: B0 / A0, b1: B1 / A0, b2: B2 / A0, a1: A1 / A0, a2: A2 / A0) }
    private static func shelf(_ high: Bool, _ f: Double, _ g: Double, _ sr: Double) -> Coef {
        let A = pow(10, g / 40), w0 = 2 * Double.pi * f / sr, c = cos(w0), s = sin(w0)
        let alpha = s / 2 * sqrt((A + 1 / A) * (1 / 0.707 - 1) + 2), sq = sqrt(A)
        if !high {
            return norm(A * ((A + 1) - (A - 1) * c + 2 * sq * alpha), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - 2 * sq * alpha),
                        (A + 1) + (A - 1) * c + 2 * sq * alpha, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - 2 * sq * alpha)
        }
        return norm(A * ((A + 1) + (A - 1) * c + 2 * sq * alpha), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - 2 * sq * alpha),
                    (A + 1) - (A - 1) * c + 2 * sq * alpha, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - 2 * sq * alpha)
    }
    private static func peaking(_ f: Double, _ g: Double, _ sr: Double) -> Coef {
        let A = pow(10, g / 40), w0 = 2 * Double.pi * f / sr, c = cos(w0), alpha = sin(w0) / 2   // Q = 1
        return norm(1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A)
    }
    static func magnitudeDb(freq: [Double], gain: [Double], at hz: Double, sr: Double = 48000) -> Double {
        let cs = [shelf(false, freq[0], gain[0], sr), peaking(freq[1], gain[1], sr), peaking(freq[2], gain[2], sr), shelf(true, freq[3], gain[3], sr)]
        let w = 2 * Double.pi * hz / sr; var db = 0.0
        for q in cs {
            let nr = q.b0 + q.b1 * cos(w) + q.b2 * cos(2 * w), ni = -(q.b1 * sin(w) + q.b2 * sin(2 * w))
            let dr = 1 + q.a1 * cos(w) + q.a2 * cos(2 * w), di = -(q.a1 * sin(w) + q.a2 * sin(2 * w))
            db += 20 * log10(max(1e-9, (nr * nr + ni * ni).squareRoot() / max(1e-12, (dr * dr + di * di).squareRoot())))
        }
        return db
    }
}

struct EQGraphView: View {
    @ObservedObject var s: PresetStore
    let prefix: String
    /// false: `<prefix>.band1.freq` / `.gain` (layer EQ).  true: flat keys `<prefix>.f1` / `.g1` (the EQ effects-rack slot).
    var flat: Bool = false
    @State private var dragging: Int? = nil
    private func fKey(_ b: Int) -> String { flat ? "\(prefix).f\(b)" : "\(prefix).band\(b).freq" }
    private func gKey(_ b: Int) -> String { flat ? "\(prefix).g\(b)" : "\(prefix).band\(b).gain" }
    private let names = ["LOW", "BELL", "BELL", "HIGH"]
    private func freqs() -> [Double] { (1...4).map { min(EQMath.hi, max(EQMath.lo, s.double(fKey($0), EQMath.defaultFreq[$0 - 1]))) } }
    private func gains() -> [Double] { (1...4).map { min(15, max(-15, s.double(gKey($0), 0))) } }

    var body: some View {
        GeometryReader { geo in
            let r = CGRect(x: 30, y: 14, width: max(10, geo.size.width - 40), height: max(10, geo.size.height - 28))
            let f = freqs(), g = gains()
            let xOf = { (hz: Double) -> CGFloat in r.minX + CGFloat(log(hz / EQMath.lo) / log(EQMath.hi / EQMath.lo)) * r.width }
            let yOf = { (db: Double) -> CGFloat in r.midY - CGFloat(db / 15) * r.height / 2 }
            ZStack(alignment: .topLeading) {
                Canvas { ctx, _ in
                    ctx.fill(Path(roundedRect: r, cornerRadius: 4), with: .color(.black.opacity(0.6)))
                    for db in [-12.0, -6, 0, 6, 12] {
                        var p = Path(); p.move(to: CGPoint(x: r.minX, y: yOf(db))); p.addLine(to: CGPoint(x: r.maxX, y: yOf(db)))
                        ctx.stroke(p, with: .color(.white.opacity(db == 0 ? 0.25 : 0.1)), lineWidth: 1)
                        ctx.draw(Text("\(Int(db))").font(.system(size: 9, design: .monospaced)).foregroundColor(.gray), at: CGPoint(x: r.minX - 14, y: yOf(db)))
                    }
                    var curve = Path()
                    for i in 0...200 {
                        let hz = EQMath.lo * pow(EQMath.hi / EQMath.lo, Double(i) / 200)
                        let pt = CGPoint(x: xOf(hz), y: min(r.maxY, max(r.minY, yOf(EQMath.magnitudeDb(freq: f, gain: g, at: hz)))))
                        if i == 0 { curve.move(to: pt) } else { curve.addLine(to: pt) }
                    }
                    ctx.stroke(curve, with: .color(vvCyan), lineWidth: 2)
                    for b in 0..<4 {
                        let c = CGPoint(x: xOf(f[b]), y: yOf(g[b]))
                        ctx.fill(Path(ellipseIn: CGRect(x: c.x - 9, y: c.y - 9, width: 18, height: 18)), with: .color(dragging == b ? .orange : .white))
                        ctx.draw(Text(names[b]).font(.system(size: 9, design: .monospaced)).foregroundColor(.gray), at: CGPoint(x: c.x, y: c.y - 18))
                    }
                }
                Color.clear.contentShape(Rectangle()).gesture(DragGesture(minimumDistance: 0)
                    .onChanged { v in
                        if dragging == nil {   // pick the nearest band handle within 28 pt
                            var best = -1, bd = CGFloat(28 * 28)
                            for b in 0..<4 { let dx = xOf(f[b]) - v.startLocation.x, dy = yOf(g[b]) - v.startLocation.y; if dx * dx + dy * dy < bd { bd = dx * dx + dy * dy; best = b } }
                            if best >= 0 { dragging = best }
                        }
                        guard let b = dragging else { return }
                        let t = min(1, max(0, (v.location.x - r.minX) / r.width))
                        s.set(fKey(b + 1), (EQMath.lo * pow(EQMath.hi / EQMath.lo, Double(t))).rounded())
                        let db = Double((r.midY - v.location.y) / (r.height / 2)) * 15
                        s.set(gKey(b + 1), (min(15, max(-15, db)) * 2).rounded() / 2)   // 0.5 dB steps
                    }
                    .onEnded { _ in dragging = nil })
            }
        }
        .frame(height: 190)
    }
}
