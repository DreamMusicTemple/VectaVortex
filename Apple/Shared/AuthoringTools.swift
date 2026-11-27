// Wavetable authoring (EDIT HARMONICS / FORMULA / DRAW WAVEFORM / IMPORT AUDIO) and the sampler slot tools (IMPORT / STRETCH / WARP / SPECTRAL).
// The DSP is the C++ core's (vv_wt_* / vv_audio_* in vv_engine.h); results are stored in the preset like the web app does:
// customWavetables entries ("custom:<id>") and sampleLibrary entries (16-bit WAV, base64).
// NOTE: written without a compiler (the Mac compile pass is still pending) - see docs/MAC_DAY_ONE.md.
import SwiftUI
import AVFoundation
import UniformTypeIdentifiers
import ImageIO

// MARK: - store helpers
extension PresetStore {
    /// Built-in wavetables + the preset's custom tables, with display labels for the custom ones.
    var tableChoices: (ids: [String], labels: [String: String]) {
        var ids = Assets.wavetableNames, labels: [String: String] = [:]
        for case let t as [String: Any] in array("customWavetables") { if let id = t["id"] as? String { ids.append(id); labels[id] = ((t["name"] as? String) ?? "custom") + " (custom)" } }
        return (ids, labels)
    }
    func putCustomTable(oscPath: String, idKey: String, name: String, frames: [[Float]]) {
        let fr: [[Double]] = frames.map { $0.map { (Double($0) * 1e6).rounded() / 1e6 } }
        var id = string(oscPath + "." + idKey, "")
        let existing = array("customWavetables")
        let idx = existing.firstIndex { ($0 as? [String: Any])?["id"] as? String == id && !id.isEmpty }
        if idx == nil { id = "custom:\(Int(Date().timeIntervalSince1970 * 1000)):\(Int.random(in: 0..<1_000_000))" }
        let entry: [String: Any] = ["id": id, "name": name, "frames": fr]
        if let i = idx { set("customWavetables.\(i)", entry) } else { append("customWavetables", entry); set(oscPath + "." + idKey, id) }
        set(oscPath + ".table", id)
    }
    func sampleWav(_ name: String) -> Data? {
        guard !name.isEmpty else { return nil }
        for case let e as [String: Any] in array("sampleLibrary") where e["name"] as? String == name { if let b = e["base64"] as? String { return Data(base64Encoded: b) } }
        let bundle = Bundle(for: VectaAudioUnit.self)
        guard let listURL = bundle.url(forResource: "builtin_samples", withExtension: "json"), let d = try? Data(contentsOf: listURL),
              let arr = try? JSONSerialization.jsonObject(with: d) as? [[String: String]], let dir = bundle.url(forResource: "Samples", withExtension: nil) else { return nil }
        for e in arr where e["name"] == name { if let f = e["file"] { return try? Data(contentsOf: dir.appendingPathComponent(f)) } }
        return nil
    }
    func uniqueSampleName(_ base: String) -> String {
        let taken: (String) -> Bool = { n in self.array("sampleLibrary").contains { ($0 as? [String: Any])?["name"] as? String == n } || Assets.sampleNames.contains(n) }
        var n = base, k = 2; while taken(n) { n = "\(base) \(k)"; k += 1 }; return n
    }
    func putSample(name: String, wav: Data) {
        let entry: [String: Any] = ["name": name, "base64": wav.base64EncodedString()]
        if let i = array("sampleLibrary").firstIndex(where: { ($0 as? [String: Any])?["name"] as? String == name }) { set("sampleLibrary.\(i)", entry) } else { append("sampleLibrary", entry) }
    }
}

