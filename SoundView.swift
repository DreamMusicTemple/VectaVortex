// SOUND tab: Wavetable / FM / Sampler / Procedural layers - oscillators, filters, envelopes, EQ.
import SwiftUI

struct SoundView: View {
    @ObservedObject var s: PresetStore
    private var layer: Int { get { s.viewLayer } nonmutating set { s.viewLayer = newValue } }
    private let keys = ["WT", "FM", "SMP", "PRO", "SID", "LUA"]
    @State private var showXfx = false
    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                LayerTabBar(selection: Binding(get: { s.viewLayer }, set: { s.viewLayer = $0 })).padding([.horizontal, .top])
                Rectangle().fill(LayerID.color(s.viewLayer).opacity(0.6)).frame(height: 2).padding(.horizontal).padding(.top, 4)
                Form {
                    switch layer {
                    case 0: WavetableEditor(s: s)
                    case 1: FMEditor(s: s)
                    case 2: SamplerEditor(s: s)
                    case 3: ProceduralEditor(s: s)
                    case 4: SidEditor(s: s)
                    default: LuaEditorView(s: s)
                    }
                    LayerTransportSection(s: s, layer: layer)
                    Section("Layer") {
                        EnvEditor(s: s, title: "Amp envelope (layer default)", prefix: "layers.\(keys[layer]).env")
                        FilterEditor(s: s, title: "Layer filter", prefix: "layers.\(keys[layer]).filter", envPrefix: "layers.\(keys[layer]).filterEnv")
                        EQEditor(s: s, prefix: "layers.\(keys[layer]).eq")
                    }
                }
                .layerIdentity(s.viewLayer)
            }
            .navigationTitle("Sound")
            .toolbar { ToolbarItem(placement: .primaryAction) { Button("XFX") { showXfx = true } } }   // web: XFX button beside the Samples mixer
            .sheet(isPresented: $showXfx) { XfxView(s: s) }
        }
    }
}

struct EQEditor: View {
    @ObservedObject var s: PresetStore
    let prefix: String
    var body: some View {
        DisclosureGroup("EQ (4 band)", isExpanded: .constant(true)) { EQGraphView(s: s, prefix: prefix) }   // draggable bands, as in Vektur Vocal
    }
}

/// Names of bundled wavetables (file names in the app bundle's Wavetables folder).
enum Assets {
    static let wavetableNames: [String] = {
        guard let dir = Bundle(for: VectaAudioUnit.self).url(forResource: "Wavetables", withExtension: nil),
              let files = try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil) else { return ["analogMorph"] }
        return files.filter { $0.pathExtension == "vvwt" }.map { $0.deletingPathExtension().lastPathComponent }.sorted()
    }()
    static let sampleNames: [String] = {
        guard let u = Bundle(for: VectaAudioUnit.self).url(forResource: "builtin_samples", withExtension: "json"), let d = try? Data(contentsOf: u),
              let arr = try? JSONSerialization.jsonObject(with: d) as? [[String: String]] else { return [] }
        return arr.compactMap { $0["name"] }.sorted()
    }()
}

