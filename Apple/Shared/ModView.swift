// MOD tab: LFOs, macros, envelope follower, modulation matrix, XY pads (modulation sources), cross-modulation and vocoder.
import SwiftUI

let modSources = ["lfo1", "lfo2", "lfo3", "lfo4", "lfo5", "lfo6", "velocity", "modwheel", "random", "envWT", "envFM", "envSMP", "envFollow",
                  "macro1", "macro2", "macro3", "macro4", "mpeTimbre", "mpePressure", "wtOsc1Pos", "wtOsc2Pos", "wtOsc1Shape", "wtOsc2Shape", "note",
                  "xy1x", "xy1y", "xy2x", "xy2y", "xy3x", "xy3y", "xy4x", "xy4y"]

/// Destinations by group. Per-voice destinations exist for each of the four layers.
let modDestGroups: [(String, [String])] = {
    var g: [(String, [String])] = []
    for l in ["WT", "FM", "SMP", "PRO"] { g.append((l + " voice", ["\(l):cutoff", "\(l):reso", "\(l):level", "\(l):pitch", "\(l):filterVolume"])) }
    g.append(("WT oscillators", ["WT:osc1Cutoff", "WT:osc2Cutoff", "WT:osc1Reso", "WT:osc2Reso", "WT:osc1Octave", "WT:osc2Octave", "WT:osc1Semi", "WT:osc2Semi", "WT:osc1Fine", "WT:osc2Fine",
        "WT:start", "WT:scan", "WT:pwm", "WT:phase", "WT:startEnd", "WT:osc1Scan", "WT:osc2Scan", "WT:osc1Pwm", "WT:osc2Pwm", "WT:osc1PwmRate", "WT:osc2PwmRate", "SID:voice1Pwm", "SID:voice2Pwm", "SID:voice3Pwm", "FM6OP:fine", "FM6OP:detune", "FM6OP:level", "FM6OP:ampModDepth", "FM6OP:r2", "FM6OP:r3", "FM6OP:r4", "FM6OP:l1", "FM6OP:l2", "WT:osc1Phase", "WT:osc2Phase",
        "WT:osc1StartEnd", "WT:osc2StartEnd", "WT:osc1Segment", "WT:osc2Segment", "WT:osc1Level", "WT:osc2Level"]))
    g.append(("FM engines", ["FM:engACutoff", "FM:engBCutoff", "FM:engAReso", "FM:engBReso", "FM:engAOctave", "FM:engBOctave", "FM:engASemi", "FM:engBSemi", "FM:engAFine", "FM:engBFine",
        "FM:engAFeedback", "FM:engBFeedback"] + (1...8).map { "FM:op\($0)Ratio" }))
    g.append(("Procedural", ["A", "B"].flatMap { e in ["Tone", "Damp", "Body", "Chaos"].map { "PRO:gen\(e)\($0)" } }))
    g.append(("Sampler", ["SMP:start", "SMP:end", "GLOBAL:sampleMix"]))
    g.append(("Global", ["GLOBAL:macro1", "GLOBAL:macro2", "GLOBAL:macro3", "GLOBAL:macro4", "GLOBAL:wtFmMix", "GLOBAL:masterTone", "GLOBAL:xmod1", "GLOBAL:xmod2", "GLOBAL:xmod3", "GLOBAL:xmod4", "ARP:rate"]))
    g.append(("LFOs", (1...6).flatMap { ["LFO\($0):rate", "LFO\($0):amount", "LFO\($0):phase"] }))
    g.append(("Effects", ["FX:reverbMix", "FX:satMix", "FX:satDrive", "FX:flangerRate", "FX:flangerMix", "FX:flangerFeedback", "FX:foldDrive", "FX:foldMix", "FX:foldSymmetry", "FX:foldPwm",
        "FX:specBlur", "FX:specShift", "FX:specThreshold", "FX:specSpread", "FX:specMix", "FX:filterCutoff", "FX:filterResonance", "FX:grainSize", "FX:grainPitch", "FX:grainPan", "FX:grainDensity",
        "FX:delayFeedback", "FX:delayMix", "FX:stutterDivision", "FX:bitDepth", "FX:phaserMix", "FX:phaserRate", "FX:phaserFeedback", "FX:resonatorFreq", "FX:resonatorResonance", "FX:resonatorMix"]))
    g.append(("FX parameters (per effect type)", ["FXP:reverb:size", "FXP:reverb:decay", "FXP:reverb:predelay", "FXP:reverb:mod", "FXP:reverb:colour", "FXP:delay:time", "FXP:saturation:tone", "FXP:spectral:freeze", "FXP:granular:spread", "FXP:granular:followerAmount", "FXP:granular:mix", "FXP:granular:delay", "FXP:granular:posRand", "FXP:granular:sizeRand", "FXP:granular:jitter", "FXP:granular:fine", "FXP:granular:pitchRand", "FXP:granular:reverse", "FXP:granular:volRand", "FXP:granular:width", "FXP:granular:drift", "FXP:granular:lrSpread", "FXP:granular:outLevel", "FXP:granular:curve", "FXP:granular:symmetry", "FXP:granular:atk", "FXP:granular:dec", "FXP:granular:sus", "FXP:granular:rel", "FXP:granular:fb", "FXP:granular:fbDamp", "FXP:granular:fbLow", "FXP:granular:fbSat", "FXP:granular:fbDiff", "FXP:granular:pingPong", "FXP:granular:rndDepth", "FXP:granular:rndRate", "FXP:granular:seed", "FXP:tape:wowRate", "FXP:tape:wowDepth", "FXP:tape:flutterRate", "FXP:tape:flutterDepth", "FXP:tape:dust", "FXP:tape:crackle", "FXP:tape:mix", "FXP:phaser:depth", "FXP:phaser:stages", "FXP:flanger:depth", "FXP:glitch:srReduction", "FXP:glitch:stutterAmount", "FXP:glitch:mix", "FXP:chorus:mix", "FXP:panning:width", "FXP:panning:rate", "FXP:panning:mix", "FXP:panning:distance", "FXP:filter:mix", "FXP:interstellar:span", "FXP:interstellar:tonal", "FXP:interstellar:band1", "FXP:interstellar:band2", "FXP:interstellar:band3", "FXP:interstellar:band4", "FXP:interstellar:band5", "FXP:interstellar:band6", "FXP:interstellar:segments", "FXP:interstellar:segTime", "FXP:interstellar:stretch", "FXP:interstellar:bloom", "FXP:interstellar:density", "FXP:interstellar:life", "FXP:interstellar:pitchUp", "FXP:interstellar:upProb", "FXP:interstellar:pitchDown", "FXP:interstellar:downProb", "FXP:interstellar:body", "FXP:interstellar:particles", "FXP:interstellar:spread", "FXP:interstellar:resonance", "FXP:interstellar:ring", "FXP:interstellar:strum", "FXP:interstellar:evolve", "FXP:interstellar:drift", "FXP:interstellar:space", "FXP:interstellar:size", "FXP:interstellar:decay", "FXP:interstellar:predelay", "FXP:interstellar:damp", "FXP:interstellar:revMod", "FXP:interstellar:width", "FXP:interstellar:shimmer", "FXP:interstellar:character", "FXP:interstellar:mix", "FXP:interstellar:level", "FXP:interstellar:freeze", "FXP:interstellar:revFreeze"]))
    g.append(("Generic layer params (envelope, pan, width, drift ...)", ["GEN:WT.pan", "GEN:WT.attack", "GEN:WT.decay", "GEN:WT.sustain", "GEN:WT.release", "GEN:FM.pan", "GEN:FM.attack", "GEN:FM.decay", "GEN:FM.sustain", "GEN:FM.release", "GEN:SMP.pan", "GEN:SMP.attack", "GEN:SMP.decay", "GEN:SMP.sustain", "GEN:SMP.release", "GEN:PRO.pan", "GEN:PRO.attack", "GEN:PRO.decay", "GEN:PRO.sustain", "GEN:PRO.release", "GEN:WT.width", "GEN:WT.detune", "GEN:WT.drift", "GEN:PRO.drift", "GEN:PRO.level", "GEN:SMP.micMix", "GEN:SMP.spkMix", "GEN:SMP.octave", "GEN:SMP.semi", "GEN:SMP.fine", "GEN:PRO.octave", "GEN:PRO.semi", "GEN:PRO.fine", "GEN:SID.pan", "GEN:SID.attack", "GEN:SID.decay", "GEN:SID.sustain", "GEN:SID.release", "GEN:SID.octave", "GEN:SID.semi", "GEN:SID.fine", "GEN:SID.vibrato", "GEN:WT2.attack", "GEN:WT2.decay", "GEN:WT2.sustain", "GEN:WT2.release", "GEN:FM2.attack", "GEN:FM2.decay", "GEN:FM2.sustain", "GEN:FM2.release", "GEN:WT.unisonBlend", "GEN:WT.unisonPhase", "GEN:WT.eqBand1Freq", "GEN:WT.eqBand1Gain", "GEN:WT.eqBand2Freq", "GEN:WT.eqBand2Gain", "GEN:WT.eqBand3Freq", "GEN:WT.eqBand3Gain", "GEN:WT.eqBand4Freq", "GEN:WT.eqBand4Gain", "GEN:FM.eqBand1Freq", "GEN:FM.eqBand1Gain", "GEN:FM.eqBand2Freq", "GEN:FM.eqBand2Gain", "GEN:FM.eqBand3Freq", "GEN:FM.eqBand3Gain", "GEN:FM.eqBand4Freq", "GEN:FM.eqBand4Gain", "GEN:SMP.eqBand1Freq", "GEN:SMP.eqBand1Gain", "GEN:SMP.eqBand2Freq", "GEN:SMP.eqBand2Gain", "GEN:SMP.eqBand3Freq", "GEN:SMP.eqBand3Gain", "GEN:SMP.eqBand4Freq", "GEN:SMP.eqBand4Gain", "GEN:PRO.eqBand1Freq", "GEN:PRO.eqBand1Gain", "GEN:PRO.eqBand2Freq", "GEN:PRO.eqBand2Gain", "GEN:PRO.eqBand3Freq", "GEN:PRO.eqBand3Gain", "GEN:PRO.eqBand4Freq", "GEN:PRO.eqBand4Gain", "GEN:SID.eqBand1Freq", "GEN:SID.eqBand1Gain", "GEN:SID.eqBand2Freq", "GEN:SID.eqBand2Gain", "GEN:SID.eqBand3Freq", "GEN:SID.eqBand3Gain", "GEN:SID.eqBand4Freq", "GEN:SID.eqBand4Gain", "GEN:LUA.eqBand1Freq", "GEN:LUA.eqBand1Gain", "GEN:LUA.eqBand2Freq", "GEN:LUA.eqBand2Gain", "GEN:LUA.eqBand3Freq", "GEN:LUA.eqBand3Gain", "GEN:LUA.eqBand4Freq", "GEN:LUA.eqBand4Gain", "GEN:VOC.voc_mix", "GEN:VOC.voc_formantSt", "GEN:VOC.voc_q", "GEN:VOC.voc_releaseMs", "GEN:VOC.voc_hpf", "GEN:VOC.voc_micGainDb", "GEN:VOC.voc_noiseDb", "GEN:VOC.voc_sibSens", "GEN:VOC.voc_pitchSpeed", "GEN:VOC.voc_chorusRate"]))
    return g
}()

