// Reusable controls bound to PresetStore paths. Everything on the Sound / Mod / Sequencer / FX screens is built from these.
import SwiftUI

/// The accent colour follows the colour scheme chosen in Settings > Appearance (see Appearance.swift).
var vvCyan: Color { VVAppearance.accent }

struct SliderRow: View {
    @ObservedObject var s: PresetStore
    let label: String; let path: String; let range: ClosedRange<Double>
    var def: Double = 0; var fmt: String = "%.2f"; var log = false; var unit = ""
    var taper: Bool? = nil       // nil = automatic: wide positive ranges (50x+) turn on a log taper so equal turns are equal musical ratios (R11)
    var modDest: String? = nil   // matrix destination key: long-press the dial to route a modulation source to it (web: modKeyFor)
    private var hasRoute: Bool { modDest.map { d in s.array("connections").contains { ($0 as? [String: Any])?["dest"] as? String == d } } ?? false }
    private var useLog: Bool {
        if log { return true }; if let t = taper { return t }
        let skip = ["sustain", "level", "mix", "vol", "gain", "depth", "amount", "pan", "wet", "dry"]
        return range.lowerBound > 0 && range.upperBound / range.lowerBound >= 50 && !skip.contains { path.lowercased().contains($0) }
    }
    private var norm: Binding<Double> {
        Binding(get: {
            let v = s.double(path, def)
            if useLog { let lo = max(range.lowerBound, 1e-6); return (Foundation.log(max(v, lo)) - Foundation.log(lo)) / (Foundation.log(range.upperBound) - Foundation.log(lo)) }
            return (v - range.lowerBound) / (range.upperBound - range.lowerBound)
        }, set: { t in
            let c = min(max(t, 0), 1)
            if useLog { let lo = max(range.lowerBound, 1e-6); s.set(path, exp(Foundation.log(lo) + c * (Foundation.log(range.upperBound) - Foundation.log(lo)))) }
            else { s.set(path, range.lowerBound + c * (range.upperBound - range.lowerBound)) }
        })
    }
    @State private var startNorm: Double?
    @State private var dragT: Double = 0
    @State private var lastTy: CGFloat = 0
    @State private var lastTx: CGFloat = 0
    @State private var startValue: Double?
    @State private var clipRecorded = false
    var body: some View {
        VStack(spacing: 4) {
            HStack(spacing: 3) { Text(label).font(.system(size: 11)).lineLimit(2).multilineTextAlignment(.center).minimumScaleFactor(0.8); if hasRoute { Circle().fill(vvCyan).frame(width: 6, height: 6) } }.frame(height: 28, alignment: .bottom)
            VVDialFace(norm: norm.wrappedValue, modNorm: nil, light: vvCyan, size: 52, active: startNorm != nil, steps: 0)
                .frame(width: 52, height: 52)
                .contentShape(Circle())
                .gesture(DragGesture(minimumDistance: 2).onChanged { g in
                    if startNorm == nil { startNorm = norm.wrappedValue; startValue = s.double(path, def); dragT = norm.wrappedValue; lastTy = g.translation.height; lastTx = g.translation.width }
                    // R11: relative, incremental drag in the dial's own (tapered) 0...1 space - 220 pt = full sweep. Dragging sideways away from the dial slows it
                    // down to 0.1x (fine control with a finger, no modifier needed); a hardware Shift / Ctrl key held does the same.
                    let away = abs(g.translation.width), fac = max(0.1, 1 / (1 + max(0, Double(away) - 30) / 45))
                    dragT = min(max(dragT + Double(lastTy - g.translation.height) / 220 * fac, 0), 1); lastTy = g.translation.height; lastTx = g.translation.width
                    norm.wrappedValue = dragT
                    // a clip is recording: the move is written into the clip's own automation (the web dial does the same)
                    if let d = modDest, ClipRecorder.shared.isActive, let sn = startNorm { ClipRecorder.shared.write(key: d, delta: norm.wrappedValue - sn, bipolar: range.lowerBound < 0); clipRecorded = true }
                }.onEnded { _ in
                    if clipRecorded, let d = modDest, let sv = startValue { clipRecorded = false; s.set(path, sv); ClipRecorder.shared.end(key: d) }   // the dial returns; the clip plays the move back
                    startNorm = nil; startValue = nil })
                .onTapGesture(count: 2) { s.set(path, def) }   // double-tap resets
                .accessibilityElement(children: .ignore)
                .accessibilityLabel(label)
                .accessibilityValue(String(format: fmt, s.double(path, def)) + unit)
                .accessibilityAdjustableAction { dir in norm.wrappedValue += dir == .increment ? 0.05 : -0.05 }
            Text(String(format: fmt, s.double(path, def)) + unit).font(.system(size: 11, design: .monospaced)).lineLimit(1).minimumScaleFactor(0.7).foregroundStyle(.white).frame(width: 74).padding(.vertical, 2).background(RoundedRectangle(cornerRadius: 3).fill(Color(red: 0.12, green: 0.15, blue: 0.19)).overlay(RoundedRectangle(cornerRadius: 3).stroke(Color.white.opacity(0.14), lineWidth: 1)))
        }
        .frame(width: 86)
        .contentShape(Rectangle())
        .contextMenu {   // iOS shows this on a long press
            if let d = modDest {
                ForEach(["lfo1", "lfo2", "lfo3", "lfo4", "lfo5", "lfo6", "velocity", "note", "modwheel", "random", "envWT", "envFM", "envSMP", "envFollow", "macro1", "macro2", "macro3", "macro4", "xy1x", "xy1y", "xy2x", "xy2y", "lorenz", "rossler"], id: \.self) { src in
                    Button("Modulate with \(src)") { s.append("connections", ["source": src, "dest": d, "amount": 30]) } }
                if hasRoute { Button("Remove routes to \(label)", role: .destructive) {
                    for i in stride(from: s.count("connections") - 1, through: 0, by: -1) where (s.array("connections")[i] as? [String: Any])?["dest"] as? String == d { s.remove("connections", at: i) } } }
            }
        }
    }
}

