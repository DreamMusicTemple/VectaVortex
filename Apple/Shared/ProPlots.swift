// "Pro" filter-response and envelope plots: the pitch-envelope editor's look (dark inset plot, mono 9pt axis labels, major/minor grid,
// glowing accent line over a soft gradient fill). Same maths as Core/include/vv/viz.hpp (which the C++ r22 test covers). Untested (no Swift build here).
import SwiftUI

enum VizMath {
    static func hzAt(_ x: Double) -> Double { 20 * pow(1000, x) }
    static func xOfHz(_ f: Double) -> Double { log(f / 20) / log(1000) }
    static func bump(_ ratio: Double, _ reso: Double, _ k: Double, _ w: Double = 4) -> Double { let l = log2(ratio); return exp(-l * l * w) * (reso / 130) * k }
    static func filterDb(_ type: String, _ cutoff: Double, _ reso: Double, _ freq: Double) -> Double {
        let ratio = freq / cutoff, safe = max(0.001, ratio); var m: Double
        switch type {
        case "onepole6db": m = 1 / sqrt(1 + ratio * ratio)
        case "twopole12db", "statevariable": m = 1 / sqrt(1 + pow(ratio, 4)); m *= 1 + bump(ratio, reso, 3.5)
        case "comb": m = 0.6 + 0.4 * sin(ratio * .pi * 6) * min(1, reso / 40)
        case "highpass": m = 1 / sqrt(1 + pow(1 / safe, 4)); m *= 1 + bump(ratio, reso, 3.5)
        case "notch": let q = 2 + (reso / 130) * 10; let bp = 1 / sqrt(1 + pow(q * (ratio - 1 / safe), 2)); m = sqrt(max(0.0001, 1 - bp * bp))
        case "isem": m = 1 / sqrt(1 + pow(ratio, 4)); m *= 1 + bump(ratio, reso, 2.5)
        case "bandpass": let q = 2 + (reso / 130) * 10; m = 1 / sqrt(1 + pow(q * (ratio - 1 / safe), 2))
        case "formant":
            let q = 4 + (reso / 130) * 14; var sum = 0.0
            for (i, r) in [1.0, 1.74, 3.71].enumerated() { let rr = freq / (cutoff * r); sum += (1 / sqrt(1 + pow(q * (rr - 1 / max(0.001, rr)), 2))) * (i == 0 ? 1 : 0.7) }
            m = min(1, sum * 0.6)
        case "sid8580": m = 1 / sqrt(1 + pow(ratio, 4)); m *= 1 + bump(ratio, reso, 4)
        case "sid6581": m = 1 / sqrt(1 + pow(ratio, 4)); m *= 1 + bump(ratio, reso, 5.5); if ratio < 0.4 { m *= 1 - (reso / 130) * 0.3 }
        default: m = 1 / sqrt(1 + pow(ratio, 8)); m *= 1 + bump(ratio, reso, 5, 3)
        }
        return 20 * log10(max(0.001, m))
    }
    struct Env { let a, d, s, r, hold, total: Double
        init(a: Double, d: Double, s: Double, r: Double) { self.a = max(0.001, a); self.d = max(0.001, d); self.s = min(1, max(0, s)); self.r = max(0.001, r); hold = max(0.1, 0.35 * (self.a + self.d)); total = self.a + self.d + hold + self.r }
        func level(_ t: Double) -> Double { if t < a { return t / a }; if t < a + d { return 1 - (1 - s) * ((t - a) / d) }; if t < a + d + hold { return s }; return s * max(0, 1 - (t - a - d - hold) / r) }
    }
    static func gridStep(_ total: Double) -> Double { for s in [0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5] where total / s <= 8 { return s }; return 5 }
    static func hzLabel(_ f: Double) -> String { f >= 1000 ? String(format: "%.1fk", (f / 100).rounded() / 10) : String(Int(f.rounded())) }
    static func secs(_ t: Double) -> String { t < 1 ? String(format: "%.2fs", t) : String(format: t < 10 ? "%.1fs" : "%.0fs", t) }
}

private let proL: CGFloat = 34, proR: CGFloat = 10, proT: CGFloat = 10, proB: CGFloat = 20
private let proGrey = Color(red: 0.59, green: 0.61, blue: 0.67)

