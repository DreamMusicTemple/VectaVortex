// SEQ screen: transport + arpeggiator, and the 4-layer step sequencer (pattern slots, chains, ratchets, probability, p-locks).
#include "ClipLauncher.h"
#include "ArpAdvPanel.h"
#include "Scales.h"
#include "PowerSettings.h"
#include "Screens.h"
#include "vv/chordmaker.hpp"
#include "ScreensRoll.h"

namespace vv {
namespace {

const char* kLayers[] = { "WT", "FM", "SMP", "PRO" };

// Which oscillators the arpeggiator drives (ids match the web app's state.arp.target)
juce::StringArray makeArpTargets(bool labels) {
    juce::StringArray r; r.addArray(labels ? juce::StringArray{ "All layers", "Wavetable 1", "Wavetable 2", "FM A", "FM B", "Procedural" } : juce::StringArray{ "ALL", "WT1", "WT2", "FMA", "FMB", "PRO" });
    for (int i = 1; i <= 16; ++i) r.add(labels ? "Sample " + juce::String(i) : "S" + juce::String(i));
    return r;
}
const juce::StringArray kArpTargets = makeArpTargets(false), kArpTargetLabels = makeArpTargets(true);

// A radio row of small buttons.
struct ButtonRow : juce::Component {
    void set(const juce::StringArray& names, int selected) {
        buttons.clear(); for (int i = 0; i < names.size(); ++i) {
            auto* b = buttons.add(new juce::TextButton(names[i])); addAndMakeVisible(b);
            b->setClickingTogglesState(false); b->setToggleState(i == selected, juce::dontSendNotification);
            b->onClick = [this, i] { if (onPick) onPick(i); };
        }
        resized();
    }
    void resized() override { auto r = getLocalBounds(); const int n = buttons.size(); if (!n) return; const int w = juce::jmin(90, r.getWidth() / n); for (auto* b : buttons) b->setBounds(r.removeFromLeft(w).reduced(2, 0)); }
    std::function<void(int)> onPick; juce::OwnedArray<juce::TextButton> buttons;
};

// The step strip is a piano roll: 32 equal columns across the full width; each ON step is a note block whose vertical
// position is its pitch (+24 at the top, -24 at the bottom). Click toggles a step, dragging up/down sets its pitch,
// right-click or shift-click only selects it for the editor below. Steps beyond the pattern length are dimmed.
struct StepGrid : juce::Component, private juce::Timer {
    static constexpr int kHeight = 150;
    StepGrid(Store& s, VVEngine* e) : store(s), engine(e) { startTimerHz(15); }
    void setPattern(const juce::String& pattern, int layerIdx) { pat = pattern; layer = layerIdx; resized(); repaint(); }
    int length() const { return juce::jlimit(1, 32, (int) std::lround(store.num(pat + ".length", 16))); }
    int heightNeeded(int) const { return kHeight; }
    float colW() const { return (float) getWidth() / 32.f; }
    juce::Rectangle<float> column(int i) const { return { colW() * (float) i, 0.f, colW(), (float) getHeight() }; }
    int columnAt(juce::Point<int> p) const { return juce::jlimit(0, 31, (int) ((float) p.x / colW())); }
    float blockY(int pitch) const { return 8.f + (1.f - (float) (pitch + 24) / 48.f) * ((float) getHeight() - 30.f); }
    int pitchAtY(float y) const { return juce::jlimit(-24, 24, (int) std::lround(24.0 - ((y - 7.f) / ((float) getHeight() - 14.f)) * 48.0)); }
    juce::String key(int i, const char* what) const { return pat + ".steps." + juce::String(i) + "." + what; }
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff0b0d0e)); g.fillRoundedRectangle(b, 10.f);
        g.saveState(); { juce::Path clip; clip.addRoundedRectangle(b, 10.f); g.reduceClipRegion(clip);
        g.setColour(juce::Colours::white.withAlpha(0.035f)); for (float y = 12.5f; y < b.getHeight(); y += 12.5f) g.drawHorizontalLine((int) y, 0.f, b.getWidth());
        const int len = length(), head = (engine && vv_engine_seq_running(engine)) ? vv_engine_seq_step(engine, layer) : -1;
        for (int i = 0; i < 32; ++i) {
            auto c = column(i); const bool on = i < len && store.flag(key(i, "on"));
            if (i % 4 == 0) { g.setGradientFill(juce::ColourGradient(col::cyan.withAlpha(0.07f), c.getX(), 0.f, juce::Colours::transparentBlack, c.getX(), b.getHeight(), false)); g.fillRect(c);
                g.setColour(col::cyan.withAlpha(0.28f)); g.drawVerticalLine((int) c.getX(), 0.f, b.getHeight()); }
            else { g.setColour(juce::Colours::white.withAlpha(0.04f)); g.drawVerticalLine((int) c.getX(), 0.f, b.getHeight()); }
            if (i >= len) { g.setColour(juce::Colours::black.withAlpha(0.55f)); g.fillRect(c); }
            if (i == head) { g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4aff8a).withAlpha(0.30f), c.getX(), 0.f, juce::Colour(0xff4aff8a).withAlpha(0.05f), c.getX(), b.getHeight(), false)); g.fillRect(c);
                g.setColour(juce::Colour(0xff4aff8a)); g.drawRect(c, 1.f); }
            if (i == selected && i < len) { g.setColour(juce::Colours::white); g.drawRect(c.reduced(1.f), 2.f); }
            g.setColour(col::dim.withAlpha(0.7f)); g.setFont(VVLookAndFeel::mono(11.f));
            if (i % 4 == 0) g.drawText(juce::String(i / 4 + 1), c.toNearestInt().removeFromTop(14), juce::Justification::centred);
            if (on) {
                const int p = (int) std::lround(store.num(key(i, "pitch"), 0)); auto blk = juce::Rectangle<float>(c.getX() + c.getWidth() * 0.12f, blockY(p), c.getWidth() * 0.76f, 14.f);
                g.setColour(col::cyan.withAlpha(0.25f)); g.fillRoundedRectangle(blk.expanded(3.f), 7.f);
                juce::ColourGradient grad(juce::Colour(0xff2fb9c4), blk.getX(), blk.getY(), juce::Colour(0xffc2408c), blk.getRight(), blk.getBottom(), false); grad.addColour(0.6, juce::Colour(0xff7e5cd6));
                g.setGradientFill(grad); g.fillRoundedRectangle(blk, 5.f);
                if (p != 0) { g.setColour(col::cyan); g.drawText((p > 0 ? "+" : "") + juce::String(p), c.toNearestInt().removeFromBottom(14), juce::Justification::centred); }
            }
        }
        } g.restoreState();
        g.setColour(juce::Colour(0xff222629)); g.drawRoundedRectangle(b.reduced(0.5f), 10.f, 1.f);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        down_ = columnAt(e.getPosition()); moved_ = false;
        if (down_ >= length()) { down_ = -1; return; }
        if (e.mods.isPopupMenu() || e.mods.isShiftDown()) { selected = down_; if (onSelect) onSelect(down_); down_ = -1; repaint(); return; }
        // long-press (550 ms) on a step opens its P-Locks / step editor, like the web app
        longFired_ = false; const int col = down_;
        juce::Component::SafePointer<juce::Component> sp(this);
        juce::Timer::callAfterDelay(550, [this, sp, col] { if (sp != nullptr && down_ == col && !moved_ && !longFired_) { longFired_ = true; selected = col; if (onSelect) onSelect(col); repaint(); } });
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (down_ < 0) return;
        if (!moved_ && std::abs(e.getDistanceFromDragStartY()) > 4) moved_ = true;
        if (moved_) { store.set(key(down_, "pitch"), (double) pitchAtY((float) e.y)); store.set(key(down_, "on"), true); selected = down_; if (onSelect) onSelect(down_); repaint(); }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (down_ >= 0 && !moved_ && !longFired_) {
            // tap anywhere in the column sets that pitch; tapping the pitch the note already has (+-1) switches it off
            const int np = pitchAtY((float) e.y), cur = (int) std::lround(store.num(key(down_, "pitch"), 0));
            if (store.flag(key(down_, "on")) && std::abs(np - cur) <= 1) store.set(key(down_, "on"), false);
            else { store.set(key(down_, "pitch"), (double) np); store.set(key(down_, "on"), true); }
            selected = down_; if (onSelect) onSelect(down_);
        }
        down_ = -1; repaint();
    }
    void timerCallback() override { if (isShowing()) { VV_UI_THROTTLE(15); repaint(); } }
    Store& store; VVEngine* engine; juce::String pat; int layer = 0, selected = 0; std::function<void(int)> onSelect;
