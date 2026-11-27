// MOD screen: LFOs + macros + envelope follower, modulation matrix, cross-modulation, vocoder.
#include "CurveEditor.h"
#include "WireOverview.h"
#include "Screens.h"

namespace vv {
namespace {

const juce::StringArray kModSources{ "lfo1", "lfo2", "lfo3", "lfo4", "lfo5", "lfo6", "velocity", "modwheel", "random", "envWT", "envFM", "envSMP", "envFollow",
    "macro1", "macro2", "macro3", "macro4", "mpeTimbre", "mpePressure", "wtOsc1Pos", "wtOsc2Pos", "wtOsc1Shape", "wtOsc2Shape", "note",
    "xy1x", "xy1y", "xy2x", "xy2y", "xy3x", "xy3y", "xy4x", "xy4y" };

std::vector<std::pair<juce::String, juce::StringArray>> destGroups() {
    std::vector<std::pair<juce::String, juce::StringArray>> g;
    for (auto l : { "WT", "FM", "SMP", "PRO" }) { juce::String L(l); g.push_back({ L + " voice", { L + ":cutoff", L + ":reso", L + ":level", L + ":pitch", L + ":filterVolume" } }); }
    g.push_back({ "WT oscillators", { "WT:osc1Cutoff", "WT:osc2Cutoff", "WT:osc1Reso", "WT:osc2Reso", "WT:osc1Octave", "WT:osc2Octave", "WT:osc1Semi", "WT:osc2Semi", "WT:osc1Fine", "WT:osc2Fine",
        "WT:start", "WT:scan", "WT:pwm", "WT:phase", "WT:startEnd", "WT:osc1Scan", "WT:osc2Scan", "WT:osc1Pwm", "WT:osc2Pwm", "WT:osc1PwmRate", "WT:osc2PwmRate", "SID:voice1Pwm", "SID:voice2Pwm", "SID:voice3Pwm", "FM6OP:fine", "FM6OP:detune", "FM6OP:level", "FM6OP:ampModDepth", "FM6OP:r2", "FM6OP:r3", "FM6OP:r4", "FM6OP:l1", "FM6OP:l2", "WT:osc1Phase", "WT:osc2Phase",
        "WT:osc1StartEnd", "WT:osc2StartEnd", "WT:osc1Segment", "WT:osc2Segment", "WT:osc1Level", "WT:osc2Level" } });
    juce::StringArray fm{ "FM:engACutoff", "FM:engBCutoff", "FM:engAReso", "FM:engBReso", "FM:engAOctave", "FM:engBOctave", "FM:engASemi", "FM:engBSemi", "FM:engAFine", "FM:engBFine", "FM:engAFeedback", "FM:engBFeedback" };
    for (int i = 1; i <= 8; ++i) fm.add("FM:op" + juce::String(i) + "Ratio");
    g.push_back({ "FM engines", fm });
    juce::StringArray pro; for (auto e : { "A", "B" }) for (auto k : { "Tone", "Damp", "Body", "Chaos" }) pro.add(juce::String("PRO:gen") + e + k);
    g.push_back({ "Procedural", pro });
    g.push_back({ "Sampler", { "SMP:start", "SMP:end", "GLOBAL:sampleMix" } });
    juce::StringArray slots; for (int n = 1; n <= 16; ++n) for (auto k : { "Cutoff", "Reso", "Bleed" }) slots.add("SMP:s" + juce::String(n) + k);
    g.push_back({ "Sampler slots (filter + bleed)", slots });
    g.push_back({ "Global", { "GLOBAL:macro1", "GLOBAL:macro2", "GLOBAL:macro3", "GLOBAL:macro4", "GLOBAL:wtFmMix", "GLOBAL:masterTone", "GLOBAL:xmod1", "GLOBAL:xmod2", "GLOBAL:xmod3", "GLOBAL:xmod4", "ARP:rate" } });
    juce::StringArray lfo; for (int n = 1; n <= 6; ++n) for (auto k : { "rate", "amount", "phase" }) lfo.add("LFO" + juce::String(n) + ":" + k);
    g.push_back({ "LFOs", lfo });
    g.push_back({ "Effects", { "FX:reverbMix", "FX:satMix", "FX:satDrive", "FX:flangerRate", "FX:flangerMix", "FX:flangerFeedback", "FX:foldDrive", "FX:foldMix", "FX:foldSymmetry", "FX:foldPwm",
        "FX:specBlur", "FX:specShift", "FX:specThreshold", "FX:specSpread", "FX:specMix", "FX:filterCutoff", "FX:filterResonance", "FX:grainSize", "FX:grainPitch", "FX:grainPan", "FX:grainDensity",
        "FX:delayFeedback", "FX:delayMix", "FX:stutterDivision", "FX:bitDepth", "FX:phaserMix", "FX:phaserRate", "FX:phaserFeedback", "FX:resonatorFreq", "FX:resonatorResonance", "FX:resonatorMix" } });
    g.push_back({ "FX parameters (per effect type)", { "FXP:reverb:size", "FXP:reverb:decay", "FXP:reverb:predelay", "FXP:reverb:mod", "FXP:reverb:colour", "FXP:reverb:rvTail", "FXP:reverb:rvTrans", "FXP:delay:time", "FXP:saturation:tone", "FXP:spectral:freeze", "FXP:granular:spread", "FXP:granular:followerAmount", "FXP:granular:mix", "FXP:granular:delay", "FXP:granular:posRand", "FXP:granular:sizeRand", "FXP:granular:jitter", "FXP:granular:fine", "FXP:granular:pitchRand", "FXP:granular:reverse", "FXP:granular:volRand", "FXP:granular:width", "FXP:granular:drift", "FXP:granular:lrSpread", "FXP:granular:outLevel", "FXP:granular:curve", "FXP:granular:symmetry", "FXP:granular:atk", "FXP:granular:dec", "FXP:granular:sus", "FXP:granular:rel", "FXP:granular:fb", "FXP:granular:fbDamp", "FXP:granular:fbLow", "FXP:granular:fbSat", "FXP:granular:fbDiff", "FXP:granular:pingPong", "FXP:granular:rndDepth", "FXP:granular:rndRate", "FXP:granular:seed", "FXP:tape:wowRate", "FXP:tape:wowDepth", "FXP:tape:flutterRate", "FXP:tape:flutterDepth", "FXP:tape:dust", "FXP:tape:crackle", "FXP:tape:mix", "FXP:phaser:depth", "FXP:phaser:stages", "FXP:flanger:depth", "FXP:glitch:srReduction", "FXP:glitch:stutterAmount", "FXP:glitch:mix", "FXP:chorus:mix", "FXP:echo:time", "FXP:echo:count", "FXP:echo:density", "FXP:echo:early", "FXP:echo:late", "FXP:echo:feedback", "FXP:echo:decay", "FXP:echo:diffusion", "FXP:echo:hfDamp", "FXP:echo:lfDamp", "FXP:echo:width", "FXP:echo:spread", "FXP:echo:modDepth", "FXP:echo:modRate", "FXP:echo:movement", "FXP:echo:mix", "FXP:echo:level", "FXP:panning:width", "FXP:panning:rate", "FXP:panning:mix", "FXP:panning:distance", "FXP:filter:mix", "FXP:interstellar:span", "FXP:interstellar:tonal", "FXP:interstellar:band1", "FXP:interstellar:band2", "FXP:interstellar:band3", "FXP:interstellar:band4", "FXP:interstellar:band5", "FXP:interstellar:band6", "FXP:interstellar:segments", "FXP:interstellar:segTime", "FXP:interstellar:stretch", "FXP:interstellar:bloom", "FXP:interstellar:density", "FXP:interstellar:life", "FXP:interstellar:pitchUp", "FXP:interstellar:upProb", "FXP:interstellar:pitchDown", "FXP:interstellar:downProb", "FXP:interstellar:body", "FXP:interstellar:particles", "FXP:interstellar:spread", "FXP:interstellar:resonance", "FXP:interstellar:ring", "FXP:interstellar:strum", "FXP:interstellar:evolve", "FXP:interstellar:drift", "FXP:interstellar:space", "FXP:interstellar:size", "FXP:interstellar:decay", "FXP:interstellar:predelay", "FXP:interstellar:damp", "FXP:interstellar:revMod", "FXP:interstellar:width", "FXP:interstellar:shimmer", "FXP:interstellar:character", "FXP:interstellar:mix", "FXP:interstellar:level", "FXP:interstellar:freeze", "FXP:interstellar:revFreeze" } });
    g.push_back({ "Generic layer params (envelope, pan, width, drift ...)", { "GEN:WT.pan", "GEN:WT.attack", "GEN:WT.decay", "GEN:WT.sustain", "GEN:WT.release", "GEN:FM.pan", "GEN:FM.attack", "GEN:FM.decay", "GEN:FM.sustain", "GEN:FM.release", "GEN:SMP.pan", "GEN:SMP.attack", "GEN:SMP.decay", "GEN:SMP.sustain", "GEN:SMP.release", "GEN:PRO.pan", "GEN:PRO.attack", "GEN:PRO.decay", "GEN:PRO.sustain", "GEN:PRO.release", "GEN:WT.width", "GEN:WT.detune", "GEN:WT.drift", "GEN:PRO.drift", "GEN:PRO.level", "GEN:SMP.micMix", "GEN:SMP.spkMix", "GEN:SMP.octave", "GEN:SMP.semi", "GEN:SMP.fine", "GEN:PRO.octave", "GEN:PRO.semi", "GEN:PRO.fine", "GEN:SID.pan", "GEN:SID.attack", "GEN:SID.decay", "GEN:SID.sustain", "GEN:SID.release", "GEN:SID.octave", "GEN:SID.semi", "GEN:SID.fine", "GEN:SID.vibrato", "GEN:WT2.attack", "GEN:WT2.decay", "GEN:WT2.sustain", "GEN:WT2.release", "GEN:FM2.attack", "GEN:FM2.decay", "GEN:FM2.sustain", "GEN:FM2.release", "GEN:WT.unisonBlend", "GEN:WT.unisonPhase", "GEN:WT.eqBand1Freq", "GEN:WT.eqBand1Gain", "GEN:WT.eqBand2Freq", "GEN:WT.eqBand2Gain", "GEN:WT.eqBand3Freq", "GEN:WT.eqBand3Gain", "GEN:WT.eqBand4Freq", "GEN:WT.eqBand4Gain", "GEN:FM.eqBand1Freq", "GEN:FM.eqBand1Gain", "GEN:FM.eqBand2Freq", "GEN:FM.eqBand2Gain", "GEN:FM.eqBand3Freq", "GEN:FM.eqBand3Gain", "GEN:FM.eqBand4Freq", "GEN:FM.eqBand4Gain", "GEN:SMP.eqBand1Freq", "GEN:SMP.eqBand1Gain", "GEN:SMP.eqBand2Freq", "GEN:SMP.eqBand2Gain", "GEN:SMP.eqBand3Freq", "GEN:SMP.eqBand3Gain", "GEN:SMP.eqBand4Freq", "GEN:SMP.eqBand4Gain", "GEN:PRO.eqBand1Freq", "GEN:PRO.eqBand1Gain", "GEN:PRO.eqBand2Freq", "GEN:PRO.eqBand2Gain", "GEN:PRO.eqBand3Freq", "GEN:PRO.eqBand3Gain", "GEN:PRO.eqBand4Freq", "GEN:PRO.eqBand4Gain", "GEN:SID.eqBand1Freq", "GEN:SID.eqBand1Gain", "GEN:SID.eqBand2Freq", "GEN:SID.eqBand2Gain", "GEN:SID.eqBand3Freq", "GEN:SID.eqBand3Gain", "GEN:SID.eqBand4Freq", "GEN:SID.eqBand4Gain", "GEN:LUA.eqBand1Freq", "GEN:LUA.eqBand1Gain", "GEN:LUA.eqBand2Freq", "GEN:LUA.eqBand2Gain", "GEN:LUA.eqBand3Freq", "GEN:LUA.eqBand3Gain", "GEN:LUA.eqBand4Freq", "GEN:LUA.eqBand4Gain", "GEN:VOC.voc_mix", "GEN:VOC.voc_formantSt", "GEN:VOC.voc_q", "GEN:VOC.voc_releaseMs", "GEN:VOC.voc_hpf", "GEN:VOC.voc_micGainDb", "GEN:VOC.voc_noiseDb", "GEN:VOC.voc_sibSens", "GEN:VOC.voc_pitchSpeed", "GEN:VOC.voc_chorusRate" } });
    return g;
}

struct LfoPage : Page {
    explicit LfoPage(Store& s) : Page(s) {
        auto& m = panel("Macros"); for (int i = 0; i < 4; ++i) m.knob("Macro " + juce::String(i + 1), "macros." + juce::String(i), 0, 1, 0);
        static const double lfoHz[6] = { 0.25, 0.5, 2, 6, 1, 3 };
        for (int n = 1; n <= 6; ++n) {
            const juce::String p = "lfo.lfo" + juce::String(n);
            auto& l = panel("LFO " + juce::String(n));
            l.choice("Waveform", p + ".waveform", { "sine", "triangle", "pulse", "sawUp", "sawDown", "sh", "noise", "staircase", "stairsDown", "stroll", "organic", "morph", "morph2", "custom" }, "sine").onChosen = [this, p](const juce::String& prev, const juce::String& now) {
                if (now == "custom" && readCurve(store, p + ".shape").size() < 2) writeCurve(store, p + ".shape", basicWaveformPoints(prev)); };   // start from the shape that was selected, like the web
            l.choice("Rate mode", p + ".rateMode", { "hz", "sync" }, "hz");
            l.choiceNum("Division (sync)", p + ".division", { { "16/1", 64 }, { "8/1", 32 }, { "4/1", 16 }, { "2/1", 8 }, { "1/1", 4 }, { "1/2", 2 }, { "1/4", 1 }, { "1/8", 0.5 }, { "1/16", 0.25 }, { "1/32", 0.125 } }, 0.25);
            l.newRow();
            l.knob("Rate (Hz mode)", p + ".hz", 0.02, 20, lfoHz[n - 1], { "%.2f", " Hz", true, 2 }); l.knob("Amount", p + ".amount", 0, 2, 1); l.knob("Phase offset", p + ".phaseOffset", 0, 1, 0); l.knob("Smoothing", p + ".smoothing", 0, 1, 0); l.knob("Morph (Morph waveforms)", p + ".morph", 0, 1, 0.5);
            curveBtns.add(new CurveButton(s, p + ".shape", "DRAW SHAPE")); l.custom(curveBtns.getLast(), 150, 34);
            l.toggle("Hold", p + ".held"); l.toggle("Bipolar", p + ".bipolar", true);
            retrig.add(new RetrigToggle(s, p + ".mode")); l.custom(retrig.getLast(), 132, 34);
        }
        auto& e = panel("Envelope follower");
        e.knob("Attack", "envFollow.a", 0.001, 2, 0.01, { "%.3f", " s", true, 3 }); e.knob("Decay", "envFollow.d", 0.001, 2, 0.3, { "%.3f", " s", true, 3 });
        e.knob("Sustain", "envFollow.s", 0, 1, 0.4); e.knob("Release", "envFollow.r", 0.001, 4, 0.4, { "%.3f", " s", true, 3 });
        curveBtns.add(new CurveButton(s, "envFollow.shape", "DRAW CUSTOM SHAPE")); e.custom(curveBtns.getLast(), 190, 34);
    }
    juce::OwnedArray<CurveButton> curveBtns;
    // "Retrigger on note" is the string mode "retrig" / "free"
    struct RetrigToggle : Control {
        RetrigToggle(Store& s, const juce::String& p) : Control(s, p) { b.setButtonText("Retrigger on note"); b.onClick = [this] { if (!updating_) store.set(path_, b.getToggleState() ? "retrig" : "free"); }; addAndMakeVisible(b); refresh(); }
        void refresh() override { updating_ = true; b.setToggleState(store.str(path_, "free") == "retrig", juce::dontSendNotification); updating_ = false; }
        void resized() override { b.setBounds(getLocalBounds().reduced(2)); }
        juce::Point<int> preferredSize() const override { return { 160, 34 }; }
        juce::ToggleButton b;
    };
    juce::OwnedArray<RetrigToggle> retrig;
};

// One modulation route: source -> destination, amount, delete.
struct RouteRow : Control {
    RouteRow(Store& s, int idx) : Control(s, "connections." + juce::String(idx)), index(idx) {
        for (int i = 0; i < kModSources.size(); ++i) src.addItem(kModSources[i], i + 1);
        int id = 1; for (auto& g : destGroups()) { dst.addSectionHeading(g.first); for (auto& d : g.second) { dst.addItem(d, id); ids.set(id, d); ++id; } }
        src.onChange = [this] { if (!updating_) store.set(path_ + ".source", src.getText()); };
        dst.onChange = [this] { if (!updating_) store.set(path_ + ".dest", ids[dst.getSelectedId()]); };
        amt.setSliderStyle(juce::Slider::LinearHorizontal); amt.setRange(-100, 100, 1); amt.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        amt.setDoubleClickReturnValue(true, 0); amt.textFromValueFunction = [](double v) { return juce::String((int) v) + " %"; };
        amt.onValueChange = [this] { if (!updating_) store.set(path_ + ".amount", amt.getValue()); };
        edit.setButtonText("Edit"); edit.onClick = [this] { openEditor(); };
        del.setButtonText("Remove"); del.onClick = [this] { juce::MessageManager::callAsync([&s = store, i = index] { s.remove("connections", i); }); };
        addAndMakeVisible(src); addAndMakeVisible(dst); addAndMakeVisible(amt); addAndMakeVisible(edit); addAndMakeVisible(del); refresh();
    }
    void refresh() override {
        updating_ = true; src.setText(store.str(path_ + ".source", "lfo1"), juce::dontSendNotification);
        const auto d = store.str(path_ + ".dest", "WT:cutoff"); for (juce::HashMap<int, juce::String>::Iterator it(ids); it.next();) if (it.getValue() == d) dst.setSelectedId(it.getKey(), juce::dontSendNotification);
        amt.setValue(store.num(path_ + ".amount", 0), juce::dontSendNotification); updating_ = false;
    }
    void resized() override { auto r = getLocalBounds().reduced(0, 4); del.setBounds(r.removeFromRight(70)); r.removeFromRight(4); edit.setBounds(r.removeFromRight(52)); r.removeFromRight(4); src.setBounds(r.removeFromLeft(130)); r.removeFromLeft(6); dst.setBounds(r.removeFromLeft(190)); r.removeFromLeft(8); amt.setBounds(r); }
    juce::Point<int> preferredSize() const override { return { -1, 44 }; }
    // Edit: dialog to change this connection's source, destination and amount (sits before Remove, like the web wire list)
    void openEditor() {
        auto* w = new juce::AlertWindow("Edit connection", "Change the source, destination and amount.", juce::MessageBoxIconType::NoIcon);
        w->addComboBox("src", kModSources, "Source"); w->addComboBox("dst", {}, "Destination");
        auto* dc = w->getComboBoxComponent("dst"); int id = 1, sel = 1; const auto cur = store.str(path_ + ".dest", "WT:cutoff");
        for (auto& g : destGroups()) { dc->addSectionHeading(g.first); for (auto& d : g.second) { dc->addItem(d, id); if (d == cur) sel = id; ++id; } }
        dc->setSelectedId(sel, juce::dontSendNotification); w->getComboBoxComponent("src")->setText(store.str(path_ + ".source", "lfo1"), juce::dontSendNotification);
        w->addTextEditor("amt", juce::String((int) store.num(path_ + ".amount", 0)), "Amount (%)");
        w->addButton("Apply", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
            if (r == 1) { store.set(path_ + ".source", w->getComboBoxComponent("src")->getText()); store.set(path_ + ".dest", w->getComboBoxComponent("dst")->getText());
                          store.set(path_ + ".amount", juce::jlimit(-100.0, 100.0, w->getTextEditorContents("amt").getDoubleValue())); refresh(); }
            delete w; }), false);
    }
    int index; juce::ComboBox src, dst; juce::Slider amt; juce::TextButton edit, del; juce::HashMap<int, juce::String> ids;
};