private func proPlot(_ size: CGSize) -> CGRect { CGRect(x: proL, y: proT, width: max(10, size.width - proL - proR), height: max(10, size.height - proT - proB)) }
private func proHLine(_ ctx: GraphicsContext, _ r: CGRect, _ y: CGFloat, major: Bool, label: String?) {
    var p = Path(); p.move(to: CGPoint(x: r.minX, y: y)); p.addLine(to: CGPoint(x: r.maxX, y: y)); ctx.stroke(p, with: .color(Color(red: 0.5, green: 0.53, blue: 0.59).opacity(major ? 0.5 : 0.22)), lineWidth: 1)
    if let l = label { ctx.draw(Text(l).font(.system(size: 9, design: .monospaced)).foregroundColor(proGrey.opacity(0.75)), at: CGPoint(x: r.minX - 4, y: y), anchor: .trailing) }
}
private func proVLine(_ ctx: GraphicsContext, _ r: CGRect, _ x: CGFloat, major: Bool, label: String?) {
    var p = Path(); p.move(to: CGPoint(x: x, y: r.minY)); p.addLine(to: CGPoint(x: x, y: r.maxY)); ctx.stroke(p, with: .color(Color(red: 0.5, green: 0.53, blue: 0.59).opacity(major ? 0.32 : 0.13)), lineWidth: 1)
    if let l = label, major || r.width > 340 { ctx.draw(Text(l).font(.system(size: 9, design: .monospaced)).foregroundColor(proGrey.opacity(0.75)), at: CGPoint(x: x, y: r.maxY + 4), anchor: .top) }
}
/// Soft gradient fill + glowing accent line + thin top highlight (vvProCurve).
private func proCurve(_ ctx: GraphicsContext, _ r: CGRect, _ fn: (Double) -> Double) {
    let n = max(64, Int(r.width)); var line = Path()
    for i in 0...n { let x = Double(i) / Double(n), y = min(1.15, max(-0.15, fn(x))); let pt = CGPoint(x: r.minX + CGFloat(x) * r.width, y: r.maxY - CGFloat(y) * r.height); if i == 0 { line.move(to: pt) } else { line.addLine(to: pt) } }
    var c = ctx; c.clip(to: Path(CGRect(x: r.minX, y: r.minY - 4, width: r.width, height: r.height + 8)))
    var fill = line; fill.addLine(to: CGPoint(x: r.maxX, y: r.maxY)); fill.addLine(to: CGPoint(x: r.minX, y: r.maxY)); fill.closeSubpath()
    c.fill(fill, with: .linearGradient(Gradient(colors: [vvCyan.opacity(0.33), vvCyan.opacity(0.02)]), startPoint: CGPoint(x: 0, y: r.minY), endPoint: CGPoint(x: 0, y: r.maxY)))
    c.stroke(line, with: .color(vvCyan.opacity(0.28)), style: StrokeStyle(lineWidth: 5, lineCap: .round, lineJoin: .round))
    c.stroke(line, with: .color(vvCyan), style: StrokeStyle(lineWidth: 2.4, lineCap: .round, lineJoin: .round))
    c.stroke(line.offsetBy(dx: 0, dy: -1), with: .color(.white.opacity(0.28)), lineWidth: 0.9)
}

/// Filter response, 20 Hz...20 kHz (log) by -36...+12 dB; drag sets cutoff (x) and resonance (y) like the web graph.
struct FilterVizView: View {
    @ObservedObject var s: PresetStore
    let prefix: String
    var body: some View {
        let type = s.string(prefix + ".type", "moog4pole"), fc = s.double(prefix + ".cutoff", 4000), res = s.double(prefix + ".reso", 20)
        Canvas { ctx, size in
            let r = proPlot(size); ctx.fill(Path(r), with: .color(.black.opacity(0.55)))
            for f in [20.0, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000] { proVLine(ctx, r, r.minX + CGFloat(VizMath.xOfHz(f)) * r.width, major: f == 100 || f == 1000 || f == 10000, label: VizMath.hzLabel(f)) }
            for db in [-36, -24, -12, 0, 12] { proHLine(ctx, r, r.maxY - CGFloat(Double(db + 36) / 48) * r.height, major: db == 0, label: (db > 0 ? "+" : "") + String(db)) }
            proCurve(ctx, r) { (VizMath.filterDb(type, fc, res, VizMath.hzAt($0)) + 36) / 48 }
            let cx = r.minX + CGFloat(min(1, max(0, VizMath.xOfHz(fc)))) * r.width
            var d = Path(); d.move(to: CGPoint(x: cx, y: r.minY)); d.addLine(to: CGPoint(x: cx, y: r.maxY)); ctx.stroke(d, with: .color(Color(red: 1, green: 0.24, blue: 0.65)), style: StrokeStyle(lineWidth: 1.2, dash: [4, 3]))
            let pk = VizMath.filterDb(type, fc, res, fc), py = r.maxY - CGFloat(min(1, max(0, (pk + 36) / 48))) * r.height
            ctx.fill(Path(ellipseIn: CGRect(x: cx - 4, y: py - 4, width: 8, height: 8)), with: .color(Color(red: 0.29, green: 1, blue: 0.54)))
            ctx.draw(Text("fc \(VizMath.hzLabel(fc)) Hz  ·  res \(Int(res.rounded()))  ·  \(pk >= 0 ? "+" : "")\(String(format: "%.1f", pk)) dB").font(.system(size: 9, design: .monospaced)).foregroundColor(proGrey.opacity(0.85)), at: CGPoint(x: r.maxX - 4, y: r.minY + 3), anchor: .topTrailing)
        }
        .frame(height: 170)
        .overlay(GeometryReader { geo in Color.clear.contentShape(Rectangle()).gesture(DragGesture(minimumDistance: 0).onChanged { g in
            let r = proPlot(geo.size); let x = min(1, max(0, Double((g.location.x - r.minX) / r.width))), y = min(1, max(0, 1 - Double((g.location.y - r.minY) / r.height)))
            s.set(prefix + ".cutoff", VizMath.hzAt(x)); s.set(prefix + ".reso", y * 130) }) })
    }
}

