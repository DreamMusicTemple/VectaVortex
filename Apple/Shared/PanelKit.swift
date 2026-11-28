// Web-style panels: Form / Section are shadowed so every screen built from them gets the web layout —
// dark glass panels with an accent title bar, dials flowing in a wrapping grid (label above, round dial, value box below).
import SwiftUI

/// Wrapping layout: fixed-size children (dials, toggles, menus) flow left to right; flexible children (graphs, editors, lists) take a full row.
struct FlowLayout: Layout {
    var spacing: CGFloat = 8
    private func rows(_ subviews: Subviews, _ width: CGFloat) -> (frames: [CGRect], height: CGFloat) {
        var frames: [CGRect] = [], x: CGFloat = 0, y: CGFloat = 0, rowH: CGFloat = 0
        for v in subviews {
            let probe = v.sizeThatFits(ProposedViewSize(width: .infinity, height: nil))
            let full = !probe.width.isFinite || probe.width > width * 0.9
            let size = full ? v.sizeThatFits(ProposedViewSize(width: width, height: nil)) : probe
            if full || (x > 0 && x + size.width > width + 0.5) { x = 0; y += rowH + (rowH > 0 ? spacing : 0); rowH = 0 }
            let w = full ? width : size.width
            frames.append(CGRect(x: x, y: y, width: w, height: size.height))
            x += w + spacing; rowH = max(rowH, size.height)
            if full { x = 0; y += rowH + spacing; rowH = 0 }
        }
        return (frames, y + rowH)
    }
    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let w = proposal.width ?? 600
        return CGSize(width: w, height: rows(subviews, w).height)
    }
    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        let r = rows(subviews, bounds.width)
        for (i, v) in subviews.enumerated() {
            let f = r.frames[i]
            v.place(at: CGPoint(x: bounds.minX + f.minX, y: bounds.minY + f.minY), anchor: .topLeading, proposal: ProposedViewSize(width: f.width, height: f.height))
        }
    }
}

/// Replaces SwiftUI.Form: a scrolling stack of panels on the app background.
struct Form<Content: View>: View {
    @ViewBuilder var content: Content
    var body: some View {
        ScrollView { LazyVStack(alignment: .leading, spacing: 12) { content }.padding(.horizontal, 10).padding(.vertical, 8) }
            .scrollDismissesKeyboard(.interactively)
    }
}

/// Replaces SwiftUI.Section: web panel with a small caps title bar (accent marker) and a wrapping grid of controls.
struct Section<Content: View>: View {
    let title: String
    @ViewBuilder var content: Content
    @Environment(\.layerAccent) private var layerAccent
    init(_ title: String, @ViewBuilder content: () -> Content) { self.title = title; self.content = content() }
    init(@ViewBuilder content: () -> Content) { self.title = ""; self.content = content() }
    var body: some View {
        let accent = layerAccent ?? vvCyan
        VStack(alignment: .leading, spacing: 0) {
            if !title.isEmpty { HStack(spacing: 6) {
                HStack(spacing: 2) { Rectangle().fill(accent).frame(width: 3, height: 12); Rectangle().fill(accent.opacity(0.5)).frame(width: 3, height: 12) }
                Text(title.uppercased()).font(.system(size: 11, weight: .semibold, design: .monospaced)).foregroundStyle(Color.white.opacity(0.85)).lineLimit(1)
                Spacer()
            }.padding(.horizontal, 12).padding(.vertical, 7)
            Rectangle().fill(accent.opacity(0.35)).frame(height: 1) }
            FlowLayout(spacing: 10) { content }.padding(12)
        }
        .background(RoundedRectangle(cornerRadius: 10).fill(Color(red: 0.05, green: 0.07, blue: 0.09).opacity(0.78)))
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(accent.opacity(0.30), lineWidth: 1))
    }
}


let vvValueTeal = Color(red: 0.40, green: 0.88, blue: 0.93)   // web value-box text

/// Web value box: dark inset field, teal monospaced text.
struct ValueBox: View {
    let text: String; var width: CGFloat = 70
    var body: some View {
        Text(text).font(.system(size: 12, design: .monospaced)).foregroundStyle(vvValueTeal).lineLimit(1).minimumScaleFactor(0.7)
            .frame(width: width).padding(.vertical, 3)
            .background(RoundedRectangle(cornerRadius: 3).fill(Color(red: 0.10, green: 0.14, blue: 0.18)).overlay(RoundedRectangle(cornerRadius: 3).stroke(Color.white.opacity(0.16), lineWidth: 1)))
    }
}

/// Round dial bound to a 0...1 value (drag up / down, 220 pt = full sweep, double-tap resets).
struct NormDial: View {
    let title: String; @Binding var norm: Double; let display: String; var size: CGFloat = 44; var reset: Double = 0
    @State private var startNorm: Double?; @State private var lastTy: CGFloat = 0
    var body: some View {
        VStack(spacing: 3) {
            Text(title).font(.system(size: 10, design: .monospaced)).foregroundStyle(Color.white.opacity(0.62)).lineLimit(2).multilineTextAlignment(.center).minimumScaleFactor(0.8).frame(height: 26, alignment: .bottom)
            VVDialFace(norm: norm, modNorm: nil, light: vvCyan, size: size, active: startNorm != nil, steps: 0).frame(width: size, height: size).contentShape(Circle())
                .gesture(DragGesture(minimumDistance: 1).onChanged { g in
                    if startNorm == nil { startNorm = norm; lastTy = g.translation.height }
                    norm = min(max(norm + Double(lastTy - g.translation.height) / 220, 0), 1); lastTy = g.translation.height
                }.onEnded { _ in startNorm = nil })
                .onTapGesture(count: 2) { norm = reset }
            ValueBox(text: display, width: 66)
        }
        .padding(.horizontal, 8).padding(.vertical, 6)
        .background(RoundedRectangle(cornerRadius: 6).fill(Color.black.opacity(0.35)).overlay(RoundedRectangle(cornerRadius: 6).stroke(Color.white.opacity(0.12), lineWidth: 1)))
    }
}

/// Web macro / master strip: MACRO 1-4, MASTER VOLUME, MASTER TONE as dial tiles.
struct MacroTiles: View {
    @ObservedObject var store: ParameterStore
    @State private var macros: [Double] = [0, 0, 0, 0]
    var size: CGFloat = 44
    private func modelDial(_ id: String, _ title: String, fmt: String) -> some View {
        Group {
            if let m = store.model(id) {
                let lo = Double(m.spec.minValue), hi = Double(m.spec.maxValue)
                NormDial(title: title, norm: Binding(get: { (Double(m.value) - lo) / max(hi - lo, 1e-9) }, set: { m.set(Float(lo + $0 * (hi - lo))) }), display: String(format: fmt, Double(m.value)), size: size, reset: 0)
            }
        }
    }
    var body: some View {
        HStack(spacing: 6) {
            ForEach(0..<4, id: \.self) { i in
                NormDial(title: "MACRO \(i + 1)", norm: Binding(get: { macros[i] }, set: { macros[i] = $0; store.audioUnit.setMacro(i, Float($0)) }), display: String(format: "%.2f", macros[i]), size: size)
            }
            modelDial("master_volume", "MASTER VOLUME", fmt: "%.2f")
            modelDial("master_tone", "MASTER TONE", fmt: "%.2f")
        }
    }
}