struct ToggleRow: View {
    @ObservedObject var s: PresetStore
    let label: String; let path: String; var def = false
    var body: some View { Toggle(label, isOn: s.binding(path, bool: def)).font(.caption).toggleStyle(.switch).frame(width: 300) }
}

struct PickerRow: View {
    @ObservedObject var s: PresetStore
    let label: String; let path: String; let options: [String]; var def: String? = nil
    var labels: [String: String] = [:]   // optional display names for option values (custom wavetables use "custom:<id>" values)
    var body: some View {
        HStack {
            Text(label).font(.caption)
            Spacer()
            Picker(label, selection: s.binding(path, string: def ?? options.first ?? "")) {
                ForEach(options, id: \.self) { Text(labels[$0] ?? $0).tag($0) }
            }.pickerStyle(.menu).labelsHidden()
        }.frame(width: 300)
    }
}

/// Integer-valued stepper (octave, unison, algorithm index …)
struct StepperRow: View {
    @ObservedObject var s: PresetStore
    let label: String; let path: String; let range: ClosedRange<Int>; var def = 0
    var body: some View { Stepper("\(label): \(s.int(path, def))", value: s.binding(path, int: def), in: range).font(.caption).frame(width: 260) }
}

/// Picker over a numeric value with named choices (e.g. arp division 1 / 0.5 / 0.25).
struct ChoiceRow: View {
    @ObservedObject var s: PresetStore
    let label: String; let path: String; let choices: [(String, Double)]; var def = 0.0
    var body: some View {
        HStack {
            Text(label).font(.caption); Spacer()
            Picker(label, selection: Binding(get: { choices.min(by: { abs($0.1 - s.double(path, def)) < abs($1.1 - s.double(path, def)) })?.1 ?? def }, set: { s.set(path, $0) })) {
                ForEach(choices, id: \.1) { Text($0.0).tag($0.1) }
            }.pickerStyle(.menu).labelsHidden()
        }.frame(width: 300)
    }
}

