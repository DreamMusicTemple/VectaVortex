// Bezier curve editor (the web app's "DRAW SHAPE" / "DRAW CUSTOM SHAPE"). Edits a preset array of {x, y, cx, cy} points:
// x, y in 0...1; (cx, cy) is the control point of the quadratic segment that STARTS at that node. First and last node are pinned in x.
//   drag a node = move it · drag the line = bow that segment · tap empty space = add a node · double-tap a node = delete it (end nodes stay)
// NOTE: written without a Mac. Not compiled yet.
import SwiftUI

struct CurvePt { var x: Double; var y: Double; var cx: Double? = nil; var cy: Double? = nil; var lin = false }   // lin: straight segment leaving this node (R11)

enum Curve {
    static func read(_ s: PresetStore, _ path: String) -> [CurvePt] {
        s.array(path).compactMap { any in
            guard let d = any as? [String: Any], let x = (d["x"] as? NSNumber)?.doubleValue, let y = (d["y"] as? NSNumber)?.doubleValue else { return nil }
            return CurvePt(x: x, y: y, cx: (d["cx"] as? NSNumber)?.doubleValue, cy: (d["cy"] as? NSNumber)?.doubleValue, lin: (d["lin"] as? Bool) ?? ((d["lin"] as? NSNumber)?.boolValue ?? false))
        }
    }
    static func ensure(_ p: inout [CurvePt]) { for i in 0..<max(0, p.count - 1) where p[i].cx == nil { p[i].cx = (p[i].x + p[i + 1].x) / 2; p[i].cy = (p[i].y + p[i + 1].y) / 2 } }
    static func write(_ s: PresetStore, _ path: String, _ pts: [CurvePt]) {
        var p = pts; ensure(&p)
        s.set(path, p.enumerated().map { i, q -> [String: Any] in
            var d: [String: Any] = ["x": q.x, "y": q.y]; if i + 1 < p.count { d["cx"] = q.cx ?? q.x; d["cy"] = q.cy ?? q.y; if q.lin { d["lin"] = true } }; return d })
    }
    /// The ONE curve evaluator for display (same maths as the C++ evalCurveY used by the audio engine).
    static func eval(_ p: [CurvePt], _ x: Double) -> Double {
        guard p.count >= 2 else { return p.first?.y ?? 0 }
        var i = 0; while i + 2 < p.count && p[i + 1].x < x { i += 1 }
        let a = p[i], b = p[i + 1]
        if a.lin { let dx = b.x - a.x; return dx < 1e-9 ? b.y : a.y + (b.y - a.y) * min(max((x - a.x) / dx, 0), 1) }
        let cx = a.cx ?? (a.x + b.x) / 2, cy = a.cy ?? (a.y + b.y) / 2
        let a0 = a.x - 2 * cx + b.x, b0 = 2 * (cx - a.x), c0 = a.x - x
        var t: Double
        if abs(a0) < 1e-6 { t = abs(b0) < 1e-6 ? 0 : min(max(-c0 / b0, 0), 1) }
        else { let disc = max(0, b0 * b0 - 4 * a0 * c0), sq = disc.squareRoot(), t1 = (-b0 + sq) / (2 * a0), t2 = (-b0 - sq) / (2 * a0); t = (t1 >= -0.001 && t1 <= 1.001) ? min(max(t1, 0), 1) : min(max(t2, 0), 1) }
        let mt = 1 - t; return mt * mt * a.y + 2 * mt * t * cy + t * t * b.y
    }
    static var standard: [CurvePt] { var p = [CurvePt(x: 0, y: 0), CurvePt(x: 0.3, y: 1), CurvePt(x: 0.6, y: 0.4), CurvePt(x: 1, y: 0)]; ensure(&p); return p }
    /// Entering Custom starts from a recognisable version of the shape that was selected (web: sampleBasicWaveformPoints).
    static func basic(_ wf: String, n: Int = 9) -> [CurvePt] {
        var p: [CurvePt] = []
        for i in 0..<n {
            let ph = Double(i) / Double(n - 1); let raw: Double
            switch wf { case "triangle": raw = ph < 0.5 ? 4 * ph - 1 : 3 - 4 * ph; case "pulse": raw = ph < 0.5 ? 1 : -1; case "sawUp": raw = 2 * ph - 1; case "sawDown": raw = 1 - 2 * ph; default: raw = sin(2 * .pi * ph) }
            p.append(CurvePt(x: ph, y: (raw + 1) / 2))
        }
        ensure(&p); return p
    }
}

