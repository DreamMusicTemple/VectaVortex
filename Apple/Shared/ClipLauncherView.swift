// Clip launcher: 8 banks x 8 slots of loopable note + automation clips, launched on the engine's transport clock (quantised).
// The model, validation, undo, recording and playback all live in the C++ core (Core/include/vv/clips.hpp); this file is the view + a thin store over the C ABI (vv_clip_*).
// The whole clip state is saved inside the preset JSON (extra.clips), so presets, AUv3 state and host session recall carry it. Same data as the web app's clip launcher.
// NOTE: written without a Mac. Not compiled yet (the C++ behind it is tested: Core/tests/clips_tests.cpp, clips_engine_tests.cpp).
import SwiftUI
import UniformTypeIdentifiers

struct ClipInfo: Identifiable, Equatable {
    var id: Int                    // slot 0..63 (bank * 8 + index)
    var present = false, factory = false
    var clipId = ""; var name = ""; var color = "#4ad4d9"; var layer = "WT"
    var bars = 4, transpose = 0, noteCount = 0, laneCount = 0
}

/// Lets the dials write their movement into the clip that is recording (the web app does the same from its dial drag handler).
final class ClipRecorder {
    static let shared = ClipRecorder()
    weak var store: ClipStore?
    var isActive: Bool { store?.recordingSlot ?? -1 >= 0 }
    func write(key: String, delta: Double, bipolar: Bool) { store?.writeAutomation(key: key, delta: delta, bipolar: bipolar) }
    func end(key: String) { store?.endAutomation(key: key) }
}

final class ClipStore: ObservableObject {
    @Published var slots: [ClipInfo] = (0..<64).map { ClipInfo(id: $0) }
    @Published var bankNames: [String] = (1...8).map { "BANK \($0)" }
    @Published var bankColors: [String] = Array(repeating: "#4ad4d9", count: 8)
    @Published var bank = 0, selected = 0, quant = 5, mode = 0
    @Published var playing: UInt64 = 0, queuedStart: UInt64 = 0, queuedStop: UInt64 = 0
    @Published var recordingSlot = -1
    @Published var phases: [Float] = Array(repeating: -1, count: 64)
    @Published var message = ""
    @Published var confirm: PendingConfirm?
    @Published var library: [URL] = []

    struct PendingConfirm: Identifiable { let id = UUID(); let text: String; let actions: [(String, Bool, () -> Void)] }   // (title, destructive, action)

    static let quantNames = ["Immediate", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars"]
    static let lens = [1, 2, 4, 8, 16, 32, 64]
    static let layerKeys = LayerID.keys + ["XFX"]   // Round 6: a clip can target XFX MODE (core layer index 6): every note plays the layers its XFX key programs
    var layerHint = 0   // the layer whose piano roll ROLL → CLIP reads when the slot is empty (the Sequencer page's current layer)
    private let store: PresetStore
    private var timer: Timer?
    private var engine: OpaquePointer? { store.audioUnit.enginePointer }

    init(store: PresetStore) {
        self.store = store
        ClipRecorder.shared.store = self
        refresh(); reloadLibrary()
        timer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in self?.poll() }
    }
    deinit { timer?.invalidate(); if ClipRecorder.shared.store === self { ClipRecorder.shared.store = nil } }

