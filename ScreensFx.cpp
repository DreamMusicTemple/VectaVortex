// FX rack, Settings and Presets screens.
#include "Screens.h"
#include "PowerSettings.h"
#include "Appearance.h"
#include "MidiLearn.h"
#include "VoidView.h"
#include "GrainBridgeView.h"
#include "WarpView.h"
#include "IrPanel.h"
#include <set>
#include "Assets.h"
#include "InterstellarPresets.h"
#include <vv/echo.hpp>

namespace vv {
// Settings > Battery Saving Mode: master toggle, profile and the five optimisation switches. Device setting (PowerSettings), not stored in presets. Untested (no JUCE build here).
class BatterySavingPanel : public juce::Component {
public:
    explicit BatterySavingPanel(Store& s) : store(s), cfg(PowerSettings::load()) {
        info.setText("Optimises processing efficiency and reduces unnecessary system activity to improve energy efficiency. Sound, timing and presets are never changed.", juce::dontSendNotification); info.setFont(VVLookAndFeel::mono(11.f)); info.setJustificationType(juce::Justification::topLeft); addAndMakeVisible(info);
        onT.setButtonText("Battery Saving Mode"); addAndMakeVisible(onT); prof.addItem("Maximum Efficiency", 3); prof.addItem("Balanced (recommended)", 2); prof.addItem("Light", 1); addAndMakeVisible(prof);
        const char* names[5] = { "Intelligent Voice Management", "DSP Optimisation", "Effects Processing Optimisation", "Modulation Processing Optimisation", "Visualisation Optimisation" };
        for (int i = 0; i < 5; ++i) { auto* t = opts.add(new juce::ToggleButton(names[i])); addAndMakeVisible(t); t->onClick = [this] { pull(); }; }
        onT.onClick = [this] { pull(); }; prof.onChange = [this] { pull(); }; push(); status.setFont(VVLookAndFeel::mono(10.f)); addAndMakeVisible(status); refreshStatus();
    }
    void resized() override { auto r = getLocalBounds().reduced(4); info.setBounds(r.removeFromTop(48)); auto row = r.removeFromTop(28); onT.setBounds(row.removeFromLeft(190)); prof.setBounds(row.removeFromLeft(210)); r.removeFromTop(4);
        for (auto* t : opts) t->setBounds(r.removeFromTop(24)); status.setBounds(r.removeFromTop(20)); }
private:
    bool setting_ = false;
    void push() { setting_ = true; onT.setToggleState(cfg.on, juce::dontSendNotification); prof.setSelectedId(cfg.profile, juce::dontSendNotification); bool* v[5] = { &cfg.voices, &cfg.dsp, &cfg.fx, &cfg.mod, &cfg.vis };
        for (int i = 0; i < 5; ++i) { opts[i]->setToggleState(*v[i], juce::dontSendNotification); opts[i]->setEnabled(cfg.on); } prof.setEnabled(cfg.on); setting_ = false; }
    void pull() { if (setting_) return; cfg.on = onT.getToggleState(); cfg.profile = prof.getSelectedId() > 0 ? prof.getSelectedId() : 2; bool* v[5] = { &cfg.voices, &cfg.dsp, &cfg.fx, &cfg.mod, &cfg.vis };
        for (int i = 0; i < 5; ++i) *v[i] = opts[i]->getToggleState(); cfg.save(); cfg.apply(store.engine()); push(); refreshStatus(); }
    void refreshStatus() { status.setText(cfg.on ? juce::String("Active: ") + (cfg.profile == 3 ? "Maximum Efficiency" : cfg.profile == 2 ? "Balanced" : "Light") + "; visual refresh /" + juce::String(vv::uiDivider()) : "Off - full quality and refresh rate", juce::dontSendNotification); }
    Store& store; PowerSettings cfg; juce::Label info, status; juce::ToggleButton onT; juce::ComboBox prof; juce::OwnedArray<juce::ToggleButton> opts;
};


// ================================================================= FX RACK
namespace {

struct Kind { const char* type; };
const char* kFxTypes[] = { "reverb", "delay", "saturation", "wavefolder", "spectral", "filter", "granular", "interstellar", "tape", "phaser", "flanger", "resonator", "mbcomp", "glitch", "panning", "chorus", "spread", "echo", "rotary", "amp", "eq", "ir" };

juce::var obj(std::initializer_list<std::pair<const char*, juce::var>> kv) { auto* o = new juce::DynamicObject(); for (auto& p : kv) o->setProperty(p.first, p.second); return juce::var(o); }
juce::var arr(std::initializer_list<juce::var> v) { return juce::var(juce::Array<juce::var>(v)); }

// 3-band compressor presets (same numbers as the web app's MBC_PRESETS)
juce::var mbDefaults(const juce::String& mode) {
    auto band = [](double tHi, double rDn, double tLo, double rUp, double atk, double rel, double gain) { return obj({ { "on", true }, { "tHi", tHi }, { "rDn", rDn }, { "tLo", tLo }, { "rUp", rUp }, { "atk", atk }, { "rel", rel }, { "gain", gain } }); };
    if (mode == "ott") return obj({ { "mode", "ott" }, { "depth", 1 }, { "mix", 1 }, { "inGain", 0 }, { "outGain", -3 }, { "xLow", 130 }, { "xHigh", 2800 },
        { "lo", band(-24, 8, -48, 3, 14, 120, 3) }, { "mi", band(-26, 8, -48, 3, 6, 80, 3) }, { "hi", band(-28, 8, -46, 3, 1.5, 50, 4) } });
    return obj({ { "mode", "normal" }, { "depth", 1 }, { "mix", 1 }, { "inGain", 0 }, { "outGain", 0 }, { "xLow", 150 }, { "xHigh", 2500 },
        { "lo", band(-18, 3, -60, 1, 25, 220, 0) }, { "mi", band(-18, 3, -60, 1, 12, 160, 0) }, { "hi", band(-18, 3, -60, 1, 5, 110, 0) } });
}

juce::var defaultsFor(const juce::String& t) {   // same as the web app's defaultFxParams()
    if (t == "reverb") return obj({ { "algo", "hall" }, { "decay", 0.5 }, { "size", 0.5 }, { "predelay", 0.02 }, { "mix", 0.3 }, { "mod", 0.4 }, { "colour", 0.5 }, { "shimA", 12 }, { "shimB", -12 } });
    if (t == "delay") return obj({ { "mode", "stereo" }, { "time", 0.35 }, { "feedback", 0.35 }, { "mix", 0.25 }, { "sync", false }, { "syncDiv", "1/8" } });
    if (t == "saturation") return obj({ { "type", "normal" }, { "drive", 0.4 }, { "tone", 6000 }, { "mix", 0.5 }, { "multiband", false }, { "bandLowFreq", 300 }, { "bandHighFreq", 3000 }, { "bandLow", 1 }, { "bandMid", 1 }, { "bandHigh", 1 } });
    if (t == "wavefolder") return obj({ { "drive", 3 }, { "symmetry", 0 }, { "mix", 1 }, { "mode", "fold" }, { "pwm", 0.5 } });
    if (t == "spectral") return obj({ { "freeze", 0 }, { "blur", 0 }, { "shift", 0 }, { "threshold", 0 }, { "spread", 0 }, { "tilt", 0 }, { "disperse", 0 }, { "mix", 1 } });
    if (t == "mbcomp") return mbDefaults("normal");
    if (t == "filter") return obj({ { "filterType", "moog4pole" }, { "cutoff", 2000 }, { "resonance", 20 }, { "mix", 1 } });
    if (t == "granular") return obj({ { "grainSize", 80 }, { "density", 20 }, { "pitch", 0 }, { "spread", 0.5 }, { "panCenter", 0 }, { "envShape", 0 }, { "followerAmount", 0 }, { "mix", 1 }, { "delay", 0 }, { "posRand", 1 }, { "sizeRand", 0 }, { "jitter", 0 }, { "fine", 0 }, { "pitchRand", 0 }, { "reverse", 0 }, { "volRand", 0 }, { "width", 1 }, { "drift", 0 }, { "lrSpread", 0 }, { "outLevel", 0 }, { "curve", 0.5 }, { "symmetry", 0.5 }, { "atk", 0 }, { "dec", 0 }, { "sus", 1 }, { "rel", 0 }, { "fb", 0 }, { "fbDamp", 0 }, { "fbLow", 0 }, { "fbSat", 0 }, { "fbDiff", 0 }, { "pingPong", 0 }, { "rndDepth", 1 }, { "rndRate", 2 }, { "seed", 0 }, { "mode", 0 }, { "window", -1 }, { "rndType", 0 }, { "sync", 0 }, { "delaySync", 0 }, { "freeze", 0 }, { "autoGain", 0 } });
    if (t == "interstellar") return juce::JSON::parse(juce::String(vv::isp::kDefaultsJson));
    if (t == "tape") return obj({ { "wowRate", 0.7 }, { "wowDepth", 0.3 }, { "flutterRate", 8 }, { "flutterDepth", 0.2 }, { "dust", 0.2 }, { "crackle", 0.2 }, { "mix", 1 } });
    if (t == "phaser") return obj({ { "rate", 0.4 }, { "depth", 0.85 }, { "feedback", 0.6 }, { "mix", 0.5 }, { "stages", 8 } });
    if (t == "flanger") return obj({ { "rate", 0.25 }, { "depth", 0.5 }, { "feedback", 0.3 }, { "mix", 0.5 } });
    if (t == "resonator") return obj({ { "freq", 440 }, { "resonance", 15 }, { "mix", 0.4 } });
    if (t == "glitch") return obj({ { "bitDepth", 16 }, { "srReduction", 1 }, { "companding", 0 }, { "retroModel", "off" }, { "stutterOn", false }, { "stutterDivision", 0.25 }, { "stutterAmount", 0.5 }, { "mix", 1 },
        { "seqOn", false }, { "seqSteps", arr({ true, true, true, true, true, true }) }, { "seqRate", "1/16" }, { "seqIntensity", 0.3 }, { "seqTempoDiv", "1" } });
    if (t == "panning") {
        auto band = [](double w, double d, double sp) { return obj({ { "on", true }, { "width", w }, { "depth", d }, { "speed", sp }, { "fx", 0 } }); };
        return obj({ { "width", 1 }, { "autoPanOn", false }, { "rate", 1 }, { "shape", "square" }, { "mode", "stereo" }, { "distance", 0.3 }, { "mix", 1 },
            { "mb", obj({ { "on", false }, { "xover", arr({ 200, 1500, 6000 }) }, { "bands", arr({ band(0.4, 0, 0.25), band(0.7, 0.5, 0.4), band(0.9, 0.6, 0.65), band(1, 0.8, 0.9) }) } }) } });
    }
    if (t == "chorus") return obj({ { "chorus1On", true }, { "chorus2On", false }, { "mix", 0.5 } });
    if (t == "echo") return obj({ { "env", 2 }, { "time", 0.42 }, { "sync", false }, { "syncDiv", "1/8" }, { "count", 8 }, { "density", 0.4 }, { "feedback", 0.4 }, { "decay", 0.5 }, { "diffusion", 0.55 },
        { "width", 1.0 }, { "spread", 0.7 }, { "early", 0.6 }, { "late", 0.8 }, { "hfDamp", 0.45 }, { "lfDamp", 0.15 }, { "modDepth", 0.25 }, { "modRate", 0.45 }, { "movement", 0.35 }, { "mix", 0.35 }, { "level", 0 } });
    if (t == "spread") return obj({ { "spread", 1 }, { "mix", 1 } });
    if (t == "eq") return obj({ { "f1", 120 }, { "g1", 0 }, { "f2", 600 }, { "g2", 0 }, { "f3", 2500 }, { "g3", 0 }, { "f4", 8000 }, { "g4", 0 }, { "out", 0 }, { "mix", 1 } });
    if (t == "ir") return obj({ { "irId", "f:hall-concert" }, { "irName", "Concert Hall" }, { "mix", 0.3 }, { "predelay", 0 }, { "lenPct", 100 }, { "earlyLate", 0 }, { "inLevel", 0 }, { "outLevel", 0 }, { "lowCut", 20 }, { "highCut", 20000 },
        { "damping", 0 }, { "tone", 0 }, { "width", 1 }, { "balance", 0 }, { "start", 0 }, { "end", 1 }, { "normalise", true }, { "reverse", false }, { "irGain", 0 } });
    if (t == "rotary") return obj({ { "speed", "slow" }, { "drive", 0.3 }, { "balance", 0.5 }, { "depth", 0.7 }, { "width", 0.8 }, { "accel", 1 }, { "mix", 1 }, { "level", 0 } });
    if (t == "amp") return obj({ { "model", "twin" }, { "gain", 0.4 }, { "bass", 0.5 }, { "mid", 0.5 }, { "treble", 0.5 }, { "presence", 0.4 }, { "master", 0.5 }, { "cab", true }, { "chorus", 0.5 }, { "mix", 1 }, { "level", 0 } });
    return obj({});
}

using KO = KnobOpts;
void stageParams(Panel& p, const juce::String& base, const juce::String& type, Store& s) {
    const juce::String q = base + ".params.";
    auto K = [&](const char* l, const char* k, double lo, double hi, double d, KO o = {}) { p.knob(l, q + k, lo, hi, d, o); };
    if (type == "eq") {
        const double F[4] = { 120, 600, 2500, 8000 };
        for (int b = 1; b <= 4; ++b) { const juce::String n(b); p.knob("Band " + n + " freq", q + "f" + n, 20, 18000, F[b - 1], { "%.0f", " Hz", true, 0 }); p.knob("Band " + n + " gain", q + "g" + n, -15, 15, 0, { "%.1f", " dB", false, 1 }); p.newRow(); }
        K("Output", "out", -24, 24, 0, { "%.1f", " dB", false, 1 }); K("Mix", "mix", 0, 1, 1);
        return;
    }
    if (type == "ir") {
        p.customOwned(new IrPanel(s, base + ".params"), -1, 200); p.newRow();
        K("Wet / dry", "mix", 0, 1, 0.3); K("Pre-delay", "predelay", 0, 500, 0, { "%.0f", " ms", false, 0 }); K("IR length", "lenPct", 1, 100, 100, { "%.0f", " %", false, 0 });
        K("Early / late", "earlyLate", -1, 1, 0); K("Input level", "inLevel", -24, 12, 0, { "%.1f", " dB", false, 1 }); K("Output level", "outLevel", -24, 12, 0, { "%.1f", " dB", false, 1 });
        p.newRow(); K("Low cut", "lowCut", 20, 2000, 20, { "%.0f", " Hz", true, 0 }); K("High cut", "highCut", 1000, 20000, 20000, { "%.0f", " Hz", true, 0 });
        K("Damping", "damping", 0, 1, 0); K("Tone", "tone", -1, 1, 0); K("Stereo width", "width", 0, 2, 1); K("L / R balance", "balance", -1, 1, 0);
        p.newRow(); K("IR start", "start", 0, 0.98, 0, { "%.3f", "", false, 3 }); K("IR end", "end", 0.02, 1, 1, { "%.3f", "", false, 3 }); K("IR gain", "irGain", -24, 24, 0, { "%.1f", " dB", false, 1 });
        p.toggle("Normalise", q + "normalise", true); p.toggle("Reverse", q + "reverse");
        return;
    }
    if (type == "reverb") {
        p.choice("Algorithm", q + "algo", { "room", "spring", "plate", "hall", "plate ambient", "kristal", "flow", "mothership", "formant", "shimmer", "reverse" }, "hall"); p.newRow();
        K("Decay", "decay", 0, 0.97, 0.5); K("Size", "size", 0.1, 2, 0.5); K("Predelay", "predelay", 0, 0.3, 0.02, { "%.3f", " s", false, 3 }); K("Mix", "mix", 0, 1, 0.3);
        K("Modulation", "mod", 0, 1, 0.4); K("Colour", "colour", 0, 1, 0.5);
        if (s.str(q + "algo") == "reverse") { K("Reverse tail", "rvTail", 0, 1, 1); K("Reverse transient", "rvTrans", 0, 1, 0.5); }   // tail: how much of the build-up is heard; transient: soft - classic - sharp
        if (s.str(q + "algo") == "shimmer") { K("Shimmer A", "shimA", -24, 24, 12, { "%.0f", " st", false, 0 }); K("Shimmer B", "shimB", -24, 24, -12, { "%.0f", " st", false, 0 }); }
    } else if (type == "delay") {
        p.choice("Mode", q + "mode", { "mono", "stereo", "pingpong", "reverse" }, "stereo"); p.toggle("Tempo sync", q + "sync");
        if (s.flag(q + "sync")) p.choice("Division", q + "syncDiv", { "1/1", "1/2", "1/4", "1/8", "1/16", "1/4.", "1/8.", "1/4t", "1/8t" }, "1/8");
        else K("Time", "time", 0.01, 1.5, 0.35, { "%.2f", " s", false, 2 });
        K("Feedback", "feedback", 0, 0.9, 0.35); K("Mix", "mix", 0, 1, 0.25);
    } else if (type == "saturation") {
        p.choice("Type", q + "type", { "soft", "normal", "hard", "tube" }, "normal");   /* tube = Tube Overdrive */ K("Drive", "drive", 0, 1, 0.4); K("Tone", "tone", 300, 14000, 6000, { "%.0f", " Hz", true, 0 }); K("Mix", "mix", 0, 1, 0.5);
        p.newRow(); p.toggle("Multiband", q + "multiband");   // 3-band saturation: crossover -> one saturator per band (Drive x band amount) -> blended with Mix
        if (s.flag(q + "multiband")) {
            K("Low / Mid crossover", "bandLowFreq", 60, 2000, 300, { "%.0f", " Hz", true, 0 }); K("Mid / High crossover", "bandHighFreq", 600, 16000, 3000, { "%.0f", " Hz", true, 0 });
            K("Low amount", "bandLow", 0, 1, 1); K("Mid amount", "bandMid", 0, 1, 1); K("High amount", "bandHigh", 0, 1, 1);
        }
    } else if (type == "wavefolder") {
        p.choice("Mode", q + "mode", { "fold", "mirror", "sine", "pwm" }, "fold"); K("Drive", "drive", 1, 20, 3, { "%.1f", "", false, 1 }); K("Symmetry", "symmetry", -1, 1, 0);
        if (s.str(q + "mode") == "pwm") K("PWM width", "pwm", 0.02, 0.98, 0.5); K("Mix", "mix", 0, 1, 1);
    } else if (type == "spectral") {
        K("Freeze", "freeze", 0, 1, 0); K("Blur", "blur", 0, 1, 0); K("Shift", "shift", -200, 200, 0, { "%.0f", "", false, 0 }); K("Threshold", "threshold", 0, 1, 0); K("Spread", "spread", 0, 1, 0);
        K("Tilt", "tilt", -12, 12, 0, { "%.1f", " dB/oct", false, 1 }); K("Dispersion", "disperse", 0, 1, 0); K("Mix", "mix", 0, 1, 1);
    } else if (type == "mbcomp") {
        auto& modeBox = p.choice("Mode", q + "mode", { "normal", "ott" }, "normal");
        // choosing a mode loads that mode's band settings and keeps Input and Mix (web: mbCompApplyMode)
        { auto prev = modeBox.box.onChange; Store* st = &s; const juce::String qq = q;
          modeBox.box.onChange = [prev, st, qq, &modeBox] { if (prev) prev(); const auto d = mbDefaults(modeBox.box.getText()); auto* o = d.getDynamicObject(); if (!o) return;
              std::vector<std::pair<juce::String, juce::var>> kv; for (auto& pr : o->getProperties()) { const auto k = pr.name.toString(); if (k == "mix" || k == "inGain" || k == "mode") continue;
                  if (auto* bo = pr.value.getDynamicObject()) { for (auto& bp : bo->getProperties()) kv.push_back({ qq + k + "." + bp.name.toString(), bp.value }); } else kv.push_back({ qq + k, pr.value }); }
              st->setMany(kv); }; }
        K("Input", "inGain", -24, 24, 0, { "%.1f", " dB", false, 1 }); K("Output", "outGain", -24, 24, 0, { "%.1f", " dB", false, 1 }); K("Depth", "depth", 0, 1, 1); K("Mix", "mix", 0, 1, 1); p.newRow();
        K("Low/Mid xover", "xLow", 40, 1000, 150, { "%.0f", " Hz", true, 0 }); K("Mid/High xover", "xHigh", 1000, 12000, 2500, { "%.0f", " Hz", true, 0 }); p.newRow();
        for (auto* b : { "lo", "mi", "hi" }) {
            const juce::String bq = q + b + ".";
            auto B = [&](const char* l, const char* k, double lo, double hi, double d, KO o = {}) { p.knob(juce::String(b == juce::String("lo") ? "Low " : b == juce::String("mi") ? "Mid " : "High ") + l, bq + k, lo, hi, d, o); };
            p.toggle(juce::String(b == juce::String("lo") ? "Low" : b == juce::String("mi") ? "Mid" : "High") + " on", bq + "on");
            B("high thr", "tHi", -60, 0, -18, { "%.1f", " dB", false, 1 }); B("down ratio", "rDn", 1, 20, 3, { "%.1f", ":1", false, 1 }); B("low thr", "tLo", -80, 0, -60, { "%.1f", " dB", false, 1 });
            B("up ratio", "rUp", 1, 10, 1, { "%.1f", ":1", false, 1 }); B("attack", "atk", 0.1, 200, 12, { "%.1f", " ms", true, 1 }); B("release", "rel", 5, 1500, 160, { "%.0f", " ms", true, 0 }); B("makeup", "gain", -12, 24, 0, { "%.1f", " dB", false, 1 });
            p.newRow();
        }
    } else if (type == "filter") {
        p.choice("Filter type", q + "filterType", kFilterTypes, "moog4pole"); p.choice("Formant table", q + "formantTable", kFormantTables, "classic"); p.choice("Vowel", q + "vowel", kFormantVowels, "a"); p.newRow();
        K("Cutoff", "cutoff", 20, 18000, 2000, { "%.0f", " Hz", true, 0 }); K("Resonance", "resonance", 0, 130, 20, { "%.1f", "", false, 1 }); K("Mix", "mix", 0, 1, 1);
    } else if (type == "granular") {
        K("Grain size", "grainSize", 5, 500, 80, { "%.0f", " ms", true, 0 }); K("Density", "density", 0.5, 100, 20, { "%.1f", "", true, 1 }); K("Pitch", "pitch", -24, 24, 0, { "%.1f", " st", false, 1 });
        K("Panning", "panCenter", -1, 1, 0); K("Spread", "spread", 0, 1, 0.5); K("Env shape", "envShape", 0, 1, 0); K("Follower", "followerAmount", 0, 1, 0); K("Mix", "mix", 0, 1, 1);
        p.newRow(); p.choiceNum("Mode", q + "mode", { { "Classic granular", 0 }, { "Granular delay", 1 }, { "Granular freeze", 2 }, { "Granular reverse", 3 }, { "Granular diffusion", 4 }, { "Granular pitch scatter", 5 }, { "Granular cloud", 6 }, { "Granular glitch", 7 }, { "Granular stutter", 8 }, { "Granular scramble", 9 }, { "Granular time scatter", 10 }, { "Rhythmic granular", 11 }, { "Ambient granular", 12 }, { "Spectral granular", 13 } }, 0); p.choiceNum("Window", q + "window", { { "Classic (envelope shape)", -1 }, { "Hann", 0 }, { "Hamming", 1 }, { "Blackman", 2 }, { "Gaussian", 3 }, { "Triangle", 4 }, { "Rectangular", 5 }, { "Tukey", 6 }, { "Exponential", 7 } }, -1); p.newRow();
        p.choiceNum("Rate sync", q + "sync", { { "Off", 0 }, { "1/1", 1 }, { "1/2", 2 }, { "1/4", 3 }, { "1/8", 4 }, { "1/16", 5 }, { "1/32", 6 }, { "1/4T", 7 }, { "1/8T", 8 }, { "1/16T", 9 } }, 0); p.choiceNum("Delay sync", q + "delaySync", { { "Off", 0 }, { "1/1", 1 }, { "1/2", 2 }, { "1/4", 3 }, { "1/8", 4 }, { "1/16", 5 }, { "1/32", 6 }, { "1/4T", 7 }, { "1/8T", 8 }, { "1/16T", 9 } }, 0); p.choiceNum("Random type", q + "rndType", { { "White", 0 }, { "Pink", 1 }, { "Brown", 2 }, { "Sample and hold", 3 }, { "Smooth random", 4 }, { "Slow evolving", 5 } }, 0); p.newRow();
        p.toggle("Freeze", q + "freeze"); p.toggle("Auto gain", q + "autoGain"); p.newRow();
        K("Delay / position","delay",0,1500,0,{ "%.0f", " ms", true, 0 }); K("Position scatter","posRand",0,8,1,{ "%.2f", "", false, 0 }); K("Size random","sizeRand",0,1,0,{ "%.2f", "", false, 0 }); K("Timing jitter","jitter",0,1,0,{ "%.2f", "", false, 0 }); p.newRow();
        K("Fine tune","fine",-100,100,0,{ "%.0f", " cents", false, 0 }); K("Pitch random","pitchRand",0,12,0,{ "%.1f", " st", false, 0 }); K("Reverse amount","reverse",0,1,0,{ "%.2f", "", false, 0 }); p.newRow();
        K("Volume random","volRand",0,1,0,{ "%.2f", "", false, 0 }); K("Stereo width","width",0,2,1,{ "%.2f", "", false, 0 }); K("Stereo drift","drift",0,1,0,{ "%.2f", "", false, 0 }); K("L/R grain offset","lrSpread",0,80,0,{ "%.1f", " ms", false, 0 }); K("Output level","outLevel",-24,12,0,{ "%.1f", "", false, 0 }); p.newRow();
        K("Curvature","curve",0,1,0.5,{ "%.2f", "", false, 0 }); K("Symmetry","symmetry",0,1,0.5,{ "%.2f", "", false, 0 }); K("Attack","atk",0,0.5,0,{ "%.2f", "", false, 0 }); K("Decay","dec",0,0.5,0,{ "%.2f", "", false, 0 }); K("Sustain","sus",0,1,1,{ "%.2f", "", false, 0 }); K("Release","rel",0,0.5,0,{ "%.2f", "", false, 0 }); p.newRow();
        K("Feedback","fb",0,0.95,0,{ "%.2f", "", false, 0 }); K("Damping","fbDamp",0,1,0,{ "%.2f", "", false, 0 }); K("Low cut","fbLow",0,1,0,{ "%.2f", "", false, 0 }); K("Saturation","fbSat",0,1,0,{ "%.2f", "", false, 0 }); K("Diffusion","fbDiff",0,1,0,{ "%.2f", "", false, 0 }); K("Ping-pong","pingPong",0,1,0,{ "%.2f", "", false, 0 }); p.newRow();
        K("Random depth","rndDepth",0,1,1,{ "%.2f", "", false, 0 }); K("Random rate","rndRate",0.05,20,2,{ "%.2f", " Hz", true, 0 }); K("Random seed","seed",0,999,0,{ "%.0f", "", false, 0 }); p.newRow();
    } else if (type == "interstellar") {
        {   // preset menu: a preset is a full parameter set, loaded over the defaults; capture is bumped so a capture-mode preset grabs fresh audio
            auto* b = new juce::TextButton("PRESETS...");
            b->onClick = [&store = s, q, b] {
                juce::PopupMenu m; int id = 1;
                for (auto& pr : vv::isp::kPresets) m.addItem(id++, juce::String(pr.category) + " - " + pr.name);
                m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(b), [&store, q](int r) {
                    if (r <= 0) return;
                    std::vector<std::pair<juce::String, juce::var>> kv;
                    const int cap = (int) store.get(q + "capture");
                    for (auto* src : { vv::isp::kDefaultsJson, vv::isp::kPresets[r - 1].json }) {
                        const juce::var parsed = juce::JSON::parse(juce::String(src));   // keep the var alive while its object is read
                        if (auto* o = parsed.getDynamicObject())
                            for (auto& pr : o->getProperties()) if (pr.name.toString() != "capture") kv.push_back({ q + pr.name.toString(), pr.value });
                    }
                    kv.push_back({ q + "capture", cap + 1 });
                    store.setMany(kv); });
            };
            p.customOwned(b, 150, 34);
        }
#include "InterstellarUi.inc"
    } else if (type == "tape") {
        K("Wow rate", "wowRate", 0.05, 5, 0.7, { "%.2f", " Hz", false, 2 }); K("Wow depth", "wowDepth", 0, 1, 0.3); K("Flutter rate", "flutterRate", 2, 20, 8, { "%.1f", " Hz", false, 1 });
        K("Flutter depth", "flutterDepth", 0, 1, 0.2); K("Dust", "dust", 0, 1, 0.2); K("Crackle", "crackle", 0, 1, 0.2); K("Mix", "mix", 0, 1, 1);
    } else if (type == "phaser" || type == "flanger") {
        K("Rate", "rate", 0.05, 5, type == "phaser" ? 0.4 : 0.25, { "%.2f", " Hz", false, 2 }); K("Depth", "depth", 0, 1, 0.5); K("Feedback", "feedback", 0, 0.9, 0.4); K("Mix", "mix", 0, 1, 0.5);
        if (type == "phaser") p.stepper("Stages", q + "stages", 2, 12, 8);
    } else if (type == "resonator") {
        K("Frequency", "freq", 40, 2000, 440, { "%.0f", " Hz", true, 0 }); K("Resonance", "resonance", 1, 40, 15, { "%.1f", "", false, 1 }); K("Mix", "mix", 0, 1, 0.4);
    } else if (type == "spread") {
        K("Spread", "spread", 0, 2, 1); K("Mix", "mix", 0, 1, 1);   // L straight through, R Haas-delayed by spread x 10 ms
    } else if (type == "rotary") {
        p.choice("Speed", q + "speed", { "slow", "fast", "stop" }, "slow");   // slow = chorale, fast = tremolo, stop = brake; the horn and drum spin up / coast down with their own inertia
        K("Drive (tube)", "drive", 0, 1, 0.3); K("Horn / drum", "balance", 0, 1, 0.5); K("Depth", "depth", 0, 1, 0.7); K("Mic width", "width", 0, 1, 0.8); p.newRow();
        K("Acceleration", "accel", 0.2, 4, 1, { "%.2f", "", true, 2 }); K("Mix", "mix", 0, 1, 1); K("Level", "level", -24, 12, 0, { "%.1f", " dB", false, 1 });
    } else if (type == "amp") {
        p.choice("Model", q + "model", { "twin", "jc", "small", "brit" }, "twin"); K("Gain", "gain", 0, 1, 0.4); K("Bass", "bass", 0, 1, 0.5); K("Mid", "mid", 0, 1, 0.5); K("Treble", "treble", 0, 1, 0.5); p.newRow();
        K("Presence", "presence", 0, 1, 0.4); K("Master", "master", 0, 1, 0.5); if (s.str(q + "model") == "jc") K("Chorus (JC)", "chorus", 0, 1, 0.5);
        p.toggle("Cabinet", q + "cab", true); K("Mix", "mix", 0, 1, 1); K("Level", "level", -24, 12, 0, { "%.1f", " dB", false, 1 });
    } else if (type == "echo") {
        {   // factory presets (a preset is a full parameter set, loaded over the current one)
            auto* b = new juce::TextButton("ECHO PRESETS...");
            b->onClick = [&store = s, q, b] {
                juce::PopupMenu m; int id = 1;
                for (auto& pr : vv::echo::kPresets) m.addItem(id++, pr.name);
                m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(b), [&store, q](int r) {
                    if (r <= 0) return;
                    std::vector<std::pair<juce::String, juce::var>> kv;
                    for (int i = 0; i < 18; i++) kv.push_back({ q + vv::echo::kPresetKeys[i], vv::echo::kPresets[r - 1].v[i] });
                    store.setMany(kv); });
            };
            p.customOwned(b, 150, 34);
        }
        { std::vector<std::pair<juce::String, double>> envs; for (int i = 0; i < 9; i++) envs.push_back({ vv::echo::kEnvs[i].name, (double) i }); p.choiceNum("Environment", q + "env", envs, 2); }
        p.toggle("Tempo sync", q + "sync");
        if (s.flag(q + "sync")) p.choice("Division", q + "syncDiv", { "1/1", "1/2", "1/4", "1/8", "1/16", "1/4.", "1/8.", "1/4t", "1/8t" }, "1/8");
        else K("Echo time", "time", 0.02, 4, 0.42, { "%.3f", " s", true, 3 });
        p.newRow();
        K("Reflections", "count", 1, 16, 8, { "%.0f", "", false, 0 }); K("Density", "density", 0, 1, 0.4); K("Early", "early", 0, 1, 0.6); K("Late", "late", 0, 1, 0.8); p.newRow();
        K("Feedback", "feedback", 0, 0.95, 0.4); K("Decay", "decay", 0, 1, 0.5); K("Diffusion", "diffusion", 0, 1, 0.55); K("HF damping", "hfDamp", 0, 1, 0.45); K("LF damping", "lfDamp", 0, 1, 0.15); p.newRow();
        K("Width", "width", 0, 1.5, 1.0); K("Spread", "spread", 0, 1, 0.7); K("Mod depth", "modDepth", 0, 1, 0.25); K("Mod rate", "modRate", 0.05, 8, 0.45, { "%.2f", " Hz", true, 2 }); K("Movement", "movement", 0, 1, 0.35); p.newRow();
        K("Mix", "mix", 0, 1, 0.35); K("Level", "level", -24, 12, 0, { "%.1f", " dB", false, 1 });
    } else if (type == "chorus") {
        p.toggle("Chorus 1", q + "chorus1On", true); p.toggle("Chorus 2", q + "chorus2On"); K("Mix", "mix", 0, 1, 0.5);
    } else if (type == "glitch") {
        K("Bit depth", "bitDepth", 1, 16, 16, { "%.0f", "", false, 0 }); K("SR reduction", "srReduction", 1, 50, 1, { "%.0f", "", false, 0 });
        K("Companding", "companding", 0, 1, 0, { "%.2f", "", false, 2 });
        {   // Retro DSP model: one click sets bitDepth / srReduction / companding (SR assumed 48 kHz), values stay editable
            struct M { const char* key; const char* label; int bits; double hz; double comp; };
            static const M models[] = { { "off", "Off (plain bitcrush)", 0, 0, 0 }, { "sp1200", "E-mu SP-1200 (12-bit, 26.04kHz)", 12, 26040, 0.12 },
                                        { "mirage", "Ensoniq Mirage/EPS (8-13-bit)", 8, 32000, 0.4 }, { "fairlight", "Fairlight CMI (8/16-bit companding)", 8, 22000, 0.75 } };
            auto* b = new juce::TextButton("RETRO DSP MODEL");
            b->onClick = [&store = s, q, b] {
                juce::PopupMenu m; int id = 1;
                for (auto& md : models) m.addItem(id++, md.label);
                m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(b), [&store, q](int r) {
                    if (r <= 0) return;
                    const M& md = models[r - 1];
                    std::vector<std::pair<juce::String, juce::var>> kv{ { q + "retroModel", md.key } };
                    if (md.bits > 0) { kv.push_back({ q + "bitDepth", md.bits }); kv.push_back({ q + "srReduction", std::max(1, (int) std::lround(48000.0 / md.hz)) }); kv.push_back({ q + "companding", md.comp }); }
                    store.setMany(kv); });
            };
            p.customOwned(b, 150, 34);
        }
        p.toggle("Stutter", q + "stutterOn"); p.choiceNum("Stutter division", q + "stutterDivision", { { "1", 1 }, { "1/2", 0.5 }, { "1/4", 0.25 }, { "1/8", 0.125 } }, 0.25);
        K("Stutter amount", "stutterAmount", 0, 1, 0.5); K("Mix", "mix", 0, 1, 1); p.newRow();
        p.toggle("Mini glitch sequencer", q + "seqOn");
        for (int k = 0; k < 6; ++k) p.toggle(juce::StringArray{ "1 stutter", "2 bitcrush", "3 reverse", "4 gate", "5 pitch", "6 tape stop" }[k], q + "seqSteps." + juce::String(k), true);
        p.newRow();
        p.choice("Seq rate", q + "seqRate", { "1/4", "1/8", "1/16", "1/8.", "1/8t" }, "1/16"); p.choice("Tempo div", q + "seqTempoDiv", { "1", "1/2", "1/4", "2x" }, "1"); K("Intensity", "seqIntensity", 0, 1, 0.3);
    } else if (type == "panning") {
        K("Width", "width", 0, 2, 1); K("Distance (3D)", "distance", 0, 1, 0.3); p.choice("Mode", q + "mode", { "stereo", "3dspace" }, "stereo");
        p.toggle("Auto-pan", q + "autoPanOn"); K("Rate", "rate", 0.02, 10, 1, { "%.2f", " Hz", true, 2 }); p.choice("Shape", q + "shape", { "square", "triangle", "exponential" }, "square"); K("Mix", "mix", 0, 1, 1);
        p.newRow(); p.toggle("Multiband on", q + "mb.on");
        const double xo[] = { 200, 1500, 6000 }; for (int k = 0; k < 3; ++k) K(("Xover " + juce::String(k + 1)).toRawUTF8(), ("mb.xover." + juce::String(k)).toRawUTF8(), 20, 18000, xo[k], { "%.0f", " Hz", true, 0 });
        juce::Array<std::pair<juce::String, double>> others;   // band FX: patch another stage (1-based chain position) into the band
        std::vector<std::pair<juce::String, double>> opts{ { "None", 0 } };
        for (int i = 0; i < s.count("fxStages"); ++i) { auto t = s.str("fxStages." + juce::String(i) + ".type"); if (t != "panning" && ("fxStages." + juce::String(i)) != base) opts.push_back({ juce::String(i + 1) + ". " + t, (double) (i + 1) }); }
        for (int b = 0; b < 4; ++b) {
            const juce::String bp = q + "mb.bands." + juce::String(b) + ".";
            p.newRow(); p.toggle("Band " + juce::String(b + 1), bp + "on", true);
            p.knob("Width", bp + "width", 0, 1, 0.5); p.knob("Depth", bp + "depth", 0, 1, 0.5); p.knob("Speed", bp + "speed", 0, 1, 0.5);
            p.choiceNum("Band FX", bp + "fx", opts, 0);
        }
    }
}

