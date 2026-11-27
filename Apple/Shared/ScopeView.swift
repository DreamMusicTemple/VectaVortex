// Scope: waveform / X-Y / radial / spectrogram (2D and 3D), with the web app's line + glow style. Fed by the audio unit's output tap.
// NOTE: written without a Mac. Not compiled yet.
import SwiftUI
#if canImport(UIKit)
import UIKit
#endif

struct ScopeView: View {
    let audioUnit: VectaAudioUnit
    @ObservedObject private var power = BatterySaving.shared
    @AppStorage("vv.scope.mode") private var mode = 0              // 0 wave, 1 X-Y, 2 radial, 3 spectrogram, 4 spectrogram 3D
    @AppStorage("vv.scope.thick") private var thick = 1.4
    @AppStorage("vv.scope.glow") private var glow = 0.25
    @AppStorage("vv.scope.line") private var lineHex = "00E5FF"
    @AppStorage("vv.scope.glowCol") private var glowHex = "00E5FF"
    @State private var hist: [[Float]] = []
    @State private var showStyle = false
    @State private var full = false
    @AppStorage("vv.scope.engine") private var engine = 0           // 0 oscilloscope, 1 LUA visuals
    // Spectrogram options (round 26, stage 3; same set as the JUCE scope): FFT size, window, band scale, palette, source, peak-hold markers
    @AppStorage("vv.scope.specFft") private var specFft = 1         // 0/1/2 = 1024 / 2048 / 4096
    @AppStorage("vv.scope.specWin") private var specWin = 0         // Hann / Hamming / Blackman / Rectangular
    @AppStorage("vv.scope.specLin") private var specLin = false     // false = logarithmic band scale
    @AppStorage("vv.scope.specPal") private var specPal = 0         // Heat / Ice / Fire / Mono
    @AppStorage("vv.scope.specSrc") private var specSrc = 0         // L+R / L / R / Mid / Side
    @AppStorage("vv.scope.specPeak") private var specPeak = false
    @State private var peakHold = [Float](repeating: 0, count: 128)
    @AppStorage("vv.scope.power") private var scopePower = false    // Scope ON / OFF: OFF (default) draws nothing, runs no timers or LUA visuals and stops the output tap
    var compact = true

    private static let modes = ["WAVE", "X-Y", "RADIAL", "SPEC", "3D"]
    private var lineColor: Color { Color(hex: lineHex) }
    private var glowColor: Color { Color(hex: glowHex) }

    var body: some View {
        VStack(spacing: 4) {
            HStack(spacing: 6) {
                Button(scopePower ? "ON" : "OFF") { scopePower.toggle(); audioUnit.setScopeTap(scopePower) }.tint(scopePower ? .green : .red)
                if scopePower {
                    Button("OSCILLOSCOPE") { engine = 0 }.tint(engine == 0 ? vvCyan : nil)
                    Button("LUA VISUALS") { engine = 1 }.tint(engine == 1 ? vvCyan : nil)
                }
                Spacer()
                if compact { Button("FULLSCREEN") { full = true } }
            }.font(.system(size: 10, design: .monospaced)).buttonStyle(.bordered).controlSize(.mini)
            if !scopePower {
                VStack(spacing: 6) {
                    Text("SCOPE OFF").font(.system(size: 15, design: .monospaced))
                    Text("The scope is switched off and uses no processing or memory. Press ON to start it.").font(.system(size: 11, design: .monospaced)).multilineTextAlignment(.center)
                }.foregroundStyle(.secondary).frame(maxWidth: .infinity, maxHeight: .infinity).padding().background(Color(white: 0.03)).clipShape(RoundedRectangle(cornerRadius: 8))
            } else if engine == 1 { LuaVisualView(audioUnit: audioUnit) } else { oscilloscope }
        }
        .onAppear { audioUnit.setScopeTap(scopePower) }
        .onDisappear { if compact { audioUnit.setScopeTap(false) } }
        .sheet(isPresented: $showStyle) { styleSheet }
        .fullScreenCover(isPresented: $full) { ScopeView(audioUnit: audioUnit, compact: false).padding().overlay(alignment: .topTrailing) { Button("✕") { full = false }.padding() } }
    }

    private var oscilloscope: some View {
        VStack(spacing: 4) {
            HStack(spacing: 6) {
                ForEach(0..<ScopeView.modes.count, id: \.self) { i in
                    Button(ScopeView.modes[i]) { mode = i }.tint(mode == i ? vvCyan : nil)
                }
                Spacer()
                Button("STYLE") { showStyle = true }
            }.font(.system(size: 10, design: .monospaced)).buttonStyle(.bordered).controlSize(.mini)
            TimelineView(.periodic(from: .now, by: BatterySaving.shared.interval(1.0 / 30.0))) { _ in
                Canvas { ctx, size in draw(ctx, size) }
                    .background(Color(white: 0.03)).clipShape(RoundedRectangle(cornerRadius: 8))
            }
        }
        .onReceive(Timer.publish(every: 1.0 / 30.0, on: .main, in: .common).autoconnect()) { _ in if mode >= 3 { pushSpectrum() } }
    }

