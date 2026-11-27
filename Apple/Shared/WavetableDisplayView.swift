// Premium wavetable display (port of the web display): finely bevelled bezel, recessed glass, hidden-line mesh lit from above,
// perspective STACK view and the square-base isometric ISOMORPH view, smoothed position glide, live phase-warped frame ribbon.
// Frames come straight from the bundled .vvwt files (uint32 frames, uint32 size, then float32 data), decimated once and cached.
// NOTE: written without a Mac. Not compiled yet.
import SwiftUI

final class WavetableFrames {
    static let shared = WavetableFrames()
    private var cache: [String: [[Float]]] = [:]
    private let cols = 160
    func frames(_ name: String) -> [[Float]] {
        if let c = cache[name] { return c }
        var out: [[Float]] = []
        if let dir = Bundle(for: VectaAudioUnit.self).url(forResource: "Wavetables", withExtension: nil), let d = try? Data(contentsOf: dir.appendingPathComponent(name + ".vvwt")), d.count > 8 {
            let nf = Int(d.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 0, as: UInt32.self) }), sz = Int(d.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: 4, as: UInt32.self) })
            if nf > 0, sz > 0, d.count >= 8 + nf * sz * 4 {
                d.withUnsafeBytes { raw in
                    for f in 0..<nf { var row = [Float](repeating: 0, count: cols + 1); for c in 0...cols { let x = Float(c) / Float(cols) * Float(sz - 1); let i0 = Int(x), i1 = min(sz - 1, i0 + 1), t = x - Float(i0)
                        let a = raw.loadUnaligned(fromByteOffset: 8 + (f * sz + i0) * 4, as: Float.self), b = raw.loadUnaligned(fromByteOffset: 8 + (f * sz + i1) * 4, as: Float.self); row[c] = a + (b - a) * t }; out.append(row) } }
            }
        }
        if out.isEmpty { out = [(0...cols).map { sinf(Float($0) / Float(cols) * 2 * .pi) }] }
        cache[name] = out; return out
    }
}