/// Vertical fader bound to a PresetStore path (amp envelope A / D / S / R, XFX editor). Drag anywhere on the track; double-tap resets.
struct VFaderControl: View {
    @ObservedObject var s: PresetStore
    let label: String; let path: String; let range: ClosedRange<Double>
    var def: Double = 0; var fmt: String = "%.2f"; var log = false; var unit = ""
    private func norm(_ v: Double) -> Double {
        if log { let lo = max(range.lowerBound, 1e-6); return (Foundation.log(max(v, lo)) - Foundation.log(lo)) / (Foundation.log(range.upperBound) - Foundation.log(lo)) }
        return (v - range.lowerBound) / (range.upperBound - range.lowerBound)
    }
    private func value(_ t: Double) -> Double {
        let c = min(max(t, 0), 1)
        if log { let lo = max(range.lowerBound, 1e-6); return exp(Foundation.log(lo) + c * (Foundation.log(range.upperBound) - Foundation.log(lo))) }
        return range.lowerBound + c * (range.upperBound - range.lowerBound)
    }
    var body: some View {
        VStack(spacing: 4) {
            Text(label).font(.caption2).lineLimit(1)
            GeometryReader { geo in
                let h = geo.size.height, t = norm(s.double(path, def))
                ZStack(alignment: .bottom) {
                    Capsule().fill(Color.secondary.opacity(0.25)).frame(width: 6)
                    Capsule().fill(vvCyan).frame(width: 6, height: max(4, h * t))
                    Circle().fill(Color.white).overlay(Circle().stroke(vvCyan, lineWidth: 2)).frame(width: 22, height: 22).offset(y: -(h - 22) * t)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0).onChanged { g in s.set(path, value(1 - Double(g.location.y / max(h, 1)))) })
                .onTapGesture(count: 2) { s.set(path, def) }
            }
            .frame(height: 130)
            Text(String(format: fmt, s.double(path, def)) + unit).font(.caption2.monospacedDigit()).foregroundStyle(.white).padding(.horizontal, 5).padding(.vertical, 1).background(RoundedRectangle(cornerRadius: 2).fill(Color(red: 0.169, green: 0.18, blue: 0.208)))
        }
        .frame(width: 58)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(label)
        .accessibilityValue(String(format: fmt, s.double(path, def)) + unit)
        .accessibilityAdjustableAction { dir in s.set(path, value(norm(s.double(path, def)) + (dir == .increment ? 0.05 : -0.05))) }
    }
}

/// Envelope repeat choices (Off / 1...8 extra Attack+Decay cycles / Continuous until note-off), stored as env.rep (0, 1...8, -1).
let envRepeatChoices: [(String, Double)] = {
    var choices: [(String, Double)] = [("Off", 0)]
    for count in 1...8 {
        let suffix = count == 1 ? "" : "s"
        choices.append(("\(count) repeat\(suffix)", Double(count)))
    }
    choices.append(("Continuous", -1))
    return choices
}()

/// Amp envelope for a path prefix that holds a/d/s/r/rep: four vertical faders plus the Repeat menu.
struct EnvEditor: View {
    @ObservedObject var s: PresetStore
    let title: String; let prefix: String
    var body: some View {
        DisclosureGroup(title) {
            EnvVizView(s: s, prefix: prefix)
            HStack(alignment: .top, spacing: 8) {
                VFaderControl(s: s, label: "Attack", path: prefix + ".a", range: 0.001...5, def: 0.01, fmt: "%.3f", log: true, unit: " s")
                VFaderControl(s: s, label: "Decay", path: prefix + ".d", range: 0.001...5, def: 0.2, fmt: "%.3f", log: true, unit: " s")
                VFaderControl(s: s, label: "Sustain", path: prefix + ".s", range: 0...1, def: 0.7)
                VFaderControl(s: s, label: "Release", path: prefix + ".r", range: 0.001...8, def: 0.3, fmt: "%.3f", log: true, unit: " s")
                Spacer(minLength: 0)
            }
            ChoiceRow(s: s, label: "Repeat", path: prefix + ".rep", choices: envRepeatChoices, def: 0)
        }
    }
}