struct WavetableEditor: View {
    @ObservedObject var s: PresetStore
    var body: some View {
        Section("Wavetable engine") {
            ToggleRow(s: s, label: "Oscillator sync (OSC1 ← OSC2)", path: "layers.WT.syncEnabled")
            ToggleRow(s: s, label: "Ring mod (OSC2 = modulator; off while sync is on)", path: "layers.WT.ringModEnabled")
            PickerRow(s: s, label: "Ring interval", path: "layers.WT.ringMod.interval", options: ["p5", "octave", "m3", "tritone", "micro"], def: "p5")
            PickerRow(s: s, label: "Ring osc wave", path: "layers.WT.ringMod.wave", options: ["sine", "triangle", "sawtooth", "square"], def: "sine")
            SliderRow(s: s, label: "Ring detune (ct)", path: "layers.WT.ringMod.detuneCents", range: -100...100, def: 0)
            SliderRow(s: s, label: "Ring pitch env (ct)", path: "layers.WT.ringMod.pitchEnvAmt", range: 0...1200, def: 0)
            SliderRow(s: s, label: "Ring pitch env decay (s)", path: "layers.WT.ringMod.pitchEnvDecay", range: 0.01...1, def: 0.12)
            SliderRow(s: s, label: "Ring AM bleed", path: "layers.WT.ringMod.bleed", range: 0...1, def: 0.15)
            SliderRow(s: s, label: "Ring velocity depth", path: "layers.WT.ringMod.velDepth", range: 0...1, def: 0.3)
            SliderRow(s: s, label: "Ring LPG cutoff (Hz)", path: "layers.WT.ringMod.lpgCutoff", range: 200...8000, def: 4000)
            SliderRow(s: s, label: "Ring LPG keytrack", path: "layers.WT.ringMod.lpgKeytrack", range: 0...1, def: 0.5)
        }
        ForEach(0..<2, id: \.self) { i in
            let p = "layers.WT.oscs.\(i)"
            Section("Oscillator \(i + 1)") {
                if i == 0 { WtLinkBadge(s: s) }
                Group {
                ToggleRow(s: s, label: "Enabled", path: p + ".enabled", def: i == 0)
                PickerRow(s: s, label: "Wavetable", path: p + ".table", options: s.tableChoices.ids, def: "analogMorph", labels: s.tableChoices.labels)
                WavetableDisplay(s: s, oscPath: p)
                WavetableToolsSection(s: s, oscPath: p)
                SliderRow(s: s, label: "Position (scan)", path: p + ".position", range: 0...1, modDest: "WT:osc\(i + 1)Scan")
                SliderRow(s: s, label: "Level", path: p + ".level", range: 0...1, def: 0.8, modDest: "WT:osc\(i + 1)Level")
                SliderRow(s: s, label: "Pulse width", path: p + ".pwm", range: 0...1, def: 0.5, modDest: "WT:osc\(i + 1)Pwm")
                SliderRow(s: s, label: "Hz (PWM speed)", path: p + ".pwmRate", range: 0...20, def: 0, fmt: "%.2f", unit: " Hz", modDest: "WT:osc\(i + 1)PwmRate")
                SliderRow(s: s, label: "Phase", path: p + ".phase", range: 0...1, modDest: "WT:osc\(i + 1)Phase")
                SliderRow(s: s, label: "Start / end", path: p + ".startEnd", range: 0...1, def: 1, modDest: "WT:osc\(i + 1)StartEnd")
                SliderRow(s: s, label: "Segment", path: p + ".segment", range: 0.05...1, def: 1, modDest: "WT:osc\(i + 1)Segment")
                }
                Group {
                ToggleRow(s: s, label: "Ping-pong scan", path: p + ".pingPong")
                StepperRow(s: s, label: "Unison voices", path: p + ".unison", range: 1...16, def: 1)
                SliderRow(s: s, label: "Unison detune", path: p + ".detune", range: 0...100, fmt: "%.0f", unit: " ct")
                SliderRow(s: s, label: "Stereo width", path: p + ".width", range: 0...1)
                SliderRow(s: s, label: "Spread blend", path: p + ".unisonBlend", range: 0...1)
                SliderRow(s: s, label: "Spread phase", path: p + ".unisonPhase", range: 0...1)
                StepperRow(s: s, label: "Sub oscillator (octaves down)", path: p + ".sub", range: 0...2)
                PickerRow(s: s, label: "Sub shape", path: p + ".subWave", options: ["sine", "triangle", "square"], def: "sine")
                PickerRow(s: s, label: "Position auto-scan", path: p + ".positionLfoSource", options: ["off", "ampEnv", "filterEnv", "pitchEnv", "lfo1", "lfo2", "lfo3", "lfo4", "lfo5", "lfo6", "velocity", "note", "modwheel", "random", "envWT", "envFM", "envSMP", "envFollow", "macro1", "macro2", "macro3", "macro4", "mpeTimbre", "mpePressure", "wtOsc1Pos", "wtOsc2Pos", "wtOsc1Shape", "wtOsc2Shape", "xy1x", "xy1y", "xy2x", "xy2y", "xy3x", "xy3y", "xy4x", "xy4y", "lorenz", "rossler"], def: "off")
                ToggleRow(s: s, label: "Noise", path: p + ".noiseOn")
                PickerRow(s: s, label: "Noise type", path: p + ".noiseType", options: ["white", "pink", "brown", "blue", "violet"], def: "white")
                }
                OscCommonEditor(s: s, prefix: p)
                PitchEnvEditor(s: s, prefix: p, title: "Pitch envelope (OSC\(i + 1))")
                FilterEditor(s: s, title: "Oscillator filter", prefix: p + ".filter", envPrefix: p + ".filterEnv")
            }
        }
    }
}