struct StageHeader : juce::Component {   // web fx-chain-item header: power switch, SEND TO source pills (lit LED = this source feeds the effect), move / edit / remove
    StageHeader(Store& s, int idx, int n, std::function<void()> toggleOpen) : store(s), index(idx) {
        const juce::String base = "fxStages." + juce::String(idx);
        en.setButtonText("On"); en.getProperties().set("pwr", true); en.setTooltip("Switch this effect on or off"); en.setToggleState(s.flag(base + ".enabled", true), juce::dontSendNotification);
        en.onClick = [this, base] { store.set(base + ".enabled", en.getToggleState()); syncLook(); };
        addAndMakeVisible(en);
        static const char* K[] = { "WT", "FM", "SMP", "PRO", "SID", "LUA", "XFX", "VOC" }, *N[] = { "Wavetable", "FM", "Sampler", "Procedural", "SID", "LUA", "XFX mode", "Vocoder" };
        for (int i = 0; i < 8; ++i) {
            auto* b = sends.add(new juce::ToggleButton(K[i])); b->getProperties().set("sendChip", K[i]); b->setTooltip(juce::String("Send ") + N[i] + " to this effect"); b->setTitle(juce::String("Send to ") + N[i]);
            b->setToggleState(s.flag(base + ".sends." + K[i], false), juce::dontSendNotification);
            b->onClick = [this, b, base, name = juce::String(K[i])] { store.set(base + ".sends." + name, b->getToggleState()); syncLook(); }; addAndMakeVisible(b);
        }
        up.setButtonText("^"); down.setButtonText("v"); rm.setButtonText("x"); ed.setButtonText("edit");
        up.setEnabled(idx > 0); down.setEnabled(idx < n - 1);
        up.onClick = [this] { juce::MessageManager::callAsync([&s = store, i = index] { s.move("fxStages", i, i - 1); }); };
        down.onClick = [this] { juce::MessageManager::callAsync([&s = store, i = index] { s.move("fxStages", i, i + 1); }); };
        rm.onClick = [this] { juce::MessageManager::callAsync([&s = store, i = index] { s.remove("fxStages", i); }); };
        ed.onClick = toggleOpen;
        for (auto* b : { &up, &down, &rm, &ed }) addAndMakeVisible(b);
        syncLook();
    }
    bool anySend() const { for (auto* b : sends) if (b->getToggleState()) return true; return false; }
    void syncLook() { for (auto* b : sends) b->setAlpha(en.getToggleState() ? 1.f : 0.45f); repaint(); }   // a switched-off effect dims its sources
    void paint(juce::Graphics& g) override {
        g.setFont(VVLookAndFeel::mono(10.f).boldened()); g.setColour(col::dim); g.drawText("SEND TO", lblR, juce::Justification::centredLeft, false);
        if (en.getToggleState() && !anySend()) { g.setColour(col::amber); g.setFont(VVLookAndFeel::mono(10.5f)); g.drawText("No source selected: this effect is idle", lblR.withX(lblR.getRight() + 4).withWidth(juce::jmax(0, warnRight - lblR.getRight() - 4)), juce::Justification::centredLeft, true); }
    }
    void resized() override {
        auto r = getLocalBounds(); auto top = r.removeFromTop(30); r.removeFromTop(4);
        rm.setBounds(top.removeFromRight(34).reduced(2)); down.setBounds(top.removeFromRight(34).reduced(2)); up.setBounds(top.removeFromRight(34).reduced(2)); ed.setBounds(top.removeFromRight(56).reduced(2));
        en.setBounds(top.removeFromLeft(76)); lblR = top.removeFromLeft(58).withTrimmedLeft(6); warnRight = top.getRight();
        const int gap = 4, w = juce::jmin(80, (r.getWidth() - 7 * gap) / 8); int x = r.getX();
        for (auto* b : sends) { b->setBounds(x, r.getY(), w, juce::jmin(26, r.getHeight())); x += w + gap; }
    }
    Store& store; int index; juce::ToggleButton en; juce::OwnedArray<juce::ToggleButton> sends; juce::TextButton up, down, rm, ed; juce::Rectangle<int> lblR; int warnRight = 0;
};

} // namespace

