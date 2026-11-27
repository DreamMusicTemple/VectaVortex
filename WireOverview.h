// Wire Overview: sources across the top, destinations (grouped, collapsible, colour coded) down the right,
// connector lines from each source column to the destination row. Mirrors the web's overview grid.
#pragma once
#include "Widgets.h"

namespace vv {

inline juce::String destGroupFor(const juce::String& key) {
    const auto dl = key.upToFirstOccurrenceOf(":", false, false), dp = key.fromFirstOccurrenceOf(":", false, false);
    if (dl == "FXP") return "FX";
    if (dl == "GEN") { auto g = dp.upToFirstOccurrenceOf(".", false, false); return g == "WT" ? "Wavetable (both oscillators)" : g == "FM" ? "FM (whole layer)" : g == "SMP" ? "Sampler" : "Procedural"; }
    if (dl == "WT") { if (dp.startsWith("osc1")) return "Wavetable OSC1"; if (dp.startsWith("osc2")) return "Wavetable OSC2"; return "Wavetable (both oscillators)"; }
    if (dl == "FM") {
        if (dp.startsWith("op") && dp.endsWith("Ratio") && dp.length() == 8) return dp[2] <= '4' ? "FM Engine A" : "FM Engine B";
        if (dp.startsWith("engA")) return "FM Engine A"; if (dp.startsWith("engB")) return "FM Engine B"; return "FM (whole layer)"; }
    if (dl == "SMP") return "Sampler"; if (dl == "PRO") return "Procedural"; if (dl == "FX") return "FX"; if (dl == "ARP") return "Arpeggiator";
    if (dl.startsWith("LFO")) return "LFO " + dl.substring(3);
    if (dl == "GLOBAL") { if (dp.startsWith("xmod")) return "Cross-Mod"; if (dp == "wtFmMix" || dp == "sampleMix" || dp == "masterTone") return "Mixer"; return "Macros"; }
    return "Other";
}
inline juce::Colour destGroupColour(const juce::String& g) {
    static const std::pair<const char*, const char*> t[] = { { "Wavetable OSC1", "5ad1e6" }, { "Wavetable OSC2", "3b9cff" }, { "Wavetable (both oscillators)", "8f9bff" }, { "FM Engine A", "ff9f43" },
        { "FM Engine B", "ffc857" }, { "FM (whole layer)", "ff7a59" }, { "Sampler", "6bd66b" }, { "Procedural", "b8e04a" }, { "FX", "ff5fa8" }, { "Arpeggiator", "e879f9" }, { "LFO 1", "ff6b6b" },
        { "LFO 2", "ff8e72" }, { "LFO 3", "ffb070" }, { "LFO 4", "ffd166" }, { "LFO 5", "e2e26a" }, { "LFO 6", "b5e48c" }, { "Macros", "e5e7eb" }, { "Mixer", "f4a261" }, { "Cross-Mod", "2dd4bf" } };
    for (auto& e : t) if (g == e.first) return juce::Colour::fromString("ff" + juce::String(e.second));
    return juce::Colour::fromString("ff9b6bff");
}
inline juce::Colour modSourceColour(const juce::String& s) {
    static const std::pair<const char*, const char*> t[] = { { "lfo1", "4a90e2" }, { "lfo2", "e2704a" }, { "lfo3", "a24ae2" }, { "lfo4", "e2d84a" }, { "lfo5", "4ae29a" }, { "lfo6", "e24a9e" },
        { "envWT", "4aff8a" }, { "envFM", "4aff8a" }, { "envSMP", "4aff8a" }, { "envFollow", "4ad4d9" }, { "velocity", "e2564a" }, { "note", "ffb74d" }, { "modwheel", "9fa8da" }, { "random", "888888" },
        { "macro1", "ff4ab0" }, { "macro2", "ff4ab0" }, { "macro3", "ff4ab0" }, { "macro4", "ff4ab0" }, { "mpeTimbre", "4affd4" }, { "mpePressure", "4affd4" },
        { "wtOsc1Pos", "9b6bff" }, { "wtOsc2Pos", "9b6bff" }, { "wtOsc1Shape", "4ad4d9" }, { "wtOsc2Shape", "4ad4d9" },
        { "xy1x", "ffd54a" }, { "xy1y", "ffd54a" }, { "xy2x", "ff9a4a" }, { "xy2y", "ff9a4a" }, { "xy3x", "4aa8ff" }, { "xy3y", "4aa8ff" }, { "xy4x", "b04aff" }, { "xy4y", "b04aff" } };
    for (auto& e : t) if (s == e.first) return juce::Colour::fromString("ff" + juce::String(e.second));
    return juce::Colour::fromString("ffc99a52");
}

// The grid itself. `sources` / `groups` are the same lists the route pickers use.
struct WireOverview : juce::Component, private Store::Listener {
    WireOverview(Store& s, juce::StringArray srcs, std::vector<std::pair<juce::String, juce::StringArray>> groups)
        : store(s), sources(std::move(srcs)), grp(std::move(groups)) {
        store.addListener(this);
        // groups that already hold a wire start open (like the web)
        for (auto& g : grp) for (auto& d : g.second) if (conn(d, true)) { open.add(groupOf(d)); break; }
        for (auto* b : { &expand, &collapse }) { addAndMakeVisible(*b); }
        expand.setButtonText("Expand all"); collapse.setButtonText("Collapse all");
        expand.onClick = [this] { for (auto& n : groupNames()) open.addIfNotAlreadyThere(n); relayout(); };
        collapse.onClick = [this] { open.clear(); relayout(); };
    }
    ~WireOverview() override { store.removeListener(this); }
    void storeChanged() override { if (store.count("connections") != lastCount) { lastCount = store.count("connections"); relayout(); } else repaint(); }