struct ModView: View {
    @ObservedObject var s: PresetStore
    @State private var page = 0
    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                Picker("Page", selection: $page) { Text("LFO").tag(0); Text("Matrix").tag(1); Text("Cross-mod").tag(2); Text("Vocoder").tag(3); Text("XY").tag(4) }
                    .pickerStyle(.segmented).padding([.horizontal, .top])
                Form {
                    switch page {
                    case 0: LFOPage(s: s)
                    case 1: MatrixPage(s: s)
                    case 2: XmodPage(s: s)
                    case 3: VocoderPage(s: s)
                    default: XYPadsPage(s: s)
                    }
                }
            }.navigationTitle("Modulation").navigationBarTitleDisplayMode(.inline)
        }
    }
}

struct LFOPage: View {
    @ObservedObject var s: PresetStore
    var body: some View {
        Section("Macros") {
            ForEach(0..<4, id: \.self) { i in SliderRow(s: s, label: "Macro \(i + 1)", path: "macros.\(i)", range: 0...1) }
        }
        ForEach(1...6, id: \.self) { n in
            let p = "lfo.lfo\(n)"
            Section("LFO \(n)") {
                HStack {
                    Text("Waveform").font(.caption); Spacer()
                    Picker("Waveform", selection: Binding(get: { s.string(p + ".waveform", "sine") }, set: { v in
                        let prev = s.string(p + ".waveform", "sine")
                        if v == "custom" && Curve.read(s, p + ".shape").count < 2 { Curve.write(s, p + ".shape", Curve.basic(prev)) }   // start from the shape that was selected
                        s.set(p + ".waveform", v) })) { ForEach(["sine", "triangle", "pulse", "sawUp", "sawDown", "sh", "noise", "staircase", "stairsDown", "stroll", "organic", "morph", "morph2", "custom"], id: \.self) { Text($0).tag($0) } }.pickerStyle(.menu).labelsHidden()
                }
                LfoPreview(s: s, index: n - 1)
                if s.string(p + ".waveform", "sine") == "custom" { CurveButton(s: s, path: p + ".shape", label: "DRAW SHAPE", title: "LFO \(n) shape") }
                PickerRow(s: s, label: "Rate mode", path: p + ".rateMode", options: ["hz", "sync"], def: "hz")
                if s.string(p + ".rateMode", "hz") == "sync" {
                    ChoiceRow(s: s, label: "Division", path: p + ".division", choices: [("16/1", 64), ("8/1", 32), ("4/1", 16), ("2/1", 8), ("1/1", 4), ("1/2", 2), ("1/4", 1), ("1/8", 0.5), ("1/16", 0.25), ("1/32", 0.125)], def: 0.25)
                } else {
                    SliderRow(s: s, label: "Rate", path: p + ".hz", range: 0.02...20, def: 4, fmt: "%.2f", log: true, unit: " Hz")
                }
                SliderRow(s: s, label: "Amount", path: p + ".amount", range: 0...2, def: 1)
                SliderRow(s: s, label: "Phase offset", path: p + ".phaseOffset", range: 0...1)
                SliderRow(s: s, label: "Smoothing", path: p + ".smoothing", range: 0...1, def: 0)
                SliderRow(s: s, label: "Morph (Morph waveforms)", path: p + ".morph", range: 0...1, def: 0.5)
                Toggle("Retrigger on note", isOn: Binding(get: { s.string(p + ".mode", "free") == "retrig" }, set: { s.set(p + ".mode", $0 ? "retrig" : "free") })).font(.caption)
                ToggleRow(s: s, label: "Hold", path: p + ".held")
                ToggleRow(s: s, label: "Bipolar", path: p + ".bipolar", def: true)
            }
        }
        Section("Envelope follower") {
            SliderRow(s: s, label: "Attack", path: "envFollow.a", range: 0.001...2, def: 0.01, fmt: "%.3f", log: true, unit: " s")
            SliderRow(s: s, label: "Decay", path: "envFollow.d", range: 0.001...2, def: 0.3, fmt: "%.3f", log: true, unit: " s")
            SliderRow(s: s, label: "Sustain", path: "envFollow.s", range: 0...1, def: 0.4)
            SliderRow(s: s, label: "Release", path: "envFollow.r", range: 0.001...4, def: 0.4, fmt: "%.3f", log: true, unit: " s")
            CurveButton(s: s, path: "envFollow.shape", label: "DRAW CUSTOM SHAPE", title: "Global envelope follower")
        }
    }
}

