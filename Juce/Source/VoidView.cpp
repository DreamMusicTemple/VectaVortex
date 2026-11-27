#include "VoidView.h"
#include "Widgets.h"
using namespace vv::vvoid;
namespace vv {
namespace {
const juce::Colour kSkyTop(0xff04081a), kSkyBot(0xff112250), kStar(0xff8fe9ff), kShip(0xff4ad4d9), kShip2(0xff9b6bff), kPlasma(0xffc9fffb), kBomb(0xffd94a9e),
    kGround(0xff0b1740), kUi(0xff4ad4d9), kUiDim(0xff4a7f95);
const juce::Colour kEnemy[6] = { juce::Colour(0xff9b6bff), juce::Colour(0xffd94a9e), juce::Colour(0xff4ad4d9), juce::Colour(0xff7ee0ff), juce::Colour(0xffc9a0ff), juce::Colour(0xffff7ad9) };
const juce::Colour kTint[3] = { juce::Colour(0xff7dff9a), juce::Colour(0xffffa940), juce::Colour(0xffff4545) };
const juce::Colour kEbul[4] = { juce::Colour(0xffff5a5a), juce::Colour(0xffffb347), juce::Colour(0xffff7ad9), juce::Colour(0xff7dffea) };
const juce::Point<float> kStick(68, (float) H - 64), kBtnP((float) W - 62, (float) H - 64), kBtnB((float) W - 134, (float) H - 34);
juce::Font mono(float s, bool bold = false) { auto f = VVLookAndFeel::mono(s); return bold ? f.boldened() : f; }
void ring(juce::Graphics& g, float x, float y, float r, float w) { g.drawEllipse(x - r, y - r, 2 * r, 2 * r, w); }
void disc(juce::Graphics& g, float x, float y, float r) { g.fillEllipse(x - r, y - r, 2 * r, 2 * r); }
void glow(juce::Graphics& g, juce::Colour c, float x, float y, float r) { g.setColour(c.withAlpha(0.18f)); disc(g, x, y, r * 2.2f); g.setColour(c); disc(g, x, y, r); }
}

VoidView::VoidView() : rng((uint32_t) juce::Time::currentTimeMillis()) {
    setWantsKeyboardFocus(true); setMouseClickGrabsKeyboardFocus(true); last = juce::Time::getMillisecondCounterHiRes(); startTimerHz(60);
}
VoidView::~VoidView() { stopTimer(); }

juce::AffineTransform VoidView::toScreen() const {
    const float s = std::min(getWidth() / (float) W, getHeight() / (float) H);
    return juce::AffineTransform::scale(s).translated((getWidth() - W * s) * 0.5f, (getHeight() - H * s) * 0.5f);
}
juce::Point<float> VoidView::toGame(juce::Point<int> p) const { auto q = p.toFloat(); toScreen().inverted().transformPoint(q.x, q.y); return q; }
bool VoidView::menuHit(juce::Point<float> p) const {
    return (G.mode == M_TITLE || G.mode == M_CONTINUE || G.mode == M_GAMEOVER || G.mode == M_VICTORY) && std::abs(p.x - W / 2) < 95 && std::abs(p.y - (G.mode == M_VICTORY ? H - 60 : H - 110)) < 22;
}
void VoidView::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus(); const auto p = toGame(e.getPosition());
    if (menuHit(p)) { startPulse = true; return; }
    auto near = [&](juce::Point<float> q, float r) { return p.getDistanceFrom(q) <= r; };
    const int id = e.source.getIndex();
    if (near(kStick, 70) || (!near(kBtnP, 40) && !near(kBtnB, 40) && p.x < W * 0.45f)) { stickPtr = id; stickX = juce::jlimit(-1.f, 1.f, (p.x - kStick.x) / 40.f); stickY = juce::jlimit(-1.f, 1.f, (p.y - kStick.y) / 40.f); }
    else if (near(kBtnP, 40)) { plasmaPtr = id; plasmaHeld = true; } else if (near(kBtnB, 40)) { bombPtr = id; bombHeld = true; }
}
void VoidView::mouseDrag(const juce::MouseEvent& e) {
    if (e.source.getIndex() != stickPtr) return; const auto p = toGame(e.getPosition());
    stickX = juce::jlimit(-1.f, 1.f, (p.x - kStick.x) / 40.f); stickY = juce::jlimit(-1.f, 1.f, (p.y - kStick.y) / 40.f);
}
void VoidView::mouseUp(const juce::MouseEvent& e) {
    const int id = e.source.getIndex();
    if (id == stickPtr) { stickPtr = -1; stickX = stickY = 0; } if (id == plasmaPtr) { plasmaPtr = -1; plasmaHeld = false; } if (id == bombPtr) { bombPtr = -1; bombHeld = false; }
}
bool VoidView::keyPressed(const juce::KeyPress& k) {
    if (k.isKeyCode(juce::KeyPress::returnKey)) { startPulse = true; return true; }
    const auto c = k.getKeyCode();
    return c == juce::KeyPress::leftKey || c == juce::KeyPress::rightKey || c == juce::KeyPress::upKey || c == juce::KeyPress::downKey || c == juce::KeyPress::spaceKey
        || c == 'A' || c == 'D' || c == 'W' || c == 'S' || c == 'Z' || c == 'X' || c == 'B' || c == 'a' || c == 'd' || c == 'w' || c == 's' || c == 'z' || c == 'x' || c == 'b';
}
bool VoidView::keyStateChanged(bool) { return false; }

