// Vektur Void: the hidden Easter-egg shoot-em-up (opened from the EASTER EGG button at the bottom of Settings).
// Rules live in Core/include/vv/vektur_void.hpp (verified frame for frame against the web game); this file only draws a snapshot and feeds input.
// It never touches the audio engine or any parameter. NOT COMPILED on the Linux build machine: build it on the Mac (see docs/MAC_DAY_ONE.md).
import SwiftUI

/// Owns the C game object. Closing the cover releases it, so the next open starts a completely fresh game.
final class VoidGameModel: ObservableObject {
    private var game: OpaquePointer?
    private var snap = [Float](repeating: 0, count: 16384)
    private var count = 0
    private var acc = 0.0
    private var lastTime: TimeInterval = 0
    // input
    var stick = CGPoint.zero            // -1...1
    var plasmaTouch = false, bombTouch = false, startPulse = false
    var keys = Set<String>()

    init() { game = vv_void_create(UInt32(truncatingIfNeeded: Int(Date().timeIntervalSince1970 * 1000))) }
    deinit { if let g = game { vv_void_destroy(g) } }

    func advance(now: TimeInterval) {
        guard let g = game else { return }
        if lastTime == 0 { lastTime = now }
        acc += min(0.1, now - lastTime); lastTime = now
        var dx = Double(stick.x), dy = Double(stick.y)
        if keys.contains("R") { dx += 1 }; if keys.contains("L") { dx -= 1 }; if keys.contains("D") { dy += 1 }; if keys.contains("U") { dy -= 1 }
        let plasma = plasmaTouch || keys.contains("plasma"), bomb = bombTouch || keys.contains("bomb")
        var start = startPulse; startPulse = false
        while acc >= 1.0 / 60.0 {
            acc -= 1.0 / 60.0
            vv_void_step(g, 1.0 / 60.0, dx, dy, plasma ? 1 : 0, bomb ? 1 : 0, start ? 1 : 0); start = false
        }
        count = Int(vv_void_snapshot(g, &snap, Int32(snap.count)))
    }
    var banner: String { guard let g = game else { return "" }; var buf = [CChar](repeating: 0, count: 64); _ = vv_void_banner(g, &buf, 64); return String(cString: buf) }
    var s: [Float] { snap }
    var valid: Bool { count >= 39 }
}

struct VekturVoidView: View {
    @Environment(\.dismiss) private var dismiss
    @StateObject private var model = VoidGameModel()
    @FocusState private var focused: Bool
    private static let W: CGFloat = 640, H: CGFloat = 360
    private let stickC = CGPoint(x: 68, y: H - 64), btnP = CGPoint(x: W - 62, y: H - 64), btnB = CGPoint(x: W - 134, y: H - 34)

    var body: some View {
        ZStack(alignment: .topTrailing) {
            Color(red: 0.02, green: 0.03, blue: 0.03).ignoresSafeArea()
            GeometryReader { geo in
                let scale = min(geo.size.width / Self.W, geo.size.height / Self.H)
                let ox = (geo.size.width - Self.W * scale) / 2, oy = (geo.size.height - Self.H * scale) / 2
                TimelineView(.animation) { tl in
                    Canvas { ctx, _ in
                        model.advance(now: tl.date.timeIntervalSinceReferenceDate)
                        guard model.valid else { return }
                        ctx.translateBy(x: ox, y: oy); ctx.scaleBy(x: scale, y: scale)
                        ctx.clip(to: Path(CGRect(x: 0, y: 0, width: Self.W, height: Self.H)))
                        VoidPainter.draw(&ctx, s: model.s, banner: model.banner, stick: model.stick, plasma: model.plasmaTouch, bomb: model.bombTouch, stickC: stickC, btnP: btnP, btnB: btnB)
                    }
                }
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { v in handleTouch(v.location, start: v.startLocation, scale: scale, ox: ox, oy: oy) }
                    .onEnded { _ in model.stick = .zero; model.plasmaTouch = false; model.bombTouch = false })
            }
            Button { dismiss() } label: { Image(systemName: "xmark.circle.fill").font(.title).foregroundStyle(Color(red: 0.29, green: 0.5, blue: 0.58)) }
                .padding(14).accessibilityLabel("Close Vektur Void")
        }
        .focusable().focused($focused).onAppear { focused = true }
        .modifier(VoidKeys(model: model))
        .statusBarHidden(true)
    }

    private func handleTouch(_ p: CGPoint, start: CGPoint, scale: CGFloat, ox: CGFloat, oy: CGFloat) {
        func g(_ q: CGPoint) -> CGPoint { CGPoint(x: (q.x - ox) / scale, y: (q.y - oy) / scale) }
        let gp = g(p), gs = g(start)
        func near(_ a: CGPoint, _ b: CGPoint, _ r: CGFloat) -> Bool { hypot(a.x - b.x, a.y - b.y) <= r }
        // menu buttons: START / CONTINUE / NEW GAME
        let mode = Int(model.s[0]); let menu = mode == 0 || mode == 3 || mode == 4 || mode == 5
        if menu && abs(gs.x - Self.W / 2) < 95 && abs(gs.y - (mode == 5 ? Self.H - 60 : Self.H - 110)) < 22 { if hypot(p.x - start.x, p.y - start.y) < 2 { model.startPulse = true }; return }
        if near(gs, stickC, 70) || (!near(gs, btnP, 40) && !near(gs, btnB, 40) && gs.x < Self.W * 0.45) {
            model.stick = CGPoint(x: max(-1, min(1, (gp.x - stickC.x) / 40)), y: max(-1, min(1, (gp.y - stickC.y) / 40)))
        } else if near(gs, btnP, 40) { model.plasmaTouch = true } else if near(gs, btnB, 40) { model.bombTouch = true }
    }
}

