// SEQ tab: arpeggiator, transport, swing, and the 4-layer step sequencer with pattern slots, chains, ratchets, probability and p-locks.
import SwiftUI

struct SequencerView: View {
    @ObservedObject var s: PresetStore
    @State private var layer = 0
    private let keys = ["WT", "FM", "SMP", "PRO", "XFX"]   // XFX = the XFX MODE pseudo layer (patterns in extra.xfxSeq)
    var body: some View {
        NavigationStack {
            Form {
                Section("Transport") {
                    Toggle("Run sequencer", isOn: Binding(get: { s.transportRunning }, set: { s.setTransport($0) })).font(.caption)
                    SliderRow(s: s, label: "Tempo", path: "bpm", range: 40...240, def: 120, fmt: "%.0f", unit: " BPM")
                    ToggleRow(s: s, label: "Swing", path: "globalSwingOn")
                    SliderRow(s: s, label: "Swing amount", path: "globalSwingAmount", range: 50...75, def: 50, fmt: "%.0f", unit: " %")
                }
                Section("Clip launcher") { ClipLauncherView(s: s, layer: layer) }
                ArpSection(s: s)
                ChordSection(s: s)
                Section("Pattern") {
                    Picker("Layer", selection: $layer) { ForEach(0..<keys.count, id: \.self) { Text(keys[$0] == "XFX" ? "XFX MODE" : keys[$0]).tag($0) } }.pickerStyle(.segmented)
                        .onChange(of: layer) { _ in if keys[layer] == "XFX" { s.ensureXfxSeq() } }
                    PatternEditor(s: s, layerKey: keys[layer], layerIndex: layer)
                }
            }.navigationTitle("Sequencer")
        }
    }
}

/// ARP: the "Arp on" switch stays in place. Turning it ON by the user opens the settings popup once; the popup can be closed
/// independently of the arp state, and ARP SETTINGS reopens it while the arp is on. Popup visibility is view state only (never saved).
struct ArpSection: View {
    @ObservedObject var s: PresetStore
    @State private var showPopup = false
    var body: some View {
        Section("Arpeggiator") {
            Toggle("Arp on", isOn: Binding(get: { s.bool("arp.on") }, set: { on in
                s.set("arp.on", on); push()
                showPopup = on          // OFF → ON opens it; ON → OFF closes it. Unrelated refreshes never touch this.
            })).font(.caption)
            if s.bool("arp.on") { Button("ARP SETTINGS") { showPopup = true }.font(.caption) }
        }
        .sheet(isPresented: $showPopup) { ArpSettingsPopup(s: s) }
        .onChange(of: s.bool("arp.on")) { on in if !on { showPopup = false } }   // preset load / host change turning it off
    }
    private func push() { if let e = s.audioUnit.enginePointer { vv_engine_set_arp(e, s.bool("arp.on") ? 1 : 0, 0) } }
}

