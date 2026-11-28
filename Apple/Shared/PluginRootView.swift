// The SwiftUI interface shared by the standalone app and the AUv3 extension.
// Tabs: Play (keyboard, macros, wheels) · Sound (4 layers) · Mod (LFO / matrix / cross-mod / vocoder) · Seq (arp, patterns, chains)
// · FX rack · Presets · Settings · Host params (the 200 automatable AU parameters).
import SwiftUI
import UniformTypeIdentifiers

struct PluginRootView: View {
    @ObservedObject var store: ParameterStore
    @StateObject private var presets: PresetStore
    @State private var tab: Int
    @AppStorage("vv.scheme") private var scheme = 1   // changing the scheme rebuilds the UI so every vvCyan is re-read
    @AppStorage("vv.bg") private var bgChoice = 0
    @AppStorage("vv.panelOpacity") private var panelOpacityChoice = 1.0
    @AppStorage("vv.potStyle") private var potStyleChoice = 0
    init(store: ParameterStore) {
        self.store = store
        _presets = StateObject(wrappedValue: PresetStore(audioUnit: store.audioUnit))
        var startTab = store.audioUnit.uiValue("tab")                // restored from the host session (fullState)
        if let e = ProcessInfo.processInfo.environment["VV_START_TAB"], let n = Int(e) { startTab = n }   // CI screenshot aid only
        _tab = State(initialValue: startTab)
    }
    var body: some View {
        VStack(spacing: 0) {
        PresetBar(s: presets, showPresets: { tab = 5 })
        TopNav(tab: $tab)
        Group {
            switch tab {
            case 1: SoundView(s: presets)
            case 2: ModView(s: presets, page: 1)
            case 9: ModView(s: presets, page: 4)
            case 10: ModView(s: presets, page: 0)
            case 11: ModView(s: presets, page: 2)
            case 12: ModView(s: presets, page: 3)
            case 13: SoundView(s: presets)
            case 14: PlayView(store: store, presets: presets, showMod: { tab = 2 })
            case 4: FXRackView(s: presets)
            case 3: SequencerView(s: presets)
            case 8: MixerView(s: presets)
            case 5: PresetBrowserView(s: presets)
            case 6: SettingsView(s: presets)
            default: PlayView(store: store, presets: presets, showMod: { tab = 2 })
            }
        }.frame(maxWidth: .infinity, maxHeight: .infinity)
        .onChange(of: tab) { t in if t == 13 { presets.viewLayer = 4 } }   // C64 / SID nav icon opens the SID layer
        if tab != 0 && tab != 14 { KeyboardStrip(store: store, presets: presets) }   // web: the macro / keyboard strip stays on every screen
        PlayFooterView(s: presets, panic: { store.audioUnit.panic() })   // persistent footer, as in the web layout and the JUCE editor
        }
        .scrollContentBackground(.hidden)
        .background(AppBackground())
        .id("\(scheme)-\(bgChoice)-\(panelOpacityChoice)-\(potStyleChoice)")
        .preferredColorScheme(.dark)
        .tint(vvCyan)
        .onAppear { applyStoredGlobals(presets.audioUnit); MidiLearnModel(s: presets).applyActive() }
        .onChange(of: tab) { presets.audioUnit.setUIValue("tab", $0) }
        .onReceive(NotificationCenter.default.publisher(for: .vvUIStateRestored)) { _ in tab = presets.audioUnit.uiValue("tab") }
    }
}

/// The top preset bar (web: ◀ name ▶ · QUICK SAVE · SAVE AS · LOAD · INIT · UNDO · REDO).
struct PresetBar: View {
    @ObservedObject var s: PresetStore
    let showPresets: () -> Void
    @State private var msg = ""
    var body: some View {
        VStack(spacing: 2) {
            HStack(spacing: 6) {
                Button("◀") { msg = s.stepPreset(-1) }
                PresetTreeMenu(s: s, onLoaded: { msg = $0 }).frame(maxWidth: .infinity)
                Button("▶") { msg = s.stepPreset(1) }
                Button("QUICK SAVE") { msg = s.quickSave() }
                Button("SAVE AS") { showPresets(); s.requestSaveAs = true }
                Button("LOAD") { showPresets(); s.requestImport = true }
                Button("INIT") { msg = s.initPreset() }
                Menu("GENERATE") { ForEach(PresetStore.generatableCategories, id: \.self) { c in Button(c) { msg = s.generate(c) } } }
                Button("UNDO") { if s.undo() { msg = "Undo" } }.disabled(!s.canUndo)
                Button("REDO") { if s.redo() { msg = "Redo" } }.disabled(!s.canRedo)
            }.buttonStyle(WebButtonStyle()).padding(.horizontal, 8).padding(.top, 4)
            if !msg.isEmpty { Text(msg).font(.caption2).foregroundStyle(.secondary) }
        }
    }
}

