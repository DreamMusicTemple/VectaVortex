#include "WarpView.h"
#include <cmath>

namespace vv {
namespace {
const juce::Colour CY(0xff36d8ff), OR(0xffffb347), MG(0xffff4fd8), GD(0xffffd24a);
const char* kModes[] = { "fold", "mirror", "sine", "pwm" };
const char* kModeLabels[] = { "FOLD", "MIRROR", "SINE", "PWM" };
constexpr float PW = 205.f, PH = 132.f;
struct PanelPos { float x, y; };
const PanelPos kPanels[4] = { { 8, 8 }, { 640 - PW - 8, 8 }, { 8, 400 - PH - 8 }, { 640 - PW - 8, 400 - PH - 8 } };
float c01(float v) { return juce::jlimit(0.f, 1.f, v); }

float shape(float u, const juce::String& mode, float pwm) {   // same shaping maths as the DSP
    if (mode == "sine") return std::sin(u * 1.5707963f);
    if (mode == "mirror") u = std::abs(u) * 2.f - 1.f;
    float y = u; int n = 0; while ((y > 1.f || y < -1.f) && n < 32) { y = y > 1.f ? 2.f - y : -2.f - y; ++n; }
    if (mode == "pwm") return y > (0.5f - pwm) * 2.f ? 0.9f : -0.9f;
    return y;
}
void glow(juce::Graphics& g, float x, float y, float r, juce::Colour c, float a) {
    if (a <= 0.01f) return;
    g.setGradientFill(juce::ColourGradient(c.withAlpha(juce::jmin(1.f, a)), x, y, c.withAlpha(0.f), x + r, y, true)); g.fillEllipse(x - r, y - r, 2 * r, 2 * r);
}
void thruster(juce::Graphics& g, float x, float y, float r, juce::Colour c, float p, float t) {
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff59616c), x - r * 0.3f, y - r * 0.3f, juce::Colour(0xff14181f), x + r, y + r, true)); g.fillEllipse(x - r * 1.12f, y - r * 1.12f, r * 2.24f, r * 2.24f);
    g.setColour(juce::Colour(0xff2a3340)); g.drawEllipse(x - r * 1.12f, y - r * 1.12f, r * 2.24f, r * 2.24f, 1.2f);
    const float fl = 0.85f + 0.15f * std::sin(t * 14.f + x * 0.7f + y);
    glow(g, x, y, r * (1.6f + 1.6f * p), c, (0.25f + 0.75f * p) * fl);
    const float cr = r * (0.28f + 0.3f * p); g.setColour(juce::Colours::white.withAlpha(0.15f + 0.7f * p * fl)); g.fillEllipse(x - cr, y - cr, 2 * cr, 2 * cr);
}
void scope(juce::Graphics& g, float x, float y, float w, float h, juce::Colour c, float t, const std::function<float(float)>& fn) {
    g.setColour(juce::Colours::black.withAlpha(0.45f)); g.fillRect(x, y, w, h); g.setColour(juce::Colours::white.withAlpha(0.12f)); g.drawRect(x, y, w, h, 1.f);
    g.setColour(juce::Colours::white.withAlpha(0.1f)); g.drawHorizontalLine((int) (y + h / 2), x, x + w);
    juce::Path p; for (int i = 0; i <= (int) w; ++i) { const float ph = (float) i / w * 4.f * juce::MathConstants<float>::pi + t * 2.f; const float v = fn(std::sin(ph)); const float py = y + h / 2 - v * (h / 2 - 3); i == 0 ? p.startNewSubPath(x + i, py) : p.lineTo(x + i, py); }
    g.setColour(c); g.strokePath(p, juce::PathStrokeType(1.6f));
}
void modBar(juce::Graphics& g, float x, float y, float w, juce::Colour c, float live) {
    g.setColour(juce::Colours::white.withAlpha(0.1f)); g.fillRect(x, y, w, 3.f); g.setColour(c); g.fillEllipse(x + w * live - 2.4f, y - 0.9f, 4.8f, 4.8f);
}
void panel(juce::Graphics& g, int idx, juce::Colour c, const juce::String& letter, const juce::String& title, const juce::String& sub, float active) {
    const auto P = kPanels[idx];
    g.setColour(juce::Colour::fromFloatRGBA(0.03f, 0.05f, 0.09f, 0.88f)); g.fillRoundedRectangle(P.x, P.y, PW, PH, 8.f);
    g.setColour(c.withAlpha(0.35f + 0.65f * active)); g.drawRoundedRectangle(P.x, P.y, PW, PH, 8.f, 1.6f);
    g.setColour(c); g.drawEllipse(P.x + 5, P.y + 5, 16, 16, 1.2f);
    g.setFont(juce::FontOptions(10.f, juce::Font::bold)); g.drawText(letter, juce::Rectangle<float>(P.x + 5, P.y + 5, 16, 16), juce::Justification::centred);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold)); g.drawText(title, juce::Rectangle<float>(P.x + 27, P.y + 6, PW - 30, 14), juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain)); g.setColour(active > 0.15f ? c : juce::Colour(0xff666677));
    g.drawText(sub, juce::Rectangle<float>(P.x + 8, P.y + PH - 18, PW - 90, 12), juce::Justification::centredLeft);
    g.drawText(active > 0.15f ? "ONLINE" : "STANDBY", juce::Rectangle<float>(P.x + PW - 80, P.y + PH - 18, 72, 12), juce::Justification::centredRight);
}
} // namespace

