// IR convolution reverb panel for the effects-rack "ir" slot: factory IRs by category, user import (WAV / AIFF), audition, replace, remove,
// every control of the web slot, and an IR envelope with start / end / early-late markers. NOT COMPILED YET - see docs/MAC_DAY_ONE.md.
// The IR library is process-wide in the C core (vv_ir_*). Imported files are copied into a shared "IRs" folder and re-scanned at launch, so the app and
// the AUv3 extension (which share the App Group container when one is configured) both find a preset's user IR.
import SwiftUI
import AVFoundation
import UniformTypeIdentifiers

/// Owns the IR folder and the cached lists. Call `IRStore.shared.scan()` at launch and in the AUv3 extension's init.
final class IRStore: ObservableObject {
    static let shared = IRStore()
    struct UserIR: Identifiable, Hashable { let id: String; let name: String; let seconds: Double }
    struct Factory: Identifiable, Hashable { let id: String; let name: String; let category: String }
    @Published private(set) var user: [UserIR] = []
    @Published private(set) var missing: [String] = []
    @Published var message: String = ""
    let factory: [Factory]
    let categories: [String]
    private var player: AVAudioPlayer?

    /// App Group container when the entitlement exists (so the AUv3 sees the app's imports), otherwise the app's Documents folder.
    var folder: URL {
        let fm = FileManager.default
        let base = fm.containerURL(forSecurityApplicationGroupIdentifier: IRStore.appGroup) ?? fm.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let d = base.appendingPathComponent("IRs", isDirectory: true); try? fm.createDirectory(at: d, withIntermediateDirectories: true); return d
    }
    static var appGroup = "group.com.vektur.vortex"

    init() {
        factory = (0..<Int(vv_ir_factory_count())).map { Factory(id: String(cString: vv_ir_factory_id(Int32($0))), name: String(cString: vv_ir_factory_name(Int32($0))), category: String(cString: vv_ir_factory_category(Int32($0)))) }
        categories = (0..<Int(vv_ir_category_count())).map { String(cString: vv_ir_category_name(Int32($0))) }
        scan()
    }
    private func json(_ f: (UnsafeMutablePointer<CChar>?, Int) -> Int) -> [[String: Any]] {
        var cap = 1 << 14
        while true {
            var buf = [CChar](repeating: 0, count: cap); let n = buf.withUnsafeMutableBufferPointer { f($0.baseAddress, cap) }
            if n < cap { return ((try? JSONSerialization.jsonObject(with: Data(bytes: buf, count: n))) as? [[String: Any]]) ?? [] }
            cap = n + 1
        }
    }
    func scan() {
        _ = vv_ir_scan_dir(folder.path); refresh()
    }
    func refresh() {
        user = json { vv_ir_user_json($0, $1) }.compactMap { d in (d["id"] as? String).map { UserIR(id: $0, name: d["name"] as? String ?? $0, seconds: (d["seconds"] as? NSNumber)?.doubleValue ?? 0) } }
        let m = json { vv_ir_missing_json($0, $1) }; missing = m.compactMap { $0["id"] as? String }
    }
    /// Copies the picked file into the IR folder first (security-scoped URLs do not outlive the picker), then imports it. Returns the new id.
    @discardableResult func importFile(_ url: URL, replacing: String? = nil) -> String? {
        let access = url.startAccessingSecurityScopedResource(); defer { if access { url.stopAccessingSecurityScopedResource() } }
        let tmp = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString + "." + url.pathExtension)
        do { try FileManager.default.copyItem(at: url, to: tmp) } catch { message = "Could not read the file: \(error.localizedDescription)"; return nil }
        defer { try? FileManager.default.removeItem(at: tmp) }
        var id = [CChar](repeating: 0, count: 64), msg = [CChar](repeating: 0, count: 256)
        let name = url.deletingPathExtension().lastPathComponent
        let ok = vv_ir_import_file(tmp.path, name, replacing ?? "", &id, 64, &msg, 256)
        if ok == 0 { message = String(cString: msg); return nil }
        let newId = String(cString: id)
        // keep a persistent copy named <id>__<name>.<ext> (what scanDir expects) so the IR survives a relaunch and reaches the AUv3
        let dest = folder.appendingPathComponent("\(newId.replacingOccurrences(of: "u:", with: ""))__\(name).\(url.pathExtension.lowercased())")
        try? FileManager.default.removeItem(at: dest); try? FileManager.default.copyItem(at: tmp, to: dest)
        if let r = replacing, r != newId { remove(r) }
        message = String(cString: msg); refresh(); return newId
    }
    func remove(_ id: String) {
        _ = vv_ir_remove(id)
        let hex = id.replacingOccurrences(of: "u:", with: "")
        if let files = try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil) { for f in files where f.lastPathComponent.hasPrefix(hex + "__") { try? FileManager.default.removeItem(at: f) } }
        refresh()
    }
    func envelope(_ id: String, bins: Int) -> (values: [Float], seconds: Double) {
        var v = [Float](repeating: 0, count: bins); let sec = vv_ir_envelope(id, &v, Int32(bins), 48000); return (v, sec)
    }
    func autoTrim(_ id: String) -> (Double, Double) { var a = 0.0, b = 1.0; _ = vv_ir_auto_trim(id, 48000, &a, &b); return (a, b) }
    /// Plays a burst through the stage's shaped IR (rendered offline in the core, no audio-thread work).
    func audition(paramsJSON: String) {
        let cap = 48000 * 8; var l = [Float](repeating: 0, count: cap), r = [Float](repeating: 0, count: cap)
        let n = Int(vv_ir_audition(paramsJSON, &l, &r, Int32(cap), 48000)); if n <= 0 { message = "Could not audition this IR."; return }
        var pk: Float = 0.001; for i in 0..<n { pk = max(pk, abs(l[i]), abs(r[i])) }
        let g = min(1, 0.8 / pk), bytes = n * 4; var d = Data(); func u32(_ v: UInt32) { var x = v.littleEndian; d.append(Data(bytes: &x, count: 4)) }; func u16(_ v: UInt16) { var x = v.littleEndian; d.append(Data(bytes: &x, count: 2)) }
        d.append("RIFF".data(using: .ascii)!); u32(UInt32(36 + bytes)); d.append("WAVEfmt ".data(using: .ascii)!); u32(16); u16(1); u16(2); u32(48000); u32(48000 * 4); u16(4); u16(16); d.append("data".data(using: .ascii)!); u32(UInt32(bytes))
        for i in 0..<n { for s in [l[i], r[i]] { u16(UInt16(bitPattern: Int16(max(-1, min(1, s * g)) * 32767))) } }
        player = try? AVAudioPlayer(data: d); player?.play()
    }
}