class FxScreen : public Page, private Store::Listener {
public:
    explicit FxScreen(Store& s) : Page(s) { s.addListener(this); build(); }
    ~FxScreen() override { store.removeListener(this); }
    void storeChanged() override { if (signature() != sig_) build(); }
private:
    juce::String signature() const {
        juce::String sg; const int n = store.count("fxStages");
        for (int i = 0; i < n; ++i) { auto b = "fxStages." + juce::String(i); sg << store.str(b + ".type") << ',';
            if (store.str(b + ".type") == "reverb") sg << store.str(b + ".params.algo") << ',';
            if (store.str(b + ".type") == "delay") sg << (store.flag(b + ".params.sync") ? "s" : "u") << ',';
            if (store.str(b + ".type") == "wavefolder") sg << store.str(b + ".params.mode") << ',';
            if (gameOpen.count(i)) sg << "G,"; }
        return sg;
    }
    void build() {
        const auto keepScroll = view.getViewPositionY();
        clear(); headers.clear(); gameBtns.clear(); bridges.clear(); warps.clear(); sig_ = signature(); const int n = store.count("fxStages");
        if (n == 0) { auto& e = panel("Effects rack"); note.setText("No effects yet. Use Add effect below.", juce::dontSendNotification); note.setColour(juce::Label::textColourId, col::dim); e.custom(&note, -1, 24); }
        for (int i = 0; i < n; ++i) {
            const juce::String base = "fxStages." + juce::String(i), type = store.str(base + ".type");
            auto& p = panel(juce::String(i + 1) + ". " + type);
            headers.add(new StageHeader(store, i, n, [] {})); p.custom(headers.getLast(), -1, 66); p.newRow();
            stageParams(p, base, type, store);
            if (type == "granular") {
                auto* tb = gameBtns.add(new juce::TextButton(gameOpen.count(i) ? "ANIMATION: ON" : "ANIMATION: OFF")); tb->setClickingTogglesState(false);
                tb->onClick = [this, i] { juce::MessageManager::callAsync([this, i] { if (gameOpen.count(i)) gameOpen.erase(i); else gameOpen.insert(i); build(); }); };
                p.newRow(); p.custom(tb, 180, 30);
                if (gameOpen.count(i)) { auto* v = bridges.add(new GrainBridgeView(store, base)); p.newRow(); p.custom(v, -1, 440); }
            }
            if (type == "wavefolder") {   // Warp Chronology Convergence is the Wavefolder's animation
                auto* tb = gameBtns.add(new juce::TextButton(gameOpen.count(i) ? "ANIMATION: ON" : "ANIMATION: OFF")); tb->setClickingTogglesState(false);
                tb->onClick = [this, i] { juce::MessageManager::callAsync([this, i] { if (gameOpen.count(i)) gameOpen.erase(i); else gameOpen.insert(i); build(); }); };
                p.newRow(); p.custom(tb, 180, 30);
                if (gameOpen.count(i)) { auto* v = warps.add(new WarpView(store, base)); p.newRow(); p.custom(v, -1, 420); }
            }
        }
        auto& a = panel("Add effect"); addB.setButtonText(n >= 10 ? "Chain full (10 max)" : "+ Add effect"); addB.setEnabled(n < 10);
        addB.onClick = [this] { juce::PopupMenu m; int id = 1; for (auto* t : kFxTypes) m.addItem(id++, juce::String(t));
            m.showMenuAsync({}, [this](int r) { if (r > 0) { juce::String t(kFxTypes[r - 1]); store.append("fxStages", obj({ { "type", t }, { "enabled", true }, { "params", defaultsFor(t) }, { "sends", obj({ { "WT", true }, { "FM", true }, { "SMP", true }, { "PRO", true } }) } })); } }); };
        a.custom(&addB, 180, 34); a.choice("Oversampling (saturation / wavefolder)", "oversample", { "normal", "2x", "4x" }, "normal");
        layoutContent(); view.setViewPosition(0, keepScroll);
    }
    juce::String sig_; std::set<int> gameOpen; juce::OwnedArray<juce::TextButton> gameBtns; juce::OwnedArray<GrainBridgeView> bridges; juce::OwnedArray<WarpView> warps; juce::OwnedArray<StageHeader> headers; juce::TextButton addB; juce::Label note;
};