struct WavetableDisplay: View {
    @ObservedObject var s: PresetStore
    let oscPath: String
    @AppStorage("vv.wtviz.view") private var viewMode = 0     // 0 STACK, 1 ISOMORPH, 2 3D (free touch orbit)
    @State private var shown: Double = -1
    // free 3D pose, driven by touch: one finger turns the mesh (X drag = yaw, Y drag = pitch), two fingers move and zoom it, double-tap resets
    @State private var yaw: Double = 0.0
    @State private var pitch: Double = 0.62
    @State private var pan: CGSize = .zero
    @State private var zoom: Double = 1.0
    static let homeYaw = 0.0, homePitch = 0.62
    var body: some View {
        let accent = LayerID.color(0)
        let pose = Pose(yaw: yaw, pitch: pitch, pan: pan, zoom: zoom)
        ZStack(alignment: .topTrailing) {
            TimelineView(.animation(minimumInterval: BatterySaving.shared.frameInterval())) { _ in
                Canvas { ctx, size in draw(ctx, size, accent, pose) }
                    .aspectRatio(432.0 / 168.0, contentMode: .fit)
            }
            .overlay(OrbitTouchLayer(
                onBegin: { if viewMode != 2 { viewMode = 2; yaw = Self.homeYaw; pitch = Self.homePitch; pan = .zero; zoom = 1 } },
                onRotate: { d in yaw += Double(d.width) * 0.008; pitch = max(-1.5, min(1.5, pitch + Double(d.height) * 0.008)) },
                onMove: { d in pan = CGSize(width: pan.width + d.width, height: pan.height + d.height) },
                onZoom: { f in zoom = max(0.4, min(3.0, zoom * Double(f))) },
                onReset: { withAnimation(.easeOut(duration: 0.25)) { yaw = Self.homeYaw; pitch = Self.homePitch; pan = .zero; zoom = 1 } }))
            .background(Color(red: 0.02, green: 0.03, blue: 0.04))
            .clipShape(RoundedRectangle(cornerRadius: 6))
            .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(Color.black, lineWidth: 1))
            .overlay(RoundedRectangle(cornerRadius: 6).fill(LinearGradient(colors: [Color.white.opacity(0.07), Color.white.opacity(0.015), .clear], startPoint: .topLeading, endPoint: UnitPoint(x: 0.6, y: 0.5))).allowsHitTesting(false))   // glass sheen
            .shadow(color: .black.opacity(0.9), radius: 4, x: 0, y: 2).padding(7)
            Button(viewMode == 0 ? "STACK" : (viewMode == 1 ? "ISOMORPH" : "3D")) { viewMode = (viewMode + 1) % 3 }.font(.system(size: 8.5, weight: .semibold, design: .monospaced)).foregroundStyle(accent)
                .padding(.horizontal, 6).padding(.vertical, 3).background(Color.black.opacity(0.55)).overlay(RoundedRectangle(cornerRadius: 3).strokeBorder(accent.opacity(0.4))).padding(14)
        }
        .overlay(alignment: .bottomLeading) {
            if viewMode == 2 { Text("DRAG TURN · 2 FINGERS MOVE/ZOOM · DOUBLE-TAP RESET").font(.system(size: 7, weight: .medium, design: .monospaced)).foregroundStyle(accent.opacity(0.55)).padding(.leading, 16).padding(.bottom, 12).allowsHitTesting(false) }
        }
        .background(RoundedRectangle(cornerRadius: 11).fill(LinearGradient(colors: [Color(white: 0.20), Color(white: 0.10), Color(white: 0.06)], startPoint: .top, endPoint: .bottom)))
        .overlay(RoundedRectangle(cornerRadius: 11).strokeBorder(LinearGradient(colors: [Color.white.opacity(0.16), .black.opacity(0.7)], startPoint: .top, endPoint: .bottom), lineWidth: 1))
        .frame(maxWidth: 432 * 1.04 + 22).padding(.leading, 14).padding(.vertical, 6)   // roughly 20% larger than the old 360pt display, with extra room on the left
    }

    struct Pose { var yaw: Double; var pitch: Double; var pan: CGSize; var zoom: Double }

    private func draw(_ ctx: GraphicsContext, _ size: CGSize, _ accent: Color, _ pose: Pose) {
        let W = size.width, H = size.height, cols = 160
        let name = s.string(oscPath + ".table", "analogMorph"); let frames = WavetableFrames.shared.frames(name); let n = frames.count
        let target = max(0, min(1, s.double(oscPath + ".position", 0))) * Double(max(0, n - 1))
        DispatchQueue.main.async { if shown < 0 || abs(shown - target) > Double(n) { shown = target } else { shown += (target - shown) * 0.4; if abs(target - shown) < 0.002 { shown = target } } }
        let pos = shown < 0 ? target : shown
        let ui = UIColor(accent); var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0; ui.getRed(&r, green: &g, blue: &b, alpha: nil)
        func col(_ a: Double, lift: Double = 0) -> Color { Color(red: min(1, r + lift), green: min(1, g + lift), blue: min(1, b + lift)).opacity(a) }
        // faint overhead glow
        ctx.fill(Path(CGRect(origin: .zero, size: size)), with: .radialGradient(Gradient(colors: [col(0.10), .clear]), center: CGPoint(x: W / 2, y: H * 0.1), startRadius: 4, endRadius: W * 0.7))
        let geo: (Double, Double, Double) -> CGPoint
        // free 3D projection (viewMode 2): u across the table, d through the frames, sv the sample value. Real yaw/pitch rotation, perspective, pan and zoom.
        let cy = cos(pose.yaw), sy = sin(pose.yaw), cp = cos(pose.pitch), sp = sin(pose.pitch)
        let camDist = 3.4
        let unit = Double(min(W, H * 1.75)) * 0.30 * pose.zoom
        func rot3(_ u: Double, _ d: Double, _ sv: Double) -> (sx: Double, sy: Double, depth: Double) {
            let x = (u - 0.5) * 2.0, z = (d - 0.5) * 1.5, y = sv * 0.45
            let x1 = x * cy + z * sy
            let z1 = -x * sy + z * cy
            let up = y * cp + z1 * sp
            let depth = z1 * cp - y * sp
            let k = camDist / (camDist + depth)
            return (x1 * k * unit, up * k * unit, depth)
        }
        if viewMode == 2 {
            let ox = Double(W) / 2 + Double(pose.pan.width), oy = Double(H) * 0.52 + Double(pose.pan.height)
            geo = { u, d, sv in let q = rot3(u, d, sv); return CGPoint(x: CGFloat(ox + q.sx), y: CGFloat(oy - q.sy)) }
            var plate = Path(); plate.move(to: geo(0, 0, -0.55)); plate.addLine(to: geo(1, 0, -0.55)); plate.addLine(to: geo(1, 1, -0.55)); plate.addLine(to: geo(0, 1, -0.55)); plate.closeSubpath()
            for k in 0...4 { let t = Double(k) / 4; plate.move(to: geo(t, 0, -0.55)); plate.addLine(to: geo(t, 1, -0.55)); plate.move(to: geo(0, t, -0.55)); plate.addLine(to: geo(1, t, -0.55)) }
            ctx.stroke(plate, with: .color(col(0.20)), lineWidth: 0.6)
        } else if viewMode == 1 {
            let L = min(W * 0.27, H * 0.70), cs = 0.8660254, sn = 0.5, ox = W * 0.5 - L * cs, oy = H * 0.62, hh = H * 0.24
            geo = { u, d, sv in CGPoint(x: ox + (u + d) * L * cs, y: oy + (u - d) * L * sn - sv * hh) }
            var grid = Path(); for k in 0...4 { let t = Double(k) / 4; grid.move(to: geo(t, 0, -0.55)); grid.addLine(to: geo(t, 1, -0.55)); grid.move(to: geo(0, t, -0.55)); grid.addLine(to: geo(1, t, -0.55)) }
            ctx.stroke(grid, with: .color(col(0.22)), lineWidth: 0.6)
        } else {
            let bw = W * 0.80, x0 = W * 0.10, yNear = H * 0.80, rise = H * 0.46, shrink: CGFloat = 0.20, hh = H * 0.20
            geo = { u, d, sv in
                let uu = CGFloat(u), dd = CGFloat(d), ss = CGFloat(sv)
                let k: CGFloat = 1 - shrink * dd
                let x: CGFloat = x0 + bw * (1 - k) * 0.5 + uu * bw * k + dd * W * 0.03
                let y: CGFloat = yNear - dd * rise - ss * hh * (1 - 0.35 * dd)
                return CGPoint(x: x, y: y)
            }
            var plate = Path(); plate.move(to: geo(0, 0, -0.35)); plate.addLine(to: geo(1, 0, -0.35)); plate.addLine(to: geo(1, 1, -0.35)); plate.addLine(to: geo(0, 1, -0.35)); plate.closeSubpath()
            ctx.stroke(plate, with: .color(col(0.16)), lineWidth: 0.6)
        }
        let stepF = max(1, Int((Double(n) / 40).rounded(.up))); var list = Array(stride(from: 0, to: n, by: stepF)); if list.last != n - 1 { list.append(n - 1) }
        var order: [Int] = Array(list.reversed())
        var dMin = 0.0, dMax = 1.0
        if viewMode == 2 {
            func rowDepth(_ i: Int) -> Double { rot3(0.5, n > 1 ? Double(i) / Double(n - 1) : 0, 0).depth }
            order = list.sorted { rowDepth($0) > rowDepth($1) }            // far rows first, so near rows hide them whatever the angle
            let ds = list.map { rowDepth($0) }; dMin = ds.min() ?? 0; dMax = max(dMin + 0.0001, ds.max() ?? 1)
        }
        for i in order {
            let d = n > 1 ? Double(i) / Double(n - 1) : 0; let A = frames[i]
            var line = Path(); var first = CGPoint.zero
            for c in 0...cols { let p = geo(Double(c) / Double(cols), d, Double(A[c])); if c == 0 { line.move(to: p); first = p } else { line.addLine(to: p) } }
            var fill = line; fill.addLine(to: geo(1, d, -0.55)); fill.addLine(to: geo(0, d, -0.55)); fill.closeSubpath()
            ctx.fill(fill, with: .linearGradient(Gradient(colors: [Color(red: 0.03 + r * 0.05, green: 0.055 + g * 0.05, blue: 0.08 + b * 0.06), Color(red: 0.012, green: 0.024, blue: 0.04)]), startPoint: CGPoint(x: 0, y: first.y - 30), endPoint: CGPoint(x: 0, y: geo(0, d, -0.55).y)))
            let shade: Double = viewMode == 2 ? (rot3(0.5, d, 0).depth - dMin) / (dMax - dMin) : d      // nearer = brighter, also when turned
            let al = 0.30 + 0.62 * (1 - shade)
            ctx.stroke(line, with: .linearGradient(Gradient(colors: [col(min(1, al + 0.25), lift: 0.35), col(al * 0.7)]), startPoint: CGPoint(x: 0, y: first.y - 14), endPoint: CGPoint(x: 0, y: geo(0, d, -0.55).y)), lineWidth: i == 0 ? 1.5 : 0.9)
        }
        // live frame ribbon: same phase-warp math as the engine so the picture matches what plays
        let fi = min(n - 1, Int(pos)), ff = Float(pos - Double(fi)); let A = frames[fi], B = frames[min(n - 1, fi + 1)]; let d = n > 1 ? pos / Double(n - 1) : 0
        let pw = s.double(oscPath + ".pwm", 0.5), ph = s.double(oscPath + ".phase", 0), se = s.double(oscPath + ".startEnd", 1)
        func warp(_ p: Double) -> Double { abs(pw - 0.5) < 0.001 ? p : (p < pw ? (p / pw) * 0.5 : 0.5 + ((p - pw) / (1 - pw)) * 0.5) }
        var live = Path()
        for c in 0...cols { let cp = Double(c) / Double(cols); let w = warp((cp + ph).truncatingRemainder(dividingBy: 1)); let idx = min(cols, Int(w * se * Double(cols + 1))); let sv = A[idx] + (B[idx] - A[idx]) * ff
            let p = geo(cp, d, Double(sv)); if c == 0 { live.move(to: p) } else { live.addLine(to: p) } }
        var glow = ctx; glow.addFilter(.shadow(color: Color(red: 0.29, green: 1, blue: 0.54), radius: 7)); glow.stroke(live, with: .color(Color(red: 0.29, green: 1, blue: 0.54)), lineWidth: 1.9)
        ctx.stroke(live, with: .color(.white.opacity(0.55)), lineWidth: 0.7)
    }
}

