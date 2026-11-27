
struct InterstellarPresetMenu: View {
    @ObservedObject var s: PresetStore
    let base: String
    var body: some View {
        HStack {
            Text("Presets").font(.caption); Spacer()
            Menu {
                ForEach(Array(InterstellarPresets.all.enumerated()), id: \.offset) { _, p in
                    Button("\(p.category) - \(p.name)") { apply(p.json) }
                }
            } label: { Label("Load preset", systemImage: "sparkles") }
        }
    }
    /// A preset is a full parameter set; capture is bumped so a capture-mode preset grabs fresh audio.
    private func apply(_ json: String) {
        let cap = s.int("\(base).params.capture")
        for src in [InterstellarPresets.defaultsJSON, json] {
            guard let data = src.data(using: .utf8), let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { continue }
            for (k, v) in obj where k != "capture" { s.set("\(base).params.\(k)", v) }
        }
        s.set("\(base).params.capture", cap + 1)
    }
}
