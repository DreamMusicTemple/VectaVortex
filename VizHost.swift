// LUA Visuals host (UI thread): owns the C++ engine (vv_viz.h), presets (factory files + user presets), per-preset tweaks, and the Generate helper.
// NOTE: written without a Mac. Not compiled yet.
import Foundation
import SwiftUI
import AVFoundation

struct VizParamMeta: Codable, Identifiable { let id: String; let label: String; let group: String; let unit: String; let min: Float; let max: Float; let def: Float; let step: Float; let kind: Int; let band: Int; let depth: Float; let pol: Int; let options: [String] }
struct VizMeta: Codable { let name: String; let category: String; let author: String; let desc: String; let shader: String; let params: [VizParamMeta] }
struct VizAssign: Codable, Equatable { var enabled = false; var band = -1; var depth: Float = 0.5; var pol = 1; var smooth: Float = 0.25; var lo: Float = 0; var hi: Float = 1 }
struct VizDisplay: Codable { var quality = "auto"; var fps = 60; var res = "auto"; var aa = "auto" }
struct VizPreset: Codable, Identifiable { var id: String; var name: String; var src: String; var factory = false; var values: [String: Float] = [:]; var assign: [String: VizAssign] = [:]; var display: VizDisplay?; var cfg: VizCfg?; var basedOn: String?; var format: String? }
struct VizCfg: Codable { var enabled = true, logResponse = true, stereo = false; var sensitivity: Float = 1, gain: Float = 1, smoothing: Float = 0.35, attack: Float = 0.02, release: Float = 0.22, threshold: Float = 0.02; var cross: [Float] = [60, 250, 500, 2000, 4000, 8000]; var user: [Float] = [150, 1800] }

extension VizPreset {
    /// Tolerant decoding so presets exported by the web app (keys: values, assign, display, audioCfg) import too.
    enum CodingKeys: String, CodingKey { case id, name, src, factory, values, assign, display, cfg, audioCfg, basedOn, format }
    init(from d: Decoder) throws {
        let c = try d.container(keyedBy: CodingKeys.self)
        id = try c.decodeIfPresent(String.self, forKey: .id) ?? UUID().uuidString; name = try c.decodeIfPresent(String.self, forKey: .name) ?? "Imported"; src = try c.decode(String.self, forKey: .src)
        factory = false; values = (try? c.decodeIfPresent([String: Float].self, forKey: .values)) ?? [:]; assign = (try? c.decodeIfPresent([String: VizAssign].self, forKey: .assign)) ?? [:]
        display = try? c.decodeIfPresent(VizDisplay.self, forKey: .display); cfg = (try? c.decodeIfPresent(VizCfg.self, forKey: .cfg)) ?? (try? c.decodeIfPresent(VizCfg.self, forKey: .audioCfg)); basedOn = try? c.decodeIfPresent(String.self, forKey: .basedOn); format = try? c.decodeIfPresent(String.self, forKey: .format)
    }
    func encode(to e: Encoder) throws {
        var c = e.container(keyedBy: CodingKeys.self); try c.encode(id, forKey: .id); try c.encode(name, forKey: .name); try c.encode(src, forKey: .src); try c.encode(factory, forKey: .factory); try c.encode(values, forKey: .values); try c.encode(assign, forKey: .assign)
        try c.encodeIfPresent(display, forKey: .display); try c.encodeIfPresent(cfg, forKey: .cfg); try c.encodeIfPresent(basedOn, forKey: .basedOn); try c.encodeIfPresent(format, forKey: .format)
    }
}
let vizBandNames = ["Full", "Sub", "Bass", "Low mids", "Mids", "High mids", "Highs", "Treble", "User range"]

@MainActor
final class VizHost: ObservableObject {
    private(set) var v: OpaquePointer?
    @Published var meta: VizMeta?
    @Published var preset: VizPreset?
    @Published var error: String?
    @Published var errorLine = 0
    @Published var values: [Float] = []
    @Published var assigns: [VizAssign] = []
    @Published var cfg = VizCfg()
    @Published var user: [VizPreset] = []
    @Published var src = ""
    @Published var demo = false
    @Published var halted = false
    private(set) var factory: [VizPreset] = []
    private var tweaks: [String: VizPreset] = [:]
    private var lastGoodSrc = ""
    var allPresets: [VizPreset] { factory + user }