/// Re-applies the instrument-wide settings that are not part of a preset (kept in UserDefaults) after the engine is (re)created.
func applyStoredGlobals(_ au: VectaAudioUnit) {
    guard let e = au.enginePointer else { return }
    let d = UserDefaults.standard
    for (name, def) in [("masterTone", 0.0), ("wtFmMix", 0.5), ("sampleMix", 0.0), ("bendRange", 2.0)] {
        let v = d.object(forKey: "vv.global." + name) as? Double ?? def
        vv_engine_set_global(e, name, v)
    }
    vv_engine_set_global(e, "sampleMixOn", d.bool(forKey: "vv.global.sampleMixOn") ? 1 : 0)
    vv_engine_set_mpe(e, d.bool(forKey: "vv.mpe") ? 1 : 0)
}

struct ParameterSlider: View {
    @ObservedObject var model: ParameterModel
    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack { Text(model.spec.label).font(.caption).lineLimit(1); Spacer(); Text(String(format: "%.2f", model.value)).font(.caption.monospacedDigit()).foregroundStyle(.secondary) }
            Slider(value: Binding(get: { Double(model.value) }, set: { model.set(Float($0)) }),
                   in: Double(model.spec.minValue)...Double(model.spec.maxValue))
        }
    }
}

struct PlayView: View {
    @ObservedObject var store: ParameterStore
    @ObservedObject var presets: PresetStore
    var showMod: () -> Void = {}
    @State private var showBendTargets = false
    @State private var showMpc = false
    @State private var showZones = false
    @State private var macros: [Double] = [0, 0, 0, 0]
    @State private var modWheel: Double = 0
    @State private var bend: Double = 0
    @AppStorage("vv.kb.hidden") private var hidden = false
    var body: some View {
        VStack(spacing: 12) {
            HStack(spacing: 8) {   // small logo at the top left, before the app name
                if let u = Bundle.main.url(forResource: "logo", withExtension: "png"), let d = try? Data(contentsOf: u), let ui = UIImage(data: d) { Image(uiImage: ui).resizable().scaledToFit().frame(height: 24) }
                Text("VEKTUR VORTEX").font(.system(size: 22, weight: .semibold, design: .monospaced)).foregroundStyle(.tint)
                Spacer()
            }.padding(.horizontal)
            Text("Hybrid Synthesiser").font(.caption).foregroundStyle(.secondary)
            ScopeView(audioUnit: store.audioUnit).frame(height: 190).padding(.horizontal)
            MacroTiles(store: store, size: 52).padding(.horizontal)
            Spacer()
            HStack(alignment: .bottom, spacing: 12) {
                VStack(spacing: 4) {
                    Text("BEND").font(.caption2.monospaced()).foregroundStyle(.secondary)
                    VerticalSlider(value: Binding(get: { bend }, set: { bend = $0; store.audioUnit.setPitchBend(Float($0)) }), range: -1...1, springBack: true)
                }.frame(width: 44, height: 160)
                VStack(spacing: 4) {
                    Text("MOD").font(.caption2.monospaced()).foregroundStyle(.secondary)
                    VerticalSlider(value: Binding(get: { modWheel }, set: { modWheel = $0; store.audioUnit.setModWheel(Float($0)) }), range: 0...1, springBack: false)
                }.frame(width: 44, height: 160)
                if !hidden { KeyboardView(audioUnit: store.audioUnit).frame(height: 160) }
            }
            HStack {
                Button("BEND TARGETS") { showBendTargets = true }
                Button("MOD TARGETS") { showMod() }   // the mod wheel's targets are its matrix connections
                Button("MPC PADS") { showMpc = true }
                Button("ZONES") { showZones = true }
            }.font(.caption).buttonStyle(.bordered).controlSize(.small)
            KeyboardOptions(s: presets, panic: { store.audioUnit.panic() })
        }.padding()
        .sheet(isPresented: $showBendTargets) { BendTargetsSheet(s: presets) }
        .sheet(isPresented: $showMpc) { MpcPadsView(s: presets) }
        .sheet(isPresented: $showZones) { ZonesView(s: presets) }
    }
}

