// MIXER (web: #mixerPanel). One strip per track: pan, MOD (matrix destination MIX:<key>), vertical fader -60..+12 dB, dB label, meter, name.
// SMP2-16 are collapsed behind EXPAND / COLLAPSE SAMPLES (view state only, not saved). Meters read vv_engine_mixer_level().
#include "ScreensMixer.h"
#include "PowerSettings.h"
#include "../../Bridge/vv_engine.h"
#include <vv/model.hpp>

namespace vv {
namespace {
constexpr double kDbMin = -60.0, kDbMax = 12.0;
double fracToDb(double f) { return kDbMin + juce::jlimit(0.0, 1.0, f) * (kDbMax - kDbMin); }
double dbToFrac(double db) { return juce::jlimit(0.0, 1.0, (db - kDbMin) / (kDbMax - kDbMin)); }

struct TrackInfo { const char* key; const char* label; const char* panPath; };
juce::String panPathFor(int i) {
    const juce::String k = mixKey(i);
    if (k == "WT1" || k == "WT2") return "layers.WT.oscs." + juce::String(k.getTrailingIntValue() - 1) + ".pan";
    if (k == "FMA") return "layers.FM.enginePan.0";
    if (k == "FMB") return "layers.FM.enginePan.1";
    if (k == "FM6OP") return "layers.FM.dx7Pan";
    if (k.startsWith("SMP")) return "layers.SMP.oscs." + juce::String(k.getTrailingIntValue() - 1) + ".pan";
    if (k == "PRC1" || k == "PRC2") return "layers.PRO.oscs." + juce::String(k.getTrailingIntValue() - 1) + ".pan";
    return "layers.SID.pan";
}
juce::String labelFor(int i) {
    const juce::String k = mixKey(i);
    if (k == "WT1") return "WT-1"; if (k == "WT2") return "WT-2"; if (k == "FMA") return "FM-A"; if (k == "FMB") return "FM-B"; if (k == "FM6OP") return "FM-6OP";
    if (k.startsWith("SMP")) return "SMP " + juce::String(k.getTrailingIntValue());
    if (k == "PRC1") return "PRC-1"; if (k == "PRC2") return "PRC-2"; return k;
}
}

class MixerScreen::Strip : public juce::Component {
public:
    Strip(Store& s, int idx) : store(s), index(idx), master(idx == kNumMix), key(idx == kNumMix ? "MASTER" : mixKey(idx)), gainPath("mixerGains." + juce::String(mixKey(idx))), panPath(idx == kNumMix ? juce::String() : panPathFor(idx)) {
        pan.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); pan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); pan.setRange(-1, 1, 0.01); pan.setDoubleClickReturnValue(true, 0);
        pan.setRotaryParameters(juce::degreesToRadians(225.f), juce::degreesToRadians(495.f), true);
        pan.onValueChange = [this] { store.set(panPath, pan.getValue()); };
        if (!master) { addAndMakeVisible(pan); mod.setButtonText("MOD"); mod.onClick = [this] { openModMenu(); }; addAndMakeVisible(mod); }
        // M / S / A / E: mute, solo (not Master), automation on / off (green / red), edit this channel's automation. Saved with the preset (extra.mixerMute / mixerSolo / mixerAuto).
        for (int k = 0; k < 4; ++k) { auto* b = msae.add(new juce::TextButton(juce::String("MSAE").substring(k, k + 1))); b->setClickingTogglesState(false); addAndMakeVisible(b);
            b->onClick = [this, k] { if (k == 3) { if (onEdit) onEdit(key); return; } const char* m[] = { "mixerMute", "mixerSolo", "mixerAuto" }; if (k == 1 && master) return; setFlag(m[k], !flagOf(m[k])); refresh(); }; }
        msae[0]->setTooltip("Mute"); msae[1]->setTooltip("Solo"); msae[2]->setTooltip("Automation: green = on (lanes play and dial moves record), red = off"); msae[3]->setTooltip("Edit this channel's automation"); if (master) msae[1]->setEnabled(false);
        name.setText(master ? juce::String("MASTER") : labelFor(idx), juce::dontSendNotification); name.setJustificationType(juce::Justification::centred); name.setFont(VVLookAndFeel::mono(11.f)); name.setColour(juce::Label::textColourId, col::ink); addAndMakeVisible(name);
        refresh();
    }
    std::function<void(const juce::String&)> onEdit;   // E button
    bool flagOf(const char* map) const { return store.flag("extra." + juce::String(map) + "." + key); }
    void setFlag(const char* map, bool on) { if (!store.has("extra")) store.set("extra", juce::var(new juce::DynamicObject())); const juce::String m = "extra." + juce::String(map); if (!store.has(m)) store.set(m, juce::var(new juce::DynamicObject())); store.set(m + "." + key, on); }
    void refresh() {
        { const bool on[3] = { flagOf("mixerMute"), flagOf("mixerSolo"), flagOf("mixerAuto") }; const juce::Colour c[3] = { juce::Colour(0xffd8a020), juce::Colour(0xff2a7fdf), on[2] ? juce::Colour(0xff1f8a4c) : juce::Colour(0xff8a1f1f) };
          for (int k = 0; k < 3; ++k) { msae[k]->setColour(juce::TextButton::buttonColourId, (k == 2 || on[k]) ? c[k] : col::panel); msae[k]->setColour(juce::TextButton::textColourOffId, (k == 2 || on[k]) ? juce::Colours::white : col::dim); } }
        if (master) { const double lin = store.global("masterVolume", 0.8); db = lin <= 0.001 ? kDbMin : juce::jlimit(kDbMin, kDbMax, 20.0 * std::log10(lin)); repaint(); return; }   // master fader: 0 dB = volume 1.0
        if (!pan.isMouseButtonDown()) pan.setValue(store.num(panPath, 0), juce::dontSendNotification);
        bool has = false; const int n = store.count("connections");
        for (int i = 0; i < n && !has; ++i) has = store.str("connections." + juce::String(i) + ".dest") == "MIX:" + key;
        if (has != modOn) { modOn = has; mod.setToggleState(has, juce::dontSendNotification); }
        db = store.num(gainPath, 0); repaint();
    }
    // Meter state: peak (0..1 on the fader scale, standard 20 dB/s fall, 1.5 s hold) and an averaged level; both use dbToFrac so the
    // meter and the fader share one calibrated scale (-60..+12 dB). Ticks: 0 / -6 / -12 labelled; +6 -3 -18 -24 -36 -inf subtle.
    float level = 0, hold = 0, avg = 0; double holdUntil = 0;
    void setLinear(float lin, double nowSec, double dt) {
        const double dbv = 20.0 * std::log10(juce::jmax(1e-6f, lin)); const float fr = (float) dbToFrac(juce::jlimit((double) kDbMin, 12.0, dbv));
        const float fall = (float) (20.0 * dt / 72.0);   // 20 dB/s expressed on the 72 dB scale
        if (fr >= level) { level = fr; holdUntil = nowSec + 1.5; hold = fr; } else if (nowSec > holdUntil) level = juce::jmax(fr, level - fall);
        avg += (fr - avg) * (fr > avg ? 0.55f : 0.12f);
        repaint();
    }
    void paint(juce::Graphics& g) override {
        g.setColour(col::panel); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.f), 5.f);
        auto f = faderArea().toFloat(); const float tx = f.getCentreX(), fr = (float) dbToFrac(db);
        g.setColour(col::bg); g.fillRoundedRectangle(tx - 3.f, f.getY(), 6.f, f.getHeight(), 3.f);
        g.setColour(col::cyanDim); g.fillRoundedRectangle(tx - 3.f, f.getBottom() - fr * f.getHeight(), 6.f, fr * f.getHeight(), 3.f);
        const float ty = f.getBottom() - fr * f.getHeight();
        g.setColour(col::ink); g.fillRoundedRectangle(tx - 11.f, ty - 4.f, 22.f, 8.f, 2.f);
        g.setColour(col::cyan); g.fillRect(tx - 9.f, ty - 0.5f, 18.f, 1.f);
        auto m = meterArea().toFloat(); g.setColour(col::bg); g.fillRect(m);
        auto bar = [&](float v, juce::Rectangle<float> r, float alpha) { const float lv = juce::jlimit(0.f, 1.f, v); const double db0 = 12.0 * 0 + (fracToDb(lv)); g.setColour((db0 > 0 ? col::red : (db0 > -12 ? col::amber : col::green)).withAlpha(alpha)); g.fillRect(r.getX(), r.getBottom() - lv * r.getHeight(), r.getWidth(), lv * r.getHeight()); };
        bar(level, m.withWidth(m.getWidth() * 0.5f), 0.95f); bar(avg, m.withTrimmedLeft(m.getWidth() * 0.5f), 0.7f);
        { const float hy = m.getBottom() - juce::jlimit(0.f, 1.f, hold) * m.getHeight(); g.setColour(col::ink); g.fillRect(m.getX(), hy - 0.5f, m.getWidth() * 0.5f, 1.f); }
        // dB ticks, positioned on the same scale as the fader (absolutely placed, like the web's .mx-tick)
        { struct T { double db; bool lab; }; static const T ticks[] = { { 12, false }, { 6, false }, { 0, true }, { -3, false }, { -6, true }, { -12, true }, { -18, false }, { -24, false }, { -36, false }, { -60, false } };
          g.setFont(VVLookAndFeel::mono(8.f));
          for (auto& t : ticks) { const float y = m.getBottom() - (float) dbToFrac(t.db) * m.getHeight(); g.setColour(col::dim.withAlpha(t.lab ? 0.9f : 0.4f)); g.fillRect(m.getX() - 4.f, y, 4.f, 1.f);
              if (t.lab) g.drawText(t.db == 0 ? "0" : juce::String((int) t.db), juce::Rectangle<float>(m.getX() - 26.f, y - 5.f, 21.f, 10.f), juce::Justification::centredRight); }
          g.drawText(juce::String(juce::CharPointer_UTF8("-\xe2\x88\x9e")), juce::Rectangle<float>(m.getX() - 26.f, m.getBottom() - 10.f, 21.f, 10.f), juce::Justification::centredRight); }
        g.setColour(col::dim); g.setFont(VVLookAndFeel::mono(10.f));
        g.drawText(db <= kDbMin ? juce::String(juce::CharPointer_UTF8("-\xe2\x88\x9e")) : ((db > 0 ? "+" : "") + juce::String(db, 1)), dbArea(), juce::Justification::centred);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(2, 4);
        if (!master) { pan.setBounds(r.removeFromTop(34).withSizeKeepingCentre(32, 32)); mod.setBounds(r.removeFromTop(22).reduced(2, 1)); }
        name.setBounds(r.removeFromBottom(16));
        { auto row = r.removeFromBottom(36); auto top = row.removeFromTop(18); const int w = top.getWidth() / 2; msae[0]->setBounds(top.removeFromLeft(w).reduced(1)); msae[1]->setBounds(top.reduced(1)); msae[2]->setBounds(row.removeFromLeft(w).reduced(1)); msae[3]->setBounds(row.reduced(1)); }
    }
    juce::Rectangle<int> faderArea() const { return faderR; } juce::Rectangle<int> meterArea() const { return meterR; } juce::Rectangle<int> dbArea() const { return dbR; }
    void mouseDown(const juce::MouseEvent& e) override { if (faderR.expanded(8, 8).contains(e.getPosition())) { dragging = true; setFrom(e.y); } }
    void mouseDrag(const juce::MouseEvent& e) override { if (dragging) setFrom(e.y); }
    void mouseUp(const juce::MouseEvent&) override { dragging = false; }
    void mouseDoubleClick(const juce::MouseEvent& e) override { if (faderR.expanded(8, 8).contains(e.getPosition())) { if (master) store.setGlobal("masterVolume", 1.0); else store.set(gainPath, 0.0); refresh(); } }