private:
    int down_ = -1; bool moved_ = false, longFired_ = false;
};

// A p-lock: absent until switched on, then a value slider.
struct PlockControl : Control {
    PlockControl(Store& s, const juce::String& base, const juce::String& key, const juce::String& label, double lo, double hi, double def, bool log)
        : Control(s, base + "." + key), base_(base), key_(key), def_(def) {
        t.setButtonText("Lock " + label);
        t.onClick = [this] { if (updating_) return; if (t.getToggleState()) store.set(path_, def_); else removeKey(); };
        sl.setSliderStyle(juce::Slider::LinearHorizontal); sl.setRange(lo, hi, 0); if (log && lo > 0) sl.setSkewFactorFromMidPoint(std::sqrt(lo * hi));
        sl.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20); sl.setDoubleClickReturnValue(true, def);
        sl.onValueChange = [this] { if (!updating_) store.set(path_, sl.getValue()); };
        addAndMakeVisible(t); addAndMakeVisible(sl); refresh();
    }
    void removeKey() {   // rebuild the plocks object without this key
        auto cur = store.get(base_); auto* o = cur.getDynamicObject(); if (!o) return;
        auto* n = new juce::DynamicObject(); for (auto& kv : o->getProperties()) if (kv.name.toString() != key_) n->setProperty(kv.name, kv.value);
        store.set(base_, juce::var(n));
    }
    void refresh() override { updating_ = true; const bool has = store.has(path_); t.setToggleState(has, juce::dontSendNotification); sl.setEnabled(has); if (has) sl.setValue(store.num(path_, def_), juce::dontSendNotification); updating_ = false; }
    void resized() override { auto r = getLocalBounds(); t.setBounds(r.removeFromLeft(170)); sl.setBounds(r); }
    juce::Point<int> preferredSize() const override { return { -1, 34 }; }
    juce::String base_, key_; double def_; juce::ToggleButton t; juce::Slider sl;
};

