// Sampler waveform editor (the web app's slot waveform + FULL SCREEN EDIT), the SwiftUI twin of Juce/Source/SampleEditor.cpp:
// start / end markers, fade shading, loop markers, up to 16 numbered transient (slice) markers with per-slice reverse + pan, zoom + pan
// (pinch, buttons, view slider), live playhead, CROP / CUT (silence) / NORMALIZE / DETECT TRANSIENTS, snap to zero-crossing, and a full-screen
// cover with the slice list and press-and-hold slice audition. Audio edits write a processed sample into the preset's sampleLibrary.
// Touch editing of transient markers: every marker has a numbered flag you can grab (52 pt wide touch zone, the marker follows the finger
// without jumping), markers cannot cross their neighbours, a drag is held in a working copy and written once on release (no engine/preset
// traffic mid-drag), tap selects (and auditions), the selected marker shows a red x badge, double-tap a marker deletes it, double-tap empty
// waveform adds one, DELETE SELECTED / CLEAR ALL / UNDO buttons, haptics. Peaks come from a 64-sample min/max pyramid so zooming and
// panning stay O(width), and the waveform only redraws at full rate while a voice is playing or a finger is down.
// NOTE: this file has not been compiled (no Mac in the build environment); the JUCE twin is the tested reference.
import SwiftUI
#if canImport(UIKit)
import UIKit
#endif

struct SliceMark: Equatable { var pos: Double; var pan: Double = 0; var reverse: Bool = false }

private final class PeakCache {
    var key = ""; var cols: [(Float, Float)] = []
    var bmin: [Float] = [], bmax: [Float] = []      // min/max per 64-sample block (the pyramid)
    static let block = 64
}

struct SampleWaveformEditor: View {
    @ObservedObject var s: PresetStore
    @ObservedObject private var power = BatterySaving.shared
    let slot: Int
    var full = false
    static let maxSlices = 16

    @State private var data: [Float] = []
    @State private var sr = 48000.0
    @State private var loaded = ""
    @State private var vs = 0.0, ve = 1.0
    @State private var snapZC = false
    @State private var status = ""
    @State private var drag: Drag?
    @State private var showFull = false
    @State private var lastMag: CGFloat = 1
    @State private var playing: Int?
    @State private var cache = PeakCache()
    @State private var work: [SliceMark]?            // marker positions while a finger is down (written to the preset on release)
    @State private var sel: Int?                     // selected marker
    @State private var undoStack: [[SliceMark]] = []
    @State private var moved = false
    @State private var grabDX: CGFloat = 0           // finger-to-line offset at touch-down so the marker never jumps
    @State private var panVS0 = 0.0
    @State private var pinching = false
    @State private var lastTapT = Date.distantPast
    @State private var lastTapX: CGFloat = -999
    @State private var lastTapSlice = -1
    @State private var auditionTap = true
    @State private var live = false                  // a voice is playing this slot (drives the redraw rate)
    private static let ticker = Timer.publish(every: 0.2, on: .main, in: .common).autoconnect()
    private enum Drag: Equatable { case start, end, slice(Int), loopStart, loopEnd, badge(Int), pan, empty }
    private let hitR: CGFloat = 26                   // half-width of a marker's touch zone

    private var p: String { "layers.SMP.oscs.\(slot)" }