struct MatrixPage: View {
    @ObservedObject var s: PresetStore
    var body: some View {
        Section("Routes (\(s.count("connections")))") {
            ForEach(0..<s.count("connections"), id: \.self) { i in
                let p = "connections.\(i)"
                VStack(alignment: .leading, spacing: 4) {
                    HStack {
                        Picker("Source", selection: s.binding(p + ".source", string: "lfo1")) { ForEach(modSources, id: \.self) { Text($0).tag($0) } }.pickerStyle(.menu).labelsHidden()
                        Image(systemName: "arrow.right").foregroundStyle(.secondary)
                        Picker("Destination", selection: s.binding(p + ".dest", string: "WT:cutoff")) {
                            ForEach(modDestGroups, id: \.0) { grp in Section(grp.0) { ForEach(grp.1, id: \.self) { Text($0).tag($0) } } }
                        }.pickerStyle(.menu).labelsHidden()
                        Spacer()
                        Button(role: .destructive) { s.remove("connections", at: i) } label: { Image(systemName: "trash") }.buttonStyle(.borderless)
                    }
                    SliderRow(s: s, label: "Amount", path: p + ".amount", range: -100...100, def: 0, fmt: "%.0f", unit: " %")
                }
            }
            Button { s.append("connections", ["source": "lfo1", "dest": "WT:cutoff", "amount": 30]) } label: { Label("Add route", systemImage: "plus") }
        }
        Section("Wire Overview (tap a cell to add / remove a wire; tap a group header to expand / collapse)") { WireOverviewView(s: s) }
    }
}