/// Hardware keyboard (iPad / Mac): arrows or WASD, Z or Space = plasma, X or B = bomb, Return = start. Needs iOS 17.
struct VoidKeys: ViewModifier {
    let model: VoidGameModel
    func body(content: Content) -> some View {
        if #available(iOS 17.0, macOS 14.0, *) {
            content
                .onKeyPress(phases: [.down, .up]) { press in
                    let k: String?
                    switch press.key {
                    case .leftArrow: k = "L"; case .rightArrow: k = "R"; case .upArrow: k = "U"; case .downArrow: k = "D"; case .space: k = "plasma"
                    case .return: model.startPulse = true; return .handled
                    default:
                        switch press.characters.lowercased() { case "a": k = "L"; case "d": k = "R"; case "w": k = "U"; case "s": k = "D"; case "z": k = "plasma"; case "x", "b": k = "bomb"; default: k = nil }
                    }
                    guard let key = k else { return .ignored }
                    if press.phase == .down { model.keys.insert(key) } else { model.keys.remove(key) }
                    return .handled
                }
        } else { content }
    }
}

/// Draws one snapshot in game space (640 x 360), mirroring the web renderer. Glows are approximated with translucent discs (no shadowBlur in SwiftUI).
enum VoidPainter {
    static let W: CGFloat = 640, H: CGFloat = 360
    static let ui = Color(red: 0.29, green: 0.83, blue: 0.85), uiDim = Color(red: 0.29, green: 0.5, blue: 0.58), plasma = Color(red: 0.79, green: 1, blue: 0.98)
    static let ship2 = Color(red: 0.61, green: 0.42, blue: 1), bomb = Color(red: 0.85, green: 0.29, blue: 0.62)
    static let enemy: [Color] = [Color(red: 0.61, green: 0.42, blue: 1), Color(red: 0.85, green: 0.29, blue: 0.62), Color(red: 0.29, green: 0.83, blue: 0.85), Color(red: 0.49, green: 0.88, blue: 1), Color(red: 0.79, green: 0.63, blue: 1), Color(red: 1, green: 0.48, blue: 0.85)]
    static let tint: [Color] = [Color(red: 0.49, green: 1, blue: 0.6), Color(red: 1, green: 0.66, blue: 0.25), Color(red: 1, green: 0.27, blue: 0.27)]
    static let ebul: [Color] = [Color(red: 1, green: 0.35, blue: 0.35), Color(red: 1, green: 0.7, blue: 0.28), Color(red: 1, green: 0.48, blue: 0.85), Color(red: 0.49, green: 1, blue: 0.92)]

