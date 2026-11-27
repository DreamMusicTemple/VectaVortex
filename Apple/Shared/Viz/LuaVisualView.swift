// LUA Visuals mode for the Scope window: Metal view + hideable control panel (presets, quality, parameters with band assignment, audio analysis, script editor).
// NOTE: written without a Mac. Not compiled yet.
import SwiftUI
import MetalKit
import UniformTypeIdentifiers
import AVFoundation

// ------------------------------------------------------------------ Metal view
struct VizMetalView: UIViewRepresentable {
    let host: VizHost; let audioUnit: VectaAudioUnit; let stats: VizStats
    @AppStorage("vv.viz.quality") private var quality = "auto"
    @AppStorage("vv.viz.fps") private var fps = 60
    @AppStorage("vv.viz.res") private var res = "auto"
    @AppStorage("vv.viz.aa") private var aa = "auto"

    func makeCoordinator() -> Coord { Coord(host: host, audioUnit: audioUnit, stats: stats) }
    func makeUIView(context: Context) -> MTKView {
        let v = MTKView(frame: .zero, device: MTLCreateSystemDefaultDevice()); v.delegate = context.coordinator; v.framebufferOnly = true; v.colorPixelFormat = .bgra8Unorm
        v.autoResizeDrawable = false; v.isPaused = false; v.enableSetNeedsDisplay = false; v.contentScaleFactor = min(UIScreen.main.scale, 2)
        context.coordinator.view = v; context.coordinator.renderer = VizMetalRenderer(device: v.device); return v
    }
    func updateUIView(_ v: MTKView, context: Context) {
        let c = context.coordinator; c.qualityPref = quality; c.aaPref = aa; c.resPref = res; c.fpsPref = fps
        v.preferredFramesPerSecond = max(15, min(fps, UIScreen.main.maximumFramesPerSecond)); c.dirty = true
    }
    static func dismantleUIView(_ v: MTKView, coordinator: Coord) { v.isPaused = true; v.delegate = nil }

    @MainActor final class Coord: NSObject, MTKViewDelegate {
        let host: VizHost; let audioUnit: VectaAudioUnit; let stats: VizStats; weak var view: MTKView?; var renderer: VizMetalRenderer?
        var qualityPref = "auto", aaPref = "auto", resPref = "auto", fpsPref = 60, dirty = true
        private var qIdx = 2, auto = true, last = CACurrentMediaTime(), slowT: Float = 0, fastT: Float = 0, settleUntil = CACurrentMediaTime() + 1.8
        private var drawDt: Float = 1.0 / 60, ms: Float = 0, frameNo = 0, lastUI = CACurrentMediaTime(), t0 = CACurrentMediaTime(), bounds = CGSize(width: 1, height: 1)
        private var curSize = (w: 0, h: 0)
        init(host: VizHost, audioUnit: VectaAudioUnit, stats: VizStats) { self.host = host; self.audioUnit = audioUnit; self.stats = stats }
        func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