// MARK: - C audio handle wrapper
final class AudioHandle {
    let p: OpaquePointer
    init?(_ p: OpaquePointer?) { guard let p = p else { return nil }; self.p = p }
    deinit { vv_audio_free(p) }
    convenience init?(wav: Data) { let h = wav.withUnsafeBytes { vv_audio_from_wav($0.bindMemory(to: UInt8.self).baseAddress, wav.count) }; self.init(h) }
    convenience init?(file url: URL) {   // any format AVFoundation reads (WAV, AIFF, CAF, MP3, M4A ...), first two channels
        guard let f = try? AVAudioFile(forReading: url), f.length > 0, f.length < 48000 * 600, let buf = AVAudioPCMBuffer(pcmFormat: f.processingFormat, frameCapacity: AVAudioFrameCount(f.length)) else { return nil }
        do { try f.read(into: buf) } catch { return nil }
        guard let ch = buf.floatChannelData else { return nil }
        let n = Int(buf.frameLength), nCh = min(2, Int(buf.format.channelCount))
        self.init(vv_audio_create(Int32(buf.format.sampleRate), Int32(nCh), Int32(n), ch[0], nCh > 1 ? ch[1] : nil))
    }
    var length: Int { Int(vv_audio_length(p)) }
    func channel(_ c: Int) -> [Float] { var d = [Float](repeating: 0, count: length); vv_audio_read(p, Int32(c), &d); return d }
    var wav: Data { let n = vv_audio_to_wav16(p, nil, 0); var d = Data(count: n); d.withUnsafeMutableBytes { _ = vv_audio_to_wav16(p, $0.bindMemory(to: UInt8.self).baseAddress, n) }; return d }
}

// MARK: - wavetable tools (one section per oscillator)
struct WavetableToolsSection: View {
    @ObservedObject var s: PresetStore
    let oscPath: String
    @State private var sheet: Tool?
    @State private var importing = false
    enum Tool: String, Identifiable { case harmonics, formula, draw; var id: String { rawValue } }
    var body: some View {
        Group {
            HStack {
                Button("DRAW WAVEFORM") { sheet = .draw }
                Button("EDIT HARMONICS") { sheet = .harmonics }
                Button("FORMULA…") { sheet = .formula }
                Button("IMPORT AUDIO…") { importing = true }
            }.font(.caption2).buttonStyle(.bordered).controlSize(.mini)
        }
        .sheet(item: $sheet) { t in
            NavigationStack {
                Group {
                    switch t {
                    case .harmonics: HarmonicsEditor(s: s, oscPath: oscPath)
                    case .formula: FormulaEditor(s: s, oscPath: oscPath)
                    case .draw: DrawEditor(s: s, oscPath: oscPath)
                    }
                }.toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { sheet = nil } } }
            }
        }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.audio]) { r in
            guard case .success(let url) = r else { return }
            let scoped = url.startAccessingSecurityScopedResource(); defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            guard let a = AudioHandle(file: url) else { return }
            let mono = a.channel(0); var out = [Float](repeating: 0, count: 1024 * 256)
            let n = Int(vv_wt_slice(mono, Int32(mono.count), 256, &out, 256)); guard n > 0 else { return }
            s.putCustomTable(oscPath: oscPath, idKey: "_importCustomId", name: url.lastPathComponent, frames: (0..<n).map { Array(out[($0 * 1024)..<(($0 + 1) * 1024)]) })
        }
    }
}

struct HarmonicsEditor: View {
    @ObservedObject var s: PresetStore
    let oscPath: String
    @State private var amps: [Double] = []
    var body: some View {
        VStack(alignment: .leading) {
            Text("16 partials, bipolar. The table updates as you drag.").font(.caption).foregroundStyle(.secondary)
            HStack(alignment: .center, spacing: 4) {
                ForEach(0..<16, id: \.self) { i in
                    VStack(spacing: 2) {
                        VerticalSlider(value: Binding(get: { amps.indices.contains(i) ? amps[i] : 0 }, set: { v in if amps.indices.contains(i) { amps[i] = v; apply() } }), range: -1...1, springBack: false).frame(height: 200)
                        Text("\(i + 1)").font(.system(size: 8, design: .monospaced))
                    }
                }
            }
            Button("Reset") { amps = [1] + [Double](repeating: 0, count: 15); apply() }.buttonStyle(.bordered)
            Spacer()
        }.padding().navigationTitle("Edit harmonics")
        .onAppear {
            let saved = (s.array(oscPath + "._harmAmps") as? [NSNumber])?.map { $0.doubleValue } ?? []
            amps = saved.count == 16 ? saved : [1] + [Double](repeating: 0, count: 15); apply()
        }
    }
    private func apply() {
        var out = [Float](repeating: 0, count: 1024); let a = amps.map { Float($0) }
        guard vv_wt_harmonics(a, 16, &out) != 0 else { return }
        s.set(oscPath + "._harmAmps", amps)
        s.putCustomTable(oscPath: oscPath, idKey: "_harmCustomId", name: "Harmonic Wavetable", frames: [out])
    }
}