/// Every existing arp control, unchanged bindings (arp.targets / order / rate mode / division / hz / octave range).
struct ArpSettingsPopup: View {
    @ObservedObject var s: PresetStore
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        NavigationStack {
            Form {
                Section { Text(s.bool("arp.on") ? "ARP is ON" : "ARP is OFF").font(.caption.monospaced()).foregroundStyle(s.bool("arp.on") ? vvCyan : .secondary) }
                // STANDARD / ADVANCED (web: state.arp.mode). Both share the same clock, scheduling and note management.
                Picker("Mode", selection: s.binding("arp.mode", string: "standard")) { Text("STANDARD").tag("standard"); Text("ADVANCED").tag("advanced") }.pickerStyle(.segmented)
                ArpLayersRow(s: s)   // several arp layers at once (web: state.arp.targets)
                if s.string("arp.mode", "standard") != "advanced" { PickerRow(s: s, label: "Playback mode", path: "arp.order", options: ["UP", "DOWN", "UP/DOWN", "DOWN/UP", "RANDOM", "RANDOM WALK", "PENDULUM", "SPIRAL", "STRUM", "OCTAVE ORBIT", "CHORD", "AS PLAYED"], def: "UP",
                          labels: ["UP": "Up", "DOWN": "Down", "UP/DOWN": "Up/Down", "DOWN/UP": "Down/Up", "RANDOM": "Random", "RANDOM WALK": "Random Walk", "PENDULUM": "Pendulum", "SPIRAL": "Spiral", "STRUM": "Chord Strum", "OCTAVE ORBIT": "Octave Orbit", "CHORD": "Chord (all)", "AS PLAYED": "As played"]) }
                PickerRow(s: s, label: "Rate mode", path: "arp.rateMode", options: ["sync", "hz"], def: "sync")
                if s.string("arp.rateMode", "sync") == "sync" {
                    ChoiceRow(s: s, label: "Division", path: "arp.division", choices: [("1/64", 0.0625), ("1/32T", 1.0/12), ("1/32", 0.125), ("1/16T", 1.0/6), ("1/16", 0.25), ("1/8T", 1.0/3), ("1/8", 0.5), ("1/4T", 2.0/3), ("1/4", 1), ("1/2T", 4.0/3), ("1/2", 2), ("1 bar", 4), ("2 bars", 8)], def: 0.5)
                } else {
                    SliderRow(s: s, label: "Rate", path: "arp.hz", range: 0.5...20, def: 4, fmt: "%.1f", unit: " Hz")
                }
                StepperRow(s: s, label: "Octave range", path: "arp.octRange", range: 1...4, def: 1)
                SliderRow(s: s, label: "Gate", path: "arp.gate", range: 0.05...0.95, def: 0.8, fmt: "%.2f")
                SliderRow(s: s, label: "Swing", path: "arp.swing", range: 50...75, def: 50, fmt: "%.0f", unit: " %")
                SliderRow(s: s, label: "Velocity", path: "arp.velocity", range: 0.1...1, def: 0.9, fmt: "%.2f")
                StepperRow(s: s, label: "Note repeat", path: "arp.repeat", range: 1...4, def: 1)
                StepperRow(s: s, label: "Transpose (st)", path: "arp.transpose", range: -24...24, def: 0)
                ToggleRow(s: s, label: "Latch", path: "arp.latch")
                if s.string("arp.mode", "standard") == "advanced" { ArpAdvancedSection(s: s) }
            }
            .navigationTitle("ARP settings")
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Close") { dismiss() } } }
        }.presentationDetents([.medium, .large])
    }
}

/// ADVANCED mode: 35 seeded generative algorithms (web ARP_ALGOS) with only the parameters the chosen algorithm uses.
struct ArpAdvancedSection: View {
    @ObservedObject var s: PresetStore
    private var algoId: String { s.string("arp.adv.algo", "fractal") }
    private var info: ArpAlgoInfo { ArpAdvInfo.all.first { $0.id == algoId } ?? ArpAdvInfo.all[0] }
    private var shown: [(String, String, Double, Double, Double, Bool)] { let keys = info.keys + ArpAdvInfo.core; return ArpAdvInfo.params.filter { keys.contains($0.0) } }
    var body: some View {
        Section("Advanced algorithm") {
            Picker("Algorithm", selection: s.binding("arp.adv.algo", string: "fractal")) {
                ForEach(Array(ArpAdvInfo.all.enumerated()), id: \.element.id) { n, a in Text("\(n + 1). \(a.name)").tag(a.id) }
            }
            Text(info.desc).font(.caption2).foregroundStyle(.secondary)
            HStack {
                Text("Seed: \(Int(s.double("arp.adv.seed", 1234)))").font(.caption.monospaced())
                Spacer()
                Button("NEW SEED") { s.set("arp.adv.seed", Double(Int.random(in: 0...999_999))) }   // regenerates the pattern (same seed = same pattern)
            }
        }
        Section("Parameters") {
            ForEach(shown, id: \.0) { p in
                if p.5 { SliderRow(s: s, label: p.1, path: "arp.adv." + p.0, range: p.2...p.3, def: ArpAdvInfo.defaults[p.0] ?? 0, fmt: "%.2f") }
                else { StepperRow(s: s, label: p.1, path: "arp.adv." + p.0, range: Int(p.2)...Int(p.3), def: Int(ArpAdvInfo.defaults[p.0] ?? 0)) }
            }
            if info.keys.contains("scale") { PickerRow(s: s, label: "Scale", path: "arp.adv.scale", options: ArpAdvInfo.scales, def: "major") }
        }
        ArpPatternSection(s: s)
    }
}