struct FMEditor: View {
    @ObservedObject var s: PresetStore
    private let waves = ["sine", "triangle", "square"]
    var body: some View {
        Section("6-operator (DX7) mode") {   // replaces the two 4-operator engines while on
            ToggleRow(s: s, label: "6-op mode", path: "layers.FM.sixOp")
            ToggleRow(s: s, label: "6-op enabled", path: "layers.FM.sixOpEnabled")
            PickerRow(s: s, label: "Bank", path: "layers.FM.dx7Bank", options: ["ROM1A", "ROM1B", "ROM2A", "ROM2B", "ROM3A", "ROM3B", "ROM4A", "ROM4B"], def: "ROM1A")
            StepperRow(s: s, label: "Patch", path: "layers.FM.dx7PatchIdx", range: 0...31, def: 0)
            SliderRow(s: s, label: "Transpose (st)", path: "layers.FM.dx7Transpose", range: -24...24, def: 0)
            SliderRow(s: s, label: "Output trim", path: "layers.FM.dx7OutputTrim", range: 0...2, def: 1)
            SliderRow(s: s, label: "Pan", path: "layers.FM.dx7Pan", range: -1...1, def: 0)
        }
        ForEach(0..<2, id: \.self) { e in
            Section("FM engine \(e == 0 ? "A" : "B")") {
                ToggleRow(s: s, label: "Enabled", path: "layers.FM.engineEnabled.\(e)")
                StepperRow(s: s, label: "Algorithm", path: "layers.FM.fmAlgorithms.\(e)", range: 0...5, def: 2)
                StepperRow(s: s, label: "Octave", path: "layers.FM.engineOctave.\(e)", range: -4...4)
                StepperRow(s: s, label: "Semitone", path: "layers.FM.engineSemitone.\(e)", range: -12...12)
                SliderRow(s: s, label: "Fine", path: "layers.FM.engineFine.\(e)", range: -100...100, fmt: "%.0f", unit: " ct")
                SliderRow(s: s, label: "Glide", path: "layers.FM.engineGlide.\(e)", range: 0...3, unit: " s")
                StepperRow(s: s, label: "Sub oscillator (octaves down)", path: "layers.FM.fmSub.\(e)", range: 0...2)
                ForEach(0..<4, id: \.self) { k in
                    let p = "layers.FM.oscs.\(e * 4 + k)"
                    DisclosureGroup("Operator \(k + 1)") {
                        SliderRow(s: s, label: "Ratio", path: p + ".ratio", range: 0.05...16, def: 1)
                        SliderRow(s: s, label: "Level", path: p + ".level", range: 0...1, def: k == 0 ? 0.9 : 0)
                        PickerRow(s: s, label: "Wave", path: p + ".wave", options: waves, def: "sine")
                        if k == 0 {
                            SliderRow(s: s, label: "Feedback", path: p + ".feedback", range: 0...1)
                            SliderRow(s: s, label: "Pan", path: p + ".pan", range: -1...1)
                            ToggleRow(s: s, label: "Noise", path: p + ".noiseOn")
                            PickerRow(s: s, label: "Noise type", path: p + ".noiseType", options: ["white", "pink", "brown", "blue", "violet"], def: "white")
                            OscCommonEditor(s: s, prefix: p, drift: false)
                            PitchEnvEditor(s: s, prefix: p, title: "Pitch envelope (Engine \(e == 0 ? "A" : "B"))")
                            FilterEditor(s: s, title: "Engine filter", prefix: p + ".filter", envPrefix: p + ".filterEnv")
                        }
                    }
                }
            }
        }
    }
}

