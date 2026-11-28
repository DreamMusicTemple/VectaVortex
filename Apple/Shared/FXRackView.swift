// FX tab: the effects rack - up to 8 stages in series, each with enable, per-layer sends, reorder/remove and the full parameter set
// of its type. Everything edits "fxStages.N..." in the live preset through PresetStore (no audio-thread work in the UI).
// NOTE: written without a Mac. Not compiled yet. See docs/MAC_DAY_ONE.md.
import SwiftUI

enum FXKind: String, CaseIterable {
    case reverb, delay, saturation, wavefolder, spectral, filter, granular, interstellar, tape, phaser, flanger, resonator, mbcomp, glitch, panning, chorus, spread, eq, ir, rotary, amp
    var title: String { self == .ir ? "IR Reverb (convolution)" : self == .eq ? "EQ (4-band)" : self == .spread ? "Stereo spread" : self == .rotary ? "Rotary Speaker (Leslie)" : self == .amp ? "Amp + Cabinet" : self == .mbcomp ? "Multi-band compressor" : rawValue.capitalized }
    /// Same defaults as the web app's defaultFxParams().
    var defaults: [String: Any] {
        switch self {
        case .reverb: return ["algo": "hall", "decay": 0.5, "size": 0.5, "predelay": 0.02, "mix": 0.3, "mod": 0.4, "colour": 0.5, "shimA": 12, "shimB": -12]
        case .delay: return ["mode": "stereo", "time": 0.35, "feedback": 0.35, "mix": 0.25, "sync": false, "syncDiv": "1/8"]
        case .saturation: return ["type": "normal", "drive": 0.4, "tone": 6000, "mix": 0.5, "multiband": false, "bandLowFreq": 300, "bandHighFreq": 3000, "bandLow": 1, "bandMid": 1, "bandHigh": 1]
        case .wavefolder: return ["drive": 3, "symmetry": 0, "mix": 1, "mode": "fold", "pwm": 0.5]
        case .spectral: return ["freeze": 0, "blur": 0, "shift": 0, "threshold": 0, "spread": 0, "tilt": 0, "disperse": 0, "mix": 1]
        case .mbcomp: return FXKind.mbPreset("normal")
        case .filter: return ["filterType": "moog4pole", "cutoff": 2000, "resonance": 20, "mix": 1]
        case .granular: return ["grainSize": 80, "density": 20, "pitch": 0, "spread": 0.5, "panCenter": 0, "envShape": 0, "followerAmount": 0, "mix": 1, "delay": 0, "posRand": 1, "sizeRand": 0, "jitter": 0, "fine": 0, "pitchRand": 0, "reverse": 0, "volRand": 0, "width": 1, "drift": 0, "lrSpread": 0, "outLevel": 0, "curve": 0.5, "symmetry": 0.5, "atk": 0, "dec": 0, "sus": 1, "rel": 0, "fb": 0, "fbDamp": 0, "fbLow": 0, "fbSat": 0, "fbDiff": 0, "pingPong": 0, "rndDepth": 1, "rndRate": 2, "seed": 0, "mode": 0, "window": -1, "rndType": 0, "sync": 0, "delaySync": 0, "freeze": 0, "autoGain": 0]
        case .interstellar:
            if let d = InterstellarPresets.defaultsJSON.data(using: .utf8), let o = try? JSONSerialization.jsonObject(with: d) as? [String: Any] { return o }
            return [:]
        case .tape: return ["wowRate": 0.7, "wowDepth": 0.3, "flutterRate": 8, "flutterDepth": 0.2, "dust": 0.2, "crackle": 0.2, "mix": 1]
        case .phaser: return ["rate": 0.4, "depth": 0.85, "feedback": 0.6, "mix": 0.5, "stages": 8]
        case .flanger: return ["rate": 0.25, "depth": 0.5, "feedback": 0.3, "mix": 0.5]
        case .resonator: return ["freq": 440, "resonance": 15, "mix": 0.4]
        case .glitch: return ["bitDepth": 16, "srReduction": 1, "companding": 0, "retroModel": "off", "stutterOn": false, "stutterDivision": 0.25, "stutterAmount": 0.5, "mix": 1,
                              "seqOn": false, "seqSteps": [true, true, true, true, true, true], "seqRate": "1/16", "seqIntensity": 0.3, "seqTempoDiv": "1"]
        case .panning: return ["width": 1, "autoPanOn": false, "rate": 1, "shape": "square", "mode": "stereo", "distance": 0.3, "mix": 1,
                               "mb": ["on": false, "xover": [200, 1500, 6000], "bands": [
                                ["on": true, "width": 0.4, "depth": 0.0, "speed": 0.25, "fx": 0], ["on": true, "width": 0.7, "depth": 0.5, "speed": 0.4, "fx": 0],
                                ["on": true, "width": 0.9, "depth": 0.6, "speed": 0.65, "fx": 0], ["on": true, "width": 1.0, "depth": 0.8, "speed": 0.9, "fx": 0]]] as [String: Any]]
        case .chorus: return ["chorus1On": true, "chorus2On": false, "mix": 0.5]
        case .spread: return ["spread": 1, "mix": 1]
        case .rotary: return ["speed": "slow", "drive": 0.3, "balance": 0.5, "depth": 0.7, "width": 0.8, "accel": 1, "mix": 1, "level": 0]
        case .amp: return ["model": "twin", "gain": 0.4, "bass": 0.5, "mid": 0.5, "treble": 0.5, "presence": 0.4, "master": 0.5, "cab": true, "chorus": 0.5, "mix": 1, "level": 0]
        case .eq: return ["f1": 120, "g1": 0, "f2": 600, "g2": 0, "f3": 2500, "g3": 0, "f4": 8000, "g4": 0, "out": 0, "mix": 1]
        case .ir: return ["irId": "f:hall-concert", "irName": "Concert Hall", "mix": 0.3, "predelay": 0, "lenPct": 100, "earlyLate": 0, "inLevel": 0, "outLevel": 0, "lowCut": 20, "highCut": 20000,
                          "damping": 0, "tone": 0, "width": 1, "balance": 0, "start": 0, "end": 1, "normalise": true, "reverse": false, "irGain": 0]
        }
    }
}