struct XmodPage: View {
    @ObservedObject var s: PresetStore
    private let layers = ["WT", "FM", "SMP", "PRO"]
    var body: some View {
        Section { Text("The source layer's own sound (tapped before the WT/FM mixer and solo/mute) modulates the target layer. FM applies to notes played after the change; RM, AM and PM are live.").font(.caption).foregroundStyle(.secondary) }
        ForEach(0..<4, id: \.self) { i in
            let p = "xmod.\(i)"
            Section("Slot \(i + 1)") {
                ToggleRow(s: s, label: "On", path: p + ".on")
                PickerRow(s: s, label: "Source layer", path: p + ".src", options: layers, def: "WT")
                PickerRow(s: s, label: "Target layer", path: p + ".dst", options: layers, def: "FM")
                PickerRow(s: s, label: "Type", path: p + ".type", options: ["RM", "AM", "PM", "FM"], def: "RM")
                SliderRow(s: s, label: "Amount", path: p + ".amount", range: 0...1, def: 0.5)
            }
        }
    }
}

struct VocoderPage: View {
    @ObservedObject var s: PresetStore
    var body: some View {
        Section("Hybrid Vocoder & Vocal Resynthesis") {
            ToggleRow(s: s, label: "On", path: "vocoder.on")
            PickerRow(s: s, label: "Mode", path: "vocoder.mode", options: ["classic", "pitch", "spectral"], def: "classic")
            PickerRow(s: s, label: "Modulator", path: "vocoder.modSrc", options: ["mic", "WT", "FM", "SMP", "PRO"], def: "WT")
            PickerRow(s: s, label: "Carrier", path: "vocoder.carSrc", options: ["internal", "SMP", "WT", "FM", "PRO", "external"], def: "SMP")
            SliderRow(s: s, label: "Dry / wet", path: "vocoder.mix", range: 0...1, def: 0.8)
        }
        Section("Modulator conditioning") {
            SliderRow(s: s, label: "Mic gain (dB)", path: "vocoder.micGainDb", range: -24...36, def: 0)
            SliderRow(s: s, label: "High-pass (Hz)", path: "vocoder.hpf", range: 80...150, def: 100)
            SliderRow(s: s, label: "Gate (dB)", path: "vocoder.gateDb", range: -80...(-20), def: -80)
            ToggleRow(s: s, label: "Compressor / limiter", path: "vocoder.comp")
            SliderRow(s: s, label: "Sibilance sensitivity", path: "vocoder.sibSens", range: 0...1, def: 0.5)
            SliderRow(s: s, label: "Unvoiced noise (dB)", path: "vocoder.noiseDb", range: -60...0, def: -18)
        }
        Section("Bank & formants") {
            ChoiceRow(s: s, label: "Bands", path: "vocoder.bands", choices: [("8", 8), ("12", 12), ("16", 16), ("24", 24)], def: 8)
            PickerRow(s: s, label: "Band spacing", path: "vocoder.spacing", options: ["log", "lin"], def: "log")
            ToggleRow(s: s, label: "4 Pole Bands", path: "vocoder.poles4")
            SliderRow(s: s, label: "Formant Shift (gender, st)", path: "vocoder.formantSt", range: -12...12, def: 0)
            SliderRow(s: s, label: "Envelope Release (ms)", path: "vocoder.releaseMs", range: 2...500, def: 18)
            SliderRow(s: s, label: "Band Resonance (Q)", path: "vocoder.q", range: 0.5...20, def: 4)
        }
        Section("Carrier, ensemble & pitch corrector") {
            SliderRow(s: s, label: "Carrier drive (%)", path: "vocoder.drive", range: 0...100, def: 0)
            SliderRow(s: s, label: "Chorus depth (%)", path: "vocoder.chorusDepth", range: 0...100, def: 0)
            SliderRow(s: s, label: "Chorus rate (Hz)", path: "vocoder.chorusRate", range: 0.1...5, def: 0.8)
            ToggleRow(s: s, label: "Internal carrier drone (C2) when no key held", path: "vocoder.drone")
            PickerRow(s: s, label: "Pitch Scale", path: "vocoder.pitchScale", options: ["chromatic", "major", "minor", "harmonicMinor", "dorian", "mixolydian", "majorPent", "minorPent", "blues", "japanese", "spanish", "egyptian", "indonesian", "filipino", "jazz", "fSharpMajor", "kumoi", "inSen", "chineseGong", "chineseShang", "majorBlues", "altered"], def: "chromatic")
            ChoiceRow(s: s, label: "Root", path: "vocoder.pitchRoot", choices: [("C", 0), ("C#", 1), ("D", 2), ("D#", 3), ("E", 4), ("F", 5), ("F#", 6), ("G", 7), ("G#", 8), ("A", 9), ("A#", 10), ("B", 11)], def: 0)
            SliderRow(s: s, label: "Correction Speed (ms)", path: "vocoder.pitchSpeed", range: 0...400, def: 20)
            ToggleRow(s: s, label: "Snap to held keys", path: "vocoder.pitchHeld")
        }
        Section("Carrier slots (Sampler slots that feed the vocoder)") {
            LazyVGrid(columns: Array(repeating: GridItem(.flexible()), count: 4)) {
                ForEach(0..<16, id: \.self) { i in
                    Toggle("\(i + 1)", isOn: Binding(get: { (s.value("vocoder.slots.\(i)") as? NSNumber)?.boolValue ?? true }, set: { s.set("vocoder.slots.\(i)", $0) })).font(.caption2).toggleStyle(.button)
                }
            }
        }
    }
}

