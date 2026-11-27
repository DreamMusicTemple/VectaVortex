// SOUND screen: Wavetable / FM / Sampler / Procedural oscillators, per-layer filter, envelopes and EQ.
#include "LuaEditor.h"
#include "NavIcons.h"
#include "Screens.h"
#include "ScreensAuto.h"
#include "AuthoringTools.h"
#include "SampleEditor.h"
#include "EqGraph.h"
#include "ArpSteps.h"
#include "Assets.h"
#include <vv/flute.hpp>
#include <cstring>

namespace vv {
/// Link badge shown right of OSC1 in the Wavetable page: hard-sync icon while layers.WT.syncEnabled, ring-mod icon while ringModEnabled (sync wins, like the web app). Untested (no JUCE build here).
class WtLinkBadge : public juce::Component, public juce::SettableTooltipClient, private Store::Listener {
public:
    explicit WtLinkBadge(Store& s) : store(s) { store.addListener(this); refresh(); }
    ~WtLinkBadge() override { store.removeListener(this); }
    void paint(juce::Graphics& g) override {
        if (mode == 0) return;
        const auto c = mode == 1 ? juce::Colour(0xff4aa8ff) : juce::Colour(0xffe070ff);
        navicon::draw(g, mode == 1 ? navicon::Sync : navicon::RingMod, getLocalBounds().toFloat().reduced(4.f), c);
    }
private:
    void refresh() { const int m = store.flag("layers.WT.syncEnabled") ? 1 : store.flag("layers.WT.ringModEnabled") ? 2 : 0; if (m != mode) { mode = m; setTooltip(m == 1 ? "Hard sync: OSC1 (master) resets OSC2 (slave)" : m == 2 ? "Ring mod: OSC1 x OSC2" : juce::String()); repaint(); } }
    void storeChanged() override { refresh(); }
    Store& store; int mode = 0;
};
// "NEW NOTE NEXT SAMPLE" (web layer.slotAdvance): one tap = on/off; a quick double tap swaps Next <-> Random and leaves the on/off state as it was.
// Stored as layers.SMP.slotAdvance = "off" | "next" | "random"; the Core plays one playable slot per new note. Untested (no JUCE build here).
class SlotAdvanceButton : public juce::Component, private Store::Listener {
public:
    explicit SlotAdvanceButton(Store& s) : store(s) {
        const auto v = store.str("layers.SMP.slotAdvance", "off"); flavour = v == "random" ? "random" : "next"; addAndMakeVisible(b); b.setClickingTogglesState(false);
        b.onClick = [this] {
            const auto now = juce::Time::getMillisecondCounterHiRes(); const bool on = isOn();
            if (now - last < 380 && before >= 0) { flavour = flavour == "random" ? "next" : "random"; store.set("layers.SMP.slotAdvance", before ? flavour : juce::String("off")); last = 0; before = -1; }
            else { before = on ? 1 : 0; last = now; store.set("layers.SMP.slotAdvance", on ? juce::String("off") : flavour); }
            refresh(); };
        store.addListener(this); refresh();
    }
    ~SlotAdvanceButton() override { store.removeListener(this); }
    void resized() override { b.setBounds(getLocalBounds().reduced(2)); }
private:
    bool isOn() const { const auto v = store.str("layers.SMP.slotAdvance", "off"); return v == "next" || v == "random"; }
    void refresh() { const auto v = store.str("layers.SMP.slotAdvance", "off"); if (v == "next" || v == "random") flavour = v; b.setButtonText(juce::String("NEW NOTE ") + (flavour == "random" ? "RANDOM" : "NEXT") + " SAMPLE: " + (isOn() ? "ON" : "OFF")); b.setColour(juce::TextButton::buttonColourId, isOn() ? juce::Colour(0xff1d6b3c) : juce::Colour(0xff2a2c30)); }
    void storeChanged() override { refresh(); }
    Store& store; juce::TextButton b; juce::String flavour = "next"; double last = 0; int before = -1;
};


void SubTabs::add(const juce::String& name, juce::Component* page) {
    auto* b = buttons_.add(new juce::TextButton(name)); b->setClickingTogglesState(false); b->setRadioGroupId(7001 + (int) reinterpret_cast<uintptr_t>(this) % 1000);
    const int idx = buttons_.size() - 1; b->onClick = [this, idx] { select(idx); };
    addAndMakeVisible(b); pages_.add(page); addChildComponent(page); if (sel_ < 0) select(0);
}
void SubTabs::select(int i) {
    sel_ = i; for (int k = 0; k < pages_.size(); ++k) { pages_[k]->setVisible(k == i); buttons_[k]->setToggleState(k == i, juce::dontSendNotification); }
    resized(); if (onChange) onChange(i);
}
void SubTabs::resized() {
    auto r = getLocalBounds(); auto bar = r.removeFromTop(38).reduced(6, 4);
    const int n = buttons_.size(); if (n == 0) return; const int w = juce::jmin(150, bar.getWidth() / n);
    for (auto* b : buttons_) b->setBounds(bar.removeFromLeft(w).reduced(2, 0));
    for (auto* p : pages_) p->setBounds(r);
}

namespace {

const juce::StringArray kMicIds{ "none", "sm57", "u87", "r121", "u47", "c414", "sm7", "coles", "phone", "walkie", "piezo", "tincan", "lav", "boundary", "hydro", "babymon", "carbon" };
const juce::StringArray kMicLabels{ "None (direct)", "Classic: Dynamic (SM57-style)", "Classic: Condenser (U87-style)", "Classic: Ribbon (R-121-style)",
    "Classic: Tube large-diaphragm (U47-style)", "Classic: Multi-pattern condenser (C414-style)", "Classic: Broadcast dynamic (SM7-style)", "Classic: Ribbon (Coles-style)",
    "Unusual: Old telephone", "Unusual: Walkie-talkie", "Unusual: Contact piezo pickup", "Unusual: Tin can",
    "Unusual: Lavalier clip mic", "Unusual: Boundary (PZM) room mic", "Unusual: Hydrophone (underwater)", "Unusual: Baby monitor",
    "Unusual: Carbon button mic" };
const juce::StringArray kSpkIds{ "none", "studio", "cab412", "hifi", "tuberadio", "nearfield", "cab112", "sub18", "jukebox", "oldtv", "pocket", "laptop", "mega", "intercom", "handheld", "btmicro", "bucket" };
const juce::StringArray kSpkLabels{ "None (direct)", "Classic: Studio monitor", "Classic: Guitar cab 4x12", "Classic: Hi-fi floorstander",
    "Classic: 1950s tube radio", "Classic: Nearfield 2-way monitor", "Classic: Guitar combo 1x12", "Classic: 18-inch subwoofer",
    "Classic: Jukebox cabinet", "Unusual: Old TV", "Unusual: Pocket transistor radio", "Unusual: Laptop speaker",
    "Unusual: Megaphone", "Unusual: Door intercom", "Unusual: Handheld game console", "Unusual: Micro Bluetooth speaker",
    "Unusual: Speaker in a bucket" };

void layerPanels(Page& p, const juce::String& L) {
    const juce::String pre = "layers." + L;
    auto& env = p.panel(L + " amp envelope (layer default)"); addEnv(env, pre + ".env");
    auto& fl = p.panel(L + " layer filter"); addFilter(fl, pre + ".filter", pre + ".filterEnv");
    auto& eq = p.panel(L + " EQ (4 band)");
    eq.customOwned(new EqGraph(p.store, pre), -1, 214);   // draggable bands (LOW / BELL / BELL / HIGH), as in Vektur Vocal
}

struct ActionButton : juce::TextButton { ActionButton(const juce::String& t, std::function<void(juce::Component&)> f) { setButtonText(t); onClick = [this, f] { f(*this); }; } };

struct WtPage : Page {
    explicit WtPage(Store& s) : Page(s) { build(); }
    void rebuildSoon() { juce::MessageManager::callAsync([alive = juce::Component::SafePointer<WtPage>(this)] { if (alive) alive->build(); }); }   // table list changed: rebuild after the click handler returns
    void build() {
        clear();
        auto& e = panel("Wavetable engine"); e.toggle("Oscillator sync (OSC1 <- OSC2)", "layers.WT.syncEnabled");
        {   // OSC1 x OSC2 ring mod (OSC2 becomes the modulator); ignored while sync is on
            auto& r = panel("Ring mod (OSC1 x OSC2)"); r.setKnobScale(0.9f);
            r.toggle("Ring mod on (OSC2 = modulator)", "layers.WT.ringModEnabled");
            r.choice("Interval", "layers.WT.ringMod.interval", { "p5", "octave", "m3", "tritone", "micro" }, "p5"); r.choice("Osc wave", "layers.WT.ringMod.wave", { "sine", "triangle", "sawtooth", "square" }, "sine");
            r.newRow();
            r.knob("Detune (ct)", "layers.WT.ringMod.detuneCents", -100, 100, 0, { "%.0f", " ct", false, 0 }); r.knob("Pitch env (ct)", "layers.WT.ringMod.pitchEnvAmt", 0, 1200, 0, { "%.0f", " ct", false, 0 }); r.knob("Pitch env decay", "layers.WT.ringMod.pitchEnvDecay", 0.01, 1, 0.12);
            r.knob("AM bleed", "layers.WT.ringMod.bleed", 0, 1, 0.15); r.knob("Velocity depth", "layers.WT.ringMod.velDepth", 0, 1, 0.3); r.knob("LPG cutoff", "layers.WT.ringMod.lpgCutoff", 200, 8000, 4000, { "%.0f", " Hz", true, 0 }); r.knob("LPG keytrack", "layers.WT.ringMod.lpgKeytrack", 0, 1, 0.5);
        }
        for (int i = 0; i < 2; ++i) {
            const juce::String p = "layers.WT.oscs." + juce::String(i);
            auto& o = panel("Oscillator " + juce::String(i + 1)); o.setKnobScale(0.9f); if (i == 0) o.customOwned(new WtLinkBadge(store), 34, 34);
            o.toggle("Enabled", p + ".enabled", i == 0);
            { const auto tl = authoring::tableChoices(store); o.choice("Wavetable", p + ".table", tl.ids, "analogMorph").setLabels(tl.labels); }
            o.newRow();
            o.customOwned(new ActionButton("DRAW WAVEFORM", [this, p](juce::Component& a) { authoring::openDraw(store, p, a, [this] { rebuildSoon(); }); }), 130, 34);
            o.customOwned(new ActionButton("EDIT HARMONICS", [this, p](juce::Component& a) { authoring::openHarmonics(store, p, a, [this] { rebuildSoon(); }); }), 130, 34);
            o.customOwned(new ActionButton("FORMULA...", [this, p](juce::Component& a) { authoring::openFormula(store, p, a, [this] { rebuildSoon(); }); }), 110, 34);
            o.customOwned(new ActionButton("IMPORT AUDIO...", [this, p](juce::Component&) { authoring::importWavetableAudio(store, p, [this] { rebuildSoon(); }); }), 130, 34);
            o.newRow();
            const juce::String mk = "WT:osc" + juce::String(i + 1);   // long-press a dial to route a modulation source to it (same keys as the web's modKeyFor)
            o.knob("Position", p + ".position", 0, 1, 0).setModDest(mk + "Scan"); o.knob("Level", p + ".level", 0, 1, 0.8).setModDest(mk + "Level");
            o.knob("Pulse width", p + ".pwm", 0, 1, 0.5).setModDest(mk + "Pwm"); o.knob("Hz", p + ".pwmRate", 0, 20, 0, { "%.2f", " Hz", false, 2 }).setModDest(mk + "PwmRate");   // PWM speed
            o.knob("Phase", p + ".phase", 0, 1, 0).setModDest(mk + "Phase"); o.knob("Start/end", p + ".startEnd", 0, 1, 1).setModDest(mk + "StartEnd"); o.knob("Segment", p + ".segment", 0.05, 1, 1).setModDest(mk + "Segment");
            o.knob("Unison detune", p + ".detune", 0, 100, 0, { "%.0f", " ct", false, 0 }); o.knob("Stereo width", p + ".width", 0, 1, 0);
            o.knob("Spread blend", p + ".unisonBlend", 0, 1, 0); o.knob("Spread phase", p + ".unisonPhase", 0, 1, 0);   // centre-weighted voice levels / evenly spread start phases
            o.newRow();
            o.stepper("Unison voices", p + ".unison", 1, 16, 1); o.stepper("Sub (oct down)", p + ".sub", 0, 2, 0); o.choice("Sub shape", p + ".subWave", { "sine", "triangle", "square" }, "sine");
            o.choice("Position auto-scan", p + ".positionLfoSource", { "off", "ampEnv", "filterEnv", "pitchEnv", "lfo1", "lfo2", "lfo3", "lfo4", "lfo5", "lfo6", "velocity", "note", "modwheel", "random", "envWT", "envFM", "envSMP", "envFollow", "macro1", "macro2", "macro3", "macro4", "mpeTimbre", "mpePressure", "wtOsc1Pos", "wtOsc2Pos", "wtOsc1Shape", "wtOsc2Shape", "xy1x", "xy1y", "xy2x", "xy2y", "xy3x", "xy3y", "xy4x", "xy4y", "lorenz", "rossler" }, "off");   // Amp / Filter / Pitch envelope or any Matrix source drives Position 0..1
            o.toggle("Ping-pong scan", p + ".pingPong"); o.toggle("Noise", p + ".noiseOn"); o.choice("Noise type", p + ".noiseType", { "white", "pink", "brown", "blue", "violet" }, "white");
            auto& c = panel("Oscillator " + juce::String(i + 1) + " performance"); c.setKnobScale(0.9f); addOscCommon(c, p);
            auto& f = panel("Oscillator " + juce::String(i + 1) + " filter"); addFilter(f, p + ".filter", p + ".filterEnv");
        }
        layerPanels(*this, "WT");
        layoutContent();
    }
};

struct FmPage : Page {
    explicit FmPage(Store& s) : Page(s) {
        {   // 6-operator DX7-compatible voice: replaces the two 4-operator engines while on (patch bank = the 4 ROM cartridges x 2)
            auto& d = panel("6-operator (DX7) mode"); d.setKnobScale(0.9f);
            d.toggle("6-op mode", "layers.FM.sixOp"); d.toggle("6-op enabled", "layers.FM.sixOpEnabled");
            d.choice("Bank", "layers.FM.dx7Bank", { "ROM1A", "ROM1B", "ROM2A", "ROM2B", "ROM3A", "ROM3B", "ROM4A", "ROM4B" }, "ROM1A"); d.stepper("Patch", "layers.FM.dx7PatchIdx", 0, 31, 0);
            d.newRow(); d.knob("Transpose", "layers.FM.dx7Transpose", -24, 24, 0, { "%.0f", " st", false, 0 }); d.knob("Output trim", "layers.FM.dx7OutputTrim", 0, 2, 1); d.knob("Pan", "layers.FM.dx7Pan", -1, 1, 0);
        }
        const juce::StringArray waves{ "sine", "triangle", "square" };
        for (int e = 0; e < 2; ++e) {
            const juce::String E = juce::String(e);
            auto& p = panel(juce::String("FM engine ") + (e == 0 ? "A" : "B")); p.setKnobScale(0.9f);
            p.toggle("Enabled", "layers.FM.engineEnabled." + E); p.stepper("Algorithm", "layers.FM.fmAlgorithms." + E, 0, 5, 2);
            p.stepper("Octave", "layers.FM.engineOctave." + E, -4, 4, 0); p.stepper("Semitone", "layers.FM.engineSemitone." + E, -12, 12, 0);
            p.stepper("Sub (oct down)", "layers.FM.fmSub." + E, 0, 2, 0);
            p.knob("Fine", "layers.FM.engineFine." + E, -100, 100, 0, { "%.0f", " ct", false, 0 }); p.knob("Glide", "layers.FM.engineGlide." + E, 0, 3, 0, { "%.2f", " s", false, 2 });
            for (int k = 0; k < 4; ++k) {
                const juce::String o = "layers.FM.oscs." + juce::String(e * 4 + k);
                auto& op = panel(juce::String("Engine ") + (e == 0 ? "A" : "B") + " - operator " + juce::String(k + 1)); op.setKnobScale(0.9f);
                op.knob("Ratio", o + ".ratio", 0.05, 16, 1, { "%.2f", "", true, 2 }); op.knob("Level", o + ".level", 0, 1, k == 0 ? 0.9 : 0);
                op.choice("Wave", o + ".wave", waves, "sine");
                if (k == 0) {
                    op.knob("Feedback", o + ".feedback", 0, 1, 0); op.knob("Pan", o + ".pan", -1, 1, 0); op.toggle("Noise", o + ".noiseOn"); op.choice("Noise type", o + ".noiseType", { "white", "pink", "brown", "blue", "violet" }, "white");
                    auto& c = panel(juce::String("Engine ") + (e == 0 ? "A" : "B") + " performance"); c.setKnobScale(0.9f); addOscCommon(c, o, false);
                    auto& f = panel(juce::String("Engine ") + (e == 0 ? "A" : "B") + " filter"); addFilter(f, o + ".filter", o + ".filterEnv");
                }
            }
        }
        layerPanels(*this, "FM");
    }
};

struct SmpPage : Page {
    explicit SmpPage(Store& s) : Page(s) { slotBox.addItem("", 1); for (int i = 0; i < 16; ++i) slotBox.addItem("Slot " + juce::String(i + 1), i + 1);
        slotBox.setSelectedId(1, juce::dontSendNotification); slotBox.onChange = [this] { slot = slotBox.getSelectedId() - 1; build(); }; addAndMakeVisible(slotBox);
        toolsBtn.setButtonText("SAMPLE TOOLS..."); toolsBtn.onClick = [this] { authoring::openSampleTools(store, slot, toolsBtn, [this] { juce::MessageManager::callAsync([alive = juce::Component::SafePointer<SmpPage>(this)] { if (alive) alive->build(); }); }); }; addAndMakeVisible(toolsBtn);
        recBtn.setButtonText("RECORD..."); recBtn.onClick = [this] { authoring::openRecord(store, slot, recBtn, [this] { juce::MessageManager::callAsync([alive = juce::Component::SafePointer<SmpPage>(this)] { if (alive) alive->build(); }); }); }; addAndMakeVisible(recBtn);
        imgBtn.setButtonText("IMAGE > AUDIO..."); imgBtn.onClick = [this] { authoring::openImageAudio(store, slot, imgBtn, [this] { juce::MessageManager::callAsync([alive = juce::Component::SafePointer<SmpPage>(this)] { if (alive) alive->build(); }); }); }; addAndMakeVisible(imgBtn);
        popBtn.setButtonText("RANDOM POPULATE"); fxBtn.setButtonText("RANDOM SAMPLER FX"); addAndMakeVisible(popBtn); addAndMakeVisible(fxBtn);
        popBtn.onClick = [this] {   // every slot gets a random sample from the library
            auto names = assets::sampleNames(); if (names.isEmpty()) return; juce::Random rnd;
            for (int i = 0; i < 16; ++i) { const juce::String p = "layers.SMP.oscs." + juce::String(i); store.set(p + ".name", names[rnd.nextInt(names.size())]); if (store.num(p + ".level", 0) <= 0) store.set(p + ".level", 0.8); }
            build(); };
        fxBtn.onClick = [this] {    // every slot gets its own microphone and its own speaker (two independent shuffles of the 16 real types)
            juce::Random rnd; juce::Array<int> mics, spks; for (int i = 1; i < kMicIds.size(); ++i) mics.add(i); for (int i = 1; i < kSpkIds.size(); ++i) spks.add(i);
            auto shuffle = [&rnd](juce::Array<int>& a) { for (int i = a.size() - 1; i > 0; --i) a.swap(i, rnd.nextInt(i + 1)); }; shuffle(mics); shuffle(spks);
            for (int i = 0; i < 16; ++i) { const juce::String p = "layers.SMP.oscs." + juce::String(i);
                store.set(p + ".mic", kMicIds[mics[i % mics.size()]]); store.set(p + ".spk", kSpkIds[spks[i % spks.size()]]);
                if (store.num(p + ".micMix", 0) <= 0) store.set(p + ".micMix", 0.5); if (store.num(p + ".spkMix", 0) <= 0) store.set(p + ".spkMix", 0.5); }
            build(); };
        build(); }
    void resized() override { slotBox.setBounds(8, 4, 140, 26); popBtn.setBounds(156, 4, 150, 26); fxBtn.setBounds(312, 4, 170, 26); toolsBtn.setBounds(488, 4, 160, 26); recBtn.setBounds(654, 4, 110, 26); imgBtn.setBounds(770, 4, 150, 26); view.setBounds(getLocalBounds().withTrimmedTop(34)); layoutContent(); }
    void build() {
        clear(); const juce::String p = "layers.SMP.oscs." + juce::String(slot);
        {   // responsive waveform editor (inline); FULL SCREEN EDIT opens the overlay with the slice list
            auto rebuild = [alive = juce::Component::SafePointer<SmpPage>(this)] { juce::MessageManager::callAsync([alive] { if (alive) alive->build(); }); };
            auto& w = panel("Sample waveform - slot " + juce::String(slot + 1));
            auto* ed = new SampleEditorPanel(store, slot, false, rebuild);
            ed->setOpenFull([this, rebuild] { openSampleEditorFullScreen(*this, store, slot, rebuild); });
            w.customOwned(ed, -1, 300);
        }
        auto& o = panel("Sampler slot " + juce::String(slot + 1)); o.setKnobScale(0.9f);
        auto names = assets::sampleNames(); { auto lib = store.get("sampleLibrary"); if (lib.isArray()) for (int i = 0; i < lib.size(); ++i) names.addIfNotAlreadyThere(lib[i].getProperty("name", {}).toString()); } names.insert(0, "");   // built-in + the preset's own embedded / processed samples
        o.choice("Sample", p + ".name", names, "");
        // microphone and speaker colouring, side by side under the sample list
        o.choice("Microphone", p + ".mic", kMicIds, "none").setLabels(kMicLabels);
        o.choice("Speaker", p + ".spk", kSpkIds, "none").setLabels(kSpkLabels);
        o.newRow();
        o.toggle("Bleed", p + ".bleedOn");
        o.knob("Start", p + ".start", 0, 1, 0); o.knob("End", p + ".end", 0, 1, 1);
        o.knob("Fade in", p + ".fadeIn", 0, 2, 0, { "%.2f", " s", false, 2 }); o.knob("Fade out", p + ".fadeOut", 0, 2, 0, { "%.2f", " s", false, 2 });
        o.newRow();
        o.toggle("Loop", p + ".loop", true); o.toggle("Ping-pong", p + ".pingPong"); o.toggle("Reverse", p + ".reverse"); o.toggle("Noise", p + ".noiseOn"); o.choice("Noise type", p + ".noiseType", { "white", "pink", "brown", "blue", "violet" }, "white");
        auto& ga = panel("Global sampler ADSR"); ga.customOwned(new SlotAdvanceButton(store), 250, 34); ga.toggle("Global ADSR (all 16 slots)", "layers.SMP.globalAdsrActive"); addEnv(ga, "layers.SMP.globalAdsr");
        auto& c = panel("Slot performance"); c.setKnobScale(0.9f);
        c.knob("Level", p + ".level", 0, 1, 0.8); c.knob("Bleed amount", p + ".bleed", 0, 1, 0.5);   // Level sits left of Octave; Bleed Amount follows Level
        addOscCommon(c, p, false);
        auto& e = panel("Slot envelope"); addEnv(e, p + ".env");
        e.toggle("Arp On/Off", p + ".arpOn", slot == 0);   // per-sample arp participation (default: slot 1 only)
        auto& f = panel("Slot filter"); addFilter(f, p + ".filter", p + ".filterEnv", 18000);
        layerPanels(*this, "SMP");
        layoutContent();
    }
    int slot = 0; juce::ComboBox slotBox; juce::TextButton popBtn, fxBtn, toolsBtn, recBtn, imgBtn;
};

struct ProPage : Page {
    explicit ProPage(Store& s) : Page(s) {
        for (int i = 0; i < 2; ++i) {
            const juce::String p = "layers.PRO.oscs." + juce::String(i);
            auto& g = panel(juce::String("Generator ") + (i == 0 ? "A" : "B")); g.setKnobScale(0.9f);
            g.toggle("Enabled", p + ".enabled"); g.choice("Model", p + ".model", { "pluck", "modal", "breath", "chaos", "bow", "flute" }, "pluck");
            g.newRow();
            g.knob("Level", p + ".level", 0, 1, 0.8); g.knob("Tone", p + ".bright", 0, 1, 0.5); g.knob("Damp", p + ".damp", 0, 1, 0.4);
            g.knob("Body", p + ".body", 0, 1, 0.3); g.knob("Chaos", p + ".chaos", 0, 1, 0.4);
            g.newRow();   // model-specific dials (the web shows each only for the models that use it: Stiffness = pluck / bow, Strike material = modal, Bore = breath)
            g.knob("Stiffness", p + ".stiffness", 0, 1, 0); g.knob("Bore (cyl-conical)", p + ".bore", 0, 1, 0);
            g.choiceNum("Strike material", p + ".excite", { { "Felt mallet", 0 }, { "Hard wood", 1 }, { "Metal hammer", 2 }, { "Finger pluck", 3 } }, 0);
            g.knob("Tone keytrack", p + ".brightKeyTrack", -100, 100, 0, { "%.0f", "", false, 0 }); g.knob("Damp keytrack", p + ".dampKeyTrack", -100, 100, 0, { "%.0f", "", false, 0 });
            {   // wooden flutes / physical breath (model "flute"): instrument + the 43 controls, grouped as in the web UI; every knob drives the flute core live and is a GEN:PRO.fl_<key> modulation target
                juce::StringArray insts; for (int k = 0; k < 7; k++) insts.add(vv::flute::kProfiles[k].name);
                auto& fp = panel(juce::String("Generator ") + (i == 0 ? "A" : "B") + " flute (model: flute)"); fp.setKnobScale(0.9f);
                fp.choice("Instrument", p + ".flute.instrument", insts, "shakuhachi");
                const char* last = nullptr; int n = 0;
                for (int k = 0; k < vv::flute::kNumParams; k++) { const auto& d = vv::flute::kParams[k];
                    if (!last || std::strcmp(last, d.section) != 0 || n >= 6) { fp.newRow(); n = 0; last = d.section; }
                    fp.knob(d.label, p + ".flute." + d.key, d.lo, d.hi, d.def); n++; }
            }
            auto& c = panel(juce::String("Generator ") + (i == 0 ? "A" : "B") + " performance"); c.setKnobScale(0.9f); addOscCommon(c, p);
        }
        { auto& cp = panel("Coupling (A <-> B)"); cp.knob("Coupling", "layers.PRO.proCoupling", 0, 1, 0); }   // each generator's output excites the other through a ~3 ms junction delay
        layerPanels(*this, "PRO");
    }
};

struct SidPage : Page {
    explicit SidPage(Store& s) : Page(s) {
        const juce::StringArray waves{ "triangle", "sawtooth", "pulse", "noise", "tri_saw", "tri_pulse", "saw_pulse" };
        for (int i = 0; i < 3; ++i) {
            const juce::String p = "layers.SID.oscs." + juce::String(i);
            auto& g = panel("SID voice " + juce::String(i + 1)); g.setKnobScale(0.9f);
            g.toggle("Enabled", p + ".enabled"); g.choice("Waveform", p + ".wave", waves, "triangle"); g.toggle("Ring mod (from previous voice)", p + ".ringModOn"); g.toggle("Hard sync (to previous voice)", p + ".syncOn"); g.toggle("Follow arp", p + ".arpOn");
            g.newRow();
            g.knob("Level", p + ".level", 0, 1, 0.8); g.knob("Pulse width", p + ".pulseWidth", 0.02, 0.98, 0.5); g.knob("Vibrato", p + ".vibratoDepth", 0, 1, 0);
            auto& c = panel("SID voice " + juce::String(i + 1) + " performance"); c.setKnobScale(0.9f); addOscCommon(c, p);
        }
        panel("SID layer").knob("Balance", "layers.SID.pan", -1, 1, 0);
        panel("Wavetable arp (chiptune step table; voices with Follow arp rewrite pitch / wave / gate each step)").customOwned(new ArpSteps(s), -1, 190);
        layerPanels(*this, "SID");
    }
};

struct LuaPage : Page {
    explicit LuaPage(Store& s) : Page(s) {
        panel("LUA oscillator (scripted - four script slots)").customOwned(new LuaEditor(s), -1, 860);
        for (int i = 0; i < 4; ++i) {
            const juce::String p = "layers.LUA.oscs." + juce::String(i); auto& g = panel("LUA slot " + juce::String(i + 1) + " output"); g.setKnobScale(0.9f);
            g.knob("Level", p + ".level", 0, 1, 0.8); g.knob("Width", p + ".width", 0, 2, 1); g.toggle("Invert polarity", p + ".polarity"); g.toggle("Mono", p + ".mono"); g.newRow();
            g.knob("Velocity low", p + ".velLow", 1, 127, 1, { "%.0f", "", false, 0 }); g.knob("Velocity high", p + ".velHigh", 1, 127, 127, { "%.0f", "", false, 0 }); g.knob("Velocity curve", p + ".velCurve", -1, 1, 0); g.knob("Velocity sensitivity", p + ".velSens", 0, 1, 1); g.newRow();
            g.knob("Seed", p + ".lua.seed", 1, 9999, 1, { "%.0f", "", false, 0 }); g.toggle("Lock seed (same sound every note)", p + ".lua.seedLock", true);
            auto& c = panel("LUA slot " + juce::String(i + 1) + " performance"); c.setKnobScale(0.9f); addOscCommon(c, p);
        }
        panel("LUA layer").knob("Balance", "layers.LUA.pan", -1, 1, 0);
        layerPanels(*this, "LUA");
    }
};

} // namespace

SoundScreen::SoundScreen(Store& s) {
    pages_.add(new WtPage(s)); pages_.add(new FmPage(s)); pages_.add(new SmpPage(s)); pages_.add(new ProPage(s)); pages_.add(new SidPage(s)); pages_.add(new LuaPage(s));
    automation_ = new AutomationScreen(s); pages_.add(automation_);   // the AUTOMATION button after SID
    const char* n[] = { "Wavetable", "FM", "Sampler", "Procedural", "SID", "LUA", "AUTOMATION" };
    for (int i = 0; i < 7; ++i) tabs.add(n[i], pages_[i]);
    addAndMakeVisible(tabs);
}
void SoundScreen::showAutomation(const juce::String& ch) { tabs.select(6); if (automation_) automation_->setChannel(ch); }

} // namespace vv