// ================================================================= SETTINGS
class SettingsScreen : public Page {
public:
    explicit SettingsScreen(Store& s) : Page(s) {
        auto& v = panel("Voices");
        v.stepper("Max polyphony", "maxPolyphony", 1, 64, 64); v.toggle("Legato (mono)", "legatoMode");
        panic.setButtonText("Panic (all notes off)"); panic.onClick = [this] { if (store.engine()) vv_engine_all_notes_off(store.engine()); }; v.custom(&panic, 200, 34);
        auto& t = panel("Tuning and tempo");
        t.knob("A4 reference", "a4Reference", 415, 466, 440, { "%.1f", " Hz", false, 1 }); t.knob("Tempo (no host)", "bpm", 20, 300, 120, { "%.0f", " BPM", false, 0 });
        auto& e = panel("Expression");
        e.toggle("MPE (per-note bend / slide / pressure, ch 2-16)", "@mpe"); e.knob("Bend range", "@bendRange", 1, 48, 2, { "%.0f", " st", false, 0 });
        auto& bsm = panel("Battery Saving Mode"); bsm.customOwned(new BatterySavingPanel(store), 420, 230);
        auto& dv = panel("Device");
        dv.toggle("Background audio (keep playing when minimised / switching apps)", "@bgAudio");
        dv.toggle("Do not sleep (stop the screen sleeping / locking)", "@noSleep");
        dv.stepper("App MIDI channel (0 = Omni, 1-16; ignored with MPE)", "@midiChannel", 0, 16, 0);
        // (the master limiter + loudness meter moved to the right-hand side of the Mixer, before the master out)
        auto& m = panel("Mix");
        m.knob("WT / FM mix", "@wtFmMix", 0, 1, 0.5); m.knob("Master tone", "@masterTone", 0, 1, 0); m.toggle("Sample mix morph", "@sampleMixOn"); m.knob("Sample mix pos", "@sampleMix", 0, 15, 0, { "%.1f", "", false, 1 });
        auto& mp = panel("MIDI learn profiles and CC map"); midiPanel = std::make_unique<midi::MidiPanel>(store); mp.custom(midiPanel.get(), 900, 215);
        auto& ap = panel("Appearance"); appearance = std::make_unique<look::AppearanceControls>(store); ap.custom(appearance.get(), 440, 150);
        auto& o = panel("Output"); o.choice("Oversampling", "oversample", { "normal", "2x", "4x" }, "normal");
        auto& a = panel("About"); about.setText("Vektur Vortex - Hybrid Synthesiser. Wavetable, FM, Sampler and Procedural layers, native C++ DSP.\n\nSend to Vektur Sonic (PLAY > Scope): splits the output into discrete Left and Right channels for Vektur Sonic, a fully comprehensive oscilloscope music production synthesiser. In a DAW such as AUM load Vektur Sonic as an insert effect after Vektur Vortex so it receives both channels. Vektur Vortex warns when the signal is mono or silent, because Vektur Sonic then has only one usable signal.\n\nFX games: the Wavefolder and Spectral FX each have an ANIMATION switch that opens a space shoot-em-up wired to the effect parameters.", juce::dontSendNotification);
        about.setColour(juce::Label::textColourId, col::dim); a.custom(&about, -1, 190);
        // the hidden Easter egg: deliberately small and dim, at the very bottom of Settings (web: vvEasterEggBtn)
        egg.setButtonText("EASTER EGG"); egg.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack); egg.setColour(juce::TextButton::textColourOffId, col::dim.withAlpha(0.55f));
        egg.setAlpha(0.7f); egg.onClick = [this] { VoidView::openWindow(this); }; a.custom(&egg, 110, 24);
    }