/// The four XY pads. Each pad is a modulation source (X and Y, 0...1 mapped to -1...+1) in the matrix: xy1x, xy1y ... xy4x, xy4y.
struct XYPadsPage: View {
    @ObservedObject var s: PresetStore
    var body: some View {
        Section("XY pads (matrix sources XY Pad 1 - 4, X and Y)") {
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 150), spacing: 12)], spacing: 12) {
                ForEach(0..<4, id: \.self) { i in XYPadCell(s: s, index: i) }
            }
        }
    }
}

/// XY pad destinations (port of the web app's applyXYAxis): the pad also writes its X / Y straight into the preset. Layer-relative targets use the pad's own
/// layer, which can be any oscillator layer (WT, FM, SMP, PRO, SID, LUA).
enum XYDest {
    struct Item: Identifiable { let id: String; let label: String; let group: String }
    static let layers = ["WT", "FM", "SMP", "PRO", "SID", "LUA"]
    static let names = ["WT": "Wavetable", "FM": "FM", "SMP": "Sampler", "PRO": "Procedural", "SID": "SID", "LUA": "LUA"]
    static let fxTypes: [(String, String, [(String, String, Double, Double, Bool)])] = [
        ("phaser", "Phaser", [("rate", "Rate", 0.05, 5, true), ("depth", "Depth", 0, 1, false), ("feedback", "Feedback", 0, 0.9, false), ("mix", "Mix", 0, 1, false)]),
        ("flanger", "Flanger", [("rate", "Rate", 0.05, 5, true), ("depth", "Depth", 0, 1, false), ("feedback", "Feedback", 0, 0.9, false), ("mix", "Mix", 0, 1, false)]),
        ("resonator", "Resonator", [("freq", "Frequency", 40, 2000, true), ("resonance", "Resonance", 1, 40, false), ("mix", "Mix", 0, 1, false)]),
        ("ir", "Convolution Reverb", [("mix", "Wet / dry", 0, 1, false), ("predelay", "Pre-delay", 0, 500, false), ("damping", "Damping", 0, 1, false), ("tone", "Tone", -1, 1, false), ("width", "Width", 0, 2, false)]),
        ("echo", "Echo", [("time", "Time", 0.02, 4, true), ("feedback", "Feedback", 0, 0.95, false), ("density", "Density", 0, 1, false), ("decay", "Decay", 0, 1, false), ("mix", "Mix", 0, 1, false)]),
        ("spread", "Stereo Spread", [("spread", "Spread", 0, 2, false), ("mix", "Mix", 0, 1, false)]),
        ("interstellar", "Interstellar", [("tonal", "Tonal focus", 0, 1, false), ("bloom", "Bloom", 0, 1, false), ("density", "Density", 0, 1, false), ("space", "Space", 0, 1, false), ("shimmer", "Shimmer", 0, 1, false), ("mix", "Mix", 0, 1, false)]),
        ("reverb", "Reverb", [("mix", "Mix", 0, 1, false), ("decay", "Decay", 0, 0.97, false)]), ("delay", "Delay", [("mix", "Mix", 0, 1, false), ("feedback", "Feedback", 0, 0.9, false)]),
        ("saturation", "Saturation", [("mix", "Mix", 0, 1, false)]),
    ]
    static let all: [Item] = {
        var o: [Item] = [Item(id: "cutoff", label: "Filter cutoff (pad layer)", group: "Layer"), Item(id: "reso", label: "Filter resonance (pad layer)", group: "Layer"),
                         Item(id: "pitch", label: "Pitch octave (pad layer, osc 1)", group: "Layer"), Item(id: "level", label: "Level (pad layer)", group: "Layer"), Item(id: "pan", label: "Pan (pad layer)", group: "Layer")]
        for b in 1...4 { o.append(Item(id: "eq\(b)f", label: "EQ band \(b) frequency (pad layer)", group: "EQ")); o.append(Item(id: "eq\(b)g", label: "EQ band \(b) gain (pad layer)", group: "EQ")) }
        o += [Item(id: "wtposition", label: "Wavetable position (all)", group: "Oscillators"), Item(id: "smpposition", label: "Sample position (all)", group: "Oscillators"), Item(id: "fmratio", label: "FM ratio (all operators)", group: "Oscillators")]
        o += [Item(id: "macro1", label: "Macro 1", group: "Global"), Item(id: "macro2", label: "Macro 2", group: "Global"), Item(id: "macro3", label: "Macro 3", group: "Global"), Item(id: "macro4", label: "Macro 4", group: "Global"),
              Item(id: "wtfmmix", label: "WT-FM mixer", group: "Global"), Item(id: "mastertone", label: "Master tone", group: "Global"), Item(id: "arprate", label: "Arp rate", group: "Global")]
        for k in ["mix", "formantSt", "q", "releaseMs", "hpf", "micGainDb", "noiseDb", "sibSens", "pitchSpeed", "chorusRate"] { o.append(Item(id: "voc:\(k)", label: "Vocoder \(k)", group: "Vocoder")) }
        for (t, n, ps) in fxTypes { for p in ps { o.append(Item(id: "fx:\(t):\(p.0)", label: "\(n) \(p.1)", group: n)) } }
        return o
    }()
    static let vocRanges: [String: (Double, Double, Bool)] = ["mix": (0, 1, false), "formantSt": (-12, 12, false), "q": (0.5, 20, true), "releaseMs": (2, 500, true), "hpf": (80, 150, false), "micGainDb": (-24, 36, false), "noiseDb": (-60, 0, false), "sibSens": (0, 1, false), "pitchSpeed": (0, 400, false), "chorusRate": (0.1, 5, true)]
    static func scale(_ v: Double, _ lo: Double, _ hi: Double, _ exp: Bool) -> Double { exp && lo > 0 ? lo * pow(hi / lo, v) : lo + v * (hi - lo) }

