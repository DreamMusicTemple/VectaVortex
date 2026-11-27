// Wire Overview: sources across the top, destination groups (collapsible, colour coded) down the right,
// connector lines from each source column to its destination row. Mirrors the web's overview grid.
import SwiftUI

func destGroupFor(_ key: String) -> String {
    let parts = key.split(separator: ":", maxSplits: 1, omittingEmptySubsequences: false).map(String.init)
    let dl = parts.first ?? "", dp = parts.count > 1 ? parts[1] : ""
    switch dl {
    case "FXP": return "FX"
    case "GEN":
        let g = dp.split(separator: ".").first.map(String.init) ?? ""
        return g == "WT" ? "Wavetable (both oscillators)" : g == "FM" ? "FM (whole layer)" : g == "SMP" ? "Sampler" : "Procedural"
    case "WT": return dp.hasPrefix("osc1") ? "Wavetable OSC1" : dp.hasPrefix("osc2") ? "Wavetable OSC2" : "Wavetable (both oscillators)"
    case "FM":
        if dp.hasPrefix("op"), dp.hasSuffix("Ratio"), dp.count == 8, let d = dp.dropFirst(2).first { return d <= "4" ? "FM Engine A" : "FM Engine B" }
        if dp.hasPrefix("engA") { return "FM Engine A" }
        if dp.hasPrefix("engB") { return "FM Engine B" }
        return "FM (whole layer)"
    case "SMP": return "Sampler"
    case "PRO": return "Procedural"
    case "FX": return "FX"
    case "ARP": return "Arpeggiator"
    case "GLOBAL":
        if dp.hasPrefix("xmod") { return "Cross-Mod" }
        if dp == "wtFmMix" || dp == "sampleMix" || dp == "masterTone" { return "Mixer" }
        return "Macros"
    default:
        if dl.hasPrefix("LFO") { return "LFO " + dl.dropFirst(3) }
        return "Other"
    }
}

let destGroupOrder = ["Wavetable OSC1", "Wavetable OSC2", "Wavetable (both oscillators)", "FM Engine A", "FM Engine B", "FM (whole layer)", "Sampler", "Procedural", "FX", "Arpeggiator",
                      "LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6", "Macros", "Mixer", "Cross-Mod"]
let destGroupHex: [String: String] = ["Wavetable OSC1": "5ad1e6", "Wavetable OSC2": "3b9cff", "Wavetable (both oscillators)": "8f9bff", "FM Engine A": "ff9f43", "FM Engine B": "ffc857",
    "FM (whole layer)": "ff7a59", "Sampler": "6bd66b", "Procedural": "b8e04a", "FX": "ff5fa8", "Arpeggiator": "e879f9", "LFO 1": "ff6b6b", "LFO 2": "ff8e72", "LFO 3": "ffb070", "LFO 4": "ffd166",
    "LFO 5": "e2e26a", "LFO 6": "b5e48c", "Macros": "e5e7eb", "Mixer": "f4a261", "Cross-Mod": "2dd4bf"]
let modSourceHex: [String: String] = ["lfo1": "4a90e2", "lfo2": "e2704a", "lfo3": "a24ae2", "lfo4": "e2d84a", "lfo5": "4ae29a", "lfo6": "e24a9e", "envWT": "4aff8a", "envFM": "4aff8a", "envSMP": "4aff8a",
    "envFollow": "4ad4d9", "velocity": "e2564a", "note": "ffb74d", "modwheel": "9fa8da", "random": "888888", "macro1": "ff4ab0", "macro2": "ff4ab0", "macro3": "ff4ab0", "macro4": "ff4ab0",
    "mpeTimbre": "4affd4", "mpePressure": "4affd4", "wtOsc1Pos": "9b6bff", "wtOsc2Pos": "9b6bff", "wtOsc1Shape": "4ad4d9", "wtOsc2Shape": "4ad4d9",
    "xy1x": "ffd54a", "xy1y": "ffd54a", "xy2x": "ff9a4a", "xy2y": "ff9a4a", "xy3x": "4aa8ff", "xy3y": "4aa8ff", "xy4x": "b04aff", "xy4y": "b04aff"]
