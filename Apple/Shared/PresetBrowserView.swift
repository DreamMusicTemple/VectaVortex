// PRESETS tab: factory presets (bundled), the user library (Documents/Presets, visible in Files), import/export of the web app's
// preset JSON, save-as, and an "initialise" button.
import SwiftUI
import UniformTypeIdentifiers

struct PresetBrowserView: View {
    @ObservedObject var s: PresetStore
    @State private var entries: [PresetEntry] = []
    @State private var artistSel = ""          // "" = all artist banks
    @State private var catSel = ""             // "" = all categories
    @State private var importDest = "My presets"
    @State private var artistPrompt: ArtistPrompt?
    @State private var artistText = ""
    @State private var confirmDelete = false
    @State private var saveArtist = ""
    @State private var saveCat = "Misc"
    @State private var zipURL: URL?
    enum ArtistPrompt: Identifiable { case new, rename; var id: Int { self == .new ? 0 : 1 } }
    @State private var importing = false
    @State private var naming = false
    @State private var newName = ""
    @State private var message = ""
    private var current: String { get { s.presetName } nonmutating set { s.presetName = newValue } }
    @State private var query = ""
    @State private var genCategory = "Pad"

    private var userFolder: URL {
        let u = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("Presets", isDirectory: true)
        try? FileManager.default.createDirectory(at: u, withIntermediateDirectories: true)
        return u
    }
    var body: some View {
        NavigationStack {
            List {
                Section {
                    Button("Initialise (default sound)") { loadDefault() }
                    Button("Save current sound as…") { newName = ""; naming = true }
                    Picker("Generate for", selection: $genCategory) { ForEach(PresetStore.generatableCategories, id: \.self) { Text($0).tag($0) } }
                    Button("🪄 GENERATE") { message = s.generate(genCategory); refresh() }
                    Button("MODIFY (~10% variation)") { message = s.modify(); refresh() }
                    Button("Import preset (.json / .vvpreset.json) from the web app…") { importing = true }
                    ShareLink(item: s.exportSinglePresetFile(name: current.isEmpty ? "Vektur Vortex preset" : current), subject: Text("Vektur Vortex preset"), message: Text("")) { Label("Export current sound (JSON)", systemImage: "square.and.arrow.up") }
                    if !message.isEmpty { Text(message).font(.caption).foregroundStyle(.secondary) }
                }
                Section("Artist banks") {
                    Picker("Artist folder", selection: $artistSel) { Text("All artist banks").tag(""); ForEach(PresetLibrary.artists(entries), id: \.self) { Text($0).tag($0) } }
                    Picker("Import into", selection: $importDest) { Text("My presets").tag("My presets"); ForEach(PresetLibrary.artists(entries).filter { $0 != "Factory" && $0 != "My presets" }, id: \.self) { Text($0).tag($0) } }
                    Button("NEW ARTIST FOLDER") { artistText = ""; artistPrompt = .new }
                    Button("RENAME") { artistText = artistSel; artistPrompt = .rename }.disabled(!PresetLibrary.isUserArtist(artistSel))
                    Button("DELETE ARTIST BANK", role: .destructive) { confirmDelete = true }.disabled(!PresetLibrary.isUserArtist(artistSel))
                    if let z = zipURL { ShareLink(item: z) { Label("Share bank .zip", systemImage: "square.and.arrow.up") } }
                    Button("EXPORT BANK (.zip)") { exportBank() }
                }
                Section {
                    ScrollView(.horizontal, showsIndicators: false) {
                        HStack { ForEach([""] + presetCategories, id: \.self) { c in
                            Button(c.isEmpty ? "All" : c) { catSel = c }.buttonStyle(.borderedProminent).tint(catSel == c ? vvCyan : .gray)
                        } }
                    }
                }
                ForEach(presetCategories, id: \.self) { cat in
                    let rows = entries.filter { $0.category == cat && (catSel.isEmpty || catSel == cat) && (artistSel.isEmpty || $0.artist == artistSel) && (query.isEmpty || $0.name.localizedCaseInsensitiveContains(query)) }
                    if !rows.isEmpty {
                        SwiftUI.Section(cat.uppercased()) {
                            ForEach(rows) { p in
                                Button { loadFile(p.url, p.name) } label: { HStack { Text(p.name); Spacer(); if artistSel.isEmpty { Text(p.artist).font(.caption2).foregroundStyle(.secondary) }; if current == p.name { Image(systemName: "checkmark").foregroundStyle(vvCyan) } } }
                            }.onDelete { idx in for i in idx where !rows[i].factory { try? FileManager.default.removeItem(at: rows[i].url) }; refresh() }
                        }
                    }
                }
            }
            .searchable(text: $query)
            .navigationTitle("Presets")
            .fileImporter(isPresented: $importing, allowedContentTypes: [.json]) { result in
                guard case .success(let url) = result else { return }
                let ok = url.startAccessingSecurityScopedResource(); defer { if ok { url.stopAccessingSecurityScopedResource() } }
                guard let text = try? String(contentsOf: url) else { message = "Could not read that file."; return }
                if s.load(json: text) {
                    let n = url.deletingPathExtension().lastPathComponent
                    let dest = importDest == "My presets" ? userFolder : userFolder.appendingPathComponent(importDest, isDirectory: true)
                    try? FileManager.default.createDirectory(at: dest, withIntermediateDirectories: true)
                    try? text.write(to: dest.appendingPathComponent(n + ".json"), atomically: true, encoding: .utf8)
                    current = n; message = "Imported \(n)"; refresh()
                } else { message = "That file is not a valid Vektur Vortex preset." }
            }
            .alert("Save preset", isPresented: $naming) {
                TextField("Name", text: $newName)
                TextField("Artist bank (blank = My presets)", text: $saveArtist)
                TextField("Category (\(presetCategories.joined(separator: ", ")))", text: $saveCat)
                Button("Save") { save(newName) }
                Button("Cancel", role: .cancel) {}
            }
            .alert(artistPrompt == .rename ? "Rename artist folder" : "New artist folder", isPresented: Binding(get: { artistPrompt != nil }, set: { if !$0 { artistPrompt = nil } })) {
                TextField("Name", text: $artistText)
                Button("OK") { applyArtistPrompt() }
                Button("Cancel", role: .cancel) { artistPrompt = nil }
            }
            .confirmationDialog("Delete '\(artistSel)' and every preset in it? This cannot be undone.", isPresented: $confirmDelete, titleVisibility: .visible) {
                Button("Delete", role: .destructive) { try? FileManager.default.removeItem(at: userFolder.appendingPathComponent(artistSel)); artistSel = ""; refresh() }
            }
            .onAppear { refresh(); consumeRequests() }
            .onChange(of: s.requestImport) { _ in consumeRequests() }
            .onChange(of: s.requestSaveAs) { _ in consumeRequests() }
        }
    }