/// Vertical slider built from a rotated horizontal one; optionally springs back to centre on release (pitch bend).
struct VerticalSlider: View {
    @Binding var value: Double
    let range: ClosedRange<Double>
    let springBack: Bool
    var body: some View {
        GeometryReader { geo in
            ZStack {
                RoundedRectangle(cornerRadius: 6).fill(Color.white.opacity(0.08))
                let t = (value - range.lowerBound) / (range.upperBound - range.lowerBound)
                RoundedRectangle(cornerRadius: 6).fill(Color.cyan.opacity(0.6)).frame(height: 14)
                    .position(x: geo.size.width / 2, y: geo.size.height * (1 - CGFloat(t)))
            }
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0).onChanged { g in
                let t = 1 - min(max(Double(g.location.y / geo.size.height), 0), 1)
                value = range.lowerBound + t * (range.upperBound - range.lowerBound)
            }.onEnded { _ in if springBack { value = (range.lowerBound + range.upperBound) / 2 } })
        }
    }
}

struct ParameterBrowserView: View {
    @ObservedObject var store: ParameterStore
    @State private var search = ""
    var body: some View {
        NavigationStack {
            List {
                ForEach(store.groups, id: \.name) { g in
                    let items = g.models.filter { search.isEmpty || $0.spec.label.localizedCaseInsensitiveContains(search) }
                    if !items.isEmpty {
                        SwiftUI.Section(g.name) { ForEach(items) { ParameterSlider(model: $0) } }
                    }
                }
            }
            .searchable(text: $search)
            .navigationTitle("Parameters")
        }
    }
}