    init() {
        v = vv_viz_create()
        factory = VizHost.loadFactory()
        if let d = UserDefaults.standard.data(forKey: "vv.viz.user"), let u = try? JSONDecoder().decode([VizPreset].self, from: d) { user = u }
        if let d = UserDefaults.standard.data(forKey: "vv.viz.tweaks"), let t = try? JSONDecoder().decode([String: VizPreset].self, from: d) { tweaks = t }
        if let d = UserDefaults.standard.data(forKey: "vv.viz.cfg"), let c = try? JSONDecoder().decode(VizCfg.self, from: d) { cfg = c }
        pushCfg()
        load(UserDefaults.standard.string(forKey: "vv.viz.last") ?? "02_fractal_tunnel")
    }
    deinit { if let v = v { vv_viz_destroy(v) } }

    static func loadFactory() -> [VizPreset] {
        guard let urls = Bundle.main.urls(forResourcesWithExtension: "lua", subdirectory: "factory") else { return [] }
        let names: [String: String] = ["01_audio_particles": "Audio Particles", "02_fractal_tunnel": "Fractal Tunnel", "03_infinite_tunnel": "Infinite Tunnel", "04_liquid_warp": "Liquid Warp", "05_chromatic_dream": "Chromatic Dream", "06_fractal_bloom": "Fractal Bloom", "07_neural_network": "Neural Network", "08_geometric_pulse": "Geometric Pulse", "09_spiral_dimension": "Spiral Dimension", "10_plasma_field": "Plasma Field", "11_cyber_grid": "Cyber Grid", "12_vortex": "Vortex", "13_kaleidoscope": "Kaleidoscope", "14_synthetic_universe": "Synthetic Universe", "15_spectral_waves": "Spectral Waves", "16_hyperspace": "Hyperspace", "17_cymatics_liquid": "Cymatics - Liquid Resonance", "18_cymatics_sand": "Cymatics - Resonant Sand", "19_wormhole_descent": "Wormhole Descent", "20_neon_subway": "Neon Subway", "21_crystal_throat": "Crystal Throat", "22_hyperloop_ribs": "Hyperloop Ribs", "23_acid_kaleido": "Acid Kaleido", "24_melting_plasma": "Melting Plasma", "25_mushroom_vortex": "Mushroom Vortex", "26_liquid_prism_dream": "Liquid Prism Dream"]
        return urls.sorted { $0.lastPathComponent < $1.lastPathComponent }.compactMap { u in
            guard let s = try? String(contentsOf: u, encoding: .utf8) else { return nil }
            let id = u.deletingPathExtension().lastPathComponent; return VizPreset(id: id, name: names[id] ?? id, src: s, factory: true) }
    }

    // MARK: script
    /// Compile `s`; on failure the previous script keeps running and `error` explains.
    @discardableResult func apply(source s: String, keepValues: Bool = false) -> Bool {
        guard let v = v else { return false }
        if vv_viz_load(v, s) == 1 {
            src = s; lastGoodSrc = s; error = nil; errorLine = 0; halted = false; refreshMeta(); return true
        }
        var e = [CChar](repeating: 0, count: 240); var l: Int32 = 0, c: Int32 = 0; _ = vv_viz_check(s, &e, 240, &l, &c)
        error = String(cString: e).isEmpty ? String(cString: vv_viz_error(v)) : String(cString: e); errorLine = Int(l); return false
    }
    func check(_ s: String) -> String? { var e = [CChar](repeating: 0, count: 240); var l: Int32 = 0, c: Int32 = 0; if vv_viz_check(s, &e, 240, &l, &c) == 1 { return nil }; return "line \(l): " + String(cString: e) }
    private func refreshMeta() {
        guard let v = v, let d = String(cString: vv_viz_meta_json(v)).data(using: .utf8) else { return }
        meta = try? JSONDecoder().decode(VizMeta.self, from: d)
        syncFromEngine()
    }
    func syncFromEngine() {
        guard let v = v, let m = meta else { return }
        values = m.params.indices.map { vv_viz_get_param(v, Int32($0)) }
        assigns = m.params.indices.map { i in var en: Int32 = 0, b: Int32 = 0, p: Int32 = 0; var d: Float = 0, s: Float = 0, lo: Float = 0, hi: Float = 0; vv_viz_get_assign(v, Int32(i), &en, &b, &d, &p, &s, &lo, &hi); return VizAssign(enabled: en != 0, band: Int(b), depth: d, pol: Int(p), smooth: s, lo: lo, hi: hi) }
    }