struct FormulaEditor: View {
    @ObservedObject var s: PresetStore
    let oscPath: String
    @State private var text = "Math.sin(2*Math.PI*x) + 0.3*Math.sin(2*Math.PI*x*3)"
    @State private var frames = 32
    @State private var error = ""
    var body: some View {
        Form {
            Section("Expression in x (phase 0..1) and optionally n (position 0..1 across frames)") {
                TextField("Formula", text: $text, axis: .vertical).font(.system(.body, design: .monospaced)).textInputAutocapitalization(.never).autocorrectionDisabled()
                Text("Supports + - * / % **, comparisons, && || ?: and every Math.* function, e.g. Math.sin(2*Math.PI*x*(1+6*n)). Output is normalised per frame.").font(.caption2).foregroundStyle(.secondary)
                Picker("Frames (if formula uses n)", selection: $frames) { ForEach([8, 16, 32, 64, 128, 256], id: \.self) { Text("\($0)").tag($0) } }
                if !error.isEmpty { Text(error).foregroundStyle(.red).font(.caption) }
                Button("Apply") { apply() }
            }
        }.navigationTitle("Wavetable formula")
        .onAppear { text = s.string(oscPath + "._wtFormula", text); let f = s.int(oscPath + "._wtFormulaFrames", 32); if f > 0 { frames = f } }
    }
    private func apply() {
        var buf = [Float](repeating: 0, count: 1024 * 256); var err = [CChar](repeating: 0, count: 200)
        let n = Int(vv_wt_formula(text, Int32(frames), &buf, 256, &err, 200))
        if n <= 0 { error = String(cString: err); return }
        error = ""
        s.set(oscPath + "._wtFormula", text); s.set(oscPath + "._wtFormulaFrames", frames)
        s.putCustomTable(oscPath: oscPath, idKey: "_formulaCustomId", name: "Formula Wavetable", frames: (0..<n).map { Array(buf[($0 * 1024)..<(($0 + 1) * 1024)]) })
    }
}

/// Freehand one-cycle drawing (the web uses a bezier curve editor; here a 64-point polyline sampled to a frame through the same curve code).
struct DrawEditor: View {
    @ObservedObject var s: PresetStore
    let oscPath: String
    @State private var pts: [Double] = (0..<65).map { 0.5 + 0.5 * sin(2 * Double.pi * Double($0) / 64) }
    var body: some View {
        VStack {
            Text("Drag across the box to draw one cycle.").font(.caption).foregroundStyle(.secondary)
            GeometryReader { g in
                ZStack {
                    Rectangle().fill(Color.black)
                    Path { p in for i in 0..<pts.count { let pt = CGPoint(x: g.size.width * CGFloat(i) / 64, y: g.size.height * CGFloat(1 - pts[i])); if i == 0 { p.move(to: pt) } else { p.addLine(to: pt) } } }.stroke(Color.purple, lineWidth: 2)
                }.gesture(DragGesture(minimumDistance: 0).onChanged { v in
                    let i = Int((v.location.x / g.size.width * 64).rounded()); if (0...64).contains(i) { pts[i] = Double(min(1, max(0, 1 - v.location.y / g.size.height))); if i == 0 { pts[64] = pts[0] } else if i == 64 { pts[0] = pts[64] } }
                }.onEnded { _ in apply() })
            }.frame(height: 240)
            Button("Reset to sine") { pts = (0..<65).map { 0.5 + 0.5 * sin(2 * Double.pi * Double($0) / 64) }; apply() }.buttonStyle(.bordered)
            Spacer()
        }.padding().navigationTitle("Draw waveform").onAppear { apply() }
    }
    private func apply() {
        var flat: [Float] = []; for i in 0..<pts.count { let nx = pts.indices.contains(i + 1) ? i + 1 : i; flat += [Float(i) / 64, Float(pts[i]), Float(Double(i) / 64 + 0.5 / 64), Float((pts[i] + pts[nx]) / 2)] }
        var out = [Float](repeating: 0, count: 1024); guard vv_wt_curve(flat, Int32(pts.count), &out) != 0 else { return }
        s.putCustomTable(oscPath: oscPath, idKey: "_drawCustomId", name: "Drawn Wavetable", frames: [out])
    }
}