/// Touch keyboard with the web app's options: scale highlight / scale lock, root, invert keys, note labels, octave count and shift, hide.
struct KeyboardView: View {
    let audioUnit: VectaAudioUnit
    @AppStorage("vv.kb.scale") private var scaleIdx = 0
    @AppStorage("vv.kb.root") private var root = 0
    @AppStorage("vv.kb.lock") private var lock = false
    @AppStorage("vv.kb.invert") private var invert = false
    @AppStorage("vv.kb.labels") private var labels = false
    @AppStorage("vv.kb.low") private var low = 48
    @AppStorage("vv.kb.octaves") private var octaves = 2
    @State private var down: Set<Int> = []
    // MPE drag-glide: with MPE on, press a key and drag to any other - the note glides (no retrigger) and a hue-per-octave line follows the finger.
    @AppStorage("vv.mpe") private var mpe = false
    /// Round 7: MPE is polyphonic - every finger owns its own note and glide (up to 10 at once; an 11th touch is ignored, like the web keyboard).
    struct Finger { var note: Int; var start: CGPoint; var cur: CGPoint; var p0 = 0.0 /* continuous pitch under the finger at touch-down: movement is measured from here, so the glide starts at 0 exactly where touch begins */; var semis = 0.0 /* target */; var applied = 0.0 /* eased value sent to the engine */ }
    static let maxPoly = 10
    @State private var fingers: [Int: Finger] = [:]
    private let glideTimer = Timer.publish(every: 1.0 / 120.0, on: .main, in: .common).autoconnect()
    private var count: Int { octaves * 12 + 1 }
    /// Hue sweeps once per octave (C red ... B violet); brightness steps with the octave.
    private func glideColor(_ p: Double, _ a: Double = 1) -> Color {
        let pc = (p.truncatingRemainder(dividingBy: 12) + 12).truncatingRemainder(dividingBy: 12), oct = Int((p / 12).rounded(.down))
        return Color(hue: pc / 12 * 300 / 360, saturation: 0.88, brightness: 0.96 + Double((oct % 3 + 3) % 3 - 1) * 0.04).opacity(a)
    }
    /// continuous MIDI pitch under x: linear between neighbouring key centres
    private func pitchAt(_ x: CGFloat, first: Int, whites: [Int], w: CGFloat) -> Double {
        var pts: [(CGFloat, Int)] = []
        for (i, idx) in whites.enumerated() { pts.append((CGFloat(i) * w + w / 2, first + idx)) }
        for idx in 0..<count where isBlack(first + idx) { pts.append((CGFloat(whites.filter { $0 < idx }.count) * w, first + idx)) }
        pts.sort { $0.0 < $1.0 }
        guard let f = pts.first, let l = pts.last else { return 60 }
        if x <= f.0 { return Double(f.1) }; if x >= l.0 { return Double(l.1) }
        for i in 1..<pts.count where x <= pts[i].0 { let a = pts[i - 1], b = pts[i]; return Double(a.1) + Double((x - a.0) / max(1e-6, b.0 - a.0)) * Double(b.1 - a.1) }
        return Double(l.1)
    }
    private func glideLine(_ ctx: inout GraphicsContext, _ size: CGSize) { for f in fingers.values { glideLine(&ctx, f) } }
    private func glideLine(_ ctx: inout GraphicsContext, _ f: Finger) {
        let glideSemis = f.semis
        let a = f.start, b = f.cur, dx = b.x - a.x, dy = b.y - a.y, len = hypot(dx, dy), p0 = Double(f.note), p1 = p0 + glideSemis
        if len > 3 {
            var o = Path(); o.move(to: a); o.addLine(to: b); ctx.stroke(o, with: .color(.black.opacity(0.55)), style: StrokeStyle(lineWidth: 7, lineCap: .round))
            let n = max(8, min(120, Int(len / 5)))
            for i in 0..<n {
                let f0 = CGFloat(i) / CGFloat(n), f1 = CGFloat(i + 1) / CGFloat(n), pm = p0 + (p1 - p0) * Double(f0 + f1) / 2
                var s = Path(); s.move(to: CGPoint(x: a.x + dx * f0, y: a.y + dy * f0)); s.addLine(to: CGPoint(x: a.x + dx * f1, y: a.y + dy * f1))
                ctx.stroke(s, with: .color(glideColor(pm)), style: StrokeStyle(lineWidth: 3.6, lineCap: .round))
            }
            let lo = min(p0, p1), hi = max(p0, p1), nx = -dy / len, ny = dx / len
            var k = (lo / 12).rounded(.up) * 12
            while k <= hi {   // a tick + label on every C crossed
                let f = CGFloat((k - p0) / ((p1 - p0) == 0 ? 1 : (p1 - p0)))
                if f > 0 && f < 1 {
                    let px = a.x + dx * f, py = a.y + dy * f; var t = Path(); t.move(to: CGPoint(x: px - nx * 8, y: py - ny * 8)); t.addLine(to: CGPoint(x: px + nx * 8, y: py + ny * 8))
                    ctx.stroke(t, with: .color(glideColor(k)), lineWidth: 2)
                    ctx.draw(Text("C\(Int(k / 12) - 1)").font(.system(size: 10, weight: .bold, design: .monospaced)).foregroundColor(glideColor(k)), at: CGPoint(x: px + nx * 18, y: py + ny * 18))
                }
                k += 12
            }
            ctx.draw(Text(String(format: "%+.1f st", glideSemis)).font(.system(size: 10, weight: .bold, design: .monospaced)).foregroundColor(glideColor(p1)), at: CGPoint(x: b.x + 34, y: b.y - 14))
        }
        let d0 = CGRect(x: a.x - 4, y: a.y - 4, width: 8, height: 8), d1 = CGRect(x: b.x - 6, y: b.y - 6, width: 12, height: 12)
        ctx.fill(Path(ellipseIn: d0), with: .color(glideColor(p0))); ctx.stroke(Path(ellipseIn: d0), with: .color(.black.opacity(0.7)), lineWidth: 1.5)
        ctx.fill(Path(ellipseIn: d1), with: .color(glideColor(p1))); ctx.stroke(Path(ellipseIn: d1), with: .color(.black.opacity(0.7)), lineWidth: 1.5)
    }
    private func hitNote(_ p: CGPoint, count: Int, first: Int, whites: [Int], w: CGFloat, h: CGFloat) -> Int? {
        guard p.x >= 0, whites.count > 0 else { return nil }
        for idx in 0..<count where isBlack(first + idx) {
            let wi = whites.filter { $0 < idx }.count, x0 = CGFloat(wi) * w - w * 0.3
            if p.x >= x0 && p.x <= x0 + w * 0.6 && p.y <= h * 0.6 { return first + idx }
        }
        return first + whites[min(max(Int(p.x / w), 0), whites.count - 1)]
    }
    private func isBlack(_ n: Int) -> Bool { [1, 3, 6, 8, 10].contains(n % 12) }
    private func allowed(_ n: Int) -> Bool { !(lock && scaleIdx > 0) || kbInScale(scaleIdx, root, n) }
    private let names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    private func fill(_ n: Int, black: Bool) -> Color {
        if down.contains(n) { return .cyan }
        let locked = lock && scaleIdx > 0, inS = kbInScale(scaleIdx, root, n)
        if locked { return inS ? (black ? Color(red: 0.10, green: 0.36, blue: 0.16) : Color(red: 0.72, green: 0.94, blue: 0.66)) : Color(white: 0.29) }
        let base: Color = black ? (invert ? Color(red: 0.91, green: 0.88, blue: 0.82) : .black) : (invert ? Color(red: 0.09, green: 0.08, blue: 0.06) : .white)
        return inS ? (black ? Color.green.opacity(0.55) : Color.green.opacity(0.35)) : base
    }
    private func label(_ n: Int, _ white: Bool) -> String? { (labels || (white && n % 12 == 0)) ? names[n % 12] + String(n / 12 - 1) : nil }
    var body: some View {
        GeometryReader { geo in
            let first = low
            let whites = (0..<count).filter { !isBlack(first + $0) }
            let w = geo.size.width / CGFloat(whites.count)
            ZStack(alignment: .topLeading) {
                ForEach(Array(whites.enumerated()), id: \.offset) { i, idx in
                    ZStack(alignment: .bottom) {
                        Rectangle().fill(fill(first + idx, black: false)).border(Color.black, width: 1)
                        if let t = label(first + idx, true) { Text(t).font(.system(size: 9, design: .monospaced)).foregroundColor(invert ? .white.opacity(0.7) : .black.opacity(0.6)).padding(.bottom, 3) }
                    }.frame(width: w, height: geo.size.height).offset(x: CGFloat(i) * w)
                }
                ForEach(0..<count, id: \.self) { idx in
                    if isBlack(first + idx) {
                        let wi = whites.filter { $0 < idx }.count
                        ZStack(alignment: .bottom) {
                            Rectangle().fill(fill(first + idx, black: true))
                            if let t = label(first + idx, false) { Text(t).font(.system(size: 8, design: .monospaced)).foregroundColor(invert ? .black.opacity(0.7) : .white.opacity(0.7)).padding(.bottom, 2) }
                        }.frame(width: w * 0.6, height: geo.size.height * 0.6).offset(x: CGFloat(wi) * w - w * 0.3)
                    }
                }
            }
            .overlay(Canvas { ctx, size in var c = ctx; glideLine(&c, size) }.allowsHitTesting(false))
            .onReceive(glideTimer) { _ in   // ease each finger toward its target at 120 Hz and push to the engine (the engine smooths again per block)
                guard mpe, !fingers.isEmpty else { return }
                for (id, var f) in fingers where f.applied != f.semis {
                    f.applied += (f.semis - f.applied) * 0.1; if abs(f.semis - f.applied) < 0.002 { f.applied = f.semis }
                    fingers[id] = f; if let e = audioUnit.enginePointer { vv_engine_mpe_glide(e, Int32(f.note), f.applied) }
                }
            }
            .overlay(MultiTouchSurface(
                onBegan: { id, p in
                    guard fingers.count < Self.maxPoly, let n = hitNote(p, count: count, first: first, whites: whites, w: w, h: geo.size.height), allowed(n), !down.contains(n) else { return }
                    down.insert(n); audioUnit.sendMIDI(0x90, UInt8(n), 100)
                    fingers[id] = Finger(note: n, start: p, cur: p, p0: pitchAt(p.x, first: first, whites: whites, w: w))
                    if mpe, let e = audioUnit.enginePointer { vv_engine_mpe_glide(e, Int32(n), 0) }
                },
                onMoved: { id, p in
                    guard var f = fingers[id] else { return }
                    if mpe {   // MPE: dragging glides this finger's note instead of retriggering
                        f.cur = p; f.semis = max(-60, min(60, pitchAt(p.x, first: first, whites: whites, w: w) - f.p0)); fingers[id] = f
                    } else if let n = hitNote(p, count: count, first: first, whites: whites, w: w, h: geo.size.height), n != f.note, allowed(n), !down.contains(n) {   // classic slide: retrigger
                        audioUnit.sendMIDI(0x80, UInt8(f.note), 0); down.remove(f.note); down.insert(n); audioUnit.sendMIDI(0x90, UInt8(n), 100); f.note = n; f.cur = p; fingers[id] = f
                    }
                },
                onEnded: { id in
                    guard let f = fingers.removeValue(forKey: id) else { return }
                    audioUnit.sendMIDI(0x80, UInt8(f.note), 0); down.remove(f.note)
                    if let e = audioUnit.enginePointer { vv_engine_mpe_glide(e, Int32(f.note), 0) }
                }))
        }
    }
}