/// Envelope plot: amplitude (0...100 %) when `amountPath` is nil, otherwise a signed-Hz filter envelope scaled by that amount.
struct EnvVizView: View {
    @ObservedObject var s: PresetStore
    let prefix: String; var amountPath: String? = nil
    var body: some View {
        let amp = amountPath == nil, A = amp ? 1.0 : s.double(amountPath!, 0)
        let e = VizMath.Env(a: s.double(prefix + ".a", 0.01), d: s.double(prefix + ".d", 0.2), s: s.double(prefix + ".s", amp ? 0.7 : 0.3), r: s.double(prefix + ".r", 0.3))
        Canvas { ctx, size in
            let r = proPlot(size); ctx.fill(Path(r), with: .color(.black.opacity(0.55)))
            let lo = amp ? 0 : min(0, A), hi = amp ? 1 : (A > 0 ? A : (A < 0 ? 0 : 1000)), span = (hi - lo) != 0 ? hi - lo : 1
            func fmtY(_ v: Double) -> String { amp ? "\(Int((v * 100).rounded()))%" : (v > 0 ? "+" : "") + (v == 0 ? "0" : abs(v) >= 1000 ? String(format: "%.1fk", v / 1000) : String(Int(v.rounded()))) }
            let step = VizMath.gridStep(e.total); var t = 0.0
            while t <= e.total + 1e-9 { let k = Int((t / step * 2).rounded()); proVLine(ctx, r, r.minX + CGFloat(t / e.total) * r.width, major: k % 2 == 0, label: k % 2 == 0 ? VizMath.secs(t) : nil); t += step / 2 }
            for f in [0.0, 0.25, 0.5, 0.75, 1] { proHLine(ctx, r, r.maxY - CGFloat(f) * r.height, major: f == 0 || f == 0.5 || f == 1, label: fmtY(lo + f * span)) }
            proCurve(ctx, r) { u in let l = e.level(u * e.total); return ((amp ? l : A * l) - lo) / span }
            let edges = [0, e.a, e.a + e.d, e.a + e.d + e.hold, e.total], names = ["A", "D", "S", "R"]
            for i in 1...3 { let x = r.minX + CGFloat(edges[i] / e.total) * r.width; var p = Path(); p.move(to: CGPoint(x: x, y: r.minY)); p.addLine(to: CGPoint(x: x, y: r.maxY)); ctx.stroke(p, with: .color(proGrey.opacity(0.38)), style: StrokeStyle(lineWidth: 1, dash: [3, 3])) }
            for i in 0..<4 { let x0 = r.minX + CGFloat(edges[i] / e.total) * r.width, x1 = r.minX + CGFloat(edges[i + 1] / e.total) * r.width; if x1 - x0 > 16 { ctx.draw(Text(names[i]).font(.system(size: 9, design: .monospaced)).foregroundColor(proGrey.opacity(0.85)), at: CGPoint(x: (x0 + x1) / 2, y: r.minY + 17), anchor: .top) } }
            ctx.draw(Text((amp ? "" : "Hz · ") + "A \(VizMath.secs(e.a))  D \(VizMath.secs(e.d))  S \(Int((e.s * 100).rounded()))%  R \(VizMath.secs(e.r))").font(.system(size: 9, design: .monospaced)).foregroundColor(proGrey.opacity(0.7)), at: CGPoint(x: r.maxX - 4, y: r.minY + 3), anchor: .topTrailing)
        }
        .frame(height: amp ? 120 : 140)
    }
}