struct IRReverbPanel: View {
    @ObservedObject var s: PresetStore
    let base: String
    @ObservedObject private var store = IRStore.shared
    @State private var importing = false
    @State private var replacing = false
    @State private var confirmRemove = false
    @State private var showAdvanced = false
    @State private var category = "Halls"

    private var irId: String { s.string("\(base).irId", "f:hall-concert") }
    private var isUser: Bool { irId.hasPrefix("u:") }
    private func sl(_ label: String, _ key: String, _ r: ClosedRange<Double>, _ def: Double = 0, fmt: String = "%.2f", log: Bool = false, unit: String = "") -> some View {
        SliderRow(s: s, label: label, path: "\(base).\(key)", range: r, def: def, fmt: fmt, log: log, unit: unit)
    }
    private var paramsJSON: String {
        var d: [String: Any] = ["irId": irId]
        for k in ["start", "end", "lenPct", "earlyLate", "damping", "irGain"] { d[k] = s.double("\(base).\(k)", k == "end" ? 1 : k == "lenPct" ? 100 : 0) }
        d["reverse"] = s.bool("\(base).reverse"); d["normalise"] = s.bool("\(base).normalise", true)
        return (try? String(data: JSONSerialization.data(withJSONObject: d), encoding: .utf8)) ?? "{}"
    }
    private func pick(_ id: String, _ name: String) { s.set("\(base).irId", id); s.set("\(base).irName", name) }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            if store.missing.contains(irId) || (isUser && !store.user.contains { $0.id == irId }) {
                Label("The impulse response \"\(s.string("\(base).irName", irId))\" is missing. The default hall is playing instead - import the file again (REPLACE) or choose another IR.", systemImage: "exclamationmark.triangle.fill")
                    .font(.caption).foregroundStyle(.orange)
            }
            HStack {
                Text("Category").font(.caption); Spacer()
                Picker("Category", selection: $category) { ForEach(store.categories + ["Imported"], id: \.self) { Text($0).tag($0) } }.pickerStyle(.menu).labelsHidden()
            }
            HStack {
                Text("Impulse response").font(.caption); Spacer()
                Picker("IR", selection: Binding(get: { irId }, set: { id in pick(id, store.factory.first { $0.id == id }?.name ?? store.user.first { $0.id == id }?.name ?? id) })) {
                    if category == "Imported" { ForEach(store.user) { Text($0.name).tag($0.id) } }
                    else { ForEach(store.factory.filter { $0.category == category }) { Text($0.name).tag($0.id) } }
                    if !irId.isEmpty { Text(s.string("\(base).irName", irId)).tag(irId) }
                }.pickerStyle(.menu).labelsHidden()
            }
            IREnvelopeView(s: s, base: base, irId: irId)
            HStack {
                Button("Audition") { store.audition(paramsJSON: paramsJSON) }.buttonStyle(.bordered)
                Button("Import…") { importing = true }.buttonStyle(.bordered)
                if isUser {
                    Button("Replace…") { replacing = true }.buttonStyle(.bordered)
                    Button("Remove", role: .destructive) { confirmRemove = true }.buttonStyle(.bordered)
                }
                Button("Trim silence") { let t = store.autoTrim(irId); s.set("\(base).start", t.0); s.set("\(base).end", t.1) }.buttonStyle(.bordered)
            }.font(.caption)
            if !store.message.isEmpty { Text(store.message).font(.caption2).foregroundStyle(.secondary) }
            sl("Wet / dry", "mix", 0...1, 0.3); sl("Pre-delay", "predelay", 0...500, 0, fmt: "%.0f", unit: " ms"); sl("IR length", "lenPct", 1...100, 100, fmt: "%.0f", unit: " %")
            sl("Early / late", "earlyLate", -1...1, 0); sl("Input level", "inLevel", -24...12, 0, fmt: "%.1f", unit: " dB"); sl("Output level", "outLevel", -24...12, 0, fmt: "%.1f", unit: " dB")
            sl("Low cut", "lowCut", 20...2000, 20, fmt: "%.0f", log: true, unit: " Hz"); sl("High cut", "highCut", 1000...20000, 20000, fmt: "%.0f", log: true, unit: " Hz")
            sl("Damping", "damping", 0...1, 0); sl("Tone", "tone", -1...1, 0); sl("Stereo width", "width", 0...2, 1); sl("L / R balance", "balance", -1...1, 0)
            DisclosureGroup("IR editing", isExpanded: $showAdvanced) {
                sl("IR start", "start", 0...0.98, 0, fmt: "%.3f"); sl("IR end", "end", 0.02...1, 1, fmt: "%.3f"); sl("IR gain", "irGain", -24...24, 0, fmt: "%.1f", unit: " dB")
                ToggleRow(s: s, label: "Normalise", path: "\(base).normalise", def: true); ToggleRow(s: s, label: "Reverse", path: "\(base).reverse")
            }.font(.caption)
        }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.wav, .aiff, .audio]) { if case .success(let u) = $0, let id = store.importFile(u) { pick(id, u.deletingPathExtension().lastPathComponent); category = "Imported" } }
        .fileImporter(isPresented: $replacing, allowedContentTypes: [.wav, .aiff, .audio]) { if case .success(let u) = $0, let id = store.importFile(u, replacing: irId) { pick(id, u.deletingPathExtension().lastPathComponent) } }
        .confirmationDialog("Remove this imported IR from the library?", isPresented: $confirmRemove, titleVisibility: .visible) {
            Button("Remove", role: .destructive) { let old = irId; pick("f:hall-concert", "Concert Hall"); store.remove(old) }
        }
        .onAppear { if isUser { category = "Imported" } else if let f = store.factory.first(where: { $0.id == irId }) { category = f.category } }
    }
}

