#include "EqGraph.h"
#include <complex>
#include <vv/fx_basic.hpp>
namespace vv {
static const double kFr[4] = { 120, 600, 2500, 8000 };
static const double kLo = 30.0, kHi = 20000.0, kDragLo = 40.0, kDragHi = 16000.0, kRange = 18.0;   // drawn range / drag limits / dB half-range of the plot
static const juce::Colour kMag(0xffff3da6);   // the web's --vk-magenta
static const char* kNames[4] = { "LOW", "BELL", "BELL", "HIGH" };

EqGraph::EqGraph(Store& s, const juce::String& layerPath) : Control(s, layerPath), base(layerPath) { setWantsKeyboardFocus(true); for (int i = 0; i < 4; ++i) { freq[i] = kFr[i]; gain[i] = 0; } refresh(); }
void EqGraph::refresh() {
    for (int b = 0; b < 4; ++b) {
        const juce::String p = base + ".eq.band" + juce::String(b + 1);
        freq[b] = juce::jlimit(kDragLo, kDragHi, store.num(p + ".freq", kFr[b])); gain[b] = juce::jlimit(-15.0, 15.0, store.num(p + ".gain", 0));
    }
    repaint();
}
double EqGraph::magnitudeDb(const double* f, const double* g, double hz, double sr) {
    Biquad b[4]; b[0].setLowshelf(f[0], g[0], sr); b[1].setPeaking(f[1], g[1], 1, sr); b[2].setPeaking(f[2], g[2], 1, sr); b[3].setHighshelf(f[3], g[3], sr);
    const double w = 2 * 3.141592653589793 * hz / sr; const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2 * w); double db = 0;
    for (auto& q : b) { const std::complex<double> n = q.b0 + q.b1 * z1 + q.b2 * z2; const std::complex<double> d = 1.0 + q.a1 * z1 + q.a2 * z2; db += 20 * std::log10(std::max(1e-9, std::abs(n / d))); }
    return db;
}
float EqGraph::xOf(double hz) const { auto r = plot(); return r.getX() + (float) (std::log(hz / kLo) / std::log(kHi / kLo)) * r.getWidth(); }
float EqGraph::yOf(double db) const { auto r = plot(); return r.getCentreY() - (float) (db / kRange) * r.getHeight() * 0.5f; }
double EqGraph::hzAt(float x) const { auto r = plot(); return kLo * std::pow(kHi / kLo, juce::jlimit(0.f, 1.f, (x - r.getX()) / r.getWidth())); }
double EqGraph::dbAt(float y) const { auto r = plot(); return juce::jlimit(-15.0, 15.0, (double) (r.getCentreY() - y) / (r.getHeight() * 0.5) * kRange); }
void EqGraph::paint(juce::Graphics& g) {
    auto r = plot(); g.setColour(col::bg); g.fillRoundedRectangle(r, 6.f);
    g.setColour(col::edge); g.drawRoundedRectangle(r.reduced(0.5f), 6.f, 1.f);
    g.saveState(); g.reduceClipRegion(r.toNearestInt());
    const juce::Colour grid(0x47808696), zero(0x8c808696);   // rgba(128,134,150,.28) / .55 as in the web canvas
    g.setColour(grid);
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 }) g.drawVerticalLine((int) xOf(hz), r.getY(), r.getBottom());
    for (double db : { -12.0, -6.0, 6.0, 12.0 }) g.drawHorizontalLine((int) yOf(db), r.getX(), r.getRight());
    g.setColour(zero); g.drawHorizontalLine((int) yOf(0), r.getX(), r.getRight());
    g.setColour(juce::Colour(0xb3969cac)); g.setFont(VVLookAndFeel::mono(9.f));
    { const double lhz[3] = { 100.0, 1000.0, 10000.0 }; const char* ltx[3] = { "100", "1k", "10k" };
      for (int i = 0; i < 3; ++i) g.drawText(ltx[i], (int) xOf(lhz[i]) - 16, (int) r.getBottom() - 13, 32, 11, juce::Justification::centred); }
    juce::Path p; const int n = 128;
    for (int i = 0; i < n; ++i) { const double hz = hzAt(r.getX() + (float) i / (n - 1) * r.getWidth()); const float x = r.getX() + (float) i / (n - 1) * r.getWidth(), y = yOf(juce::jlimit(-24.0, 24.0, magnitudeDb(freq, gain, hz))); if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y); }
    g.setColour(kMag); g.strokePath(p, juce::PathStrokeType(2.5f));
    g.setFont(VVLookAndFeel::mono(11.f));
    for (int b = 0; b < 4; ++b) {
        const auto c = juce::Point<float>(xOf(freq[b]), yOf(gain[b]));
        g.setColour(b == drag ? kMag : col::bg); g.fillEllipse(c.x - 11.f, c.y - 11.f, 22.f, 22.f);
        g.setColour(kMag); g.drawEllipse(c.x - 11.f, c.y - 11.f, 22.f, 22.f, 2.f);
        g.setColour(col::ink); g.drawText(juce::String(b + 1), (int) c.x - 11, (int) c.y - 11, 22, 22, juce::Justification::centred);
    }
    g.restoreState();
    // readout: "1 LOW 120 Hz +3.0 dB" per band, bands with a non-zero gain highlighted (the web's .vk-eq-readout)
    static const char* names[4] = { "LOW", "BELL", "BELL", "HIGH" };
    auto ro = getLocalBounds().toFloat().removeFromBottom(22.f); g.setFont(VVLookAndFeel::mono(10.f));
    const float cw = ro.getWidth() / 4.f;
    for (int b = 0; b < 4; ++b) {
        const bool on = std::abs(gain[b]) > 0.01;
        g.setColour(on ? kMag : col::dim);
        g.drawText(juce::String(b + 1) + " " + names[b] + " " + juce::String((int) std::round(freq[b])) + " Hz " + (gain[b] > 0 ? "+" : "") + juce::String(gain[b], 1) + " dB", (int) (ro.getX() + b * cw), (int) ro.getY(), (int) cw, 22, juce::Justification::centred);
    }
}
int EqGraph::hit(juce::Point<float> pt) const { int best = 0; float bd = 1e9f; for (int b = 0; b < 4; ++b) { const float d = std::abs(xOf(freq[b]) - pt.x); if (d < bd) { bd = d; best = b; } } return best; }   // nearest band by horizontal distance, as the web does
void EqGraph::mouseDown(const juce::MouseEvent& e) { if (!plot().contains(e.position)) return; grabKeyboardFocus(); drag = hit(e.position); mouseDrag(e); repaint(); }
void EqGraph::mouseDrag(const juce::MouseEvent& e) {
    if (drag < 0) return;
    const double hz = std::round(juce::jlimit(kDragLo, kDragHi, hzAt(e.position.x))), db = std::round(dbAt(e.position.y) * 2.0) / 2.0;   // gain in 0.5 dB steps
    const juce::String p = base + ".eq.band" + juce::String(drag + 1); store.set(p + ".freq", hz); store.set(p + ".gain", db); freq[drag] = hz; gain[drag] = db; repaint();
}
void EqGraph::mouseUp(const juce::MouseEvent&) { if (drag >= 0) lastBand = drag; drag = -1; repaint(); }
void EqGraph::mouseDoubleClick(const juce::MouseEvent& e) { const int b = hit(e.position); if (b < 0) return; const juce::String p = base + ".eq.band" + juce::String(b + 1); store.set(p + ".freq", kFr[b]); store.set(p + ".gain", 0.0); refresh(); }
bool EqGraph::keyPressed(const juce::KeyPress& k) {   // arrow up / down nudge the last-dragged band's gain by 0.5 dB (the web's keyboard handling)
    const int b = lastBand; const juce::String p = base + ".eq.band" + juce::String(b + 1);
    if (k == juce::KeyPress::upKey) gain[b] = juce::jmin(15.0, gain[b] + 0.5); else if (k == juce::KeyPress::downKey) gain[b] = juce::jmax(-15.0, gain[b] - 0.5); else return false;
    store.set(p + ".gain", gain[b]); repaint(); return true;
}
}