/// Round 11: EQ-style direct manipulation, shared by the LFO shape, filter-envelope shape and the six pitch envelopes (mirrors the web VVCurve editor).
///   drag a node = move it (end nodes slide vertically only) · drag the curve = bend that segment (BEZIER) or move both ends vertically (STRAIGHT) ·
///   tap empty space = add a node · double-tap a node = delete it · FINE = 0.15x relative drag · SNAP = grid / semitone snapping · UNDO · zoom (pinch).
/// The displayed curve is evaluated with the same quadratic-segment maths as the audio engine (evalCurveY), so what is drawn is what modulates.
struct CurveCanvas: View {
    @Binding var pts: [CurvePt]
    var onChange: () -> Void = {}
    var yGrid: [(y: Double, label: String)] = []      // optional labelled horizontal lines (e.g. semitones)
    var snapY: ((Double) -> Double)? = nil            // snap used when SNAP is on (default 1/20 steps)
    var playhead: (() -> Double?)? = nil              // normalised x of a live playhead (drawn at display rate)
    @State private var sel = -1
    @State private var snap = false, fine = false
    @State private var undo: [[CurvePt]] = []
    @State private var drag: Drag? = nil
    @State private var zoom: Double = 1, x0: Double = 0
    @State private var fineAnchor: (CGPoint, Double, Double)? = nil
    private enum Drag { case node(Int), seg(Int), handle(Int), empty }
    private let pad = EdgeInsets(top: 10, leading: 30, bottom: 18, trailing: 10)