/// IR envelope (dB, last 70 dB) with start / end markers; the dim region is trimmed away.
struct IREnvelopeView: View {
    @ObservedObject var s: PresetStore
    let base: String; let irId: String
    var body: some View {
        let env = IRStore.shared.envelope(irId, bins: 160)
        let st = s.double("\(base).start", 0), en = s.double("\(base).end", 1), el = s.double("\(base).earlyLate", 0)
        Canvas { ctx, size in
            ctx.fill(Path(roundedRect: CGRect(origin: .zero, size: size), cornerRadius: 4), with: .color(.black.opacity(0.6)))
            let n = env.values.count; let w = size.width / CGFloat(n)
            for i in 0..<n {
                let h = CGFloat(env.values[i]) * (size.height - 6); let x = CGFloat(i) * w, t = Double(i) / Double(n)
                ctx.fill(Path(CGRect(x: x, y: size.height - h, width: max(1, w - 0.5), height: h)), with: .color(vvCyan.opacity(t >= st && t <= en ? 0.9 : 0.25)))
            }
            for t in [st, en] { var p = Path(); p.move(to: CGPoint(x: size.width * CGFloat(t), y: 0)); p.addLine(to: CGPoint(x: size.width * CGFloat(t), y: size.height)); ctx.stroke(p, with: .color(.orange), lineWidth: 1.5) }
            if el != 0 { let x = size.width * CGFloat(st + (en - st) * 0.25); var p = Path(); p.move(to: CGPoint(x: x, y: 0)); p.addLine(to: CGPoint(x: x, y: size.height)); ctx.stroke(p, with: .color(.white.opacity(0.4)), style: StrokeStyle(lineWidth: 1, dash: [3, 3])) }
            ctx.draw(Text(String(format: "%.2f s", env.seconds)).font(.system(size: 9, design: .monospaced)).foregroundColor(.gray), at: CGPoint(x: size.width - 24, y: 9))
        }.frame(height: 80)
    }
}