/// Pattern management for ADVANCED mode (web Stage 3): Generate / Randomise / Mutate / Regenerate Variation / Reset, A/B slots with Morph, and a saved-pattern library.
/// A pattern is just the seeded parameter set (arp.adv), so a saved pattern reproduces exactly.
struct ArpPatternSection: View {
    @ObservedObject var s: PresetStore
    @State private var libSel = 0
    private var cur: String { s.string("arp.ab.cur", "a") }
    private func adv() -> [String: Any] { (s.value("arp.adv") as? [String: Any]) ?? [:] }
    private func num(_ d: [String: Any], _ k: String, _ def: Double = 0) -> Double { (d[k] as? NSNumber)?.doubleValue ?? def }
    private func rr(_ a: Double, _ b: Double) -> Double { Double.random(in: a...b) }
    private func newSeed(_ d: inout [String: Any]) { d["seed"] = Double(Int.random(in: 0...999_999)); d["mutSalt"] = 0.0; d["mutAmt"] = 0.0; d["varSeed"] = 0.0 }
    private var lib: [[String: Any]] { s.array("arp.lib").compactMap { $0 as? [String: Any] } }
    var body: some View {
        Section("Pattern") {
            HStack {
                Button("GENERATE") { var d = adv(); newSeed(&d); s.set("arp.adv", d) }
                Button("RANDOMISE") { var d = adv(); newSeed(&d); d["complexity"] = rr(0.1, 1); d["density"] = rr(0.3, 0.9); d["mutation"] = rr(0, 0.5); d["variation"] = rr(0, 0.4); d["randomness"] = rr(0, 0.2); d["octSpread"] = rr(0, 0.4); d["ratchet"] = rr(0, 0.3); d["accent"] = rr(0, 0.7); d["velVar"] = rr(0, 0.3); d["gateVar"] = rr(0, 0.3); d["patLen"] = Double([8, 12, 16, 24, 32].randomElement()!); s.set("arp.adv", d) }
            }.buttonStyle(.bordered)
            HStack {
                Button("MUTATE") { var d = adv(); d["mutSalt"] = num(d, "mutSalt") + 1; d["mutAmt"] = max(0.15, num(d, "mutation", 0.2)); s.set("arp.adv", d) }   // re-rolls some steps
                Button("REGEN VARIATION") { var d = adv(); d["varSeed"] = num(d, "varSeed") + 1; s.set("arp.adv", d) }                                        // re-rolls only the variation layer
                Button("RESET") { s.set("arp.adv", ["algo": s.string("arp.adv.algo", "fractal")]) }
            }.buttonStyle(.bordered)
            Picker("A/B", selection: Binding(get: { cur }, set: { x in
                if x == cur { return }
                s.set("arp.ab." + cur, adv()); let nxt = (s.value("arp.ab." + x) as? [String: Any]) ?? adv(); s.set("arp.adv", nxt); s.set("arp.ab.cur", x)
            })) { Text("A").tag("a"); Text("B").tag("b") }.pickerStyle(.segmented)
            Button("COPY TO OTHER SLOT") { s.set("arp.ab." + (cur == "b" ? "a" : "b"), adv()) }.buttonStyle(.bordered)
            ToggleRow(s: s, label: "Morph A↔B", path: "arp.morph.on")
            SliderRow(s: s, label: "Morph amount", path: "arp.morph.amt", range: 0...1, def: 0.5, fmt: "%.2f")
            if lib.isEmpty { Text("No saved patterns").font(.caption2).foregroundStyle(.secondary) }
            else { Picker("Saved", selection: $libSel) { ForEach(Array(lib.enumerated()), id: \.offset) { n, it in Text((it["name"] as? String) ?? "Pattern \(n + 1)").tag(n) } } }
            HStack {
                Button("SAVE") { let d = adv(); var l = s.array("arp.lib"); l.append(["name": "\(s.string("arp.adv.algo", "fractal")) #\(Int(num(d, "seed", 1234)))", "adv": d]); s.set("arp.lib", l); libSel = l.count - 1 }
                Button("LOAD") { if libSel < lib.count, let d = lib[libSel]["adv"] as? [String: Any] { s.set("arp.adv", d) } }.disabled(lib.isEmpty)
                Button("DELETE") { var l = s.array("arp.lib"); if libSel < l.count { l.remove(at: libSel); s.set("arp.lib", l); libSel = max(0, libSel - 1) } }.disabled(lib.isEmpty)
            }.buttonStyle(.bordered)
        }
    }
}

