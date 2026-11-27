// DAW-style RECORDER block (web footer: REC / STOP / time / settings with bit depth, sample rate, click track, takes). Needed for standalone use.
// Engine side: Core/include/vv/daw_recorder.hpp via vv_daw_*. NEVER COMPILED (no Mac); see docs/MAC_DAY_ONE.md.
import SwiftUI

struct DawTake: Identifiable { let id = UUID(); var name: String; var url: URL; var sampleName: String }
final class DawTakes: ObservableObject {
    static let shared = DawTakes()
    @Published var takes: [DawTake] = []
    static var folder: URL {
        let d = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("Vektur Vortex Recordings", isDirectory: true)
        try? FileManager.default.createDirectory(at: d, withIntermediateDirectories: true); return d
    }
}

struct DawRecorderBlock: View {
    @ObservedObject var s: PresetStore
    @ObservedObject private var store = DawTakes.shared
    @AppStorage("vv.dawBits") private var bits = 16
    @AppStorage("vv.dawRate") private var rate = 48000
    @AppStorage("vv.dawClick") private var click = 0
    @State private var recording = false, busy = false, showSettings = false
    @State private var elapsed = 0.0
    @State private var note = ""
    private let timer = Timer.publish(every: 0.1, on: .main, in: .common).autoconnect()
    private func fmt(_ t: Double) -> String { String(format: "%02d:%02d", Int(t) / 60, Int(t) % 60) }
    var body: some View {
        HStack(spacing: 4) {
            Button { start() } label: { Text("REC").foregroundStyle(recording ? .white : .red) }.tint(recording ? .red : nil).disabled(recording || busy)
            Button("STOP") { stop() }.disabled(!recording)
            Text(busy ? "..." : fmt(elapsed)).font(.system(size: 12, design: .monospaced)).foregroundStyle(vvCyan).frame(minWidth: 44)
            Button("SET") { showSettings = true }
                .popover(isPresented: $showSettings) { settings }
        }
        .onReceive(timer) { _ in
            guard recording else { return }
            elapsed = s.dawDrain(); if s.dawState == 2 { stop() }
        }
        .onAppear { s.dawSetClick(click) }
        .onChange(of: click) { s.dawSetClick($0) }
    }
    private func start() { s.dawSetClick(click); s.dawStart(); recording = true; elapsed = 0 }
    private func stop() {
        guard recording else { return }
        s.dawStop(); recording = false; busy = true
        let secs = s.dawDrain(), dropped = s.dawDropped, b = bits, r = rate
        let name = "Recording " + { let f = DateFormatter(); f.dateFormat = "yyyy-MM-dd HH-mm-ss"; return f.string(from: Date()) }()
        DispatchQueue.global(qos: .userInitiated).async {
            let wav = s.dawTakeWav(bits: b, rate: r)
            let lib: Data = secs <= 60 && !wav.isEmpty ? s.dawTakeWav(bits: 16, rate: 0) : Data()   // sampler copy only for short takes (it is embedded in the preset)
            DispatchQueue.main.async {
                busy = false; elapsed = 0
                guard !wav.isEmpty else { return }
                let url = DawTakes.folder.appendingPathComponent(name + ".wav"); try? wav.write(to: url)
                var t = DawTake(name: name, url: url, sampleName: "")
                if !lib.isEmpty { t.sampleName = s.uniqueSampleName(name); s.putSample(name: t.sampleName, wav: lib) }   // usable straight away as a sampler sample
                store.takes.append(t)
                note = dropped > 0 ? "The recorder fell behind and dropped \(dropped) samples; the take may have a gap." : ""
            }
        }
    }
    private var settings: some View {
        VStack(alignment: .leading, spacing: 8) {
            Picker("Bit depth", selection: $bits) { Text("16-bit").tag(16); Text("24-bit").tag(24) }
            Picker("Sample rate", selection: $rate) { Text("44.1 kHz").tag(44100); Text("48 kHz").tag(48000); Text("96 kHz").tag(96000) }
            Picker("Click track", selection: $click) { Text("Off").tag(0); Text("Beep").tag(1); Text("Rimshot").tag(2) }
            if !note.isEmpty { Text(note).font(.caption).foregroundStyle(.orange) }
            Text("Takes (this session)").font(.caption).foregroundStyle(.secondary)
            if store.takes.isEmpty { Text("No recordings yet.").font(.caption) }
            ForEach(store.takes) { t in
                HStack {
                    TextField("Name", text: Binding(get: { t.name }, set: { n in if let i = store.takes.firstIndex(where: { $0.id == t.id }) { store.takes[i].name = n } }))
                    ShareLink(item: t.url) { Image(systemName: "square.and.arrow.up") }
                    Button(role: .destructive) { remove(t) } label: { Image(systemName: "xmark") }
                }
            }
        }.padding().frame(minWidth: 360)
    }
    private func remove(_ t: DawTake) {
        try? FileManager.default.removeItem(at: t.url)
        if !t.sampleName.isEmpty, let i = s.array("sampleLibrary").firstIndex(where: { ($0 as? [String: Any])?["name"] as? String == t.sampleName }) { s.remove("sampleLibrary", at: i) }
        store.takes.removeAll { $0.id == t.id }
    }
}