// MARK: - sampler slot tools
struct SampleToolsSheet: View {
    @ObservedObject var s: PresetStore
    let slot: Int
    @Environment(\.dismiss) private var dismiss
    private var path: String { "layers.SMP.oscs.\(slot)" }
    @State private var orig: AudioHandle?
    @State private var status = ""
    @State private var importing = false
    @State private var stMode = 0, stQuality = 1; @State private var stValue = 1.0
    @State private var bands = [Double](repeating: 1, count: 8)
    @State private var spEngine = 0, spScale = 0, spWindow = 0
    @State private var markers: [(orig: Double, now: Double)] = []
    private var origName: String { let o = s.string(path + ".origName", ""); return o.isEmpty ? s.string(path + ".name", "") : o }
    var body: some View {
        NavigationStack {
            Form {
                Section { Text("Slot sample: \(origName.isEmpty ? "(none - import audio or pick a sample first)" : origName)").font(.caption)
                    HStack { Button("IMPORT AUDIO…") { importing = true }; Button("REVERT TO ORIGINAL") { revert() } }.buttonStyle(.bordered)
                    if !status.isEmpty { Text(status).font(.caption).foregroundStyle(.tint) } }
                Section("Time stretch (WSOLA, keeps pitch)") {
                    Picker("Mode", selection: $stMode) { Text("Time ratio").tag(0); Text("Pitch shift (keep length)").tag(1); Text("Fit to beats").tag(2) }
                        .onChange(of: stMode) { m in stValue = m == 0 ? 1 : m == 1 ? 0 : 4 }
                    HStack { Text(stMode == 0 ? "Ratio" : stMode == 1 ? "Semitones" : "Beats").font(.caption); Slider(value: $stValue, in: stMode == 0 ? 0.1...8 : stMode == 1 ? -24...24 : 1...32); Text(String(format: "%.2f", stValue)).font(.caption.monospacedDigit()) }
                    Picker("Quality", selection: $stQuality) { Text("Percussive").tag(0); Text("Normal").tag(1); Text("Smooth").tag(2) }
                    Button("APPLY STRETCH") { stretch() }
                }
                Section("Audio warp (markers)") {
                    WarpMarkerEditor(audio: orig, markers: $markers)
                    HStack { Button("APPLY WARP") { warp() }; Button("CLEAR MARKERS", role: .destructive) { markers = [] } }.buttonStyle(.bordered)
                }
                Section("Spectral / resynthesis (STFT band shaping, low → high)") {
                    Picker("Engine", selection: $spEngine) { Text("Spectral (STFT resynthesis)").tag(0); Text("Granular (grain-jittered)").tag(1) }
                    Picker("Band scale", selection: $spScale) { Text("Linear").tag(0); Text("Logarithmic").tag(1) }
                    Picker("Window", selection: $spWindow) { Text("Hann").tag(0); Text("Hamming").tag(1); Text("Blackman").tag(2); Text("Rectangular").tag(3) }
                    HStack(alignment: .bottom) { ForEach(0..<8, id: \.self) { i in VerticalSlider(value: $bands[i], range: 0...2, springBack: false).frame(height: 110) } }
                    HStack { Button("APPLY SPECTRAL") { spectral() }; Button("RESET BANDS") { bands = [Double](repeating: 1, count: 8) } }.buttonStyle(.bordered)
                }
            }.navigationTitle("Sample tools").toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
        }
        .onAppear { loadOriginal() }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.audio]) { r in
            guard case .success(let url) = r else { return }
            let scoped = url.startAccessingSecurityScopedResource(); defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            guard let a = AudioHandle(file: url) else { status = "Could not read that file."; return }
            let nm = s.uniqueSampleName(url.deletingPathExtension().lastPathComponent); s.putSample(name: nm, wav: a.wav)
            s.set(path + ".name", nm); s.set(path + ".origName", ""); status = "Imported \(nm)"; loadOriginal()
        }
    }
    private func loadOriginal() { orig = s.sampleWav(origName).flatMap { AudioHandle(wav: $0) } }
    private func commit(_ res: AudioHandle?, _ suffix: String, key: String, cfg: [String: Any]) {
        guard let res = res else { status = "Processing failed."; return }
        let base = origName, nm = s.uniqueSampleName("\(base) (\(suffix))")
        s.putSample(name: nm, wav: res.wav); s.set(path + ".origName", base); s.set(path + ".name", nm); s.set(path + "." + key, cfg)
        status = "Done: \(nm)"; loadOriginal()
    }
    private func need() -> Bool { if orig == nil { status = "Load or import a sample into this slot first."; return false }; return true }
    private func stretch() {
        guard need(), let o = orig else { return }
        let r = AudioHandle(vv_audio_stretch(o.p, Int32(stMode), stMode == 0 ? stValue : 1, stMode == 1 ? stValue : 0, stMode == 2 ? stValue : 4, s.double("bpm", 120), Int32(stQuality)))
        commit(r, ["stretched", "pitched", "fit"][stMode], key: "stretch", cfg: ["mode": ["ratio", "pitch", "fit"][stMode], "value": stValue, "quality": stQuality])
    }
    private func warp() {
        guard need(), let o = orig else { return }
        guard !markers.isEmpty else { status = "Add at least one warp marker first."; return }
        let flat = markers.flatMap { [$0.orig, $0.now] }
        commit(AudioHandle(vv_audio_warp(o.p, flat, Int32(markers.count))), "warped", key: "warp", cfg: ["markers": markers.map { ["orig": $0.orig, "now": $0.now] }])
    }
    private func spectral() {
        guard need(), let o = orig else { return }
        commit(AudioHandle(vv_audio_spectral(o.p, Int32(spEngine), Int32(spScale), Int32(spWindow), bands, 8)), "spectral", key: "spectral",
               cfg: ["engine": spEngine == 1 ? "granular" : "spectral", "scale": spScale == 1 ? "log" : "linear", "window": spWindow, "bands": bands])
    }
    private func revert() {
        let o = s.string(path + ".origName", ""); guard !o.isEmpty else { status = "Nothing to revert."; return }
        s.set(path + ".name", o); s.set(path + ".origName", ""); for k in ["stretch", "warp", "spectral"] { s.set(path + "." + k, NSNull()) }
        status = "Original restored."; loadOriginal()
    }
}