    private func consumeRequests() {
        if s.requestImport { s.requestImport = false; importing = true }
        if s.requestSaveAs { s.requestSaveAs = false; newName = current; naming = true }
    }
    private func refresh() { entries = PresetLibrary.scan() }
    private func applyArtistPrompt() {
        let n = PresetLibrary.clean(artistText); defer { artistPrompt = nil }
        guard !n.isEmpty else { return }
        let fm = FileManager.default
        if artistPrompt == .new { try? fm.createDirectory(at: userFolder.appendingPathComponent(n), withIntermediateDirectories: true); artistSel = n }
        else if PresetLibrary.isUserArtist(artistSel), (try? fm.moveItem(at: userFolder.appendingPathComponent(artistSel), to: userFolder.appendingPathComponent(n))) != nil { artistSel = n }
        refresh()
    }
    private func exportBank() {
        let src = entries.filter { artistSel.isEmpty || $0.artist == artistSel }
        let files: [(path: String, data: Data)] = src.compactMap { e in (try? Data(contentsOf: e.url)).map { ("\(e.artist)/\(e.category)/\(e.name).json", $0) } }
        let url = FileManager.default.temporaryDirectory.appendingPathComponent((artistSel.isEmpty ? "All presets" : artistSel) + ".zip")
        do { try PresetLibrary.zip(files).write(to: url); zipURL = url; message = "Prepared \(files.count) presets: use Share bank .zip" } catch { message = "Export failed: \(error.localizedDescription)" }
    }
    private func loadFile(_ url: URL, _ name: String) {
        guard let text = try? String(contentsOf: url) else { message = "Could not read \(name)."; return }
        if s.load(json: text) { current = name; message = "Loaded \(name)" } else { message = "Could not load \(name)." }
    }
    private func loadDefault() {
        let b = Bundle(for: VectaAudioUnit.self)
        guard let u = b.url(forResource: "default_preset", withExtension: "json"), let t = try? String(contentsOf: u) else { return }
        if s.load(json: t) { current = ""; message = "Initialised." }
    }
    private func save(_ raw: String) {
        let n = PresetLibrary.clean(raw)
        guard !n.isEmpty else { return }
        let json = s.exportJSON(); guard !json.isEmpty else { message = "Nothing to save yet."; return }
        let art = PresetLibrary.clean(saveArtist)
        let cat = presetCategories.first { $0.caseInsensitiveCompare(saveCat.trimmingCharacters(in: .whitespaces)) == .orderedSame } ?? "Misc"
        let dir = art.isEmpty ? userFolder : userFolder.appendingPathComponent(art, isDirectory: true).appendingPathComponent(cat, isDirectory: true)
        do { try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true); try json.write(to: dir.appendingPathComponent(n + ".json"), atomically: true, encoding: .utf8); current = n; message = "Saved \(n)" + (art.isEmpty ? "" : " to \(art)"); refresh() }
        catch { message = "Could not save: \(error.localizedDescription)" }
    }
}