    static constexpr int cellW = 30, cellH = 24, headH = 190, labW = 330, topBar = 34;

    juce::String groupOf(const juce::String& d) const { return destGroupFor(d); }
    juce::StringArray groupNames() const {
        static const char* order[] = { "Wavetable OSC1", "Wavetable OSC2", "Wavetable (both oscillators)", "FM Engine A", "FM Engine B", "FM (whole layer)", "Sampler", "Procedural", "FX", "Arpeggiator",
            "LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6", "Macros", "Mixer", "Cross-Mod" };
        juce::StringArray all; for (auto& g : grp) for (auto& d : g.second) all.addIfNotAlreadyThere(groupOf(d));
        juce::StringArray out; for (auto* o : order) if (all.contains(o)) out.add(o);
        for (auto& a : all) if (!out.contains(a)) out.add(a);
        return out;
    }
    juce::StringArray destsIn(const juce::String& g) const { juce::StringArray r; for (auto& gr : grp) for (auto& d : gr.second) if (groupOf(d) == g) r.addIfNotAlreadyThere(d); return r; }
    // index of the connection source->dest, -1 if none. anySource: any source reaching dest.
    int conn(const juce::String& dest, bool anySource, const juce::String& src = {}) const {
        const int n = store.count("connections");
        for (int i = 0; i < n; ++i) { const auto p = "connections." + juce::String(i); if (store.str(p + ".dest", "") == dest && (anySource || store.str(p + ".source", "") == src)) return i; }
        return -1;
    }
    struct Row { juce::String name, dest; bool header; };
    std::vector<Row> rows() const {
        std::vector<Row> r;
        for (auto& g : groupNames()) { r.push_back({ g, {}, true }); if (open.contains(g)) for (auto& d : destsIn(g)) r.push_back({ d, d, false }); }
        return r;
    }
    int rowY(int i) const { return topBar + headH + i * cellH; }
    void relayout() { setSize(sources.size() * cellW + labW + 10, topBar + headH + (int) rows().size() * cellH + 10); repaint(); }
    void resized() override { expand.setBounds(4, 4, 100, 26); collapse.setBounds(110, 4, 100, 26); }