struct PatternEditor: View {
    @ObservedObject var s: PresetStore
    @ObservedObject private var power = BatterySaving.shared
    let layerKey: String; let layerIndex: Int
    private var base: String { layerKey == "XFX" ? "extra.xfxSeq" : "layers.\(layerKey)" }
    @State private var editing: Int? = nil
    @State private var playhead = 0
    private var slot: Int { s.int("\(base).currentSlot", 0) }
    private var pPath: String { "\(base).patterns.\(slot)" }
    var body: some View {
        Group {
            Picker("Slot", selection: s.binding("\(base).currentSlot", int: 0)) { ForEach(0..<max(1, s.count("\(base).patterns")), id: \.self) { Text("Pattern \($0 + 1)").tag($0) } }.pickerStyle(.segmented)
            StepperRow(s: s, label: "Length", path: pPath + ".length", range: 1...32, def: 16)
            HStack {
                Button("COPY PATTERN") { PatternClipboard.shared.steps = s.array(pPath + ".steps"); PatternClipboard.shared.length = s.int(pPath + ".length", 16); PatternClipboard.shared.layerKey = layerKey }
                Button("PASTE PATTERN") { paste() }.disabled(PatternClipboard.shared.steps.isEmpty)
                Button("RANDOMISE") { randomise() }
                Button("CLEAR STEPS") { s.set(pPath + ".steps", (0..<max(32, s.count(pPath + ".steps"))).map { _ in emptyStep() }) }
            }.font(.caption2).buttonStyle(.bordered).controlSize(.small)
            RollRecBar(s: s, pPath: pPath)
            TimelineView(.periodic(from: .now, by: BatterySaving.shared.interval(1.0 / 15.0))) { _ in
                let len = max(1, s.int(pPath + ".length", 16))
                let head = s.audioUnit.enginePointer.map { Int(vv_engine_seq_step($0, Int32(layerIndex))) } ?? 0
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 4), count: 8), spacing: 4) {
                    ForEach(0..<len, id: \.self) { i in
                        let on = s.bool("\(pPath).steps.\(i).on")
                        RoundedRectangle(cornerRadius: 5).fill(on ? vvCyan : Color.white.opacity(0.1))
                            .overlay(RoundedRectangle(cornerRadius: 5).stroke(i == head ? Color.white : Color.clear, lineWidth: 2))
                            .overlay(Text(on ? noteLabel(s.int("\(pPath).steps.\(i).pitch")) : "").font(.system(size: 9, design: .monospaced)).foregroundColor(.black))
                            .frame(height: 34)
                            .onTapGesture { s.set("\(pPath).steps.\(i).on", !on) }
                            .onLongPressGesture { editing = i }
                    }
                }
            }
            Text("Tap a step to toggle it, long-press to edit pitch, ratchet, probability, tempo ratio and p-locks.").font(.caption2).foregroundStyle(.secondary)
            PickerRow(s: s, label: "Chain mode", path: "\(base).chainMode", options: ["off", "loop", "oneshot"], def: "off")
            SliderRow(s: s, label: "Gate", path: "\(base).gate", range: 0.02...1, def: 0.7, fmt: "%.2f", unit: "")
            PickerRow(s: s, label: "Sidechain source", path: "\(base).scSrc", options: ["none", "WT", "FM", "SMP", "PRO"], def: "none")
            SliderRow(s: s, label: "SC threshold", path: "\(base).scThresh", range: 0...1, def: 0.25, fmt: "%.2f", unit: "")
            SliderRow(s: s, label: "SC amount", path: "\(base).scAmount", range: 0...1, def: 0.8, fmt: "%.2f", unit: "")
            ChainEditor(s: s, layerKey: layerKey)
        }
        .sheet(item: Binding(get: { editing.map { IdentifiedInt(id: $0) } }, set: { editing = $0?.id })) { it in
            StepEditor(s: s, path: "\(pPath).steps.\(it.id)", index: it.id, layerKey: layerKey)
        }
    }
    private func emptyStep() -> [String: Any] { ["on": false, "pitch": 0, "ratchet": 1, "prob": 100, "tempoRatio": 1, "plocks": [String: Any](), "transientIdx": 0] }
    private func paste() {
        let c = PatternClipboard.shared; guard !c.steps.isEmpty else { return }
        var steps = c.steps
        if c.layerKey != layerKey { steps = steps.map { var d = ($0 as? [String: Any]) ?? [:]; d["plocks"] = [String: Any](); return d } }   // p-locks are layer specific
        s.set(pPath + ".steps", steps); s.set(pPath + ".length", c.length)
    }
    /// Notes fitted to what the arpeggiator is playing (engine snapshot -> chord tones + a containing scale -> melodic picks in 48...72). nil = arp not playing / setting off.
    private func arpFittedPitches(count: Int, scale: Int, root: Int) -> [Int]? {
        guard UserDefaults.standard.object(forKey: "vv.fitArp") as? Bool ?? true, count > 0, let e = s.audioUnit.enginePointer else { return nil }
        var kb: Int32 = 0
        if scale > 0 && scale < kbScales.count { for i in kbScales[scale].iv { kb |= Int32(1) << Int32(((i + root) % 12 + 12) % 12) } }
        var strong = (0..<count).map { UInt8($0 % 4 == 0 ? 1 : 0) }
        var out = [Int32](repeating: 0, count: count)
        var name = [CChar](repeating: 0, count: 32)
        let ok = vv_engine_arp_fit(e, Int32(count), &strong, kb, UInt32.random(in: 1...UInt32.max), 48, 72, &out, &name, 32)
        return ok == 1 ? out.map { Int($0) } : nil
    }
    /// Web "RANDOMISE": 65 % on, mostly single hits with some 2-4 ratchets, probability 50-100, scale-aware pitch, random slice on the sampler, p-locks nudged +-30 %.
    private func randomise() {
        let scale = UserDefaults.standard.integer(forKey: "vv.kb.scale"), root = UserDefaults.standard.integer(forKey: "vv.kb.root")
        let slices = layerKey == "SMP" ? s.count("layers.SMP.oscs.0.transients") : 0
        let spread = [-12, 0, 0, 0, 12]
        var steps = s.array(pPath + ".steps")
        // Arp playing -> pitches that fit the arp's own harmony (chord tones on strong steps, stepwise, in key). Lock-free engine call; nil when the arp is silent or the setting is off.
        let fit = layerKey == "SMP" ? nil : arpFittedPitches(count: steps.count, scale: scale, root: root)
        for i in 0..<steps.count {
            var d = (steps[i] as? [String: Any]) ?? emptyStep()
            d["on"] = Double.random(in: 0..<1) < 0.65
            d["ratchet"] = Double.random(in: 0..<1) < 0.8 ? 1 : 1 + Int.random(in: 0..<3)
            d["prob"] = 50 + Int.random(in: 0...50)
            if let f = fit { d["pitch"] = max(-12, min(12, f[i] - 60)) }
            else if scale > 0 && scale < kbScales.count {
                let iv = kbScales[scale].iv
                d["pitch"] = ((iv.randomElement()! - ((60 - root) % 12 + 12) % 12 + 24) % 12) - 12 + spread.randomElement()!
            } else { d["pitch"] = Int.random(in: -12...12) }
            if layerKey == "SMP" { d["transientIdx"] = slices > 0 ? Int.random(in: 0...slices) : 0 }
            if var pl = d["plocks"] as? [String: Any] { for (k, v) in pl { if let n = v as? Double { pl[k] = n * Double.random(in: 0.7...1.3) } }; d["plocks"] = pl }
            steps[i] = d
        }
        s.set(pPath + ".steps", steps)
    }
    private func noteLabel(_ p: Int) -> String { p == 0 ? "•" : (p > 0 ? "+\(p)" : "\(p)") }
}