func destGroupColor(_ g: String) -> Color { Color(hex: destGroupHex[g] ?? "9b6bff") }
func modSourceColor(_ s: String) -> Color { Color(hex: modSourceHex[s] ?? "c99a52") }

struct WireOverviewView: View {
    @ObservedObject var s: PresetStore
    @State private var open: Set<String> = []
    @State private var initialised = false
    private let cellW: CGFloat = 30, cellH: CGFloat = 28, headH: CGFloat = 176, labW: CGFloat = 300

    private var groups: [String] {
        let all = Set(modDestGroups.flatMap { $0.1 }.map(destGroupFor))
        return destGroupOrder.filter(all.contains) + all.subtracting(destGroupOrder).sorted()
    }
    private func dests(_ g: String) -> [String] {
        var seen = Set<String>(); return modDestGroups.flatMap { $0.1 }.filter { destGroupFor($0) == g && seen.insert($0).inserted }
    }
    private struct Wire { let src: String; let dest: String; let amount: Double; let index: Int }
    private var wires: [Wire] {
        (0..<s.count("connections")).map { i in let p = "connections.\(i)"; return Wire(src: s.string(p + ".source"), dest: s.string(p + ".dest"), amount: s.double(p + ".amount"), index: i) }
    }
    private struct Row: Identifiable { let id: String; let name: String; let dest: String?; let group: String }
    private var rows: [Row] {
        groups.flatMap { g -> [Row] in [Row(id: "g:" + g, name: g, dest: nil, group: g)] + (open.contains(g) ? dests(g).map { Row(id: $0, name: $0, dest: $0, group: g) } : []) }
    }