extension FXKind {
    /// 3-band compressor presets (same numbers as the web app's MBC_PRESETS).
    static func mbPreset(_ mode: String) -> [String: Any] {
        func band(_ tHi: Double, _ rDn: Double, _ tLo: Double, _ rUp: Double, _ atk: Double, _ rel: Double, _ gain: Double) -> [String: Any] {
            ["on": true, "tHi": tHi, "rDn": rDn, "tLo": tLo, "rUp": rUp, "atk": atk, "rel": rel, "gain": gain]
        }
        if mode == "ott" { return ["mode": "ott", "depth": 1, "mix": 1, "inGain": 0, "outGain": -3, "xLow": 130, "xHigh": 2800,
                                   "lo": band(-24, 8, -48, 3, 14, 120, 3), "mi": band(-26, 8, -48, 3, 6, 80, 3), "hi": band(-28, 8, -46, 3, 1.5, 50, 4)] }
        return ["mode": "normal", "depth": 1, "mix": 1, "inGain": 0, "outGain": 0, "xLow": 150, "xHigh": 2500,
                "lo": band(-18, 3, -60, 1, 25, 220, 0), "mi": band(-18, 3, -60, 1, 12, 160, 0), "hi": band(-18, 3, -60, 1, 5, 110, 0)]
    }
}

struct FXRackView: View {
    @ObservedObject var s: PresetStore
    @State private var expanded: Set<Int> = []
    private let maxStages = 10

    var body: some View {
        NavigationStack {
            List {
                let n = s.count("fxStages")
                if n == 0 { Text("No effects yet. Add one below.").font(.caption).foregroundStyle(.secondary) }
                ForEach(0..<n, id: \.self) { i in StageCard(s: s, index: i, count: n, open: Binding(get: { expanded.contains(i) }, set: { if $0 { expanded.insert(i) } else { expanded.remove(i) } })) }
                Section {
                    Menu {
                        ForEach(FXKind.allCases, id: \.self) { k in Button(k.title) { add(k) } }
                    } label: { Label(n >= maxStages ? "Chain full (\(maxStages) max)" : "Add effect", systemImage: "plus.circle") }
                    .disabled(n >= maxStages)
                }
                Section("Global FX") {
                    PickerRow(s: s, label: "Oversampling (saturation / wavefolder)", path: "oversample", options: ["normal", "2x", "4x"], def: "normal")
                }
            }
            .navigationTitle("FX rack").navigationBarTitleDisplayMode(.inline).toolbar(.hidden, for: .navigationBar)
        }
    }