struct SamplerEditor: View {
    @ObservedObject var s: PresetStore
    @State private var slot = 0
    @State private var showTools = false, showRecord = false, showImageAudio = false
    var body: some View {
        Section("Sampler") {
            SlotAdvanceRow(s: s)
            ToggleRow(s: s, label: "Global ADSR override", path: "layers.SMP.globalAdsrActive")
            EnvEditor(s: s, title: "Global ADSR", prefix: "layers.SMP.globalAdsr")
            GlobalToggle(s: s, label: "Sample-mix focus", name: "sampleMixOn")
            GlobalSlider(s: s, label: "Focus position", name: "sampleMix", range: 0...15, def: 0, fmt: "%.2f")
            Picker("Slot", selection: $slot) { ForEach(0..<16, id: \.self) { Text("Slot \($0 + 1)").tag($0) } }.pickerStyle(.menu)
            HStack {
                Button("Random populate") { randomPopulate() }
                Button("Random sampler FX") { randomSamplerFx() }
            }.buttonStyle(.bordered)
        }
        let p = "layers.SMP.oscs.\(slot)"
        Section("Waveform — slot \(slot + 1)") { SampleWaveformEditor(s: s, slot: slot) }
        Section("Slot \(slot + 1)") {
            Group {
            PickerRow(s: s, label: "Sample", path: p + ".name", options: [""] + Assets.sampleNames + s.array("sampleLibrary").compactMap { ($0 as? [String: Any])?["name"] as? String }, def: "")
            Button("SAMPLE TOOLS… (import / stretch / warp / spectral)") { showTools = true }
            Button("RECORD…") { showRecord = true }
            Button("IMAGE → AUDIO…") { showImageAudio = true }
            PickerRow(s: s, label: "Microphone", path: p + ".mic", options: sampMicIds, def: "none")
            PickerRow(s: s, label: "Speaker", path: p + ".spk", options: sampSpkIds, def: "none")
            ToggleRow(s: s, label: "Bleed", path: p + ".bleedOn")
            SliderRow(s: s, label: "Level", path: p + ".level", range: 0...1, def: 0.8)
            SliderRow(s: s, label: "Bleed amount", path: p + ".bleed", range: 0...1, def: 0.5)
            ToggleRow(s: s, label: "Arp On/Off", path: p + ".arpOn", def: slot == 0)
            SliderRow(s: s, label: "Start", path: p + ".start", range: 0...1)
            SliderRow(s: s, label: "End", path: p + ".end", range: 0...1, def: 1)
            ToggleRow(s: s, label: "Loop", path: p + ".loop", def: true)
            ToggleRow(s: s, label: "Ping-pong", path: p + ".pingPong")
            ToggleRow(s: s, label: "Reverse", path: p + ".reverse")
            }
            RoundRobinRow(s: s, slot: slot)
            Group {
            SliderRow(s: s, label: "Fade in", path: p + ".fadeIn", range: 0...2, unit: " s")
            SliderRow(s: s, label: "Fade out", path: p + ".fadeOut", range: 0...2, unit: " s")
            ToggleRow(s: s, label: "Noise", path: p + ".noiseOn")
            PickerRow(s: s, label: "Noise type", path: p + ".noiseType", options: ["white", "pink", "brown", "blue", "violet"], def: "white")
            }
            OscCommonEditor(s: s, prefix: p, drift: false)
            EnvEditor(s: s, title: "Slot envelope", prefix: p + ".env")
            FilterEditor(s: s, title: "Slot filter", prefix: p + ".filter", envPrefix: p + ".filterEnv")
        }
        .sheet(isPresented: $showTools) { SampleToolsSheet(s: s, slot: slot) }
        .sheet(isPresented: $showRecord) { RecordSheet(s: s, slot: slot) }
        .sheet(isPresented: $showImageAudio) { ImageAudioSheet(s: s, slot: slot) }
    }
}