let formantTables = ["classic", "male", "female"]
let formantVowels = ["a", "e", "i", "o", "u", "ae", "er", "schwa", "ih", "uh", "oh", "turn", "y", "oe"]
let filterTypes = ["moog4pole", "diode4pole", "onepole6db", "twopole12db", "statevariable", "comb", "highpass", "bandpass", "formant", "lpg", "notch", "isem", "sid6581", "sid8580"]

/// Voice filter editor (type, cutoff, reso, keytrack, volume, LPG decay) + its envelope.
struct FilterEditor: View {
    @ObservedObject var s: PresetStore
    let title: String; let prefix: String; let envPrefix: String
    var body: some View {
        DisclosureGroup(title) {
            FilterVizView(s: s, prefix: prefix)
            Group {
            ToggleRow(s: s, label: "On", path: prefix + ".on", def: true)
            PickerRow(s: s, label: "Type", path: prefix + ".type", options: filterTypes, def: "moog4pole")
            PickerRow(s: s, label: "Formant table", path: prefix + ".formantTable", options: formantTables, def: "classic")
            PickerRow(s: s, label: "Vowel", path: prefix + ".vowel", options: formantVowels, def: "a")
            SliderRow(s: s, label: "Cutoff", path: prefix + ".cutoff", range: 20...18000, def: 4000, fmt: "%.0f", log: true, unit: " Hz")
            SliderRow(s: s, label: "Resonance", path: prefix + ".reso", range: 0...130, def: 20, fmt: "%.0f")
            SliderRow(s: s, label: "Key tracking", path: prefix + ".keytrack", range: -100...200, def: 0, fmt: "%.0f", unit: " %")
            SliderRow(s: s, label: "Volume", path: prefix + ".volume", range: 0...2, def: 1)
            SliderRow(s: s, label: "LPG decay", path: prefix + ".lpgDecay", range: 0.05...3, def: 0.8)
            }
            Group {
            SliderRow(s: s, label: "Env amount", path: envPrefix + ".amount", range: -18000...18000, def: 0, fmt: "%.0f", unit: " Hz")
            SliderRow(s: s, label: "Env attack", path: envPrefix + ".a", range: 0.001...5, def: 0.01, fmt: "%.3f", log: true, unit: " s")
            SliderRow(s: s, label: "Env decay", path: envPrefix + ".d", range: 0.001...5, def: 0.2, fmt: "%.3f", log: true, unit: " s")
            SliderRow(s: s, label: "Env sustain", path: envPrefix + ".s", range: 0...1, def: 0.3)
            SliderRow(s: s, label: "Env release", path: envPrefix + ".r", range: 0.001...8, def: 0.3, fmt: "%.3f", log: true, unit: " s")
            EnvVizView(s: s, prefix: envPrefix, amountPath: envPrefix + ".amount")
            CurveButton(s: s, path: envPrefix + ".shape", label: "DRAW CUSTOM SHAPE", title: "Filter envelope")
            }
        }
    }
}

/// Common per-oscillator performance controls (pitch, pan, glide, zones, hold)
struct OscCommonEditor: View {
    @ObservedObject var s: PresetStore
    let prefix: String; var drift = true
    var body: some View {
        Group {
            StepperRow(s: s, label: "Octave", path: prefix + ".oct", range: -4...4)
            StepperRow(s: s, label: "Semitone", path: prefix + ".semitone", range: -12...12)
            SliderRow(s: s, label: "Fine tune", path: prefix + ".fineTune", range: -100...100, fmt: "%.0f", unit: " ct")
            SliderRow(s: s, label: "Pan", path: prefix + ".pan", range: -1...1)
            if drift { SliderRow(s: s, label: "Drift", path: prefix + ".drift", range: 0...30, fmt: "%.1f") }
            SliderRow(s: s, label: "Glide", path: prefix + ".glideTime", range: 0...3, unit: " s")
            SliderRow(s: s, label: "Key tracking", path: prefix + ".pitchKeytrack", range: 0...200, def: 100, fmt: "%.0f", unit: " %")
            StepperRow(s: s, label: "Zone low", path: prefix + ".rangeLow", range: 0...127, def: 0)
            StepperRow(s: s, label: "Zone high", path: prefix + ".rangeHigh", range: 0...127, def: 127)
            ToggleRow(s: s, label: "Hold (latch this layer)", path: prefix + ".hold")
        }
    }
}

