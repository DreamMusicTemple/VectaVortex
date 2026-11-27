// Round 8: three extra dial (potentiometer) styles, ported from the web app's POT_DRAW. All are lit from the top: a highlight on the upper edge and a semi-transparent
// shadow, cast downwards, in the outline of the dial. 270 degree sweep like every other style (135 deg = minimum).
// NOTE: written without a Mac. Not compiled yet.
import SwiftUI

enum VVDialStyle: Int, CaseIterable {
    case interstellar, aqua, tech, holo
    var label: String { ["Future (Interstellar)", "Aqua (liquid)", "Scientific hardware", "Holo ring"][rawValue] }
    static var current: VVDialStyle { VVDialStyle(rawValue: UserDefaults.standard.integer(forKey: "vv.potStyle")) ?? .interstellar }
}

/// Face for the three new styles; `size` is the square edge, `norm` 0...1, `light` the accent colour.
struct VVStyledDial: View {
    var style: VVDialStyle
    var norm: Double
    var light: Color
    var size: CGFloat
    var dragging: Bool
    var body: some View {
        if style == .aqua && UserDefaults.standard.object(forKey: "vv.animations") as? Bool ?? true {
            TimelineView(.animation(minimumInterval: BatterySaving.shared.frameInterval())) { tl in canvas(t: tl.date.timeIntervalSinceReferenceDate) }
        } else { canvas(t: 0) }
    }
    private func canvas(t: Double) -> some View {
        Canvas { ctx, sz in
            let w = min(sz.width, sz.height)
            switch style {
            case .aqua: VVDialDraw.aqua(&ctx, w, norm, light, dragging, t)
            case .tech: VVDialDraw.tech(&ctx, w, norm, light)
            case .holo: VVDialDraw.holo(&ctx, w, norm, light)
            case .interstellar: break
            }
        }.frame(width: size, height: size)
    }
}

enum VVDialDraw {
    static let A0 = 0.75 * Double.pi, SW = 1.5 * Double.pi, TAU = 2 * Double.pi
    static func hash(_ n: Double) -> Double { let x = sin(n * 127.1 + 311.7) * 43758.5453; return x - x.rounded(.down) }
    static func circle(_ c: CGPoint, _ r: Double) -> Path { Path(ellipseIn: CGRect(x: c.x - r, y: c.y - r, width: r * 2, height: r * 2)) }
    static func pt(_ c: CGPoint, _ a: Double, _ r: Double) -> CGPoint { CGPoint(x: c.x + cos(a) * r, y: c.y + sin(a) * r) }
    /// the cast shadow: same outline, shifted down, semi-transparent
    static func shadowed(_ ctx: inout GraphicsContext, _ w: Double, _ alpha: Double, _ body: (inout GraphicsContext) -> Void) {
        ctx.drawLayer { l in l.addFilter(.shadow(color: .black.opacity(alpha), radius: w * 0.045, x: 0, y: w * 0.032)); body(&l) }
    }
    static func topLight(_ c: CGPoint, _ r: Double, _ a: Double) -> GraphicsContext.Shading {
        .linearGradient(Gradient(colors: [.white.opacity(a), .white.opacity(a * 0.15), .white.opacity(0)]), startPoint: CGPoint(x: c.x, y: c.y - r), endPoint: CGPoint(x: c.x, y: c.y + r * 0.6))
    }
    static func arc(_ c: CGPoint, _ r: Double, _ a0: Double, _ a1: Double) -> Path {
        var p = Path(); p.addArc(center: c, radius: r, startAngle: .radians(a0), endAngle: .radians(a1), clockwise: false); return p
    }