struct MatrixPage : Page, private Store::Listener {
    explicit MatrixPage(Store& s) : Page(s) { s.addListener(this); build(); }
    ~MatrixPage() override { store.removeListener(this); }
    void storeChanged() override { if (store.count("connections") != built) build(); }
    void build() {
        clear(); rows.clear(); built = store.count("connections");
        auto& p = panel("Routes (" + juce::String(built) + ")");
        for (int i = 0; i < built; ++i) { rows.add(new RouteRow(store, i)); p.custom(rows.getLast(), -1, 44); p.newRow(); }
        add.setButtonText("+ Add route"); add.onClick = [this] { juce::MessageManager::callAsync([this] { store.append("connections", [] { auto* o = new juce::DynamicObject(); o->setProperty("source", "lfo1"); o->setProperty("dest", "WT:cutoff"); o->setProperty("amount", 30); return juce::var(o); }()); }); };
        p.custom(&add, 160, 32);
        auto& ov = panel("Wire Overview (click a cell to add / remove a wire; click a group header to expand / collapse)");
        overview = std::make_unique<WireOverview>(store, kModSources, destGroups());
        overview->relayout(); ov.custom(overview.get(), overview->getWidth(), overview->getHeight());
        layoutContent();
    }
    std::unique_ptr<WireOverview> overview;
    int built = -1; juce::OwnedArray<RouteRow> rows; juce::TextButton add;
};

