#include "GrainBridgeView.h"
#include "Widgets.h"
namespace vv {
namespace {
const char* kShapes[] = { "HANN", "TRI", "GAUSS", "RECT" };
constexpr float kFy0 = 294.f, kFy1 = 396.f;
float faderX(int i) { return 40.f + (float) i * 52.f; }
juce::Colour amber(float a) { a = juce::jlimit(0.f, 1.f, a); return juce::Colour::fromFloatRGBA(1.f, (150.f + 60.f * a) / 255.f, 40.f / 255.f, 0.15f + 0.85f * a); }
juce::Font mono(float h, bool bold = false) { auto f = VVLookAndFeel::mono(h); return bold ? f.boldened() : f; }
}

GrainBridgeView::GrainBridgeView(Store& s, juce::String b, std::function<float()> lv) : store(s), base(std::move(b)), levelFn(std::move(lv)) { startTimerHz(30); }

static const double kLo[] = { 5, 0.5, -24, -1, 0, 0, 0 }, kHi[] = { 500, 100, 24, 1, 1, 1, 1 };
static const char* kKeys[] = { "grainSize", "density", "pitch", "panCenter", "spread", "followerAmount", "mix" };
static const char* kLabels[] = { "SIZE", "DENS", "PITCH", "PAN", "SPREAD", "FOLLOW", "MIX" };
static bool isExp(int i) { return i < 2; }
static double kDef[] = { 80, 20, 0, 0, 0.5, 0, 1 };

double GrainBridgeView::get(int i) const { return store.num(base + ".params." + kKeys[i], kDef[i]); }
static double norm(int i, double v) { return isExp(i) ? std::log(v / kLo[i]) / std::log(kHi[i] / kLo[i]) : (v - kLo[i]) / (kHi[i] - kLo[i]); }
static juce::String fmt(int i, double v) {
    switch (i) { case 0: return juce::String((int) std::round(v)) + " ms"; case 1: return juce::String(v, 1) + "/s";
        case 2: return (v > 0 ? "+" : "") + juce::String(v, 1) + " st"; case 3: return juce::String(v < -0.02 ? "L " : v > 0.02 ? "R " : "C ") + juce::String(std::abs(v), 2);
        default: return juce::String((int) std::round(v * 100)) + " %"; }
}
void GrainBridgeView::setFromY(int i, float y) {
    const double n = juce::jlimit(0.0, 1.0, 1.0 - (y - kFy0) / (kFy1 - kFy0));
    double v = isExp(i) ? kLo[i] * std::pow(kHi[i] / kLo[i], n) : kLo[i] + n * (kHi[i] - kLo[i]);
    v = i == 0 ? std::round(v) : i == 1 ? std::round(v * 2) / 2 : i == 2 ? std::round(v * 10) / 10 : std::round(v * 100) / 100;
    store.set(base + ".params." + kKeys[i], v);
}
juce::Point<float> GrainBridgeView::toLocal(juce::Point<int> p) const {
    const float s = std::min((float) getWidth() / W, (float) getHeight() / H);
    return { ((float) p.x - ((float) getWidth() - W * s) / 2) / s, ((float) p.y - ((float) getHeight() - H * s) / 2) / s };
}
void GrainBridgeView::mouseDown(const juce::MouseEvent& e) {
    const auto p = toLocal(e.getPosition());
    for (int i = 0; i < 7; ++i) if (std::abs(p.x - faderX(i)) < 22 && p.y > kFy0 - 18 && p.y < kFy1 + 18) { drag = i; setFromY(i, p.y); repaint(); return; }
    for (int i = 0; i < 4; ++i) { const float rx = 384.f + (float) i * 34.f; if (p.x > rx && p.x < rx + 31 && p.y > 296 && p.y < 376) { store.set(base + ".params.envShape", i); return; } }
}
void GrainBridgeView::mouseDrag(const juce::MouseEvent& e) { if (drag >= 0) setFromY(drag, toLocal(e.getPosition()).y); }

float GrainBridgeView::env(float p, int shape) const {
    if (shape < 1) return std::sin(juce::MathConstants<float>::pi * p); if (shape < 2) return 1.f - std::abs(2 * p - 1);
    if (shape < 3) return std::exp(-std::pow((p - 0.5f) * 4.f, 2.f) / 2.f); return (p < 0.04f || p > 0.96f) ? (p < 0.04f ? p / 0.04f : (1 - p) / 0.04f) : 1.f;
}

void GrainBridgeView::step(float dt) {
    const double size = get(0), dens = get(1), pitch = get(2), pan = get(3), spr = get(4), fol = get(5);
    level += ((levelFn ? levelFn() : 0.f) - level) * 0.2f;
    spawnAcc += (float) std::min(dens * (1 + fol * level * 3), 60.0) * dt / 4.f;
    while (spawnAcc >= 1.f) { spawnAcc -= 1.f;
        if (grains.size() < 48) { std::vector<int> freeT; for (int i = 0; i < 48; ++i) { bool used = false; for (auto& g : grains) used = used || g.tile == i; if (!used) freeT.push_back(i); }
            if (!freeT.empty()) grains.push_back({ freeT[(size_t) rng.nextInt((int) freeT.size())], 0.f, (float) std::max(0.2, size / 1000.0 * 4.0), rng.nextFloat(),
                (float) juce::jlimit(-1.0, 1.0, pan + (rng.nextDouble() * 2 - 1) * spr), (float) std::pow(2.0, pitch / 12.0) }); } }
    for (auto& g : grains) g.age += dt;
    grains.erase(std::remove_if(grains.begin(), grains.end(), [](const Grain& g) { return g.age >= g.dur; }), grains.end());
    histAcc += dt; if (histAcc > 0.05f) { histAcc = 0; histPos = (histPos + 1) % (int) hist.size(); hist[(size_t) histPos] = level; }
}

void GrainBridgeView::paint(juce::Graphics& g) {
    const float s = std::min((float) getWidth() / W, (float) getHeight() / H);
    g.fillAll(juce::Colour(0xff050708)); g.addTransform(juce::AffineTransform::scale(s).translated(((float) getWidth() - W * s) / 2, ((float) getHeight() - H * s) / 2));
    const int shape = (int) store.num(base + ".params.envShape", 0); double v[7]; for (int i = 0; i < 7; ++i) v[i] = get(i);
    auto bevel = [&](float x, float y, float w, float h, juce::Colour fill) { g.setColour(fill); g.fillRect(x, y, w, h); g.setColour(juce::Colour(0xffb8b69a)); g.drawLine(x, y + h, x, y); g.drawLine(x, y, x + w, y); g.setColour(juce::Colour(0xff2c2c1e)); g.drawLine(x + w, y, x + w, y + h); g.drawLine(x + w, y + h, x, y + h); };
    auto txt = [&](const juce::String& t, float x, float y, float size, juce::Colour c, juce::Justification j, bool bold = false) { g.setColour(c); g.setFont(mono(size, bold)); g.drawText(t, juce::Rectangle<float>(x - 100, y - size, 200, size + 4).toNearestInt(), j == juce::Justification::centred ? juce::Justification::centred : j == juce::Justification::centredLeft ? juce::Justification::centredLeft : juce::Justification::centredRight, false); };
    auto left = [&](const juce::String& t, float x, float y, float size, juce::Colour c, bool b = false) { g.setColour(c); g.setFont(mono(size, b)); g.drawText(t, (int) x, (int) (y - size), 300, (int) size + 4, juce::Justification::centredLeft, false); };
    auto crt = [&](float x, float y, float w, float h, const char* title) { bevel(x - 4, y - 4, w + 8, h + 8, juce::Colour(0xff23231a)); g.setColour(juce::Colour(0xff05080c)); g.fillRect(x, y, w, h); left(title, x + 4, y + 11, 9, juce::Colour(0xff9ab6c4));
        g.setColour(juce::Colour(0x2e508cb4)); for (int i = 0; i <= 6; ++i) g.drawVerticalLine((int) (x + w * i / 6), y, y + h); for (int i = 0; i <= 4; ++i) g.drawHorizontalLine((int) (y + h * i / 4), x, x + w); };

    bevel(0, 0, W, H, juce::Colour(0xff7c7b5e)); g.setColour(juce::Colour(0xff5f5e46)); g.fillRect(6.f, 6.f, W - 12, 20.f);
    left("GRAIN CLOUD  -  COMMAND CENTRE", 14, 20, 11, juce::Colour(0xffe8e2c0), true);
    txt(juce::String((int) grains.size()) + " GRAINS ACTIVE", W - 14, 20, 11, grains.empty() ? juce::Colour(0xff9a9a80) : juce::Colour(0xffffb347), juce::Justification::centredRight, true);
    // grain annunciator
    std::array<float, 48> tiles{}; for (auto& gr : grains) tiles[(size_t) gr.tile] = env(gr.age / gr.dur, shape);
    bevel(8, 32, 330, 150, juce::Colour(0xff26261c));
    for (int i = 0; i < 48; ++i) { const float x = 14.f + (float) (i % 8) * 40.f, y = 38.f + (float) (i / 8) * 24.f, a = tiles[(size_t) i];
        g.setColour(a > 0.02f ? amber(a) : juce::Colour(0xff3b2a14)); g.fillRect(x, y, 37.f, 21.f); g.setColour(juce::Colours::black); g.drawRect(x, y, 37.f, 21.f, 1.f);
        txt("G" + juce::String(i + 1).paddedLeft('0', 2), x + 18, y + 13, 8, a > 0.4f ? juce::Colour(0xff3a1d00) : juce::Colour(0xff6b5128), juce::Justification::centred); }
    // parameter annunciator
    bevel(346, 32, 368, 66, juce::Colour(0xff26261c));
    for (int i = 0; i < 8; ++i) { const float x = 352.f + (float) (i % 4) * 90.f, y = 37.f + (float) (i / 4) * 30.f;
        const bool isShape = i == 7; const float a = isShape ? 0.35f + 0.18f * (float) shape : juce::jlimit(0.12f, 1.f, (float) norm(i, v[i]));
        g.setColour(amber(a)); g.fillRect(x, y, 86.f, 26.f); g.setColour(juce::Colours::black); g.drawRect(x, y, 86.f, 26.f, 1.f);
        const auto tc = a > 0.5f ? juce::Colour(0xff3a1d00) : juce::Colour(0xffd9a64e);
        txt(isShape ? "SHAPE" : kLabels[i], x + 43, y + 11, 9, tc, juce::Justification::centred, true); txt(isShape ? juce::String(kShapes[shape & 3]) : fmt(i, v[i]), x + 43, y + 22, 10, tc, juce::Justification::centred); }
    // CRT buffer
    crt(352, 114, 168, 92, "BUFFER  2.0 s"); g.setColour(juce::Colour(0xff4ac3ff)); { juce::Path p; for (size_t i = 0; i < hist.size(); ++i) { const float val = hist[(size_t) (((int) histPos + 1 + (int) i) % (int) hist.size())]; const float x = 352.f + (float) i * 168.f / (float) hist.size(), y = 160.f - val * 40.f * (0.4f + (float) v[6] * 0.6f); if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y); } g.strokePath(p, juce::PathStrokeType(1.2f)); }
    g.setColour(juce::Colour(0xff6a7b88)); g.drawHorizontalLine(160, 352, 520);
    for (auto& gr : grains) { const float x = 352.f + (1.f - gr.pos) * 168.f, a = env(gr.age / gr.dur, shape); g.setColour(juce::Colour::fromFloatRGBA(1.f, 0.69f, 0.16f, 0.25f + 0.75f * a)); g.drawLine(x, 126, x, 200, (float) std::max(1.0, v[0] / 40)); }
    // CRT stereo field
    crt(534, 114, 168, 92, "STEREO FIELD  L . R"); const float cx = 618.f + (float) v[3] * 72.f, hw = std::max(4.f, (float) v[4] * 72.f), bx = std::max(534.f, cx - hw), bw = std::min(168.f, hw * 2);
    g.setColour(juce::Colour(0x1a4ac3ff)); g.fillRect(bx, 128.f, bw, 72.f); g.setColour(juce::Colour(0x994ac3ff)); g.drawRect(bx, 128.f, bw, 72.f, 1.f); g.setColour(juce::Colour(0x40ffffff)); g.drawVerticalLine(618, 120, 204);
    for (auto& gr : grains) { const float a = env(gr.age / gr.dur, shape), x = 618.f + gr.pan * 76.f, y = 164.f - juce::jlimit(-1.f, 1.f, std::log2(gr.rate) / 2.f) * 32.f, r = 2.f + a * (float) juce::jlimit(1.0, 6.0, v[0] / 40);
        g.setColour(juce::Colour::fromFloatRGBA(1.f, (150.f + 80.f * a) / 255.f, 60.f / 255.f, 0.2f + 0.8f * a)); g.fillEllipse(x - r, y - r, r * 2, r * 2); }
    // mimic strip
    bevel(8, 190, 330, 52, juce::Colour(0xff6c6b50)); const char* names[] = { "INPUT", "BUFFER", "GRAINS", "OUT" }; const float lv[] = { level, 0.6f, std::min(1.f, (float) grains.size() / 16.f), (float) v[6] };
    for (int i = 0; i < 4; ++i) { const float x = 22.f + (float) i * 80.f; g.setColour(juce::Colour(0xff2c2c1e)); if (i < 3) g.drawLine(x + 52, 216, x + 80, 216, 2.f);
        g.setColour(juce::Colour(0xff4e4d38)); g.fillRect(x, 202.f, 56.f, 28.f); g.setColour(juce::Colours::black); g.drawRect(x, 202.f, 56.f, 28.f, 1.f); g.setColour(amber(std::max(0.1f, lv[i]))); g.fillEllipse(x + 4.5f, 205.5f, 7.f, 7.f);
        txt(names[i], x + 30, 224, 9, juce::Colour(0xffe8e2c0), juce::Justification::centred); }
    // console
    bevel(8, 246, 704, 188, juce::Colour(0xff5c5b44)); g.setColour(juce::Colour(0xff2b2b1f)); g.fillRect(14.f, 252.f, 500.f, 176.f); g.fillRect(526.f, 252.f, 180.f, 176.f);
    for (int i = 0; i < 7; ++i) { const float x = faderX(i), n = (float) juce::jlimit(0.0, 1.0, norm(i, v[i])), ky = kFy1 - n * (kFy1 - kFy0);
        g.setColour(amber(0.15f + n * 0.85f)); g.fillEllipse(x - 4, 262.f, 8.f, 8.f); txt(kLabels[i], x, 278, 9, juce::Colour(0xffe8e2c0), juce::Justification::centred, true);
        g.setColour(juce::Colour(0xff0b0b08)); g.fillRect(x - 2, kFy0, 4.f, kFy1 - kFy0); g.setColour(juce::Colour(0xff8a896c)); for (int t = 0; t <= 10; ++t) { const float ty = kFy0 + (float) t * (kFy1 - kFy0) / 10.f; g.drawLine(x - 12, ty, x - (t % 5 == 0 ? 6.f : 9.f), ty); }
        g.setColour(amber(0.9f)); g.fillRect(x - 1.5f, ky, 3.f, kFy1 - ky);
        g.setColour(drag == i ? juce::Colour(0xfff6f0cf) : juce::Colour(0xffcfc9a4)); g.fillRect(x - 14, ky - 8, 28.f, 16.f); g.setColour(juce::Colours::black); g.drawRect(x - 14, ky - 8, 28.f, 16.f, 1.f); g.setColour(juce::Colour(0xff222222)); g.fillRect(x - 12, ky - 1, 24.f, 2.f);
        txt(fmt(i, v[i]), x, 412, 9, juce::Colour(0xffffd58a), juce::Justification::centred); }
    left("GRAIN ENVELOPE", 384, 278, 9, juce::Colour(0xffe8e2c0), true);
    for (int i = 0; i < 4; ++i) { const float rx = 384.f + (float) i * 34.f; const bool on = (shape & 3) == i; bevel(rx, 296, 31, 80, juce::Colour(0xffcfc9a4)); const float ry = on ? 336.f : 300.f;
        g.setColour(on ? juce::Colour(0xff8a8566) : juce::Colour(0xffe6e0bc)); g.fillRect(rx + 3, ry, 25.f, 36.f); g.setColour(juce::Colour(0xff666666)); g.drawRect(rx + 3, ry, 25.f, 36.f, 1.f);
        juce::Path p; for (int k = 0; k <= 20; ++k) { const float e = env((float) k / 20.f, i), px = rx + 6 + (float) k * 0.95f, py = (on ? 366.f : 330.f) - e * 14.f; if (k == 0) p.startNewSubPath(px, py); else p.lineTo(px, py); }
        g.setColour(on ? juce::Colour(0xffffb347) : juce::Colour(0xff555555)); g.strokePath(p, juce::PathStrokeType(1.4f));
        g.setColour(amber(on ? 1.f : 0.08f)); g.fillEllipse(rx + 11.5f, 384.5f, 7.f, 7.f); txt(kShapes[i], rx + 15, 408, 8, juce::Colour(0xffe8e2c0), juce::Justification::centred); }
    left("INPUT LEVEL", 540, 268, 9, juce::Colour(0xffe8e2c0), true);
    for (int i = 0; i < 14; ++i) { const bool on = level * 14 > (float) i; g.setColour(on ? (i > 10 ? juce::Colour(0xffff4a3a) : i > 7 ? juce::Colour(0xffffb347) : juce::Colour(0xff7ddc6a)) : juce::Colour(0xff1a1a12)); g.fillRect(540.f + (float) i * 11, 274.f, 9.f, 16.f); }
    left("FOLLOWER DRIVE", 540, 312, 9, juce::Colour(0xffe8e2c0), true);
    for (int i = 0; i < 14; ++i) { g.setColour((v[5] * 14 > i && level * 14 > (float) i * 0.5f) ? juce::Colour(0xffffb347) : (v[5] * 14 > i ? juce::Colour(0xff5a4220) : juce::Colour(0xff1a1a12))); g.fillRect(540.f + (float) i * 11, 318.f, 9.f, 10.f); }
    left("DRY / WET", 540, 352, 9, juce::Colour(0xffe8e2c0), true);
    for (int i = 0; i < 14; ++i) { g.setColour(v[6] * 14 > i ? juce::Colour(0xff4ac3ff) : juce::Colour(0xff1a1a12)); g.fillRect(540.f + (float) i * 11, 358.f, 9.f, 10.f); }
    left("SPAWN " + juce::String((int) std::min(v[1] * (1 + v[5] * level * 3), 60.0)) + "/s  PITCH x" + juce::String(std::pow(2.0, v[2] / 12.0), 2), 540, 404, 9, juce::Colour(0xff9a9a80));
}

} // namespace vv