        private func targetSize(_ v: MTKView) -> (Int, Int) {
            let cw = max(64, v.bounds.width * v.contentScaleFactor), ch = max(64, v.bounds.height * v.contentScaleFactor); let Q = VizQuality.table[qIdx]
            var h: CGFloat
            if resPref == "auto" { h = ch * CGFloat(Q.scale) } else if let n = Double(resPref), n >= 144 { h = CGFloat(n) } else if let k = Double(resPref) { h = ch * CGFloat(k) } else { h = ch * CGFloat(Q.scale) }
            h = max(144, min(h, 2160)); var w = h * cw / ch; if w * h > 8.4e6 { let k = (8.4e6 / (w * h)).squareRoot(); w *= k; h *= k }; return (Int(w.rounded()), Int(h.rounded()))
        }
        func draw(in view: MTKView) {
            guard let r = renderer else { return }
            let now = CACurrentMediaTime(); let dt = Float(max(0.001, min(0.1, now - last))); last = now; let cap = Float(min(fpsPref, UIScreen.main.maximumFramesPerSecond)); frameNo += 1
            if qualityPref == "auto" { auto = true; if qIdx > 2 { qIdx = 2 } } else { auto = false; qIdx = max(0, VizQuality.table.firstIndex { $0.id == qualityPref } ?? 2) }
            let Q = VizQuality.table[qIdx]
            let ts = targetSize(view); if dirty || ts.0 != curSize.w || ts.1 != curSize.h { curSize = (ts.0, ts.1); view.drawableSize = CGSize(width: ts.0, height: ts.1); r.resize(w: ts.0, h: ts.1, bloomDiv: Q.bloomDiv); dirty = false }
            guard let drawable = view.currentDrawable, let v = host.v, host.meta != nil else { return }
            let tStart = CACurrentMediaTime()
            let snap = audioUnit.scopeSnapshot(2048); let sr = Float(AVAudioSession.sharedInstance().sampleRate > 0 ? AVAudioSession.sharedInstance().sampleRate : 48000)
            var ok = true
            snap.l.withUnsafeBufferPointer { lp in snap.r.withUnsafeBufferPointer { rp in
                ok = vv_viz_frame(v, dt, lp.baseAddress, rp.baseAddress, Int32(snap.l.count), sr, Int32(ts.0), Int32(ts.1), Int32(Q.q), Int32(cap), Float(ts.1) / Float(max(1, view.bounds.height * view.contentScaleFactor)), host.demo ? 1 : 0) == 1 } }
            if !ok && !host.halted { host.halted = true; host.error = "Script stopped: \(String(cString: vv_viz_error(v))) - the last picture is held. Fix the script and press RUN, or pick another preset." }
            let uni = vv_viz_uniforms(v)!, post = vv_viz_post(v)!
            var K = [Float](repeating: 0, count: 56); K[3] = Float(Q.q); let bands = vv_viz_bands(v)!; for i in 0..<9 { K[8 + i] = bands[i] }; for i in 0..<32 { K[24 + i] = uni[i] }
            let shader = host.meta?.shader ?? "none"; let isSand = shader == "sand_plate"
            var sand: (n: Int, size: Float, density: Float)? = nil
            if isSand { let n = max(64, min(Q.sand, Int((Float(ts.0 * ts.1) * 0.45).squareRoot().rounded(.up)))); sand = (n, uni[28] > 0 ? uni[28] : 1.6, uni[30] > 0 ? uni[30] : 1) }
            let useAA = aaPref == "auto" ? Q.aa : aaPref == "fxaa"
            let frame = VizFrameInput(shader: shader == "none" ? nil : shader, K: K, post: Array(UnsafeBufferPointer(start: post, count: 12)), cmds: vv_viz_ncmd(v) > 0 && !host.halted ? vv_viz_cmds(v) : nil, ncmd: host.halted ? 0 : Int(vv_viz_ncmd(v)), scriptW: Float(ts.0), scriptH: Float(ts.1), time: Float(now - t0), dt: dt, aa: useAA, sand: sand)
            r.setDrawableFormat(drawable.texture.pixelFormat); _ = r.render(frame, into: drawable)
            let used = Float(CACurrentMediaTime() - tStart) * 1000; ms = ms * 0.9 + used * 0.1; drawDt = drawDt * 0.92 + dt * 0.08
            // auto quality: drop when frames run long, climb back slowly
            if auto && now > settleUntil { let target = 1.0 / cap; let ratio = drawDt / target
                if ratio > 1.3 { slowT += dt; fastT = 0 } else if ratio < 1.12 && ms < target * 450 { fastT += dt; slowT = max(0, slowT - dt) } else { slowT = max(0, slowT - dt * 0.5); fastT = 0 }
                if slowT > 1.0 && qIdx > 0 { qIdx -= 1; slowT = 0; dirty = true; settleUntil = now + 1.5 } else if fastT > 5 && qIdx < 2 { qIdx += 1; fastT = 0; dirty = true; settleUntil = now + 1.5 } }
            if now - lastUI > 0.5 { lastUI = now; let st = VizStatsSnapshot(fps: 1 / max(0.001, drawDt), ms: ms, w: ts.0, h: ts.1, quality: Q.label + (auto ? " (auto)" : ""), updateInstr: Int(vv_viz_last_update(v)), renderInstr: Int(vv_viz_last_render(v)), bands: (0..<9).map { bands[$0] }, beat: vv_viz_beat(v), displayHz: UIScreen.main.maximumFramesPerSecond)
                DispatchQueue.main.async { self.stats.s = st } }
        }
    }
}
struct VizStatsSnapshot { var fps: Float = 0, ms: Float = 0; var w = 0, h = 0; var quality = ""; var updateInstr = 0, renderInstr = 0; var bands = [Float](repeating: 0, count: 9); var beat: Float = 0; var displayHz = 60 }
final class VizStats: ObservableObject { @Published var s = VizStatsSnapshot() }

