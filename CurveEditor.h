// Bezier curve editor (the web app's "DRAW SHAPE" / "DRAW CUSTOM SHAPE" overlay). Edits a preset array of {x, y, cx, cy} points:
// x, y in 0..1; (cx, cy) is the control point of the quadratic segment that STARTS at that node. First and last node are pinned in x.
//   click a node and drag  = move it         drag on the curve itself = bow that segment
//   click on empty space   = add a node      right-click / ctrl-click a node = delete it (end nodes stay)
#pragma once
#include "Widgets.h"

namespace vv {

struct CurvePoint { double x = 0, y = 0, cx = 0, cy = 0; bool hasC = false; };

inline std::vector<CurvePoint> readCurve(Store& st, const juce::String& path) {
    std::vector<CurvePoint> pts; auto v = st.get(path);
    if (v.isArray()) for (int i = 0; i < v.size(); ++i) { CurvePoint p; p.x = (double) v[i].getProperty("x", 0); p.y = (double) v[i].getProperty("y", 0); if (v[i].hasProperty("cx")) { p.cx = (double) v[i].getProperty("cx", 0); p.cy = (double) v[i].getProperty("cy", 0); p.hasC = true; } pts.push_back(p); }
    return pts;
}
inline void ensureControls(std::vector<CurvePoint>& pts) { for (size_t i = 0; i + 1 < pts.size(); ++i) if (!pts[i].hasC) { pts[i].cx = (pts[i].x + pts[i + 1].x) / 2; pts[i].cy = (pts[i].y + pts[i + 1].y) / 2; pts[i].hasC = true; } }
inline void writeCurve(Store& st, const juce::String& path, std::vector<CurvePoint> pts) {
    ensureControls(pts); juce::Array<juce::var> a;
    for (size_t i = 0; i < pts.size(); ++i) { auto* o = new juce::DynamicObject(); o->setProperty("x", pts[i].x); o->setProperty("y", pts[i].y); if (i + 1 < pts.size()) { o->setProperty("cx", pts[i].cx); o->setProperty("cy", pts[i].cy); } a.add(juce::var(o)); }
    st.set(path, juce::var(a));
}
inline std::vector<CurvePoint> defaultCurve() { std::vector<CurvePoint> p(4); p[0].x = 0; p[0].y = 0; p[1].x = 0.3; p[1].y = 1; p[2].x = 0.6; p[2].y = 0.4; p[3].x = 1; p[3].y = 0; ensureControls(p); return p; }
// the web's sampleBasicWaveformPoints: entering Custom starts from a recognisable version of the shape already selected
inline std::vector<CurvePoint> basicWaveformPoints(const juce::String& wf, int n = 9) {
    std::vector<CurvePoint> p;
    for (int i = 0; i < n; ++i) { const double ph = (double) i / (n - 1); double raw;
        if (wf == "triangle") raw = ph < 0.5 ? 4 * ph - 1 : 3 - 4 * ph; else if (wf == "pulse") raw = ph < 0.5 ? 1 : -1; else if (wf == "sawUp") raw = 2 * ph - 1; else if (wf == "sawDown") raw = 1 - 2 * ph;
        else raw = std::sin(2 * juce::MathConstants<double>::pi * ph);
        CurvePoint c; c.x = ph; c.y = (raw + 1) / 2; p.push_back(c); }
    ensureControls(p); return p;
}

class CurveEditor : public juce::Component {
public:
    CurveEditor(Store& s, const juce::String& path) : store(s), path_(path) {
        pts = readCurve(store, path_); if (pts.size() < 2) { pts = defaultCurve(); writeCurve(store, path_, pts); } ensureControls(pts);
        reset.setButtonText("RESET"); reset.onClick = [this] { pts = defaultCurve(); writeCurve(store, path_, pts); repaint(); }; addAndMakeVisible(reset);
        setSize(600, 290);
    }
    void resized() override { reset.setBounds(getLocalBounds().removeFromBottom(30).removeFromLeft(90).reduced(2)); }
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced(10).withTrimmedBottom(34); }
    juce::Point<float> toPx(double x, double y) const { auto a = area(); return { a.getX() + (float) x * a.getWidth(), a.getBottom() - (float) y * a.getHeight() }; }
    void paint(juce::Graphics& g) override {
        auto a = area(); g.setColour(juce::Colours::black); g.fillRect(a); g.setColour(juce::Colour(0xff222222)); for (int i = 0; i <= 10; ++i) g.drawVerticalLine((int) (a.getX() + a.getWidth() * i / 10.f), a.getY(), a.getBottom());
        juce::Path p; for (size_t i = 0; i < pts.size(); ++i) { auto q = toPx(pts[i].x, pts[i].y); if (i == 0) p.startNewSubPath(q); else { auto c = toPx(pts[i - 1].cx, pts[i - 1].cy); p.quadraticTo(c, q); } }
        g.setColour(juce::Colour(0xff9b6bff)); g.strokePath(p, juce::PathStrokeType(2.f));
        for (size_t i = 0; i + 1 < pts.size(); ++i) { auto c = toPx(pts[i].cx, pts[i].cy); g.setColour(juce::Colour(0x599b6bff)); g.fillEllipse(c.x - 3, c.y - 3, 6, 6); }
        for (size_t i = 0; i < pts.size(); ++i) { auto q = toPx(pts[i].x, pts[i].y); g.setColour(i == 0 || i + 1 == pts.size() ? col::cyan : juce::Colour(0xff9b6bff)); g.fillEllipse(q.x - 5, q.y - 5, 10, 10); }
        g.setColour(col::dim); g.setFont(VVLookAndFeel::mono(10.f)); g.drawText("drag a node to move it, drag the line to bow it, click empty space to add a node, right-click a node to remove it", getLocalBounds().removeFromBottom(30).withTrimmedLeft(100), juce::Justification::centredLeft);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        auto a = area(); if (!a.contains(e.position)) return;
        const double x = (e.position.x - a.getX()) / a.getWidth(), y = 1 - (e.position.y - a.getY()) / a.getHeight();
        int closest = -1; double best = 0.05; for (size_t i = 0; i < pts.size(); ++i) { const double d = std::hypot(pts[i].x - x, pts[i].y - y); if (d < best) { best = d; closest = (int) i; } }
        if (e.mods.isPopupMenu() || e.mods.isCtrlDown()) { if (closest > 0 && closest + 1 < (int) pts.size()) { pts.erase(pts.begin() + closest); commit(); } return; }
        dragPt = dragSeg = -1;
        if (closest >= 0) { dragPt = closest; return; }
        int seg = -1; double bd = 0.035;
        for (size_t i = 0; i + 1 < pts.size(); ++i) for (int k = 0; k <= 20; ++k) { const double t = k / 20.0, mt = 1 - t;
            const double px = mt * mt * pts[i].x + 2 * mt * t * pts[i].cx + t * t * pts[i + 1].x, py = mt * mt * pts[i].y + 2 * mt * t * pts[i].cy + t * t * pts[i + 1].y; const double d = std::hypot(px - x, py - y); if (d < bd) { bd = d; seg = (int) i; } }
        if (seg >= 0) { dragSeg = seg; return; }
        CurvePoint np; np.x = juce::jlimit(0.0, 1.0, x); np.y = juce::jlimit(0.0, 1.0, y); pts.push_back(np); std::sort(pts.begin(), pts.end(), [](const CurvePoint& l, const CurvePoint& r) { return l.x < r.x; });
        size_t ni = 0; for (size_t i = 0; i < pts.size(); ++i) if (&pts[i] && pts[i].x == np.x && pts[i].y == np.y) { ni = i; break; }
        pts[ni].hasC = false; if (ni > 0) pts[ni - 1].hasC = false; ensureControls(pts); commit();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragPt < 0 && dragSeg < 0) return; auto a = area();
        const double x = juce::jlimit(0.0, 1.0, (double) ((e.position.x - a.getX()) / a.getWidth())), y = juce::jlimit(0.0, 1.0, 1.0 - (double) ((e.position.y - a.getY()) / a.getHeight()));
        if (dragPt >= 0) { const bool pinned = dragPt == 0 || dragPt + 1 == (int) pts.size(); pts[(size_t) dragPt].y = y; if (!pinned) pts[(size_t) dragPt].x = x; }
        else { pts[(size_t) dragSeg].cx = x; pts[(size_t) dragSeg].cy = y; }
        commit();
    }
    void mouseUp(const juce::MouseEvent&) override { dragPt = dragSeg = -1; }
private:
    void commit() { writeCurve(store, path_, pts); repaint(); }
    Store& store; juce::String path_; std::vector<CurvePoint> pts; int dragPt = -1, dragSeg = -1; juce::TextButton reset;
};

// A panel control: "DRAW SHAPE" button that opens the curve editor for a store path (call-out).
struct CurveButton : Control {
    CurveButton(Store& s, const juce::String& pointsPath, const juce::String& label) : Control(s, pointsPath) { b.setButtonText(label); b.onClick = [this] {
        auto* ed = new CurveEditor(store, path_); juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(ed), b.getScreenBounds(), nullptr); }; addAndMakeVisible(b); }
    void refresh() override {}
    void resized() override { b.setBounds(getLocalBounds().reduced(2)); }
    juce::Point<int> preferredSize() const override { return { 150, 34 }; }
    juce::TextButton b;
};

} // namespace vv