private:
    ~SettingsScreen() override = default;
    juce::TextButton panic, egg; juce::Label about; std::unique_ptr<look::AppearanceControls> appearance; std::unique_ptr<midi::MidiPanel> midiPanel;
};


// Preset browser model: artist bank -> category. Factory files are "Artist - Category - Name.json" (or plain names, filed under
// "Factory"); user files live in <user>/<Artist>/[<Category>/]<name>.json, and loose files in <user> belong to "My presets".
const juce::StringArray kPresetCats{ "Arp", "Bass", "Key", "Lead", "Pad", "Drum", "FX", "Seq", "Plucked", "Misc" };
struct PresetEntry { juce::String artist, cat, name; juce::File file; bool factory = false; };
juce::String inferCategory(const juce::String& s) {
    for (auto& c : kPresetCats) if (s.equalsIgnoreCase(c)) return c;
    const auto w = s.upToFirstOccurrenceOf(" ", false, false); for (auto& c : kPresetCats) if (w.equalsIgnoreCase(c)) return c;
    return {};
}
juce::Array<PresetEntry> scanPresets(const juce::File& userDir) {
    juce::Array<PresetEntry> out;
    for (auto& f : assets::root().getChildFile("Presets").findChildFiles(juce::File::findFiles, false, "*.json")) {
        auto n = f.getFileNameWithoutExtension(); juce::StringArray parts; { auto rest = n; for (int k; (k = rest.indexOf(" - ")) >= 0 && parts.size() < 2;) { parts.add(rest.substring(0, k)); rest = rest.substring(k + 3); } parts.add(rest); } PresetEntry e; e.file = f; e.factory = true; e.artist = "Factory"; e.cat = "Misc"; e.name = n;
        if (parts.size() >= 3 && kPresetCats.contains(parts[1])) { e.artist = parts[0]; e.cat = parts[1]; e.name = parts[2]; }
        else if (auto c = inferCategory(n); c.isNotEmpty()) e.cat = c;
        out.add(e);
    }
    for (auto& f : userDir.findChildFiles(juce::File::findFiles, true, "*.json")) {
        PresetEntry e; e.file = f; e.name = f.getFileNameWithoutExtension().replace(".vvpreset", ""); auto rel = f.getRelativePathFrom(userDir).replaceCharacter('\\', '/'); auto seg = juce::StringArray::fromTokens(rel, "/", "");
        e.artist = seg.size() >= 2 ? seg[0] : juce::String("My presets"); e.cat = seg.size() >= 3 ? seg[1] : inferCategory(e.name); if (e.cat.isEmpty()) e.cat = "Misc";
        out.add(e);
    }
    std::sort(out.begin(), out.end(), [](const PresetEntry& a, const PresetEntry& b) { return a.name.compareIgnoreCase(b.name) < 0; });
    return out;
}

