// Piano roll over layers.<L>.patterns.<slot>.roll = { bars, loopOn, loopStart, loopEnd, notes:[{t,l,p,v,prob,rat,pl,tr}] } (beats = quarter notes) - the same data the web app saves; the Core plays it.
// Tap empty space = add a note (length = grid), double-tap a note = delete, drag = move, drag the right edge = resize, Select tool (or Shift-drag) = marquee; arrows shift a group by semitone / octave;
// Quantise snaps the selection (or everything); grid 1/4..1/32 + triplets, snap on/off; velocity lane; piano keys on the left sound notes; scale lock uses the keyboard's scale; Underlay shows the other layers' notes.
// REC (round 19): the REC button arms the engine's lock-free recorder (Core roll_rec.hpp); keyboard / MIDI notes with velocity, pitch bend and mod wheel are written at the playhead (Overdub / Replace, REC Q snaps to the grid) and the bend / mod lanes (roll.lanes) replay with the roll.
// Written without a JUCE toolchain in this environment: UNTESTED - build and try it (see docs/NATIVE_PARITY_AUDIT.md). Not yet ported: chord-maker reflection onto the roll, the per-note P-lock inspector (probability / ratchet are in the right-click menu).
#include "ScreensRoll.h"
#include "PowerSettings.h"
#include "Scales.h"
#include "../../Bridge/vv_engine.h"
#include <vv/model.hpp>
#include <set>
#include <optional>
#include <algorithm>
#include <cmath>