struct IdentifiedInt: Identifiable { let id: Int }

/// COPY / PASTE PATTERN clipboard (shared by every layer's editor, like the web app's state.clipboard).
final class PatternClipboard { static let shared = PatternClipboard(); var steps: [Any] = []; var length = 16; var layerKey = "" }

struct ChainEditor: View {
    @ObservedObject var s: PresetStore
    let layerKey: String
    var body: some View {
        let path = "\(layerKey == "XFX" ? "extra.xfxSeq" : "layers.\(layerKey)").chainOrder"
        VStack(alignment: .leading, spacing: 4) {
            Text("Chain order").font(.caption)
            HStack {
                ForEach(0..<s.count(path), id: \.self) { i in
                    Button("\(s.int("\(path).\(i)") + 1)") { s.remove(path, at: i) }.buttonStyle(.bordered).controlSize(.small)
                }
                Menu { ForEach(0..<max(1, s.count("\(layerKey == "XFX" ? "extra.xfxSeq" : "layers.\(layerKey)").patterns")), id: \.self) { n in Button("Add pattern \(n + 1)") { s.append(path, n) } } } label: { Image(systemName: "plus.circle") }
                if s.count(path) > 0 { Button("Clear") { s.set(path, [Int]()) }.font(.caption) }
            }
        }
    }
}