/// Multi-touch surface: reports every finger (UITouch identity) separately so the keyboard can play and glide up to 10 notes at once.
#if canImport(UIKit)
import UIKit
struct MultiTouchSurface: UIViewRepresentable {
    var onBegan: (Int, CGPoint) -> Void, onMoved: (Int, CGPoint) -> Void, onEnded: (Int) -> Void
    final class V: UIView {
        var began: ((Int, CGPoint) -> Void)?, moved: ((Int, CGPoint) -> Void)?, ended: ((Int) -> Void)?
        override init(frame: CGRect) { super.init(frame: frame); isMultipleTouchEnabled = true; backgroundColor = .clear }
        required init?(coder: NSCoder) { fatalError() }
        override func touchesBegan(_ t: Set<UITouch>, with e: UIEvent?) { for x in t { began?(x.hash, x.location(in: self)) } }
        override func touchesMoved(_ t: Set<UITouch>, with e: UIEvent?) { for x in t { moved?(x.hash, x.location(in: self)) } }
        override func touchesEnded(_ t: Set<UITouch>, with e: UIEvent?) { for x in t { ended?(x.hash) } }
        override func touchesCancelled(_ t: Set<UITouch>, with e: UIEvent?) { for x in t { ended?(x.hash) } }
    }
    func makeUIView(context: Context) -> V { V() }
    func updateUIView(_ v: V, context: Context) { v.began = onBegan; v.moved = onMoved; v.ended = onEnded }
}
#else
import AppKit
struct MultiTouchSurface: NSViewRepresentable {   // macOS: a mouse is one pointer
    var onBegan: (Int, CGPoint) -> Void, onMoved: (Int, CGPoint) -> Void, onEnded: (Int) -> Void
    final class V: NSView {
        var began: ((Int, CGPoint) -> Void)?, moved: ((Int, CGPoint) -> Void)?, ended: ((Int) -> Void)?
        override var isFlipped: Bool { true }
        override func mouseDown(with e: NSEvent) { began?(0, convert(e.locationInWindow, from: nil)) }
        override func mouseDragged(with e: NSEvent) { moved?(0, convert(e.locationInWindow, from: nil)) }
        override func mouseUp(with e: NSEvent) { ended?(0) }
    }
    func makeNSView(context: Context) -> V { V() }
    func updateNSView(_ v: V, context: Context) { v.began = onBegan; v.moved = onMoved; v.ended = onEnded }
}
#endif