struct SidEditor: View {
    @ObservedObject var s: PresetStore
    private let waves = ["triangle", "sawtooth", "pulse", "noise", "tri_saw", "tri_pulse", "saw_pulse"]
    var body: some View {
        ForEach(0..<3, id: \.self) { i in
            let p = "layers.SID.oscs.\(i)"
            Section("SID voice \(i + 1)") {
                ToggleRow(s: s, label: "Enabled", path: p + ".enabled")
                ToggleRow(s: s, label: "Follow arp (wavetable arp below)", path: p + ".arpOn")
                PickerRow(s: s, label: "Waveform", path: p + ".wave", options: waves, def: "triangle")
                ToggleRow(s: s, label: "Ring mod (from previous voice)", path: p + ".ringModOn")
                ToggleRow(s: s, label: "Hard sync (to previous voice)", path: p + ".syncOn")
                SliderRow(s: s, label: "Level", path: p + ".level", range: 0...1, def: 0.8)
                SliderRow(s: s, label: "Pulse width", path: p + ".pulseWidth", range: 0.02...0.98, def: 0.5)
                SliderRow(s: s, label: "Vibrato", path: p + ".vibratoDepth", range: 0...1, def: 0)
                OscCommonEditor(s: s, prefix: p)
            }
        }
        Section("SID layer") { SliderRow(s: s, label: "Balance", path: "layers.SID.pan", range: -1...1, def: 0) }
        SidArpEditor(s: s)
    }
}

/// SID "wavetable" arp (web layer.wtArp): a chiptune step table that rewrites pitch / wave / ring / sync / gate of every voice with "Follow arp" on.
struct SidArpEditor: View {
    @ObservedObject var s: PresetStore
    private let waves = ["triangle", "sawtooth", "pulse", "noise", "tri_saw", "tri_pulse", "saw_pulse"]
    private let base = "layers.SID.wtArp"
    private func defaultStep(_ v: Double) -> [String: Any] { ["wave": "pulse", "mode": "relative", "value": v, "gate": true, "ringMod": false, "sync": false, "lengthTicks": 1] }
    var body: some View {
        Section("Wavetable arp") {
            ToggleRow(s: s, label: "On", path: base + ".on")
            ToggleRow(s: s, label: "Hard restart (gate low 1 ms before each step)", path: base + ".hardRestart")
            SliderRow(s: s, label: "Speed (ticks/s)", path: base + ".speedHz", range: 1...200, def: 50)
            ForEach(0..<s.count(base + ".steps"), id: \.self) { i in
                let p = base + ".steps.\(i)"
                DisclosureGroup("Step \(i + 1)") {
                    PickerRow(s: s, label: "Wave", path: p + ".wave", options: waves, def: "triangle")
                    PickerRow(s: s, label: "Mode", path: p + ".mode", options: ["relative", "absolute"], def: "relative")
                    SliderRow(s: s, label: "Value (semitones / Hz)", path: p + ".value", range: -48...2000, def: 0)
                    SliderRow(s: s, label: "Length (ticks)", path: p + ".lengthTicks", range: 1...16, def: 1)
                    ToggleRow(s: s, label: "Gate", path: p + ".gate", def: true)
                    ToggleRow(s: s, label: "Ring mod", path: p + ".ringMod")
                    ToggleRow(s: s, label: "Sync", path: p + ".sync")
                    Button("Remove step", role: .destructive) { s.remove(base + ".steps", at: i) }
                }
            }
            Button("+ Step") {
                if s.count(base + ".steps") == 0 { s.set(base + ".steps", [defaultStep(0), defaultStep(3), defaultStep(7)]) }
                else { s.append(base + ".steps", defaultStep(0)) }
            }
        }
    }
}

