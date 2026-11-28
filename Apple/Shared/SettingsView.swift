// SETTINGS tab: voices, tuning, MPE, tempo, global mix controls. Preset-backed values live in the preset (saved with it);
// the rest are instrument-wide settings kept in UserDefaults and re-applied at launch.
import SwiftUI

/// The standalone host may control screen sleep; an AUv3 extension leaves this to its host DAW.
enum HostApp { static var setIdleTimerDisabled: (Bool) -> Void = { _ in } }

struct SettingsView: View {
    @ObservedObject var s: PresetStore
    @AppStorage("vv.mpe") private var mpe = false
    @AppStorage("vv.fitArp") private var fitArp = true   // RANDOMISE fits notes to the playing arp
    @AppStorage("vv.kb.labels") private var kbLabels = false   // moved here from the keyboard options row
    @AppStorage("vv.bgAudio") private var bgAudio = true
    @AppStorage("vv.noSleep") private var noSleep = false
    @AppStorage("vv.midiChannel") private var midiChannel = 0   // 0 = Omni, 1...16
    @AppStorage("vv.lim.on") private var limOn = false
    @AppStorage("vv.lim.thr") private var limThr = -1.0
    @AppStorage("vv.lim.ceil") private var limCeil = -1.0
    @AppStorage("vv.lim.rel") private var limRel = 100.0
    @AppStorage("vv.lim.knee") private var limKnee = 0.0
    @AppStorage("vv.lim.soft") private var limSoft = 0.0
    @State private var loud: [Double] = [-1000, -1000, -1000, -1000, -1000, -1000]
    private let loudTimer = Timer.publish(every: 0.2, on: .main, in: .common).autoconnect()
    @State private var showVoid = false   // hidden Easter egg: Vektur Void
    var body: some View {
        NavigationStack {
            Form {
                Section("Performance") {
                    // Poly: the one and only polyphony control (moved from the keyboard bar). Same bindings: maxPolyphony + legatoMode.
                    Picker("Poly", selection: Binding(get: { s.bool("legatoMode", false) ? -1 : ((s.value("maxPolyphony") as? NSNumber)?.intValue ?? 64) }, set: { v in
                        s.set("maxPolyphony", v < 0 ? 1 : v); s.set("legatoMode", v < 0) })) {
                        Text("1 (mono)").tag(1); Text("1 (Legato)").tag(-1)
                        ForEach([2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,24,32,48,64], id: \.self) { Text("\($0)").tag($0) }
                    }.font(.caption)
                    Button("Panic (all notes off)", role: .destructive) { s.audioUnit.panic() }
                }
                Section("Tuning & tempo") {
                    SliderRow(s: s, label: "A4 reference", path: "a4Reference", range: 415...466, def: 440, fmt: "%.1f", unit: " Hz")
                    SliderRow(s: s, label: "Tempo (standalone; the host sets it in AUv3)", path: "bpm", range: 20...300, def: 120, fmt: "%.0f", unit: " BPM")
                }
                Section("Keyboard") {
                    Toggle("Show note names on the keys (Labels)", isOn: $kbLabels).font(.caption)
                    Toggle("RANDOMISE fits the arp (while the arp plays, random steps use its chord tones and a matching scale)", isOn: $fitArp).font(.caption)
                }
                Section("Expression") {
                    Toggle("MPE (per-note bend / slide / pressure on channels 2-16)", isOn: Binding(get: { mpe }, set: { mpe = $0; apply() })).font(.caption)
                    GlobalSlider(s: s, label: "Pitch-bend range (semitones)", name: "bendRange", range: 1...48, def: 2, fmt: "%.0f")
                }
                BatterySavingSection()
                Section("Device") {
                    Toggle("Background audio (keep playing when minimised or switching apps)", isOn: $bgAudio).font(.caption)
                    Toggle("Do not sleep (stop the screen sleeping / locking)", isOn: Binding(get: { noSleep }, set: { noSleep = $0; apply() })).font(.caption)
                    Picker("App MIDI channel", selection: Binding(get: { midiChannel }, set: { midiChannel = $0; apply() })) {
                        Text("Omni").tag(0); ForEach(1...16, id: \.self) { Text("\($0)").tag($0) }
                    }.font(.caption)
                    Text("MIDI channel is ignored while MPE is on.").font(.caption2).foregroundStyle(.secondary)
                }
                Section("Master output - limiter & loudness") {
                    Toggle("Limiter on", isOn: Binding(get: { limOn }, set: { limOn = $0; apply() })).font(.caption)
                    limRow("Threshold", $limThr, -24...0, " dB"); limRow("Ceiling", $limCeil, -12...0, " dB"); limRow("Release", $limRel, 5...1000, " ms")
                    limRow("Knee", $limKnee, 0...12, " dB"); limRow("Soft clip", $limSoft, 0...1, "")
                    Text(loudText).font(.system(size: 11, design: .monospaced))
                    Button("Reset integrated") { if let e = s.audioUnit.enginePointer { vv_engine_reset_loudness(e) } }.font(.caption)
                }
                Section("Mix") {
                    GlobalSlider(s: s, label: "WT / FM mix", name: "wtFmMix", range: 0...1, def: 0.5)
                    GlobalSlider(s: s, label: "Master tone (low-pass ... high-pass)", name: "masterTone", range: 0...1, def: 0)
                    GlobalToggle(s: s, label: "Sample mix morph on", name: "sampleMixOn")
                    GlobalSlider(s: s, label: "Sample mix position (1-16)", name: "sampleMix", range: 0...15, def: 0, fmt: "%.1f")
                }
                MidiLearnSection(s: s)
                AppearanceSection()
                Section("Output") {
                    PickerRow(s: s, label: "Oversampling", path: "oversample", options: ["normal", "2x", "4x"], def: "normal")
                }
                Section("About") {
                    Text("Vektur Vortex - Hybrid Synthesiser").font(.caption)
                    Text("Wavetable · FM · Sampler · Procedural, 64-voice polyphony, native C++ DSP.").font(.caption2).foregroundStyle(.secondary)
                }
                Section {   // the Easter egg: deliberately small and dim, at the very bottom (web: vvEasterEggBtn)
                    Button("EASTER EGG") { showVoid = true }.font(.system(size: 9, design: .monospaced)).foregroundStyle(.secondary).opacity(0.55)
                }
            }
            .navigationTitle("Settings").navigationBarTitleDisplayMode(.inline)
            .onAppear(perform: apply)
            .onReceive(loudTimer) { _ in if let e = s.audioUnit.enginePointer { var o = [Double](repeating: 0, count: 6); vv_engine_get_loudness(e, &o); loud = o } }
            .fullScreenCover(isPresented: $showVoid) { VekturVoidView() }   // closing it releases the whole game; the next open is brand new
        }
    }
    private func limRow(_ label: String, _ v: Binding<Double>, _ r: ClosedRange<Double>, _ unit: String) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack { Text(label).font(.caption); Spacer(); Text(String(format: "%.1f", v.wrappedValue) + unit).font(.caption.monospacedDigit()).foregroundStyle(.secondary) }
            Slider(value: Binding(get: { v.wrappedValue }, set: { v.wrappedValue = $0; apply() }), in: r)
        }
    }
    private var loudText: String {
        func f(_ x: Double) -> String { x <= -999 ? "-" : String(format: "%.1f", x) }
        return "M \(f(loud[0])) S \(f(loud[1])) I \(f(loud[2])) LUFS  Peak \(f(loud[3]))/\(f(loud[4])) dBFS  GR \(loud[5] <= -999 ? "0.0" : String(format: "%.1f", loud[5])) dB"
    }
    private func apply() {
        if let e = s.audioUnit.enginePointer { vv_engine_set_master_limiter(e, limOn ? 1 : 0, limThr, limCeil, limRel, limKnee, limSoft) }
        if let e = s.audioUnit.enginePointer { vv_engine_set_mpe(e, mpe ? 1 : 0); vv_engine_set_midi_channel(e, Int32(midiChannel)) }
        HostApp.setIdleTimerDisabled(noSleep)
    }
}