private:
    void setFrom(int y) { const double f = 1.0 - (double) (y - faderR.getY()) / juce::jmax(1, faderR.getHeight()); const double d = std::round(fracToDb(f) * 10.0) / 10.0;
        if (master) store.setGlobal("masterVolume", d <= kDbMin ? 0.0 : juce::jlimit(0.0, 4.0, std::pow(10.0, d / 20.0))); else store.set(gainPath, d); refresh(); }
    void openModMenu() {
        static const char* src[] = { "lfo1", "lfo2", "lfo3", "lfo4", "lfo5", "lfo6", "velocity", "modwheel", "random", "envWT", "envFM", "envSMP", "envFollow", "macro1", "macro2", "macro3", "macro4" };
        juce::PopupMenu m; m.addSectionHeader("Modulate " + labelFor(index) + " level");
        for (int i = 0; i < (int) std::size(src); ++i) m.addItem(i + 1, src[i]);
        if (modOn) { m.addSeparator(); m.addItem(1000, "Remove all routes to this track"); }
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&mod), [this](int r) {
            if (r <= 0) return;
            if (r == 1000) { for (int i = store.count("connections") - 1; i >= 0; --i) if (store.str("connections." + juce::String(i) + ".dest") == "MIX:" + key) store.remove("connections", i); }
            else { auto* o = new juce::DynamicObject(); o->setProperty("source", src[r - 1]); o->setProperty("dest", "MIX:" + key); o->setProperty("amount", 30); store.append("connections", juce::var(o)); }
            refresh(); });
    }
    Store& store; int index; bool master = false; juce::String key, gainPath, panPath; double db = 0; bool modOn = false, dragging = false; juce::OwnedArray<juce::TextButton> msae;
    juce::Slider pan; juce::TextButton mod; juce::Label name; juce::Rectangle<int> faderR, meterR, dbR;
};