WarpView::WarpView(Store& s, juce::String stageBase) : store(s), base(std::move(stageBase)) {
    juce::Random r(98765); for (int i = 0; i < 110; ++i) stars.push_back({ r.nextFloat() * W, r.nextFloat() * H, 0.4f + r.nextFloat() * 1.3f, r.nextFloat() * 6.28f });
    setOpaque(true); startTimerHz(30);
}

juce::AffineTransform WarpView::fit() const {
    const float sc = juce::jmin((float) getWidth() / W, (float) getHeight() / H);
    return juce::AffineTransform::scale(sc).translated(((float) getWidth() - W * sc) * 0.5f, ((float) getHeight() - H * sc) * 0.5f);
}
juce::Point<float> WarpView::toScene(juce::Point<float> p) const { return p.transformedBy(fit().inverted()); }

void WarpView::step(float dt) {
    t += dt;
    const juce::String q = base + ".params.";
    mode = store.str(q + "mode"); if (mode.isEmpty()) mode = "fold";
    const float tD = c01((float) (store.num(q + "drive", 3) - 1) / 19.f), tS = juce::jlimit(-1.f, 1.f, (float) store.num(q + "symmetry", 0)), tM = c01((float) store.num(q + "mix", 1)), tW = juce::jlimit(0.02f, 0.98f, (float) store.num(q + "pwm", 0.5));
    const float k = 1.f - std::exp(-dt * 9.f); cd += (tD - cd) * k; cs += (tS - cs) * k; cm += (tM - cm) * k; cw += (tW - cw) * k;
    drive = 1.f + cd * 19.f; sym = cs; mix = cm; pwm = cw;
    const float aA = cd, aB = std::abs(sym), aC = mode == "pwm" ? c01(0.3f + 0.7f * std::abs(pwm - 0.5f) * 2.f) : c01((drive - 1.f) / 9.f), aD = mix;
    power = c01(0.12f + 0.88f * (0.35f * aA + 0.15f * aB + 0.3f * aC + 0.2f * aD) * (0.4f + 0.6f * mix));
    spin += dt * (0.35f + 2.4f * cd) * (sym < 0 ? -1.f : 1.f) * (0.6f + 0.4f * juce::jmax(0.2f, std::abs(sym) * 2.f));
}