// ================================================================= PRESETS
class PresetsScreen : public juce::Component, private juce::ListBoxModel {
public:
    explicit PresetsScreen(Store& s, juce::File userDir) : store(s), user(userDir) {
        list.setModel(this); list.setRowHeight(30); addAndMakeVisible(list);
        search.setTextToShowWhenEmpty("search presets", col::dim); search.onTextChange = [this] { refresh(); }; addAndMakeVisible(search);
        auto mk = [this](juce::TextButton& b, const char* t, std::function<void()> f) { b.setButtonText(t); b.onClick = std::move(f); addAndMakeVisible(b); };
        mk(init, "Initialise", [this] { loadDefault(); }); mk(save, "Save as...", [this] { saveAs(); });
        mk(imp, "Import .json", [this] { importFile(); }); mk(exp, "Export .json", [this] { exportFile(); });
        for (auto* c : { "Arp", "Bass", "Key", "Lead", "Pad", "Drum", "FX", "XFX" }) genCat.addItem(c, genCat.getNumItems() + 1); genCat.setSelectedId(1); addAndMakeVisible(genCat);
        mk(gen, "GENERATE", [this] { const auto n = store.generatePreset(genCat.getText(), user); status.setText(n.isNotEmpty() ? "Generated " + n : "Generate failed", juce::dontSendNotification); current = n; refresh(); });
        mk(modify, "MODIFY (~10%)", [this] { const auto n = store.modifyPreset(user); status.setText(n.isNotEmpty() ? "Saved " + n : "Modify failed", juce::dontSendNotification); current = n; refresh(); });
        artistBox.onChange = [this] { artistSel = artistBox.getSelectedId() == 1 ? juce::String() : artistBox.getText(); refresh(); }; addAndMakeVisible(artistBox);
        mk(newArtist, "NEW ARTIST FOLDER", [this] { promptName("New artist folder", "Name:", {}, [this](const juce::String& n) { user.getChildFile(n).createDirectory(); artistSel = n; refresh(); }); });
        mk(renArtist, "RENAME", [this] { if (!userArtist()) return; promptName("Rename artist folder", "New name:", artistSel, [this](const juce::String& n) { if (user.getChildFile(artistSel).moveFileTo(user.getChildFile(n))) { artistSel = n; refresh(); } }); });
        mk(delArtist, "DELETE ARTIST BANK", [this] { if (!userArtist()) return;
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Delete artist bank", "Delete '" + artistSel + "' and every preset in it? This cannot be undone.", "Delete", "Cancel", this,
                juce::ModalCallbackFunction::create([this](int r) { if (r == 1) { user.getChildFile(artistSel).deleteRecursively(); artistSel = {}; refresh(); } })); });
        mk(expBank, "EXPORT BANK (.zip)", [this] { exportBank(); });
        for (auto* c : { "All", "Arp", "Bass", "Key", "Lead", "Pad", "Drum", "FX", "Seq", "Plucked", "Misc" }) { auto* b = catBtns.add(new juce::TextButton(c)); b->setClickingTogglesState(true); b->setRadioGroupId(77);
            b->onClick = [this, c] { catSel = juce::String(c) == "All" ? juce::String() : juce::String(c); refresh(); }; addAndMakeVisible(b); }
        catBtns[0]->setToggleState(true, juce::dontSendNotification);
        importDest.setTooltip("Import destination: artist bank"); addAndMakeVisible(importDest);
        status.setColour(juce::Label::textColourId, col::dim); addAndMakeVisible(status);
        notesLabel.setText("Preset notes (saved with the preset - use Save as... to keep changes)", juce::dontSendNotification); notesLabel.setColour(juce::Label::textColourId, col::dim); addAndMakeVisible(notesLabel);
        notesBox.setMultiLine(true, true); notesBox.setReturnKeyStartsNewLine(true); notesBox.setTextToShowWhenEmpty("How to use this preset: macros, XY pads, tempo, tips...", col::dim);
        notesBox.onTextChange = [this] { store.setNotes(notesBox.getText()); }; addAndMakeVisible(notesBox);
        notesBox.setText(store.notes(), juce::dontSendNotification);
        refresh();
    }
    void resized() override {
        auto r = getLocalBounds().reduced(8); auto bar = r.removeFromTop(34);
        for (auto* b : { &init, &save, &imp, &exp }) b->setBounds(bar.removeFromLeft(130).reduced(2));
        { auto bar2 = r.removeFromTop(34); genCat.setBounds(bar2.removeFromLeft(90).reduced(2)); gen.setBounds(bar2.removeFromLeft(130).reduced(2)); modify.setBounds(bar2.removeFromLeft(150).reduced(2)); }
        { auto b3 = r.removeFromTop(34); artistBox.setBounds(b3.removeFromLeft(170).reduced(2)); for (auto* b : { &newArtist, &renArtist, &delArtist, &expBank }) b->setBounds(b3.removeFromLeft(b == &renArtist ? 90 : 160).reduced(2));
          importDest.setBounds(b3.removeFromLeft(170).reduced(2)); }
        { auto b4 = r.removeFromTop(30); const int w = juce::jmin(80, b4.getWidth() / catBtns.size()); for (auto* b : catBtns) b->setBounds(b4.removeFromLeft(w).reduced(2)); }
        search.setBounds(r.removeFromTop(30).reduced(2)); status.setBounds(r.removeFromBottom(22)); notesBox.setBounds(r.removeFromBottom(90).reduced(2)); notesLabel.setBounds(r.removeFromBottom(20)); list.setBounds(r);
    }
    void visibilityChanged() override { if (isVisible()) { current = store.presetName(); refresh(); notesBox.setText(store.notes(), juce::dontSendNotification); } }