namespace vv {
namespace {
const char* kLayers[] = { "WT", "FM", "SMP", "PRO", "SID" };
const char* kGridNames[] = { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };
const double kGridBeats[] = { 1, 0.5, 0.25, 0.125, 1.0 / 3, 1.0 / 6 };
constexpr int kKeyW = 44, kLo = 24, kHi = 96;   // visible pitch range C1..C7
bool isBlack(int n) { const int m = n % 12; return m == 1 || m == 3 || m == 6 || m == 8 || m == 10; }
}

class RollPage : public juce::Component, private Store::Listener, private juce::Timer {
public:
    RollPage(Store& s, VVEngine* e) : store(s), engine(e) {
        for (int i = 0; i < 5; ++i) { auto* b = layerB.add(new juce::TextButton(kLayers[i])); b->onClick = [this, i] { layer = i; sel.clear(); refreshButtons(); repaint(); }; addAndMakeVisible(b); }
        gridBox.addItem("Grid off", 100); for (int i = 0; i < 6; ++i) gridBox.addItem(kGridNames[i], i + 1); gridBox.setSelectedId(3, juce::dontSendNotification); gridBox.onChange = [this] { const int id = gridBox.getSelectedId(); snapOn = id != 100; if (snapOn) grid = id - 1; };
        quantB.setButtonText("Quantise"); quantB.onClick = [this] { quantise(); }; velB.setButtonText("Velocity"); velB.setClickingTogglesState(true); velB.onClick = [this] { velOpen = velB.getToggleState(); repaint(); };
        underB.setButtonText("Underlay"); underB.setClickingTogglesState(true); underB.onClick = [this] { underlay = underB.getToggleState(); repaint(); };
        toolB.setButtonText("Draw"); toolB.onClick = [this] { selectTool = !selectTool; toolB.setButtonText(selectTool ? "Select" : "Draw"); };
        const char* an[] = { juce::CharPointer_UTF8("\xe2\x96\xb2"), juce::CharPointer_UTF8("\xe2\x96\xbc"), "8va+", "8va-" }; const int dn[] = { 1, -1, 12, -12 };
        for (int i = 0; i < 4; ++i) { auto* b = arrows.add(new juce::TextButton(an[i])); b->onClick = [this, i, dn] { shift(dn[i]); }; addAndMakeVisible(b); }
        delB.setButtonText("Delete"); delB.onClick = [this] { deleteSel(); }; barsBox.addItem("1 bar", 1); for (int b : { 2, 4, 8, 16, 32, 64 }) barsBox.addItem(juce::String(b) + " bars", b); barsBox.onChange = [this] { setBars(barsBox.getSelectedId()); };
        recB.setButtonText(juce::CharPointer_UTF8("\xe2\x97\x8f REC")); recB.setClickingTogglesState(true); recB.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffb01818)); recB.onClick = [this] { setRec(recB.getToggleState()); };
        recModeBox.addItem("Overdub", 1); recModeBox.addItem("Replace", 2); recModeBox.setSelectedId(1, juce::dontSendNotification); recQB.setButtonText("REC Q"); recQB.setClickingTogglesState(true);
        recClrB.setButtonText("Clr bend/mod"); recClrB.onClick = [this] { ensureRoll(); store.set(rollPath() + ".lanes", emptyLanes()); };
        for (auto* c : std::initializer_list<juce::Component*>{ &recB, &recModeBox, &recQB, &recClrB }) addAndMakeVisible(c);
        loopB.setButtonText("Loop"); loopB.setClickingTogglesState(true); loopB.onClick = [this] { write("loopOn", loopB.getToggleState()); };
        zoomIn.setButtonText("+"); zoomOut.setButtonText("-"); zoomIn.onClick = [this] { pxBeat = juce::jmin(400.f, pxBeat * 1.4f); repaint(); }; zoomOut.onClick = [this] { pxBeat = juce::jmax(10.f, pxBeat / 1.4f); repaint(); };
        for (auto* c : std::initializer_list<juce::Component*>{ &gridBox, &quantB, &velB, &underB, &toolB, &delB, &barsBox, &loopB, &zoomIn, &zoomOut }) addAndMakeVisible(c);
        store.addListener(this); startTimerHz(20); refreshButtons();
    }
    ~RollPage() override { stopTimer(); store.removeListener(this); }
    void resized() override {
        auto r = getLocalBounds().reduced(4); auto row = r.removeFromTop(28); for (auto* b : layerB) { b->setBounds(row.removeFromLeft(56).reduced(1)); } row.removeFromLeft(6);
        gridBox.setBounds(row.removeFromLeft(86)); row.removeFromLeft(3); quantB.setBounds(row.removeFromLeft(78)); row.removeFromLeft(3); toolB.setBounds(row.removeFromLeft(64)); row.removeFromLeft(3); velB.setBounds(row.removeFromLeft(72)); row.removeFromLeft(3); underB.setBounds(row.removeFromLeft(78)); row.removeFromLeft(6); recB.setBounds(row.removeFromLeft(64)); row.removeFromLeft(3); recModeBox.setBounds(row.removeFromLeft(84)); row.removeFromLeft(3); recQB.setBounds(row.removeFromLeft(58)); row.removeFromLeft(3); recClrB.setBounds(row.removeFromLeft(96));
        r.removeFromTop(3); row = r.removeFromTop(28); for (auto* b : arrows) { b->setBounds(row.removeFromLeft(44).reduced(1)); } row.removeFromLeft(6); delB.setBounds(row.removeFromLeft(64)); row.removeFromLeft(6); barsBox.setBounds(row.removeFromLeft(86)); row.removeFromLeft(3); loopB.setBounds(row.removeFromLeft(56)); row.removeFromLeft(6); zoomOut.setBounds(row.removeFromLeft(30)); zoomIn.setBounds(row.removeFromLeft(30));
        r.removeFromTop(3); area = r;
    }
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(0xff0a0b0c)); g.fillRect(area); auto vr = velArea(), gr = gridArea(); const double len = bars() * 4.0;
        g.saveState(); g.reduceClipRegion(area);
        int nsc; const KbScale* tab = kbScales(nsc); (void) tab; const int scIdx = (int) store.global("scaleIdx", 0), root = (int) store.global("scaleRoot", 0);
        for (int n = kHi; n >= kLo; --n) { const auto row = rowRect(n); g.setColour(isBlack(n) ? juce::Colour(0xff101214) : juce::Colour(0xff15181a)); g.fillRect(row);
            if (scIdx > 0 && kbInScale(scIdx, root, n)) { g.setColour(juce::Colours::cyan.withAlpha(0.05f)); g.fillRect(row); }
            g.setColour(juce::Colours::white.withAlpha(n % 12 == 0 ? 0.18f : 0.05f)); g.drawHorizontalLine(row.getY(), (float) row.getX(), (float) row.getRight());
            g.setColour(isBlack(n) ? juce::Colours::black : juce::Colours::white.withAlpha(0.85f)); g.fillRect(juce::Rectangle<int>(area.getX(), row.getY(), kKeyW - 1, rowH() - 1)); if (n % 12 == 0) { g.setColour(juce::Colours::black); g.setFont(9.f); g.drawText("C" + juce::String(n / 12 - 1), area.getX() + 2, row.getY(), kKeyW - 6, rowH(), juce::Justification::centredRight); } }
        for (double b = 0; b <= len + 1e-6; b += 1) { const int x = xOf(b); g.setColour(juce::Colours::white.withAlpha(std::fmod(b, 4) < 1e-6 ? 0.3f : 0.1f)); g.drawVerticalLine(x, (float) gr.getY(), (float) gr.getBottom()); }
        if (snapOn) { const double gb = kGridBeats[grid]; if (gb * pxBeat >= 6) { g.setColour(juce::Colours::white.withAlpha(0.04f)); for (double b = 0; b <= len; b += gb) g.drawVerticalLine(xOf(b), (float) gr.getY(), (float) gr.getBottom()); } }
        if (loopOn()) { g.setColour(juce::Colours::orange.withAlpha(0.08f)); g.fillRect(juce::Rectangle<int>(xOf(loopStart() * 4), gr.getY(), xOf(loopEnd() * 4) - xOf(loopStart() * 4), gr.getHeight())); }
        if (underlay) for (int l = 0; l < 5; ++l) if (l != layer) { const auto pn = patPath(l); for (int i = 0, n = store.count(pn + ".roll.notes"); i < n; ++i) { const auto q = pn + ".roll.notes." + juce::String(i); drawNote(g, store.num(q + ".t"), store.num(q + ".l", 0.25), (int) store.num(q + ".p", 60), 0.8, juce::Colours::grey.withAlpha(0.18f), false); } }
        const auto np = notesPath(); for (int i = 0, n = store.count(np); i < n; ++i) { const auto q = np + "." + juce::String(i); const bool s = sel.count(i) > 0; drawNote(g, store.num(q + ".t"), store.num(q + ".l", 0.25), (int) store.num(q + ".p", 60), store.num(q + ".v", 0.9), s ? juce::Colour(0xffffc850) : juce::Colour(0xff3fe08a), true); }
        if (marquee) { g.setColour(juce::Colours::white.withAlpha(0.15f)); g.fillRect(*marquee); g.setColour(juce::Colours::white); g.drawRect(*marquee); }
        if (playBeat >= 0) { g.setColour(juce::Colours::red); const double bt = loopOn() ? loopStart() * 4 + std::fmod(playBeat, juce::jmax(4.0, (loopEnd() - loopStart()) * 4)) : std::fmod(playBeat, len); g.drawVerticalLine(xOf(bt), (float) gr.getY(), (float) area.getBottom()); }
        if (velOpen) { g.setColour(juce::Colour(0xff111416)); g.fillRect(vr); for (int i = 0, n = store.count(np); i < n; ++i) { const auto q = np + "." + juce::String(i); const double v = store.num(q + ".v", 0.9); g.setColour(sel.count(i) ? juce::Colour(0xffffc850) : juce::Colour(0xff3fe08a)); g.fillRect(xOf(store.num(q + ".t")), vr.getBottom() - (int) (v * vr.getHeight()), 4, (int) (v * vr.getHeight())); } }
        if (velOpen) { const auto vr2 = velArea();   // recorded pitch-bend (cyan) and mod-wheel (magenta) lanes
            for (int k = 0; k < 2; ++k) { auto* a = store.get(rollPath() + ".lanes." + (k ? "mod" : "bend")).getArray(); if (!a || a->isEmpty()) continue;
                g.setColour(k ? juce::Colour(0xffff6fd8) : juce::Colour(0xff4fd1ff)); juce::Path pth; float py = 0; bool first = true;
                for (auto& q : *a) { const float x = (float) xOf((double) q[0]); const double v = (double) q[1]; const float y = k ? vr2.getBottom() - 4 - (float) v * (vr2.getHeight() - 8) : vr2.getCentreY() - (float) v * (vr2.getHeight() / 2 - 4);
                    if (first) { pth.startNewSubPath((float) vr2.getX(), y); first = false; } else pth.lineTo(x, py); pth.lineTo(x, y); py = y; }
                pth.lineTo((float) vr2.getRight(), py); g.strokePath(pth, juce::PathStrokeType(1.4f)); } }
        if (recB.getToggleState()) { g.setColour(juce::Colours::red); g.fillEllipse((float) area.getRight() - 18, (float) area.getY() + 6, 10.f, 10.f); }
        g.restoreState();
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (!area.contains(e.getPosition())) return; const auto p = e.getPosition(); dragMode = None;
        if (p.x < area.getX() + kKeyW) { const int n = pitchAt(p.y); if (n >= 0 && engine) { vv_engine_note_on(engine, n, 0.8f); heldKey = n; } return; }
        if (velOpen && velArea().contains(p)) { dragMode = Vel; setVel(p); return; }
        if (e.mods.isRightButtonDown()) { const int hit = noteAt(p); if (hit >= 0) noteMenu(hit); return; }
        const int hit = noteAt(p);
        if (e.getNumberOfClicks() >= 2 && hit >= 0) { remove({ hit }); sel.clear(); repaint(); return; }
        if (hit >= 0) { if (!sel.count(hit)) { if (!e.mods.isShiftDown()) sel.clear(); sel.insert(hit); } dragMode = (p.x > xOf(num(hit, "t") + num(hit, "l", 0.25)) - 6) ? Resize : (e.mods.isAltDown() ? CopyMove : Move); anchor = p; origNotes = snapshot(); if (dragMode == CopyMove) duplicateSel(); repaint(); return; }
        if (selectTool || e.mods.isShiftDown()) { dragMode = Marquee; anchor = p; marquee = juce::Rectangle<int>(p, p); if (!e.mods.isShiftDown()) sel.clear(); return; }
        sel.clear(); addNote(p); repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        const auto p = e.getPosition();
        if (dragMode == Marquee) { marquee = juce::Rectangle<int>(anchor, p).getIntersection(gridArea()); sel.clear(); for (int i = 0, n = store.count(notesPath()); i < n; ++i) { juce::Rectangle<int> r(xOf(num(i, "t")), rowRect((int) num(i, "p", 60)).getY(), juce::jmax(3, (int) (num(i, "l", 0.25) * pxBeat)), rowH()); if (r.intersects(*marquee)) sel.insert(i); } repaint(); }
        else if (dragMode == Vel) setVel(p);
        else if (dragMode == Move || dragMode == CopyMove) { const double db = snapB((p.x - anchor.x) / pxBeat, true); const int dp = (anchor.y - p.y) / rowH() * -1; applyOffset(db, -dp); }
        else if (dragMode == Resize) { for (int i : sel) { const double l = juce::jmax(0.03, snapB(origNotes[(size_t) i].l + (p.x - anchor.x) / pxBeat, false)); store.set(notesPath() + "." + juce::String(i) + ".l", l); } }
    }
    void mouseUp(const juce::MouseEvent&) override { if (heldKey >= 0 && engine) { vv_engine_note_off(engine, heldKey); heldKey = -1; } dragMode = None; marquee.reset(); repaint(); }
    void mouseMagnify(const juce::MouseEvent&, float f) override { pxBeat = juce::jlimit(10.f, 400.f, pxBeat * f); repaint(); }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {   // two-finger scroll: horizontal = time, vertical = pitch; Ctrl/Cmd = zoom
        if (e.mods.isCtrlDown() || e.mods.isCommandDown()) { pxBeat = juce::jlimit(10.f, 400.f, pxBeat * (w.deltaY > 0 ? 1.15f : 0.87f)); } else { scrollX = juce::jmax(0.0, scrollX - w.deltaX * 400.0); scrollY = juce::jlimit(0.0, (double) ((kHi - kLo) * rowH()), scrollY - w.deltaY * 200.0); } repaint(); }