    var body: some View {
        VStack(spacing: 6) {
            HStack(spacing: 6) {
                Button("STRAIGHT") { push(); for i in 0..<max(0, pts.count - 1) { pts[i].lin = true }; onChange() }
                Button("BEZIER") { push(); for i in 0..<max(0, pts.count - 1) { pts[i].lin = false; pts[i].cx = (pts[i].x + pts[i + 1].x) / 2; pts[i].cy = (pts[i].y + pts[i + 1].y) / 2 }; onChange() }
                Toggle("SNAP", isOn: $snap).toggleStyle(.button); Toggle("FINE", isOn: $fine).toggleStyle(.button)
                Button("UNDO") { if let u = undo.popLast() { pts = u; onChange() } }.disabled(undo.isEmpty)
                Button("−") { setZoom(zoom / 1.6) }; Button("+") { setZoom(zoom * 1.6) }
            }.font(.caption2).buttonStyle(.bordered).controlSize(.mini)
            GeometryReader { geo in
                // TimelineView(.animation) = display link: 60 Hz, 120 Hz on ProMotion. Only the playhead needs it, so it idles when there is none.
                TimelineView(.animation(minimumInterval: nil, paused: playhead == nil)) { _ in
                    Canvas { ctx, size in draw(ctx, size) }
                }
                .background(Color.black).clipShape(RoundedRectangle(cornerRadius: 6))
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { g in dragChanged(g, geo.size) }
                    .onEnded { _ in drag = nil; fineAnchor = nil })
                .simultaneousGesture(SpatialTapGesture(count: 2).onEnded { t in removeNode(at: t.location, geo.size) })
                .simultaneousGesture(MagnificationGesture().onChanged { m in setZoom(m) })
            }
        }
    }

    // MARK: geometry (screen px <-> curve units; hit testing in SCREEN px so grab radius is constant at any zoom and on touch)
    private func area(_ s: CGSize) -> CGRect { CGRect(x: pad.leading, y: pad.top, width: max(10, s.width - pad.leading - pad.trailing), height: max(10, s.height - pad.top - pad.bottom)) }
    private func toP(_ x: Double, _ y: Double, _ s: CGSize) -> CGPoint { let a = area(s); return CGPoint(x: a.minX + (x - x0) / zoomSpan * a.width, y: a.minY + (1 - y) * a.height) }
    private func fromP(_ p: CGPoint, _ s: CGSize) -> (Double, Double) { let a = area(s); return (x0 + Double((p.x - a.minX) / a.width) * zoomSpan, 1 - Double((p.y - a.minY) / a.height)) }
    private var zoomSpan: Double { 1 / zoom }
    private func setZoom(_ z: Double) { zoom = min(max(z, 1), 16); x0 = min(max(x0, 0), 1 - zoomSpan) }
    private func push() { undo.append(pts); if undo.count > 40 { undo.removeFirst() } }
    private func clamp01(_ v: Double) -> Double { min(max(v, 0), 1) }
    private func sy(_ y: Double) -> Double { snap ? (snapY?(y) ?? (y * 20).rounded() / 20) : y }
    private func sx(_ x: Double) -> Double { snap ? (x * 16).rounded() / 16 : x }

    private func segPoint(_ i: Int, _ t: Double) -> (Double, Double) {
        let a = pts[i], b = pts[i + 1]
        if a.lin { return (a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t) }
        let cx = a.cx ?? (a.x + b.x) / 2, cy = a.cy ?? (a.y + b.y) / 2, mt = 1 - t
        return (mt * mt * a.x + 2 * mt * t * cx + t * t * b.x, mt * mt * a.y + 2 * mt * t * cy + t * t * b.y)
    }
    private func hit(_ p: CGPoint, _ s: CGSize) -> Drag {
        let R: CGFloat = 22
        var best = -1, bd = R
        for (i, q) in pts.enumerated() { let d = hypot(toP(q.x, q.y, s).x - p.x, toP(q.x, q.y, s).y - p.y); if d < bd { bd = d; best = i } }
        if best >= 0 { return .node(best) }
        for i in 0..<max(0, pts.count - 1) where !pts[i].lin {   // bezier handles of the selected neighbourhood
            let c = toP(pts[i].cx ?? pts[i].x, pts[i].cy ?? pts[i].y, s); if hypot(c.x - p.x, c.y - p.y) < 16 { return .handle(i) }
        }
        var seg = -1; bd = 16
        for i in 0..<max(0, pts.count - 1) { for k in 0...24 { let (qx, qy) = segPoint(i, Double(k) / 24); let q = toP(qx, qy, s); let d = hypot(q.x - p.x, q.y - p.y); if d < bd { bd = d; seg = i } } }
        return seg >= 0 ? .seg(seg) : .empty
    }
    private func dragChanged(_ g: DragGesture.Value, _ s: CGSize) {
        if drag == nil {
            push(); drag = hit(g.startLocation, s); fineAnchor = nil; startNode = nil
            switch drag! {
            case .node(let i): sel = i
            case .empty:   // add a node where the finger landed
                let (x, y) = fromP(g.startLocation, s); let nx = clamp01(sx(x)), ny = clamp01(sy(y))
                pts.append(CurvePt(x: nx, y: ny)); pts.sort { $0.x < $1.x }
                if let ni = pts.firstIndex(where: { $0.x == nx && $0.y == ny }) { sel = ni; drag = .node(ni) ; resetCps(ni) }
                onChange(); return
            default: break
            }
        }
        let (rx, ry) = fromP(g.location, s)
        // FINE: motion is relative to where fine mode began, scaled by 0.15
        var x = rx, y = ry
        if fine { let (sx0, sy0) = fromP(g.startLocation, s); x = sx0 + (rx - sx0) * 0.15; y = sy0 + (ry - sy0) * 0.15 }
        switch drag! {
        case .node(let i):
            let end = i == 0 || i == pts.count - 1
            if fine, i < pts.count { let q0 = startNode ?? pts[i]; if startNode == nil { startNode = q0 }; x = q0.x + (rx - fromP(g.startLocation, s).0) * 0.15; y = q0.y + (ry - fromP(g.startLocation, s).1) * 0.15 }
            pts[i].y = clamp01(sy(y))
            if !end { pts[i].x = min(max(sx(x), pts[i - 1].x + 0.002), pts[i + 1].x - 0.002); resetCps(i) }
        case .handle(let i): pts[i].cx = min(max(x, pts[i].x), pts[i + 1].x); pts[i].cy = clamp01(y)
        case .seg(let i):
            let a = pts[i], b = pts[i + 1]
            if a.lin { let (_, y0) = fromP(g.startLocation, s); let dy = y - y0; pts[i].y = clamp01(sy(a.y + dy * 0.2)); pts[i + 1].y = clamp01(sy(b.y + dy * 0.2)) }
            else {   // solve the control point so the grabbed point of the segment tracks the pointer (quadratic: B(t) = 2(1-t)t C + ... )
                let t = 0.5, mt = 1 - t; let cx = (x - (mt * mt * a.x + t * t * b.x)) / (2 * mt * t), cy = (y - (mt * mt * a.y + t * t * b.y)) / (2 * mt * t)
                pts[i].cx = min(max(cx, a.x), b.x); pts[i].cy = min(max(cy, -0.5), 1.5)
            }
        case .empty: break
        }
        onChange()
    }
    @State private var startNode: CurvePt? = nil
    private func resetCps(_ i: Int) {   // keep neighbouring control points inside their segment after a node moves
        for k in [i - 1, i] where k >= 0 && k + 1 < pts.count { if pts[k].lin { continue }; pts[k].cx = min(max(pts[k].cx ?? (pts[k].x + pts[k + 1].x) / 2, pts[k].x), pts[k + 1].x); pts[k].cy = pts[k].cy ?? (pts[k].y + pts[k + 1].y) / 2 }
    }
    private func removeNode(at loc: CGPoint, _ s: CGSize) {
        if case .node(let n) = hit(loc, s), n > 0, n < pts.count - 1 { push(); pts.remove(at: n); sel = -1; onChange() }
    }
    private func draw(_ ctx: GraphicsContext, _ size: CGSize) {
        let a = area(size)
        var grid = Path()
        for i in 0...10 { let x = a.minX + CGFloat(i) / 10 * a.width; grid.move(to: CGPoint(x: x, y: a.minY)); grid.addLine(to: CGPoint(x: x, y: a.maxY)) }
        for i in 0...4 { let y = a.minY + CGFloat(i) / 4 * a.height; grid.move(to: CGPoint(x: a.minX, y: y)); grid.addLine(to: CGPoint(x: a.maxX, y: y)) }
        ctx.stroke(grid, with: .color(Color(white: 0.12)), lineWidth: 1)
        for g in yGrid {
            let y = toP(0, g.y, size).y; var l = Path(); l.move(to: CGPoint(x: a.minX, y: y)); l.addLine(to: CGPoint(x: a.maxX, y: y))
            ctx.stroke(l, with: .color(Color(white: 0.22)), lineWidth: 1); ctx.draw(Text(g.label).font(.system(size: 8)).foregroundColor(Color(white: 0.5)), at: CGPoint(x: 15, y: y))
        }
        // the curve = evalCurveY sampled per pixel, so straight and bezier segments are drawn exactly as the engine reads them
        var p = Path()
        if pts.count >= 2 {
            let n = Int(a.width)
            for px in 0...n { let x = x0 + Double(px) / Double(max(1, n)) * zoomSpan; let y = Curve.eval(pts, x); let q = toP(x, y, size); if px == 0 { p.move(to: q) } else { p.addLine(to: q) } }
        }
        ctx.stroke(p, with: .color(Color(red: 0.61, green: 0.42, blue: 1)), lineWidth: 2)
        for i in 0..<max(0, pts.count - 1) where !pts[i].lin {
            let c = toP(pts[i].cx ?? pts[i].x, pts[i].cy ?? pts[i].y, size); ctx.fill(Path(ellipseIn: CGRect(x: c.x - 3, y: c.y - 3, width: 6, height: 6)), with: .color(Color(white: 0.55)))
        }
        for (i, q) in pts.enumerated() { let c = toP(q.x, q.y, size); let r: CGFloat = i == sel ? 7 : 6
            ctx.fill(Path(ellipseIn: CGRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r)), with: .color(i == sel ? .white : (i == 0 || i == pts.count - 1 ? vvCyan : .white.opacity(0.85)))) }
        if let ph = playhead?() { let x = toP(ph, 0, size).x; var l = Path(); l.move(to: CGPoint(x: x, y: a.minY)); l.addLine(to: CGPoint(x: x, y: a.maxY)); ctx.stroke(l, with: .color(.green), lineWidth: 1.5) }
    }
}