struct TransportPage : Page {
    explicit TransportPage(Store& s) : Page(s) {
        auto& t = panel("Transport");
        run.setButtonText("Run sequencer"); run.onClick = [this] { store.setGlobal("transportRunning", run.getToggleState()); if (store.engine()) vv_engine_start_sequencer(store.engine(), run.getToggleState() ? 1 : 0); };
        t.custom(&run, 160, 34);
        t.knob("Tempo", "bpm", 40, 240, 120, { "%.0f", " BPM", false, 0 });
        t.toggle("Swing", "globalSwingOn"); t.knob("Swing amount", "globalSwingAmount", 50, 75, 50, { "%.0f", " %", false, 0 });
        auto& a = panel("Arpeggiator");
        auto push = [this](bool) { if (store.engine()) vv_engine_set_arp(store.engine(), store.flag("arp.on") ? 1 : 0, 0); };   // (the Latch switch is gone: Hold latches the arp per layer)
        a.toggle("Arp on", "arp.on").onChanged = push;
        arpBtn.onClick = [this] { showArpMenu(); }; a.custom(&arpBtn, 190, 30); refreshArpBtn();   // several layers can be ticked at once (web: state.arp.targets)
        a.choice("Mode", "arp.mode", { "standard", "advanced" }, "standard");   // advanced = the 35 generative algorithms below
        a.choice("Order", "arp.order", { "UP", "DOWN", "UP/DOWN", "DOWN/UP", "RANDOM", "RANDOM WALK", "PENDULUM", "SPIRAL", "STRUM", "OCTAVE ORBIT", "CHORD", "AS PLAYED" }, "UP");
        a.choice("Rate mode", "arp.rateMode", { "sync", "hz" }, "sync");
        a.choiceNum("Division", "arp.division", { { "1/4", 1 }, { "1/8", 0.5 }, { "1/16", 0.25 }, { "1/32", 0.125 } }, 0.5);
        a.knob("Rate (Hz mode)", "arp.hz", 0.5, 20, 4, { "%.1f", " Hz", false, 1 }); a.stepper("Octave range", "arp.octRange", 1, 4, 1); a.knob("Gate", "arp.gate", 0.05, 0.95, 0.8, { "%.2f", "", false, 2 }); a.knob("Swing", "arp.swing", 50, 75, 50, { "%.0f", " %", false, 0 });
        a.newRow(); a.customOwned(new ArpAdvPanel(store), -1, ArpAdvPanel::heightFor(12));
    }
    juce::ToggleButton run; juce::TextButton arpBtn;
    juce::StringArray arpTargets() const {   // ['ALL'] or a list of ids; older presets only carry the single arp.target
        juce::StringArray r; auto v = store.get("arp.targets");
        if (auto* arr = v.getArray()) for (auto& e : *arr) r.add(e.toString());
        if (r.isEmpty()) { auto t = store.get("arp.target").toString(); r.add(t.isEmpty() ? "ALL" : t); }
        return r;
    }
    void setArpTargets(juce::StringArray l) { if (l.isEmpty()) l.add("ALL"); juce::Array<juce::var> a; for (auto& s : l) a.add(s); store.set("arp.targets", a); store.set("arp.target", l[0]); refreshArpBtn(); }
    void refreshArpBtn() {
        auto t = arpTargets(); juce::String txt;
        if (t.contains("ALL")) txt = "All layers"; else if (t.size() > 2) txt = juce::String(t.size()) + " layers selected"; else for (auto& id : t) txt << (txt.isEmpty() ? "" : " + ") << kArpTargetLabels[kArpTargets.indexOf(id)];
        arpBtn.setButtonText("Arp layer: " + txt);
    }
    void showArpMenu() {   // tick-list; re-opens after each pick so several layers can be chosen in a row
        auto cur = arpTargets(); juce::PopupMenu m;
        for (int i = 0; i < kArpTargets.size(); ++i) m.addItem(i + 1, kArpTargetLabels[i], true, cur.contains(kArpTargets[i]));
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&arpBtn), [this, cur](int r) mutable {
            if (r <= 0) return; const juce::String id = kArpTargets[r - 1];
            if (id == "ALL") cur = juce::StringArray("ALL"); else { cur.removeString("ALL"); if (cur.contains(id)) cur.removeString(id); else cur.add(id); }
            setArpTargets(cur); showArpMenu(); });
    }
};