    // MARK: engine access
    @discardableResult func op(_ name: String, _ args: [String: Any] = [:], refresh doRefresh: Bool = true) -> Bool {
        guard let e = engine else { return false }
        let data = (try? JSONSerialization.data(withJSONObject: args)) ?? Data("{}".utf8)
        let ok = vv_clip_op(e, name, String(data: data, encoding: .utf8) ?? "{}", nil, 0) == 1
        if doRefresh { refresh() }
        return ok
    }
    private func opText(_ name: String, _ args: [String: Any]) -> String? {
        guard let e = engine else { return nil }
        let data = (try? JSONSerialization.data(withJSONObject: args)) ?? Data("{}".utf8)
        var buf = [CChar](repeating: 0, count: 1 << 20)
        let ok = vv_clip_op(e, name, String(data: data, encoding: .utf8) ?? "{}", &buf, buf.count) == 1
        return ok ? String(cString: buf) : nil
    }
    func refresh() {
        guard let e = engine else { return }
        let n = vv_clip_state_json(e, nil, 0); if n == 0 { return }
        var buf = [CChar](repeating: 0, count: n + 1); _ = vv_clip_state_json(e, &buf, n + 1)
        guard let d = String(cString: buf).data(using: .utf8), let j = try? JSONSerialization.jsonObject(with: d) as? [String: Any] else { return }
        quant = quantIndex(j["quant"] as? String ?? "1bar"); mode = (j["mode"] as? String) == "overdub" ? 1 : 0; bank = j["bank"] as? Int ?? 0
        if let s = j["sel"] as? [String: Any] { selected = (s["b"] as? Int ?? 0) * 8 + (s["s"] as? Int ?? 0) }
        var out = (0..<64).map { ClipInfo(id: $0) }
        if let banks = j["banks"] as? [[String: Any]] {
            for (b, bk) in banks.prefix(8).enumerated() {
                bankNames[b] = bk["name"] as? String ?? "BANK \(b + 1)"; bankColors[b] = bk["color"] as? String ?? "#4ad4d9"
                if let sl = bk["slots"] as? [Any] { for (i, c) in sl.prefix(8).enumerated() {
                    guard let c = c as? [String: Any] else { continue }
                    var ci = ClipInfo(id: b * 8 + i); ci.present = true; ci.factory = c["factory"] as? Bool ?? false; ci.clipId = c["id"] as? String ?? ""
                    ci.name = c["name"] as? String ?? "CLIP"; ci.color = c["color"] as? String ?? "#4ad4d9"; ci.layer = c["layer"] as? String ?? "WT"
                    ci.bars = c["bars"] as? Int ?? 4; ci.transpose = c["tr"] as? Int ?? 0; ci.noteCount = (c["notes"] as? [Any])?.count ?? 0; ci.laneCount = (c["auto"] as? [String: Any])?.count ?? 0
                    out[ci.id] = ci } }
            }
        }
        slots = out
    }
    private func quantIndex(_ s: String) -> Int { ["imm", "1/16", "1/8", "1/4", "1/2", "1bar", "2bar", "4bar"].firstIndex(of: s) ?? 5 }
    private func poll() {
        guard let e = engine else { return }
        let p = vv_clip_playing_mask(e), qs = vv_clip_queued_start_mask(e), qt = vv_clip_queued_stop_mask(e), rec = Int(vv_clip_recording_slot(e))
        if p != playing { playing = p }; if qs != queuedStart { queuedStart = qs }; if qt != queuedStop { queuedStop = qt }; if rec != recordingSlot { recordingSlot = rec }
        var ph = phases; var changed = false
        for i in 0..<64 where (p >> UInt64(i)) & 1 == 1 || ph[i] >= 0 { let v = vv_clip_phase(e, Int32(i)); if v != ph[i] { ph[i] = v; changed = true } }
        if changed { phases = ph }
        if rec >= 0 || recordWasOn { if vv_clip_drain_recording(e) > 0 || recordWasOn != (rec >= 0) { refresh() }; recordWasOn = rec >= 0 }
    }
    private var recordWasOn = false

    // MARK: transport-facing actions
    func isPlaying(_ s: Int) -> Bool { (playing >> UInt64(s)) & 1 == 1 }
    func isQueuedStart(_ s: Int) -> Bool { (queuedStart >> UInt64(s)) & 1 == 1 }
    func isQueuedStop(_ s: Int) -> Bool { (queuedStop >> UInt64(s)) & 1 == 1 }
    func toggle(_ s: Int) {
        guard let e = engine, slots[s].present else { return }
        if isPlaying(s) { vv_clip_stop(e, Int32(s), 0) } else if isQueuedStart(s) { vv_clip_stop(e, Int32(s), 1) } else { vv_clip_launch(e, Int32(s)); store.transportRunning = true }
    }
    func stopAll() { if let e = engine { vv_clip_stop_all(e) } }
    func toggleRecord(_ s: Int) {
        guard let e = engine, slots[s].present else { say("Select or create a clip first."); return }
        if recordingSlot == s { op("recordEnd"); say("Recording stopped.") }
        else { op("recordBegin", ["slot": s]); vv_clip_launch(e, Int32(s)); store.transportRunning = true; say(mode == 0 ? "Recording (replace): play notes or move dials when the clip starts." : "Recording (overdub): new notes are added to the clip.") }
    }
    func say(_ t: String) { message = t; let m = t; DispatchQueue.main.asyncAfter(deadline: .now() + 5) { [weak self] in if self?.message == m { self?.message = "" } } }