// ------------------------------------------------------------------ the mode view
struct LuaVisualView: View {
    let audioUnit: VectaAudioUnit
    @StateObject private var host = VizHost()
    @StateObject private var stats = VizStats()
    @AppStorage("vv.viz.controls") private var controls = true
    @AppStorage("vv.viz.autohide") private var autoHide = 0            // seconds, 0 = never
    @AppStorage("vv.viz.quality") private var quality = "auto"
    @AppStorage("vv.viz.fps") private var fps = 60
    @AppStorage("vv.viz.res") private var res = "auto"
    @AppStorage("vv.viz.aa") private var aa = "auto"
    @State private var hideTask: Task<Void, Never>?
    @State private var askName: String?; @State private var askKind = ""; @State private var nameText = ""
    @State private var showImport = false; @State private var exportURL: URL?; @State private var genText = ""; @State private var showGen = false
    @State private var assignIdx: Int?; @State private var script = ""; @State private var scriptMsg = ""
    @State private var banner: String?

    var body: some View {
        ZStack(alignment: .topTrailing) {
            VizMetalView(host: host, audioUnit: audioUnit, stats: stats)
                .contentShape(Rectangle()).onTapGesture { if !controls { controls = true }; armHide() }
            if controls { panel.transition(.move(edge: .trailing)) }
            Button(controls ? "HIDE CONTROLS" : "SHOW CONTROLS") { controls.toggle(); armHide() }
                .font(.system(size: 10, design: .monospaced)).buttonStyle(.bordered).controlSize(.mini).padding(6).opacity(controls ? 1 : 0.5)
            VStack { Spacer(); if let e = host.error ?? banner { Text(e).font(.system(size: 11)).padding(8).background(Color.red.opacity(0.85)).cornerRadius(6).padding(8) } }
        }
        .background(Color.black).clipShape(RoundedRectangle(cornerRadius: 8))
        .onAppear { armHide(); script = host.src }
        .onChange(of: host.src) { script = $0 }
        .alert(askKind, isPresented: Binding(get: { askName != nil }, set: { if !$0 { askName = nil } })) {
            TextField("Name", text: $nameText); Button("OK") { let n = nameText.trimmingCharacters(in: .whitespaces); if !n.isEmpty { if askKind == "Rename preset" { host.rename(n) } else { host.save(asNew: true, name: n, display: display) } }; askName = nil }; Button("Cancel", role: .cancel) { askName = nil }
        }
        .alert("Generate a visual", isPresented: $showGen) { TextField("e.g. calm blue liquid ripples", text: $genText); Button("GENERATE") { banner = host.generate(genText); if banner != nil { DispatchQueue.main.asyncAfter(deadline: .now() + 3) { banner = nil } } }; Button("Cancel", role: .cancel) {} } message: { Text("Picks the closest factory algorithm and tunes its parameters from your words (offline, template based - it does not write new algorithms).") }
        .fileImporter(isPresented: $showImport, allowedContentTypes: [.json, .plainText, .data]) { r in if case .success(let u) = r, u.startAccessingSecurityScopedResource() { defer { u.stopAccessingSecurityScopedResource() }; if let d = try? Data(contentsOf: u) { banner = host.importData(d, fileName: u.lastPathComponent); if banner != nil { DispatchQueue.main.asyncAfter(deadline: .now() + 3) { banner = nil } } } } }
        .sheet(isPresented: Binding(get: { assignIdx != nil }, set: { if !$0 { assignIdx = nil } })) { if let i = assignIdx { assignSheet(i) } }
    }
    private var display: VizDisplay { VizDisplay(quality: quality, fps: fps, res: res, aa: aa) }
    private func armHide() { hideTask?.cancel(); guard autoHide > 0, controls else { return }; hideTask = Task { try? await Task.sleep(nanoseconds: UInt64(autoHide) * 1_000_000_000); if !Task.isCancelled { await MainActor.run { controls = false } } } }

