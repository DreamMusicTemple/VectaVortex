// Appearance (web parity: Color scheme + Background image). Choices persist in UserDefaults and rebuild the UI when changed.
// Potentiometer style has no SwiftUI equivalent: the native app uses system sliders and menus.
import SwiftUI

enum VVAppearance {
    struct Scheme { let name: String; let accent: UInt32; let background: Int; var pot: Int = 0 /* VVDialStyle raw value this scheme selects (0 = leave the Interstellar dial) */ }   // background: index into `backgrounds` shown for "Scheme default" (6 = Brushed metal, the app default)
    static let schemes: [Scheme] = [Scheme(name: "Gold", accent: 0xC99A52, background: 6), Scheme(name: "Cyan", accent: 0x26D9E6, background: 6), Scheme(name: "Magenta", accent: 0xD94A9E, background: 6),
                                    Scheme(name: "Cream", accent: 0xD97F2A, background: 6), Scheme(name: "Future", accent: 0x5AD1E6, background: 1), Scheme(name: "Rustic", accent: 0xC96B4A, background: 2),
                                    Scheme(name: "Metaphysical + Aqua", accent: 0x5AD1E6, background: 7, pot: 1),
                                    Scheme(name: "Metaphysical + Scientific", accent: 0x5AD1E6, background: 7, pot: 2),
                                    Scheme(name: "Metaphysical + Holo", accent: 0x5AD1E6, background: 7, pot: 3),
                                    Scheme(name: "Samurai + Aqua", accent: 0x5AD1E6, background: 8, pot: 1),
                                    Scheme(name: "Samurai + Scientific", accent: 0x5AD1E6, background: 8, pot: 2),
                                    Scheme(name: "Samurai + Holo", accent: 0x5AD1E6, background: 8, pot: 3)]
    /// (label, file in Assets/Backgrounds). Index 0 = scheme default, last = none.
    static let backgrounds: [(String, String)] = [("Scheme default (Brushed metal)", ""), ("Future (cyan wireframe)", "future"), ("Rustic", "rustic"), ("Ocean", "ocean"), ("Nebula", "nebula"),
                                                  ("Vektur Vortex", "vortex"), ("Brushed metal", "brushed"), ("Metaphysical library", "meta"), ("Samurai vs ninja", "ninja"), ("None", "")]
    static var scheme: Int { let v = UserDefaults.standard.object(forKey: "vv.scheme") as? Int ?? 1; return min(max(v, 0), schemes.count - 1) }
    static var accent: Color { Color(hex: String(format: "%06X", schemes[scheme].accent)) }
    /// Effective background index (scheme default follows the scheme).
    static func effectiveBackground(scheme s: Int, choice: Int) -> Int { choice != 0 ? choice : schemes[min(max(s, 0), schemes.count - 1)].background }
    static func image(_ index: Int) -> UIImage? {
        guard index > 0, index < backgrounds.count - 1 else { return nil }
        let name = backgrounds[index].1
        for b in [Bundle.main, Bundle(for: BundleToken.self)] {
            if let u = b.url(forResource: name, withExtension: "jpg", subdirectory: "Backgrounds") ?? b.url(forResource: name, withExtension: "jpg"), let i = UIImage(contentsOfFile: u.path) { return i }
        }
        return nil
    }
    private final class BundleToken {}
    /// Panel opacity behind the dials: 1.0 solid, 0.5 or 0.1 (lets more of the background picture show through). Web: Settings > Panel opacity.
    static var panelOpacity: Double { let v = UserDefaults.standard.object(forKey: "vv.panelOpacity") as? Double ?? 1; return v }
}

/// The picture behind the whole UI (cover-fit, dimmed so controls stay readable).
struct AppBackground: View {
    @AppStorage("vv.scheme") private var scheme = 1
    @AppStorage("vv.bg") private var bg = 0
    @AppStorage("vv.panelOpacity") private var panelOp = 1.0
    var body: some View {
        let idx = VVAppearance.effectiveBackground(scheme: scheme, choice: bg)
        ZStack {
            Color(red: 0.04, green: 0.045, blue: 0.05)
            // the dark tint beneath the controls scales with the panel opacity (100% = 0.35 as before; 50% / 10% show more of the picture)
            if let ui = VVAppearance.image(idx) { Image(uiImage: ui).resizable().scaledToFill().overlay(Color.black.opacity(0.35 * panelOp)) }
        }.ignoresSafeArea()
    }
}

struct AppearanceSection: View {
    @AppStorage("vv.scheme") private var scheme = 1
    @AppStorage("vv.bg") private var bg = 0
    @AppStorage("vv.panelOpacity") private var panelOp = 1.0
    @AppStorage("vv.potStyle") private var potStyle = 0
    var body: some View {
        Section("Appearance") {
            Picker("Potentiometer style", selection: $potStyle) { ForEach(0..<VVDialStyle.allCases.count, id: \.self) { Text(VVDialStyle.allCases[$0].label).tag($0) } }
            Picker("Colour scheme", selection: Binding(get: { scheme }, set: { v in
                let prevPot = VVAppearance.schemes[min(max(scheme, 0), VVAppearance.schemes.count - 1)].pot, p = VVAppearance.schemes[v].pot
                scheme = v
                if p > 0 { potStyle = p; bg = 0 } else if prevPot > 0 { potStyle = 0 }   // leaving a combination restores the default dial and picture
            })) { ForEach(0..<VVAppearance.schemes.count, id: \.self) { Text(VVAppearance.schemes[$0].name).tag($0) } }
            Picker("Background image", selection: Binding(get: { bg }, set: { bg = $0; if $0 > 0 { scheme = 4 } })) {   // picking an image switches to Future, like the web
                ForEach(0..<VVAppearance.backgrounds.count, id: \.self) { Text(VVAppearance.backgrounds[$0].0).tag($0) }
            }
            Picker("Panel opacity (behind the dials)", selection: $panelOp) { Text("100% (solid)").tag(1.0); Text("50%").tag(0.5); Text("10% (see the picture)").tag(0.1) }
        }
    }
}