    // MARK: edits (each op is undoable in the core; the replace guard asks first when a slot would be overwritten)
    func select(_ s: Int) { selected = s; op("select", ["slot": s]) }
    func setBank(_ b: Int) { bank = b; op("bank", ["bank": b]) }
    func create(_ s: Int) { op("new", ["slot": s, "layer": LayerID.keys.firstIndex(of: "WT") ?? 0, "bars": 4]) }
    func duplicate(_ s: Int) { if !op("duplicate", ["slot": s]) { say("Bank is full: delete a clip or use another bank.") } }
    func delete(_ s: Int) {
        let nm = slots[s].name
        confirm = PendingConfirm(text: "Delete \"\(nm)\"? (Undo restores it)", actions: [("Delete", true, { [weak self] in self?.op("delete", ["slot": s]) })])
    }
    func rename(_ s: Int, _ n: String) { op("rename", ["slot": s, "name": n]) }
    func setBars(_ s: Int, _ b: Int) { op("bars", ["slot": s, "bars": b]) }
    func setLayer(_ s: Int, _ l: Int) { op("layer", ["slot": s, "layer": l]) }
    private static let palette = ["#4ad4d9", "#e2a04a", "#d94a9e", "#b026ff", "#f0e020", "#6cff7a", "#ff5a5a", "#7aa7ff"]
    func cycleColor(_ s: Int) { let i = Self.palette.firstIndex(of: slots[s].color) ?? -1; op("color", ["slot": s, "color": Self.palette[(i + 1) % Self.palette.count]]) }
    func transpose(_ s: Int, _ d: Int) { op("transpose", ["slot": s, "delta": d]) }
    func resetTranspose(_ s: Int) { op("setTranspose", ["slot": s, "value": 0]) }
    func quantiseNotes(_ s: Int) { op("quantiseNotes", ["slot": s]); say("Notes snapped to 1/16 (Undo restores).") }
    func clearAutomation(_ s: Int) { confirm = PendingConfirm(text: "Remove all automation from \"\(slots[s].name)\"?", actions: [("Remove", true, { [weak self] in self?.op("clearAuto", ["slot": s]) })]) }
    func setQuant(_ q: Int) { quant = q; op("quant", ["q": q]) }
    func setMode(_ m: Int) { mode = m; op("mode", ["m": m]); say(m == 0 ? "Replace: starting a recording clears the clip first (Undo restores)." : "Overdub: recording layers new notes on top of the clip.") }
    func renameBank(_ b: Int, _ n: String) { op("bankName", ["bank": b, "name": n]) }
    func undo() { if !op("undo") { say("Nothing to undo.") } }
    func restoreFactory() {
        let b = bank; let hasAny = (0..<6).contains { slots[b * 8 + $0].present }
        let act = { [weak self] in self?.op("factory", ["bank": b]); self?.say("Factory clips restored.") }
        if hasAny { confirm = PendingConfirm(text: "Restore the factory clips into bank \(b + 1)? Clips in slots 1-6 are replaced (Undo restores).", actions: [("Restore", true, act)]) } else { act() }
    }
    func fromRoll(_ s: Int) {
        let layer = max(0, min(5, slots[s].present ? (LayerID.keys.firstIndex(of: slots[s].layer) ?? layerHint) : layerHint))   // piano-roll bridge: XFX clips use the XFX pattern (Sequencer > Pattern > XFX)
        let go = { [weak self] in if self?.op("fromRoll", ["slot": s, "layer": layer]) == true { self?.say("Clip captured from the piano roll.") } else { self?.say("That layer has no piano-roll notes to capture.") } }
        if slots[s].present && slots[s].noteCount > 0 { confirm = PendingConfirm(text: "Replace the notes of \"\(slots[s].name)\" with the \(LayerID.keys[layer]) piano roll?", actions: [("Replace", true, go)]) } else { go() }
    }
    func toRoll(_ s: Int) {
        guard slots[s].present else { return }
        let go = { [weak self] in if self?.op("toRoll", ["slot": s], refresh: false) == true { self?.store.reload(); self?.say("Clip copied to the \(self?.slots[s].layer ?? "") piano roll.") } }
        confirm = PendingConfirm(text: "Replace the \(slots[s].layer) piano roll with this clip?", actions: [("Replace", true, go)])
    }