// Master output chain, placed to the right of the channel strips, before the master out: limiter + loudness meter (moved here from Settings), vertical and compact.
class MixerScreen::LimiterCol : public juce::Component, private juce::Timer {
public:
    LimiterCol(Store& s, VVEngine* e) : store(s), engine(e) {
        on = std::make_unique<Toggle>(s, "Limiter", "@limOn", false); addAndMakeVisible(*on);
        const struct { const char* l; const char* p; double lo, hi, def; KnobOpts o; } k[] = { { "Thresh", "@limThr", -24, 0, -1, { "%.1f", " dB", false, 1, 0.62f } }, { "Ceiling", "@limCeil", -12, 0, -1, { "%.1f", " dB", false, 1, 0.62f } },
            { "Release", "@limRel", 5, 1000, 100, { "%.0f", " ms", true, 0, 0.62f } }, { "Knee", "@limKnee", 0, 12, 0, { "%.1f", " dB", false, 1, 0.62f } }, { "Soft clip", "@limSoft", 0, 1, 0, { "%.2f", "", false, 2, 0.62f } } };
        for (auto& x : k) { auto* kn = knobs.add(new Knob(s, x.l, x.p, x.lo, x.hi, x.def, x.o)); addAndMakeVisible(kn); }
        reset.setButtonText("Reset I"); reset.onClick = [this] { if (engine) vv_engine_reset_loudness(engine); }; addAndMakeVisible(reset);
        text.setFont(VVLookAndFeel::mono(10.f)); text.setColour(juce::Label::textColourId, col::ink); text.setJustificationType(juce::Justification::topLeft); addAndMakeVisible(text);
        startTimerHz(5);
    }
    ~LimiterCol() override { stopTimer(); }
    void paint(juce::Graphics& g) override { g.setColour(col::panel); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.f), 5.f); g.setColour(col::dim); g.setFont(VVLookAndFeel::mono(9.f)); g.drawText("LIMITER / LUFS", 2, 2, getWidth() - 4, 12, juce::Justification::centred); }
    void resized() override {
        auto r = getLocalBounds().reduced(3, 4); r.removeFromTop(12); on->setBounds(r.removeFromTop(30));
        // two knobs per row so the whole limiter fits without scrolling
        for (int i = 0; i < knobs.size(); i += 2) {
            const auto ps = knobs[i]->preferredSize(); auto row = r.removeFromTop(ps.y); const int cw = row.getWidth() / 2;
            knobs[i]->setBounds(row.removeFromLeft(cw).withSizeKeepingCentre(juce::jmin(cw, ps.x), ps.y));
            if (i + 1 < knobs.size()) knobs[i + 1]->setBounds(row.withSizeKeepingCentre(juce::jmin(row.getWidth(), ps.x), ps.y));
        }
        reset.setBounds(r.removeFromBottom(22)); text.setBounds(r);
    }