void WarpView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff04050e));
    g.addTransform(fit()); g.reduceClipRegion(0, 0, (int) W, (int) H);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff05081a), 0, 0, juce::Colour(0xff04050e), 0, H, false)); g.fillRect(0.f, 0.f, W, H);
    for (auto& s : stars) { g.setColour(juce::Colours::white.withAlpha(0.35f + 0.35f * std::sin(t * 1.5f + s.ph))); g.fillRect(s.x, s.y, s.r, s.r); }
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffaeb8c8), 262, 40, juce::Colour(0xff39425a), 282, 40, true)); g.fillEllipse(245, 23, 34, 34);
    g.setColour(juce::Colour::fromFloatRGBA(0.27f, 0.33f, 0.43f, 0.55f)); g.drawEllipse(320 - 118, 205 - 118, 236, 236, 10.f);
    g.setColour(juce::Colour::fromFloatRGBA(0.12f, 0.15f, 0.22f, 0.9f)); g.drawEllipse(320 - 108, 205 - 108, 216, 216, 4.f);
    // central time-space vortex
    { const float cx = 320, cy = 205, R = 96, pw = power;
      glow(g, cx, cy, R * 1.25f, juce::Colour(0xff6a3dff), 0.25f + 0.4f * pw);
      const float hues[5] = { 190, 215, 270, 310, 25 };
      for (int a = 0; a < 5; ++a) { juce::Path p; for (int j = 0; j <= 64; ++j) { const float u = (float) j / 64.f, r = R * (0.05f + 0.95f * u), an = (float) a * 6.2832f / 5.f + spin + u * (2.6f + 2.2f * pw);
            const float px = cx + std::cos(an) * r, py = cy + std::sin(an) * r * 0.96f; j == 0 ? p.startNewSubPath(px, py) : p.lineTo(px, py); }
        g.setColour(juce::Colour::fromHSL(hues[a] / 360.f, 1.f, juce::jmin(0.9f, 0.55f + 0.15f * pw), 0.22f + 0.5f * pw)); g.strokePath(p, juce::PathStrokeType(2.f + 4.f * pw)); }
      for (int i = 0; i < 4; ++i) { const float u = std::fmod(t * 0.35f * (0.4f + pw) + (float) i / 4.f, 1.f); g.setColour(juce::Colour::fromFloatRGBA(0.63f, 0.78f, 1.f, (1.f - u) * 0.35f * mix * (0.4f + pw))); const float rr = R * 0.2f + R * 0.85f * u; g.drawEllipse(cx - rr, cy - rr, 2 * rr, 2 * rr, 1.2f); }
      for (int i = 0; i < 46; ++i) { const float u = 1.f - std::fmod(t * 0.25f * (0.5f + pw) + (float) i * 0.137f, 1.f), an = (float) i * 2.399f + spin * 1.3f + u * 3.f, r = R * u;
        g.setColour(juce::Colour::fromHSL((200.f + std::fmod((float) i * 3.f, 120.f)) / 360.f, 1.f, 0.75f, 0.6f * pw * (1.f - u * 0.5f))); g.fillRect(cx + std::cos(an) * r, cy + std::sin(an) * r * 0.96f, 1.6f, 1.6f); }
      glow(g, cx, cy, R * 0.34f, juce::Colours::white, 0.35f + 0.6f * pw); }
    const float aA = cd, aB = std::abs(sym), aC = mode == "pwm" ? c01(0.3f + 0.7f * std::abs(pwm - 0.5f) * 2.f) : c01((drive - 1.f) / 9.f), aD = mix;
    const juce::Colour pc[4] = { CY, OR, MG, GD }; const float pa[4] = { aA, aB, aC, aD };
    for (int i = 0; i < 4; ++i) { const float a = juce::MathConstants<float>::pi / 4.f + (float) i * juce::MathConstants<float>::halfPi; thruster(g, 320 + std::cos(a) * 112.f, 205 + std::sin(a) * 112.f, 17.f, pc[i], 0.15f + 0.85f * pa[i], t); }
    g.setColour(juce::Colour(0xffe9eefc)); g.setFont(juce::FontOptions(11.f, juce::Font::bold));
    g.drawText("WARP CHRONOLOGY", juce::Rectangle<float>(220, 68, 200, 14), juce::Justification::centred); g.drawText("CONVERGENCE", juce::Rectangle<float>(220, 82, 200, 14), juce::Justification::centred);
    g.setColour(juce::Colour(0xff9fb4ff)); g.setFont(juce::FontOptions(9.f, juce::Font::bold)); g.drawText("SPACETIME FOLDING DRIVE", juce::Rectangle<float>(200, 326, 240, 12), juce::Justification::centred);
    for (int i = 0; i < 4; ++i) { const bool on = mode == kModes[i]; const juce::Rectangle<float> r(222.f + (float) i * 50.f, 344.f, 46.f, 16.f);
        g.setColour(on ? MG.withAlpha(0.28f) : juce::Colours::white.withAlpha(0.05f)); g.fillRoundedRectangle(r, 8.f); g.setColour(on ? MG : juce::Colours::white.withAlpha(0.2f)); g.drawRoundedRectangle(r, 8.f, 1.f);
        g.setColour(on ? juce::Colours::white : juce::Colour(0xff8a93b5)); g.setFont(juce::FontOptions(9.5f, juce::Font::bold)); g.drawText(kModeLabels[i], r, juce::Justification::centred); }
    g.setColour(juce::Colour(0xff9fb4ff)); g.setFont(juce::FontOptions(10.f, juce::Font::bold)); g.drawText("WARP POWER " + juce::String(juce::roundToInt(power * 100.f)) + "%", juce::Rectangle<float>(220, 374, 200, 14), juce::Justification::centred);
    const juce::String q = base + ".params.";
    const float baseDrive = (float) store.num(q + "drive", 3), baseSym = (float) store.num(q + "symmetry", 0), baseMix = (float) store.num(q + "mix", 1), basePwm = (float) store.num(q + "pwm", 0.5);
    juce::ignoreUnused(baseDrive, baseSym, baseMix, basePwm);
    auto shp = [this](float v) { return shape(v * drive + sym, mode, pwm); };
    // A drive
    panel(g, 0, CY, "A", "Phase I: Sub-Plasma Pre-Ignition", "DRIVE x" + juce::String(drive, 1), aA);
    { const float pos[4][2] = { { 34, 58 }, { 78, 58 }, { 34, 100 }, { 78, 100 } }; for (int i = 0; i < 4; ++i) thruster(g, kPanels[0].x + pos[i][0], kPanels[0].y + pos[i][1], 14.f, CY, 0.12f + 0.88f * aA * (0.85f + 0.15f * std::sin(t * 3.f + (float) i)), t); }
    scope(g, kPanels[0].x + 110, kPanels[0].y + 30, 86, 60, CY, t, [](float v) { return v; }); modBar(g, kPanels[0].x + 110, kPanels[0].y + 100, 86, CY, c01((drive - 1.f) / 19.f));
    // B symmetry
    panel(g, 1, OR, "B", "Phase II: Core Ignition & Charge", "SYMMETRY " + juce::String(sym, 2), aB);
    { const float pos[4][3] = { { 34, 58, -1 }, { 78, 58, 1 }, { 34, 100, -1 }, { 78, 100, 1 } }; for (int i = 0; i < 4; ++i) thruster(g, kPanels[1].x + pos[i][0], kPanels[1].y + pos[i][1], 14.f, OR, 0.12f + 0.88f * c01(0.25f * aB + 0.75f * juce::jmax(0.f, pos[i][2] * sym)), t); }
    scope(g, kPanels[1].x + 110, kPanels[1].y + 30, 86, 60, OR, t, [this](float v) { return shape(v + sym, (mode == "mirror" || mode == "sine" || mode == "pwm") ? "fold" : mode, pwm); });
    modBar(g, kPanels[1].x + 110, kPanels[1].y + 100, 86, OR, (sym + 1.f) / 2.f);
    // C fold
    const int mi = mode == "mirror" ? 1 : mode == "sine" ? 2 : mode == "pwm" ? 3 : 0;
    panel(g, 2, MG, "C", "Phase III: Plasma Stabilization / FOLD", mode == "pwm" ? "PWM " + juce::String(juce::roundToInt(pwm * 100.f)) + "%" : juce::String(kModeLabels[mi]) + " - FOLDS ~ " + juce::String(juce::jmax(0, juce::roundToInt(drive - 1.f))), aC);
    { const float cx = kPanels[2].x + 56, cy = kPanels[2].y + 68; const int rings = juce::jlimit(1, 12, juce::roundToInt(drive));
      glow(g, cx, cy, 52, MG, 0.12f + 0.5f * aC);
      for (int i = 0; i < rings; ++i) { const float r = 6.f + (40.f / (float) rings) * (float) (i + 1) * (0.8f + 0.2f * std::sin(t * 3.f + (float) i)); const float a = 0.25f + 0.55f * (1.f - (float) i / (float) rings);
        g.setColour(juce::Colour::fromFloatRGBA(1.f, 0.35f, 0.88f, a)); juce::Path p;
        if (mode == "sine") p.addEllipse(cx - r, cy - r, 2 * r, 2 * r);
        else if (mode == "pwm") { const float hw = r * (0.25f + 1.5f * pwm); p.addRectangle(cx - hw, cy - r * 0.8f, hw * 2, r * 1.6f); }
        else if (mode == "mirror") { p.startNewSubPath(cx - 1, cy - r); p.lineTo(cx - r, cy); p.lineTo(cx - 1, cy + r); p.startNewSubPath(cx + 1, cy - r); p.lineTo(cx + r, cy); p.lineTo(cx + 1, cy + r); }
        else { p.startNewSubPath(0, -r); p.lineTo(r, 0); p.lineTo(0, r); p.lineTo(-r, 0); p.closeSubPath(); p.applyTransform(juce::AffineTransform::rotation(t * 0.25f * (0.3f + aC)).translated(cx, cy)); }
        g.strokePath(p, juce::PathStrokeType(1.4f)); }
      glow(g, cx, cy, 12, juce::Colours::white, 0.4f + 0.5f * aC); }
    scope(g, kPanels[2].x + 110, kPanels[2].y + 30, 86, 60, MG, t, shp); modBar(g, kPanels[2].x + 110, kPanels[2].y + 100, 86, MG, mode == "pwm" ? pwm : c01((drive - 1.f) / 19.f));
    // D mix
    panel(g, 3, GD, "D", "Phase IV: Full Quantum Thrust / MIX", "MIX " + juce::String(juce::roundToInt(mix * 100.f)) + "%", aD);
    { const float sx = kPanels[3].x + 62, sy = kPanels[3].y + 68, s = 1.15f; juce::Graphics::ScopedSaveState ss(g); g.addTransform(juce::AffineTransform::scale(s).translated(sx, sy));
      glow(g, 0, 0, 52, GD, 0.12f + 0.4f * mix);
      for (int i = 0; i < 4; ++i) { const float ey = -10.f + (float) i * 7.f, L = (8.f + 36.f * mix) * (0.85f + 0.15f * std::sin(t * 20.f + (float) i));
        g.setGradientFill(juce::ColourGradient(juce::Colour::fromFloatRGBA(1.f, 0.94f, 0.67f, 0.95f), -22, ey, juce::Colour::fromFloatRGBA(1.f, 0.55f, 0.1f, 0.f), -22 - L, ey, false));
        juce::Path f; f.addTriangle(-22, ey - 2.6f, -22 - L, ey, -22, ey + 2.6f); g.fillPath(f); }
      juce::Path hull; hull.startNewSubPath(-24, -13); for (auto pt : { juce::Point<float>(8, -14), { 34, -4 }, { 38, 0 }, { 34, 4 }, { 8, 14 }, { -24, 13 } }) hull.lineTo(pt); hull.closeSubPath();
      g.setGradientFill(juce::ColourGradient(juce::Colour(0xff8a7340), 0, -16, juce::Colour(0xff17140d), 0, 16, false)); g.fillPath(hull); g.setColour(juce::Colour(0xffd9b54e)); g.strokePath(hull, juce::PathStrokeType(1.f));
      for (int i = 0; i < 4; ++i) { g.setColour(juce::Colour::fromFloatRGBA(1.f, 0.86f, 0.47f, 0.4f + 0.6f * mix)); g.fillEllipse(-24.6f, -12.6f + (float) i * 7.f, 5.2f, 5.2f); } }
    scope(g, kPanels[3].x + 110, kPanels[3].y + 30, 86, 60, GD, t, [this, shp](float v) { return (1.f - mix) * v + mix * shp(v); }); modBar(g, kPanels[3].x + 110, kPanels[3].y + 100, 86, GD, mix);
}