    // ---- panel
    private var panel: some View {
        ScrollView { VStack(alignment: .leading, spacing: 8) {
            Spacer().frame(height: 22)
            Menu { ForEach(host.factory) { p in Button(p.name) { host.load(p.id) } }; if !host.user.isEmpty { Divider(); ForEach(host.user) { p in Button(p.name) { host.load(p.id) } } } } label: { Label(host.preset?.name ?? "Preset", systemImage: "chevron.up.chevron.down").frame(maxWidth: .infinity, alignment: .leading) }.buttonStyle(.bordered)
            if let d = host.meta?.desc, !d.isEmpty { Text(d).font(.caption2).foregroundColor(.secondary) }
            HStack { btn("RANDOMISE") { host.randomiseValues() }; btn("RESET") { host.resetParams() }; btn("♪ RANDOM") { host.randomiseAssign() }; btn("♪ CLEAR") { host.clearAssign() } }
            HStack { btn("SAVE") { host.save(asNew: false, display: display) }; btn("SAVE AS") { askKind = "Save preset as"; nameText = (host.preset?.name ?? "Preset") + " copy"; askName = nameText }; btn("DUPLICATE") { askKind = "Save preset as"; nameText = (host.preset?.name ?? "Preset") + " copy"; askName = nameText } }
            HStack { btn("RENAME") { if host.preset?.factory == true { flash("Factory presets cannot be renamed - use Save As.") } else { askKind = "Rename preset"; nameText = host.preset?.name ?? ""; askName = nameText } }
                btn("DELETE") { if host.preset?.factory == true { flash("Factory presets cannot be deleted.") } else { host.deleteCurrent() } }; btn("IMPORT") { showImport = true }; exportButton }
            HStack { btn("GENERATE") { showGen = true }; btn("RESTORE FACTORY") { host.restoreFactory() } }
            Toggle("Preview without sound", isOn: $host.demo).font(.caption)
            statRow
            DisclosureGroup("Display") { display_ }.font(.caption)
            DisclosureGroup("Audio analysis") { audio_ }.font(.caption)
            ForEach(groups, id: \.self) { g in DisclosureGroup(g) { VStack { ForEach(indices(in: g), id: \.self) { paramRow($0) } } }.font(.caption) }
            DisclosureGroup("Script") { scriptEditor }.font(.caption)
        }.padding(8) }
        .frame(width: 300).background(.ultraThinMaterial).onTapGesture { armHide() }
    }
    private func flash(_ s: String) { banner = s; DispatchQueue.main.asyncAfter(deadline: .now() + 2.5) { banner = nil } }
    private func btn(_ t: String, _ a: @escaping () -> Void) -> some View { Button(t) { a(); armHide() }.font(.system(size: 9, design: .monospaced)).buttonStyle(.bordered).controlSize(.mini) }
    @ViewBuilder private var exportButton: some View { if let d = host.exportData() { ShareLink(item: VizFile(data: d, name: (host.preset?.name ?? "preset") + ".vizpreset.json"), preview: SharePreview(host.preset?.name ?? "preset")) { Text("EXPORT").font(.system(size: 9, design: .monospaced)) }.buttonStyle(.bordered).controlSize(.mini) } }
    private var statRow: some View {
        let s = stats.s; let limit = Float(fps) > Float(s.displayHz) * 1.05 ? " (display refreshes at \(s.displayHz) Hz)" : ""
        return VStack(alignment: .leading, spacing: 2) {
            Text(String(format: "%.0f fps%@  %dx%d  %@", s.fps, limit, s.w, s.h, s.quality)).font(.system(size: 9, design: .monospaced))
            Text("script \(s.updateInstr)+\(s.renderInstr) instr   gpu+cpu \(String(format: "%.1f", s.ms)) ms").font(.system(size: 9, design: .monospaced)).foregroundColor(.secondary)
            HStack(alignment: .bottom, spacing: 2) { ForEach(0..<9, id: \.self) { i in Rectangle().fill(Color.cyan).frame(width: 8, height: max(1, CGFloat(s.bands[i]) * 22)) }; Circle().fill(Color.orange.opacity(0.25 + Double(s.beat) * 0.75)).frame(width: 8, height: 8) }.frame(height: 24, alignment: .bottom)
        }
    }
    private var display_: some View {
        VStack(alignment: .leading) {
            pick("Quality", $quality, [("auto", "Auto"), ("performance", "Performance"), ("balanced", "Balanced"), ("high", "High"), ("ultra", "Ultra")])
            HStack { Text("Frame rate"); Picker("", selection: $fps) { ForEach([30, 60, 120], id: \.self) { Text("\($0)").tag($0) } }.pickerStyle(.segmented) }
            pick("Resolution", $res, [("auto", "Auto"), ("0.5", "Half"), ("0.75", "3/4"), ("1", "Native"), ("720", "720p"), ("1080", "1080p")])
            pick("Anti-aliasing", $aa, [("auto", "Auto"), ("off", "Off"), ("fxaa", "FXAA-lite")])
            Picker("Controls auto-hide", selection: $autoHide) { Text("Never").tag(0); Text("5 s").tag(5); Text("10 s").tag(10); Text("20 s").tag(20) }.font(.caption)
            Text("120 fps needs a 120 Hz display (ProMotion iPad); otherwise the display rate is the limit.").font(.caption2).foregroundColor(.secondary)
        }
    }
    private func pick(_ t: String, _ b: Binding<String>, _ o: [(String, String)]) -> some View { Picker(t, selection: b) { ForEach(o, id: \.0) { Text($0.1).tag($0.0) } }.font(.caption) }
    private var audio_: some View {
        VStack(alignment: .leading) {
            Toggle("Analysis on (bypass off)", isOn: bind(\.enabled)); Toggle("Log response", isOn: bind(\.logResponse)); Toggle("Stereo analysis", isOn: bind(\.stereo))
            slider("Sensitivity", fbind(\.sensitivity), 0, 3); slider("Gain", fbind(\.gain), 0, 3); slider("Threshold", fbind(\.threshold), 0, 0.6); slider("Attack (s)", fbind(\.attack), 0.001, 0.5); slider("Release (s)", fbind(\.release), 0.01, 2); slider("Smoothing", fbind(\.smoothing), 0, 1)
            Text("Crossovers (Hz)").font(.caption2)
            ForEach(0..<6, id: \.self) { i in slider(["Sub|Bass", "Bass|Low mid", "Low|Mid", "Mid|High mid", "High mid|High", "High|Treble"][i], Binding(get: { host.cfg.cross[i] }, set: { host.cfg.cross[i] = $0; host.pushCfg() }), 20, 12000) }
            Text("User range (Hz)").font(.caption2); slider("From", Binding(get: { host.cfg.user[0] }, set: { host.cfg.user[0] = $0; host.pushCfg() }), 20, 12000); slider("To", Binding(get: { host.cfg.user[1] }, set: { host.cfg.user[1] = $0; host.pushCfg() }), 20, 16000)
        }
    }
    private func bind(_ k: WritableKeyPath<VizCfg, Bool>) -> Binding<Bool> { Binding(get: { host.cfg[keyPath: k] }, set: { host.cfg[keyPath: k] = $0; host.pushCfg() }) }
    private func fbind(_ k: WritableKeyPath<VizCfg, Float>) -> Binding<Float> { Binding(get: { host.cfg[keyPath: k] }, set: { host.cfg[keyPath: k] = $0; host.pushCfg() }) }
    private func slider(_ t: String, _ b: Binding<Float>, _ lo: Float, _ hi: Float) -> some View { HStack { Text(t).font(.caption2).frame(width: 92, alignment: .leading); Slider(value: b, in: lo...hi); Text(String(format: "%.2f", b.wrappedValue)).font(.system(size: 9, design: .monospaced)).frame(width: 40) } }