    // MARK: slices in the preset
    private var slices: [SliceMark] {
        s.array(p + ".transients").compactMap { e in
            if let d = e as? [String: Any] { return SliceMark(pos: (d["pos"] as? NSNumber)?.doubleValue ?? 0, pan: (d["pan"] as? NSNumber)?.doubleValue ?? 0, reverse: (d["reverse"] as? NSNumber)?.boolValue ?? false) }
            if let n = e as? NSNumber { return SliceMark(pos: n.doubleValue) }
            return nil
        }
    }
    private func setSlices(_ v: [SliceMark]) {
        let sorted = v.sorted { $0.pos < $1.pos }.prefix(Self.maxSlices)
        s.set(p + ".transients", sorted.map { ["pos": min(max($0.pos, 0), 1), "reverse": $0.reverse, "pan": min(max($0.pan, -1), 1)] as [String: Any] })
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            wave.frame(height: full ? 340 : 170)
            if ve - vs < 0.999 {
                Slider(value: Binding(get: { vs }, set: { let span = ve - vs; vs = min(max($0, 0), 1 - span); ve = vs + span })) .tint(vvCyan)
            }
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 120), spacing: 6)], spacing: 6) {
                Button("CROP TO SELECTION") { crop() }
                Button("CUT (SILENCE)") { cutSilence() }
                Button("NORMALIZE") { normalize() }
                Button("DETECT TRANSIENTS") { detect() }
                Button("SEND TRANSIENTS TO MPC") { sendToMpc() }
                Button("ZOOM −") { zoom(1 / 1.6) }
                Button("ZOOM +") { zoom(1.6) }
                Button("RESET VIEW") { vs = 0; ve = 1 }
                Button("+ SLICE") { addSlice(pos: snapped((vs + ve) / 2)) }
                Button("DELETE SELECTED") { if let i = selValid(slices.count) { removeSlice(i) } }.disabled(selValid(slices.count) == nil)
                Button("CLEAR ALL") { clearSlices() }.disabled(slices.isEmpty)
                Button("UNDO") { undo() }.disabled(undoStack.isEmpty)
                if full { Button("AUDITION") { s.auditionSlice(slot: slot, slice: 0); DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { s.auditionOff() } } }
                else { Button("FULL SCREEN EDIT") { showFull = true } }
            }.buttonStyle(.bordered).font(.caption2)
            Toggle("Snap to zero-crossing", isOn: $snapZC).font(.caption)
            Toggle("Audition a slice when its marker is tapped", isOn: $auditionTap).font(.caption)
            if !status.isEmpty { Text(status).font(.caption2).foregroundStyle(vvCyan) }
            Text("Drag a numbered yellow flag to move a slice (it stops at its neighbours). Tap a flag to select it, then tap its red ✕ or press DELETE SELECTED; double-tap a flag to delete it, double-tap the waveform to add one. Pinch to zoom; when zoomed, drag empty waveform to pan. Warp markers: SAMPLE TOOLS…").font(.caption2).foregroundStyle(.secondary)
            if full { sliceList }
        }
        .onAppear { reload() }
        .onReceive(Self.ticker) { _ in let l = s.sampleHead(slot) >= 0; if l != live { live = l } }
        .onChange(of: s.string(p + ".name", "")) { _ in reload() }
        .fullScreenCover(isPresented: $showFull) {
            NavigationStack { ScrollView { SampleWaveformEditor(s: s, slot: slot, full: true).padding() }
                .navigationTitle("Sample editor — slot \(slot + 1)").navigationBarTitleDisplayMode(.inline)
                .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Close") { showFull = false } } } }
        }
    }

    // MARK: waveform canvas
    private var wave: some View {
        GeometryReader { geo in
            TimelineView(.animation(minimumInterval: BatterySaving.shared.interval(1.0 / 30), paused: !live && drag == nil)) { _ in
                let headNow = Double(s.sampleHead(slot))
                Canvas { ctx, size in draw(ctx, size, headNow) }
            }
            .background(Color.black).clipShape(RoundedRectangle(cornerRadius: 4))
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { g in dragChanged(g, size: geo.size) }
                .onEnded { g in dragEnded(g, size: geo.size) })
            .simultaneousGesture(MagnificationGesture().onChanged { m in pinching = true; zoomAround(0.5, Double(m / lastMag)); lastMag = m }.onEnded { _ in lastMag = 1; pinching = false; drag = nil; work = nil })
        }
    }
    private func X(_ f: Double, _ w: CGFloat) -> CGFloat { CGFloat((f - vs) / (ve - vs)) * w }
    private func F(_ x: CGFloat, _ w: CGFloat) -> Double { vs + Double(min(max(x / max(1, w), 0), 1)) * (ve - vs) }

    private func draw(_ ctx: GraphicsContext, _ size: CGSize, _ head: Double) {
        let W = size.width, H = size.height
        guard !data.isEmpty else {
            ctx.draw(Text(s.string(p + ".name", "").isEmpty ? "No sample in this slot — pick one, IMPORT, RECORD or IMAGE → AUDIO" : "Sample not found in the library").font(.caption).foregroundColor(.secondary), at: CGPoint(x: W / 2, y: H / 2)); return
        }
        let st = s.double(p + ".start", 0), en = s.double(p + ".end", 1), dur = Double(data.count) / sr
        let fi = s.double(p + ".fadeIn", 0), fo = s.double(p + ".fadeOut", 0)
        let sx = X(st, W), ex = X(en, W)
        ctx.fill(Path(CGRect(x: sx, y: 0, width: max(0, X(st + fi / dur, W) - sx), height: H)), with: .color(Color(red: 0.48, green: 0.54, blue: 0.29).opacity(0.28)))
        let fox = min(X(en - fo / dur, W), ex)
        ctx.fill(Path(CGRect(x: fox, y: 0, width: max(0, ex - fox), height: H)), with: .color(Color(red: 0.54, green: 0.29, blue: 0.29).opacity(0.28)))
        ctx.fill(Path(CGRect(x: 0, y: 0, width: max(0, sx), height: H)), with: .color(.black.opacity(0.4)))
        ctx.fill(Path(CGRect(x: min(W, ex), y: 0, width: max(0, W - ex), height: H)), with: .color(.black.opacity(0.4)))
        // min/max columns, cached per view + width; wide columns read the 64-sample pyramid instead of every sample
        let key = "\(Int(W))|\(vs)|\(ve)|\(data.count)"
        if cache.key != key {
            cache.key = key; cache.cols = Array(repeating: (1, -1), count: max(0, Int(W)))
            let n = data.count, i0 = vs * Double(n), span = (ve - vs) * Double(n), B = PeakCache.block
            let haveBlocks = cache.bmin.count == (n + B - 1) / B
            for x in 0..<Int(W) {
                let a = max(0, Int((i0 + span * Double(x) / Double(W)).rounded(.down)))
                if a >= n { continue }
                let b = min(n, max(a + 1, Int((i0 + span * Double(x + 1) / Double(W)).rounded(.down))))
                var mn: Float = 1, mx: Float = -1
                if haveBlocks && b - a > 3 * B { for k in (a / B)...((b - 1) / B) { mn = min(mn, cache.bmin[k]); mx = max(mx, cache.bmax[k]) } }
                else { for i in a..<b { let v = data[i]; mn = min(mn, v); mx = max(mx, v) } }
                if mn <= mx { cache.cols[x] = (mn, mx) }
            }
        }
        var wp = Path(); let mid = H / 2
        for (x, c) in cache.cols.enumerated() where c.0 <= c.1 { wp.move(to: CGPoint(x: CGFloat(x) + 0.5, y: mid - CGFloat(c.1) * mid)); wp.addLine(to: CGPoint(x: CGFloat(x) + 0.5, y: max(mid - CGFloat(c.1) * mid + 1, mid - CGFloat(c.0) * mid))) }
        ctx.stroke(wp, with: .color(Color(red: 0.5, green: 0.75, blue: 0.5)), lineWidth: 1)
        let ls = s.double(p + ".loopStartFrac", -1), le = s.double(p + ".loopEndFrac", -1)
        if s.bool(p + ".loop", false) && ls >= 0 && le >= 0 {
            for f in [ls, le] { var l = Path(); l.move(to: CGPoint(x: X(f, W), y: 0)); l.addLine(to: CGPoint(x: X(f, W), y: H)); ctx.stroke(l, with: .color(Color(red: 0.91, green: 0.78, blue: 0.29)), style: StrokeStyle(lineWidth: 1, dash: [4, 3])) }
        }
        var sp = Path(); sp.move(to: CGPoint(x: sx, y: 0)); sp.addLine(to: CGPoint(x: sx, y: H)); ctx.stroke(sp, with: .color(Color(red: 0.29, green: 0.54, blue: 0.29)), lineWidth: 2)
        var ep = Path(); ep.move(to: CGPoint(x: ex, y: 0)); ep.addLine(to: CGPoint(x: ex, y: H)); ctx.stroke(ep, with: .color(Color(red: 0.79, green: 0.29, blue: 0.29)), lineWidth: 2)
        var sh = Path(); sh.move(to: CGPoint(x: sx, y: 0)); sh.addLine(to: CGPoint(x: sx + 12, y: 0)); sh.addLine(to: CGPoint(x: sx, y: 14)); sh.closeSubpath(); ctx.fill(sh, with: .color(Color(red: 0.37, green: 0.85, blue: 0.5)))
        var eh = Path(); eh.move(to: CGPoint(x: ex, y: 0)); eh.addLine(to: CGPoint(x: ex - 12, y: 0)); eh.addLine(to: CGPoint(x: ex, y: 14)); eh.closeSubpath(); ctx.fill(eh, with: .color(Color(red: 0.88, green: 0.42, blue: 0.42)))
        let sl = work ?? slices, selI = selValid(sl.count)
        for (i, m) in sl.enumerated() {
            let x = X(m.pos, W); if x < -30 || x > W + 30 { continue }
            let c = m.reverse ? Color(red: 1, green: 0.42, blue: 0.42) : Color(red: 0.89, green: 0.85, blue: 0.29)
            let hot = selI == i || drag == .slice(i)
            var l = Path(); l.move(to: CGPoint(x: x, y: 0)); l.addLine(to: CGPoint(x: x, y: H)); ctx.stroke(l, with: .color(c), lineWidth: hot ? 3 : 1.5)
            let side: CGFloat = x > W - 64 ? -1 : 1
            let flag = CGRect(x: side > 0 ? x : x - 22, y: 0, width: 22, height: 18)
            ctx.fill(Path(roundedRect: flag, cornerRadius: 3), with: .color(hot ? c : c.opacity(0.8)))
            ctx.draw(Text("\(i + 1)").font(.system(size: 11, weight: .bold, design: .monospaced)).foregroundColor(.black), at: CGPoint(x: flag.midX, y: flag.midY))
            if abs(m.pan) > 0.02 { ctx.fill(Path(CGRect(x: x + 3, y: 22, width: 3 + 14 * abs(m.pan), height: 2)), with: .color(c)) }
            if selI == i {
                let bc = badgeCenter(m.pos, W)
                ctx.fill(Path(ellipseIn: CGRect(x: bc.x - 12, y: bc.y - 12, width: 24, height: 24)), with: .color(Color(red: 0.88, green: 0.2, blue: 0.2)))
                ctx.draw(Text("✕").font(.system(size: 13, weight: .bold)).foregroundColor(.white), at: bc)
            }
        }
        if head >= 0 {
            if let gi = sl.lastIndex(where: { $0.pos <= head }) {
                let x0 = X(sl[gi].pos, W), x1 = X(gi + 1 < sl.count ? sl[gi + 1].pos : en, W)
                ctx.fill(Path(CGRect(x: x0, y: 0, width: max(2, x1 - x0), height: H)), with: .color(Color(red: 1, green: 0.93, blue: 0.43).opacity(0.28)))
            }
            ctx.fill(Path(CGRect(x: X(head, W) - 1, y: 0, width: 2, height: H)), with: .color(.white))
        }
    }

    // MARK: interaction
    private func snapped(_ t: Double) -> Double {
        let c = min(max(t, 0), 1); guard snapZC, !data.isEmpty else { return c }
        let n = data.count, idx = Int(c * Double(n)); var best = idx, bd = Int.max
        for k in -200...200 { let i = idx + k; if i < 1 || i >= n { continue }; let cross = data[i] == 0 || ((data[i - 1] < 0) != (data[i] < 0)); if cross && abs(k) < bd { bd = abs(k); best = i } }
        return Double(best) / Double(n)
    }
    private func selValid(_ n: Int) -> Int? { if let i = sel, i >= 0, i < n { return i }; return nil }
    private func badgeCenter(_ pos: Double, _ W: CGFloat) -> CGPoint { let x = X(pos, W); return CGPoint(x: x + (x > W - 64 ? -34 : 34), y: 11) }

    /// What a touch-down at `pt` grabs: the selected marker's ✕ badge, a start/end triangle handle, the nearest marker flag/line (52 pt zone),
    /// a loop marker, else the nearest start/end when the whole sample is in view, or a pan when zoomed in.
    private func pick(_ pt: CGPoint, _ W: CGFloat) -> Drag {
        let sl = slices
        if let si = selValid(sl.count) { let bc = badgeCenter(sl[si].pos, W); if hypot(pt.x - bc.x, pt.y - bc.y) < 22 { return .badge(si) } }
        let sx = X(s.double(p + ".start", 0), W), ex = X(s.double(p + ".end", 1), W)
        if pt.y < 22 && (abs(sx - pt.x) < 16 || abs(ex - pt.x) < 16) { return abs(sx - pt.x) <= abs(ex - pt.x) ? .start : .end }
        var best: Int?, bd = hitR
        for (i, m) in sl.enumerated() { let d = abs(X(m.pos, W) - pt.x); if d < bd { bd = d; best = i } }
        if let b = best { return .slice(b) }
        let ls = s.double(p + ".loopStartFrac", -1), le = s.double(p + ".loopEndFrac", -1), looping = s.bool(p + ".loop", false) && ls >= 0 && le >= 0
        if looping && abs(X(ls, W) - pt.x) < 14 { return .loopStart }
        if looping && abs(X(le, W) - pt.x) < 14 { return .loopEnd }
        let dS = abs(sx - pt.x), dE = abs(ex - pt.x)
        if ve - vs >= 0.999 || min(dS, dE) < 44 { return dS <= dE ? .start : .end }
        return .pan
    }
    private func grabOffset(_ d: Drag, startX: CGFloat, W: CGFloat) -> CGFloat {
        switch d {
        case .start: return X(s.double(p + ".start", 0), W) - startX
        case .end: return X(s.double(p + ".end", 1), W) - startX
        case .loopStart: return X(s.double(p + ".loopStartFrac", 0), W) - startX
        case .loopEnd: return X(s.double(p + ".loopEndFrac", 1), W) - startX
        case .slice(let i): let sl = slices; return sl.indices.contains(i) ? X(sl[i].pos, W) - startX : 0
        default: return 0
        }
    }
    private func dragChanged(_ g: DragGesture.Value, size: CGSize) {
        guard !data.isEmpty, !pinching else { return }
        let W = size.width
        if drag == nil {
            let d = pick(g.startLocation, W); drag = d; moved = false; panVS0 = vs
            grabDX = grabOffset(d, startX: g.startLocation.x, W: W)
            if case .slice(let i) = d { if sel != i { haptic(.select) }; sel = i }
        }
        if !moved { if hypot(g.translation.width, g.translation.height) < 6 { return }; moved = true }   // a tap never nudges anything
        guard let d = drag else { return }
        let x = g.location.x + grabDX
        switch d {
        case .start: s.set(p + ".start", min(max(snapped(F(x, W)), 0), s.double(p + ".end", 1) - 0.01))
        case .end: s.set(p + ".end", max(min(snapped(F(x, W)), 1), s.double(p + ".start", 0) + 0.01))
        case .loopStart: s.set(p + ".loopStartFrac", min(max(snapped(F(x, W)), 0), s.double(p + ".loopEndFrac", 1) - 0.001))
        case .loopEnd: s.set(p + ".loopEndFrac", max(min(snapped(F(x, W)), 1), s.double(p + ".loopStartFrac", 0) + 0.001))
        case .slice(let i):
            var v = work ?? slices; guard v.indices.contains(i) else { return }
            let gap = max(0.0002, 32.0 / Double(max(1, data.count)))                     // markers stop at their neighbours, so numbers never swap mid-drag
            let lo = i > 0 ? v[i - 1].pos + gap : 0.0, hi = i < v.count - 1 ? v[i + 1].pos - gap : 1.0
            v[i].pos = min(max(snapped(F(x, W)), lo), max(lo, hi)); work = v
        case .pan:
            let span = ve - vs, dx = Double(g.translation.width / max(1, W)) * span
            vs = min(max(panVS0 - dx, 0), 1 - span); ve = vs + span
        case .badge, .empty: break
        }
    }
    private func dragEnded(_ g: DragGesture.Value, size: CGSize) {
        defer { drag = nil; work = nil; moved = false }
        guard let d = drag, !data.isEmpty, !pinching else { return }
        let W = size.width
        if moved {
            if case .slice(let i) = d, let w = work, w != slices { pushUndo(); setSlices(w); sel = i; status = "Slice \(i + 1) moved." }
            return
        }
        let now = Date(), dbl = now.timeIntervalSince(lastTapT) < 0.35 && abs(g.location.x - lastTapX) < 30
        switch d {
        case .badge(let i): removeSlice(i); lastTapT = .distantPast
        case .slice(let i):
            if dbl && lastTapSlice == i { removeSlice(i); lastTapT = .distantPast }
            else { sel = i; lastTapT = now; lastTapX = g.location.x; lastTapSlice = i; if auditionTap { audition(i) } }
        default:
            if dbl && lastTapSlice == -1 { addSlice(pos: snapped(F(g.location.x, W))); lastTapT = .distantPast }
            else { sel = nil; lastTapT = now; lastTapX = g.location.x; lastTapSlice = -1 }
        }
    }

    // MARK: marker edits (every one is undoable)
    private enum Haptic { case select, add, delete }
    private func haptic(_ k: Haptic) {
        #if canImport(UIKit)
        switch k {
        case .select: UISelectionFeedbackGenerator().selectionChanged()
        case .add: UIImpactFeedbackGenerator(style: .light).impactOccurred()
        case .delete: UINotificationFeedbackGenerator().notificationOccurred(.warning)
        }
        #endif
    }
    private func pushUndo() { undoStack.append(slices); if undoStack.count > 30 { undoStack.removeFirst() } }
    private func undo() {
        guard let prev = undoStack.popLast() else { return }
        setSlices(prev); sel = nil; status = "Undone — \(prev.count) slice\(prev.count == 1 ? "" : "s")."; haptic(.select)
    }
    private func removeSlice(_ i: Int) {
        var v = slices; guard v.indices.contains(i) else { return }
        pushUndo(); v.remove(at: i); setSlices(v)
        if let k = sel { sel = k == i ? nil : (k > i ? k - 1 : k) }
        status = "Slice \(i + 1) deleted — \(v.count) left (UNDO restores it)."; haptic(.delete)
    }
    private func clearSlices() {
        guard !slices.isEmpty else { return }
        pushUndo(); setSlices([]); sel = nil; status = "All slices cleared (UNDO restores them)."; haptic(.delete)
    }
    private func addSlice(pos: Double) {
        var v = slices; guard v.count < Self.maxSlices else { status = "\(Self.maxSlices) slices is the maximum — delete one first."; return }
        pushUndo(); v.append(SliceMark(pos: pos)); setSlices(v)
        sel = slices.firstIndex { abs($0.pos - pos) < 1e-9 }; status = "\(v.count) slices — drag the new flag into place."; haptic(.add)
    }
    private func audition(_ i: Int) {
        s.auditionSlice(slot: slot, slice: i + 1)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.7) { s.auditionOff() }
    }
    private func clampView() {
        let minSpan = max(0.0005, 64.0 / Double(max(64, data.count))); let span = min(max(ve - vs, minSpan), 1), c = (vs + ve) / 2
        vs = c - span / 2; ve = c + span / 2; if vs < 0 { ve -= vs; vs = 0 }; if ve > 1 { vs -= ve - 1; ve = 1 }; vs = max(0, vs); ve = min(1, ve)
    }
    private func zoomAround(_ frac: Double, _ factor: Double) { let c = vs + frac * (ve - vs), span = (ve - vs) / factor; vs = c - frac * span; ve = vs + span; clampView() }
    private func zoom(_ f: Double) { zoomAround(0.5, f) }

    // MARK: audio
    private func reload() {
        let name = s.string(p + ".name", ""); loaded = name; data = []; cache.key = ""; cache.bmin = []; cache.bmax = []; work = nil; drag = nil
        guard !name.isEmpty, let wav = s.sampleWav(name), let a = AudioHandle(wav: wav) else { return }
        data = a.channel(0); sr = Double(vv_audio_sample_rate(a.p)); vs = 0; ve = 1
        buildPyramid()
    }
    private func buildPyramid() {
        let n = data.count, B = PeakCache.block, nb = (n + B - 1) / B
        var mn = [Float](repeating: 1, count: nb), mx = [Float](repeating: -1, count: nb)
        data.withUnsafeBufferPointer { q in
            for b in 0..<nb { var lo: Float = 1, hi: Float = -1; for i in (b * B)..<min(n, b * B + B) { let v = q[i]; if v < lo { lo = v }; if v > hi { hi = v } }; mn[b] = lo; mx[b] = hi }
        }
        cache.bmin = mn; cache.bmax = mx; cache.key = ""
    }
    private func readAll() -> (ch: [[Float]], rate: Int)? {
        guard let wav = s.sampleWav(s.string(p + ".name", "")), let a = AudioHandle(wav: wav), a.length > 0 else { return nil }
        let nc = max(1, min(2, Int(vv_audio_channels(a.p))))
        return ((0..<nc).map { a.channel($0) }, Int(vv_audio_sample_rate(a.p)))
    }
    private func apply(_ ch: [[Float]], rate: Int, suffix: String, reset: Bool) {
        guard let first = ch.first, !first.isEmpty else { return }
        let h: OpaquePointer? = ch.count > 1 ? vv_audio_create(Int32(rate), 2, Int32(first.count), ch[0], ch[1]) : vv_audio_create(Int32(rate), 1, Int32(first.count), ch[0], nil)
        guard let a = AudioHandle(h) else { return }
        var base = s.string(p + ".name", "sample"); for sfx in [" (crop)", " (cut)", " (norm)"] where base.hasSuffix(sfx) { base.removeLast(sfx.count) }
        let nm = s.uniqueSampleName(base + suffix); s.putSample(name: nm, wav: a.wav)
        s.set(p + ".name", nm); s.set(p + ".origName", "")
        if reset { undoStack.removeAll(); sel = nil; s.set(p + ".start", 0.0); s.set(p + ".end", 1.0); s.set(p + ".transients", [Any]()); s.set(p + ".loopStartFrac", NSNull()); s.set(p + ".loopEndFrac", NSNull()) }
        reload()
    }
    private func crop() {
        guard let (ch, rate) = readAll() else { status = "No sample to crop."; return }
        let len = ch[0].count, a = min(max(Int((s.double(p + ".start", 0) * Double(len)).rounded(.down)), 0), len), b = min(max(Int((s.double(p + ".end", 1) * Double(len)).rounded(.down)), 0), len)
        guard b - a >= 64 else { status = "Selection too short to crop."; return }
        apply(ch.map { Array($0[a..<b]) }, rate: rate, suffix: " (crop)", reset: true); status = "Cropped to the selection."
    }
    private func cutSilence() {   // web cutBufferToSilence: silence start..end in place with a 3 ms fade at each edge
        guard var (ch, rate) = readAll() else { return }
        let len = ch[0].count, a = max(0, Int((s.double(p + ".start", 0) * Double(len)).rounded(.down))), b = min(len, Int((s.double(p + ".end", 1) * Double(len)).rounded(.up))); guard b > a else { return }
        let fade = min((b - a) / 2, Int((Double(rate) * 0.003).rounded()))
        for c in 0..<ch.count { for i in a..<b { var g = 1.0; if fade > 0 { if i - a < fade { g = 1 - Double(i - a) / Double(fade) } else if b - i < fade { g = 1 - Double(b - i) / Double(fade) } else { g = 0 } }; ch[c][i] = Float(Double(ch[c][i]) * g) } }
        apply(ch, rate: rate, suffix: " (cut)", reset: false); status = "Selection silenced."
    }
    private func normalize() {
        guard var (ch, rate) = readAll() else { return }
        let peak = ch.flatMap { $0 }.reduce(Float(0)) { max($0, abs($1)) }; guard peak > 0 else { status = "Silent sample — nothing to normalize."; return }
        for c in 0..<ch.count { for i in 0..<ch[c].count { ch[c][i] /= peak } }
        apply(ch, rate: rate, suffix: " (norm)", reset: false); status = "Normalized to full scale."
    }
    /// Transient k (waveform order, up to 16) becomes the one-shot of MPC pad k+1; later pads revert to their whole sample (web: Send Transients to MPC).
    private func sendToMpc() {
        let n = min(16, slices.count); guard n > 0 else { return }
        var o: [String: Any] = [:]; for k in 0..<n { o[String(k)] = ["osc": slot, "ti": k] as [String: Any]; UserDefaults.standard.set((UserDefaults.standard.string(forKey: "vv.mpc.assign") ?? (0..<16).map(String.init).joined(separator: ",")).split(separator: ",").enumerated().map { $0.offset == k ? String(slot) : String($0.element) }.joined(separator: ","), forKey: "vv.mpc.assign") }
        s.set("extra.mpcPadSlice", o)
    }
    private func detect() {   // the web's energy-onset detector (10 ms window, 5 ms hop), capped at 16 slices
        let n = data.count; guard n >= 64 else { return }
        let win = max(8, Int(sr * 0.01)), hop = max(4, Int(sr * 0.005)); var en: [(Int, Double)] = []
        var i = 0; while i + win < n { var sum = 0.0; for j in 0..<win { let v = Double(data[i + j]); sum += v * v }; en.append((i, (sum / Double(win)).squareRoot())); i += hop }
        var on: [(pos: Int, str: Double)] = []; for k in 1..<max(1, en.count) { let d = en[k].1 - en[k - 1].1; if d > 0.015 { on.append((en[k].0, d)) } }
        var merged: [(pos: Int, str: Double)] = []; let gap = sr * 0.05
        for o in on { if merged.isEmpty || Double(o.pos - merged[merged.count - 1].pos) > gap { merged.append(o) } else if o.str > merged[merged.count - 1].str { merged[merged.count - 1] = o } }
        var top = Array(merged.enumerated().sorted { $0.element.str != $1.element.str ? $0.element.str > $1.element.str : $0.offset < $1.offset }.map { $0.element }.prefix(Self.maxSlices))
        if top.count < min(4, Self.maxSlices) { top = (0..<min(8, Self.maxSlices)).map { (Int(Double($0) / Double(min(8, Self.maxSlices)) * Double(n)), 0) } }
        pushUndo(); sel = nil
        setSlices(top.sorted { $0.pos < $1.pos }.map { SliceMark(pos: min(max(Double($0.pos) / Double(n), 0), 0.99)) })
        status = "\(slices.count) found — drag any flag to adjust, tap one then ✕ to delete (UNDO restores)."
    }

    // MARK: slice list (full screen)
    @ViewBuilder private var sliceList: some View {
        Divider()
        if slices.isEmpty { Text("No slices yet — DETECT TRANSIENTS or + SLICE (up to 16).").font(.caption).foregroundStyle(.secondary) }
        ForEach(Array(slices.enumerated()), id: \.offset) { i, m in
            HStack(spacing: 8) {
                Text("#\(i + 1)").font(.caption.monospaced()).frame(width: 30).onTapGesture { sel = i }
                VStack(spacing: 0) { Text(String(format: "pos %.4f", m.pos)).font(.system(size: 9, design: .monospaced))
                    Slider(value: Binding(get: { slices.indices.contains(i) ? slices[i].pos : 0 }, set: { var v = slices; if v.indices.contains(i) { v[i].pos = $0; setSlices(v) } }), in: 0...1) }
                Toggle("REV", isOn: Binding(get: { slices.indices.contains(i) ? slices[i].reverse : false }, set: { var v = slices; if v.indices.contains(i) { v[i].reverse = $0; setSlices(v) } })).toggleStyle(.button).font(.caption2)
                VStack(spacing: 0) { Text(String(format: "pan %.2f", m.pan)).font(.system(size: 9, design: .monospaced))
                    Slider(value: Binding(get: { slices.indices.contains(i) ? slices[i].pan : 0 }, set: { var v = slices; if v.indices.contains(i) { v[i].pan = $0; setSlices(v) } }), in: -1...1) }.frame(width: 110)
                Text("PLAY").font(.caption2.bold()).padding(.horizontal, 10).padding(.vertical, 6).background(playing == i ? vvCyan.opacity(0.5) : Color.gray.opacity(0.25)).clipShape(RoundedRectangle(cornerRadius: 5))
                    .gesture(DragGesture(minimumDistance: 0).onChanged { _ in if playing != i { playing = i; s.auditionSlice(slot: slot, slice: i + 1) } }.onEnded { _ in playing = nil; s.auditionOff() })
                Button(role: .destructive) { removeSlice(i) } label: { Image(systemName: "trash") }.buttonStyle(.borderless)
            }.padding(.vertical, 2).background(selValid(slices.count) == i ? vvCyan.opacity(0.15) : Color.clear)
        }
    }
}