struct StepEditor: View {
    @ObservedObject var s: PresetStore
    let path: String; let index: Int; let layerKey: String
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        NavigationStack {
            Form {
                Section("Step \(index + 1)") {
                    ToggleRow(s: s, label: "On", path: path + ".on")
                    StepperRow(s: s, label: "Pitch (semitones)", path: path + ".pitch", range: -48...48)
                    StepperRow(s: s, label: "Ratchets", path: path + ".ratchet", range: 1...8, def: 1)
                    SliderRow(s: s, label: "Probability", path: path + ".prob", range: 0...100, def: 100, fmt: "%.0f", unit: " %")
                    SliderRow(s: s, label: "Tempo ratio", path: path + ".tempoRatio", range: 0.25...4, def: 1, log: true)
                }
                Section("Parameter locks (this step only)") {
                    PlockRow(s: s, path: path + ".plocks", key: "level", label: "Level", range: 0...1, def: 1)
                    PlockRow(s: s, path: path + ".plocks", key: "cutoff", label: "Cutoff", range: 20...18000, def: 4000, log: true)
                    if layerKey == "WT" { PlockRow(s: s, path: path + ".plocks", key: "position", label: "Wavetable position", range: 0...1, def: 0) }
                    if layerKey == "SMP" {
                        StepperRow(s: s, label: "Transient slice", path: path + ".transientIdx", range: 0...16)
                    }
                }
            }
            .navigationTitle("Edit step")
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
        }
    }
}

/// A p-lock: absent until switched on.
struct PlockRow: View {
    @ObservedObject var s: PresetStore
    let path: String; let key: String; let label: String; let range: ClosedRange<Double>; let def: Double; var log = false
    var body: some View {
        VStack(alignment: .leading) {
            Toggle(label, isOn: Binding(get: { s.value("\(path).\(key)") != nil }, set: { on in
                if on { s.set("\(path).\(key)", def) } else { var d = s.dict(path); d.removeValue(forKey: key); s.set(path, d) }
            })).font(.caption)
            if s.value("\(path).\(key)") != nil { SliderRow(s: s, label: label + " value", path: "\(path).\(key)", range: range, def: def, log: log) }
        }
    }
}

// ---- Chord maker: generative chord progressions (Core/include/vv/chordmaker.hpp). Written without a Swift compiler - unbuilt.
let chordScaleIds = ["major", "dorian", "phrygian", "lydian", "mixolydian", "minor", "locrian", "harmonicMinor", "melodicMinor", "majorPent", "minorPent", "blues", "wholeTone", "diminished", "lydianDominant", "altered", "neapolitanMinor", "hijaz", "doubleHarmonic", "hungarianMinor", "persian", "enigmatic", "hirajoshi", "inSen", "iwato", "yo", "kumoi", "chineseGong", "pelog", "slendro", "ragaTodi", "ragaMarwa", "ragaPurvi", "maqamRast", "maqamSaba", "egyptian"]
let chordScaleLabels = ["Western: Major (Ionian)", "Western: Dorian", "Western: Phrygian", "Western: Lydian", "Western: Mixolydian", "Western: Natural minor (Aeolian)", "Western: Locrian", "Western: Harmonic minor", "Western: Melodic minor", "Western: Major pentatonic", "Western: Minor pentatonic", "Western: Blues", "Western: Whole tone", "Western: Diminished (whole-half)", "Western: Lydian dominant", "Western: Altered (super-locrian)", "Western: Neapolitan minor", "World: Hijaz / Phrygian dominant (Arabic, Flamenco)", "World: Double harmonic (Byzantine, Bhairav)", "World: Hungarian minor (Romani)", "World: Persian", "World: Enigmatic", "World: Hirajoshi (Japan)", "World: In Sen (Japan)", "World: Iwato (Japan)", "World: Yo (Japan)", "World: Kumoi (Japan)", "World: Chinese Gong", "World: Pelog (Bali / Java, approx.)", "World: Slendro (Java, approx.)", "World: Raga Todi (India)", "World: Raga Marwa (India)", "World: Raga Purvi (India)", "World: Maqam Rast (approx.)", "World: Maqam Saba (approx.)", "World: Egyptian (Suspended pentatonic)"]
struct ChordCard: Identifiable { let id: Int; let name: String; let deg: Int; let n: Int; let rest: Bool; let locked: Bool }