/// Web footer HOLD / SOLO / MUTE for the layer being edited (tap Solo, long-press Mute is the web's double-tap; here two separate switches).
struct LayerTransportSection: View {
    @ObservedObject var s: PresetStore
    let layer: Int
    private let keys = ["WT", "FM", "SMP", "PRO", "SID", "LUA"]
    private let counts = [2, 8, 16, 2, 3, 4]
    var body: some View {
        Section("\(keys[layer]) performance") {
            Toggle("Hold", isOn: Binding(get: { (0..<counts[layer]).allSatisfy { s.double("layers.\(keys[layer]).oscs.\($0).hold", 0) != 0 } },
                                          set: { v in for o in 0..<counts[layer] { s.set("layers.\(keys[layer]).oscs.\(o).hold", v ? 1 : 0) }; if !v { s.allNotesOff() } }))
            Toggle("Solo", isOn: Binding(get: { s.soloMask & (1 << layer) != 0 }, set: { s.setSolo(layer, $0) }))
            Toggle("Mute", isOn: Binding(get: { s.muteMask & (1 << layer) != 0 }, set: { s.setMute(layer, $0) }))
        }
    }
}

struct ProceduralEditor: View {
    @ObservedObject var s: PresetStore
    var body: some View {
        ForEach(0..<2, id: \.self) { i in
            let p = "layers.PRO.oscs.\(i)"
            Section("Generator \(i == 0 ? "A" : "B")") {
                ToggleRow(s: s, label: "Enabled", path: p + ".enabled")
                PickerRow(s: s, label: "Model", path: p + ".model", options: ["pluck", "modal", "breath", "chaos", "bow"], def: "pluck")
                SliderRow(s: s, label: "Level", path: p + ".level", range: 0...1, def: 0.8)
                SliderRow(s: s, label: "Tone", path: p + ".bright", range: 0...1, def: 0.5)
                SliderRow(s: s, label: "Damp", path: p + ".damp", range: 0...1, def: 0.4)
                SliderRow(s: s, label: "Body", path: p + ".body", range: 0...1, def: 0.3)
                SliderRow(s: s, label: "Chaos", path: p + ".chaos", range: 0...1, def: 0.4)
                SliderRow(s: s, label: "Stiffness (pluck / bow)", path: p + ".stiffness", range: 0...1, def: 0)
                SliderRow(s: s, label: "Bore (breath: cylindrical to conical)", path: p + ".bore", range: 0...1, def: 0)
                StepperRow(s: s, label: "Strike material (modal): 0 felt, 1 wood, 2 metal, 3 finger", path: p + ".excite", range: 0...3, def: 0)
                SliderRow(s: s, label: "Tone keytrack", path: p + ".brightKeyTrack", range: -100...100, def: 0, fmt: "%.0f")
                SliderRow(s: s, label: "Damp keytrack", path: p + ".dampKeyTrack", range: -100...100, def: 0, fmt: "%.0f")
                OscCommonEditor(s: s, prefix: p)
                PitchEnvEditor(s: s, prefix: p, title: "Pitch envelope (Generator \(i == 0 ? "A" : "B"))")
            }
        }
        Section("Coupling (A <-> B)") { SliderRow(s: s, label: "Coupling", path: "layers.PRO.proCoupling", range: 0...1, def: 0) }
    }
}