    static func disc(_ c: inout GraphicsContext, _ x: CGFloat, _ y: CGFloat, _ r: CGFloat, _ col: Color) { c.fill(Path(ellipseIn: CGRect(x: x - r, y: y - r, width: 2 * r, height: 2 * r)), with: .color(col)) }
    static func ring(_ c: inout GraphicsContext, _ x: CGFloat, _ y: CGFloat, _ r: CGFloat, _ col: Color, _ w: CGFloat) { c.stroke(Path(ellipseIn: CGRect(x: x - r, y: y - r, width: 2 * r, height: 2 * r)), with: .color(col), lineWidth: w) }
    static func text(_ c: inout GraphicsContext, _ s: String, _ x: CGFloat, _ y: CGFloat, size: CGFloat, bold: Bool = false, col: Color, anchor: UnitPoint = .center) {
        c.draw(Text(s).font(.system(size: size, weight: bold ? .bold : .regular, design: .monospaced)).foregroundColor(col), at: CGPoint(x: x, y: y), anchor: anchor)
    }
    static func ship(_ c: inout GraphicsContext, _ x: CGFloat, _ y: CGFloat, _ thrust: CGFloat, scale: CGFloat = 1, alpha: Double = 1) {
        var cc = c; cc.opacity = alpha; cc.translateBy(x: x, y: y); cc.scaleBy(x: scale, y: scale)
        ring(&cc, -20 - thrust * 2, 0, 5, ship2.opacity(0.6), 1.2)
        var p = Path(); p.move(to: CGPoint(x: 20, y: 0)); p.addQuadCurve(to: CGPoint(x: -16, y: -10), control: CGPoint(x: 0, y: -9)); p.addLine(to: CGPoint(x: -10, y: 0)); p.addLine(to: CGPoint(x: -16, y: 10)); p.addQuadCurve(to: CGPoint(x: 20, y: 0), control: CGPoint(x: 0, y: 9)); p.closeSubpath()
        cc.fill(p, with: .color(Color(red: 0.03, green: 0.1, blue: 0.2))); cc.stroke(p, with: .color(ui), lineWidth: 1.6)
        cc.fill(Path(ellipseIn: CGRect(x: -3, y: -2.6, width: 12, height: 5.2)), with: .color(plasma.opacity(0.85)))
    }