struct XmodPage : Page {
    explicit XmodPage(Store& s) : Page(s) {
        note.setText("The source layer's own sound (tapped before the WT/FM mixer and solo/mute) modulates the target layer. FM applies to notes played after the change; RM, AM and PM are live.", juce::dontSendNotification);
        note.setFont(VVLookAndFeel::mono(12.f)); note.setColour(juce::Label::textColourId, col::dim);
        auto& h = panel("Cross-modulation"); h.custom(&note, -1, 40);
        for (int i = 0; i < 4; ++i) {
            const juce::String p = "xmod." + juce::String(i);
            auto& x = panel("Slot " + juce::String(i + 1));
            x.toggle("On", p + ".on"); x.choice("Source layer", p + ".src", { "WT", "FM", "SMP", "PRO" }, "WT"); x.choice("Target layer", p + ".dst", { "WT", "FM", "SMP", "PRO" }, "FM");
            x.choice("Type", p + ".type", { "RM", "AM", "PM", "FM" }, "RM"); x.knob("Amount", p + ".amount", 0, 1, 0.5);
        }
    }
    juce::Label note;
};

struct VocoderPage : Page {
    explicit VocoderPage(Store& s) : Page(s) {
        auto& v = panel("Hybrid Vocoder & Vocal Resynthesis");
        v.toggle("On", "vocoder.on");
        v.choice("Mode", "vocoder.mode", { "classic", "pitch", "spectral" }, "classic");
        v.choice("Modulator", "vocoder.modSrc", { "mic", "WT", "FM", "SMP", "PRO" }, "WT");
        v.choice("Carrier", "vocoder.carSrc", { "internal", "SMP", "WT", "FM", "PRO", "external" }, "SMP");
        v.knob("Dry / wet", "vocoder.mix", 0, 1, 0.8);
        auto& m = panel("Modulator conditioning");
        m.knob("Mic gain (dB)", "vocoder.micGainDb", -24, 36, 0); m.knob("High-pass (Hz)", "vocoder.hpf", 80, 150, 100); m.knob("Gate (dB)", "vocoder.gateDb", -80, -20, -80);
        m.toggle("Compressor / limiter", "vocoder.comp", true); m.knob("Sibilance sens", "vocoder.sibSens", 0, 1, 0.5); m.knob("Unvoiced noise (dB)", "vocoder.noiseDb", -60, 0, -18);
        auto& b = panel("Bank & formants");
        b.choiceNum("Bands", "vocoder.bands", { { "8", 8 }, { "12", 12 }, { "16", 16 }, { "24", 24 } }, 8);
        b.choice("Band spacing", "vocoder.spacing", { "log", "lin" }, "log");
        b.newRow(); b.toggle("4 Pole Bands", "vocoder.poles4", false);
        b.knob("Formant Shift", "vocoder.formantSt", -12, 12, 0); b.knob("Envelope Release", "vocoder.releaseMs", 2, 500, 18); b.knob("Resonance (Q)", "vocoder.q", 0.5, 20, 4);
        auto& c = panel("Carrier, ensemble & pitch corrector");
        c.knob("Carrier drive (%)", "vocoder.drive", 0, 100, 0); c.knob("Chorus depth (%)", "vocoder.chorusDepth", 0, 100, 0); c.knob("Chorus rate (Hz)", "vocoder.chorusRate", 0.1, 5, 0.8);
        c.newRow(); c.toggle("Internal carrier drone (C2) when no key held", "vocoder.drone", false);
        c.choice("Pitch Scale", "vocoder.pitchScale", { "chromatic", "major", "minor", "harmonicMinor", "dorian", "mixolydian", "majorPent", "minorPent", "blues", "japanese", "spanish", "egyptian", "indonesian", "filipino", "jazz", "fSharpMajor", "kumoi", "inSen", "chineseGong", "chineseShang", "majorBlues", "altered" }, "chromatic");
        c.choiceNum("Root", "vocoder.pitchRoot", { { "C", 0 }, { "C#", 1 }, { "D", 2 }, { "D#", 3 }, { "E", 4 }, { "F", 5 }, { "F#", 6 }, { "G", 7 }, { "G#", 8 }, { "A", 9 }, { "A#", 10 }, { "B", 11 } }, 0);
        c.knob("Correction Speed", "vocoder.pitchSpeed", 0, 400, 20); c.toggle("Snap to held keys", "vocoder.pitchHeld", false);
        auto& sl = panel("Carrier slots (sampler slots that feed the vocoder)");
        for (int i = 0; i < 16; ++i) sl.toggle("Slot " + juce::String(i + 1), "vocoder.slots." + juce::String(i), true);
    }
};

} // namespace

ModScreen::ModScreen(Store& s, int which) {
    pages_.add(new LfoPage(s)); pages_.add(new MatrixPage(s)); pages_.add(new XmodPage(s)); pages_.add(new VocoderPage(s));
    if (which >= 0 && which < 4) { single_ = pages_[which]; addAndMakeVisible(single_); return; }
    const char* n[] = { "LFO / Macros", "Matrix", "Cross-mod", "Vocoder" };
    for (int i = 0; i < 4; ++i) tabs.add(n[i], pages_[i]);
    addAndMakeVisible(tabs);
}


// Exported for the automation screen: the same source / destination vocabulary as the matrix.
std::vector<std::pair<juce::String, juce::StringArray>> modDestGroups() { return destGroups(); }
juce::StringArray modSourceList() { return kModSources; }

} // namespace vv
