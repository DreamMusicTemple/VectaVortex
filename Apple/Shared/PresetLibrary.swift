// Preset library model shared by the browser: artist bank -> category (web: Artist folders with fixed category subfolders).
// Factory files are "Artist - Category - Name.json" (or plain names, filed under "Factory"); user files live in
// <Documents>/Presets/<Artist>/[<Category>/]<name>.json and loose files belong to "My presets".
import Foundation

let presetCategories = ["Arp", "Bass", "Key", "Lead", "Pad", "Drum", "FX", "XFX", "Seq", "Plucked", "Misc"]

struct PresetEntry: Identifiable {
    var id: String { url.path }
    let artist: String, category: String, name: String, url: URL, factory: Bool
}

enum PresetLibrary {
    static func inferCategory(_ s: String) -> String? {
        if let c = presetCategories.first(where: { $0.caseInsensitiveCompare(s) == .orderedSame }) { return c }
        let w = s.split(separator: " ").first.map(String.init) ?? ""
        return presetCategories.first { $0.caseInsensitiveCompare(w) == .orderedSame }
    }
    static func scan() -> [PresetEntry] {
        var out: [PresetEntry] = []
        let fm = FileManager.default
        let b = Bundle(for: VectaAudioUnit.self)
        let fac = (b.url(forResource: "Presets", withExtension: nil)).flatMap { try? fm.contentsOfDirectory(at: $0, includingPropertiesForKeys: nil) } ?? []
        for u in fac where u.pathExtension == "json" {
            let n = u.deletingPathExtension().lastPathComponent
            let parts = n.components(separatedBy: " - ")
            if parts.count >= 3, presetCategories.contains(parts[1]) { out.append(PresetEntry(artist: parts[0], category: parts[1], name: parts.dropFirst(2).joined(separator: " - "), url: u, factory: true)) }
            else { out.append(PresetEntry(artist: "Factory", category: inferCategory(n) ?? "Misc", name: n, url: u, factory: true)) }
        }
        let root = PresetStore.userFolder
        if let en = fm.enumerator(at: root, includingPropertiesForKeys: nil) {
            for case let u as URL in en where u.pathExtension == "json" {
                let rel = u.path.replacingOccurrences(of: root.path + "/", with: "").components(separatedBy: "/")
                let n = u.deletingPathExtension().lastPathComponent.replacingOccurrences(of: ".vvpreset", with: "")
                let artist = rel.count >= 2 ? rel[0] : "My presets"
                let cat = rel.count >= 3 ? rel[1] : (inferCategory(n) ?? "Misc")
                out.append(PresetEntry(artist: artist, category: cat, name: n, url: u, factory: false))
            }
        }
        return out.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }
    static func artists(_ e: [PresetEntry]) -> [String] {
        var a = Set(e.map { $0.artist })
        if let d = try? FileManager.default.contentsOfDirectory(at: PresetStore.userFolder, includingPropertiesForKeys: [.isDirectoryKey]) {
            for u in d where (try? u.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true { a.insert(u.lastPathComponent) }
        }
        return a.sorted { $0.localizedCaseInsensitiveCompare($1) == .orderedAscending }
    }
    static func isUserArtist(_ a: String) -> Bool {
        var dir: ObjCBool = false
        return a != "Factory" && a != "My presets" && FileManager.default.fileExists(atPath: PresetStore.userFolder.appendingPathComponent(a).path, isDirectory: &dir) && dir.boolValue
    }
    static func clean(_ s: String) -> String { s.trimmingCharacters(in: .whitespacesAndNewlines).replacingOccurrences(of: "/", with: "-").replacingOccurrences(of: ":", with: "-") }

    // MARK: .zip export (stored, no compression; iOS has no zip API)
    static func crc32(_ d: Data) -> UInt32 {
        var table = [UInt32](repeating: 0, count: 256)
        for i in 0..<256 { var c = UInt32(i); for _ in 0..<8 { c = (c & 1) != 0 ? 0xEDB88320 ^ (c >> 1) : c >> 1 }; table[i] = c }
        var crc: UInt32 = 0xFFFFFFFF
        for b in d { crc = table[Int((crc ^ UInt32(b)) & 0xFF)] ^ (crc >> 8) }
        return crc ^ 0xFFFFFFFF
    }
    static func zip(_ files: [(path: String, data: Data)]) -> Data {
        var out = Data(), central = Data()
        func le16(_ v: Int) -> [UInt8] { [UInt8(v & 0xFF), UInt8((v >> 8) & 0xFF)] }
        func le32(_ v: UInt32) -> [UInt8] { [UInt8(v & 0xFF), UInt8((v >> 8) & 0xFF), UInt8((v >> 16) & 0xFF), UInt8((v >> 24) & 0xFF)] }
        for f in files {
            let name = Data(f.path.utf8), crc = crc32(f.data), offset = UInt32(out.count)
            var h = Data([0x50, 0x4B, 0x03, 0x04]); h.append(contentsOf: le16(20) + le16(0x0800) + le16(0) + le16(0) + le16(0x21) + le32(crc) + le32(UInt32(f.data.count)) + le32(UInt32(f.data.count)) + le16(name.count) + le16(0))
            out.append(h); out.append(name); out.append(f.data)
            var c = Data([0x50, 0x4B, 0x01, 0x02]); c.append(contentsOf: le16(20) + le16(20) + le16(0x0800) + le16(0) + le16(0) + le16(0x21) + le32(crc) + le32(UInt32(f.data.count)) + le32(UInt32(f.data.count)) + le16(name.count) + le16(0) + le16(0) + le16(0) + le16(0) + le32(0) + le32(offset))
            central.append(c); central.append(name)
        }
        let cdOffset = UInt32(out.count); out.append(central)
        var e = Data([0x50, 0x4B, 0x05, 0x06]); e.append(contentsOf: le16(0) + le16(0) + le16(files.count) + le16(files.count) + le32(UInt32(central.count)) + le32(cdOffset) + le16(0)); out.append(e)
        return out
    }
}