/// The row under the keyboard: Labels, Scale, Root, Scale lock, Octaves, OCT -/+, Invert keys, Hide keyboard.
struct KeyboardOptions: View {
    @AppStorage("vv.kb.scale") private var scaleIdx = 0
    @AppStorage("vv.kb.root") private var root = 0
    @AppStorage("vv.kb.lock") private var lock = false
    @AppStorage("vv.kb.invert") private var invert = false
    @AppStorage("vv.kb.low") private var low = 48
    @AppStorage("vv.kb.octaves") private var octaves = 2
    @AppStorage("vv.kb.hidden") private var hidden = false
    @ObservedObject var s: PresetStore
    let panic: () -> Void
    private let names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    var body: some View {
        HStack(spacing: 8) {   // (RECORDER + PANIC! moved to the footer's options row in Round 12)
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 8) {
                Picker("Scale", selection: $scaleIdx) { ForEach(0..<kbScales.count, id: \.self) { Text(kbScales[$0].label).tag($0) } }.pickerStyle(.menu)
                Picker("Root", selection: $root) { ForEach(0..<12, id: \.self) { Text(names[$0]).tag($0) } }.pickerStyle(.menu)
                Toggle("Scale lock", isOn: $lock).toggleStyle(WebSwitchStyle()).fixedSize()
                Picker("Octaves", selection: $octaves) { ForEach(1...3, id: \.self) { Text("\($0) oct").tag($0) } }.pickerStyle(.menu)
                // Round 9 (web parity): PLAY, HIDE KEYBOARD and OCT ▲ / ▼ sit immediately right of the Octaves menu
                Button(s.transportRunning ? "STOP" : "PLAY") { s.setTransport(!s.transportRunning) }.tint(Color(red: 0.18, green: 0.66, blue: 0.33))
                Button(hidden ? "SHOW KEYBOARD" : "HIDE KEYBOARD") { hidden.toggle() }
                Button("OCT ▲") { low = min(84, low + 12) }
                Button("OCT ▼") { low = max(24, low - 12) }
                Button("INVERT KEYS") { invert.toggle() }.tint(invert ? vvCyan : nil)
            }.buttonStyle(WebButtonStyle())
        }
        }
    }
}

