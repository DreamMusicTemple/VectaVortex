// Oscillator layer colour identity (shared with the web app: the colours are the Zones section's ZONE_COLORS).
// WT #4ad4d9, FM #e2a04a, SMP #d94a9e, PRO #b026ff, SID #f0e020, LUA #6cff7a, XFX #e9694f.
// NOTE: written without a Mac. Not compiled yet.
import SwiftUI

enum LayerID {
    static let keys = ["WT", "FM", "SMP", "PRO", "SID", "LUA"]
    static let names = ["Wavetable", "FM", "Sampler", "Procedural", "SID", "LUA"]
    static let colors: [Color] = [Color(hex: "4ad4d9"), Color(hex: "e2a04a"), Color(hex: "d94a9e"), Color(hex: "b026ff"), Color(hex: "f0e020"), Color(hex: "6cff7a"), Color(hex: "e9694f")]   // index 6 = XFX (send-only layer)
    static let xfx = 6
    static func color(_ i: Int) -> Color { colors[max(0, min(6, i))] }
    static func color(key: String) -> Color { color(keys.firstIndex(of: key) ?? 0) }
}

private struct LayerAccentKey: EnvironmentKey { static let defaultValue: Color? = nil }
extension EnvironmentValues { var layerAccent: Color? { get { self[LayerAccentKey.self] } set { self[LayerAccentKey.self] = newValue } } }
extension View { /// tints dials, headers and chips inside this view with the layer's identity colour
    func layerIdentity(_ i: Int) -> some View { environment(\.layerAccent, LayerID.color(i)) } }

/// Low-opacity tinted button used for VIEW / HOLD / SOLO and layer tabs: restrained outline, a left accent bar, clear active state.
struct LayerButtonStyle: ButtonStyle {
    let layer: Int; var active: Bool = false; var muted: Bool = false
    func makeBody(configuration: Configuration) -> some View {
        let c = LayerID.color(layer)
        configuration.label
            .font(.system(size: 10, design: .monospaced))
            .padding(.horizontal, 8).padding(.vertical, 5)
            .foregroundStyle(active ? Color.white : Color.white.opacity(0.80))
            .background(RoundedRectangle(cornerRadius: 4).fill(LinearGradient(colors: [c.opacity(active ? 0.40 : 0.14), c.opacity(active ? 0.18 : 0.05)], startPoint: .top, endPoint: .bottom)).overlay(Color(white: 0.08).opacity(0.55)).opacity(muted ? 0.5 : 1))
            .overlay(RoundedRectangle(cornerRadius: 4).strokeBorder(c.opacity(active ? 1 : 0.40), lineWidth: 1))
            .overlay(alignment: .leading) { Rectangle().fill(c.opacity(active ? 1 : 0.55)).frame(width: 2).padding(.vertical, 1).padding(.leading, 1) }
            .shadow(color: active ? c.opacity(0.28) : .clear, radius: 4)
            .opacity(configuration.isPressed ? 0.75 : 1)
    }
}

/// Layer tab row with an identity dot and an underline for the layer being edited.
struct LayerTabBar: View {
    @Binding var selection: Int
    var body: some View {
        HStack(spacing: 4) {
            ForEach(0..<6, id: \.self) { i in
                let c = LayerID.color(i), on = selection == i
                Button { selection = i } label: {
                    HStack(spacing: 5) { Circle().fill(c.opacity(on ? 1 : 0.55)).frame(width: 6, height: 6).shadow(color: on ? c : .clear, radius: 3); Text(LayerID.names[i].uppercased()).lineLimit(1).minimumScaleFactor(0.7) }
                        .font(.system(size: 11, design: .monospaced)).frame(maxWidth: .infinity).padding(.vertical, 6)
                        .foregroundStyle(on ? Color.white : Color.white.opacity(0.8))
                        .background(RoundedRectangle(cornerRadius: 4).fill(LinearGradient(colors: [c.opacity(on ? 0.30 : 0.12), c.opacity(on ? 0.12 : 0.04)], startPoint: .top, endPoint: .bottom)))
                        .overlay(RoundedRectangle(cornerRadius: 4).strokeBorder(c.opacity(on ? 1 : 0.38), lineWidth: 1))
                        .overlay(alignment: .bottom) { Rectangle().fill(c).frame(height: on ? 2 : 0).padding(.horizontal, 2) }
                }.buttonStyle(.plain)
            }
        }
    }
}