    // ---------------- AQUA: liquid ring held by surface tension
    static func aqua(_ ctx: inout GraphicsContext, _ w: Double, _ t: Double, _ light: Color, _ dragging: Bool, _ time: Double) {
        let c = CGPoint(x: w / 2, y: w / 2), R0 = w * 0.375, tk = w * 0.085, va = A0 + t * SW, N = 96
        func wob(_ a: Double) -> Double { sin(a * 3 + time * 1.3) * w * 0.0028 + sin(a * 5 - time * 1.9) * w * 0.002 + sin(a * 8 + time * 2.7) * w * 0.0013 }
        func bump(_ a: Double) -> Double { let d = atan2(sin(a - va), cos(a - va)); return exp(-d * d / 0.12) * w * 0.034 + exp(-d * d / 0.9) * w * 0.008 }
        var ring = Path()
        for pass in 0..<2 {
            for i in 0...N {
                let a = Double(i) / Double(N) * TAU, b = bump(a), v = wob(a)
                let r = pass == 0 ? R0 + tk / 2 + v + b : R0 - tk / 2 + v * 0.6 + b * 0.45, p = pt(c, a, r)
                if i == 0 { ring.move(to: p) } else { ring.addLine(to: p) }
            }
            ring.closeSubpath()
        }
        let well = circle(c, w * 0.27)
        shadowed(&ctx, w, 0.42) { $0.fill(well, with: .color(Color(red: 0.04, green: 0.09, blue: 0.125).opacity(0.92))) }
        ctx.stroke(circle(c, w * 0.27 - 0.6), with: topLight(c, w * 0.27, 0.55), lineWidth: max(1, w * 0.012))
        let water = GraphicsContext.Shading.linearGradient(Gradient(colors: [Color(red: 0.59, green: 0.88, blue: 1).opacity(0.55), Color(red: 0.24, green: 0.63, blue: 0.84).opacity(0.38), Color(red: 0.08, green: 0.35, blue: 0.59).opacity(0.45)]),
                                                           startPoint: CGPoint(x: c.x, y: c.y - R0 - tk), endPoint: CGPoint(x: c.x, y: c.y + R0 + tk))
        shadowed(&ctx, w, 0.32) { $0.fill(ring, with: water, style: FillStyle(eoFill: true)) }
        ctx.drawLayer { l in
            l.clip(to: ring, style: FillStyle(eoFill: true))
            for k in 0..<18 {   // flowing streaks
                let kd = Double(k), rr = R0 - tk * 0.4 + tk * 0.8 * hash(kd + 1), a0 = hash(kd + 20) * TAU + time * (0.25 + hash(kd + 40) * 0.5) * (k % 2 == 0 ? -1 : 1), ln = 0.5 + hash(kd + 60) * 1.1
                l.stroke(arc(c, rr, a0, a0 + ln), with: .color(Color(red: 0.88, green: 0.98, blue: 1).opacity(0.2 + hash(kd + 80) * 0.35)), lineWidth: max(0.7, w * (0.004 + hash(kd + 95) * 0.006)))
            }
            l.addFilter(.shadow(color: light.opacity(0.9), radius: w * 0.05))
            l.stroke(arc(c, R0, A0, va), with: .color(light.opacity(0.55)), lineWidth: tk * 1.3)
        }
        ctx.stroke(ring, with: .linearGradient(Gradient(colors: [.white.opacity(0.85), .white.opacity(0.12), Color(red: 0, green: 0.12, blue: 0.24).opacity(0.35)]), startPoint: CGPoint(x: c.x, y: c.y - R0 - tk), endPoint: CGPoint(x: c.x, y: c.y + R0 + tk)), lineWidth: max(1, w * 0.011))
        for k in 0..<7 {   // droplets held near the surface
            let kd = Double(k), a = hash(kd + 3) * TAU, d = R0 + tk * 0.7 + w * 0.012 + hash(kd + 9) * w * 0.028, rr = w * (0.007 + hash(kd + 15) * 0.01), p = pt(c, a, d)
            ctx.fill(circle(p, rr), with: .color(Color(red: 0.67, green: 0.9, blue: 1).opacity(0.45)))
            ctx.fill(circle(CGPoint(x: p.x - rr * 0.3, y: p.y - rr * 0.35), rr * 0.38), with: .color(.white.opacity(0.75)))
        }
        let br = tk * (dragging ? 0.62 : 0.52), bd = R0 + (dragging ? tk * 0.1 : 0), bp = pt(c, va, bd)   // the pointer bead
        shadowed(&ctx, w, 0.4) { $0.fill(circle(bp, br), with: .radialGradient(Gradient(colors: [.white.opacity(0.95), light.opacity(0.75), Color(red: 0.08, green: 0.39, blue: 0.67).opacity(0.7)]), center: CGPoint(x: bp.x - br * 0.3, y: bp.y - br * 0.4), startRadius: br * 0.1, endRadius: br)) }
        if dragging { var neck = Path(); neck.move(to: pt(c, va, R0 - tk * 0.3)); neck.addLine(to: bp); ctx.stroke(neck, with: .color(light.opacity(0.6)), lineWidth: max(1.2, w * 0.014)) }
        for u in [0.0, 0.5, 1.0] { ctx.fill(circle(pt(c, A0 + u * SW, w * 0.475), max(1, w * 0.01)), with: .color(Color(red: 0.75, green: 0.9, blue: 0.96).opacity(0.55))) }
    }

