// Vektur Interstellar dial face (shared design with the web and JUCE apps): a ring of dots that grows from pin-pricks to large bright
// dots clockwise up to the value, a knurled top-lit skirt, a cap with a specular kiss and a white pointer.
// NOTE: written without a Mac. Not compiled yet.
import SwiftUI

struct VVDialFace: View {
    var norm: Double
    var modNorm: Double? = nil
    var light: Color
    var size: CGFloat
    var active: Bool
    var steps: Int
    @Environment(\.layerAccent) private var layerAccent: Color?
    @AppStorage("vv.potStyle") private var potStyle = 0   // Round 8: 0 = this Interstellar dial, 1 Aqua, 2 Scientific, 3 Holo ring

    var body: some View {
        if let st = VVDialStyle(rawValue: potStyle), st != .interstellar { VVStyledDial(style: st, norm: norm, light: light, size: size, dragging: active) } else { interstellar }
    }
    private var interstellar: some View {
        let skirtInset = size * 0.15
        return ZStack {
            VVDotRing(norm: norm, modNorm: modNorm, light: light, active: active, steps: steps, size: size)
            // knurled skirt, top-lit
            Circle()
                .fill(AngularGradient(colors: [Color(white: 0.30), Color(white: 0.11), Color(white: 0.26), Color(white: 0.09), Color(white: 0.30)],
                                      center: .center))
                .overlay(Circle().fill(LinearGradient(colors: [light.opacity(0.38), light.opacity(0.06), .clear],
                                                      startPoint: .top, endPoint: .center)).blendMode(.screen))
                .overlay(Circle().strokeBorder(LinearGradient(colors: [light.opacity(0.75), light.opacity(0.1), .clear],
                                                              startPoint: .top, endPoint: .bottom), lineWidth: max(0.8, size * 0.012)))
                .padding(skirtInset)
                .shadow(color: .black.opacity(0.8), radius: size * 0.07, x: 0, y: size * 0.06)
            // restrained layer-identity accent: a thin recessed arc just outside the skirt (never touches the value ring or pointer)
            if let a = layerAccent {
                let r = size * 0.5 - skirtInset + size * 0.035
                ZStack { Circle().trim(from: 0, to: 0.75).rotation(.degrees(135)).stroke(Color.black.opacity(0.55), lineWidth: max(1, size * 0.022)).offset(y: size * 0.004)
                    Circle().trim(from: 0, to: 0.75).rotation(.degrees(135)).stroke(a.opacity(0.55), lineWidth: max(1, size * 0.014)) }.frame(width: r * 2, height: r * 2)
            }
            // cap with a specular kiss of the screen light
            Circle()
                .fill(RadialGradient(colors: [Color(white: 0.24), Color(white: 0.05)],
                                     center: UnitPoint(x: 0.5, y: 0.18), startRadius: 0, endRadius: size * 0.42))
                .overlay(Ellipse().fill(RadialGradient(colors: [light.opacity(0.45), .clear], center: .center, startRadius: 0, endRadius: size * 0.16))
                    .frame(width: size * 0.34, height: size * 0.14).offset(y: -size * 0.17).blendMode(.screen))
                .padding(skirtInset + size * 0.085)
            // pointer
            Capsule()
                .fill(Color.white.opacity(0.92))
                .frame(width: max(2, size * 0.04), height: size * 0.16)
                .offset(y: -(size * 0.5 - skirtInset - size * 0.12))
                .rotationEffect(.degrees(-135 + 270 * norm))
        }
    }
}

/// Value ring of dots: pin-pricks at the start, growing larger and brighter clockwise, revealed up to the value.
struct VVDotRing: View {
    var norm: Double
    var modNorm: Double?
    var light: Color
    var active: Bool
    var steps: Int
    var size: CGFloat

    var body: some View {
        Canvas { ctx, sz in
            let n = (steps > 1 && steps <= 16) ? steps : (size < 50 ? 15 : 21)
            let minR = max(0.6, size * 0.011), maxR = max(1.8, size * 0.047)
            let c = CGPoint(x: sz.width / 2, y: sz.height / 2)
            let R = sz.width / 2 - maxR - 0.5
            func point(_ t: Double) -> CGPoint {
                let a = (135 + 270 * t) * .pi / 180
                return CGPoint(x: c.x + R * cos(a), y: c.y + R * sin(a))
            }
            func dot(_ p: CGPoint, _ r: Double) -> Path { Path(ellipseIn: CGRect(x: p.x - r, y: p.y - r, width: r * 2, height: r * 2)) }
            let pos = norm * Double(n - 1)
            for i in 0..<n {
                let t = n == 1 ? 0 : Double(i) / Double(n - 1)
                let reveal = min(1, max(0, pos - Double(i) + 1))
                let p = point(t)
                // unlit pin-prick
                ctx.fill(dot(p, minR * 0.8), with: .color(.white.opacity(0.13)))
                guard reveal > 0 else { continue }
                let r = minR * 0.8 + (minR + (maxR - minR) * pow(t, 1.15) - minR * 0.8) * reveal
                if active || t > 0.7 {
                    ctx.fill(dot(p, r * 2.1), with: .color(light.opacity(0.10 * reveal * (active ? 1.6 : 1))))
                }
                ctx.fill(dot(p, r), with: .color(light.opacity((0.45 + 0.55 * t) * reveal)))
                ctx.fill(dot(p, r * 0.62), with: .color(.white.opacity(t * 0.75 * reveal)))
            }
            if let m = modNorm {
                let p = point(min(1, max(0, m)))
                ctx.fill(dot(p, maxR * 0.75), with: .color(.white.opacity(0.95)))
                ctx.stroke(dot(p, maxR * 1.25), with: .color(light.opacity(0.9)), lineWidth: 1)
            }
        }
        .allowsHitTesting(false)
    }
}