private:
    struct Row { juce::String name; juce::File file; bool header; juce::String artist; };
    int getNumRows() override { return rows.size(); }
    void paintListBoxItem(int i, juce::Graphics& g, int w, int h, bool sel) override {
        if (i >= rows.size()) return; auto& r = rows.getReference(i);
        if (r.header) { g.setColour(col::cyan); g.setFont(VVLookAndFeel::mono(12.f).boldened()); g.drawText(r.name.toUpperCase(), 8, 0, w - 16, h, juce::Justification::centredLeft); return; }
        if (sel) { g.setColour(col::cyanDim); g.fillRoundedRectangle(2.f, 1.f, (float) w - 4, (float) h - 2, 4.f); }
        g.setColour(col::ink); g.setFont(VVLookAndFeel::mono(13.f)); g.drawText(r.name + (r.name == current ? "   *" : ""), 12, 0, w - 24, h, juce::Justification::centredLeft);
        if (r.artist.isNotEmpty()) { g.setColour(col::dim); g.setFont(VVLookAndFeel::mono(11.f)); g.drawText(r.artist, 12, 0, w - 24, h, juce::Justification::centredRight); }
    }
    void listBoxItemClicked(int i, const juce::MouseEvent& e) override {
        if (i >= rows.size() || rows[i].header) return;
        if (e.mods.isPopupMenu() && rows[i].file.isAChildOf(user)) { juce::PopupMenu m; m.addItem(1, "Delete");
            m.showMenuAsync({}, [this, f = rows[i].file](int r) { if (r == 1) { f.deleteFile(); refresh(); } }); return; }
        loadFile(rows[i].file, rows[i].name);
    }
    void refresh() {
        rows.clear(); const auto q = search.getText(); entries = scanPresets(user);
        juce::StringArray artists; for (auto& e : entries) artists.addIfNotAlreadyThere(e.artist);
        for (auto& d : user.findChildFiles(juce::File::findDirectories, false)) artists.addIfNotAlreadyThere(d.getFileName());
        artists.sort(true);
        artistBox.clear(juce::dontSendNotification); artistBox.addItem("All artist banks", 1); importDest.clear(juce::dontSendNotification); importDest.addItem("Import into: My presets", 1);
        int id = 2, sel = 1; for (auto& a : artists) { artistBox.addItem(a, id); if (a == artistSel) sel = id; if (a != "Factory") importDest.addItem("Import into: " + a, id); ++id; }
        artistBox.setSelectedId(sel, juce::dontSendNotification); if (importDest.getSelectedId() == 0) importDest.setSelectedId(1, juce::dontSendNotification);
        for (auto& c : kPresetCats) { juce::Array<Row> part;
            for (auto& e : entries) if (e.cat == c && (catSel.isEmpty() || catSel == c) && (artistSel.isEmpty() || artistSel == e.artist) && (q.isEmpty() || e.name.containsIgnoreCase(q)))
                part.add({ e.name, e.file, false, artistSel.isEmpty() ? e.artist : juce::String() });
            if (part.isEmpty()) continue; rows.add({ c, {}, true, {} }); rows.addArray(part); }
        list.updateContent(); list.repaint();
    }
    bool userArtist() { if (artistSel.isEmpty() || artistSel == "Factory" || artistSel == "My presets" || !user.getChildFile(artistSel).isDirectory()) { status.setText("Pick one of your own artist folders first.", juce::dontSendNotification); return false; } return true; }
    void promptName(const juce::String& title, const juce::String& msg, const juce::String& def, std::function<void(const juce::String&)> done) {
        auto* w = new juce::AlertWindow(title, msg, juce::MessageBoxIconType::NoIcon); w->addTextEditor("n", def);
        w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([w, done](int r) { auto n = w->getTextEditorContents("n").trim().replaceCharacters("/\\:", "---"); if (r == 1 && n.isNotEmpty()) done(n); delete w; }), false);
    }
    juce::File destDir() { const auto t = importDest.getSelectedId() <= 1 ? juce::String() : importDest.getText().fromFirstOccurrenceOf("Import into: ", false, false); return t.isEmpty() ? user : user.getChildFile(t); }
    void exportBank() {
        const auto art = artistSel.isEmpty() ? juce::String("All presets") : artistSel;
        chooser = std::make_unique<juce::FileChooser>("Export bank as .zip", user.getChildFile(art + ".zip"), "*.zip");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this, art](const juce::FileChooser& fc) {
            auto f = fc.getResult(); if (f == juce::File()) return; juce::ZipFile::Builder zb; int n = 0;
            for (auto& e : entries) if (artistSel.isEmpty() || e.artist == artistSel) { zb.addFile(e.file, 9, e.artist + "/" + e.cat + "/" + e.name + ".json"); ++n; }
            juce::FileOutputStream os(f); os.setPosition(0); os.truncate(); if (os.openedOk() && zb.writeToStream(os, nullptr)) status.setText("Exported " + juce::String(n) + " presets to " + f.getFileName(), juce::dontSendNotification); else status.setText("Export failed", juce::dontSendNotification); });
    }
    void loadFile(const juce::File& f, const juce::String& name) {
        if (store.loadJSON(f.loadFileAsString())) { notesBox.setText(store.notes(), juce::dontSendNotification); current = name; store.setPresetName(current); status.setText("Loaded " + name, juce::dontSendNotification); } else status.setText("Could not load " + name, juce::dontSendNotification);
        list.repaint();
    }
    void loadDefault() { if (store.loadJSON(assets::root().getChildFile("default_preset.json").loadFileAsString())) { notesBox.setText(store.notes(), juce::dontSendNotification); current = {}; store.setPresetName(current); status.setText("Initialised.", juce::dontSendNotification); list.repaint(); } }
    void saveAs() {
        auto* w = new juce::AlertWindow("Save preset", "Name, artist bank and category:", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor("name", current); w->addTextEditor("artist", artistSel == "Factory" ? juce::String() : artistSel); w->getTextEditor("artist")->setTextToShowWhenEmpty("artist bank (blank = My presets)", col::dim);
        w->addComboBox("cat", kPresetCats, "Category"); w->getComboBoxComponent("cat")->setSelectedId(juce::jmax(1, kPresetCats.indexOf(catSel.isEmpty() ? inferCategory(current) : catSel) + 1), juce::dontSendNotification);
        w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
            if (r == 1) { const auto bad = "/\\:"; auto n = w->getTextEditorContents("name").trim().replaceCharacters(bad, "---"); auto a = w->getTextEditorContents("artist").trim().replaceCharacters(bad, "---");
                if (n.isNotEmpty()) { auto dir = a.isEmpty() ? user : user.getChildFile(a).getChildFile(w->getComboBoxComponent("cat")->getText()); dir.createDirectory();
                    if (dir.getChildFile(n + ".json").replaceWithText(store.exportPresetFile())) { current = n; store.setPresetName(current); status.setText("Saved " + n + (a.isEmpty() ? juce::String() : " to " + a), juce::dontSendNotification); refresh(); } } }
            delete w; }), false);
    }
    void importFile() {
        chooser = std::make_unique<juce::FileChooser>("Import a preset (.json / .vvpreset.json) exported from the web app", juce::File(), "*.json");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult(); if (!f.existsAsFile()) return; auto text = f.loadFileAsString();
            if (store.loadJSON(text)) { notesBox.setText(store.notes(), juce::dontSendNotification); destDir().createDirectory(); destDir().getChildFile(f.getFileName()).replaceWithText(text); current = f.getFileNameWithoutExtension().replace(".vvpreset", ""); store.setPresetName(current); status.setText("Imported " + current, juce::dontSendNotification); refresh(); }
            else status.setText("That file is not a valid Vektur Vortex preset.", juce::dontSendNotification); });
    }
    void exportFile() {
        chooser = std::make_unique<juce::FileChooser>("Export the current sound", user.getChildFile((current.isEmpty() ? "Vektur Vortex preset" : current) + ".vvpreset.json"), "*.json");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult(); if (f != juce::File()) { f.replaceWithText(store.exportSinglePresetFile(f.getFileNameWithoutExtension().replace(".vvpreset", ""))); status.setText("Exported " + f.getFileName(), juce::dontSendNotification); } });
    }
    Store& store; juce::File user; juce::Array<Row> rows; juce::ListBox list; juce::TextEditor search; juce::TextButton init, save, imp, exp, gen, modify; juce::ComboBox genCat, artistBox, importDest; juce::TextButton newArtist, renArtist, delArtist, expBank; juce::OwnedArray<juce::TextButton> catBtns; juce::Array<PresetEntry> entries; juce::String artistSel, catSel; juce::Label status; juce::String current; juce::TextEditor notesBox; juce::Label notesLabel;
    std::unique_ptr<juce::FileChooser> chooser;
};

