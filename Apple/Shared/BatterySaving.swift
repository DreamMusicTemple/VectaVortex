// Battery Saving Mode (device setting, not part of any preset). Persisted in UserDefaults (the shared App Group suite when the app and its AUv3 extension share one, else standard),
// handed to the C++ engine as one atomic word (vv_engine_set_power: no locks or allocation on the audio thread) and used to slow visual refresh.
// Optional adaptation: when "Adapt to Low Power Mode and heat" is on, Low Power Mode or a serious/critical thermal state steps the EFFECTIVE profile up to at least Balanced
// (or Light when the mode is off). The user's own choices are never rewritten; the Settings screen shows what is active and why. System state is observed through
// notifications, never polled. Untested (Swift is never compiled in this environment).
import SwiftUI
import Combine

public final class BatterySaving: ObservableObject {
    public static let shared = BatterySaving()
    public static var suiteName: String? = nil   // set to the App Group id at launch (app and AUv3) to share the choice between them
    private var defaults: UserDefaults { (Self.suiteName.flatMap { UserDefaults(suiteName: $0) }) ?? .standard }
    private weak var holder: AnyObject?; private var engineProvider: (() -> OpaquePointer?)?

    @Published public var on: Bool { didSet { save("on", on); apply() } }
    @Published public var profile: Int { didSet { save("profile", profile); apply() } }   // 1 Light, 2 Balanced (recommended), 3 Maximum Efficiency
    @Published public var voices: Bool { didSet { save("voices", voices); apply() } }
    @Published public var dsp: Bool { didSet { save("dsp", dsp); apply() } }
    @Published public var fx: Bool { didSet { save("fx", fx); apply() } }
    @Published public var mod: Bool { didSet { save("mod", mod); apply() } }
    @Published public var vis: Bool { didSet { save("vis", vis); apply() } }
    @Published public var adaptive: Bool { didSet { save("adaptive", adaptive); apply() } }
    @Published public private(set) var uiDivider = 1
    @Published public private(set) var reason = ""

    private init() {
        let d = (Self.suiteName.flatMap { UserDefaults(suiteName: $0) }) ?? .standard
        func b(_ k: String, _ def: Bool) -> Bool { d.object(forKey: "vv.battery." + k) as? Bool ?? def }
        on = b("on", false); profile = min(3, max(1, d.object(forKey: "vv.battery.profile") as? Int ?? 2))
        voices = b("voices", true); dsp = b("dsp", true); fx = b("fx", true); mod = b("mod", true); vis = b("vis", true); adaptive = b("adaptive", false)
        let nc = NotificationCenter.default
        nc.addObserver(forName: .NSProcessInfoPowerStateDidChange, object: nil, queue: .main) { [weak self] _ in self?.apply() }
        nc.addObserver(forName: ProcessInfo.thermalStateDidChangeNotification, object: nil, queue: .main) { [weak self] _ in self?.apply() }
    }
    private func save(_ k: String, _ v: Any) { defaults.set(v, forKey: "vv.battery." + k) }

    /// Called once the engine exists (app and AUv3), and again after the engine is rebuilt.
    public func attach(engine: @escaping () -> OpaquePointer?) { engineProvider = engine; apply() }

    /// The profile actually in force (0 = off).
    public var effectiveProfile: Int {
        let pi = ProcessInfo.processInfo, stressed = pi.isLowPowerModeEnabled || pi.thermalState == .serious || pi.thermalState == .critical
        if on { return adaptive && stressed ? max(profile, 2) : profile }
        return adaptive && stressed ? 1 : 0
    }
    private var effectiveFlags: Int32 { Int32((voices ? 1 : 0) | (dsp ? 2 : 0) | (fx ? 4 : 0) | (mod ? 8 : 0) | (vis ? 16 : 0)) }

    public func apply() {
        let p = effectiveProfile, pi = ProcessInfo.processInfo
        if let e = engineProvider?() {
            vv_engine_set_power(e, Int32(p), p == 0 ? 0 : (on ? effectiveFlags : 31))   // adaptive-only Light uses every low-risk option
            var st = [Double](repeating: 0, count: 5); vv_engine_get_power_stats(e, &st)
            let div = (p > 0 && (on ? vis : true)) ? max(1, Int(st[4])) : 1
            if div != uiDivider { uiDivider = div }
        } else { uiDivider = (p > 0 && (on ? vis : true)) ? [1, 2, 3, 6][p] : 1 }
        let why: String
        if p == 0 { why = "Off: full quality and refresh rate" }
        else { let name = ["", "Light", "Balanced", "Maximum Efficiency"][p]; why = on ? (p != profile ? "Active: \(name) (raised by \(pi.isLowPowerModeEnabled ? "Low Power Mode" : "device heat"))" : "Active: \(name)") : "Active: \(name) (\(pi.isLowPowerModeEnabled ? "Low Power Mode" : "device heat"), adaptive)" }
        if why != reason { reason = why }
    }

    /// Refresh interval for a visual that normally updates every `base` seconds. While a control is being dragged callers pass `interacting: true` for twice the rate.
    /// R11: TimelineView(.animation) minimum interval. nil = every display frame (60 Hz, 120 Hz on ProMotion); Battery Saving divides it.
    public func frameInterval(interacting: Bool = false) -> Double? { uiDivider <= 1 ? nil : (1.0 / 60.0) * Double(interacting ? max(1, uiDivider / 2) : uiDivider) }
    public func interval(_ base: Double, interacting: Bool = false) -> Double { uiDivider <= 1 ? base : base * Double(interacting ? max(1, uiDivider / 2) : uiDivider) }
}

struct BatterySavingSection: View {
    @ObservedObject private var b = BatterySaving.shared
    var body: some View {
        Section("Battery Saving Mode") {
            Text("Optimises processing efficiency and reduces unnecessary system activity to improve energy efficiency. Sound, timing and presets are never changed.").font(.caption2).foregroundStyle(.secondary)
            Toggle("Battery Saving Mode", isOn: $b.on).font(.caption)
            Picker("Profile", selection: $b.profile) { Text("Maximum Efficiency").tag(3); Text("Balanced (recommended)").tag(2); Text("Light").tag(1) }.font(.caption).disabled(!b.on)
            Group {
                Toggle("Intelligent Voice Management", isOn: $b.voices); Toggle("DSP Optimisation", isOn: $b.dsp); Toggle("Effects Processing Optimisation", isOn: $b.fx)
                Toggle("Modulation Processing Optimisation", isOn: $b.mod); Toggle("Visualisation Optimisation", isOn: $b.vis)
            }.font(.caption).disabled(!b.on)
            Toggle("Adapt to Low Power Mode and heat", isOn: $b.adaptive).font(.caption)
            Text(b.reason).font(.caption2).foregroundStyle(.secondary)
        }
    }
}