    static func draw(_ c: inout GraphicsContext, s: [Float], banner: String, stick: CGPoint, plasma pl: Bool, bomb bm: Bool, stickC: CGPoint, btnP: CGPoint, btnB: CGPoint) {
        func f(_ i: Int) -> CGFloat { CGFloat(s[i]) }
        let mode = Int(s[0]), t = f(1), modeT = f(2), score = Int(s[3]), level = Int(s[4]), spares = Int(s[5]), continues = Int(s[6]), wLvl = Int(s[7])
        let sx = f(8), sy = f(9), alive = s[10] > 0.5, inv = f(11), shield = f(12), shake = f(13), bannerT = f(14), contT = f(15), typed = f(16)
        let hasBoss = s[17] > 0.5, bx = f(18), by = f(19), bw = f(20), bh = f(21), bHits = f(22), bMax = max(1, f(23)), bFlash = f(24), bDying = f(25), bT = f(26), bFinal = s[27] > 0.5, bCore = f(28), bTint = Int(s[29])
        if shake > 0 { c.translateBy(x: CGFloat.random(in: -0.5...0.5) * 6 * shake * 5, y: CGFloat.random(in: -0.5...0.5) * 6 * shake * 5) }
        c.fill(Path(CGRect(x: -10, y: -10, width: W + 20, height: H + 20)), with: .linearGradient(Gradient(colors: [Color(red: 0.016, green: 0.03, blue: 0.1), Color(red: 0.07, green: 0.13, blue: 0.31)]), startPoint: .zero, endPoint: CGPoint(x: 0, y: H)))
        for i in 0..<70 { let l = i % 3; let sp = 14 + CGFloat(l) * 28; var x = (CGFloat(i) * 97.3 - t * sp).truncatingRemainder(dividingBy: W); if x < 0 { x += W }; let y = (CGFloat(i) * 53.7).truncatingRemainder(dividingBy: H)
            c.fill(Path(CGRect(x: x, y: y, width: 1 + CGFloat(l) * 0.6, height: 1 + CGFloat(l) * 0.6)), with: .color(Color(red: 0.56, green: 0.91, blue: 1).opacity(0.25 + Double(l) * 0.25))) }
        var ground = Path(); ground.move(to: CGPoint(x: 0, y: H)); var gx: CGFloat = 0
        while gx <= W { ground.addLine(to: CGPoint(x: gx, y: H - 12 - 6 * sin((gx + t * 40) * 0.03) - 4 * sin((gx + t * 40) * 0.11))); gx += 20 }
        ground.addLine(to: CGPoint(x: W, y: H)); ground.closeSubpath(); c.fill(ground, with: .color(Color(red: 0.04, green: 0.09, blue: 0.25))); c.stroke(ground, with: .color(uiDim.opacity(0.5)), lineWidth: 1)
        // counts and records
        var o = 30; func n(_ k: Int) -> Int { Int(s[o + k]) }
        let nBul = n(0), nBomb = n(1), nBlast = n(2), nEn = n(3), nEb = n(4), nPick = n(5), nPart = n(6), nBeam = n(7), nObs = n(8); o += 9
        let bulletsAt = o; o += nBul * 4; let bombsAt = o; o += nBomb * 3; let blastsAt = o; o += nBlast * 4; let enemiesAt = o; o += nEn * 6; let ebulAt = o; o += nEb * 4
        let pickAt = o; o += nPick * 3; let partsAt = o; o += nPart * 4; let beamsAt = o; o += nBeam * 2; let obsAt = o
        for i in 0..<nBeam { let y = f(beamsAt + i * 2), bt = f(beamsAt + i * 2 + 1); let fire = bt > 0.9; let h: CGFloat = fire ? 18 : 2; let x1 = hasBoss ? bx - 70 : W
            c.fill(Path(CGRect(x: 0, y: y - h / 2, width: x1, height: h)), with: .color(fire ? Color(red: 1, green: 0.31, blue: 0.31).opacity(0.85) : Color(red: 1, green: 0.78, blue: 0.47).opacity(0.35))) }
        for i in 0..<nPick { let k = Int(s[pickAt + i * 3]), x = f(pickAt + i * 3 + 1), y = f(pickAt + i * 3 + 2); let col = k == 0 ? ship2 : Color(red: 0.27, green: 0.71, blue: 1)
            disc(&c, x, y, 11, Color(red: 0.03, green: 0.04, blue: 0.08).opacity(0.9)); ring(&c, x, y, 11, col, 2); text(&c, k == 0 ? "W" : "S", x, y + 1, size: 12, bold: true, col: col) }
        var obsO = obsAt
        for _ in 0..<nObs { let ox = f(obsO + 1), np = Int(s[obsO + 3]); var q = obsO + 4
            for _ in 0..<np { let r = CGRect(x: ox + f(q), y: f(q + 1), width: f(q + 2), height: f(q + 3)); c.fill(Path(r), with: .color(Color(red: 0.125, green: 0.165, blue: 0.31).opacity(0.95))); c.stroke(Path(r.insetBy(dx: 0.5, dy: 0.5)), with: .color(uiDim), lineWidth: 1.5); q += 4 }
            obsO = q }
        for i in 0..<nEn { let q = enemiesAt + i * 6; drawEnemy(&c, type: Int(s[q]), x: f(q + 1), y: f(q + 2), r: f(q + 3), t: f(q + 4), flash: f(q + 5)) }
        if hasBoss { drawBoss(&c, x: bx, y: by, t: bT, flash: bFlash, dying: bDying, final: bFinal, core: bCore, tint: bTint) }
        for i in 0..<nEb { let q = ebulAt + i * 4; let col = ebul[Int(s[q + 3]) & 3]; disc(&c, f(q), f(q + 1), f(q + 2) * 2.2, col.opacity(0.18)); disc(&c, f(q), f(q + 1), f(q + 2), col) }
        for i in 0..<nBul { let q = bulletsAt + i * 4; c.fill(Path(CGRect(x: f(q) - 3, y: f(q + 1) - f(q + 3) / 2 - 3, width: f(q + 2) + 6, height: f(q + 3) + 6)), with: .color(ui.opacity(0.25)))
            c.fill(Path(CGRect(x: f(q), y: f(q + 1) - f(q + 3) / 2, width: f(q + 2), height: f(q + 3))), with: .color(plasma)) }
        for i in 0..<nBomb { let q = bombsAt + i * 3; disc(&c, f(q), f(q + 1), f(q + 2) * 2.2, bomb.opacity(0.18)); disc(&c, f(q), f(q + 1), f(q + 2), bomb) }
        for i in 0..<nBlast { let q = blastsAt + i * 4; let k = f(q + 3) / 0.35; let r = f(q + 2) * (0.3 + 0.7 * k)
            disc(&c, f(q), f(q + 1), r, plasma.opacity(Double((1 - k) * 0.25))); ring(&c, f(q), f(q + 1), r, bomb.opacity(Double(1 - k)), 3 * (1 - k) + 1) }
        for i in 0..<nPart { let q = partsAt + i * 4; c.fill(Path(CGRect(x: f(q), y: f(q + 1), width: 2, height: 2)), with: .color(plasma.opacity(max(0, Double(1 - f(q + 3) / max(0.001, f(q + 2))))))) }
        if alive && (mode == 1 || mode == 2) && !(inv > 0 && Int(floor(t * 14)) % 2 == 1) {
            ship(&c, sx, sy, 1 + sin(t * 30) * 0.5)
            if shield > 0 { let a: Double = (shield < 2 && Int(floor(t * 10)) % 2 == 1) ? 0.35 : 1; var cc = c; cc.opacity = a
                cc.fill(Path(ellipseIn: CGRect(x: sx - 28, y: sy - 28, width: 56, height: 56)), with: .radialGradient(Gradient(stops: [.init(color: Color(red: 0.27, green: 0.7, blue: 1).opacity(0.05), location: 0.2), .init(color: Color(red: 0.27, green: 0.7, blue: 1).opacity(0.35), location: 0.75), .init(color: Color(red: 0.47, green: 0.82, blue: 1).opacity(0.85), location: 1)]), center: CGPoint(x: sx, y: sy), startRadius: 0, endRadius: 28))
                ring(&cc, sx, sy, 28, Color(red: 0.62, green: 0.82, blue: 1), 2) }
        }
        // HUD
        text(&c, "SCORE " + String(format: "%06d", score), 10, 15, size: 13, bold: true, col: ui, anchor: .leading)
        text(&c, "LEVEL \(level)", W / 2, 15, size: 13, bold: true, col: ui)
        text(&c, "SHIPS", 10, 31, size: 10, col: uiDim, anchor: .leading)
        for i in 0..<3 { ship(&c, 66 + CGFloat(i) * 24, 31, 0, scale: 0.5, alpha: i < spares ? 1 : 0.2) }
        func bars(_ n: Int) -> String { let h = String(repeating: "#", count: n + 1); return h + String(repeating: "-", count: max(0, 4 - h.count)) }
        text(&c, "PLASMA \(bars(wLvl))  BOMB \(bars(wLvl))", 10, 51, size: 10, col: uiDim, anchor: .leading)
        if shield > 0 { text(&c, "SHIELD " + String(format: "%.1f", shield) + "s", 10, 63, size: 10, col: Color(red: 0.27, green: 0.71, blue: 1), anchor: .leading) }
        if hasBoss && bDying <= 0 { let fr = max(0, (bMax - bHits) / bMax)
            c.fill(Path(CGRect(x: W / 2 - 110, y: H - 24, width: 220, height: 10)), with: .color(.black.opacity(0.67))); c.fill(Path(CGRect(x: W / 2 - 109, y: H - 23, width: 218 * fr, height: 8)), with: .color(tint[bTint]))
            c.stroke(Path(CGRect(x: W / 2 - 110.5, y: H - 24.5, width: 221, height: 11)), with: .color(ui), lineWidth: 1); text(&c, bFinal ? "FINAL BOSS" : "BOSS", W / 2, H - 33, size: 10, bold: true, col: ui) }
        if bannerT > 0 && mode != 0 { var cc = c; cc.opacity = Double(min(1, bannerT)); text(&cc, banner, W / 2, H / 2 - 40, size: 22, bold: true, col: ui) }
        // on-screen controls
        ring(&c, stickC.x, stickC.y, 44, ui.opacity(0.5), 2)
        for k in 0..<8 { let a = CGFloat(k) * .pi / 4; var p = Path(); p.move(to: CGPoint(x: stickC.x + cos(a) * 38, y: stickC.y + sin(a) * 38)); p.addLine(to: CGPoint(x: stickC.x + cos(a) * 44, y: stickC.y + sin(a) * 44)); c.stroke(p, with: .color(ui.opacity(0.5)), lineWidth: 2) }
        var dx: CGFloat = 0, dy: CGFloat = 0; if hypot(stick.x, stick.y) >= 0.35 { let a = (atan2(stick.y, stick.x) / (.pi / 4)).rounded() * (.pi / 4); dx = cos(a); dy = sin(a) }
        disc(&c, stickC.x + dx * 26, stickC.y + dy * 26, 15, ui.opacity(0.7))
        for (pt, label, on) in [(btnP, "PLASMA", pl), (btnB, "BOMB", bm)] { disc(&c, pt.x, pt.y, 32, on ? ui.opacity(0.9) : Color.black.opacity(0.2)); ring(&c, pt.x, pt.y, 32, ui.opacity(on ? 0.9 : 0.5), 2); text(&c, label, pt.x, pt.y, size: 10, bold: true, col: on ? .black : ui) }
        // overlays
        func panel(_ lines: [String]) { c.fill(Path(CGRect(x: 0, y: 0, width: W, height: H)), with: .color(.black.opacity(0.62)))
            for (i, l) in lines.enumerated() { text(&c, l, W / 2, i == 0 ? H / 2 - 80 : H / 2 - 50 + CGFloat(i - 1) * 24, size: i == 0 ? 30 : 14, bold: i == 0, col: i == 0 ? ui : Color(red: 0.91, green: 0.89, blue: 0.83)) } }
        func button(_ label: String, _ y: CGFloat) { let r = CGRect(x: W / 2 - 90, y: y - 18, width: 180, height: 36); c.fill(Path(r), with: .color(.black.opacity(0.5))); c.stroke(Path(r), with: .color(ui), lineWidth: 2); text(&c, label, W / 2, y, size: 15, bold: true, col: ui) }
        switch mode {
        case 0: panel(["VEKTUR VOID", "8-way joystick (or arrows / WASD) moves the ship", "PLASMA (Z / Space)   BOMB (X / B)", "Collect W = weapon upgrade, S = shield"]); button("START", H - 110)
        case 3: panel(["SHIP LOST", "Continue?  \(max(0, Int(ceil(contT))))", "Continues left: \(continues)"]); button("CONTINUE", H - 110)
        case 4: panel(["GAME OVER!", "Final score " + String(format: "%06d", score)]); if modeT > 1.5 { button("NEW GAME", H - 110) }
        case 5:
            c.fill(Path(CGRect(x: 0, y: 0, width: W, height: H)), with: .color(.black.opacity(0.72)))
            let story = ["MISSION COMPLETE", "The Spectral Eye closes and the void falls quiet.", "Every frequency drifts home to its own colour.", "The universe is safe again, pilot. Brilliant flying."]
            text(&c, story[0], W / 2, 60, size: 28, bold: true, col: ui); var nChars = Int(floor(typed))
            for i in 1..<story.count { let full = story[i]; let shown = String(full.prefix(max(0, nChars))); nChars -= full.count; text(&c, shown, W / 2, 100 + CGFloat(i) * 28, size: 14, col: Color(red: 0.91, green: 0.89, blue: 0.83)) }
            text(&c, "FINAL SCORE " + String(format: "%06d", score), W / 2, H - 110, size: 14, bold: true, col: ui); if modeT > 6 { button("NEW GAME", H - 60) }
        default: break
        }
    }