    // MARK: presets
    func find(_ id: String) -> VizPreset? { allPresets.first { $0.id == id } }
    func load(_ id: String) {
        guard let p = find(id) else { return }
        if apply(source: p.src) {
            preset = p; UserDefaults.standard.set(id, forKey: "vv.viz.last")
            if let t = p.factory ? tweaks[id] : p { restore(t) }
            if !p.factory, let c = p.cfg { cfg = c; pushCfg() }
        }
    }
    private func restore(_ t: VizPreset) {
        guard let v = v, let m = meta else { return }
        for (i, pm) in m.params.enumerated() {
            if let x = t.values[pm.id] { vv_viz_set_param(v, Int32(i), x) }
            if let a = t.assign[pm.id] { vv_viz_set_assign(v, Int32(i), a.enabled ? 1 : 0, Int32(a.band), a.depth, Int32(a.pol), a.smooth, a.lo, a.hi) }
        }
        syncFromEngine()
    }
    func snapshot(name: String, id: String? = nil) -> VizPreset {
        var r = VizPreset(id: id ?? "user_" + String(Int(Date().timeIntervalSince1970 * 1000), radix: 36) + String(Int.random(in: 0..<9999), radix: 36), name: name, src: src)
        if let m = meta { for (i, p) in m.params.enumerated() where i < values.count && i < assigns.count { r.values[p.id] = values[i]; r.assign[p.id] = assigns[i] } }
        r.cfg = cfg; r.basedOn = preset.map { $0.basedOn ?? $0.id }; r.format = "vektur-lua-visual-preset"; return r
    }
    func saveTweaksSoon() { guard let p = preset, p.factory else { return }; tweaks[p.id] = snapshot(name: p.name, id: p.id); if let d = try? JSONEncoder().encode(tweaks) { UserDefaults.standard.set(d, forKey: "vv.viz.tweaks") } }
    private func persistUser() { if let d = try? JSONEncoder().encode(user) { UserDefaults.standard.set(d, forKey: "vv.viz.user") } }
    func save(asNew: Bool, name: String? = nil, display: VizDisplay) {
        if let p = preset, !p.factory, !asNew, let i = user.firstIndex(where: { $0.id == p.id }) { var r = snapshot(name: p.name, id: p.id); r.display = display; user[i] = r; preset = r; persistUser(); return }
        var r = snapshot(name: name ?? ((preset?.name ?? "Preset") + " copy")); r.display = display; user.append(r); preset = r; persistUser(); UserDefaults.standard.set(r.id, forKey: "vv.viz.last")
    }
    func duplicate(name: String, display: VizDisplay) { save(asNew: true, name: name, display: display) }
    func rename(_ name: String) { guard let p = preset, !p.factory, let i = user.firstIndex(where: { $0.id == p.id }) else { return }; user[i].name = name; preset = user[i]; persistUser() }
    func deleteCurrent() { guard let p = preset, !p.factory else { return }; user.removeAll { $0.id == p.id }; persistUser(); load(p.basedOn.flatMap { find($0) != nil ? $0 : nil } ?? "02_fractal_tunnel") }
    func exportData() -> Data? { guard let p = preset else { return nil }; var r = snapshot(name: p.name); r.id = p.id; let e = JSONEncoder(); e.outputFormatting = [.prettyPrinted, .sortedKeys]; return try? e.encode(r) }
    /// Accepts a .vizpreset.json (or plain Lua source) and adds it to the user presets.
    @discardableResult func importData(_ d: Data, fileName: String) -> String? {
        if var r = try? JSONDecoder().decode(VizPreset.self, from: d), !r.src.isEmpty {
            if let e = check(r.src) { return "Import failed: " + e }; r.id = "user_" + String(Int(Date().timeIntervalSince1970 * 1000), radix: 36); r.factory = false; user.append(r); persistUser(); load(r.id); return nil }
        guard let s = String(data: d, encoding: .utf8) else { return "Import failed: not text" }
        if let e = check(s) { return "Import failed: " + e }
        let r = VizPreset(id: "user_" + String(Int(Date().timeIntervalSince1970 * 1000), radix: 36), name: (fileName as NSString).deletingPathExtension, src: s); user.append(r); persistUser(); load(r.id); return nil
    }
    func restoreFactory() { tweaks = [:]; UserDefaults.standard.removeObject(forKey: "vv.viz.tweaks"); load(preset?.factory == true ? preset!.id : "02_fractal_tunnel") }