struct PatternPage : juce::Component, private Store::Listener {
    PatternPage(Store& s) : store(s), grid(s, s.engine()) {
        s.addListener(this);
        addAndMakeVisible(layerRow); addAndMakeVisible(slotRow); addAndMakeVisible(grid); addAndMakeVisible(chain);
        layerRow.set({ "WT", "FM", "SMP", "PRO" }, 0); layerRow.onPick = [this](int i) { layer = i; selected = 0; rebuild(); };
        grid.onSelect = [this](int i) { selected = i; buildEditor(); };
        lenLabel.setText("Length", juce::dontSendNotification); addAndMakeVisible(lenLabel);
        for (auto* b : { &copyB, &pasteB, &randB, &clearB }) addAndMakeVisible(*b);
        copyB.setButtonText("COPY PATTERN"); pasteB.setButtonText("PASTE PATTERN"); randB.setButtonText("RANDOMISE"); clearB.setButtonText("CLEAR STEPS");
        copyB.onClick = [this] { clip().steps = store.get(pat() + ".steps").clone(); clip().length = store.num(pat() + ".length", 16); clip().layer = layer; clip().has = true; };
        pasteB.onClick = [this] { if (!clip().has) return; store.set(pat() + ".steps", clip().steps.clone()); store.set(pat() + ".length", clip().length);
            if (clip().layer != layer) { const int n = store.count(pat() + ".steps"); for (int i = 0; i < n; ++i) store.set(pat() + ".steps." + juce::String(i) + ".plocks", juce::var(new juce::DynamicObject())); } };
        randB.onClick = [this] { randomise(); };
        clearB.onClick = [this] { const int n = juce::jmax(store.count(pat() + ".steps"), 32); juce::Array<juce::var> a; for (int i = 0; i < n; ++i) a.add(emptyStep()); store.set(pat() + ".steps", juce::var(a)); };
        rebuild();
    }
    ~PatternPage() override { store.removeListener(this); }
    struct Clip { juce::var steps; double length = 16; int layer = 0; bool has = false; };
    static Clip& clip() { static Clip c; return c; }
    static juce::var emptyStep() { auto* o = new juce::DynamicObject(); o->setProperty("on", false); o->setProperty("pitch", 0); o->setProperty("ratchet", 1); o->setProperty("prob", 100); o->setProperty("tempoRatio", 1); o->setProperty("plocks", juce::var(new juce::DynamicObject())); o->setProperty("transientIdx", 0); return juce::var(o); }
    // Web "RANDOMISE": 65 % on, mostly single hits with some 2-4 ratchets, prob 50-100, scale-aware pitch, random slice on the sampler, p-locks nudged +-30 %
    void randomise() {
        juce::Random rnd; const int n = store.count(pat() + ".steps");
        int nsc; const KbScale* tab = kbScales(nsc); const int sc = juce::jlimit(0, nsc - 1, (int) store.global("scaleIdx", 0)), root = (int) store.global("scaleRoot", 0);
        const int slices = layer == 2 ? store.count("layers.SMP.oscs.0.transients") : 0;
        std::vector<std::pair<juce::String, juce::var>> kv;
        for (int i = 0; i < n; ++i) {
            const juce::String sp = pat() + ".steps." + juce::String(i);
            kv.push_back({ sp + ".on", rnd.nextFloat() < 0.65f }); kv.push_back({ sp + ".ratchet", rnd.nextFloat() < 0.8f ? 1 : 1 + rnd.nextInt(3) });
            kv.push_back({ sp + ".prob", 50 + rnd.nextInt(51) });
            int pitch = rnd.nextInt(25) - 12;
            if (sc > 0) { const int* d = tab[sc].iv; const int cnt = tab[sc].n; const int spread[] = { -12, 0, 0, 0, 12 };
                pitch = ((d[rnd.nextInt(cnt)] - ((60 - root) % 12 + 12) % 12 + 24) % 12) - 12 + spread[rnd.nextInt(5)]; }
            kv.push_back({ sp + ".pitch", pitch });
            if (layer == 2) kv.push_back({ sp + ".transientIdx", slices > 0 ? rnd.nextInt(slices + 1) : 0 });
            for (const char* k : { "level", "cutoff", "position" }) { const juce::String pp = sp + ".plocks." + k; if (store.has(pp)) kv.push_back({ pp, store.num(pp) * (0.7 + rnd.nextDouble() * 0.6) }); }
        }
        store.setMany(kv);
    }
    juce::String L() const { return kLayers[layer]; }
    int slot() const { return (int) std::lround(store.num("layers." + L() + ".currentSlot", 0)); }
    juce::String pat() const { return "layers." + L() + ".patterns." + juce::String(slot()); }
    void storeChanged() override { if (store.count("layers." + L() + ".patterns") != nPat || slot() != lastSlot) rebuild(); else repaint(); }
    void rebuild() {
        nPat = store.count("layers." + L() + ".patterns"); lastSlot = slot();
        layerRow.set({ "WT", "FM", "SMP", "PRO" }, layer);
        juce::StringArray names; for (int i = 0; i < juce::jmax(1, nPat); ++i) names.add("Pattern " + juce::String(i + 1));
        slotRow.set(names, lastSlot); slotRow.onPick = [this](int i) { store.set("layers." + L() + ".currentSlot", i); };
        grid.setPattern(pat(), layer); grid.selected = juce::jmin(selected, grid.length() - 1);
        buildEditor(); resized();
    }
    void buildEditor() {
        editorPage.reset(); plocks.clear(); editorPage.reset(new Page(store)); addAndMakeVisible(*editorPage);
        const juce::String sp = pat() + ".steps." + juce::String(grid.selected);
        auto& o = editorPage->panel("Pattern");
        o.stepper("Length", pat() + ".length", 1, 32, 16);
        o.choice("Chain mode", "layers." + L() + ".chainMode", { "off", "loop", "oneshot" }, "off");
        o.knob("Gate", "layers." + L() + ".gate", 0.02, 1, 0.7, { "%.2f", "", false, 2 });
        o.choice("Sidechain source", "layers." + L() + ".scSrc", { "none", "WT", "FM", "SMP", "PRO" }, "none");
        o.knob("SC threshold", "layers." + L() + ".scThresh", 0, 1, 0.25, { "%.2f", "", false, 2 });
        o.knob("SC amount", "layers." + L() + ".scAmount", 0, 1, 0.8, { "%.2f", "", false, 2 });
        auto& e = editorPage->panel("Step " + juce::String(grid.selected + 1));
        e.toggle("On", sp + ".on"); e.stepper("Pitch (semitones)", sp + ".pitch", -48, 48, 0); e.stepper("Ratchets", sp + ".ratchet", 1, 8, 1);
        e.knob("Probability", sp + ".prob", 0, 100, 100, { "%.0f", " %", false, 0 }); e.knob("Tempo ratio", sp + ".tempoRatio", 0.25, 4, 1, { "%.2f", "", true, 2 });
        auto& p = editorPage->panel("Parameter locks (this step only)");
        p.custom(plocks.add(new PlockControl(store, sp + ".plocks", "level", "level", 0, 1, 1, false)), -1, 34); p.newRow();
        p.custom(plocks.add(new PlockControl(store, sp + ".plocks", "cutoff", "cutoff", 20, 18000, 4000, true)), -1, 34); p.newRow();
        if (layer == 0) { p.custom(plocks.add(new PlockControl(store, sp + ".plocks", "position", "wavetable position", 0, 1, 0, false)), -1, 34); p.newRow(); }
        if (layer == 2) p.stepper("Transient slice", sp + ".transientIdx", 0, 16, 0);
        resized(); buildChain();
    }
    void buildChain() {
        chain.removeAllChildren(); chainButtons.clear(); const juce::String cp = "layers." + L() + ".chainOrder";
        int x = 0; auto* lab = chainButtons.add(new juce::TextButton("Chain order:")); lab->setEnabled(false); chain.addAndMakeVisible(lab); lab->setBounds(x, 2, 110, 28); x += 116;
        for (int i = 0; i < store.count(cp); ++i) {
            auto* b = chainButtons.add(new juce::TextButton(juce::String((int) std::lround((double) store.get(cp + "." + juce::String(i))) + 1)));
            b->onClick = [this, cp, i] { juce::MessageManager::callAsync([this, cp, i] { store.remove(cp, i); }); }; chain.addAndMakeVisible(b); b->setBounds(x, 2, 36, 28); x += 40;
        }
        auto* add = chainButtons.add(new juce::TextButton("+")); chain.addAndMakeVisible(add); add->setBounds(x, 2, 36, 28); x += 40;
        add->onClick = [this, cp] { juce::PopupMenu m; for (int n = 0; n < juce::jmax(1, nPat); ++n) m.addItem(n + 1, "Add pattern " + juce::String(n + 1));
            m.showMenuAsync({}, [this, cp](int r) { if (r > 0) store.append(cp, r - 1); }); };
        auto* clr = chainButtons.add(new juce::TextButton("Clear")); chain.addAndMakeVisible(clr); clr->setBounds(x, 2, 60, 28);
        clr->onClick = [this, cp] { juce::MessageManager::callAsync([this, cp] { store.set(cp, juce::var(juce::Array<juce::var>())); }); };
    }
    void resized() override {
        auto r = getLocalBounds().reduced(6);
        layerRow.setBounds(r.removeFromTop(32)); slotRow.setBounds(r.removeFromTop(32)); r.removeFromTop(4);
        { auto row = r.removeFromTop(28); for (auto* b : { &copyB, &pasteB, &randB, &clearB }) { b->setBounds(row.removeFromLeft(130).reduced(2, 0)); } r.removeFromTop(4); }
        grid.setBounds(r.removeFromTop(grid.heightNeeded(r.getWidth())));
        chain.setBounds(r.removeFromTop(34)); r.removeFromTop(4);
        if (editorPage) editorPage->setBounds(r);
    }
    Store& store; int layer = 0, selected = 0, nPat = -1, lastSlot = -1;
    ButtonRow layerRow, slotRow; juce::TextButton copyB, pasteB, randB, clearB; StepGrid grid; juce::Component chain; juce::Label lenLabel;
    std::unique_ptr<Page> editorPage; juce::OwnedArray<PlockControl> plocks; juce::OwnedArray<juce::TextButton> chainButtons;
};