void VoidView::timerCallback() {
    const double now = juce::Time::getMillisecondCounterHiRes(); acc += juce::jmin(0.1, (now - last) / 1000.0); last = now;
    auto down = [](int k) { return juce::KeyPress::isKeyCurrentlyDown(k); };
    auto dn = [&](char c) { return down(c) || down(c + 32); };
    Input in; in.dx = stickX + (down(juce::KeyPress::rightKey) || dn('D') ? 1 : 0) - (down(juce::KeyPress::leftKey) || dn('A') ? 1 : 0);
    in.dy = stickY + (down(juce::KeyPress::downKey) || dn('S') ? 1 : 0) - (down(juce::KeyPress::upKey) || dn('W') ? 1 : 0);
    in.plasma = plasmaHeld || dn('Z') || down(juce::KeyPress::spaceKey); in.bomb = bombHeld || dn('X') || dn('B');
    in.start = startPulse; startPulse = false;
    while (acc >= 1.0 / 60) { acc -= 1.0 / 60; step(G, in, 1.0 / 60, [this] { return rng(); }); in.start = false; }
    repaint();
}

void VoidView::drawShip(juce::Graphics& g, float x, float y, float thrust) const {
    g.setColour(kShip2.withAlpha(0.6f)); ring(g, x - 20 - thrust * 2, y, 5, 1.2f);
    juce::Path p; p.startNewSubPath(x + 20, y); p.quadraticTo(x + 0, y - 9, x - 16, y - 10); p.lineTo(x - 10, y); p.lineTo(x - 16, y + 10); p.quadraticTo(x + 0, y + 9, x + 20, y); p.closeSubPath();
    g.setColour(juce::Colour(0xff071a33)); g.fillPath(p); g.setColour(kShip); g.strokePath(p, juce::PathStrokeType(1.6f));
    g.setColour(kPlasma.withAlpha(0.85f)); g.fillEllipse(x + 3 - 6, y - 2.6f, 12, 5.2f);
}
void VoidView::drawEnemy(juce::Graphics& g, const Enemy& e) const {
    const juce::Colour col = kEnemy[e.type]; const juce::Colour edge = e.flash > 0 ? juce::Colours::white : col; const float r = (float) e.r, x = (float) e.x, y = (float) e.y;
    g.setColour(edge);
    auto body = [&](juce::Path& p) { g.setColour(juce::Colour(0xd90a0610)); g.fillPath(p); g.setColour(edge); g.strokePath(p, juce::PathStrokeType(1.7f)); };
    juce::Path p;
    switch (e.type) {
        case E_DRONE: for (int k = -1; k <= 1; k++) { const float h = r * (0.6f + 0.5f * std::abs(std::sin((float) e.t * 5 + k * 1.7f))); g.setColour(col.withAlpha(0.85f)); g.fillRect(x + k * 7 - 2.5f, y - h, 5.f, h * 2); } break;
        case E_ZIG: p.addEllipse(x - r * 0.9f, y - r * 0.9f, r * 1.8f, r * 1.8f); body(p);
            for (int k = 0; k < 4; k++) { const float a = (float) e.t * 3 + k * 1.57f; g.setColour(col); disc(g, x + std::cos(a) * r * 1.1f, y + std::sin(a) * r * 1.1f, 2.3f); } break;
        case E_DIVER: p.startNewSubPath(x - r, y); p.quadraticTo(x, y - r * 1.3f, x + r, y - r * 0.2f); p.lineTo(x + r * 0.3f, y); p.lineTo(x + r, y + r * 0.2f); p.quadraticTo(x, y + r * 1.3f, x - r, y); p.closeSubPath(); body(p); break;
        case E_TANK: for (int k = 0; k < 8; k++) { const float a = k * juce::MathConstants<float>::pi / 4 + juce::MathConstants<float>::pi / 8; const float px = x + std::cos(a) * r, py = y + std::sin(a) * r; if (k == 0) p.startNewSubPath(px, py); else p.lineTo(px, py); }
            p.closeSubPath(); body(p); g.setColour(edge); g.drawLine(x - r * 0.5f, y, x + r * 0.5f, y, 1.7f); g.drawLine(x, y - r * 0.5f, x, y + r * 0.5f, 1.7f); break;
        case E_SHOOTER: p.addEllipse(x - r, y - r * 0.62f, r * 2, r * 1.24f); body(p); g.setColour(col); disc(g, x - 3, y, 3.6f); break;
        default: for (int k = 0; k < 10; k++) { const float a = k * juce::MathConstants<float>::pi / 5 + (float) e.t; const float rr = k % 2 ? r * 0.5f : r * 1.1f; const float px = x + std::cos(a) * rr, py = y + std::sin(a) * rr; if (k == 0) p.startNewSubPath(px, py); else p.lineTo(px, py); }
            p.closeSubPath(); body(p);
    }
}
void VoidView::drawBoss(juce::Graphics& g) const {
    const Boss& b = G.boss; const juce::Colour tint = kTint[bossTint(b)]; const bool fl = b.flash > 0; const float esc = b.final_ ? 1.25f : 1.f;
    juce::Graphics::ScopedSaveState ss(g); float jx = 0, jy = 0, alpha = 1; if (b.dying > 0) { jx = (rand.nextFloat() - 0.5f) * 6; jy = (rand.nextFloat() - 0.5f) * 6; alpha = 0.5f + 0.5f * rand.nextFloat(); }
    const float bx = (float) b.x + jx, by = (float) b.y + jy; const juce::Colour edge = fl ? juce::Colours::white : tint;
    g.setOpacity(alpha);
    g.setColour(juce::Colour(0xeb0e0a12).withMultipliedAlpha(alpha)); g.fillEllipse(bx - 66 * esc, by - 84 * esc, 132 * esc, 168 * esc); g.setColour(edge.withMultipliedAlpha(alpha)); g.drawEllipse(bx - 66 * esc, by - 84 * esc, 132 * esc, 168 * esc, 2.2f);
    if (b.final_) { g.setColour(edge.withAlpha(0.4f * alpha)); g.drawEllipse(bx - 80 * esc, by - 98 * esc, 160 * esc, 196 * esc, 2.2f); }
    for (int k = 0; k < 14; k++) { const float a = -juce::MathConstants<float>::pi / 2 + (k / 13.f) * juce::MathConstants<float>::pi; const float h = (10 + 10 * std::abs(std::sin((float) b.t * 3 + k))) * esc;
        juce::Path r; r.addRectangle(70 * esc, -3.f, h, 6.f); g.setColour(tint.withAlpha((b.dying > 0 ? 0.5f : 0.55f) * alpha)); g.fillPath(r, juce::AffineTransform::rotation(a).translated(bx, by)); }
    g.setColour(edge.withMultipliedAlpha(alpha)); g.drawEllipse(bx + 10 * esc - 38 * esc, by - 50 * esc, 76 * esc, 100 * esc, 2.2f);
    const float cx = (float) (b.x + b.core), cy = (float) (b.y + 10 * std::sin(b.t * 2)); const float pulse = 0.6f + 0.4f * std::sin((float) b.t * 8);
    g.setColour(juce::Colours::white.withAlpha(0.2f * (0.5f + 0.5f * pulse))); disc(g, cx, cy, 20 + pulse * 3); g.setColour(juce::Colours::white.withAlpha(0.5f + 0.5f * pulse)); disc(g, cx, cy, 10 + pulse * 3);
    g.setColour(tint); ring(g, cx, cy, 15, 2);
}
void VoidView::panel(juce::Graphics& g, const juce::StringArray& lines) const {
    g.setColour(juce::Colours::black.withAlpha(0.62f)); g.fillRect(0.f, 0.f, (float) W, (float) H);
    for (int i = 0; i < lines.size(); i++) { g.setColour(i == 0 ? kUi : juce::Colour(0xffe8e2d4)); g.setFont(i == 0 ? mono(30, true) : mono(14)); const float y = i == 0 ? (float) H / 2 - 80 : (float) H / 2 - 50 + (i - 1) * 24;
        g.drawText(lines[i], juce::Rectangle<float>(0, y - 18, (float) W, 36), juce::Justification::centred); }
}
void VoidView::button(juce::Graphics& g, const juce::String& label, float y) const {
    juce::Rectangle<float> r((float) W / 2 - 90, y - 18, 180, 36); g.setColour(juce::Colours::black.withAlpha(0.5f)); g.fillRect(r); g.setColour(kUi); g.drawRect(r, 2.f);
    g.setFont(mono(15, true)); g.drawText(label, r, juce::Justification::centred);
}