/// Which oscillators the pitch wheel bends (web: TARGETS next to the pitch wheel). Stored in the preset as extra.pitchBendTargets.
struct BendTargetsSheet: View {
    @ObservedObject var s: PresetStore
    @Environment(\.dismiss) private var dismiss
    private let groups: [(key: String, title: String, n: Int)] = [("WT", "Wavetable", 2), ("FM", "FM engine", 2), ("SMP", "Sampler", 16), ("PRO", "Procedural", 2)]
    var body: some View {
        NavigationStack {
            Form {
                ForEach(groups, id: \.key) { g in
                    SwiftUI.Section(g.title) {
                        ForEach(0..<g.n, id: \.self) { i in
                            Toggle(g.key == "FM" ? "FM \(i == 0 ? "A" : "B")" : "\(g.key) \(i + 1)", isOn: Binding(
                                get: { s.value("extra.pitchBendTargets.\(g.key).\(i)") as? Bool ?? true },
                                set: { s.set("extra.pitchBendTargets.\(g.key).\(i)", $0) }))
                        }
                    }
                }
            }
            .navigationTitle("Pitch-bend targets")
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
        }
    }
}


/// Top-bar preset name: tapping it opens a tree - Factory / User Presets / Banks, then bank, category and preset.
struct PresetTreeMenu: View {
    @ObservedObject var s: PresetStore
    let onLoaded: (String) -> Void
    @State private var entries: [PresetEntry] = []
    private func banks(_ f: [PresetEntry]) -> [String] { Array(Set(f.map { $0.artist })).sorted { $0.localizedCaseInsensitiveCompare($1) == .orderedAscending } }
    @ViewBuilder private func bankMenu(_ bank: String, _ list: [PresetEntry]) -> some View {
        Menu(bank + " (\(list.count))") {
            ForEach(presetCategories, id: \.self) { cat in
                let items = list.filter { $0.category == cat }.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
                if !items.isEmpty {
                    Menu(cat + " (\(items.count))") { ForEach(items) { p in Button(p.name == s.presetName ? "✓ " + p.name : p.name) { load(p) } } }
                }
            }
        }
    }
    private func load(_ p: PresetEntry) {
        guard let text = try? String(contentsOf: p.url) else { onLoaded("Could not read \(p.name)."); return }
        if s.load(json: text) { s.presetName = p.name; onLoaded("Loaded \(p.name)") } else { onLoaded("Could not load \(p.name).") }
    }
    var body: some View {
        let factory = entries.filter { $0.factory }, user = entries.filter { !$0.factory && $0.artist == "My presets" }, other = entries.filter { !$0.factory && $0.artist != "My presets" }
        Menu {
            if !factory.isEmpty { Menu("Factory") { ForEach(banks(factory), id: \.self) { b in bankMenu(b, factory.filter { $0.artist == b }) } } }
            if !user.isEmpty { bankMenu("User Presets", user) }
            if !other.isEmpty { Menu("Banks") { ForEach(banks(other), id: \.self) { b in bankMenu(b, other.filter { $0.artist == b }) } } }
            if entries.isEmpty { Text("No presets yet") }
        } label: {
            HStack(spacing: 4) { Text(s.presetName.isEmpty ? "(none loaded)" : s.presetName).lineLimit(1); Image(systemName: "chevron.down").font(.system(size: 9)) }
                .font(.caption.monospaced()).foregroundStyle(vvCyan)
        }
        .onAppear { entries = PresetLibrary.scan() }
        .onChange(of: s.presetName) { _ in entries = PresetLibrary.scan() }
    }
}


/// The web app's top icon navigation: a row of rounded glass buttons, the active one outlined in the accent colour.
struct TopNav: View {
    @Binding var tab: Int
    /// The web nav, in web order: Oscillators, XY, Matrix, FX, Sequencer, LFO, Cross Mod, Vocoder, Scope, Mixer, C64/SID, Spectrogram, Presets, Settings.
    private let items: [(NavIcon?, String, Int)] = [(.osc, "Oscillators", 1), (.xy, "XY Pads", 9), (.matrix, "Matrix", 2), (.fx, "FX", 4), (.seq, "Sequencer", 3), (.lfo, "LFO", 10), (.crossMod, "Cross Mod", 11), (.vocoder, "Vocoder", 12), (.scope, "Scope", 0), (.mixer, "Mixer", 8), (.c64, "C64 / SID", 13), (.spectrogram, "Spectrogram", 14), (.presets, "Presets", 5), (.settings, "Settings", 6)]
    var body: some View {
        HStack(spacing: 4) {
            ForEach(items, id: \.2) { it in
                let on = tab == it.2
                Button { tab = it.2 } label: {
                    Group {
                        if let ic = it.0 { ic.image(points: 22) } else { Image(systemName: it.2 == 0 ? "pianokeys" : "slider.horizontal.3").font(.system(size: 18)) }
                    }
                    .frame(maxWidth: .infinity, minHeight: 36)
                    .foregroundStyle(on ? vvCyan : Color.white.opacity(0.75))
                    .background(RoundedRectangle(cornerRadius: 8).fill(on ? vvCyan.opacity(0.14) : Color.black.opacity(0.35)))
                    .overlay(RoundedRectangle(cornerRadius: 8).stroke(on ? vvCyan : Color.white.opacity(0.18), lineWidth: on ? 1.5 : 1))
                }.buttonStyle(.plain).accessibilityLabel(it.1)
            }
        }.padding(.horizontal, 8).padding(.vertical, 4)
    }
}