    private func add(_ k: FXKind) {
        let stage: [String: Any] = ["type": k.rawValue, "enabled": true, "params": k.defaults,
                                    "sends": ["WT": true, "FM": true, "SMP": true, "PRO": true]]
        s.append("fxStages", stage)
    }
}

private struct StageCard: View {
    @ObservedObject var s: PresetStore
    let index: Int; let count: Int
    @Binding var open: Bool
    private var base: String { "fxStages.\(index)" }
    private var type: String { s.string("\(base).type") }
    private var enabled: Bool { s.bool("\(base).enabled", true) }
    private var anySend: Bool { FXStyle.sources.contains { s.bool("\(base).sends.\($0.key)", false) } }

    var body: some View {
        Section {
            HStack(spacing: 10) {
                Text("\(index + 1)").font(.system(.caption2, design: .monospaced).weight(.semibold)).foregroundStyle(.secondary)
                    .padding(.horizontal, 5).padding(.vertical, 2).overlay(RoundedRectangle(cornerRadius: 4).strokeBorder(Color.secondary.opacity(0.6), lineWidth: 1))
                Text(type.uppercased()).font(.system(.headline, design: .monospaced).bold())
                Toggle("", isOn: s.binding("\(base).enabled", bool: true)).labelsHidden().accessibilityLabel("Effect on or off")
                Text(enabled ? "ON" : "OFF").font(.system(.caption2, design: .monospaced).weight(.bold)).foregroundStyle(enabled ? FXStyle.cyan : Color.secondary)
                Spacer()
                Button { s.move("fxStages", from: index, to: index - 1) } label: { Image(systemName: "chevron.up") }.disabled(index == 0).buttonStyle(.borderless)
                Button { s.move("fxStages", from: index, to: index + 1) } label: { Image(systemName: "chevron.down") }.disabled(index >= count - 1).buttonStyle(.borderless)
                Button { open.toggle() } label: { Image(systemName: open ? "chevron.up.circle" : "slider.horizontal.3") }.buttonStyle(.borderless)
                Button(role: .destructive) { s.remove("fxStages", at: index) } label: { Image(systemName: "trash") }.buttonStyle(.borderless)
            }
            VStack(alignment: .leading, spacing: 6) {
                Text("SEND TO").font(.system(size: 10, weight: .semibold, design: .monospaced)).tracking(1.4).foregroundStyle(.secondary)
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 70), spacing: 6)], alignment: .leading, spacing: 6) {
                    ForEach(FXStyle.sources) { src in   // pills: lit LED + cyan edge = this source feeds the effect
                        Toggle(src.key, isOn: s.binding("\(base).sends.\(src.key)", bool: false)).toggleStyle(SendChipStyle(xfx: src.key == "XFX"))
                            .accessibilityLabel("Send \(src.name) to this effect")
                    }
                }
                if enabled && !anySend { Text("No source selected: this effect is idle").font(.caption2).foregroundStyle(.orange) }
            }.opacity(enabled ? 1 : 0.45)
            if open { params }
        }
    }

    private func sl(_ label: String, _ key: String, _ r: ClosedRange<Double>, _ def: Double = 0, fmt: String = "%.2f", log: Bool = false, unit: String = "") -> some View {
        SliderRow(s: s, label: label, path: "\(base).params.\(key)", range: r, def: def, fmt: fmt, log: log, unit: unit)
    }
    private func pk(_ label: String, _ key: String, _ opts: [String]) -> some View { PickerRow(s: s, label: label, path: "\(base).params.\(key)", options: opts) }
    private func tg(_ label: String, _ key: String, _ def: Bool = false) -> some View { ToggleRow(s: s, label: label, path: "\(base).params.\(key)", def: def) }

    @ViewBuilder private var params: some View {
        switch type {
        case "reverb":
            pk("Algorithm", "algo", ["room", "spring", "plate", "hall", "plate ambient", "kristal", "flow", "mothership", "formant", "shimmer", "reverse"])
            sl("Decay", "decay", 0...0.97, 0.5); sl("Size", "size", 0.1...2, 0.5); sl("Predelay", "predelay", 0...0.3, 0.02, fmt: "%.3f", unit: " s")
            sl("Mix", "mix", 0...1, 0.3); sl("Modulation", "mod", 0...1, 0.4); sl("Colour / shift / vowel", "colour", 0...1, 0.5)
            if s.string("\(base).params.algo") == "shimmer" { sl("Shimmer A", "shimA", -24...24, 12, fmt: "%.0f", unit: " st"); sl("Shimmer B", "shimB", -24...24, -12, fmt: "%.0f", unit: " st") }
        case "delay":
            pk("Mode", "mode", ["mono", "stereo", "pingpong", "reverse"])
            tg("Tempo sync", "sync")
            if s.bool("\(base).params.sync") { pk("Division", "syncDiv", ["1/1", "1/2", "1/4", "1/8", "1/16", "1/4.", "1/8.", "1/4t", "1/8t"]) }
            else { sl("Time", "time", 0.01...1.5, 0.35, unit: " s") }
            sl("Feedback", "feedback", 0...0.9, 0.35); sl("Mix", "mix", 0...1, 0.25)
        case "saturation":
            PickerRow(s: s, label: "Type", path: "\(base).params.type", options: ["soft", "normal", "hard", "tube"], labels: ["hard": "hard (distortion)", "tube": "Tube Overdrive"]); sl("Drive", "drive", 0...1, 0.4); sl("Tone", "tone", 300...14000, 6000, fmt: "%.0f", log: true, unit: " Hz"); sl("Mix", "mix", 0...1, 0.5)
            tg("Multiband", "multiband")
            if s.bool("\(base).params.multiband") {
                sl("Low / Mid crossover", "bandLowFreq", 60...2000, 300, fmt: "%.0f", log: true, unit: " Hz"); sl("Mid / High crossover", "bandHighFreq", 600...16000, 3000, fmt: "%.0f", log: true, unit: " Hz")
                sl("Low amount", "bandLow", 0...1, 1); sl("Mid amount", "bandMid", 0...1, 1); sl("High amount", "bandHigh", 0...1, 1)
            }
        case "wavefolder":
            pk("Mode", "mode", ["fold", "mirror", "sine", "pwm"]); sl("Drive", "drive", 1...20, 3, fmt: "%.1f"); sl("Symmetry", "symmetry", -1...1, 0)
            if s.string("\(base).params.mode") == "pwm" { sl("PWM width", "pwm", 0.02...0.98, 0.5) }
            sl("Mix", "mix", 0...1, 1)
        case "spectral":
            sl("Freeze", "freeze", 0...1); sl("Blur", "blur", 0...1); sl("Shift", "shift", -200...200, 0, fmt: "%.0f"); sl("Threshold", "threshold", 0...1); sl("Spread", "spread", 0...1)
            sl("Tilt", "tilt", -12...12, 0, fmt: "%.1f", unit: " dB/oct"); sl("Dispersion", "disperse", 0...1, 0); sl("Mix", "mix", 0...1, 1)
        case "mbcomp": mbcomp
        case "eq":
            EQGraphView(s: s, prefix: "\(base).params", flat: true)
            sl("Output", "out", -24...24, 0, fmt: "%.1f", unit: " dB"); sl("Wet / dry", "mix", 0...1, 1)
        case "ir": IRReverbPanel(s: s, base: "\(base).params")
        case "filter":
            pk("Filter type", "filterType", filterTypes); pk("Formant table", "formantTable", formantTables); pk("Vowel", "vowel", formantVowels); sl("Cutoff", "cutoff", 20...18000, 2000, fmt: "%.0f", log: true, unit: " Hz"); sl("Resonance", "resonance", 0...130, 20, fmt: "%.1f"); sl("Mix", "mix", 0...1, 1)
        case "granular":
            sl("Grain size", "grainSize", 5...500, 80, fmt: "%.0f", log: true, unit: " ms"); sl("Density", "density", 0.5...100, 20, fmt: "%.1f", log: true)
            sl("Pitch", "pitch", -24...24, 0, fmt: "%.1f", unit: " st"); sl("Panning", "panCenter", -1...1, 0); sl("Stereo spread", "spread", 0...1, 0.5)
            sl("Envelope shape", "envShape", 0...1, 0); sl("Follower amount", "followerAmount", 0...1, 0); sl("Mix", "mix", 0...1, 1)
            ChoiceRow(s: s, label: "Mode", path: "\(base).params.mode", choices: [("Classic granular", 0), ("Granular delay", 1), ("Granular freeze", 2), ("Granular reverse", 3), ("Granular diffusion", 4), ("Granular pitch scatter", 5), ("Granular cloud", 6), ("Granular glitch", 7), ("Granular stutter", 8), ("Granular scramble", 9), ("Granular time scatter", 10), ("Rhythmic granular", 11), ("Ambient granular", 12), ("Spectral granular", 13)], def: 0)
            ChoiceRow(s: s, label: "Window", path: "\(base).params.window", choices: [("Classic (envelope shape)", -1), ("Hann", 0), ("Hamming", 1), ("Blackman", 2), ("Gaussian", 3), ("Triangle", 4), ("Rectangular", 5), ("Tukey", 6), ("Exponential", 7)], def: -1)
            ChoiceRow(s: s, label: "Rate sync", path: "\(base).params.sync", choices: [("Off", 0), ("1/1", 1), ("1/2", 2), ("1/4", 3), ("1/8", 4), ("1/16", 5), ("1/32", 6), ("1/4T", 7), ("1/8T", 8), ("1/16T", 9)], def: 0)
            ChoiceRow(s: s, label: "Delay sync", path: "\(base).params.delaySync", choices: [("Off", 0), ("1/1", 1), ("1/2", 2), ("1/4", 3), ("1/8", 4), ("1/16", 5), ("1/32", 6), ("1/4T", 7), ("1/8T", 8), ("1/16T", 9)], def: 0)
            ChoiceRow(s: s, label: "Random type", path: "\(base).params.rndType", choices: [("White", 0), ("Pink", 1), ("Brown", 2), ("Sample and hold", 3), ("Smooth random", 4), ("Slow evolving", 5)], def: 0)
            ToggleRow(s: s, label: "Freeze", path: "\(base).params.freeze")
            ToggleRow(s: s, label: "Auto gain", path: "\(base).params.autoGain")
            sl("Delay / position", "delay", 0...1500, 0, fmt: "%.0f", log: true, unit: " ms")
            sl("Position scatter", "posRand", 0...8, 1, fmt: "%.2f")
            sl("Size random", "sizeRand", 0...1, 0, fmt: "%.2f")
            sl("Timing jitter", "jitter", 0...1, 0, fmt: "%.2f")
            sl("Fine tune", "fine", -100...100, 0, fmt: "%.0f", unit: " cents")
            sl("Pitch random", "pitchRand", 0...12, 0, fmt: "%.1f", unit: " st")
            sl("Reverse amount", "reverse", 0...1, 0, fmt: "%.2f")
            sl("Volume random", "volRand", 0...1, 0, fmt: "%.2f")
            sl("Stereo width", "width", 0...2, 1, fmt: "%.2f")
            sl("Stereo drift", "drift", 0...1, 0, fmt: "%.2f")
            sl("L/R grain offset", "lrSpread", 0...80, 0, fmt: "%.1f", unit: " ms")
            sl("Output level", "outLevel", -24...12, 0, fmt: "%.1f")
            sl("Curvature", "curve", 0...1, 0.5, fmt: "%.2f")
            sl("Symmetry", "symmetry", 0...1, 0.5, fmt: "%.2f")
            sl("Attack", "atk", 0...0.5, 0, fmt: "%.2f")
            sl("Decay", "dec", 0...0.5, 0, fmt: "%.2f")
            sl("Sustain", "sus", 0...1, 1, fmt: "%.2f")
            sl("Release", "rel", 0...0.5, 0, fmt: "%.2f")
            sl("Feedback", "fb", 0...0.95, 0, fmt: "%.2f")
            sl("Damping", "fbDamp", 0...1, 0, fmt: "%.2f")
            sl("Low cut", "fbLow", 0...1, 0, fmt: "%.2f")
            sl("Saturation", "fbSat", 0...1, 0, fmt: "%.2f")
            sl("Diffusion", "fbDiff", 0...1, 0, fmt: "%.2f")
            sl("Ping-pong", "pingPong", 0...1, 0, fmt: "%.2f")
            sl("Random depth", "rndDepth", 0...1, 1, fmt: "%.2f")
            sl("Random rate", "rndRate", 0.05...20, 2, fmt: "%.2f", log: true, unit: " Hz")
            sl("Random seed", "seed", 0...999, 0, fmt: "%.0f")
        case "interstellar": InterstellarParams(s: s, base: base)
        case "tape":
            sl("Wow rate", "wowRate", 0.05...5, 0.7, unit: " Hz"); sl("Wow depth", "wowDepth", 0...1, 0.3); sl("Flutter rate", "flutterRate", 2...20, 8, fmt: "%.1f", unit: " Hz")
            sl("Flutter depth", "flutterDepth", 0...1, 0.2); sl("Dust", "dust", 0...1, 0.2); sl("Crackle", "crackle", 0...1, 0.2); sl("Mix", "mix", 0...1, 1)
        case "phaser":
            sl("Rate", "rate", 0.05...5, 0.4, unit: " Hz"); sl("Depth", "depth", 0...1, 0.85); sl("Feedback", "feedback", 0...0.9, 0.6); sl("Mix", "mix", 0...1, 0.5)
            StepperRow(s: s, label: "Stages", path: "\(base).params.stages", range: 2...12, def: 8)
        case "flanger":
            sl("Rate", "rate", 0.05...5, 0.25, unit: " Hz"); sl("Depth", "depth", 0...1, 0.5); sl("Feedback", "feedback", 0...0.9, 0.3); sl("Mix", "mix", 0...1, 0.5)
        case "resonator":
            sl("Frequency", "freq", 40...2000, 440, fmt: "%.0f", log: true, unit: " Hz"); sl("Resonance", "resonance", 1...40, 15, fmt: "%.1f"); sl("Mix", "mix", 0...1, 0.4)
        case "chorus":
            tg("Chorus 1", "chorus1On", true); tg("Chorus 2", "chorus2On"); sl("Mix", "mix", 0...1, 0.5)
        case "spread":
            sl("Spread", "spread", 0...2, 1); sl("Mix", "mix", 0...1, 1)
        case "rotary":
            HStack {   // Slow / Fast / Stop rotor switch: the horn and drum spin up and coast down with their own inertia
                Text("Speed").font(.caption)
                Picker("Speed", selection: s.binding("\(base).params.speed", string: "slow")) { Text("SLOW").tag("slow"); Text("FAST").tag("fast"); Text("STOP").tag("stop") }.pickerStyle(.segmented)
            }
            sl("Drive (tube)", "drive", 0...1, 0.3); sl("Horn / drum balance", "balance", 0...1, 0.5); sl("Depth (doppler + AM)", "depth", 0...1, 0.7); sl("Mic width", "width", 0...1, 0.8)
            sl("Acceleration", "accel", 0.2...4, 1, log: true); sl("Mix", "mix", 0...1, 1); sl("Level", "level", -24...12, 0, fmt: "%.1f", unit: " dB")
        case "amp":
            PickerRow(s: s, label: "Model", path: "\(base).params.model", options: ["twin", "jc", "small", "brit"], def: "twin", labels: ["twin": "Twin (clean, scooped)", "jc": "JC (solid-state + chorus)", "small": "Small (practice combo)", "brit": "Brit (crunch stack)"])
            sl("Gain", "gain", 0...1, 0.4); sl("Bass", "bass", 0...1, 0.5); sl("Mid", "mid", 0...1, 0.5); sl("Treble", "treble", 0...1, 0.5); sl("Presence", "presence", 0...1, 0.4); sl("Master", "master", 0...1, 0.5)
            if s.string("\(base).params.model", "twin") == "jc" { sl("Chorus (JC)", "chorus", 0...1, 0.5) }
            tg("Cabinet", "cab", true); sl("Mix", "mix", 0...1, 1); sl("Level", "level", -24...12, 0, fmt: "%.1f", unit: " dB")
        case "glitch": glitch
        case "panning": panning
        default: Text("Unknown effect").font(.caption).foregroundStyle(.secondary)
        }
    }

    /// Choosing a mode loads that mode's band settings and keeps Input and Mix (web: mbCompApplyMode).
    @ViewBuilder private var mbcomp: some View {
        Group {
            HStack {
                Text("Mode").font(.caption)
                Picker("Mode", selection: Binding(get: { s.string("\(base).params.mode", "normal") }, set: { m in
                    for (k, v) in FXKind.mbPreset(m) where k != "mix" && k != "inGain" { s.set("\(base).params.\(k)", v) }
                })) { Text("Normal").tag("normal"); Text("OTT").tag("ott") }.pickerStyle(.segmented)
            }
            sl("Input", "inGain", -24...24, 0, fmt: "%.1f", unit: " dB"); sl("Output", "outGain", -24...24, 0, fmt: "%.1f", unit: " dB"); sl("Depth", "depth", 0...1, 1); sl("Mix", "mix", 0...1, 1)
            sl("Low / mid crossover", "xLow", 40...1000, 150, fmt: "%.0f", log: true, unit: " Hz"); sl("Mid / high crossover", "xHigh", 1000...12000, 2500, fmt: "%.0f", log: true, unit: " Hz")
            ForEach([("lo", "Low"), ("mi", "Mid"), ("hi", "High")], id: \.0) { b, name in
                Text(name + " band").font(.caption.bold())
                tg(name + " on", "\(b).on", true)
                sl("High threshold", "\(b).tHi", -60...0, -18, fmt: "%.1f", unit: " dB"); sl("Down ratio", "\(b).rDn", 1...20, 3, fmt: "%.1f", unit: ":1")
                sl("Low threshold", "\(b).tLo", -80...0, -60, fmt: "%.1f", unit: " dB"); sl("Up ratio", "\(b).rUp", 1...10, 1, fmt: "%.1f", unit: ":1")
                sl("Attack", "\(b).atk", 0.1...200, 12, fmt: "%.1f", log: true, unit: " ms"); sl("Release", "\(b).rel", 5...1500, 160, fmt: "%.0f", log: true, unit: " ms"); sl("Makeup", "\(b).gain", -12...24, 0, fmt: "%.1f", unit: " dB")
            }
        }
    }

    @ViewBuilder private var glitch: some View {
        Group {
            sl("Bit depth", "bitDepth", 1...16, 16, fmt: "%.0f"); sl("Sample-rate reduction", "srReduction", 1...50, 1, fmt: "%.0f")
            sl("Companding", "companding", 0...1, 0)
            Menu("Retro DSP model") {   // sets bitDepth / srReduction / companding together (SR assumed 48 kHz); values stay editable
                Button("Off (plain bitcrush)") { s.set("\(base).params.retroModel", "off") }
                ForEach([("sp1200", "E-mu SP-1200 (12-bit, 26.04kHz)", 12, 26040.0, 0.12), ("mirage", "Ensoniq Mirage/EPS (8-13-bit)", 8, 32000.0, 0.4), ("fairlight", "Fairlight CMI (8/16-bit companding)", 8, 22000.0, 0.75)], id: \.0) { m in
                    Button(m.1) {
                        s.set("\(base).params.retroModel", m.0); s.set("\(base).params.bitDepth", m.2)
                        s.set("\(base).params.srReduction", max(1, Int((48000.0 / m.3).rounded()))); s.set("\(base).params.companding", m.4)
                    }
                }
            }
            tg("Stutter", "stutterOn")
            ChoiceRow(s: s, label: "Stutter division", path: "\(base).params.stutterDivision", choices: [("1", 1), ("1/2", 0.5), ("1/4", 0.25), ("1/8", 0.125)], def: 0.25)
            sl("Stutter amount", "stutterAmount", 0...1, 0.5); sl("Mix", "mix", 0...1, 1)
        }
        Divider()
        Group {
            tg("Mini glitch sequencer", "seqOn")
            HStack {
                Text("Steps").font(.caption)
                ForEach(0..<6, id: \.self) { k in
                    Toggle("\(k + 1)", isOn: s.binding("\(base).params.seqSteps.\(k)", bool: true)).toggleStyle(.button).font(.caption2.monospaced())
                }
            }
            Text("1 stutter · 2 bitcrush · 3 reverse · 4 gate · 5 pitch · 6 tape stop").font(.caption2).foregroundStyle(.secondary)
            pk("Rate", "seqRate", ["1/4", "1/8", "1/16", "1/8.", "1/8t"])
            pk("Tempo div", "seqTempoDiv", ["1", "1/2", "1/4", "2x"])
            sl("Intensity", "seqIntensity", 0...1, 0.3)
        }
    }

    @ViewBuilder private var panning: some View {
        sl("Width", "width", 0...2, 1); sl("Distance (3D)", "distance", 0...1, 0.3)
        pk("Mode", "mode", ["stereo", "3dspace"]); tg("Auto-pan", "autoPanOn")
        sl("Rate", "rate", 0.02...10, 1, unit: " Hz"); pk("Shape", "shape", ["square", "triangle", "exponential"]); sl("Mix", "mix", 0...1, 1)
        DisclosureGroup("Multiband panner") {
            tg("Multiband on", "mb.on")
            ForEach(0..<3, id: \.self) { k in sl("Crossover \(k + 1)", "mb.xover.\(k)", 20...18000, [200, 1500, 6000][k], fmt: "%.0f", log: true, unit: " Hz") }
            ForEach(0..<4, id: \.self) { b in
                Text("Band \(b + 1)").font(.caption.bold())
                tg("On", "mb.bands.\(b).on", true); sl("Width", "mb.bands.\(b).width", 0...1, 0.5); sl("Depth", "mb.bands.\(b).depth", 0...1, 0.5); sl("Speed", "mb.bands.\(b).speed", 0...1, 0.5)
                // optional per-band effect: patch another stage into this band (1-based chain position; 0 = none)
                HStack {
                    Text("Band FX").font(.caption); Spacer()
                    Picker("Band FX", selection: s.binding("\(base).params.mb.bands.\(b).fx", int: 0)) {
                        Text("None").tag(0)
                        ForEach(otherStages, id: \.id) { Text($0.name).tag($0.id) }
                    }.pickerStyle(.menu).labelsHidden()
                }
            }
        }
    }

    private var otherStages: [(id: Int, name: String)] {
        var out: [(Int, String)] = []
        for (i, st) in s.array("fxStages").enumerated() {
            guard i != index, let d = st as? [String: Any], let t = d["type"] as? String, t != "panning" else { continue }
            out.append((i + 1, "\(i + 1). \(t)"))   // band FX refers to a stage by its 1-based chain position (the web app numbers stages the same way on load)
        }
        return out
    }
}