juce::Array<juce::File> allPresetFiles(const juce::File& userDir) {
    juce::Array<juce::File> out;
    auto add = [&](juce::File dir) { auto a = dir.findChildFiles(juce::File::findFiles, false, "*.json"); std::sort(a.begin(), a.end(), [](const juce::File& x, const juce::File& y) { return x.getFileName().compareIgnoreCase(y.getFileName()) < 0; }); out.addArray(a); };
    add(assets::root().getChildFile("Presets")); add(userDir); return out;
}
// Preset tree for the top bar. Factory = bundled presets grouped by artist bank (plain-named factory files go under "Factory"), User Presets = loose files and
// the "My presets" bank, Banks = every other user bank. Untested (no JUCE build here).
void showPresetTree(juce::Component& target, Store& store, const juce::File& userDir, std::function<void(const juce::String&)> onLoaded) {
    auto entries = std::make_shared<juce::Array<PresetEntry>>(scanPresets(userDir));
    const auto cur = store.presetName();
    juce::PopupMenu root;
    auto bankMenu = [&](const juce::String& bank, bool factory, bool userBank) {
        juce::PopupMenu bm; int total = 0;
        for (auto& cat : kPresetCats) {
            juce::PopupMenu cm; int n = 0;
            for (int i = 0; i < entries->size(); ++i) { auto& e = entries->getReference(i); const juce::String ea = e.artist.isEmpty() ? juce::String(e.factory ? "Factory" : "My presets") : e.artist;
                if (e.factory != factory || ea != bank || e.cat != cat) continue; cm.addItem(i + 1, e.name, true, e.name == cur); ++n; }
            if (n) { bm.addSubMenu(cat + " (" + juce::String(n) + ")", cm); total += n; }
        }
        (void) userBank; return std::make_pair(bm, total);
    };
    juce::StringArray fb, ub;
    for (auto& e : *entries) { const juce::String a = e.artist.isEmpty() ? juce::String(e.factory ? "Factory" : "My presets") : e.artist; if (e.factory) fb.addIfNotAlreadyThere(a); else ub.addIfNotAlreadyThere(a); }
    fb.sort(true); ub.sort(true);
    { juce::PopupMenu f; for (auto& b : fb) { auto r = bankMenu(b, true, false); if (r.second) f.addSubMenu(b + " (" + juce::String(r.second) + ")", r.first); } if (fb.size()) root.addSubMenu("Factory", f); }
    { auto r = bankMenu("My presets", false, true); if (r.second) { juce::PopupMenu u; for (auto& cat : kPresetCats) { juce::PopupMenu cm; int n = 0; for (int i = 0; i < entries->size(); ++i) { auto& e = entries->getReference(i); if (!e.factory && (e.artist.isEmpty() ? juce::String("My presets") : e.artist) == "My presets" && e.cat == cat) { cm.addItem(i + 1, e.name, true, e.name == cur); ++n; } } if (n) u.addSubMenu(cat + " (" + juce::String(n) + ")", cm); } root.addSubMenu("User Presets", u); } }
    { juce::PopupMenu o; int n = 0; for (auto& b : ub) { if (b == "My presets") continue; auto r = bankMenu(b, false, false); if (r.second) { o.addSubMenu(b + " (" + juce::String(r.second) + ")", r.first); ++n; } } if (n) root.addSubMenu("Banks", o); }
    if (root.getNumItems() == 0) root.addItem(-1, "No presets yet", false);
    root.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target).withMinimumWidth(260), [entries, &store, onLoaded](int r) {
        if (r <= 0 || r > entries->size()) return; auto& e = entries->getReference(r - 1);
        if (store.loadJSON(e.file.loadFileAsString())) { store.setPresetName(e.name); if (onLoaded) onLoaded(e.name); } });
}
// factory functions used by the editor (keeps the class definitions in this file)
juce::Component* makeFxScreen(Store& s) { return new FxScreen(s); }
juce::Component* makeSettingsScreen(Store& s) { return new SettingsScreen(s); }
juce::Component* makePresetsScreen(Store& s, const juce::File& userDir) { return new PresetsScreen(s, userDir); }

} // namespace vv