    void paint(juce::Graphics& g) override {
        g.fillAll(col::bg);
        const auto rs = rows(); const int nS = sources.size(), x0 = 0, gridR = x0 + nS * cellW;
        g.setFont(VVLookAndFeel::mono(13.f));
        for (int si = 0; si < nS; ++si) {   // vertical source labels, source colour
            juce::Graphics::ScopedSaveState ss(g);
            g.setColour(modSourceColour(sources[si]));
            g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, (float) (x0 + si * cellW + cellW / 2), (float) (topBar + headH)).translated(0.f, 0.f));
            g.drawText(sources[si], x0 + si * cellW + cellW / 2, topBar + headH - cellW / 2 - 2, headH - 6, cellW, juce::Justification::centredLeft);
        }
        std::vector<int> anchor(rs.size(), 0);
        for (size_t i = 0; i < rs.size(); ++i) {
            const int y = rowY((int) i); const auto& r = rs[i];
            const auto gc = destGroupColour(r.header ? r.name : destGroupFor(r.dest));
            if (r.header) {
                g.setColour(juce::Colours::white.withAlpha(0.05f)); g.fillRect(x0, y, gridR - x0 + labW, cellH);
                const auto ds = destsIn(r.name); int nConn = 0;
                for (auto& d : ds) for (int si = 0; si < nS; ++si) if (conn(d, false, sources[si]) >= 0) ++nConn;
                for (int si = 0; si < nS; ++si) { int n = 0; for (auto& d : ds) if (conn(d, false, sources[si]) >= 0) ++n;
                    if (n) { g.setColour(modSourceColour(sources[si]).withAlpha(open.contains(r.name) ? 0.3f : 0.9f)); g.fillRect(x0 + si * cellW + 2, y + 2, cellW - 4, cellH - 4);
                             g.setColour(juce::Colours::black); g.setFont(VVLookAndFeel::mono(12.f)); g.drawText(juce::String(n), x0 + si * cellW, y, cellW, cellH, juce::Justification::centred); } }
                g.setColour(gc); g.setFont(VVLookAndFeel::mono(14.f).boldened());
                g.drawText(juce::String(open.contains(r.name) ? juce::CharPointer_UTF8("\xE2\x96\xBE ") : juce::CharPointer_UTF8("\xE2\x96\xB8 ")) + r.name + " (" + juce::String(ds.size()) + (nConn ? " - " + juce::String(nConn) + " wired" : "") + ")",
                           gridR + 8, y, labW - 8, cellH, juce::Justification::centredLeft);
            } else {
                g.setColour(gc); g.fillRect(gridR + 2, y + 2, 4, cellH - 4);
                g.setFont(VVLookAndFeel::mono(13.f)); g.drawText(r.name, gridR + 12, y, labW - 12, cellH, juce::Justification::centredLeft);
                for (int si = 0; si < nS; ++si) {
                    const int ci = conn(r.dest, false, sources[si]); const juce::Rectangle<int> c(x0 + si * cellW, y, cellW, cellH);
                    g.setColour(juce::Colours::white.withAlpha(0.06f)); g.drawRect(c);
                    if (ci >= 0) { g.setColour(modSourceColour(sources[si]).withAlpha(0.85f)); g.fillRect(c.reduced(2));
                        g.setColour(juce::Colours::black); g.setFont(VVLookAndFeel::mono(11.f)); g.drawText(juce::String((int) store.num("connections." + juce::String(ci) + ".amount", 0)), c, juce::Justification::centred); }
                }
            }
        }
        // connector lines: source column header -> down -> rounded turn -> row label. Wires to a collapsed group end at its header.
        for (auto& gName : groupNames()) {
            int headerRow = -1; for (size_t i = 0; i < rs.size(); ++i) if (rs[i].header && rs[i].name == gName) headerRow = (int) i;
            const bool isOpen = open.contains(gName);
            for (auto& d : destsIn(gName)) {
                int target = headerRow; if (isOpen) for (size_t i = 0; i < rs.size(); ++i) if (!rs[i].header && rs[i].dest == d) target = (int) i;
                if (target < 0) continue;
                const float ry = rowY(target) + cellH * 0.5f;
                for (int si = 0; si < nS; ++si) {
                    if (conn(d, false, sources[si]) < 0) continue;
                    const float cx = x0 + si * cellW + cellW * 0.5f, cy = (float) (topBar + headH), lx = (float) gridR + 2;
                    const float rad = juce::jmin(10.f, std::abs(ry - cy) * 0.5f, std::abs(lx - cx) * 0.5f);
                    juce::Path p; p.startNewSubPath(cx, cy); p.lineTo(cx, ry - rad); p.quadraticTo(cx, ry, cx + rad, ry); p.lineTo(lx, ry);
                    const auto sc = modSourceColour(sources[si]), dc = destGroupColour(gName);
                    g.setGradientFill(juce::ColourGradient(sc, cx, cy, dc, lx, ry, false)); g.strokePath(p, juce::PathStrokeType(1.6f));
                    g.setColour(sc); g.fillEllipse(cx - 2.2f, cy - 2.2f, 4.4f, 4.4f); g.setColour(dc); g.fillEllipse(lx - 2.2f, ry - 2.2f, 4.4f, 4.4f);
                }
            }
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const auto rs = rows(); const int ri = (e.y - (topBar + headH)) / cellH;
        if (e.y < topBar + headH || ri < 0 || ri >= (int) rs.size()) return;
        const int gridR = sources.size() * cellW;
        if (rs[(size_t) ri].header) { if (open.contains(rs[(size_t) ri].name)) open.removeString(rs[(size_t) ri].name); else open.add(rs[(size_t) ri].name); relayout(); return; }
        const int si = e.x / cellW; if (e.x >= gridR || si < 0 || si >= sources.size()) return;
        const auto& d = rs[(size_t) ri].dest; const int ci = conn(d, false, sources[si]);
        if (ci >= 0) juce::MessageManager::callAsync([this, ci] { store.remove("connections", ci); });
        else juce::MessageManager::callAsync([this, s = sources[si], d] { auto* o = new juce::DynamicObject(); o->setProperty("source", s); o->setProperty("dest", d); o->setProperty("amount", 30); store.append("connections", juce::var(o)); });
    }

    Store& store; juce::StringArray sources; std::vector<std::pair<juce::String, juce::StringArray>> grp; juce::StringArray open; int lastCount = -1;
    juce::TextButton expand, collapse;
};

} // namespace vv