    // ---- parameters (groups come from the script's param declarations)
    private var groups: [String] { var seen: [String] = []; for p in host.meta?.params ?? [] where !seen.contains(p.group) { seen.append(p.group) }; return seen }
    private func indices(in g: String) -> [Int] { (host.meta?.params ?? []).indices.filter { host.meta!.params[$0].group == g } }
    @ViewBuilder private func paramRow(_ i: Int) -> some View {
        if let p = host.meta?.params[i], i < host.values.count {
            switch p.kind {
            case 2: Toggle(p.label, isOn: Binding(get: { host.values[i] >= 0.5 }, set: { host.set(i, $0 ? 1 : 0) })).font(.caption)
            case 3: Picker(p.label, selection: Binding(get: { Int(host.values[i].rounded()) }, set: { host.set(i, Float($0)) })) { ForEach(p.options.indices, id: \.self) { Text(p.options[$0]).tag($0) } }.font(.caption)
            default:
                HStack { Text(p.label + (p.unit.isEmpty ? "" : " (\(p.unit))")).font(.caption2).frame(width: 96, alignment: .leading)
                    Slider(value: Binding(get: { host.values[i] }, set: { host.set(i, $0) }), in: p.min...p.max)
                    Button("♪") { assignIdx = i }.font(.caption).buttonStyle(.bordered).controlSize(.mini).tint(host.assigns[i].enabled ? .cyan : nil) }
            }
        }
    }
    private func assignSheet(_ i: Int) -> some View {
        NavigationStack { Form {
            let p = host.meta!.params[i]; let a = host.assigns[i]
            Toggle("Reactive", isOn: Binding(get: { a.enabled }, set: { var x = host.assigns[i]; x.enabled = $0; if $0 && x.band < 0 { x.band = 2 }; host.setAssign(i, x) }))
            Picker("Band", selection: Binding(get: { a.band }, set: { var x = host.assigns[i]; x.band = $0; x.enabled = $0 >= 0; host.setAssign(i, x) })) { Text("None").tag(-1); ForEach(0..<9, id: \.self) { Text(vizBandNames[$0]).tag($0) } }
            Picker("Polarity", selection: Binding(get: { a.pol }, set: { var x = host.assigns[i]; x.pol = $0; host.setAssign(i, x) })) { Text("Positive (louder = more)").tag(1); Text("Inverted (louder = less)").tag(-1); Text("Bipolar (centre = base)").tag(0) }
            slider("Depth", Binding(get: { a.depth }, set: { var x = host.assigns[i]; x.depth = $0; host.setAssign(i, x) }), 0, 1)
            slider("Smoothing", Binding(get: { a.smooth }, set: { var x = host.assigns[i]; x.smooth = $0; host.setAssign(i, x) }), 0, 1)
            slider("Range min", Binding(get: { a.lo }, set: { var x = host.assigns[i]; x.lo = $0; host.setAssign(i, x) }), p.min, p.max)
            slider("Range max", Binding(get: { a.hi }, set: { var x = host.assigns[i]; x.hi = $0; host.setAssign(i, x) }), p.min, p.max)
        }.navigationTitle(host.meta!.params[i].label).toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { assignIdx = nil } } } }
    }
    private var scriptEditor: some View {
        VStack(alignment: .leading) {
            TextEditor(text: $script).font(.system(size: 10, design: .monospaced)).frame(height: 220).autocorrectionDisabled().textInputAutocapitalization(.never)
            HStack { btn("RUN") { if host.apply(source: script) { scriptMsg = "Running - \(host.meta?.params.count ?? 0) parameter(s)." } else { scriptMsg = host.error ?? "error" } }
                btn("CHECK") { scriptMsg = host.check(script) ?? "Syntax OK." }; btn("REVERT") { script = host.src } }
            if !scriptMsg.isEmpty { Text(scriptMsg).font(.caption2).foregroundColor(.secondary) }
            Text("A script with an error never replaces the running one: the last working picture stays on screen.").font(.caption2).foregroundColor(.secondary)
        }
    }
}
struct VizFile: Transferable { let data: Data; let name: String
    static var transferRepresentation: some TransferRepresentation { DataRepresentation(exportedContentType: .json) { $0.data } } }
