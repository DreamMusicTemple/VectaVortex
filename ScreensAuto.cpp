// AUTOMATION screen. A lane is 8 samples per beat in 0..1 (0.5 = no change) stored in the preset at extra.automation.lanes.<key> = { d:1, v:[...] }, exactly what the web app writes;
// the Core plays it (hidden matrix route for destinations, offset on the live value for sources) while a mixer channel of the lane has A on.
// Pencils: Normal, Smooth, Noise, Angle (straight line, ends snap to the grid), Block (level section, edges snap), Erase. Snap: Off / Bar / 1/2 bar / 1/4 bar / 1/8 / 1/16.
// Written without a JUCE toolchain in this environment: UNTESTED - build and try it (see docs/NATIVE_PARITY_AUDIT.md).
#include "ScreensAuto.h"
#include "PowerSettings.h"
#include "../../Bridge/vv_engine.h"
#include <vv/model.hpp>

namespace vv {
namespace {
constexpr int kR = kAutoR;
const char* kToolNames[] = { "Normal", "Smooth", "Noise", "Angle", "Block", "Erase" };
const char* kSnapNames[] = { "Off", "Bar", "1/2 bar", "1/4 bar", "1/8", "1/16" };
const double kSnapBeats[] = { 0, 4, 2, 1, 0.5, 0.25 };
juce::String labelFor(const juce::String& key) { return key.startsWith("S:") ? key.substring(2) + " (source)" : key; }
}

// ---- lane data helpers (the whole extra.automation var is edited and written back; lane keys contain '.' so they cannot be Store paths)
struct AutoData {
    Store& s;
    juce::var root() { juce::var a = s.get("extra.automation"); if (!a.isObject()) { auto* o = new juce::DynamicObject(); o->setProperty("bars", 4); o->setProperty("lanes", juce::var(new juce::DynamicObject())); a = juce::var(o); } if (!a["lanes"].isObject()) a.getDynamicObject()->setProperty("lanes", juce::var(new juce::DynamicObject())); return a; }
    int bars() { return juce::jlimit(1, 32, (int) (double) root().getProperty("bars", 4)); }
    juce::StringArray keys() { juce::StringArray k; if (auto* o = root()["lanes"].getDynamicObject()) for (auto& p : o->getProperties()) if ((int) p.value.getProperty("d", 0)) k.add(p.name.toString()); return k; }
    bool has(const juce::String& key) { for (auto& k : keys()) if (k == key) return true; return false; }
    std::vector<float> lane(const juce::String& key) {
        std::vector<float> v((size_t) (bars() * 4 * kR), 0.5f);
        if (auto* o = root()["lanes"].getDynamicObject()) { auto l = o->getProperty(juce::Identifier(key)); if (auto* a = l["v"].getArray()) if (a->size() > 0) for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (double) a->getReference((int) (i % (size_t) a->size())); }
        return v;
    }
    void ensureExtra() { if (!s.has("extra")) s.set("extra", juce::var(new juce::DynamicObject())); }
    void save(const juce::String& key, const std::vector<float>& v) {
        ensureExtra(); juce::var a = root(); auto* lanes = a["lanes"].getDynamicObject(); bool any = false; for (float x : v) if (std::abs(x - 0.5f) > 0.0005f) { any = true; break; }
        if (!any) lanes->removeProperty(juce::Identifier(key));
        else { juce::Array<juce::var> arr; for (float x : v) arr.add(std::round(x * 1000.f) / 1000.f); auto* l = new juce::DynamicObject(); l->setProperty("d", 1); l->setProperty("v", juce::var(arr)); lanes->setProperty(juce::Identifier(key), juce::var(l)); }
        a.getDynamicObject()->setProperty("bars", bars()); s.set("extra.automation", a);
    }
    void setBars(int b) { ensureExtra(); juce::var a = root(); auto* lanes = a["lanes"].getDynamicObject();
        for (auto& k : keys()) { auto v = lane(k); std::vector<float> nv((size_t) (b * 4 * kR)); for (size_t i = 0; i < nv.size(); ++i) nv[i] = v[i % v.size()]; juce::Array<juce::var> arr; for (float x : nv) arr.add(x); auto* l = new juce::DynamicObject(); l->setProperty("d", 1); l->setProperty("v", juce::var(arr)); lanes->setProperty(juce::Identifier(k), juce::var(l)); }
        a.getDynamicObject()->setProperty("bars", b); s.set("extra.automation", a); }
    bool channelOn(const juce::String& key, const juce::String& forced) {
        const auto chs = forced.isNotEmpty() ? std::vector<int>{ forced == "MASTER" ? kNumMix : mixIndex(forced.toStdString()) } : autoLaneChannels(key.toStdString());
        for (int c : chs) if (c >= 0 && s.flag("extra.mixerAuto." + juce::String(c == kNumMix ? "MASTER" : mixKey(c)))) return true; return false; }
    void setChannel(const juce::String& key, const juce::String& forced, bool on) {
        ensureExtra(); if (!s.has("extra.mixerAuto")) s.set("extra.mixerAuto", juce::var(new juce::DynamicObject()));
        auto chs = forced.isNotEmpty() ? std::vector<int>{ forced == "MASTER" ? kNumMix : mixIndex(forced.toStdString()) } : std::vector<int>{ autoLaneChannels(key.toStdString()).front() };
        for (int c : chs) if (c >= 0) s.set("extra.mixerAuto." + juce::String(c == kNumMix ? "MASTER" : mixKey(c)), on); }
};

class AutomationScreen::Canvas : public juce::Component {
public:
    explicit Canvas(AutomationScreen& o) : owner(o), data{ o.store } {}
    juce::String key; int tool = 0, snap = 5; float pxBeat = 80.f; std::vector<float> v; double playBeat = -1;
    void load() { v = key.isEmpty() ? std::vector<float>() : data.lane(key); if (key.isNotEmpty() && v.empty()) v.assign((size_t) (data.bars() * 4 * kR), 0.5f); setSize(juce::jmax(200, (int) (data.bars() * 4 * pxBeat)), getHeight()); repaint(); }
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat(); const float H = b.getHeight(), W = b.getWidth(); g.setColour(juce::Colour(0xff0a0b0c)); g.fillRect(b);
        const int beats = data.bars() * 4; const double sn = kSnapBeats[snap];
        if (sn > 0 && sn * pxBeat >= 6) { g.setColour(juce::Colours::white.withAlpha(0.05f)); for (double x = 0; x <= beats + 1e-6; x += sn) g.drawVerticalLine((int) (x * pxBeat), 0.f, H); }
        for (int x = 0; x <= beats; ++x) { const bool bar = x % 4 == 0; g.setColour(juce::Colours::white.withAlpha(bar ? 0.28f : 0.1f)); g.drawVerticalLine((int) (x * pxBeat), 0.f, H); if (bar) { g.setFont(VVLookAndFeel::mono(10.f)); g.drawText(juce::String(x / 4 + 1), (int) (x * pxBeat) + 3, 1, 30, 12, juce::Justification::left); } }
        g.setColour(juce::Colour(0xffffc850).withAlpha(0.55f)); g.drawHorizontalLine((int) (H / 2), 0.f, W);
        if (v.empty()) { g.setColour(col::dim); g.drawText("Pick a destination or source to draw its automation", getLocalBounds(), juce::Justification::centred); return; }
        const float sx = pxBeat / (float) kR; juce::Path p; p.startNewSubPath(0, H / 2); for (size_t i = 0; i < v.size(); ++i) { const float y = (1.f - v[i]) * H; p.lineTo((float) i * sx, y); p.lineTo((float) (i + 1) * sx, y); } p.lineTo((float) v.size() * sx, H / 2); p.closeSubPath();
        g.setColour(juce::Colour(0xff3fe08a).withAlpha(0.18f)); g.fillPath(p); g.setColour(juce::Colour(0xff3fe08a)); g.strokePath(p, juce::PathStrokeType(1.5f));
        if (playBeat >= 0) { const float x = (float) std::fmod(playBeat, (double) beats) * pxBeat; g.setColour(juce::Colour(0xffff5a5a)); g.drawVerticalLine((int) x, 0.f, H); }
    }
    void mouseDown(const juce::MouseEvent& e) override { if (v.empty()) return; back = v; b0 = snapBeat(e.x / pxBeat); y0 = valAt(e.y); last = idxAt(e.x); lastV = y0; sm = y0; touched0 = touched1 = last; if (tool < 3 || tool == 5) paint1(e); }
    void mouseDrag(const juce::MouseEvent& e) override { if (v.empty()) return; if (tool == 3 || tool == 4) shape(e); else paint1(e); repaint(); }
    void mouseUp(const juce::MouseEvent&) override {
        if (v.empty()) return;
        if (tool == 1) { const int a = juce::jmax(1, touched0 - 2), z = juce::jmin((int) v.size() - 2, touched1 + 2); for (int k = 0; k < 2; ++k) { auto c = v; for (int i = a; i <= z; ++i) v[(size_t) i] = (c[(size_t) i - 1] + c[(size_t) i] + c[(size_t) i + 1]) / 3.f; } }
        data.save(key, v); if (!data.channelOn(key, owner.channel_)) data.setChannel(key, owner.channel_, true);   // drawing switches the channel's A on
        owner.dirty_ = true; repaint();
    }
    void mouseMagnify(const juce::MouseEvent&, float f) override { pxBeat = juce::jlimit(8.f, 400.f, pxBeat * f); load(); }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { if (e.mods.isCtrlDown() || e.mods.isCommandDown()) { pxBeat = juce::jlimit(8.f, 400.f, pxBeat * (w.deltaY > 0 ? 1.15f : 0.87f)); load(); } else Component::mouseWheelMove(e, w); }
private:
    AutomationScreen& owner; AutoData data; std::vector<float> back; double b0 = 0; float y0 = 0.5f, lastV = 0.5f, sm = 0.5f; int last = 0, touched0 = 0, touched1 = 0; juce::Random rnd;
    int idxAt(int x) const { return juce::jlimit(0, (int) v.size() - 1, (int) (x / pxBeat * kR)); }
    float valAt(int y) const { return juce::jlimit(0.f, 1.f, 1.f - (float) y / (float) juce::jmax(1, getHeight())); }
    double snapBeat(double b) const { const double s = kSnapBeats[snap]; return s > 0 ? std::round(b / s) * s : b; }
    void paint1(const juce::MouseEvent& e) {
        const int idx = idxAt(e.x); const float val = valAt(e.y); const int a = last, z = idx, lo = juce::jmin(a, z), hi = juce::jmax(a, z); if (tool == 1) sm += (val - sm) * 0.3f;
        for (int i = lo; i <= hi; ++i) { const float f = hi == lo ? 1.f : (float) (i - a) / (float) (z - a); const float base = lastV + (val - lastV) * juce::jlimit(0.f, 1.f, f);
            v[(size_t) i] = juce::jlimit(0.f, 1.f, tool == 5 ? 0.5f : tool == 1 ? sm : tool == 2 ? base + (rnd.nextFloat() - 0.5f) * 0.18f : base); }
        last = idx; lastV = tool == 1 ? sm : val; touched0 = juce::jmin(touched0, lo); touched1 = juce::jmax(touched1, hi);
    }
    void shape(const juce::MouseEvent& e) {
        v = back; const double b1 = snapBeat(e.x / pxBeat), bs = juce::jmin(b0, b1), be = juce::jmax(b0, b1); const float val = valAt(e.y); const int n = (int) v.size();
        int i0 = (int) std::lround(bs * kR), i1 = (int) std::lround(be * kR) - 1; if (i1 < i0) i1 = i0 + (kSnapBeats[snap] > 0 ? juce::jmax(0, (int) std::lround(kSnapBeats[snap] * kR) - 1) : 0);
        i0 = juce::jlimit(0, n - 1, i0); i1 = juce::jlimit(0, n - 1, i1);
        const float ya = b0 <= b1 ? y0 : val, yb = b0 <= b1 ? val : y0;
        for (int i = i0; i <= i1; ++i) v[(size_t) i] = tool == 4 ? (std::abs(val - 0.5f) < 0.02f ? 0.5f : val) : (i1 == i0 ? yb : ya + (yb - ya) * (float) (i - i0) / (float) (i1 - i0));
    }
};

