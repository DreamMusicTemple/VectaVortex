#include "XYPads.h"
#include <cmath>
#include <deque>
#include <map>
#include <vv/mod_defs.hpp>

namespace vv {
namespace {

struct Dest { const char* id; const char* label; const char* group; };
const Dest kBaseDests[] = {
    { "cutoff", "Filter Cutoff (layer)", "Filter" }, { "reso", "Filter Resonance (layer)", "Filter" },
    { "lfo1speed", "LFO 1 Speed", "LFO" }, { "lfo2speed", "LFO 2 Speed", "LFO" }, { "lfo3speed", "LFO 3 Speed", "LFO" }, { "lfo4speed", "LFO 4 Speed", "LFO" },
    { "wtposition", "Wavetable Position (all)", "Wavetable" }, { "wtsegment", "Wavetable SEG (all)", "Wavetable" }, { "wtoctave", "Wavetable Octave (all)", "Wavetable" },
    { "wtsemi", "Wavetable Semitone (all)", "Wavetable" }, { "wtfine", "Wavetable Fine (all)", "Wavetable" },
    { "smpposition", "Sample Position (all)", "Sampler" }, { "samplemix", "Sample Mixer", "Sampler" },
    { "fmratio", "FM Ratio (all operators)", "FM" }, { "fmop1ratio", "FM Engine A Op 1 Ratio", "FM" }, { "fmop2ratio", "FM Engine A Op 2 Ratio", "FM" }, { "fmop3ratio", "FM Engine A Op 3 Ratio", "FM" },
    { "fmop4ratio", "FM Engine A Op 4 Ratio", "FM" }, { "fmop5ratio", "FM Engine B Op 1 Ratio", "FM" }, { "fmop6ratio", "FM Engine B Op 2 Ratio", "FM" }, { "fmop7ratio", "FM Engine B Op 3 Ratio", "FM" },
    { "fmop8ratio", "FM Engine B Op 4 Ratio", "FM" }, { "fmoctave", "FM Octave (all)", "FM" }, { "fmsemi", "FM Semitone (all)", "FM" }, { "fmfine", "FM Fine (all)", "FM" },
    { "xmod1", "Cross-Mod Slot 1 Amount", "Cross-mod" }, { "xmod2", "Cross-Mod Slot 2 Amount", "Cross-mod" }, { "xmod3", "Cross-Mod Slot 3 Amount", "Cross-mod" }, { "xmod4", "Cross-Mod Slot 4 Amount", "Cross-mod" },
    { "reverbmix", "Reverb Mix", "Effects" }, { "satmix", "Saturation Mix", "Effects" }, { "delayfeedback", "Delay Feedback (all delays)", "Effects" }, { "delaymix", "Delay Mix", "Effects" },
    { "grainsize", "Granular grain size", "Effects" }, { "grainpitch", "Granular pitch", "Effects" }, { "grainpan", "Granular panning", "Effects" }, { "graindensity", "Granular density", "Effects" },
    { "glitchbitdepth", "Glitch Bit Depth (all)", "Effects" }, { "glitchsr", "Glitch Sample Rate (all)", "Effects" }, { "glitchstutterrate", "Glitch Stutter Rate (all)", "Effects" }, { "glitchintensity", "Glitch Intensity (all)", "Effects" },
    { "macro1", "Macro 1 amount", "Global" }, { "macro2", "Macro 2 amount", "Global" }, { "macro3", "Macro 3 amount", "Global" }, { "macro4", "Macro 4 amount", "Global" },
    { "wtfmmix", "WT-FM Mixer", "Global" }, { "mastertone", "Master Tone", "Global" }, { "arprate", "Arp Rate", "Global" }, { "pitch", "Pitch (choose oscillator)", "Global" },
};

// ---- Round 25: layer EQ bands, SID, LUA, vocoder, Phaser, Flanger, Resonator, Convolution Reverb, Echo, Stereo Spread, Interstellar (same set as the web app's XY list)
struct Spec { juce::String path; double mn, mx, step; bool exp; bool fxType; };   // fxType: path holds "<effect type>.<key>" and is written to every stage of that type
struct DestList {
    std::deque<juce::String> own;                    // owns the generated id / label strings
    std::vector<Dest> list; std::map<juce::String, Spec> specs;
    const char* keep(const juce::String& s) { own.push_back(s); return own.back().toRawUTF8(); }
    void add(const juce::String& id, const juce::String& label, const char* group, const Spec* sp) { list.push_back({ keep(id), keep(label), group }); if (sp) specs[id] = *sp; }
    DestList() {
        for (auto& d : kBaseDests) list.push_back(d);
        static const char* LN[] = { "Wavetable", "FM", "Sampler", "Procedural", "SID", "LUA" }; static const char* LK[] = { "WT", "FM", "SMP", "PRO", "SID", "LUA" };
        for (int l = 0; l < 6; ++l) for (int b = 1; b <= 4; ++b) for (int g = 0; g < 2; ++g) {
            const juce::String id = juce::String("eq:") + LK[l] + ":" + juce::String(b) + (g ? ":gain" : ":freq");
            Spec sp { juce::String("layers.") + LK[l] + ".eq.band" + juce::String(b) + (g ? ".gain" : ".freq"), g ? -15 : 40, g ? 15 : 16000, g ? 0.5 : 10, !g, false };
            add(id, juce::String(LN[l]) + " EQ band " + juce::String(b) + (g ? " gain" : " frequency"), "EQ", &sp);
        }
        for (int i = 0; i < kNumFxp; ++i) if (juce::String(kFxpDefs[i].type) == "eq") {
            const auto& d = kFxpDefs[i]; Spec sp { juce::String("eq.") + d.key, d.mn, d.mx, d.step, d.exp, true };
            add(juce::String("fx:eq:") + d.key, juce::String("FX EQ ") + d.key, "EQ", &sp);
        }
        add("sidcutoff", "SID Filter cutoff", "SID", nullptr); add("sidreso", "SID Filter resonance", "SID", nullptr);
        add("luacutoff", "LUA Filter cutoff", "LUA", nullptr); add("luareso", "LUA Filter resonance", "LUA", nullptr);
        for (int i = 0; i < kNumVoc; ++i) { Spec sp { juce::String("vocoder.") + kVocDefs[i].key, kVocDefs[i].mn, kVocDefs[i].mx, kVocDefs[i].step, kVocDefs[i].exp, false }; add(juce::String("voc:") + kVocDefs[i].key, juce::String("Vocoder ") + kVocDefs[i].key, "Vocoder", &sp); }
        struct Ex { const char* t; const char* k; const char* lab; double mn, mx, st; bool ex; };
        static const Ex extra[] = { {"phaser","rate","Rate",0.05,5,0.01,true}, {"phaser","feedback","Feedback",0,0.9,0.01,false}, {"phaser","mix","Mix",0,1,0.01,false},
            {"flanger","rate","Rate",0.05,5,0.01,true}, {"flanger","feedback","Feedback",0,0.9,0.01,false}, {"flanger","mix","Mix",0,1,0.01,false},
            {"resonator","freq","Frequency",40,2000,1,true}, {"resonator","resonance","Resonance",1,40,0.5,false}, {"resonator","mix","Mix",0,1,0.01,false} };
        struct Ty { const char* t; const char* name; const char* group; };
        static const Ty types[] = { {"phaser","Phaser","Phaser"}, {"flanger","Flanger","Flanger"}, {"resonator","Resonator","Resonator"}, {"ir","Convolution Reverb","Convolution Reverb"}, {"echo","Echo","Echo"}, {"spread","Stereo Spread","Stereo Spread"}, {"interstellar","Interstellar","Interstellar"} };
        for (auto& ty : types) {
            for (auto& e : extra) if (juce::String(e.t) == ty.t) { Spec sp { juce::String(e.t) + "." + e.k, e.mn, e.mx, e.st, e.ex, true }; add(juce::String("fx:") + e.t + ":" + e.k, juce::String(ty.name) + " " + e.lab, ty.group, &sp); }
            for (int i = 0; i < kNumFxp; ++i) { const auto& d = kFxpDefs[i]; if (juce::String(d.type) != ty.t) continue; bool dup = false; for (auto& e : extra) if (juce::String(e.t) == ty.t && juce::String(e.k) == d.key) dup = true; if (dup) continue;
                Spec sp { juce::String(d.type) + "." + d.key, d.mn, d.mx, d.step, d.exp, true }; add(juce::String("fx:") + d.type + ":" + d.key, juce::String(ty.name) + " " + d.key, ty.group, &sp); }
        }
    }
};
const DestList& destList() { static DestList d; return d; }
#define kDests (destList().list)
double xyScale(double v, const Spec& d) { double val = d.exp && d.mn > 0 ? d.mn * std::pow(d.mx / d.mn, v) : d.mn + v * (d.mx - d.mn); if (d.step > 0) val = std::round(val / d.step) * d.step; return juce::jlimit(d.mn, d.mx, val); }

using Edits = std::vector<std::pair<juce::String, juce::var>>;

// Port of applyXYAxis(): turns a 0..1 axis value into preset edits.
void applyAxis(Store& s, const juce::String& dest, double v, const juce::String& layer, int pitchOsc) {
    Edits e; auto num = [](double x) { return juce::var(x); };
    auto allStages = [&](const char* type, const char* key, double val) { for (int i = 0; i < s.count("fxStages"); ++i) if (s.str("fxStages." + juce::String(i) + ".type") == type) e.push_back({ "fxStages." + juce::String(i) + ".params." + key, num(val) }); };
    auto allOscs = [&](const char* L, int n, const char* key, double val) { for (int i = 0; i < n; ++i) e.push_back({ juce::String("layers.") + L + ".oscs." + juce::String(i) + "." + key, num(val) }); };
    if (dest == "cutoff") e.push_back({ "layers." + layer + ".filter.cutoff", num(40 + v * 13960) });
    else if (dest == "reso") e.push_back({ "layers." + layer + ".filter.reso", num(v * 130) });
    else if (dest.startsWith("lfo") && dest.endsWith("speed")) e.push_back({ "lfo.lfo" + dest.substring(3, 4) + ".hz", num(0.1 + v * 19.9) });
    else if (dest == "wtposition") allOscs("WT", 2, "position", v);
    else if (dest == "wtsegment") allOscs("WT", 2, "segment", 0.05 + v * 0.95);
    else if (dest == "smpposition") allOscs("SMP", 16, "start", v * 0.9);
    else if (dest.startsWith("xmod")) e.push_back({ "xmod." + juce::String(dest.getTrailingIntValue() - 1) + ".amount", num(v) });
    else if (dest == "fmratio") allOscs("FM", 8, "ratio", 0.5 + v * 11.5);
    else if (dest.startsWith("fmop") && dest.endsWith("ratio")) e.push_back({ "layers.FM.oscs." + juce::String(dest.substring(4, 5).getIntValue() - 1) + ".ratio", num(0.5 + v * 11.5) });
    else if (dest == "reverbmix") allStages("reverb", "mix", v);
    else if (dest == "satmix") allStages("saturation", "mix", v);
    else if (dest == "delayfeedback") allStages("delay", "feedback", v * 0.9);
    else if (dest == "delaymix") allStages("delay", "mix", v);
    else if (dest.startsWith("macro")) { const int i = dest.getTrailingIntValue() - 1; e.push_back({ "macros." + juce::String(i), num(v) }); if (s.engine()) vv_engine_set_macro(s.engine(), i, (float) v); }
    else if (dest == "pitch") e.push_back({ "layers." + layer + ".oscs." + juce::String(pitchOsc) + ".oct", num(-2 + v * 4) });
    else if (dest == "grainsize") allStages("granular", "grainSize", 5 + v * 495);
    else if (dest == "grainpitch") allStages("granular", "pitch", -24 + v * 48);
    else if (dest == "grainpan") allStages("granular", "panCenter", -1 + v * 2);
    else if (dest == "graindensity") allStages("granular", "density", 0.5 + v * 99.5);
    else if (dest == "arprate") { e.push_back({ "arp.rateMode", "hz" }); e.push_back({ "arp.hz", num(0.5 + v * 19.5) }); }
    else if (dest == "glitchbitdepth") allStages("glitch", "bitDepth", std::round(16 - v * 15));
    else if (dest == "glitchsr") allStages("glitch", "srReduction", std::round(1 + v * 49));
    else if (dest == "glitchstutterrate") { const double d[] = { 1, 0.5, 0.25, 0.125 }; allStages("glitch", "stutterDivision", d[juce::jmin(3, (int) std::floor(v * 4))]); }
    else if (dest == "glitchintensity") allStages("glitch", "stutterAmount", v);
    else if (dest == "wtfmmix") e.push_back({ "@wtFmMix", num(v) });
    else if (dest == "samplemix") e.push_back({ "@sampleMix", num(v * 15) });
    else if (dest == "mastertone") e.push_back({ "@masterTone", num(v) });
    else if (dest == "wtoctave") allOscs("WT", 2, "oct", v * 4 - 2);
    else if (dest == "wtsemi") allOscs("WT", 2, "semitone", v * 24 - 12);
    else if (dest == "wtfine") allOscs("WT", 2, "fineTune", v * 200 - 100);
    else if (dest == "fmoctave") { juce::Array<juce::var> a{ num(v * 8 - 4), num(v * 8 - 4) }; e.push_back({ "layers.FM.engineOctave", juce::var(a) }); }
    else if (dest == "fmsemi") { juce::Array<juce::var> a{ num(v * 24 - 12), num(v * 24 - 12) }; e.push_back({ "layers.FM.engineSemitone", juce::var(a) }); }
    else if (dest == "fmfine") { juce::Array<juce::var> a{ num(v * 200 - 100), num(v * 200 - 100) }; e.push_back({ "layers.FM.engineFine", juce::var(a) }); }
    else if (dest == "sidcutoff" || dest == "luacutoff") e.push_back({ juce::String("layers.") + (dest == "sidcutoff" ? "SID" : "LUA") + ".filter.cutoff", num(40 + v * 13960) });
    else if (dest == "sidreso" || dest == "luareso") e.push_back({ juce::String("layers.") + (dest == "sidreso" ? "SID" : "LUA") + ".filter.reso", num(v * 130) });
    else { auto it = destList().specs.find(dest);
        if (it != destList().specs.end()) { const Spec& sp = it->second; const double val = xyScale(v, sp);
            if (sp.fxType) { const int dot = sp.path.indexOf("."); allStages(sp.path.substring(0, dot).toRawUTF8(), sp.path.substring(dot + 1).toRawUTF8(), val); } else e.push_back({ sp.path, num(val) }); } }
    if (!e.empty()) s.setMany(e);
}

struct Sample { double t, x, y; };

} // namespace

// One pad: controls + the pad surface + automation.
struct PadPanel : juce::Component, private juce::Timer {
    PadPanel(Store& s, int idx, const juce::String& layer0, const juce::String& xa, const juce::String& ya, bool snap0) : store(s), index(idx) {
        layer = layer0; xAssign = xa; yAssign = ya; snapBack = snap0;
        title.setText("XY Pad " + juce::String(idx + 1), juce::dontSendNotification); title.setColour(juce::Label::textColourId, col::cyan); addAndMakeVisible(title);
        { const juce::StringArray LK { "WT", "FM", "SMP", "PRO", "SID", "LUA" }; layerBox.addItemList(LK, 1); layerBox.setSelectedId(juce::jmax(0, LK.indexOf(layer)) + 1, juce::dontSendNotification); }
        layerBox.onChange = [this] { layer = layerBox.getText(); }; addAndMakeVisible(layerBox);
        auto fill = [](juce::ComboBox& b) { juce::String g; int id = 1; for (auto& d : kDests) { if (g != d.group) { b.addSectionHeading(d.group); g = d.group; } b.addItem(d.label, id++); } };
        fill(xBox); fill(yBox);
        auto sel = [](juce::ComboBox& b, const juce::String& v) { int id = 1; for (auto& d : kDests) { if (v == d.id) b.setSelectedId(id, juce::dontSendNotification); ++id; } };
        sel(xBox, xAssign); sel(yBox, yAssign);
        xBox.onChange = [this] { xAssign = kDests[xBox.getSelectedId() - 1].id; }; yBox.onChange = [this] { yAssign = kDests[yBox.getSelectedId() - 1].id; };
        addAndMakeVisible(xBox); addAndMakeVisible(yBox);
        for (auto* l : { &xl, &yl, &ll, &dl }) { l->setColour(juce::Label::textColourId, col::dim); l->setFont(VVLookAndFeel::mono(11.f)); addAndMakeVisible(l); }
        xl.setText("X", juce::dontSendNotification); yl.setText("Y", juce::dontSendNotification); ll.setText("Layer", juce::dontSendNotification); dl.setText("Auto length", juce::dontSendNotification);
        holdT.setButtonText("Hold"); snapT.setButtonText("Snap back"); snapT.setToggleState(snapBack, juce::dontSendNotification);
        holdT.onClick = [this] { hold = holdT.getToggleState(); }; snapT.onClick = [this] { snapBack = snapT.getToggleState(); };
        addAndMakeVisible(holdT); addAndMakeVisible(snapT);
        durBox.addItemList({ "1 second", "3 seconds", "1 bar", "2 bars", "4 bars" }, 1); durBox.setSelectedId(1, juce::dontSendNotification); addAndMakeVisible(durBox);
        rec.setButtonText("REC"); onoff.setButtonText("AUTOMATION: OFF"); reset.setButtonText("RESET");
        rec.onClick = [this] { if (recording) { recording = false; armed = false; } else armed = !armed; sync(); };
        onoff.onClick = [this] { if (playing) { playing = false; } else if (!data.empty()) { playing = true; startPlayTime = now(); } sync(); };
        reset.onClick = [this] { data.clear(); playing = recording = armed = false; sync(); };
        addAndMakeVisible(rec); addAndMakeVisible(onoff); addAndMakeVisible(reset);
        startTimerHz(60);
    }
    static double now() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }
    double durationSeconds() const { const double bpm = store.num("bpm", 120), bar = 4 * 60 / bpm; const double d[] = { 1, 3, bar, 2 * bar, 4 * bar }; return d[juce::jlimit(0, 4, durBox.getSelectedId() - 1)]; }
    void sync() { rec.setToggleState(armed || recording, juce::dontSendNotification); rec.setButtonText(recording ? "RECORDING..." : (armed ? "ARMED (drag)" : "REC")); onoff.setToggleState(playing, juce::dontSendNotification); onoff.setButtonText(juce::String("AUTOMATION: ") + (playing ? "ON" : "OFF")); }
    void push() {
        applyAxis(store, xAssign, x, layer, osc(xAssign)); applyAxis(store, yAssign, 1.0 - y, layer, osc(yAssign));
        if (store.engine()) { vv_engine_set_xy(store.engine(), index * 2, (float) x); vv_engine_set_xy(store.engine(), index * 2 + 1, (float) (1.0 - y)); }   // the pad is also a matrix source
        repaint(pad);
    }
    int osc(const juce::String&) const { return 0; }
    juce::Rectangle<int> pad;
    void paint(juce::Graphics& g) override {
        g.setColour(col::panel); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1), 8.f);
        g.setColour(col::line); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1), 8.f, 1.f);
        auto p = pad.toFloat(); g.setColour(juce::Colour(0xff0d1013)); g.fillRoundedRectangle(p, 6.f);
        g.setColour(col::line); for (int i = 1; i < 4; ++i) { g.drawLine(p.getX() + p.getWidth() * i / 4, p.getY(), p.getX() + p.getWidth() * i / 4, p.getBottom(), 0.5f); g.drawLine(p.getX(), p.getY() + p.getHeight() * i / 4, p.getRight(), p.getY() + p.getHeight() * i / 4, 0.5f); }
        const float px = p.getX() + (float) x * p.getWidth(), py = p.getY() + (float) y * p.getHeight();
        g.setColour(col::cyanDim); g.drawLine(px, p.getY(), px, p.getBottom()); g.drawLine(p.getX(), py, p.getRight(), py);
        g.setColour(playing ? col::amber : (recording ? col::red : col::cyan)); g.fillEllipse(px - 9, py - 9, 18, 18);
        if (!data.empty()) { g.setColour(col::violet.withAlpha(0.5f)); juce::Path path; bool first = true; for (auto& s : data) { auto pt = juce::Point<float>(p.getX() + (float) s.x * p.getWidth(), p.getY() + (float) s.y * p.getHeight()); if (first) path.startNewSubPath(pt); else path.lineTo(pt); first = false; } g.strokePath(path, juce::PathStrokeType(1.5f)); }
    }
    void resized() override {
        auto r = getLocalBounds().reduced(8); title.setBounds(r.removeFromTop(20));
        auto row1 = r.removeFromTop(26); ll.setBounds(row1.removeFromLeft(42)); layerBox.setBounds(row1.removeFromLeft(70)); row1.removeFromLeft(8); xl.setBounds(row1.removeFromLeft(14)); xBox.setBounds(row1.reduced(0, 0));
        auto row2 = r.removeFromTop(26); row2.removeFromLeft(50); holdT.setBounds(row2.removeFromLeft(70)); snapT.setBounds(row2.removeFromLeft(110)); row2.removeFromLeft(10); yl.setBounds(row2.removeFromLeft(14)); yBox.setBounds(row2);
        auto row3 = r.removeFromTop(28); dl.setBounds(row3.removeFromLeft(80)); durBox.setBounds(row3.removeFromLeft(110)); row3.removeFromLeft(6); rec.setBounds(row3.removeFromLeft(100)); onoff.setBounds(row3.removeFromLeft(170).reduced(2, 0)); reset.setBounds(row3.removeFromLeft(80));
        r.removeFromTop(4); pad = r;
    }
    void mouseDown(const juce::MouseEvent& e) override { if (pad.contains(e.getPosition())) { dragging = true; if (armed) { armed = false; recording = true; data.clear(); recStart = now(); sync(); } move(e); } }
    void mouseDrag(const juce::MouseEvent& e) override { if (dragging) move(e); }
    void mouseUp(const juce::MouseEvent&) override {
        if (!dragging) return; dragging = false;
        if (recording && now() - recStart >= durationSeconds()) { recording = false; sync(); }
        if (!hold && snapBack && !playing) { x = 0.5; y = 0.5; push(); }
    }
    void move(const juce::MouseEvent& e) {
        x = juce::jlimit(0.0, 1.0, (e.x - pad.getX()) / (double) juce::jmax(1, pad.getWidth())); y = juce::jlimit(0.0, 1.0, (e.y - pad.getY()) / (double) juce::jmax(1, pad.getHeight()));
        if (recording) { data.push_back({ now() - recStart, x, y }); if (now() - recStart >= durationSeconds()) { recording = false; if (!data.empty()) { playing = true; startPlayTime = now(); } sync(); } }
        push();
    }
    void timerCallback() override {
        if (recording && !dragging && now() - recStart >= durationSeconds()) { recording = false; sync(); }
        if (!playing || data.empty() || dragging) return;
        const double d = juce::jmax(0.05, data.back().t), t = std::fmod(now() - startPlayTime, d);
        size_t i = 0; while (i + 1 < data.size() && data[i + 1].t <= t) ++i;
        const auto& a = data[i]; const auto& b = data[juce::jmin(i + 1, data.size() - 1)];
        const double f = b.t > a.t ? juce::jlimit(0.0, 1.0, (t - a.t) / (b.t - a.t)) : 0.0;
        x = a.x + (b.x - a.x) * f; y = a.y + (b.y - a.y) * f; push();
    }
    Store& store; int index; juce::String layer, xAssign, yAssign;
    double x = 0.5, y = 0.5; bool hold = false, snapBack = false, dragging = false, armed = false, recording = false, playing = false;
    double recStart = 0, startPlayTime = 0; std::vector<Sample> data;
    juce::Label title, xl, yl, ll, dl; juce::ComboBox layerBox, xBox, yBox, durBox; juce::ToggleButton holdT, snapT; juce::TextButton rec, onoff, reset;
};

struct XYPadsView::Impl { juce::OwnedArray<PadPanel> pads; };

XYPadsView::XYPadsView(Store& s) : impl_(new Impl) {
    struct D { const char* layer; const char* x; const char* y; bool snap; };
    const D d[] = { { "WT", "cutoff", "reso", true }, { "FM", "wtposition", "delaymix", false }, { "SMP", "smpposition", "reverbmix", false }, { "WT", "glitchbitdepth", "glitchintensity", true } };
    for (int i = 0; i < 4; ++i) { auto* p = impl_->pads.add(new PadPanel(s, i, d[i].layer, d[i].x, d[i].y, d[i].snap)); addAndMakeVisible(p); }
}
XYPadsView::~XYPadsView() = default;
int XYPadsView::heightForWidth(int w) const { return w >= 760 ? 2 * 400 + 8 : 4 * 400 + 24; }
void XYPadsView::resized() {
    auto r = getLocalBounds(); const bool two = r.getWidth() >= 760; const int cols = two ? 2 : 1, cw = r.getWidth() / cols, ch = 400;
    for (int i = 0; i < 4; ++i) impl_->pads[i]->setBounds((i % cols) * cw + 2, (i / cols) * (ch + 8), cw - 4, ch);
}

} // namespace vv
