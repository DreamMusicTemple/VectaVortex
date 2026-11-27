// "Pro" plots for the voice filter and the envelopes: the same look as the pitch-envelope editor (dark inset plot, mono 9px axis labels,
// major/minor grid, glowing accent line over a soft gradient fill, green playhead-colour reserved for live markers). Untested (no JUCE build here);
// the maths lives in Core/include/vv/viz.hpp and is covered by the r22 core test.
#pragma once
#include "Widgets.h"
#include <vv/viz.hpp>

namespace vv {

struct ProPlotBase : public juce::Component, protected Store::Listener {
    explicit ProPlotBase(Store& s) : store(s) { store.addListener(this); }
    ~ProPlotBase() override { store.removeListener(this); }
    static constexpr float kL = 34.f, kR = 10.f, kT = 10.f, kB = 20.f;
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().withTrimmedLeft(kL).withTrimmedRight(kR).withTrimmedTop(kT).withTrimmedBottom(kB); }
    juce::Font mono() const { return VVLookAndFeel::mono(9.f); }
    void storeChanged() override { repaint(); }
    void beginPlot(juce::Graphics& g) const { g.setColour(juce::Colours::black.withAlpha(0.55f)); g.fillRect(plot()); }
    void hLine(juce::Graphics& g, float y, bool major, const juce::String& label) const {
        const auto a = plot(); g.setColour(juce::Colour(0xff808696).withAlpha(major ? 0.5f : 0.22f)); g.drawHorizontalLine((int) y, a.getX(), a.getRight());
        if (label.isNotEmpty()) { g.setColour(juce::Colour(0xff969cac).withAlpha(0.75f)); g.setFont(mono()); g.drawText(label, juce::Rectangle<float>(0.f, y - 6.f, kL - 4.f, 12.f), juce::Justification::centredRight); }
    }
    void vLine(juce::Graphics& g, float x, bool major, const juce::String& label) const {
        const auto a = plot(); g.setColour(juce::Colour(0xff808696).withAlpha(major ? 0.32f : 0.13f)); g.drawVerticalLine((int) x, a.getY(), a.getBottom());
        if (label.isNotEmpty() && (major || a.getWidth() > 340.f)) { g.setColour(juce::Colour(0xff969cac).withAlpha(0.75f)); g.setFont(mono()); g.drawText(label, juce::Rectangle<float>(x - 22.f, a.getBottom() + 4.f, 44.f, 12.f), juce::Justification::centred); }
    }
    /// fn(x 0..1) -> y 0..1; soft fill, glowing accent line, thin top highlight (vvProCurve).
    template <class F> void curve(juce::Graphics& g, F fn) const {
        const auto a = plot(); const int N = juce::jmax(64, (int) a.getWidth()); juce::Path line;
        for (int i = 0; i <= N; ++i) { const double x = (double) i / N, y = juce::jlimit(-0.15, 1.15, (double) fn(x)); const float px = a.getX() + (float) x * a.getWidth(), py = a.getBottom() - (float) y * a.getHeight(); if (i) line.lineTo(px, py); else line.startNewSubPath(px, py); }
        juce::Graphics::ScopedSaveState ss(g); g.reduceClipRegion(a.expanded(0.f, 4.f).toNearestInt());
        juce::Path fill(line); fill.lineTo(a.getRight(), a.getBottom()); fill.lineTo(a.getX(), a.getBottom()); fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(col::cyan.withAlpha(0.33f), 0.f, a.getY(), col::cyan.withAlpha(0.02f), 0.f, a.getBottom(), false)); g.fillPath(fill);
        g.setColour(col::cyan.withAlpha(0.28f)); g.strokePath(line, juce::PathStrokeType(5.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));   // glow
        g.setColour(col::cyan); g.strokePath(line, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(juce::Colours::white.withAlpha(0.28f)); g.strokePath(line, juce::PathStrokeType(0.9f), juce::AffineTransform::translation(0.f, -1.f));
    }
    static juce::String hz(double f) { return f >= 1000 ? juce::String(std::round(f / 100) / 10, 1) + "k" : juce::String((int) std::round(f)); }
    static juce::String secs(double t) { return t < 1 ? juce::String(t, 2) + "s" : juce::String(t, t < 10 ? 1 : 0) + "s"; }
    Store& store;
};