    static func drawEnemy(_ c: inout GraphicsContext, type: Int, x: CGFloat, y: CGFloat, r: CGFloat, t: CGFloat, flash: CGFloat) {
        let col = enemy[max(0, min(5, type))]; let edge = flash > 0 ? Color.white : col; let fillC = Color(red: 0.04, green: 0.02, blue: 0.06).opacity(0.85)
        func body(_ p: Path) { c.fill(p, with: .color(fillC)); c.stroke(p, with: .color(edge), lineWidth: 1.7) }
        switch type {
        case 0: for k in -1...1 { let h = r * (0.6 + 0.5 * abs(sin(t * 5 + CGFloat(k) * 1.7))); c.fill(Path(CGRect(x: x + CGFloat(k) * 7 - 2.5, y: y - h, width: 5, height: h * 2)), with: .color(col.opacity(0.85))) }
        case 1: body(Path(ellipseIn: CGRect(x: x - r * 0.9, y: y - r * 0.9, width: r * 1.8, height: r * 1.8))); for k in 0..<4 { let a = t * 3 + CGFloat(k) * 1.57; disc(&c, x + cos(a) * r * 1.1, y + sin(a) * r * 1.1, 2.3, col) }
        case 2: var p = Path(); p.move(to: CGPoint(x: x - r, y: y)); p.addQuadCurve(to: CGPoint(x: x + r, y: y - r * 0.2), control: CGPoint(x: x, y: y - r * 1.3)); p.addLine(to: CGPoint(x: x + r * 0.3, y: y)); p.addLine(to: CGPoint(x: x + r, y: y + r * 0.2)); p.addQuadCurve(to: CGPoint(x: x - r, y: y), control: CGPoint(x: x, y: y + r * 1.3)); p.closeSubpath(); body(p)
        case 3: var p = Path(); for k in 0..<8 { let a = CGFloat(k) * .pi / 4 + .pi / 8; let q = CGPoint(x: x + cos(a) * r, y: y + sin(a) * r); if k == 0 { p.move(to: q) } else { p.addLine(to: q) } }; p.closeSubpath(); body(p)
            var cr = Path(); cr.move(to: CGPoint(x: x - r * 0.5, y: y)); cr.addLine(to: CGPoint(x: x + r * 0.5, y: y)); cr.move(to: CGPoint(x: x, y: y - r * 0.5)); cr.addLine(to: CGPoint(x: x, y: y + r * 0.5)); c.stroke(cr, with: .color(edge), lineWidth: 1.7)
        case 4: body(Path(ellipseIn: CGRect(x: x - r, y: y - r * 0.62, width: r * 2, height: r * 1.24))); disc(&c, x - 3, y, 3.6, col)
        default: var p = Path(); for k in 0..<10 { let a = CGFloat(k) * .pi / 5 + t; let rr = k % 2 == 1 ? r * 0.5 : r * 1.1; let q = CGPoint(x: x + cos(a) * rr, y: y + sin(a) * rr); if k == 0 { p.move(to: q) } else { p.addLine(to: q) } }; p.closeSubpath(); body(p)
        }
    }