    // MARK: parameters
    func set(_ i: Int, _ x: Float) { guard let v = v, i < values.count else { return }; vv_viz_set_param(v, Int32(i), x); values[i] = x; saveTweaksSoon() }
    func setAssign(_ i: Int, _ a: VizAssign) { guard let v = v, i < assigns.count else { return }; vv_viz_set_assign(v, Int32(i), a.enabled ? 1 : 0, Int32(a.band), a.depth, Int32(a.pol), a.smooth, a.lo, a.hi); assigns[i] = a; saveTweaksSoon() }
    func resetParams() { guard let v = v else { return }; vv_viz_reset_params(v); syncFromEngine(); saveTweaksSoon() }
    func randomiseValues() { guard let v = v else { return }; vv_viz_randomise_values(v); syncFromEngine(); saveTweaksSoon() }
    func randomiseAssign() { guard let v = v else { return }; vv_viz_randomise_assign(v); syncFromEngine(); saveTweaksSoon() }
    func clearAssign() { guard let v = v else { return }; vv_viz_clear_assign(v); syncFromEngine(); saveTweaksSoon() }
    func pushCfg() {
        guard let v = v else { return }
        var c = VVVizCfg(); c.enabled = cfg.enabled ? 1 : 0; c.logResponse = cfg.logResponse ? 1 : 0; c.stereo = cfg.stereo ? 1 : 0; c.sensitivity = cfg.sensitivity; c.gain = cfg.gain; c.smoothing = cfg.smoothing; c.attack = cfg.attack; c.release = cfg.release; c.threshold = cfg.threshold
        c.cross = (cfg.cross[0], cfg.cross[1], cfg.cross[2], cfg.cross[3], cfg.cross[4], cfg.cross[5]); c.user = (cfg.user[0], cfg.user[1]); vv_viz_set_cfg(v, &c)
        if let d = try? JSONEncoder().encode(cfg) { UserDefaults.standard.set(d, forKey: "vv.viz.cfg") }
    }

    // MARK: Generate (offline, template based: picks the closest factory algorithm and tunes its parameters from the words)
    func generate(_ prompt: String) -> String? {
        let t = prompt.lowercased(); func has(_ p: String) -> Bool { t.range(of: p, options: .regularExpression) != nil }
        var id = VizGenData.rules.first { has($0.pattern) }?.id
        if id == nil { var h: UInt32 = 0; for u in t.unicodeScalars { h = h &* 31 &+ UInt32(u.value) }; id = factory[Int(h) % max(1, factory.count)].id }
        guard let base = find(id!), apply(source: base.src) else { return error ?? "could not load the base preset" }
        guard let m = meta else { return "no metadata" }
        func tweak(_ k: String, _ f: (Float, VizParamMeta) -> Float) { if let i = m.params.firstIndex(where: { $0.id == k }) { set(i, max(m.params[i].min, min(m.params[i].max, f(values[i], m.params[i])))) } }
        resetParams()
        if let c = VizGenData.colours.first(where: { has($0.pattern) }) { tweak("palette") { _, _ in c.hue }; tweak("hue") { _, _ in (c.hue - 0.5) * 0.5 } }
        if has("calm|slow|ambient|gentle|peaceful|drone|relax") { for k in ["speed", "flow", "orbitSpeed", "fly", "spin"] { tweak(k) { v, _ in v * 0.5 } }; tweak("bloom") { v, _ in v * 1.2 }; tweak("viscosity") { v, _ in min(1, v * 1.4) } }
        if has("fast|intense|aggressive|energetic|hard|frantic|wild") { for k in ["speed", "flow", "orbitSpeed", "fly", "spin"] { tweak(k) { v, _ in v * 1.7 } }; tweak("chroma") { v, _ in v * 1.6 }; tweak("grain") { v, _ in v * 1.5 } }
        if has("dark|moody|noir|black") { tweak("brightness") { v, _ in v * 0.8 }; tweak("vignette") { v, _ in v * 1.6 }; tweak("bloom") { v, _ in v * 0.8 } }
        if has("bright|vivid|vibrant|glow|neon") { tweak("bloom") { v, _ in v * 1.5 }; tweak("saturation") { v, _ in v * 1.2 } }
        if has("dense|detailed|complex|intricate") { for k in ["detail", "complexity", "layers", "nodes", "count", "density", "rows"] { tweak(k) { v, p in v + (p.max - v) * 0.5 } } }
        if has("simple|minimal|sparse|clean") { for k in ["detail", "complexity", "layers", "nodes", "count", "density", "rows"] { tweak(k) { v, p in v - (v - p.min) * 0.4 } } }
        var r = snapshot(name: String(("Generated: " + prompt).prefix(48))); r.basedOn = base.id; user.append(r); persistUser(); preset = r; return nil
    }
}