    // ---------------- SCIENTIFIC: engraved scale, vernier, knurled skirt, jewelled cap, trim LED arc
    static func tech(_ ctx: inout GraphicsContext, _ w: Double, _ t: Double, _ light: Color) {
        let c = CGPoint(x: w / 2, y: w / 2), va = A0 + t * SW, big = w >= 96
        let metal = GraphicsContext.Shading.conicGradient(Gradient(colors: [Color(white: 0.6), Color(white: 0.29), Color(white: 0.67), Color(white: 0.24), Color(white: 0.6)]), center: c, angle: .degrees(-90))
        shadowed(&ctx, w, 0.45) { $0.fill(circle(c, w * 0.47), with: metal) }
        ctx.fill(circle(c, w * 0.47), with: topLight(c, w * 0.47, 0.45))
        ctx.fill(circle(c, w * 0.44), with: .color(Color(red: 0.04, green: 0.07, blue: 0.09)))
        ctx.stroke(circle(c, w * 0.445), with: .color(.black.opacity(0.7)), lineWidth: max(1, w * 0.01))
        for i in 0...50 {   // engraved scale 0...100
            let a = A0 + Double(i) / 50 * SW, major = i % 5 == 0
            var p = Path(); p.move(to: pt(c, a, w * 0.44)); p.addLine(to: pt(c, a, w * (major ? 0.375 : 0.405)))
            ctx.stroke(p, with: .color(major ? Color(white: 0.92).opacity(0.9) : Color(white: 0.75).opacity(0.55)), lineWidth: max(0.8, w * (major ? 0.012 : 0.007)))
            if major && big && i % 10 == 0 { ctx.draw(Text("\(i * 2)").font(.system(size: w * 0.06, design: .monospaced)).foregroundColor(Color(white: 0.88).opacity(0.85)), at: pt(c, a, w * 0.345)) }
        }
        if big { for i in 0...200 where i % 4 != 0 { let a = A0 + Double(i) / 200 * SW; var p = Path(); p.move(to: pt(c, a, w * 0.44)); p.addLine(to: pt(c, a, w * 0.428)); ctx.stroke(p, with: .color(Color(white: 0.5).opacity(0.4)), lineWidth: 0.6) } }
        ctx.drawLayer { l in l.addFilter(.shadow(color: light.opacity(0.9), radius: w * 0.04)); l.stroke(arc(c, w * 0.46, A0, va), with: .color(light), style: StrokeStyle(lineWidth: max(1.5, w * 0.018), lineCap: .round)) }
        let Rk = w * 0.30, teeth = 40; var sk = Path()   // knurled skirt
        for i in 0..<(teeth * 2) { let a = Double(i) / Double(teeth * 2) * TAU, p = pt(c, a, i % 2 == 1 ? Rk * 0.955 : Rk); if i == 0 { sk.move(to: p) } else { sk.addLine(to: p) } }
        sk.closeSubpath()
        let kn = GraphicsContext.Shading.conicGradient(Gradient(colors: [Color(white: 0.8), Color(white: 0.36), Color(white: 0.72), Color(white: 0.27), Color(white: 0.72), Color(white: 0.36), Color(white: 0.8)]), center: c, angle: .radians(-Double.pi / 2 - va + A0))
        shadowed(&ctx, w, 0.55) { $0.fill(sk, with: kn) }
        ctx.fill(sk, with: topLight(c, Rk, 0.5)); ctx.stroke(sk, with: .color(.black.opacity(0.5)), lineWidth: 0.8)
        for (rr, al) in [(0.255, 0.55), (0.225, 0.35)] {
            ctx.stroke(circle(c, w * rr), with: .color(.black.opacity(al)), lineWidth: max(0.8, w * 0.008))
            ctx.stroke(arc(c, w * rr + w * 0.008, Double.pi * 1.05, Double.pi * 1.95), with: .color(.white.opacity(al * 0.35)), lineWidth: max(0.8, w * 0.008))
        }
        ctx.fill(circle(c, w * 0.2), with: .radialGradient(Gradient(colors: [Color(white: 0.95), Color(white: 0.56), Color(white: 0.18)]), center: CGPoint(x: c.x, y: c.y - w * 0.08), startRadius: w * 0.01, endRadius: w * 0.2))
        ctx.stroke(circle(c, w * 0.2), with: .color(.black.opacity(0.55)), lineWidth: 1)
        var cross = Path(); cross.move(to: CGPoint(x: c.x - w * 0.07, y: c.y)); cross.addLine(to: CGPoint(x: c.x + w * 0.07, y: c.y)); cross.move(to: CGPoint(x: c.x, y: c.y - w * 0.07)); cross.addLine(to: CGPoint(x: c.x, y: c.y + w * 0.07))
        ctx.stroke(cross, with: .color(Color(red: 0.08, green: 0.12, blue: 0.16).opacity(0.55)), lineWidth: 0.8)
        var idx = Path(); idx.move(to: pt(c, va, w * 0.215)); idx.addLine(to: pt(c, va, w * 0.285))   // index line on the skirt
        ctx.drawLayer { l in l.addFilter(.shadow(color: light.opacity(0.8), radius: w * 0.025)); l.stroke(idx, with: .color(light), style: StrokeStyle(lineWidth: max(1.6, w * 0.022), lineCap: .round)) }
        var tri = Path(); tri.move(to: pt(c, va, w * 0.372)); let q1 = pt(c, va, w * 0.41); let nx = -sin(va) * w * 0.022, ny = cos(va) * w * 0.022   // triangular pointer on the scale
        tri.addLine(to: CGPoint(x: q1.x + nx, y: q1.y + ny)); tri.addLine(to: CGPoint(x: q1.x - nx, y: q1.y - ny)); tri.closeSubpath(); ctx.fill(tri, with: .color(.white))
    }