    // MARK: drag / drop, copy / paste
    func drop(from: Int, to: Int) {
        guard from != to, slots[from].present else { return }
        if !slots[to].present { op("move", ["from": from, "to": to, "mode": 0]); return }
        confirm = PendingConfirm(text: "\"\(slots[to].name)\" is already there. What should happen?", actions: [
            ("Replace it (move)", true, { [weak self] in self?.op("move", ["from": from, "to": to, "mode": 0]) }),
            ("Swap", false, { [weak self] in self?.op("move", ["from": from, "to": to, "mode": 2]) }),
            ("Replace with a copy", true, { [weak self] in self?.op("move", ["from": from, "to": to, "mode": 1]) })])
    }
    func copyToBank(from: Int, bank b: Int) {
        guard let free = (0..<8).first(where: { !slots[b * 8 + $0].present }) else { say("Bank \(b + 1) is full."); return }
        op("move", ["from": from, "to": b * 8 + free, "mode": 0]); bank = b; op("bank", ["bank": b])
    }
    private var clipboard: String?
    func copy(_ s: Int) { if let t = opText("export", ["slot": s]) { clipboard = t; say("Copied \"\(slots[s].name)\".") } }
    func paste(_ s: Int) {
        guard let t = clipboard else { say("Clipboard is empty."); return }
        let go = { [weak self] in self?.op("paste", ["slot": s, "json": t]) }
        if slots[s].present { confirm = PendingConfirm(text: "Replace \"\(slots[s].name)\" with the copied clip?", actions: [("Replace", true, go)]) } else { _ = go() }
    }

    // MARK: automation recording (dial drags while recording) - the same delta mapping as the web app
    private var autoStartPhase: [String: Bool] = [:]
    func writeAutomation(key: String, delta: Double, bipolar: Bool) {
        guard let e = engine, recordingSlot >= 0 else { return }
        let ph = vv_clip_phase(e, Int32(recordingSlot)); if ph < 0 { return }
        let sv = max(-1, min(1, delta * (bipolar ? 2 : 1)))
        op("autoWrite", ["slot": recordingSlot, "key": key, "phase": Double(ph), "value": 0.5 + 0.5 * sv], refresh: false)
    }
    func endAutomation(key: String) { if recordingSlot >= 0 { op("autoEnd", ["slot": recordingSlot, "key": key]) } }

    // MARK: user library (files in Documents/Clips) + import / export
    static var libraryFolder: URL {
        let u = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("Clips", isDirectory: true)
        try? FileManager.default.createDirectory(at: u, withIntermediateDirectories: true); return u
    }
    func reloadLibrary() {
        let f = (try? FileManager.default.contentsOfDirectory(at: Self.libraryFolder, includingPropertiesForKeys: nil)) ?? []
        library = f.filter { $0.pathExtension == "vvclip" }.sorted { $0.lastPathComponent < $1.lastPathComponent }
    }
    func saveToLibrary(_ s: Int) {
        guard let t = opText("export", ["slot": s]) else { return }
        let base = slots[s].name.replacingOccurrences(of: "[^A-Za-z0-9_-]+", with: "_", options: .regularExpression)
        var url = Self.libraryFolder.appendingPathComponent(base + ".vvclip"); var n = 2
        while FileManager.default.fileExists(atPath: url.path) { url = Self.libraryFolder.appendingPathComponent("\(base)_\(n).vvclip"); n += 1 }
        do { try Data(t.utf8).write(to: url, options: .atomic); reloadLibrary(); say("Saved \"\(slots[s].name)\" to the library.") } catch { say("Could not save the clip: \(error.localizedDescription)") }
    }
    func loadFromLibrary(_ url: URL, into s: Int) { importFile(url, into: s) }
    func importFile(_ url: URL, into s: Int) {
        let scoped = url.startAccessingSecurityScopedResource(); defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        guard let d = try? Data(contentsOf: url), d.count < 20_000_000, let t = String(data: d, encoding: .utf8) else { say("Could not read that file."); return }
        let go = { [weak self] in if self?.op("paste", ["slot": s, "json": t]) == true { self?.say("Loaded the clip.") } else { self?.say("That is not a valid clip file.") } }
        if slots[s].present { confirm = PendingConfirm(text: "Replace \"\(slots[s].name)\" with the clip from \(url.deletingPathExtension().lastPathComponent)?", actions: [("Replace", true, go)]) } else { go() }
    }
    func deleteLibrary(_ url: URL) { try? FileManager.default.removeItem(at: url); reloadLibrary() }
    func exportURL(_ s: Int) -> URL? {
        guard let t = opText("export", ["slot": s]) else { return nil }
        let u = FileManager.default.temporaryDirectory.appendingPathComponent(slots[s].name.replacingOccurrences(of: "[^A-Za-z0-9_-]+", with: "_", options: .regularExpression) + ".vvclip")
        return (try? Data(t.utf8).write(to: u, options: .atomic)) != nil ? u : nil
    }
}