    static func apply(_ s: PresetStore, dest: String, value v: Double, layer: String) {
        func allOscs(_ L: String, _ key: String, _ val: Double) { for i in 0..<s.array("layers.\(L).oscs").count { s.set("layers.\(L).oscs.\(i).\(key)", val) } }
        switch dest {
        case "cutoff": s.set("layers.\(layer).filter.cutoff", 40 + v * 13960)
        case "reso": s.set("layers.\(layer).filter.reso", v * 130)
        case "pitch": if s.array("layers.\(layer).oscs").count > 0 { s.set("layers.\(layer).oscs.0.oct", -2 + v * 4) }
        case "level": allOscs(layer, "level", v)
        case "pan": allOscs(layer, "pan", v * 2 - 1)
        case "wtposition": allOscs("WT", "position", v)
        case "smpposition": allOscs("SMP", "start", v * 0.9)
        case "fmratio": allOscs("FM", "ratio", 0.5 + v * 11.5)
        case "wtfmmix": s.set("wtFmMix", v)
        case "mastertone": s.set("masterTone", v)
        case "arprate": s.set("arp.rateMode", "hz"); s.set("arp.hz", 0.5 + v * 19.5)
        default:
            if dest.hasPrefix("macro"), let n = Int(dest.dropFirst(5)) { s.set("macros.\(n - 1)", v) }
            else if dest.hasPrefix("eq"), dest.count == 4, let b = Int(String(dest.dropFirst(2).prefix(1))) {
                if dest.hasSuffix("f") { s.set("layers.\(layer).eq.band\(b).freq", scale(v, 40, 16000, true)) } else { s.set("layers.\(layer).eq.band\(b).gain", scale(v, -15, 15, false)) }
            } else if dest.hasPrefix("voc:"), let r = vocRanges[String(dest.dropFirst(4))] { s.set("vocoder.\(dest.dropFirst(4))", scale(v, r.0, r.1, r.2)) }
            else if dest.hasPrefix("fx:") {
                let parts = dest.split(separator: ":").map(String.init); guard parts.count == 3, let def = fxTypes.first(where: { $0.0 == parts[1] })?.2.first(where: { $0.0 == parts[2] }) else { return }
                for i in 0..<s.array("fxStages").count where s.string("fxStages.\(i).type") == parts[1] { s.set("fxStages.\(i).params.\(parts[2])", scale(v, def.2, def.3, def.4)) }
            }
        }
    }
}