struct ChordSection: View {
    @ObservedObject var s: PresetStore
    @State private var cards: [ChordCard] = []
    @State private var playing = -1
    private let timer = Timer.publish(every: 0.1, on: .main, in: .common).autoconnect()
    private var count: Int { s.bool("chordMaker.euclid.on") ? max(1, min(s.int("chordMaker.euclid.pulses", 5), s.int("chordMaker.euclid.steps", 8))) : s.int("chordMaker.steps", 8) }
    var body: some View {
        Section("Chord maker (generative)") {
            Toggle("On (plays with Run sequencer)", isOn: Binding(get: { s.bool("chordMaker.on") }, set: { s.set("chordMaker.on", $0) })).font(.caption)
            HStack {
                Button("Generate") { act("generate") }; Button("Mutate") { act("mutate") }; Button("Re-voice") { act("revoice") }
            }.buttonStyle(.bordered)
            HStack {
                Button("◀ Rotate") { act("rotl") }; Button("Rotate ▶") { act("rotr") }; Button("Reverse") { act("reverse") }
            }.buttonStyle(.bordered)
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 96))], spacing: 6) {
                ForEach(cards) { c in
                    VStack(alignment: .leading, spacing: 2) {
                        Text(c.rest ? "rest" : c.name).font(.headline).foregroundColor(.cyan)
                        Text(c.rest ? "" : "deg \(c.deg) · \(c.n) notes").font(.caption2).foregroundStyle(.secondary)
                        Text(c.locked ? "locked" : "tap: lock").font(.caption2).foregroundColor(c.locked ? .orange : .secondary)
                    }
                    .padding(6).frame(maxWidth: .infinity, alignment: .leading)
                    .overlay(RoundedRectangle(cornerRadius: 6).stroke(c.id == playing ? Color.cyan : (c.locked ? Color.orange : Color.gray.opacity(0.4)), lineWidth: c.id == playing ? 2 : 1))
                    .opacity(c.rest ? 0.5 : 1)
                    .onTapGesture { act("lock", c.id) }
                    .contextMenu { Button("Re-roll this chord") { act("reroll", c.id) } }
                }
            }
            ChoiceRow(s: s, label: "Key", path: "chordMaker.key", choices: (0..<12).map { (["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"][$0], Double($0)) }, def: 0)
            Picker("Mode / scale", selection: s.binding("chordMaker.scale", string: "major")) { ForEach(0..<chordScaleIds.count, id: \.self) { Text(chordScaleLabels[$0]).tag(chordScaleIds[$0]) } }.pickerStyle(.menu).font(.caption)
            PickerRow(s: s, label: "Voicing", path: "chordMaker.voicing", options: ["close", "open", "drop2"], def: "close")
            PickerRow(s: s, label: "Play on", path: "chordMaker.target", options: ["ALL", "WT", "FM", "SMP", "PRO", "XFX"], def: "ALL")   // XFX: each chord note plays the layers its XFX key programs
            StepperRow(s: s, label: "Chords (steps)", path: "chordMaker.steps", range: 1...16, def: 8)
            StepperRow(s: s, label: "Octave", path: "chordMaker.octave", range: 1...6, def: 3)
            ChoiceRow(s: s, label: "Rate", path: "chordMaker.rate", choices: [("÷4", 0.25), ("÷3", 1.0 / 3), ("÷2", 0.5), ("÷1.5", 2.0 / 3), ("×1", 1), ("×1.5", 1.5), ("×2", 2), ("×3", 3), ("×4", 4)], def: 1)
            SliderRow(s: s, label: "Naturalness", path: "chordMaker.naturalness", range: 0...1, def: 0.7)
            SliderRow(s: s, label: "Density", path: "chordMaker.density", range: 0...1, def: 0.4)
            SliderRow(s: s, label: "Sparseness", path: "chordMaker.sparseness", range: 0...1, def: 0.15)
            SliderRow(s: s, label: "Randomness", path: "chordMaker.randomness", range: 0...1, def: 0.1)
            SliderRow(s: s, label: "Probability", path: "chordMaker.prob", range: 0...100, def: 100, fmt: "%.0f", unit: " %")
            SliderRow(s: s, label: "Gate", path: "chordMaker.gate", range: 0.05...1, def: 0.9)
            SliderRow(s: s, label: "Strum", path: "chordMaker.strum", range: 0...120, def: 0, fmt: "%.0f", unit: " ms")
            SliderRow(s: s, label: "Velocity", path: "chordMaker.vel", range: 0.1...1, def: 0.8)
            ToggleRow(s: s, label: "Euclidean (one chord per pulse)", path: "chordMaker.euclid.on")
            StepperRow(s: s, label: "Pulses", path: "chordMaker.euclid.pulses", range: 1...32, def: 5)
            StepperRow(s: s, label: "Steps", path: "chordMaker.euclid.steps", range: 1...32, def: 8)
            StepperRow(s: s, label: "Rotate", path: "chordMaker.euclid.rotate", range: 0...31, def: 0)
        }
        .onAppear { if s.value("chordMaker.progression") == nil { act("generate") } else { refresh() } }
        .onChange(of: count) { _ in act("fit") }
        .onChange(of: s.string("chordMaker.scale", "major")) { _ in act("revoice") }
        .onReceive(timer) { _ in if let e = s.audioUnit.enginePointer { let p = Int(vv_engine_chord_playing(e)); if p != playing { playing = p } } }
    }
    private func act(_ a: String, _ i: Int = 0) {
        guard let e = s.audioUnit.enginePointer else { return }
        _ = vv_engine_chord_action(e, a, Int32(i), 0); s.reload(); refresh()
    }
    private func refresh() {
        guard let e = s.audioUnit.enginePointer else { return }
        let n = vv_engine_chord_info(e, nil, 0); var buf = [CChar](repeating: 0, count: n + 1); _ = vv_engine_chord_info(e, &buf, n + 1)
        guard let d = String(cString: buf).data(using: .utf8), let arr = try? JSONSerialization.jsonObject(with: d) as? [[String: Any]] else { return }
        cards = arr.enumerated().map { ChordCard(id: $0.offset, name: $0.element["name"] as? String ?? "", deg: ($0.element["deg"] as? NSNumber)?.intValue ?? 0, n: ($0.element["n"] as? NSNumber)?.intValue ?? 0, rest: $0.element["rest"] as? Bool ?? false, locked: $0.element["locked"] as? Bool ?? false) }
    }
}