// ---- Chord maker page: generative chord progressions (see Core/include/vv/chordmaker.hpp)
// 16-lane drum sequencer (web #drumSeqPanel). Lane i always one-shots Sampler slot i (the MPC pad slots), straight sixteenths, per-lane length 1..32.
// Stored as drumSeq.on and drumSeq.lanes.<i>.{length, steps.<k>}; plays with the transport (engine: Performance::drumStep).
struct DrumInner : juce::Component, private juce::Timer {
    explicit DrumInner(Store& s) : store(s) {
        on.setButtonText("On (plays alongside the per-layer patterns)"); on.onClick = [this] { store.set("drumSeq.on", on.getToggleState() ? 1.0 : 0.0); }; addAndMakeVisible(on);
        info.setText("Each lane plays one of the Sampler's 16 slots (the MPC pad slots). Click cells to toggle steps; the bar on the right is the lane length.", juce::dontSendNotification); info.setColour(juce::Label::textColourId, juce::Colours::grey); addAndMakeVisible(info);
        for (int i = 0; i < 16; ++i) { auto* l = len.add(new juce::Slider(juce::Slider::LinearBar, juce::Slider::TextBoxLeft)); l->setRange(1, 32, 1); l->setValue(16, juce::dontSendNotification); l->setTextValueSuffix(" steps");
            l->onValueChange = [this, i] { store.set("drumSeq.lanes." + juce::String(i) + ".length", len[i]->getValue()); repaint(); }; addAndMakeVisible(l); }
        refresh(); startTimerHz(6);
    }
    int laneLen(int i) const { return juce::jlimit(1, 32, (int) std::lround(store.num("drumSeq.lanes." + juce::String(i) + ".length", 16))); }
    bool cell(int i, int k) const { return store.num("drumSeq.lanes." + juce::String(i) + ".steps." + juce::String(k), 0) != 0; }
    juce::Rectangle<int> grid() const { auto r = getLocalBounds().withTrimmedTop(54).reduced(8, 0); r.removeFromLeft(56); r.removeFromRight(150); return r; }
    void refresh() {
        on.setToggleState(store.num("drumSeq.on", 0) != 0, juce::dontSendNotification);
        for (int i = 0; i < 16; ++i) if (!len[i]->isMouseButtonDown()) len[i]->setValue(laneLen(i), juce::dontSendNotification);
        repaint();
    }
    void timerCallback() override { if (isShowing()) refresh(); }
    void resized() override {
        auto r = getLocalBounds(); auto top = r.removeFromTop(54); on.setBounds(top.removeFromTop(26).reduced(8, 2)); info.setBounds(top.reduced(8, 0));
        auto g = grid(); const int rh = g.getHeight() / 16;
        for (int i = 0; i < 16; ++i) len[i]->setBounds(g.getRight() + 6, g.getY() + i * rh + 2, 138, rh - 4);
    }
    void paint(juce::Graphics& g) override {
        auto gr = grid(); const int rh = gr.getHeight() / 16; const float cw = (float) gr.getWidth() / 32.f;
        g.setFont(11.f);
        for (int i = 0; i < 16; ++i) {
            const int y = gr.getY() + i * rh; g.setColour(juce::Colours::grey); g.drawText("LANE " + juce::String(i + 1), 8, y, 54, rh, juce::Justification::centredLeft);
            for (int k = 0; k < 32; ++k) {
                auto c = juce::Rectangle<float>(gr.getX() + k * cw, (float) y, cw, (float) rh).reduced(1.5f); const bool act = k < laneLen(i);
                g.setColour(!act ? juce::Colour(0xff15181a) : cell(i, k) ? juce::Colour(0xff2fd4e0) : (k % 4 == 0 ? juce::Colour(0xff2a3236) : juce::Colour(0xff20262a))); g.fillRoundedRectangle(c, 3.f);
            }
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        auto gr = grid(); if (!gr.contains(e.getPosition())) return; const int rh = gr.getHeight() / 16; const float cw = (float) gr.getWidth() / 32.f;
        const int i = (e.y - gr.getY()) / rh, k = (int) ((e.x - gr.getX()) / cw); if (i < 0 || i > 15 || k < 0 || k >= laneLen(i)) return;
        store.set("drumSeq.lanes." + juce::String(i) + ".steps." + juce::String(k), cell(i, k) ? 0.0 : 1.0); repaint();
    }
    Store& store; juce::ToggleButton on; juce::Label info; juce::OwnedArray<juce::Slider> len;
};

struct DrumPage : juce::Viewport {   // scrolls so lanes keep a usable row height in a short window
    explicit DrumPage(Store& s) : inner(new DrumInner(s)) { setViewedComponent(inner, true); setScrollBarsShown(true, false); }
    void resized() override { Viewport::resized(); inner->setSize(getMaximumVisibleWidth(), juce::jmax(getHeight(), 54 + 16 * 22 + 8)); }
    DrumInner* inner;
};

struct ChordStrip : juce::Component, private juce::Timer {
    explicit ChordStrip(Store& s) : store(s) { startTimerHz(10); refreshInfo(); }
    void refreshInfo() {
        if (!store.engine()) return; const size_t n = vv_engine_chord_info(store.engine(), nullptr, 0); std::vector<char> b(n + 1, 0); vv_engine_chord_info(store.engine(), b.data(), b.size());
        auto parsed = juce::JSON::parse(juce::String::fromUTF8(b.data())); cards.clear();
        if (auto* a = parsed.getArray()) for (auto& v : *a) cards.push_back({ v.getProperty("name", "").toString(), (int) v.getProperty("deg", 0), (int) v.getProperty("n", 0), (bool) v.getProperty("rest", false), (bool) v.getProperty("locked", false) });
        repaint();
    }
    void timerCallback() override { const int p = store.engine() ? vv_engine_chord_playing(store.engine()) : -1; if (p != playing) { playing = p; repaint(); } }
    int heightNeeded(int w) const { const int per = juce::jmax(1, w / 110); return 8 + 64 * (((int) cards.size() + per - 1) / per == 0 ? 1 : ((int) cards.size() + per - 1) / per); }
    juce::Rectangle<int> cardRect(int i) const { const int per = juce::jmax(1, getWidth() / 110); return { 4 + (i % per) * 110, 4 + (i / per) * 64, 104, 58 }; }
    void paint(juce::Graphics& g) override {
        for (int i = 0; i < (int) cards.size(); ++i) {
            auto r = cardRect(i).toFloat(); const auto& c = cards[(size_t) i];
            g.setColour(col::panel2.withAlpha(c.rest ? 0.4f : 1.f)); g.fillRoundedRectangle(r, 6.f);
            g.setColour(i == playing ? col::cyan : (c.locked ? col::amber : col::line)); g.drawRoundedRectangle(r, 6.f, i == playing ? 2.f : 1.f);
            g.setColour(col::cyan); g.setFont(juce::Font(juce::FontOptions(16.f, juce::Font::bold))); g.drawText(c.rest ? "rest" : c.name, r.reduced(8, 4).removeFromTop(22), juce::Justification::centredLeft);
            g.setColour(col::dim); g.setFont(juce::Font(juce::FontOptions(10.f)));
            g.drawText(c.rest ? juce::String() : "deg " + juce::String(c.deg) + " | " + juce::String(c.n) + " notes", r.reduced(8, 0).withTrimmedTop(26).removeFromTop(14), juce::Justification::centredLeft);
            g.setColour(c.locked ? col::amber : col::dim); g.drawText(c.locked ? "LOCKED" : "tap: lock | 2-finger: re-roll", r.reduced(8, 0).withTrimmedTop(40).removeFromTop(14), juce::Justification::centredLeft);
        }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!store.engine()) return;
        for (int i = 0; i < (int) cards.size(); ++i) if (cardRect(i).contains(e.getPosition())) {
            vv_engine_chord_action(store.engine(), e.mods.isPopupMenu() ? "reroll" : "lock", i, 0); store.reload(); refreshInfo(); return; }
    }
    struct Card { juce::String name; int deg, n; bool rest, locked; };
    Store& store; std::vector<Card> cards; int playing = -1;
};