void WarpView::mouseDown(const juce::MouseEvent& e) {
    const auto p = toScene(e.position); const juce::String q = base + ".params.";
    for (int i = 0; i < 4; ++i) if (juce::Rectangle<float>(222.f + (float) i * 50.f, 344.f, 46.f, 16.f).contains(p)) { store.set(q + "mode", kModes[i]); return; }
    for (int i = 0; i < 4; ++i) if (juce::Rectangle<float>(kPanels[i].x, kPanels[i].y, PW, PH).contains(p)) { panelDrag = i; applyDrag(p); return; }
}

void WarpView::applyDrag(juce::Point<float> p) {
    if (panelDrag < 0) return;
    const auto P = kPanels[panelDrag]; const juce::String q = base + ".params.";
    const float vy = 1.f - juce::jlimit(0.f, 1.f, (p.y - (P.y + 22.f)) / (PH - 34.f)), hx = juce::jlimit(0.f, 1.f, (p.x - (P.x + 8.f)) / (PW - 16.f));
    if (panelDrag == 0) store.set(q + "drive", std::round((1.f + 19.f * vy) * 10.f) / 10.0);
    else if (panelDrag == 1) store.set(q + "symmetry", std::round((hx * 2.f - 1.f) * 100.f) / 100.0);
    else if (panelDrag == 2) { if (store.str(q + "mode") == "pwm") store.set(q + "pwm", std::round((0.02f + 0.96f * vy) * 100.f) / 100.0); else store.set(q + "drive", std::round((1.f + 19.f * vy) * 10.f) / 10.0); }
    else store.set(q + "mix", std::round(hx * 100.f) / 100.0);
}

} // namespace vv