/// Engine-level settings that are not part of the preset JSON (master tone, WT/FM mix …). Kept in UserDefaults so they survive relaunch.
struct GlobalSlider: View {
    @ObservedObject var s: PresetStore
    let label: String; let name: String; let range: ClosedRange<Double>; var def: Double; var fmt = "%.2f"
    @State private var v: Double = 0
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack { Text(label).font(.caption).onTapGesture(count: 2) { apply(def) }; Spacer(); Text(String(format: fmt, v)).font(.caption.monospacedDigit()).foregroundStyle(.secondary) }
            Slider(value: Binding(get: { v }, set: { apply($0) }), in: range)
        }.onAppear { v = UserDefaults.standard.object(forKey: "vv.global." + name) as? Double ?? def; if let e = s.audioUnit.enginePointer { vv_engine_set_global(e, name, v) } }
    }
    private func apply(_ x: Double) { v = x; UserDefaults.standard.set(x, forKey: "vv.global." + name); if let e = s.audioUnit.enginePointer { vv_engine_set_global(e, name, x) } }
}
struct GlobalToggle: View {
    @ObservedObject var s: PresetStore
    let label: String; let name: String
    @State private var on = false
    var body: some View {
        Toggle(label, isOn: Binding(get: { on }, set: { on = $0; UserDefaults.standard.set($0, forKey: "vv.global." + name); if let e = s.audioUnit.enginePointer { vv_engine_set_global(e, name, $0 ? 1 : 0) } })).font(.caption)
            .onAppear { on = UserDefaults.standard.bool(forKey: "vv.global." + name); if let e = s.audioUnit.enginePointer { vv_engine_set_global(e, name, on ? 1 : 0) } }
    }
}

// Ids of the 16 real microphone / speaker colourings (+ "none"); identical to MIC_TYPES / SPK_TYPES in the web app and kMic / kSpk in the engine.
let sampMicIds = ["none", "sm57", "u87", "r121", "u47", "c414", "sm7", "coles", "phone", "walkie", "piezo", "tincan", "lav", "boundary", "hydro", "babymon", "carbon"]
let sampSpkIds = ["none", "studio", "cab412", "hifi", "tuberadio", "nearfield", "cab112", "sub18", "jukebox", "oldtv", "pocket", "laptop", "mega", "intercom", "handheld", "btmicro", "bucket"]
extension SamplerEditor {
    /// Every slot gets a random sample from the library.
    func randomPopulate() {
        let names = Assets.sampleNames; guard !names.isEmpty else { return }
        for i in 0..<16 { let p = "layers.SMP.oscs.\(i)"; s.set(p + ".name", names.randomElement() ?? ""); if s.double(p + ".level", 0) <= 0 { s.set(p + ".level", 0.8) } }
    }
    /// Every slot gets its own microphone and its own speaker (two independent shuffles of the 16 real types).
    func randomSamplerFx() {
        let mics = Array(sampMicIds.dropFirst()).shuffled(), spks = Array(sampSpkIds.dropFirst()).shuffled()
        for i in 0..<16 { let p = "layers.SMP.oscs.\(i)"
            s.set(p + ".mic", mics[i % mics.count]); s.set(p + ".spk", spks[i % spks.count])
            if s.double(p + ".micMix", 0) <= 0 { s.set(p + ".micMix", 0.5) }; if s.double(p + ".spkMix", 0) <= 0 { s.set(p + ".spkMix", 0.5) } }
    }
}