struct ChordPage : juce::Component, private Store::Listener {
    explicit ChordPage(Store& s) : store(s), page(s), strip(s) {
        s.addListener(this);
        auto& m = page.panel("Chord maker (generative)");
        m.toggle("On (plays with the sequencer run switch)", "chordMaker.on");
        auto& h = page.panel("Harmony");
        juce::StringArray ids, labels; for (int i = 0; i < cm::kNumScales; ++i) { ids.add(cm::kScales[i].id); labels.add(juce::String(cm::kScales[i].group) + ": " + cm::kScales[i].label); }
        juce::StringArray keys; for (int i = 0; i < 12; ++i) keys.add(cm::kNames[i]);
        std::vector<std::pair<juce::String, double>> ks; for (int i = 0; i < 12; ++i) ks.push_back({ cm::kNames[i], (double) i });
        h.choiceNum("Key", "chordMaker.key", ks, 0);
        h.choice("Mode / scale", "chordMaker.scale", ids, "major").setLabels(labels);
        h.choice("Voicing", "chordMaker.voicing", { "close", "open", "drop2" }, "close").setLabels({ "Close", "Open (spread)", "Drop 2" });
        h.choice("Play on", "chordMaker.target", { "ALL", "WT", "FM", "SMP", "PRO" }, "ALL");
        h.newRow();
        h.stepper("Chords (steps)", "chordMaker.steps", 1, 16, 8); h.stepper("Octave", "chordMaker.octave", 1, 6, 3);
        h.choiceNum("Rate", "chordMaker.rate", { { "/4", 0.25 }, { "/3", 1.0 / 3 }, { "/2", 0.5 }, { "/1.5", 2.0 / 3 }, { "x1", 1 }, { "x1.5", 1.5 }, { "x2", 2 }, { "x3", 3 }, { "x4", 4 } }, 1);
        auto& f = page.panel("Feel");
        f.knob("Naturalness", "chordMaker.naturalness", 0, 1, 0.7); f.knob("Density", "chordMaker.density", 0, 1, 0.4); f.knob("Sparseness", "chordMaker.sparseness", 0, 1, 0.15); f.knob("Randomness", "chordMaker.randomness", 0, 1, 0.1);
        f.knob("Probability", "chordMaker.prob", 0, 100, 100, { "%.0f", " %", false, 0 }); f.knob("Gate", "chordMaker.gate", 0.05, 1, 0.9);
        f.knob("Strum", "chordMaker.strum", 0, 120, 0, { "%.0f", " ms", false, 0 }); f.knob("Velocity", "chordMaker.vel", 0.1, 1, 0.8);
        auto& e = page.panel("Euclidean");
        e.toggle("Euclidean (one chord per pulse)", "chordMaker.euclid.on"); e.stepper("Pulses", "chordMaker.euclid.pulses", 1, 32, 5); e.stepper("Steps", "chordMaker.euclid.steps", 1, 32, 8); e.stepper("Rotate", "chordMaker.euclid.rotate", 0, 31, 0);
        addAndMakeVisible(page); addAndMakeVisible(strip);
        const char* names[] = { "GENERATE", "MUTATE", "RE-VOICE", "< ROTATE", "ROTATE >", "REVERSE" }; const char* acts[] = { "generate", "mutate", "revoice", "rotl", "rotr", "reverse" };
        for (int i = 0; i < 6; ++i) { auto* b = buttons.add(new juce::TextButton(names[i])); addAndMakeVisible(b); const juce::String a(acts[i]); b->onClick = [this, a] { act(a.toRawUTF8()); }; }
        lastScale = store.str("chordMaker.scale"); lastCount = count();
        if (store.engine() && !store.has("chordMaker.progression")) act("generate");
    }
    ~ChordPage() override { store.removeListener(this); }
    int count() const { return store.flag("chordMaker.euclid.on") ? juce::jmax(1, juce::jmin((int) store.num("chordMaker.euclid.pulses", 5), (int) store.num("chordMaker.euclid.steps", 8))) : (int) store.num("chordMaker.steps", 8); }
    void act(const char* a) { if (!store.engine()) return; vv_engine_chord_action(store.engine(), a, 0, 0); store.reload(); strip.refreshInfo(); resized(); }
    void storeChanged() override {
        if (busy) return; busy = true;
        const juce::String sc = store.str("chordMaker.scale"); const int n = count();
        if (store.engine() && store.has("chordMaker.progression")) {
            if (sc != lastScale) { lastScale = sc; act("revoice"); }
            else if (n != lastCount) { act("fit"); }
        }
        lastCount = n; strip.refreshInfo(); busy = false;
    }
    void resized() override {
        auto r = getLocalBounds().reduced(4); auto bar = r.removeFromTop(34);
        for (auto* b : buttons) b->setBounds(bar.removeFromLeft(112).reduced(2));
        strip.setBounds(r.removeFromTop(juce::jmin(r.getHeight() / 3, strip.heightNeeded(r.getWidth())))); page.setBounds(r);
    }
    Store& store; Page page; ChordStrip strip; juce::OwnedArray<juce::TextButton> buttons; juce::String lastScale; int lastCount = 0; bool busy = false;
};

} // namespace

SeqScreen::SeqScreen(Store& s) {
    pages_.add(new TransportPage(s)); pages_.add(new PatternPage(s)); pages_.add(new ChordPage(s)); pages_.add(new DrumPage(s));
    pages_.add(makeRollPage(s, s.engine()));   // full piano roll: replaces the step grid as the way to write notes (old step patterns still play and convert on first roll edit)
    pages_.add(new ClipLauncher(s));
    tabs.add("Transport / Arp", pages_[0]); tabs.add("Piano Roll", pages_[4]); tabs.add("Patterns", pages_[1]); tabs.add("Chords", pages_[2]); tabs.add("Drums", pages_[3]); tabs.add("Clips", pages_[5]);
    addAndMakeVisible(tabs);
}

} // namespace vv