/// Multi-select arp layers (web: Arp layer dropdown with ticks). Stored as arp.targets (["ALL"] or ids WT1, WT2, FMA, FMB, PRO, S1...S16); arp.target keeps the first entry for older readers.
struct ArpLayersRow: View {
    @ObservedObject var s: PresetStore
    private let all: [(String, String)] = [("ALL", "All layers"), ("WT1", "Wavetable 1"), ("WT2", "Wavetable 2"), ("FMA", "FM A"), ("FMB", "FM B"), ("PRO", "Procedural"), ("SID", "SID"), ("LUA", "LUA")] + (1...16).map { ("S\($0)", "Sample \($0)") }
    private var current: [String] {
        if let a = s.value("arp.targets") as? [String], !a.isEmpty { return a }
        let t = (s.value("arp.target") as? String) ?? "ALL"; return [t.isEmpty ? "ALL" : t]
    }
    private func set(_ l: [String]) { let n = l.isEmpty ? ["ALL"] : l; s.set("arp.targets", n); s.set("arp.target", n[0]) }
    private var summary: String { let c = current; if c.contains("ALL") { return "All layers" }; return c.count > 2 ? "\(c.count) layers selected" : c.compactMap { id in all.first { $0.0 == id }?.1 }.joined(separator: " + ") }
    var body: some View {
        Menu {
            ForEach(all, id: \.0) { item in
                Button { var c = current
                    if item.0 == "ALL" { c = ["ALL"] } else { c.removeAll { $0 == "ALL" }; if let i = c.firstIndex(of: item.0) { c.remove(at: i) } else { c.append(item.0) } }
                    set(c) } label: { Label(item.1, systemImage: current.contains(item.0) ? "checkmark" : "") }
            }
        } label: { HStack { Text("Arp layer"); Spacer(); Text(summary).foregroundStyle(.secondary) } }
    }
}