/// Waveform with warp markers: tap the waveform to add one, drag its dot on the ruler to move where that moment plays, long-press a marker line to remove it.
struct WarpMarkerEditor: View {
    let audio: AudioHandle?
    @Binding var markers: [(orig: Double, now: Double)]
    @State private var peaks: [Float] = []
    @State private var dragging: Int?
    var body: some View {
        GeometryReader { g in
            let h = g.size.height - 26
            ZStack(alignment: .topLeading) {
                Rectangle().fill(Color.black)
                Path { p in for (i, v) in peaks.enumerated() { let x = g.size.width * CGFloat(i) / CGFloat(max(1, peaks.count)); p.move(to: CGPoint(x: x, y: h / 2 - CGFloat(v) * h * 0.48)); p.addLine(to: CGPoint(x: x, y: h / 2 + CGFloat(v) * h * 0.48)) } }.stroke(Color.cyan.opacity(0.8), lineWidth: 1)
                ForEach(markers.indices, id: \.self) { i in
                    Path { p in p.move(to: CGPoint(x: g.size.width * CGFloat(markers[i].orig), y: 0)); p.addLine(to: CGPoint(x: g.size.width * CGFloat(markers[i].orig), y: h)); p.addLine(to: CGPoint(x: g.size.width * CGFloat(markers[i].now), y: h + 13)) }.stroke(Color.orange, lineWidth: 1)
                    Circle().fill(Color.orange).frame(width: 14, height: 14).position(x: g.size.width * CGFloat(markers[i].now), y: h + 13)
                }
            }
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0).onChanged { v in
                let f = Double(min(1, max(0, v.location.x / g.size.width)))
                if dragging == nil, v.location.y > h - 2, let i = markers.indices.min(by: { abs(markers[$0].now - f) < abs(markers[$1].now - f) }), abs(markers[i].now - f) < 0.03 { dragging = i }
                if let i = dragging {
                    let lo = i > 0 ? markers[i - 1].now + 0.002 : 0.002, hi = i + 1 < markers.count ? markers[i + 1].now - 0.002 : 0.998
                    markers[i].now = min(max(f, lo), max(lo, hi))
                }
            }.onEnded { v in
                if dragging == nil, v.location.y < h { let f = Double(min(1, max(0, v.location.x / g.size.width))); markers.append((f, f)); markers.sort { $0.orig < $1.orig } }
                dragging = nil
            })
        }.frame(height: 150).onAppear(perform: computePeaks).onChange(of: audio?.length) { _ in computePeaks() }
    }
    private func computePeaks() {
        guard let a = audio else { peaks = []; return }
        let d = a.channel(0), n = 400; var out = [Float](repeating: 0, count: n)
        for i in 0..<n { let s0 = i * d.count / n, s1 = max(s0 + 1, (i + 1) * d.count / n); var m: Float = 0; for k in s0..<min(s1, d.count) { m = max(m, abs(d[k])) }; out[i] = m }
        peaks = out
    }
}