private:
    enum Drag { None, Move, CopyMove, Resize, Marquee, Vel };
    // ---- REC. The engine captures the user's input on the audio thread (lock-free); this page drains it ~20 Hz and writes the finished notes / lanes into the roll.
    juce::TextButton recB, recQB, recClrB; juce::ComboBox recModeBox; std::vector<char> recBuf = std::vector<char>(65536);
    static juce::var emptyLanes() { auto* o = new juce::DynamicObject(); o->setProperty("bend", juce::var(juce::Array<juce::var>())); o->setProperty("mod", juce::var(juce::Array<juce::var>())); return juce::var(o); }
    void setRec(bool on) {
        if (!engine) { recB.setToggleState(false, juce::dontSendNotification); return; }
        if (on) {
            ensureRoll(); if (recModeBox.getSelectedId() == 2) { store.set(notesPath(), juce::var(juce::Array<juce::var>())); store.set(rollPath() + ".lanes", emptyLanes()); sel.clear(); }   // Replace clears the roll when recording starts
            vv_engine_roll_rec_set(engine, 1); if (!vv_engine_seq_running(engine)) vv_engine_start_sequencer(engine, 1);   // starts the transport when stopped, like the web
        } else { vv_engine_roll_rec_set(engine, 0); drainRec(true); }
        repaint();
    }
    void mergeLane(const char* kind, const juce::var& pts, const juce::var& cuts) {
        auto* pa = pts.getArray(); auto* ca = cuts.getArray(); if ((!pa || pa->isEmpty()) && (!ca || ca->isEmpty())) return;
        if (!store.has(rollPath() + ".lanes")) store.set(rollPath() + ".lanes", emptyLanes());
        const juce::String lp = rollPath() + ".lanes." + kind; juce::Array<juce::var> keep;
        if (auto* cur = store.get(lp).getArray()) for (auto& q : *cur) { const double t = (double) q[0]; bool cut = false; if (ca) for (auto& c : *ca) if (t >= (double) c[0] - 1e-9 && t <= (double) c[1] + 1e-9) { cut = true; break; } if (!cut) keep.add(q); }
        if (pa) for (auto& q : *pa) keep.add(q);
        std::stable_sort(keep.begin(), keep.end(), [](const juce::var& a, const juce::var& b) { return (double) a[0] < (double) b[0]; });
        store.set(lp, juce::var(keep));
    }
    void drainRec(bool final_) {
        if (!engine) return; const bool lo = loopOn(); const double ls = lo ? loopStart() * 4 : 0, le = lo ? juce::jmax(ls + 4, loopEnd() * 4) : bars() * 4.0;
        const size_t n = vv_engine_roll_rec_drain_json(engine, ls, le, bars() * 4.0, recQB.getToggleState() ? kGridBeats[grid] : 0.0, final_ ? 1 : 0, recBuf.data(), recBuf.size()); if (!n) return;
        const juce::var j = juce::JSON::parse(juce::String::fromUTF8(recBuf.data(), (int) n)); auto* o = j.getDynamicObject(); if (!o) return;
        if (auto* na = o->getProperty("notes").getArray()) for (auto& q : *na) { auto* nn = new juce::DynamicObject(); nn->setProperty("t", q["t"]); nn->setProperty("l", q["l"]); nn->setProperty("p", q["p"]); nn->setProperty("v", q["v"]); nn->setProperty("prob", 100); nn->setProperty("rat", 1); nn->setProperty("pl", juce::var(new juce::DynamicObject())); nn->setProperty("tr", 0); ensureRoll(); store.append(notesPath(), juce::var(nn)); }
        mergeLane("bend", o->getProperty("bend"), o->getProperty("bendCut")); mergeLane("mod", o->getProperty("mod"), o->getProperty("modCut"));
        if (final_) recB.setToggleState(false, juce::dontSendNotification);
    }
    Store& store; VVEngine* engine; int layer = 0, grid = 2, heldKey = -1; bool snapOn = true, velOpen = false, underlay = false, selectTool = false; float pxBeat = 80.f; double scrollX = 0, scrollY = 0, playBeat = -1;
    juce::OwnedArray<juce::TextButton> layerB, arrows; juce::ComboBox gridBox, barsBox; juce::TextButton quantB, velB, underB, toolB, delB, loopB, zoomIn, zoomOut; juce::Rectangle<int> area; std::set<int> sel; Drag dragMode = None; juce::Point<int> anchor; std::optional<juce::Rectangle<int>> marquee;
    struct N { double t, l, v, prob; int p, rat; }; std::vector<N> origNotes;
    void storeChanged() override { repaint(); } void timerCallback() override { if (recB.getToggleState()) drainRec(false); if (!isShowing()) return; VV_UI_THROTTLE(20); playBeat = (engine && vv_engine_seq_running(engine)) ? vv_engine_transport_beat(engine) : -1; refreshButtons(); repaint(); }
    juce::String L() const { return kLayers[layer]; } juce::String patPath(int l) const { return "layers." + juce::String(kLayers[l]) + ".patterns." + juce::String((int) store.num("layers." + juce::String(kLayers[l]) + ".currentSlot", 0)); }
    juce::String rollPath() const { return patPath(layer) + ".roll"; } juce::String notesPath() const { return rollPath() + ".notes"; }
    int bars() const { return juce::jlimit(1, 64, (int) store.num(rollPath() + ".bars", 4)); } bool loopOn() const { return store.flag(rollPath() + ".loopOn", true); } double loopStart() const { return store.num(rollPath() + ".loopStart", 0); } double loopEnd() const { return store.num(rollPath() + ".loopEnd", bars()); }
    double num(int i, const char* k, double d = 0) const { return store.num(notesPath() + "." + juce::String(i) + "." + k, d); }
    void ensureRoll() { if (store.has(rollPath())) return; auto* r = new juce::DynamicObject(); r->setProperty("bars", 4); r->setProperty("loopOn", true); r->setProperty("loopStart", 0); r->setProperty("loopEnd", 4); r->setProperty("notes", juce::var(juce::Array<juce::var>())); store.set(rollPath(), juce::var(r)); }   // a step pattern becomes a roll on first edit
    void write(const char* k, const juce::var& v) { ensureRoll(); store.set(rollPath() + "." + k, v); }
    void setBars(int b) { ensureRoll(); store.set(rollPath() + ".bars", b); if (loopEnd() > b || loopEnd() >= bars()) store.set(rollPath() + ".loopEnd", b); }
    void refreshButtons() { for (int i = 0; i < layerB.size(); ++i) layerB[i]->setToggleState(i == layer, juce::dontSendNotification); barsBox.setSelectedId(bars(), juce::dontSendNotification); loopB.setToggleState(loopOn(), juce::dontSendNotification); }
    int rowH() const { return 14; } juce::Rectangle<int> velArea() const { return velOpen ? juce::Rectangle<int>(area.getX() + kKeyW, area.getBottom() - 70, area.getWidth() - kKeyW, 70) : juce::Rectangle<int>(); }
    juce::Rectangle<int> gridArea() const { auto r = area.withTrimmedLeft(kKeyW); if (velOpen) r.removeFromBottom(70); return r; }
    int xOf(double beat) const { return area.getX() + kKeyW + (int) ((beat * pxBeat) - scrollX); } double beatAt(int x) const { return ((x - area.getX() - kKeyW) + scrollX) / pxBeat; }
    juce::Rectangle<int> rowRect(int n) const { return { area.getX() + kKeyW, area.getY() + (int) ((kHi - n) * rowH() - scrollY), area.getWidth() - kKeyW, rowH() }; }
    int pitchAt(int y) const { return kHi - (int) std::floor((y - area.getY() + scrollY) / rowH()); }
    double snapB(double b, bool signedOk) const { if (!snapOn) return b; const double g = kGridBeats[grid]; return signedOk ? std::round(b / g) * g : std::round(b / g) * g; }
    int scaleSnap(int p) const { const int idx = (int) store.global("scaleIdx", 0), root = (int) store.global("scaleRoot", 0); if (store.global("scaleLock", 0) == 0 || idx <= 0) return p; for (int d = 0; d < 7; ++d) { if (kbInScale(idx, root, p + d)) return p + d; if (kbInScale(idx, root, p - d)) return p - d; } return p; }
    void drawNote(juce::Graphics& g, double t, double l, int p, double v, juce::Colour c, bool border) { const auto row = rowRect(p); juce::Rectangle<float> r((float) xOf(t), (float) row.getY() + 1, juce::jmax(3.f, (float) (l * pxBeat)), (float) rowH() - 2); g.setColour(border ? c.withAlpha((float) (0.45 + 0.55 * v)) : c); g.fillRoundedRectangle(r, 2.f); if (border) { g.setColour(juce::Colours::black.withAlpha(0.5f)); g.drawRoundedRectangle(r, 2.f, 1.f); } }
    int noteAt(juce::Point<int> p) const { for (int i = store.count(notesPath()) - 1; i >= 0; --i) { juce::Rectangle<int> r(xOf(num(i, "t")), rowRect((int) num(i, "p", 60)).getY(), juce::jmax(5, (int) (num(i, "l", 0.25) * pxBeat)), rowH()); if (r.contains(p)) return i; } return -1; }
    void addNote(juce::Point<int> p) {
        ensureRoll(); const int pitch = scaleSnap(pitchAt(p.y)); if (pitch < 0 || pitch > 127) return; const double gb = snapOn ? kGridBeats[grid] : 0.25, t = juce::jmax(0.0, snapOn ? std::floor(beatAt(p.x) / gb) * gb : beatAt(p.x));
        auto* n = new juce::DynamicObject(); n->setProperty("t", t); n->setProperty("l", gb); n->setProperty("p", pitch); n->setProperty("v", 0.9); n->setProperty("prob", 100); n->setProperty("rat", 1); n->setProperty("pl", juce::var(new juce::DynamicObject())); n->setProperty("tr", 0);
        if (t + gb > bars() * 4) setBars(juce::jmin(64, (int) std::ceil((t + gb) / 4))); store.append(notesPath(), juce::var(n)); if (engine) { vv_engine_note_on(engine, pitch, 0.8f); juce::Timer::callAfterDelay(160, [e = engine, pitch] { vv_engine_note_off(e, pitch); }); }
    }
    std::vector<N> snapshot() const { std::vector<N> v; for (int i = 0, n = store.count(notesPath()); i < n; ++i) v.push_back({ num(i, "t"), num(i, "l", 0.25), num(i, "v", 0.9), num(i, "prob", 100), (int) num(i, "p", 60), (int) num(i, "rat", 1) }); return v; }
    void applyOffset(double db, int dp) { std::vector<std::pair<juce::String, juce::var>> kv; for (int i : sel) { if (i >= (int) origNotes.size()) continue; kv.push_back({ notesPath() + "." + juce::String(i) + ".t", juce::jmax(0.0, origNotes[(size_t) i].t + db) }); kv.push_back({ notesPath() + "." + juce::String(i) + ".p", scaleSnap(juce::jlimit(0, 127, origNotes[(size_t) i].p + dp)) }); } store.setMany(kv); }
    void duplicateSel() { std::set<int> ns; int cnt = store.count(notesPath()); for (int i : sel) { juce::var n = store.get(notesPath() + "." + juce::String(i)); if (auto* o = n.getDynamicObject()) { auto* c = new juce::DynamicObject(); for (auto& pr : o->getProperties()) c->setProperty(pr.name, pr.value); store.append(notesPath(), juce::var(c)); ns.insert(cnt++); } } sel = ns; origNotes = snapshot(); }
    void remove(std::vector<int> idx) { std::sort(idx.rbegin(), idx.rend()); for (int i : idx) store.remove(notesPath(), i); }
    void deleteSel() { remove(std::vector<int>(sel.begin(), sel.end())); sel.clear(); }
    void shift(int d) { origNotes = snapshot(); if (sel.empty()) for (int i = 0; i < (int) origNotes.size(); ++i) sel.insert(i); applyOffset(0, d); }   // arrows: whole roll when nothing is selected
    void quantise() { const double gb = snapOn ? kGridBeats[grid] : 0.25; std::vector<std::pair<juce::String, juce::var>> kv; const int n = store.count(notesPath()); for (int i = 0; i < n; ++i) { if (!sel.empty() && !sel.count(i)) continue; const double t = num(i, "t"), l = num(i, "l", 0.25); kv.push_back({ notesPath() + "." + juce::String(i) + ".t", std::round(t / gb) * gb }); kv.push_back({ notesPath() + "." + juce::String(i) + ".l", juce::jmax(gb, std::round(l / gb) * gb) }); } store.setMany(kv); }
    void setVel(juce::Point<int> p) { const auto vr = velArea(); const double v = juce::jlimit(0.02, 1.0, 1.0 - (double) (p.y - vr.getY()) / vr.getHeight()); const double t = beatAt(p.x); int best = -1; double bd = 1e9; for (int i = 0, n = store.count(notesPath()); i < n; ++i) { const double d = std::abs(num(i, "t") - t); if (d < bd) { bd = d; best = i; } } if (best >= 0 && bd * pxBeat < 12) store.set(notesPath() + "." + juce::String(best) + ".v", v); }
    void noteMenu(int i) { juce::PopupMenu m; m.addSectionHeader("Note"); for (int pr : { 100, 75, 50, 25 }) m.addItem(pr, "Probability " + juce::String(pr) + " %"); for (int r = 1; r <= 4; ++r) m.addItem(200 + r, "Ratchet x" + juce::String(r)); m.addItem(300, "Delete");
        m.showMenuAsync({}, [this, i](int r) { if (r <= 0) return; if (r == 300) { remove({ i }); sel.clear(); } else if (r >= 200) store.set(notesPath() + "." + juce::String(i) + ".rat", r - 200); else store.set(notesPath() + "." + juce::String(i) + ".prob", r); }); }
};

juce::Component* makeRollPage(Store& s, VVEngine* e) { return new RollPage(s, e); }
}  // namespace vv