struct ClipLauncherView: View {
    @ObservedObject var s: PresetStore
    @StateObject private var c: ClipStore
    @State private var importing = false
    @State private var nameDraft = ""
    init(s: PresetStore, layer: Int = 0) { self.s = s; self.layer = layer; _c = StateObject(wrappedValue: ClipStore(store: s)) }
    private var sel: ClipInfo { c.slots[c.selected] }
    var layer = 0
    private func col(_ hex: String) -> Color { Color(hex: hex) }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            header
            bankTabs
            grid
            if sel.present { editor } else { Text("Empty slot - double-tap it (or tap NEW) to create a clip.").font(.caption2).foregroundStyle(.secondary) }
            footer
            if !c.message.isEmpty { Text(c.message).font(.caption2).foregroundStyle(Color(hex: "e2a04a")) }
        }
        .alert(c.confirm?.text ?? "", isPresented: Binding(get: { c.confirm != nil }, set: { if !$0 { c.confirm = nil } }), presenting: c.confirm) { p in
            ForEach(Array(p.actions.enumerated()), id: \.offset) { _, a in Button(a.0, role: a.1 ? .destructive : nil) { a.2(); c.confirm = nil } }
            Button("Cancel", role: .cancel) { c.confirm = nil }
        }
        .onAppear { c.layerHint = layer }.onChange(of: layer) { c.layerHint = $0 }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.json, .data]) { r in if case .success(let u) = r { c.importFile(u, into: c.selected) } }
    }

    private var header: some View {
        HStack(spacing: 8) {
            Picker("Launch quantise", selection: Binding(get: { c.quant }, set: { c.setQuant($0) })) { ForEach(0..<8, id: \.self) { Text(ClipStore.quantNames[$0]).tag($0) } }.pickerStyle(.menu).font(.caption)
            Picker("Record mode", selection: Binding(get: { c.mode }, set: { c.setMode($0) })) { Text("Replace").tag(0); Text("Overdub").tag(1) }.pickerStyle(.menu).font(.caption)
            Spacer()
            Button("STOP ALL") { c.stopAll() }.buttonStyle(.bordered).font(.caption2)
            Button("UNDO") { c.undo() }.buttonStyle(.bordered).font(.caption2)
        }
    }
    private var bankTabs: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 4) {
                ForEach(0..<8, id: \.self) { b in
                    let active = (0..<8).contains { c.isPlaying(b * 8 + $0) }
                    Button { c.setBank(b) } label: { HStack(spacing: 4) { Circle().fill(active ? Color.green : Color.gray.opacity(0.4)).frame(width: 6, height: 6); Text(c.bankNames[b]).font(.system(size: 10, design: .monospaced)) } }
                        .buttonStyle(.bordered).tint(col(c.bankColors[b])).opacity(c.bank == b ? 1 : 0.6)
                        .dropDestination(for: String.self) { items, _ in if let f = items.first.flatMap({ Int($0) }) { c.copyToBank(from: f, bank: b); return true }; return false }
                }
            }
        }
    }
    private var grid: some View {
        LazyVGrid(columns: [GridItem(.adaptive(minimum: 78), spacing: 4)], spacing: 4) {
            ForEach(0..<8, id: \.self) { i in cell(c.bank * 8 + i) }
        }
    }
    private func cell(_ slot: Int) -> some View {
        let ci = c.slots[slot], isSel = c.selected == slot, playing = c.isPlaying(slot), queued = c.isQueuedStart(slot) || c.isQueuedStop(slot), rec = c.recordingSlot == slot
        let tint = ci.present ? col(ci.color) : Color.gray
        let ph = c.phases[slot]; let frac = ci.present && ph >= 0 ? Double(ph) / Double(ci.bars * 4) : 0
        return ZStack(alignment: .bottomLeading) {
            RoundedRectangle(cornerRadius: 4).fill(playing ? tint.opacity(0.30) : Color.black.opacity(0.35))
            RoundedRectangle(cornerRadius: 4).strokeBorder(rec ? Color.red : tint.opacity(isSel ? 1 : 0.45), style: StrokeStyle(lineWidth: isSel ? 2 : 1, dash: ci.present ? [] : [3, 2]))
            if ci.present {
                HStack(alignment: .top, spacing: 4) {
                    Button { c.select(slot); c.toggle(slot) } label: { Image(systemName: playing || c.isQueuedStart(slot) ? "stop.fill" : "play.fill").font(.system(size: 10)).foregroundStyle(tint).opacity(queued ? 0.4 : 1) }.buttonStyle(.plain).frame(width: 18, height: 18)
                    VStack(alignment: .leading, spacing: 1) {
                        Text(ci.name + (ci.factory ? " F" : "")).font(.system(size: 9, weight: .bold)).lineLimit(1)
                        Text("\(ci.layer) · \(ci.bars)b" + (ci.transpose != 0 ? " · \(ci.transpose > 0 ? "+" : "")\(ci.transpose)" : "") + (ci.laneCount > 0 ? " · A" : "") + (rec ? " · REC" : "")).font(.system(size: 8)).foregroundStyle(.secondary).lineLimit(1)
                    }
                }.padding(.horizontal, 3).padding(.top, 3).frame(maxHeight: .infinity, alignment: .top)
                GeometryReader { g in Rectangle().fill(tint).frame(width: g.size.width * CGFloat(min(1, max(0, frac))), height: 3).frame(maxHeight: .infinity, alignment: .bottom) }
            } else { Text("+ empty").font(.system(size: 9)).foregroundStyle(.secondary).frame(maxWidth: .infinity, maxHeight: .infinity) }
        }
        .frame(height: 46)
        .contentShape(Rectangle())
        .onTapGesture(count: 2) { if !ci.present { c.create(slot) } }
        .onTapGesture { c.select(slot) }
        .draggable(String(slot))
        .dropDestination(for: String.self) { items, _ in if let f = items.first.flatMap({ Int($0) }) { c.drop(from: f, to: slot); return true }; return false }
        .contextMenu {
            if ci.present {
                Button("Launch / stop") { c.select(slot); c.toggle(slot) }; Button("Record") { c.select(slot); c.toggleRecord(slot) }
                Button("Duplicate") { c.duplicate(slot) }; Button("Copy") { c.copy(slot) }; Button("Delete", role: .destructive) { c.delete(slot) }
            } else { Button("New clip") { c.create(slot) } }
            Button("Paste") { c.paste(slot) }
        }
        .accessibilityLabel(ci.present ? "Clip \(ci.name), \(ci.layer), \(ci.bars) bars" + (playing ? ", playing" : "") : "Empty clip slot")
    }
    private var editor: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 6) {
                Button { c.toggle(c.selected) } label: { Image(systemName: c.isPlaying(c.selected) ? "stop.fill" : "play.fill") }.buttonStyle(.bordered)
                Button { c.toggleRecord(c.selected) } label: { Label("REC", systemImage: "circle.fill").foregroundStyle(.red) }.buttonStyle(.bordered).tint(c.recordingSlot == c.selected ? .red : nil).font(.caption2)
                TextField("Name", text: Binding(get: { sel.name }, set: { nameDraft = $0 }), onCommit: { c.rename(c.selected, nameDraft.isEmpty ? sel.name : nameDraft) }).textFieldStyle(.roundedBorder).font(.caption).frame(maxWidth: 130)
                Picker("Layer", selection: Binding(get: { ClipStore.layerKeys.firstIndex(of: sel.layer) ?? 0 }, set: { c.setLayer(c.selected, $0) })) { ForEach(0..<7, id: \.self) { Text(ClipStore.layerKeys[$0]).tag($0) } }.pickerStyle(.menu).font(.caption)
                Picker("Length", selection: Binding(get: { sel.bars }, set: { c.setBars(c.selected, $0) })) { ForEach(ClipStore.lens + (ClipStore.lens.contains(sel.bars) ? [] : [sel.bars]), id: \.self) { Text("\($0) bar" + ($0 == 1 ? "" : "s")).tag($0) } }.pickerStyle(.menu).font(.caption)
                Button { c.cycleColor(c.selected) } label: { Circle().fill(col(sel.color)).frame(width: 14, height: 14) }.buttonStyle(.plain)
            }
            HStack(spacing: 4) {
                Text("TRANSPOSE").font(.system(size: 9)).foregroundStyle(.secondary)
                Button("-12") { c.transpose(c.selected, -12) }; Button("-1") { c.transpose(c.selected, -1) }
                Text((sel.transpose > 0 ? "+" : "") + "\(sel.transpose) st").font(.caption.monospacedDigit()).frame(minWidth: 44)
                Button("+1") { c.transpose(c.selected, 1) }; Button("+12") { c.transpose(c.selected, 12) }; Button("0") { c.resetTranspose(c.selected) }
            }.buttonStyle(.bordered).font(.caption2)
            HStack(spacing: 4) {
                Button("DUPLICATE") { c.duplicate(c.selected) }; Button("COPY") { c.copy(c.selected) }; Button("PASTE") { c.paste(c.selected) }; Button("DELETE", role: .destructive) { c.delete(c.selected) }
                Button("QUANTISE") { c.quantiseNotes(c.selected) }
            }.buttonStyle(.bordered).font(.caption2)
            HStack(spacing: 4) {
                Button("ROLL → CLIP") { c.fromRoll(c.selected) }; Button("CLIP → ROLL") { c.toRoll(c.selected) }
                Text("AUTO \(sel.laneCount)").font(.system(size: 9)).foregroundStyle(.secondary); Button("CLEAR AUTO") { c.clearAutomation(c.selected) }.disabled(sel.laneCount == 0)
                TextField("Bank name", text: Binding(get: { c.bankNames[c.selected / 8] }, set: { c.renameBank(c.selected / 8, $0) })).textFieldStyle(.roundedBorder).font(.caption).frame(maxWidth: 110)
            }.buttonStyle(.bordered).font(.caption2)
            Text("Move a dial while recording to write it into the clip's automation. It adds to the dial and to DAW automation; it stops with the clip.").font(.system(size: 9)).foregroundStyle(.secondary)
        }
    }
    private var footer: some View {
        HStack(spacing: 6) {
            Button("NEW") { if !sel.present { c.create(c.selected) } else if let f = (0..<8).first(where: { !c.slots[c.bank * 8 + $0].present }) { c.select(c.bank * 8 + f); c.create(c.bank * 8 + f) } else { c.say("Bank is full.") } }
            Button("FACTORY") { c.restoreFactory() }
            Menu("LIBRARY") {
                Button("Save selected clip") { if sel.present { c.saveToLibrary(c.selected) } }.disabled(!sel.present)
                if c.library.isEmpty { Text("No saved clips yet") }
                ForEach(c.library, id: \.self) { u in
                    Menu(u.deletingPathExtension().lastPathComponent) { Button("Load into selected slot") { c.loadFromLibrary(u, into: c.selected) }; Button("Remove from library", role: .destructive) { c.deleteLibrary(u) } }
                }
            }
            Button("IMPORT") { importing = true }
            if sel.present, let u = c.exportURL(c.selected) { ShareLink("EXPORT", item: u) }
        }.buttonStyle(.bordered).font(.caption2)
    }
}