    static func drawBoss(_ c: inout GraphicsContext, x: CGFloat, y: CGFloat, t: CGFloat, flash: CGFloat, dying: CGFloat, final: Bool, core: CGFloat, tint ti: Int) {
        let col = tint[max(0, min(2, ti))]; let edge = flash > 0 ? Color.white : col; let esc: CGFloat = final ? 1.25 : 1
        var cc = c; var jx: CGFloat = 0, jy: CGFloat = 0
        if dying > 0 { jx = CGFloat.random(in: -3...3); jy = CGFloat.random(in: -3...3); cc.opacity = Double.random(in: 0.5...1) }
        let bx = x + jx, by = y + jy
        cc.fill(Path(ellipseIn: CGRect(x: bx - 66 * esc, y: by - 84 * esc, width: 132 * esc, height: 168 * esc)), with: .color(Color(red: 0.055, green: 0.04, blue: 0.07).opacity(0.92)))
        cc.stroke(Path(ellipseIn: CGRect(x: bx - 66 * esc, y: by - 84 * esc, width: 132 * esc, height: 168 * esc)), with: .color(edge), lineWidth: 2.2)
        if final { cc.stroke(Path(ellipseIn: CGRect(x: bx - 80 * esc, y: by - 98 * esc, width: 160 * esc, height: 196 * esc)), with: .color(edge.opacity(0.4)), lineWidth: 2.2) }
        for k in 0..<14 { let a = -CGFloat.pi / 2 + CGFloat(k) / 13 * .pi; let h = (10 + 10 * abs(sin(t * 3 + CGFloat(k)))) * esc
            var bar = cc; bar.translateBy(x: bx, y: by); bar.rotate(by: .radians(Double(a))); bar.fill(Path(CGRect(x: 70 * esc, y: -3, width: h, height: 6)), with: .color(col.opacity(dying > 0 ? 0.5 : 0.55))) }
        cc.stroke(Path(ellipseIn: CGRect(x: bx + 10 * esc - 38 * esc, y: by - 50 * esc, width: 76 * esc, height: 100 * esc)), with: .color(edge), lineWidth: 2.2)
        let cx = x + core, cy = y + 10 * sin(t * 2); let pulse = 0.6 + 0.4 * sin(t * 8)
        disc(&c, cx, cy, 20 + pulse * 3, Color.white.opacity(0.1 + 0.1 * Double(pulse))); disc(&c, cx, cy, 10 + pulse * 3, Color.white.opacity(0.5 + 0.5 * Double(pulse))); ring(&c, cx, cy, 15, col, 2)
    }
}