struct CurveEditorView: View {
    @ObservedObject var s: PresetStore
    let path: String
    let title: String
    @Environment(\.dismiss) private var dismiss
    @State private var pts: [CurvePt] = []
    var body: some View {
        NavigationStack {
            VStack(spacing: 10) {
                CurveCanvas(pts: $pts, onChange: { Curve.write(s, path, pts) }).frame(height: 300)
                Text("Drag a node to move it, drag the curve to bend it (STRAIGHT mode moves the segment), tap empty space to add a node, double-tap a node to remove it. FINE = 0.15x precision, SNAP = grid.").font(.caption2).foregroundStyle(.secondary)
                Button("RESET") { pts = Curve.standard; Curve.write(s, path, pts) }.buttonStyle(.bordered)
                Spacer()
            }
            .padding()
            .navigationTitle(title).navigationBarTitleDisplayModeInline()
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
            .onAppear { pts = Curve.read(s, path); if pts.count < 2 { pts = Curve.standard; Curve.write(s, path, pts) }; Curve.ensure(&pts) }
        }
    }
}

private extension View {
    @ViewBuilder func navigationBarTitleDisplayModeInline() -> some View {
        #if os(iOS)
        self.navigationBarTitleDisplayMode(.inline)
        #else
        self
        #endif
    }
}

/// A button that opens the curve editor for a preset path.
struct CurveButton: View {
    @ObservedObject var s: PresetStore
    let path: String; let label: String; let title: String
    @State private var open = false
    var body: some View { Button(label) { open = true }.buttonStyle(.bordered).sheet(isPresented: $open) { CurveEditorView(s: s, path: path, title: title) } }
}