// MARK: - RECORD (master output or external input -> slot), web "RECORD" sample tool
struct RecordSheet: View {
    @ObservedObject var s: PresetStore
    let slot: Int
    @Environment(\.dismiss) private var dismiss
    private var path: String { "layers.SMP.oscs.\(slot)" }
    @State private var arm = true, loadIntoSlot = true
    @State private var recording = false
    @State private var result: AudioHandle?
    @State private var name = "Recording"
    @State private var status = "", elapsed = 0.0, level: Float = 0
    private let ticker = Timer.publish(every: 1.0 / 15.0, on: .main, in: .common).autoconnect()
    var body: some View {
        NavigationStack {
            Form {
                // The AUv3 is an instrument (aumu) with no input bus, so nothing feeds vv_engine_feed_input here: only the master output can be recorded.
                // (The JUCE build records external input when the host provides it.) Do not add an input bus without testing in a host.
                Text("Source: master output (resample what the instrument plays).").font(.caption)
                Toggle("Arm: start on sound", isOn: $arm)
                Toggle("Load into this slot", isOn: $loadIntoSlot)
                HStack {
                    Button(recording ? "RECORDING…" : "RECORD") { s.recPrepare(external: false, arm: arm); s.recStart(); result = nil; recording = true; status = arm ? "Armed: waiting for sound…" : "Recording…" }.disabled(recording)
                    Button("STOP") { s.recStop(); finish() }.disabled(!recording)
                    Spacer(); Text(String(format: "%d:%04.1f / max 2:00", Int(elapsed) / 60, elapsed - 60 * Double(Int(elapsed) / 60))).font(.caption.monospacedDigit())
                }.buttonStyle(.bordered)
                ProgressView(value: Double(min(1, sqrt(level))))
                if result != nil { TextField("Name", text: $name); Button("SAVE") { save() }.buttonStyle(.borderedProminent) }
                if !status.isEmpty { Text(status).font(.caption).foregroundStyle(.secondary) }
            }
            .navigationTitle("Record").toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { s.recStop(); dismiss() } } }
        }
        .onReceive(ticker) { _ in
            guard recording else { return }
            level = s.recPeak; elapsed = Double(s.recFrames) / s.engineSampleRate
            if s.recState == 3 { finish() } else if s.recState == 2, status.hasPrefix("Armed") { status = "Recording…" }
        }
    }
    private func finish() {
        recording = false
        guard let r = s.recTake() else { status = "Nothing was recorded."; return }
        result = r; let f = DateFormatter(); f.dateFormat = "yyyy-MM-dd HH.mm"; name = "Recording " + f.string(from: Date())
        status = String(format: "Recorded %.1f s. Name it, then save.", Double(r.length) / s.engineSampleRate)
    }
    private func save() {
        guard let r = result else { return }
        let nm = s.uniqueSampleName(name.trimmingCharacters(in: .whitespaces).isEmpty ? "Recording" : name); s.putSample(name: nm, wav: r.wav)
        if loadIntoSlot { s.set(path + ".name", nm); s.set(path + ".origName", "") }
        status = "Saved \"\(nm)\" to the preset's sample library."; result = nil
    }
}