AutomationScreen::AutomationScreen(Store& s, VVEngine* e) : store(s), engine(e) {
    canvas = std::make_unique<Canvas>(*this); canvas->setSize(400, 300); view.setViewedComponent(canvas.get(), false); view.setScrollBarsShown(false, true); addAndMakeVisible(view);
    for (auto* b : { &destBox, &srcBox, &barsBox, &snapBox }) addAndMakeVisible(*b);
    for (int i : { 1, 2, 4, 8, 16, 32 }) barsBox.addItem(juce::String(i) + (i == 1 ? " bar" : " bars"), i);
    for (int i = 0; i < 6; ++i) snapBox.addItem(kSnapNames[i], i + 1); snapBox.setSelectedId(6, juce::dontSendNotification);
    barsBox.onChange = [this] { AutoData{ store }.setBars(barsBox.getSelectedId()); canvas->load(); };
    snapBox.onChange = [this] { canvas->snap = snapBox.getSelectedId() - 1; canvas->repaint(); };
    int id = 1; destBox.addItem("- Destination -", 1000); for (auto& g : modDestGroups()) { destBox.addSectionHeading(g.first); for (auto& d : g.second) { destIds_.add(d); destBox.addItem(d, id++); } }
    id = 1; srcBox.addItem("- Source -", 1000); for (auto& sname : modSourceList()) { srcIds_.add(sname); srcBox.addItem(sname, id++); }
    destBox.onChange = [this] { const int i = destBox.getSelectedId() - 1; if (i >= 0 && i < destIds_.size()) { canvas->key = destIds_[i]; srcBox.setSelectedId(1000, juce::dontSendNotification); dirty_ = true; canvas->load(); } };
    srcBox.onChange = [this] { const int i = srcBox.getSelectedId() - 1; if (i >= 0 && i < srcIds_.size()) { canvas->key = "S:" + srcIds_[i]; destBox.setSelectedId(1000, juce::dontSendNotification); dirty_ = true; canvas->load(); } };
    for (int i = 0; i < 6; ++i) { auto* b = toolBtns.add(new juce::TextButton(kToolNames[i])); b->setClickingTogglesState(false); b->onClick = [this, i] { canvas->tool = i; for (int k = 0; k < toolBtns.size(); ++k) toolBtns[k]->setToggleState(k == i, juce::dontSendNotification); }; addAndMakeVisible(b); }
    toolBtns[0]->setToggleState(true, juce::dontSendNotification);
    zoomIn.setButtonText("+"); zoomOut.setButtonText("-"); zoomIn.onClick = [this] { canvas->pxBeat = juce::jmin(400.f, canvas->pxBeat * 1.4f); canvas->load(); }; zoomOut.onClick = [this] { canvas->pxBeat = juce::jmax(8.f, canvas->pxBeat / 1.4f); canvas->load(); };
    laneOn.onClick = [this] { if (canvas->key.isEmpty()) return; AutoData d{ store }; d.setChannel(canvas->key, channel_, !d.channelOn(canvas->key, channel_)); dirty_ = true; };
    clearB.setButtonText("Clear lane"); clearB.onClick = [this] { if (canvas->key.isEmpty()) return; AutoData d{ store }; d.save(canvas->key, std::vector<float>((size_t) (d.bars() * 4 * kR), 0.5f)); canvas->load(); dirty_ = true; };
    chanChip.onClick = [this] { setChannel({}); };
    info.setColour(juce::Label::textColourId, col::dim); info.setFont(VVLookAndFeel::mono(11.f)); addAndMakeVisible(info);
    for (auto* b : { &zoomIn, &zoomOut, &laneOn, &clearB, &chanChip }) addAndMakeVisible(*b);
    store.addListener(this); startTimerHz(15);
}
AutomationScreen::~AutomationScreen() { stopTimer(); store.removeListener(this); }
void AutomationScreen::setChannel(const juce::String& ch) {
    channel_ = ch;
    if (ch.isNotEmpty()) {   // jump to that channel's first lane that already holds automation, else its first destination
        AutoData d{ store }; juce::String pick;
        for (auto& k : d.keys()) { const auto chs = autoLaneChannels(k.toStdString()); const int want = ch == "MASTER" ? kNumMix : mixIndex(ch.toStdString()); if (std::find(chs.begin(), chs.end(), want) != chs.end()) { pick = k; break; } }
        if (pick.isEmpty()) for (auto& g : modDestGroups()) { for (auto& dd : g.second) { const auto chs = autoLaneChannels(dd.toStdString()); const int want = ch == "MASTER" ? kNumMix : mixIndex(ch.toStdString()); if (std::find(chs.begin(), chs.end(), want) != chs.end()) { pick = dd; break; } } if (pick.isNotEmpty()) break; }
        canvas->key = pick; canvas->load();
    }
    dirty_ = true;
}
void AutomationScreen::timerCallback() {
    if (!isShowing()) return; VV_UI_THROTTLE(15);
    if (engine) { canvas->playBeat = vv_engine_seq_running(engine) ? vv_engine_transport_beat(engine) : -1; canvas->repaint(); }
    if (dirty_) { dirty_ = false; rebuild(); }
}
void AutomationScreen::rebuild() {
    AutoData d{ store }; const auto keys = d.keys();
    barsBox.setSelectedId(d.bars(), juce::dontSendNotification);
    for (int i = 0; i < destIds_.size(); ++i) destBox.changeItemText(i + 1, (keys.contains(destIds_[i]) ? juce::String(juce::CharPointer_UTF8("\xe2\x97\x8f ")) : juce::String()) + destIds_[i]);
    for (int i = 0; i < srcIds_.size(); ++i) srcBox.changeItemText(i + 1, (keys.contains("S:" + srcIds_[i]) ? juce::String(juce::CharPointer_UTF8("\xe2\x97\x8f ")) : juce::String()) + srcIds_[i]);
    const bool on = canvas->key.isNotEmpty() && d.channelOn(canvas->key, channel_);
    laneOn.setButtonText(canvas->key.isEmpty() ? "Lane: -" : (on ? "Lane: ON" : "Lane: OFF")); laneOn.setColour(juce::TextButton::buttonColourId, canvas->key.isEmpty() ? col::panel : (on ? juce::Colour(0xff1f8a4c) : juce::Colour(0xff8a1f1f)));
    chanChip.setVisible(channel_.isNotEmpty()); chanChip.setButtonText("Channel: " + channel_ + " x");
    info.setText(canvas->key.isEmpty() ? "Pick a destination or source (the dot marks lanes that already hold automation). Centre line = no change." : "Editing " + labelFor(canvas->key) + (d.has(canvas->key) ? "  [automated]" : "  [empty]") + ".  A button green + transport running = plays and records dial moves.", juce::dontSendNotification);
    chips.clear(); for (auto& k : keys) { auto* b = chips.add(new juce::TextButton(juce::String(juce::CharPointer_UTF8("\xe2\x97\x8f ")) + labelFor(k))); b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff12351f)); b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xff7dffb0)); b->setToggleState(k == canvas->key, juce::dontSendNotification);
        b->onClick = [this, k] { canvas->key = k; canvas->load(); dirty_ = true; }; addAndMakeVisible(b); }
    resized(); canvas->load();
}
void AutomationScreen::resized() {
    auto r = getLocalBounds().reduced(6); auto row = r.removeFromTop(28);
    destBox.setBounds(row.removeFromLeft(220)); row.removeFromLeft(4); srcBox.setBounds(row.removeFromLeft(130)); row.removeFromLeft(4); barsBox.setBounds(row.removeFromLeft(80)); row.removeFromLeft(4); snapBox.setBounds(row.removeFromLeft(90)); row.removeFromLeft(4);
    zoomOut.setBounds(row.removeFromLeft(30)); zoomIn.setBounds(row.removeFromLeft(30)); row.removeFromLeft(4); laneOn.setBounds(row.removeFromLeft(90)); row.removeFromLeft(4); clearB.setBounds(row.removeFromLeft(90));
    r.removeFromTop(4); row = r.removeFromTop(26); for (auto* b : toolBtns) { b->setBounds(row.removeFromLeft(78)); row.removeFromLeft(3); } row.removeFromLeft(8); chanChip.setBounds(row.removeFromLeft(150));
    r.removeFromTop(4); info.setBounds(r.removeFromTop(18)); auto chipRow = r.removeFromBottom(28); int x = chipRow.getX(); for (auto* c : chips) { const int w = juce::jmin(220, c->getButtonText().length() * 8 + 20); if (x + w > chipRow.getRight()) { c->setVisible(false); continue; } c->setVisible(true); c->setBounds(x, chipRow.getY(), w, 24); x += w + 4; }
    view.setBounds(r); canvas->setSize(juce::jmax(r.getWidth(), (int) (AutoData{ store }.bars() * 4 * canvas->pxBeat)), juce::jmax(160, r.getHeight() - view.getScrollBarThickness()));
}
}  // namespace vv