/// Persistent macro + keyboard strip (web: MACRO 1-4, MASTER VOLUME / TONE, pitch and mod wheels, keyboard) shown under every screen except Play.
struct KeyboardStrip: View {
    @ObservedObject var store: ParameterStore
    @ObservedObject var presets: PresetStore
    @State private var showBend = false
    @State private var showZones = false
    @State private var macros: [Double] = [0, 0, 0, 0]
    @State private var modWheel: Double = 0
    @State private var bend: Double = 0
    @AppStorage("vv.kb.hidden") private var hidden = false
    var body: some View {
        VStack(spacing: 4) {
            HStack(spacing: 8) {
                MacroTiles(store: store, size: 38)
                Spacer(minLength: 4)
            }
            HStack(spacing: 8) {   // web row: ARP · ZONES · scale · lock · root · octaves · PLAY · HIDE KEYBOARD · OCT
                Text("ARP").font(.system(size: 11, weight: .semibold, design: .monospaced)).foregroundStyle(vvCyan)
                Toggle("On", isOn: presets.binding("arp.on", bool: false)).font(.system(size: 11, design: .monospaced)).toggleStyle(WebSwitchStyle()).fixedSize().scaleEffect(0.8)
                Button("ZONES ▸") { showZones = true }.buttonStyle(WebButtonStyle())
                KeyboardOptions(s: presets, panic: { store.audioUnit.panic() })
            }
            if !hidden {
                HStack(spacing: 8) {
                    VStack(spacing: 2) { Button("TARGETS") { showBend = true }.buttonStyle(WebButtonStyle()).font(.system(size: 9)); VerticalSlider(value: Binding(get: { bend }, set: { bend = $0; store.audioUnit.setPitchBend(Float($0)) }), range: -1...1, springBack: true).frame(width: 30, height: 72) }
                    VerticalSlider(value: Binding(get: { modWheel }, set: { modWheel = $0; store.audioUnit.setModWheel(Float($0)) }), range: 0...1, springBack: false).frame(width: 30, height: 96)
                    KeyboardView(audioUnit: store.audioUnit).frame(height: 96)
                }
            }
        }
        .sheet(isPresented: $showBend) { BendTargetsSheet(s: presets) }
        .sheet(isPresented: $showZones) { ZonesView(s: presets) }
        .padding(.horizontal, 10).padding(.vertical, 6)
        .background(Color.black.opacity(0.55))
        .overlay(alignment: .top) { Rectangle().fill(vvCyan.opacity(0.4)).frame(height: 1) }
    }
}


/// Web button: dark rectangle, thin grey border, monospaced uppercase label.
struct WebButtonStyle: ButtonStyle {
    var active = false
    func makeBody(configuration: Configuration) -> some View {
        configuration.label.font(.system(size: 11, design: .monospaced)).foregroundStyle(Color.white.opacity(0.9))
            .foregroundStyle(Color(red: 230/255, green: 237/255, blue: 243/255)).padding(.horizontal, 10).frame(height: 27)
            .background(RoundedRectangle(cornerRadius: 4).fill(Color(red: 27/255, green: 31/255, blue: 42/255).opacity(configuration.isPressed ? 0.6 : 1)))
            .overlay(RoundedRectangle(cornerRadius: 4).stroke(active ? vvCyan : Color(red: 42/255, green: 47/255, blue: 61/255), lineWidth: 1))
    }
}


extension View {
    /// Web button look for non-Button labels (Menu labels).
    func webButtonLook() -> some View {
        self.font(.system(size: 11, design: .monospaced)).foregroundStyle(Color(red: 230/255, green: 237/255, blue: 243/255)).padding(.horizontal, 10).frame(height: 27)
            .background(RoundedRectangle(cornerRadius: 4).fill(Color(red: 27/255, green: 31/255, blue: 42/255)))
            .overlay(RoundedRectangle(cornerRadius: 4).stroke(Color(red: 42/255, green: 47/255, blue: 61/255), lineWidth: 1))
    }
}
