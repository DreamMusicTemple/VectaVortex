// Persistent footer under every tab (web: transport row + MPE / GLOBAL SWING / GENERATE / OS / CHORD row).
import SwiftUI

struct PlayFooterView: View {
    @ObservedObject var s: PresetStore
    var panic: () -> Void = {}
    @AppStorage("vv.mpe") private var mpe = false
    @State private var chordOn = false
    @State private var chord = "—"
    @State private var showSwing = false
    private let keys = ["WT", "FM", "SMP", "PRO", "SID", "LUA", "XFX"]
    private let counts = [2, 8, 16, 2, 3, 4]
    private let timer = Timer.publish(every: 0.1, on: .main, in: .common).autoconnect()
    @State private var showXfx = false
    @State private var hostBpm: Double? = nil   // non-nil while the AUv3 host supplies tempo: BPM is then read-only
    private func xfxHoldOn() -> Bool { (s.xfx()?["hold"] as? Bool) ?? false }
    private func holdOn(_ k: Int) -> Bool { if k == 6 { return xfxHoldOn() }; return (0..<counts[k]).allSatisfy { s.double("layers.\(keys[k]).oscs.\($0).hold", 0) != 0 } }
    private func setHold(_ k: Int, _ v: Bool) { if k == 6 { s.xfxAction("hold", ["on": v]); if !v { s.allNotesOff() }; return }; for o in 0..<counts[k] { s.set("layers.\(keys[k]).oscs.\(o).hold", v ? 1 : 0) }; if !v { s.allNotesOff() } }
    /// Standalone: this field is the tempo for arp, sequencer, clips and synced modulation / FX. Hosted AUv3: shows the host tempo read-only.
    private var bpmField: some View {
        HStack(spacing: 4) {
            Text("BPM").foregroundStyle(.secondary)
            if let h = hostBpm {
                Text(String(format: "%.1f", h)).font(.system(size: 11, weight: .bold, design: .monospaced)).frame(minWidth: 44)
                    .padding(.vertical, 3).padding(.horizontal, 4).overlay(RoundedRectangle(cornerRadius: 3).strokeBorder(.secondary.opacity(0.5)))
                Text("HOST").font(.system(size: 8, design: .monospaced)).foregroundStyle(.secondary)
            } else {
                TextField("BPM", value: Binding(get: { s.double("bpm", 120) }, set: { s.set("bpm", min(300, max(20, $0))) }), format: .number)
                    .keyboardType(.decimalPad).multilineTextAlignment(.center).frame(width: 52).textFieldStyle(.roundedBorder)
            }
        }.fixedSize()
    }
    /// VIEW LAYER dropdown: layer names tinted with the layer colour; XFX opens the XFX Mode editor, any other layer is shown and XFX closes.
    private var viewLayerMenu: some View {
        let names = ["WAVETABLE", "FM", "SAMPLER", "PROCEDURAL", "SID", "LUA", "XFX"]
        return Menu {
            ForEach(0..<7, id: \.self) { k in
                Button { if k == 6 { showXfx = true } else { showXfx = false; s.viewLayer = k } } label: {
                    Label(names[k], systemImage: (k == 6 ? showXfx : (!showXfx && s.viewLayer == k)) ? "checkmark.circle.fill" : "circle")
                }
            }
        } label: { Text("VIEW LAYER ▾") }
        .buttonStyle(LayerButtonStyle(layer: showXfx ? 6 : max(0, min(5, s.viewLayer)), active: true))
    }
    /// Oversampling: sits immediately right of the BPM field (web parity).
    private var osField: some View {
        HStack(spacing: 4) {
            Text("OS").foregroundStyle(.secondary)
            Picker("OS", selection: s.binding("oversample", string: "normal")) { Text("Normal").tag("normal"); Text("X2").tag("2x"); Text("X4").tag("4x") }.pickerStyle(.menu)
        }.fixedSize()
    }
    var body: some View {
        VStack(spacing: 4) {
            // Round 12: every layer HOLD / SOLO button shares ONE row (equal widths, text shrinks to fit) - no scrolling, no wrapping.
            HStack(spacing: 3) {
                ForEach(0..<7, id: \.self) { k in
                    Button("HOLD \(keys[k])") { setHold(k, !holdOn(k)) }.buttonStyle(LayerButtonStyle(layer: k, active: holdOn(k)))
                        .lineLimit(1).minimumScaleFactor(0.5).frame(maxWidth: .infinity)
                    let muted = s.muteMask & (1 << k) != 0, soloed = s.soloMask & (1 << k) != 0
                    // tap = SOLO; long-press mutes (the web's double-tap MUTE)
                    Button(muted ? "MUTED" : "SOLO \(keys[k])") { s.setSolo(k, !soloed) }
                        .simultaneousGesture(LongPressGesture(minimumDuration: 0.5).onEnded { _ in s.setMute(k, !muted) })
                        .buttonStyle(LayerButtonStyle(layer: k, active: soloed, muted: muted))
                        .lineLimit(1).minimumScaleFactor(0.5).frame(maxWidth: .infinity)
                }
            }
            HStack(spacing: 8) {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    viewLayerMenu   // ONE "VIEW LAYER" dropdown (web parity)
                    bpmField
                    osField
                    Button("MPE") { mpe.toggle(); if let e = s.audioUnit.enginePointer { vv_engine_set_mpe(e, mpe ? 1 : 0) } }.tint(mpe ? vvCyan : nil)
                    Button("GLOBAL SWING") { showSwing = true }.tint(s.bool("globalSwingOn", false) ? vvCyan : nil)
                        .popover(isPresented: $showSwing) {
                            VStack(alignment: .leading, spacing: 8) {
                                ToggleRow(s: s, label: "Swing on", path: "globalSwingOn")
                                SliderRow(s: s, label: "Swing amount", path: "globalSwingAmount", range: 50...75, def: 50, fmt: "%.0f", unit: " %")
                            }.padding().frame(minWidth: 280)
                        }
                    Menu("GENERATE") { ForEach(PresetStore.generatableCategories, id: \.self) { c in Button(c) { _ = s.generate(c) } } }
                    Toggle("CHORD", isOn: $chordOn).toggleStyle(WebSwitchStyle()).fixedSize().onChange(of: chordOn) { _ in chord = "—" }
                    Text(chord).font(.system(size: 13, weight: .bold, design: .monospaced)).foregroundStyle(vvCyan).frame(minWidth: 140, alignment: .leading)
                }
            }
            // Round 12: RECORDER elements and PANIC! sit at the far right of this row, right of the CHORD pill and readout.
            DawRecorderBlock(s: s).fixedSize()
            Button("PANIC!", role: .destructive, action: panic).font(.system(size: 10, design: .monospaced)).buttonStyle(.bordered).controlSize(.mini)
            }
        }
        .font(.system(size: 10, design: .monospaced)).buttonStyle(.bordered).controlSize(.mini).padding(.horizontal, 8).padding(.vertical, 4)
        .background(VVAppearance.panelOpacity < 1 ? AnyShapeStyle(Color.black.opacity(0.5 * VVAppearance.panelOpacity)) : AnyShapeStyle(.ultraThinMaterial))
        .sheet(isPresented: $showXfx) { XfxView(s: s) }
        .onReceive(timer) { _ in
            hostBpm = s.audioUnit.hostTempo
            guard chordOn else { return }
            let l = s.chordLabel(); if !l.isEmpty && l != chord { chord = l }
        }
    }
}