/// FX send-source pill (web .fx-chain-item .fx-chip): a capsule with an LED. Lit LED + cyan edge = on; XFX keeps its orange identity.
enum FXStyle {
    static let cyan = Color(red: 0.15, green: 0.85, blue: 0.90)
    struct Source: Identifiable { let key: String; let name: String; var id: String { key } }
    static let sources: [Source] = [Source(key: "WT", name: "Wavetable"), Source(key: "FM", name: "FM"), Source(key: "SMP", name: "Sampler"), Source(key: "PRO", name: "Procedural"),
                                    Source(key: "SID", name: "SID"), Source(key: "LUA", name: "LUA"), Source(key: "XFX", name: "XFX mode"), Source(key: "VOC", name: "Vocoder")]
}

private struct SendChipStyle: ToggleStyle {
    let xfx: Bool
    func makeBody(configuration: Configuration) -> some View {
        let on = configuration.isOn; let led = xfx ? LayerID.color(6) : FXStyle.cyan
        return Button { configuration.isOn.toggle() } label: {
            HStack(spacing: 6) {
                Circle().fill(on ? led : Color.clear).overlay(Circle().strokeBorder(on ? led : (xfx ? led : Color.gray.opacity(0.7)), lineWidth: 1.5))
                    .frame(width: 8, height: 8).shadow(color: on ? led.opacity(0.7) : .clear, radius: 3)
                configuration.label.font(.system(.caption, design: .monospaced).weight(.semibold))
            }
            .padding(.horizontal, 10).frame(height: 28).frame(maxWidth: .infinity)
            .foregroundStyle(on ? Color(white: 0.93) : Color(white: 0.55))
            .background(Capsule().fill(on ? FXStyle.cyan.opacity(0.14) : Color.black.opacity(0.35)))
            .overlay(Capsule().strokeBorder(on ? FXStyle.cyan.opacity(0.75) : Color.gray.opacity(0.35), lineWidth: 1))
        }
        .buttonStyle(.plain).accessibilityAddTraits(on ? .isSelected : [])
    }
}