/// Filter response: 20 Hz..20 kHz (log) x -36..+12 dB, cutoff marker + peak dot + readout. Drag = cutoff (x) and resonance (y), like the web graph.
class FilterVizView : public ProPlotBase {
public:
    FilterVizView(Store& s, juce::String prefix) : ProPlotBase(s), pre(std::move(prefix)) { setSize(400, 170); }
    void paint(juce::Graphics& g) override {
        const auto a = plot(); beginPlot(g);
        for (int f : { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 }) vLine(g, a.getX() + (float) viz::xOfHz(f) * a.getWidth(), f == 100 || f == 1000 || f == 10000, hz(f));
        for (int db : { -36, -24, -12, 0, 12 }) hLine(g, a.getBottom() - (float) ((db + 36) / 48.0) * a.getHeight(), db == 0, (db > 0 ? "+" : "") + juce::String(db));
        const auto type = store.str(pre + ".type", "moog4pole"); const double fc = store.num(pre + ".cutoff", 4000), res = store.num(pre + ".reso", 20);
        curve(g, [&](double x) { return (viz::filterMagnitudeDb(type.toStdString(), fc, res, viz::hzAt(x)) + 36.0) / 48.0; });
        const float cx = a.getX() + (float) juce::jlimit(0.0, 1.0, viz::xOfHz(fc)) * a.getWidth();
        g.setColour(juce::Colour(0xffff3da6)); const float dash[] = { 4.f, 3.f }; g.drawDashedLine(juce::Line<float>(cx, a.getY(), cx, a.getBottom()), dash, 2, 1.2f);
        const double pk = viz::filterMagnitudeDb(type.toStdString(), fc, res, fc), py = a.getBottom() - (float) juce::jlimit(0.0, 1.0, (pk + 36) / 48) * a.getHeight();
        g.setColour(juce::Colour(0xff4aff8a)); g.fillEllipse(cx - 4.f, (float) py - 4.f, 8.f, 8.f); g.setColour(juce::Colours::black.withAlpha(0.6f)); g.drawEllipse(cx - 4.f, (float) py - 4.f, 8.f, 8.f, 1.f);
        g.setColour(juce::Colour(0xff969cac).withAlpha(0.85f)); g.setFont(mono());
        g.drawText("fc " + hz(fc) + " Hz  -  res " + juce::String((int) std::round(res)) + "  -  " + (pk >= 0 ? "+" : "") + juce::String(pk, 1) + " dB", a.reduced(4.f, 3.f).withHeight(12.f), juce::Justification::topRight);
    }
    void mouseDown(const juce::MouseEvent& e) override { drag(e); }
    void mouseDrag(const juce::MouseEvent& e) override { drag(e); }
private:
    void drag(const juce::MouseEvent& e) { const auto a = plot(); const double x = juce::jlimit(0.0, 1.0, (e.position.x - a.getX()) / a.getWidth()), y = juce::jlimit(0.0, 1.0, 1.0 - (e.position.y - a.getY()) / a.getHeight());
        store.set(pre + ".cutoff", viz::hzAt(x)); store.set(pre + ".reso", y * 130.0); }
    juce::String pre;
};

/// Envelope plot (amp: 0..100 %, filter: signed Hz from the env amount). Stage dividers + A/D/S/R letters and a readout.
class EnvVizView : public ProPlotBase {
public:
    EnvVizView(Store& s, juce::String prefix, juce::String amountPath = {}) : ProPlotBase(s), pre(std::move(prefix)), amtPath(std::move(amountPath)) { setSize(400, amtPath.isEmpty() ? 120 : 140); }
    void paint(juce::Graphics& g) override {
        const auto a = plot(); beginPlot(g);
        const auto e = viz::envShape(store.num(pre + ".a", 0.01), store.num(pre + ".d", 0.2), store.num(pre + ".s", amtPath.isEmpty() ? 0.7 : 0.3), store.num(pre + ".r", 0.3));
        const bool amp = amtPath.isEmpty(); const double A = amp ? 1.0 : store.num(amtPath, 0.0);
        const double lo = amp ? 0.0 : juce::jmin(0.0, A), hi = amp ? 1.0 : (A > 0 ? A : (A < 0 ? 0.0 : 1000.0)), span = (hi - lo) != 0 ? hi - lo : 1.0;
        auto fmtY = [&](double v) { return amp ? juce::String((int) std::round(v * 100)) + "%" : juce::String(v > 0 ? "+" : "") + (v == 0 ? juce::String("0") : std::abs(v) >= 1000 ? juce::String(v / 1000, 1) + "k" : juce::String((int) std::round(v))); };
        const double step = viz::envGridStep(e.total);
        for (double t = 0; t <= e.total + 1e-9; t += step / 2) { const int k = (int) std::round(t / step * 2); vLine(g, a.getX() + (float) (t / e.total) * a.getWidth(), k % 2 == 0, k % 2 == 0 ? secs(t) : juce::String()); }
        for (double f : { 0.0, 0.25, 0.5, 0.75, 1.0 }) hLine(g, a.getBottom() - (float) f * a.getHeight(), f == 0 || f == 0.5 || f == 1, fmtY(lo + f * span));
        curve(g, [&](double u) { const double l = viz::envLevel(e, u * e.total); return ((amp ? l : A * l) - lo) / span; });
        const double edges[5] = { 0, e.a, e.a + e.d, e.a + e.d + e.hold, e.total }; const char* nm[4] = { "A", "D", "S", "R" };
        g.setColour(juce::Colour(0xff969cac).withAlpha(0.38f)); const float dash[] = { 3.f, 3.f };
        for (int i = 1; i <= 3; ++i) { const float x = a.getX() + (float) (edges[i] / e.total) * a.getWidth(); g.drawDashedLine(juce::Line<float>(x, a.getY(), x, a.getBottom()), dash, 2, 1.f); }
        g.setColour(juce::Colour(0xff969cac).withAlpha(0.85f)); g.setFont(mono());
        for (int i = 0; i < 4; ++i) { const float x0 = a.getX() + (float) (edges[i] / e.total) * a.getWidth(), x1 = a.getX() + (float) (edges[i + 1] / e.total) * a.getWidth(); if (x1 - x0 > 16.f) g.drawText(nm[i], juce::Rectangle<float>(x0, a.getY() + 17.f, x1 - x0, 12.f), juce::Justification::centred); }
        g.setColour(juce::Colour(0xff969cac).withAlpha(0.7f));
        g.drawText(juce::String(amp ? "" : "Hz - ") + "A " + secs(e.a) + "  D " + secs(e.d) + "  S " + juce::String((int) std::round(e.s * 100)) + "%  R " + secs(e.r), a.reduced(4.f, 3.f).withHeight(12.f), juce::Justification::topRight);
    }
private:
    juce::String pre, amtPath;
};

}   // namespace vv