/// The web app's keyboard scale table (index 0 = Off): used by the keyboard (highlight / scale lock) and the sequencer's RANDOMISE.
let kbScales: [(id: String, label: String, iv: [Int])] = [
    ("", "Off", []), ("major", "Major", [0, 2, 4, 5, 7, 9, 11]), ("fSharpMajor", "F# Major", [1, 3, 5, 6, 8, 10, 11]), ("minor", "Natural minor", [0, 2, 3, 5, 7, 8, 10]),
    ("harmonicMinor", "Harmonic minor", [0, 2, 3, 5, 7, 8, 11]), ("dorian", "Dorian", [0, 2, 3, 5, 7, 9, 10]), ("mixolydian", "Mixolydian", [0, 2, 4, 5, 7, 9, 10]),
    ("majorPent", "Major pentatonic", [0, 2, 4, 7, 9]), ("minorPent", "Minor pentatonic", [0, 3, 5, 7, 10]), ("blues", "Blues", [0, 3, 5, 6, 7, 10]),
    ("majorBlues", "Major blues", [0, 2, 3, 4, 7, 9]), ("jazz", "Jazz (bebop dominant)", [0, 2, 4, 5, 7, 9, 10, 11]), ("altered", "Jazz (altered)", [0, 1, 3, 4, 6, 8, 10]),
    ("japanese", "Japanese (Hirajoshi)", [0, 2, 3, 7, 8]), ("kumoi", "Japanese (Kumoi)", [0, 2, 3, 7, 9]), ("inSen", "Japanese (In Sen)", [0, 1, 5, 7, 10]),
    ("chineseGong", "Chinese (Gong)", [0, 4, 6, 7, 11]), ("chineseShang", "Chinese (Shang)", [0, 2, 5, 7, 10]), ("spanish", "Spanish (Phrygian dominant)", [0, 1, 4, 5, 7, 8, 10]),
    ("egyptian", "Egyptian", [0, 2, 5, 7, 10]), ("indonesian", "Indonesian (Pelog-ish)", [0, 1, 3, 7, 8]), ("filipino", "Filipino (Kulintang-ish)", [0, 2, 3, 7, 9]),
]
func kbInScale(_ idx: Int, _ root: Int, _ note: Int) -> Bool {
    guard idx > 0 && idx < kbScales.count else { return false }
    return kbScales[idx].iv.contains(((note - root) % 12 + 12) % 12)
}

/// "NEW NOTE NEXT SAMPLE" (web layer.slotAdvance = off | next | random): tap = on/off, double tap = Next <-> Random (on/off stays as it was).
struct SlotAdvanceRow: View {
    @ObservedObject var s: PresetStore
    @State private var flavour = "next"
    private var mode: String { s.string("layers.SMP.slotAdvance", "off") }
    private var on: Bool { mode == "next" || mode == "random" }
    private var shown: String { (mode == "random" || (!on && flavour == "random")) ? "RANDOM" : "NEXT" }
    var body: some View {
        Text("NEW NOTE \(shown) SAMPLE: \(on ? "ON" : "OFF")").font(.caption.bold()).frame(maxWidth: .infinity).padding(8)
            .background(on ? Color.green.opacity(0.35) : Color.gray.opacity(0.25)).clipShape(RoundedRectangle(cornerRadius: 6))
            .onAppear { flavour = mode == "random" ? "random" : "next" }
            .onTapGesture(count: 2) { flavour = (mode == "random" || (!on && flavour == "random")) ? "next" : "random"; s.set("layers.SMP.slotAdvance", on ? flavour : "off") }
            .onTapGesture { s.set("layers.SMP.slotAdvance", on ? "off" : flavour) }
    }
}