private:
    void timerCallback() override {
        if (!isShowing() || !engine) return; double v[6]; vv_engine_get_loudness(engine, v);
        auto f = [](double x) { return x <= -999 ? juce::String(juce::CharPointer_UTF8("\xe2\x80\x93")) : juce::String(x, 1); };
        text.setText("M " + f(v[0]) + "\nS " + f(v[1]) + "\nI " + f(v[2]) + "\nPk " + f(v[3]) + "/" + f(v[4]) + "\nGR " + (v[5] <= -999 ? juce::String("0.0") : juce::String(v[5], 1)), juce::dontSendNotification);
    }
    Store& store; VVEngine* engine; std::unique_ptr<Toggle> on; juce::OwnedArray<Knob> knobs; juce::TextButton reset; juce::Label text;
};

MixerScreen::MixerScreen(Store& s, VVEngine* e) : store(s), engine(e) {
    for (int i = 0; i < kNumMix; ++i) { auto* st = strips.add(new Strip(s, i)); st->onEdit = [this](const juce::String& k) { if (onEditAutomation) onEditAutomation(k); }; content.addAndMakeVisible(st); }
    limiter = std::make_unique<LimiterCol>(s, e); content.addAndMakeVisible(*limiter);
    masterStrip = strips.add(new Strip(s, kNumMix)); masterStrip->onEdit = [this](const juce::String& k) { if (onEditAutomation) onEditAutomation(k); }; content.addAndMakeVisible(masterStrip);
    expandB.setClickingTogglesState(true); expandB.onClick = [this] { expanded = expandB.getToggleState(); updateButton(); layoutStrips(); };
    content.addAndMakeVisible(expandB); updateButton();
    view.setViewedComponent(&content, false); view.setScrollBarsShown(false, true); addAndMakeVisible(view);
    startTimerHz(20);
}
MixerScreen::~MixerScreen() { stopTimer(); }
void MixerScreen::updateButton() { expandB.setButtonText(expanded ? "COLLAPSE SAMPLES" : "EXPAND SAMPLES"); }
void MixerScreen::resized() { view.setBounds(getLocalBounds()); layoutStrips(); }
void MixerScreen::layoutStrips() {
    const int h = juce::jmax(260, getHeight() - view.getScrollBarThickness() - 4), sw = 62, tw = 78;
    int x = 4;
    for (int i = 0; i < kNumMix; ++i) {   // (the Master strip and limiter column are laid out after the channel strips)
        const bool smp = juce::String(mixKey(i)).startsWith("SMP"); const int n = smp ? juce::String(mixKey(i)).getTrailingIntValue() : 0;
        if (n == 1) { expandB.setBounds(x, 4, tw, h - 8); x += tw + 4; }
        const bool show = !(smp && n > 1 && !expanded); strips[i]->setVisible(show);
        if (show) { strips[i]->setBounds(x, 0, sw, h); x += sw + 3; }
    }
    if (limiter) { limiter->setBounds(x + 6, 0, 168, h); x += 6 + 168 + 3; }   // limiter / LUFS column: right of the channels, before Master
    if (masterStrip) { masterStrip->setBounds(x, 0, sw + 8, h); x += sw + 8 + 3; }
    content.setSize(x + 4, h);
}
void MixerScreen::timerCallback() {
    if (!isShowing()) return; VV_UI_THROTTLE(20);
    for (auto* s : strips) { s->refresh(); }
    if (engine) for (int i = 0; i < kNumMix; ++i) { const float db = 20.f * std::log10(juce::jmax(1e-5f, vv_engine_mixer_level(engine, i))); (void) db; strips[i]->setLinear(vv_engine_mixer_level(engine, i), juce::Time::getMillisecondCounterHiRes() / 1000.0, 1.0 / 20.0); }
    if (engine && masterStrip) { double v[6]; vv_engine_get_loudness(engine, v); const double pk = juce::jmax(v[3] <= -999 ? -90.0 : v[3], v[4] <= -999 ? -90.0 : v[4]); masterStrip->setLinear((float) std::pow(10.0, pk / 20.0), juce::Time::getMillisecondCounterHiRes() / 1000.0, 1.0 / 20.0); }   // master meter: post-limiter peak
}
}  // namespace vv
