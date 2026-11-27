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