// Touch layer for the free 3D wavetable view (UIKit recognisers, so one finger, two fingers and pinch can act together).
// It also asks any enclosing scroll view to wait, so touching the display turns the mesh instead of scrolling the page.
struct OrbitTouchLayer: UIViewRepresentable {
    var onBegin: () -> Void
    var onRotate: (CGSize) -> Void
    var onMove: (CGSize) -> Void
    var onZoom: (CGFloat) -> Void
    var onReset: () -> Void

    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIView(context: Context) -> UIView {
        let v = UIView(); v.backgroundColor = .clear; v.isMultipleTouchEnabled = true
        let c = context.coordinator
        let one = UIPanGestureRecognizer(target: c, action: #selector(Coordinator.rotate(_:))); one.minimumNumberOfTouches = 1; one.maximumNumberOfTouches = 1
        let two = UIPanGestureRecognizer(target: c, action: #selector(Coordinator.move(_:))); two.minimumNumberOfTouches = 2; two.maximumNumberOfTouches = 2
        let pinch = UIPinchGestureRecognizer(target: c, action: #selector(Coordinator.zoom(_:)))
        let tap = UITapGestureRecognizer(target: c, action: #selector(Coordinator.reset(_:))); tap.numberOfTapsRequired = 2
        for g in [one, two, pinch, tap] as [UIGestureRecognizer] { g.delegate = c; v.addGestureRecognizer(g); c.ours.append(g) }
        return v
    }
    func updateUIView(_ uiView: UIView, context: Context) { context.coordinator.parent = self }

    final class Coordinator: NSObject, UIGestureRecognizerDelegate {
        var parent: OrbitTouchLayer
        var ours: [UIGestureRecognizer] = []
        init(_ p: OrbitTouchLayer) { parent = p }
        @objc func rotate(_ g: UIPanGestureRecognizer) {
            if g.state == .began { parent.onBegin() }
            let t = g.translation(in: g.view); g.setTranslation(.zero, in: g.view)
            if g.state == .began || g.state == .changed { parent.onRotate(CGSize(width: t.x, height: t.y)) }
        }
        @objc func move(_ g: UIPanGestureRecognizer) {
            if g.state == .began { parent.onBegin() }
            let t = g.translation(in: g.view); g.setTranslation(.zero, in: g.view)
            if g.state == .began || g.state == .changed { parent.onMove(CGSize(width: t.x, height: t.y)) }
        }
        @objc func zoom(_ g: UIPinchGestureRecognizer) {
            if g.state == .began { parent.onBegin() }
            if g.state == .began || g.state == .changed { parent.onZoom(g.scale); g.scale = 1 }
        }
        @objc func reset(_ g: UITapGestureRecognizer) { if g.state == .ended { parent.onReset() } }
        // two-finger move and pinch may run together
        func gestureRecognizer(_ a: UIGestureRecognizer, shouldRecognizeSimultaneouslyWith b: UIGestureRecognizer) -> Bool {
            let ours = self.ours.contains(a) && self.ours.contains(b)
            return ours && (a is UIPinchGestureRecognizer || b is UIPinchGestureRecognizer)
        }
        // other pans (a ScrollView's) must wait for ours, so the page does not scroll while turning the mesh
        func gestureRecognizer(_ a: UIGestureRecognizer, shouldBeRequiredToFailBy other: UIGestureRecognizer) -> Bool {
            other is UIPanGestureRecognizer && !self.ours.contains(other)
        }
    }
}