    // ---------------- HOLO RING: glass dial, segmented concentric rings, growing LED arc, white index bar
    static func holo(_ ctx: inout GraphicsContext, _ w: Double, _ t: Double, _ light: Color) {
        let c = CGPoint(x: w / 2, y: w / 2), va = A0 + t * SW
        shadowed(&ctx, w, 0.4) { $0.fill(circle(c, w * 0.43), with: .radialGradient(Gradient(colors: [Color(red: 0.03, green: 0.1, blue: 0.14).opacity(0.9), Color(red: 0.01, green: 0.04, blue: 0.06).opacity(0.92)]), center: CGPoint(x: c.x, y: c.y - w * 0.12), startRadius: w * 0.05, endRadius: w * 0.43)) }
        ctx.stroke(circle(c, w * 0.43), with: topLight(c, w * 0.43, 0.5), lineWidth: max(1, w * 0.012))
        let bands: [(Double, Double, [CGFloat], Double)] = [(0.40, 0.020, [w * 0.025, w * 0.012], 0.45), (0.355, 0.034, [w * 0.012, w * 0.012], 0.30), (0.305, 0.018, [w * 0.03, w * 0.01], 0.4)]
        for (r, lw, dash, al) in bands { ctx.stroke(circle(c, w * r), with: .color(light.opacity(al)), style: StrokeStyle(lineWidth: w * lw, dash: dash)) }
        for r in [0.42, 0.285, 0.265] { ctx.stroke(circle(c, w * r), with: .color(light.opacity(0.55)), lineWidth: max(0.8, w * 0.007)) }
        ctx.drawLayer { l in l.addFilter(.shadow(color: light.opacity(0.9), radius: w * 0.05)); l.stroke(arc(c, w * 0.355, A0, va), with: .color(light.opacity(0.95)), style: StrokeStyle(lineWidth: w * 0.034, dash: [w * 0.012, w * 0.012])) }
        ctx.fill(circle(c, w * 0.25), with: .linearGradient(Gradient(colors: [.white.opacity(0.10), .black.opacity(0.3)]), startPoint: CGPoint(x: c.x, y: c.y - w * 0.24), endPoint: CGPoint(x: c.x, y: c.y + w * 0.24)))
        for i in 0..<15 {   // outer LED dots growing clockwise, lit up to the value
            let u = Double(i) / 14, p = pt(c, A0 + u * SW, w * 0.465), rr = w * (0.012 + 0.024 * u)
            if u <= t + 0.001 {
                ctx.drawLayer { l in l.addFilter(.shadow(color: light.opacity(0.95), radius: w * 0.05)); l.fill(circle(p, rr), with: .color(light.opacity(0.95))) }
                ctx.fill(circle(CGPoint(x: p.x - rr * 0.2, y: p.y - rr * 0.3), rr * 0.4), with: .color(.white.opacity(0.7)))
            } else { ctx.fill(circle(p, rr * 0.85), with: .color(light.opacity(0.22))) }
        }
        ctx.fill(Path(CGRect(x: c.x - w * 0.007, y: c.y - w * 0.425, width: w * 0.014, height: w * 0.05)), with: .color(.white.opacity(0.9)))   // fixed index at 12 o'clock
        var bar = Path(); bar.addRoundedRect(in: CGRect(x: w * 0.2, y: -w * 0.021, width: w * 0.17, height: w * 0.042), cornerSize: CGSize(width: w * 0.021, height: w * 0.021))
        bar = bar.applying(CGAffineTransform(rotationAngle: va)).applying(CGAffineTransform(translationX: c.x, y: c.y))
        ctx.drawLayer { l in l.addFilter(.shadow(color: light.opacity(0.95), radius: w * 0.045)); l.fill(bar, with: .color(Color(red: 0.96, green: 0.98, blue: 1))) }
    }
}