/// Sampler Round Robin for one slot (web: osc.rr): rotate through this slot's sample and the alternate takes ticked below on every new note.
/// Takes keep their own start / end markers and root; the playing slot supplies loop, reverse, level, envelope, filter, pan and pitch. The engine does the rotation (Synth::rrPick).
struct RoundRobinRow: View {
    @ObservedObject var s: PresetStore
    let slot: Int
    @State private var lastTake = -1
    private let ticker = Timer.publish(every: 0.1, on: .main, in: .common).autoconnect()
    private var p: String { "layers.SMP.oscs.\(slot).rr" }
    private var takes: [Int] { s.array(p + ".takes").compactMap { ($0 as? NSNumber)?.intValue } }
    private func hasSample(_ j: Int) -> Bool { !s.string("layers.SMP.oscs.\(j).name").isEmpty }
    private func toggle(_ j: Int) { var t = takes; if let i = t.firstIndex(of: j) { t.remove(at: i) } else { t.append(j) }; s.set(p + ".takes", t) }
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            ToggleRow(s: s, label: "Round Robin", path: p + ".on")
            if s.bool(p + ".on") {
                PickerRow(s: s, label: "Order", path: p + ".order", options: ["seq", "random", "norep", "pingpong"], def: "seq", labels: ["seq": "Sequential", "random": "Random", "norep": "Random, no repeat", "pingpong": "Ping-pong"])
                PickerRow(s: s, label: "Restart", path: p + ".scope", options: ["free", "key", "transport", "idle"], def: "free", labels: ["free": "Continuous", "key": "Per key", "transport": "Reset on transport start", "idle": "Reset after 1.5 s idle"])
                SliderRow(s: s, label: "Variation", path: p + ".variation", range: 0...1, def: 0.3)
                Text("Takes (this slot's sample always plays; tick other slots to rotate through their samples)").font(.caption2).foregroundStyle(.secondary)
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 30), spacing: 3)], spacing: 3) {
                    ForEach(0..<16, id: \.self) { j in
                        let isSelf = j == slot, on = isSelf ? hasSample(j) : takes.contains(j), has = hasSample(j)
                        Button { if !isSelf && (has || on) { toggle(j) } } label: {
                            Text("\(j + 1)").font(.system(size: 10, design: .monospaced)).frame(width: 26, height: 24)
                                .background(RoundedRectangle(cornerRadius: 3).fill(lastTake == j ? LayerID.color(2) : (on ? LayerID.color(2).opacity(0.28) : Color.black.opacity(0.3))))
                                .overlay(RoundedRectangle(cornerRadius: 3).strokeBorder(on ? LayerID.color(2) : Color.gray.opacity(0.4), style: StrokeStyle(lineWidth: 1, dash: isSelf ? [3, 2] : [])))
                                .foregroundStyle(lastTake == j ? Color.black : (has ? Color.white : Color.gray.opacity(0.5)))
                        }.buttonStyle(.plain).disabled(isSelf || (!has && !on))
                        .accessibilityLabel("Take slot \(j + 1)" + (on ? ", in rotation" : "") + (has ? "" : ", no sample"))
                    }
                }
                HStack {
                    Button("Add all loaded") { s.set(p + ".takes", (0..<16).filter { $0 != slot && hasSample($0) }) }
                    Button("Clear takes") { s.set(p + ".takes", [Int]()) }
                    Button("Reset rotation") { if let e = s.audioUnit.enginePointer { vv_engine_smp_rr_reset(e) } }
                }.buttonStyle(.bordered).font(.caption2)
            }
        }
        .onReceive(ticker) { _ in
            guard s.bool(p + ".on"), let e = s.audioUnit.enginePointer else { if lastTake != -1 { lastTake = -1 }; return }
            let t = Int(vv_engine_smp_rr_last(e, Int32(slot))); if t != lastTake { lastTake = t }
        }
    }
}


/// Link badge right of OSC1: hard-sync icon while syncEnabled, ring-mod icon while ringModEnabled (sync wins, like the web app).
struct WtLinkBadge: View {
    @ObservedObject var s: PresetStore
    var body: some View {
        let sync = s.bool("layers.WT.syncEnabled"), ring = s.bool("layers.WT.ringModEnabled")
        if sync || ring {
            HStack(spacing: 8) {
                NavIconView(icon: sync ? .sync : .ringmod).frame(width: 26, height: 26)
                    .foregroundStyle(sync ? Color(red: 0.29, green: 0.66, blue: 1) : Color(red: 0.88, green: 0.44, blue: 1))
                Text(sync ? "Hard sync: OSC1 (master) resets OSC2 (slave)" : "Ring mod: OSC1 × OSC2").font(.caption2).foregroundStyle(.secondary)
            }
        }
    }
}