struct XYPadCell: View {
    @ObservedObject var s: PresetStore
    let index: Int
    @State private var x = 0.5
    @State private var y = 0.5
    @State private var snapBack = true
    @State private var layer = "WT"
    @State private var xDest = "cutoff"
    @State private var yDest = "reso"
    private func push() {
        XYDest.apply(s, dest: xDest, value: x, layer: layer); XYDest.apply(s, dest: yDest, value: y, layer: layer)
        guard let e = s.audioUnit.enginePointer else { return }
        vv_engine_set_xy(e, Int32(index * 2), Float(x)); vv_engine_set_xy(e, Int32(index * 2 + 1), Float(y))
    }
    var body: some View {
        VStack(spacing: 6) {
            Text("XY Pad \(index + 1)").font(.caption.bold())
            Picker("Layer", selection: $layer) { ForEach(XYDest.layers, id: \.self) { Text(XYDest.names[$0] ?? $0).tag($0) } }.pickerStyle(.menu).font(.caption2)
            Picker("X", selection: $xDest) { ForEach(XYDest.all) { Text($0.label).tag($0.id) } }.pickerStyle(.menu).font(.caption2)
            Picker("Y", selection: $yDest) { ForEach(XYDest.all) { Text($0.label).tag($0.id) } }.pickerStyle(.menu).font(.caption2)
            GeometryReader { g in
                ZStack {
                    RoundedRectangle(cornerRadius: 8).fill(Color.black.opacity(0.55))
                    RoundedRectangle(cornerRadius: 8).stroke(vvCyan.opacity(0.6), lineWidth: 1)
                    Circle().fill(vvCyan).frame(width: 16, height: 16).position(x: x * g.size.width, y: (1 - y) * g.size.height)
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0).onChanged { v in
                    x = min(max(v.location.x / max(g.size.width, 1), 0), 1); y = 1 - min(max(v.location.y / max(g.size.height, 1), 0), 1); push()
                }.onEnded { _ in if snapBack { x = 0.5; y = 0.5; push() } })
            }
            .aspectRatio(1, contentMode: .fit)
            Toggle("Snap back on release", isOn: $snapBack).font(.caption2)
            Text(String(format: "X %.2f   Y %.2f", x, y)).font(.caption2.monospacedDigit()).foregroundStyle(.secondary)
        }
    }
}