    private var styleSheet: some View {
        NavigationStack {
            Form {
                Section("Line + glow") {
                    HStack { Text("Line thickness").font(.caption); Slider(value: $thick, in: 0.5...6) }
                    HStack { Text("Glow").font(.caption); Slider(value: $glow, in: 0...1) }
                    ColorPicker("Line colour", selection: Binding(get: { lineColor }, set: { lineHex = $0.hexString }))
                    ColorPicker("Glow colour", selection: Binding(get: { glowColor }, set: { glowHex = $0.hexString }))
                    Button("RESET") { thick = 1.4; glow = 0.25; lineHex = "00E5FF"; glowHex = "00E5FF" }
                }
                Section("Spectrogram") {
                    Picker("FFT size", selection: $specFft) { Text("1024").tag(0); Text("2048").tag(1); Text("4096").tag(2) }
                    Picker("Window", selection: $specWin) { Text("Hann").tag(0); Text("Hamming").tag(1); Text("Blackman").tag(2); Text("Rectangular").tag(3) }
                    Picker("Band scale", selection: $specLin) { Text("Logarithmic").tag(false); Text("Linear").tag(true) }
                    Picker("Palette", selection: $specPal) { Text("Heat").tag(0); Text("Ice").tag(1); Text("Fire").tag(2); Text("Mono").tag(3) }
                    Picker("Source", selection: $specSrc) { Text("Left + Right").tag(0); Text("Left").tag(1); Text("Right").tag(2); Text("Mid").tag(3); Text("Side").tag(4) }
                    Toggle("Peak-hold markers", isOn: $specPeak)
                }
            }.navigationTitle("Scope style").toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { showStyle = false } } }
        }
    }

    private func stroke(_ ctx: GraphicsContext, _ p: Path) {
        ctx.stroke(p, with: .color(glowColor.opacity(glow)), lineWidth: thick + 2.6)
        ctx.stroke(p, with: .color(lineColor), lineWidth: thick)
    }
    private func draw(_ ctx: GraphicsContext, _ size: CGSize) {
        let snap = audioUnit.scopeSnapshot(2048)
        let L = snap.l, R = snap.r
        let rect = CGRect(origin: .zero, size: size)
        var p = Path()
        switch mode {
        case 1:
            let s = min(size.width, size.height) * 0.44, cx = size.width / 2, cy = size.height / 2
            for i in (L.count - 1024)..<L.count { let pt = CGPoint(x: cx + CGFloat(L[i]) * s, y: cy - CGFloat(R[i]) * s); i == L.count - 1024 ? p.move(to: pt) : p.addLine(to: pt) }
            stroke(ctx, p)
        case 2:
            let start = trigger(L), s = min(size.width, size.height) * 0.5
            for i in 0...1024 { let v = CGFloat(L[start + (i % 1024)]), rad = s * (0.55 + v * 0.4), a = 2 * .pi * CGFloat(i) / 1024
                let pt = CGPoint(x: size.width / 2 + cos(a) * rad, y: size.height / 2 + sin(a) * rad); i == 0 ? p.move(to: pt) : p.addLine(to: pt) }
            stroke(ctx, p)
        case 3: drawSpec(ctx, rect, threeD: false)
        case 4: drawSpec(ctx, rect, threeD: true)
        default:
            let start = trigger(L)
            for i in 0..<1024 { let pt = CGPoint(x: size.width * CGFloat(i) / 1023, y: size.height / 2 - CGFloat(L[start + i]) * size.height * 0.45); i == 0 ? p.move(to: pt) : p.addLine(to: pt) }
            stroke(ctx, p)
        }
    }
    /// A rising zero crossing within the first 1024 samples of the window, so periodic sounds hold still.
    private func trigger(_ L: [Float]) -> Int { for i in 0..<1023 where L[i] <= 0 && L[i + 1] > 0 { return i }; return 0 }

    private let bins = 128, histLen = 160
    private func pushSpectrum() {
        let n = 1024 << min(max(specFft, 0), 2), snap = audioUnit.scopeSnapshot(n), half = n / 2
        var re = [Float](repeating: 0, count: n), im = [Float](repeating: 0, count: n)
        for i in 0..<n {
            let t = Float(i) / Float(n - 1), tp = 2 * Float.pi * t
            let wv: Float = specWin == 0 ? 0.5 - 0.5 * cos(tp) : specWin == 1 ? 0.54 - 0.46 * cos(tp) : specWin == 2 ? 0.42 - 0.5 * cos(tp) + 0.08 * cos(2 * tp) : 1
            let l = snap.l[i], r = snap.r[i]
            let x: Float = specSrc == 1 ? l : specSrc == 2 ? r : specSrc == 4 ? 0.5 * (l - r) : 0.5 * (l + r)
            re[i] = x * wv
        }
        ScopeView.fft(&re, &im)
        var col = [Float](repeating: 0, count: bins)
        for b in 0..<bins {
            let lo = specLin ? b * half / bins : Int(pow(Double(half - 1), Double(b) / Double(bins)))
            let hi = max(lo + 1, specLin ? (b + 1) * half / bins : Int(pow(Double(half - 1), Double(b + 1) / Double(bins))))
            var m: Float = 0; for k in lo..<min(hi, half) { m = max(m, (re[k] * re[k] + im[k] * im[k]).squareRoot()) }
            let db = 20 * log10(m / (Float(n) / 4) + 1e-6); col[b] = min(max((db + 90) / 90, 0), 1)
            peakHold[b] = max(col[b], peakHold[b] - 0.004)
        }
        hist.append(col); if hist.count > histLen { hist.removeFirst(hist.count - histLen) }
    }
    private func heat(_ v: Float) -> Color {
        let c = Double(min(1, v * 1.6)), x = Double(v)
        switch specPal {
        case 1: return Color(hue: 0.62 - 0.12 * x, saturation: 0.55 * (1 - x * 0.6) + 0.2, brightness: c)
        case 2: return Color(hue: 0.13 * x, saturation: 0.95 - 0.4 * x, brightness: c)
        case 3: return Color(white: c)
        default: return Color(hue: 0.66 - 0.66 * x, saturation: 0.9, brightness: c)
        }
    }
    private func drawSpec(_ ctx: GraphicsContext, _ r: CGRect, threeD: Bool) {
        guard !hist.isEmpty else { return }
        let n = hist.count
        if !threeD {
            let cw = r.width / CGFloat(histLen), bh = r.height / CGFloat(bins)
            for x in 0..<n { for b in 0..<bins { let v = hist[x][b]; if v < 0.02 { continue }
                ctx.fill(Path(CGRect(x: CGFloat(x + histLen - n) * cw, y: r.height - CGFloat(b + 1) * bh, width: cw + 0.6, height: bh + 0.6)), with: .color(heat(v))) } }
            if specPeak { for b in 0..<bins where peakHold[b] > 0.05 { ctx.fill(Path(CGRect(x: r.width - 4, y: r.height - CGFloat(b + 1) * bh, width: 4, height: max(1, bh * 0.6))), with: .color(.white.opacity(0.85))) } }
            return
        }
        let rows = min(n, 48), dx = 0.28 * r.width / 48, dy = 0.34 * r.height / 48, w = r.width * 0.68, h = r.height * 0.5
        for rr in 0..<rows {
            let col = hist[n - 1 - rr]; let ox = 10 + CGFloat(rr) * dx, oy = r.height - 14 - CGFloat(rr) * dy
            var p = Path(); p.move(to: CGPoint(x: ox, y: oy))
            for b in 0..<bins { p.addLine(to: CGPoint(x: ox + w * CGFloat(b) / CGFloat(bins - 1), y: oy - CGFloat(col[b]) * h)) }
            p.addLine(to: CGPoint(x: ox + w, y: oy)); p.closeSubpath()
            ctx.fill(p, with: .color(Color(white: 0.03)))
            ctx.stroke(p, with: .color(lineColor.opacity(1 - 0.75 * Double(rr) / Double(rows))), lineWidth: thick * 0.7)
        }
    }
    /// In-place radix-2 FFT (n must be a power of two).
    static func fft(_ re: inout [Float], _ im: inout [Float]) {
        let n = re.count; var j = 0
        for i in 1..<n { var bit = n >> 1; while j & bit != 0 { j ^= bit; bit >>= 1 }; j ^= bit; if i < j { re.swapAt(i, j); im.swapAt(i, j) } }
        var len = 2
        while len <= n {
            let ang = -2 * Float.pi / Float(len), wr = cos(ang), wi = sin(ang)
            var i = 0
            while i < n {
                var cr: Float = 1, ci: Float = 0
                for k in 0..<(len / 2) {
                    let a = i + k, b = i + k + len / 2
                    let tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr
                    re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti
                    let ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr
                }
                i += len
            }
            len <<= 1
        }
    }
}

extension Color {
    init(hex: String) {
        var v: UInt64 = 0; Scanner(string: hex.trimmingCharacters(in: CharacterSet(charactersIn: "#"))).scanHexInt64(&v)
        self.init(red: Double((v >> 16) & 255) / 255, green: Double((v >> 8) & 255) / 255, blue: Double(v & 255) / 255)
    }
    var hexString: String {
        #if canImport(UIKit)
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        UIColor(self).getRed(&r, green: &g, blue: &b, alpha: &a)
        return String(format: "%02X%02X%02X", Int(r * 255), Int(g * 255), Int(b * 255))
        #else
        return "00E5FF"
        #endif
    }
}