    var body: some View {
        let ws = wires, rs = rows, nS = modSources.count
        let gridW = CGFloat(nS) * cellW
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Button("Expand all") { open = Set(groups) }.buttonStyle(.bordered)
                Button("Collapse all") { open.removeAll() }.buttonStyle(.bordered)
            }
            ScrollView([.horizontal, .vertical]) {
                ZStack(alignment: .topLeading) {
                    // vertical source labels
                    ForEach(0..<nS, id: \.self) { si in
                        Text(modSources[si]).font(.system(size: 13, design: .monospaced)).foregroundColor(modSourceColor(modSources[si])).fixedSize()
                            .rotationEffect(.degrees(-90)).frame(width: cellW, height: headH, alignment: .bottom)
                            .position(x: CGFloat(si) * cellW + cellW / 2, y: headH / 2)
                    }
                    ForEach(Array(rs.enumerated()), id: \.element.id) { ri, r in
                        let y = headH + CGFloat(ri) * cellH
                        if let d = r.dest {
                            Rectangle().fill(destGroupColor(r.group)).frame(width: 4, height: cellH - 6).position(x: gridW + 4, y: y + cellH / 2)
                            Text(d).font(.system(size: 13, design: .monospaced)).foregroundColor(destGroupColor(r.group)).lineLimit(1).frame(width: labW - 14, alignment: .leading)
                                .position(x: gridW + 12 + (labW - 14) / 2, y: y + cellH / 2)
                            ForEach(0..<nS, id: \.self) { si in
                                let w = ws.first { $0.src == modSources[si] && $0.dest == d }
                                ZStack {
                                    Rectangle().stroke(Color.white.opacity(0.07))
                                    if let w = w { Rectangle().fill(modSourceColor(modSources[si]).opacity(0.85)).padding(2); Text("\(Int(w.amount))").font(.system(size: 11, design: .monospaced)).foregroundColor(.black) }
                                }.frame(width: cellW, height: cellH).position(x: CGFloat(si) * cellW + cellW / 2, y: y + cellH / 2)
                                 .onTapGesture { if let w = w { s.remove("connections", at: w.index) } else { s.append("connections", ["source": modSources[si], "dest": d, "amount": 30]) } }
                            }
                        } else {
                            let ds = dests(r.group); let n = ws.filter { w in ds.contains(w.dest) }.count
                            Rectangle().fill(Color.white.opacity(0.05)).frame(width: gridW + labW, height: cellH).position(x: (gridW + labW) / 2, y: y + cellH / 2)
                            ForEach(0..<nS, id: \.self) { si in
                                let c = ws.filter { $0.src == modSources[si] && ds.contains($0.dest) }.count
                                if c > 0 {
                                    Text("\(c)").font(.system(size: 12, design: .monospaced)).foregroundColor(.black).frame(width: cellW - 4, height: cellH - 4)
                                        .background(modSourceColor(modSources[si]).opacity(open.contains(r.group) ? 0.3 : 0.9)).position(x: CGFloat(si) * cellW + cellW / 2, y: y + cellH / 2)
                                }
                            }
                            Text((open.contains(r.group) ? "▾ " : "▸ ") + r.group + " (\(ds.count)" + (n > 0 ? " · \(n) wired" : "") + ")")
                                .font(.system(size: 14, weight: .bold, design: .monospaced)).foregroundColor(destGroupColor(r.group)).lineLimit(1).frame(width: labW - 8, alignment: .leading)
                                .position(x: gridW + 8 + (labW - 8) / 2, y: y + cellH / 2)
                        }
                    }
                    // connector lines (wires into a collapsed group end at its header)
                    Canvas { ctx, _ in
                        for w in ws {
                            guard let si = modSources.firstIndex(of: w.src) else { continue }
                            let g = destGroupFor(w.dest)
                            guard let ri = rs.firstIndex(where: { open.contains(g) ? $0.dest == w.dest : ($0.dest == nil && $0.group == g) }) else { continue }
                            let cx = CGFloat(si) * cellW + cellW / 2, cy = headH, ry = headH + CGFloat(ri) * cellH + cellH / 2, lx = gridW + 2
                            let rad = min(10, abs(ry - cy) / 2, abs(lx - cx) / 2)
                            var p = Path(); p.move(to: CGPoint(x: cx, y: cy)); p.addLine(to: CGPoint(x: cx, y: ry - rad))
                            p.addQuadCurve(to: CGPoint(x: cx + rad, y: ry), control: CGPoint(x: cx, y: ry)); p.addLine(to: CGPoint(x: lx, y: ry))
                            let sc = modSourceColor(w.src), dc = destGroupColor(g)
                            ctx.stroke(p, with: .linearGradient(Gradient(colors: [sc, dc]), startPoint: CGPoint(x: cx, y: cy), endPoint: CGPoint(x: lx, y: ry)), lineWidth: 1.6)
                            ctx.fill(Path(ellipseIn: CGRect(x: cx - 2.2, y: cy - 2.2, width: 4.4, height: 4.4)), with: .color(sc))
                            ctx.fill(Path(ellipseIn: CGRect(x: lx - 2.2, y: ry - 2.2, width: 4.4, height: 4.4)), with: .color(dc))
                        }
                    }.allowsHitTesting(false)
                    // group header tap targets
                    ForEach(Array(rs.enumerated()), id: \.element.id) { ri, r in
                        if r.dest == nil {
                            Color.clear.contentShape(Rectangle()).frame(width: labW, height: cellH).position(x: gridW + labW / 2, y: headH + CGFloat(ri) * cellH + cellH / 2)
                                .onTapGesture { if open.contains(r.group) { open.remove(r.group) } else { open.insert(r.group) } }
                        }
                    }
                }.frame(width: gridW + labW + 10, height: headH + CGFloat(rs.count) * cellH + 10)
            }.frame(minHeight: 360)
        }
        .onAppear { if !initialised { initialised = true; open = Set(ws.map { destGroupFor($0.dest) }) } }
    }
}