// MARK: - IMAGE -> AUDIO (columns = time, rows = harmonics of the base note), web "IMAGE → AUDIO" tool
struct ImageAudioSheet: View {
    @ObservedObject var s: PresetStore
    let slot: Int
    @Environment(\.dismiss) private var dismiss
    private var path: String { "layers.SMP.oscs.\(slot)" }
    @State private var importing = false
    @State private var image: CGImage?, imageName = ""
    @State private var rgb = false, invert = false, loop = true
    @State private var duration = 6.0, tilt = 0.4, gamma = 1.2
    @State private var note = 36, harmonics = 96
    @State private var status = ""
    private func noteName(_ m: Int) -> String { ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][m % 12] + String(m / 12 - 1) }
    var body: some View {
        NavigationStack {
            Form {
                Text("Each image column is a moment in time and each row a harmonic of the base note (top row = highest). Brightness sets the harmonic's loudness.").font(.caption)
                Button("CHOOSE IMAGE…") { importing = true }.buttonStyle(.bordered)
                if let im = image { Text("\(imageName)  \(im.width) × \(im.height)").font(.caption); Image(decorative: im, scale: 1).resizable().scaledToFit().frame(maxHeight: 120) }
                Picker("Colour", selection: $rgb) { Text("Brightness only (mono)").tag(false); Text("RGB: red/blue = L/R, green = phase").tag(true) }
                Stepper("Duration \(String(format: "%.1f", duration)) s", value: $duration, in: 0.5...30, step: 0.5)
                Picker("Base note", selection: $note) { ForEach(24...84, id: \.self) { Text(noteName($0)).tag($0) } }
                Picker("Harmonics", selection: $harmonics) { ForEach([32, 64, 96, 128, 192, 256], id: \.self) { Text("\($0)").tag($0) } }
                HStack { Text("High tilt"); Slider(value: $tilt, in: 0...1.5, step: 0.05) }
                HStack { Text("Contrast"); Slider(value: $gamma, in: 0.4...3, step: 0.05) }
                Toggle("Invert", isOn: $invert); Toggle("Seamless loop", isOn: $loop)
                Button("RENDER INTO SLOT") { render() }.buttonStyle(.borderedProminent).disabled(image == nil)
                if !status.isEmpty { Text(status).font(.caption).foregroundStyle(.secondary) }
            }
            .navigationTitle("Image → Audio").toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
        }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.image]) { r in
            guard case .success(let url) = r else { return }
            let ok = url.startAccessingSecurityScopedResource(); defer { if ok { url.stopAccessingSecurityScopedResource() } }
            guard let src = CGImageSourceCreateWithURL(url as CFURL, nil), let im = CGImageSourceCreateImageAtIndex(src, 0, nil) else { status = "Could not read that image."; return }
            image = im; imageName = url.deletingPathExtension().lastPathComponent; status = ""
        }
    }
    private func render() {
        guard let im = image else { return }
        let w = im.width, h = im.height; var px = [UInt8](repeating: 0, count: w * h * 4)
        guard let cg = CGContext(data: &px, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4, space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { status = "Could not read that image."; return }
        cg.draw(im, in: CGRect(x: 0, y: 0, width: w, height: h))
        guard let a = AudioHandle(vv_audio_from_image(px, Int32(w), Int32(h), Int32(s.engineSampleRate), duration, Int32(note), Int32(harmonics), tilt, gamma, invert ? 1 : 0, loop ? 1 : 0, rgb ? 1 : 0)) else { status = "Could not render this image."; return }
        let nm = s.uniqueSampleName("Image: " + imageName); s.putSample(name: nm, wav: a.wav)
        s.set(path + ".name", nm); s.set(path + ".origName", ""); s.set(path + ".root", note); s.set(path + ".loop", loop)
        status = String(format: "Rendered %.2f s into the slot (root note %@).", Double(a.length) / s.engineSampleRate, noteName(note))
    }
}