void VoidView::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff050708));
    juce::Graphics::ScopedSaveState outer(g); g.addTransform(toScreen());
    g.reduceClipRegion(juce::Rectangle<int>(0, 0, (int) W, (int) H));
    if (G.shake > 0) g.addTransform(juce::AffineTransform::translation((rand.nextFloat() - 0.5f) * 6 * (float) G.shake * 5, (rand.nextFloat() - 0.5f) * 6 * (float) G.shake * 5));
    g.setGradientFill(juce::ColourGradient(kSkyTop, 0, 0, kSkyBot, 0, (float) H, false)); g.fillRect(-10.f, -10.f, (float) W + 20, (float) H + 20);
    g.setColour(kStar); for (int i = 0; i < 70; i++) { const int l = i % 3; const float sp = 14.f + l * 28; float x = std::fmod(i * 97.3f - (float) G.t * sp, (float) W); if (x < 0) x += (float) W; const float y = std::fmod(i * 53.7f, (float) H);
        g.setOpacity(0.25f + l * 0.25f); g.fillRect(x, y, 1 + l * 0.6f, 1 + l * 0.6f); }
    { juce::Path gr; gr.startNewSubPath(0, (float) H); for (float x = 0; x <= W; x += 20) gr.lineTo(x, (float) H - 12 - 6 * std::sin((x + (float) G.t * 40) * 0.03f) - 4 * std::sin((x + (float) G.t * 40) * 0.11f));
      gr.lineTo((float) W, (float) H); gr.closeSubPath(); g.setColour(kGround); g.fillPath(gr); g.setColour(kUiDim.withAlpha(0.5f)); g.strokePath(gr, juce::PathStrokeType(1.f)); }
    for (auto& bm : G.beams) { const bool fire = bm.t > 0.9; const float h = fire ? 18.f : 2.f; const float x1 = G.hasBoss ? (float) G.boss.x - 70 : (float) W; g.setColour(fire ? juce::Colour(0xd9ff5050) : juce::Colour(0x59ffc878)); g.fillRect(0.f, (float) bm.y - h / 2, x1, h); }
    for (auto& p : G.pick) { const auto c = p.kind == 'W' ? kShip2 : juce::Colour(0xff46b4ff); g.setColour(juce::Colour(0xe6080a14)); disc(g, (float) p.x, (float) p.y, 11); g.setColour(c); ring(g, (float) p.x, (float) p.y, 11, 2);
        g.setFont(mono(12, true)); g.drawText(juce::String::charToString(p.kind), juce::Rectangle<float>((float) p.x - 10, (float) p.y - 8, 20, 18), juce::Justification::centred); }
    for (auto& ob : G.obstacles) for (int i = 0; i < ob.nparts; i++) { const Rect& r = ob.parts[i]; const juce::Rectangle<float> rc((float) (ob.x + r.x), (float) r.y, (float) r.w, (float) r.h);
        g.setColour(juce::Colour(0xf2202a4e)); g.fillRect(rc); g.setColour(kUiDim); g.drawRect(rc.reduced(0.5f), 1.5f); }
    for (auto& e : G.enemies) drawEnemy(g, e);
    if (G.hasBoss) drawBoss(g);
    for (auto& b : G.ebul) glow(g, kEbul[b.col & 3], (float) b.x, (float) b.y, (float) b.r);
    for (auto& b : G.bullets) { g.setColour(kShip.withAlpha(0.25f)); g.fillRect((float) b.x - 3, (float) b.y - (float) b.h / 2 - 3, (float) b.w + 6, (float) b.h + 6); g.setColour(kPlasma); g.fillRect((float) b.x, (float) b.y - (float) b.h / 2, (float) b.w, (float) b.h); }
    for (auto& b : G.bombs) glow(g, kBomb, (float) b.x, (float) b.y, (float) b.r);
    for (auto& b : G.blasts) { const float k = (float) b.t / 0.35f; const float rr = (float) b.r * (0.3f + 0.7f * k); g.setColour(kPlasma.withAlpha((1 - k) * 0.25f)); disc(g, (float) b.x, (float) b.y, rr); g.setColour(kBomb.withAlpha(1 - k)); ring(g, (float) b.x, (float) b.y, rr, 3 * (1 - k) + 1); }
    for (auto& p : G.parts) { g.setColour(kPlasma.withAlpha(juce::jlimit(0.f, 1.f, 1 - (float) (p.t / p.life)))); g.fillRect((float) p.x, (float) p.y, 2.f, 2.f); }
    const Ship& s = G.ship;
    if (s.alive && (G.mode == M_PLAY || G.mode == M_LEVELCLEAR) && !(s.inv > 0 && (int) std::floor(G.t * 14) % 2)) {
        drawShip(g, (float) s.x, (float) s.y, 1 + std::sin((float) G.t * 30) * 0.5f);
        if (s.shield > 0) { const float a = s.shield < 2 && (int) std::floor(G.t * 10) % 2 ? 0.35f : 1.f;
            g.setGradientFill(juce::ColourGradient(juce::Colour(0x0d46b4ff), (float) s.x, (float) s.y, juce::Colour(0xd978d2ff), (float) s.x + 28, (float) s.y, true)); g.setOpacity(a); disc(g, (float) s.x, (float) s.y, 28);
            g.setColour(juce::Colour(0xff9fd0ff).withAlpha(a)); ring(g, (float) s.x, (float) s.y, 28, 2); }
    }
    // HUD
    g.setColour(kUi); g.setFont(mono(13, true));
    g.drawText("SCORE " + juce::String((juce::int64) G.score).paddedLeft('0', 6), juce::Rectangle<float>(10, 6, 200, 18), juce::Justification::centredLeft);
    g.drawText("LEVEL " + juce::String(G.level), juce::Rectangle<float>(0, 6, (float) W, 18), juce::Justification::centred);
    g.setColour(kUiDim); g.setFont(mono(10)); g.drawText("SHIPS", juce::Rectangle<float>(10, 24, 50, 14), juce::Justification::centredLeft);
    for (int i = 0; i < 3; i++) { juce::Graphics::ScopedSaveState ss(g); g.setOpacity(i < G.spares ? 1.f : 0.2f); g.addTransform(juce::AffineTransform::scale(0.5f).translated(66.f + i * 24, 31.f)); drawShip(g, 0, 0, 0); }
    auto bars = [](int n) { return juce::String::repeatedString("#", n + 1).paddedRight('-', 4); };
    g.setColour(kUiDim); g.setFont(mono(10)); g.drawText("PLASMA " + bars(G.wLvl) + "  BOMB " + bars(G.wLvl), juce::Rectangle<float>(10, 40, 300, 14), juce::Justification::centredLeft);
    if (s.shield > 0) { g.setColour(juce::Colour(0xff46b4ff)); g.drawText("SHIELD " + juce::String((double) s.shield, 1) + "s", juce::Rectangle<float>(10, 52, 200, 14), juce::Justification::centredLeft); }
    if (G.hasBoss && G.boss.dying <= 0) { const Boss& b = G.boss; const float f = std::max(0.f, (float) (b.maxHits - b.hits) / b.maxHits);
        g.setColour(juce::Colour(0xaa000000)); g.fillRect((float) W / 2 - 110, (float) H - 24, 220.f, 10.f); g.setColour(kTint[bossTint(b)]); g.fillRect((float) W / 2 - 109, (float) H - 23, 218 * f, 8.f);
        g.setColour(kUi); g.drawRect((float) W / 2 - 110.5f, (float) H - 24.5f, 221.f, 11.f, 1.f); g.setFont(mono(10, true)); g.drawText(b.final_ ? "FINAL BOSS" : "BOSS", juce::Rectangle<float>(0, (float) H - 40, (float) W, 14), juce::Justification::centred); }
    if (G.bannerT > 0 && G.mode != M_TITLE) { g.setColour(kUi.withAlpha(juce::jmin(1.f, (float) G.bannerT))); g.setFont(mono(22, true)); g.drawText(G.banner, juce::Rectangle<float>(0, (float) H / 2 - 56, (float) W, 32), juce::Justification::centred); }
    // on-screen controls
    { const float stX = stickX, stY = stickY; double dx, dy; dir8(stX, stY, dx, dy); g.setColour(kUi.withAlpha(0.5f)); ring(g, kStick.x, kStick.y, 44, 2);
      for (int k = 0; k < 8; k++) { const float a = k * juce::MathConstants<float>::pi / 4; g.drawLine(kStick.x + std::cos(a) * 38, kStick.y + std::sin(a) * 38, kStick.x + std::cos(a) * 44, kStick.y + std::sin(a) * 44, 2); }
      g.setColour(kUi.withAlpha(0.7f)); disc(g, kStick.x + (float) dx * 26, kStick.y + (float) dy * 26, 15);
      auto btn = [&](juce::Point<float> c, const char* label, bool on) { g.setColour(on ? kUi.withAlpha(0.9f) : juce::Colours::black.withAlpha(0.2f)); disc(g, c.x, c.y, 32); g.setColour(kUi.withAlpha(on ? 0.9f : 0.5f)); ring(g, c.x, c.y, 32, 2);
          g.setColour(on ? juce::Colours::black : kUi); g.setFont(mono(10, true)); g.drawText(label, juce::Rectangle<float>(c.x - 32, c.y - 8, 64, 16), juce::Justification::centred); };
      btn(kBtnP, "PLASMA", plasmaHeld || juce::KeyPress::isKeyCurrentlyDown('Z') || juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey)); btn(kBtnB, "BOMB", bombHeld || juce::KeyPress::isKeyCurrentlyDown('X')); }
    if (G.mode == M_TITLE) { panel(g, { "VEKTUR VOID", "8-way joystick (or arrows / WASD) moves the ship", "PLASMA (Z / Space)   BOMB (X / B)", "Collect W = weapon upgrade, S = shield" }); button(g, "START", (float) H - 110); }
    else if (G.mode == M_CONTINUE) { panel(g, { "SHIP LOST", "Continue?  " + juce::String(std::max(0, (int) std::ceil(G.contT))), "Continues left: " + juce::String(G.continues) }); button(g, "CONTINUE", (float) H - 110); }
    else if (G.mode == M_GAMEOVER) { panel(g, { "GAME OVER!", "Final score " + juce::String((juce::int64) G.score).paddedLeft('0', 6) }); if (G.modeT > 1.5) button(g, "NEW GAME", (float) H - 110); }
    else if (G.mode == M_VICTORY) {
        static const char* story[4] = { "MISSION COMPLETE", "The Spectral Eye closes and the void falls quiet.", "Every frequency drifts home to its own colour.", "The universe is safe again, pilot. Brilliant flying." };
        g.setColour(juce::Colours::black.withAlpha(0.72f)); g.fillRect(0.f, 0.f, (float) W, (float) H); g.setColour(kUi); g.setFont(mono(28, true)); g.drawText(story[0], juce::Rectangle<float>(0, 40, (float) W, 40), juce::Justification::centred);
        int n = (int) std::floor(G.typed); g.setFont(mono(14)); g.setColour(juce::Colour(0xffe8e2d4));
        for (int i = 1; i < 4; i++) { const juce::String full(story[i]); const juce::String t = full.substring(0, std::max(0, n)); n -= full.length(); g.drawText(t, juce::Rectangle<float>(0, 100.f + i * 28 - 12, (float) W, 24), juce::Justification::centred); }
        g.setColour(kUi); g.setFont(mono(14, true)); g.drawText("FINAL SCORE " + juce::String((juce::int64) G.score).paddedLeft('0', 6), juce::Rectangle<float>(0, (float) H - 122, (float) W, 24), juce::Justification::centred);
        if (G.modeT > 6) button(g, "NEW GAME", (float) H - 60);
    }
}

void VoidView::openWindow(juce::Component*) {
    struct Win : juce::DocumentWindow {
        Win() : juce::DocumentWindow("VEKTUR VOID", juce::Colour(0xff0a0d14), juce::DocumentWindow::closeButton) {
            setUsingNativeTitleBar(false); setContentOwned(new VoidView(), true); setResizable(true, true); centreWithSize(900, 540); setVisible(true); setAlwaysOnTop(true);
            if (auto* c = getContentComponent()) c->grabKeyboardFocus();
        }
        void closeButtonPressed() override { delete this; }   // X: the game, its timer and its whole state are discarded; the next open starts fresh
    };
    new Win();
}

} // namespace vv
