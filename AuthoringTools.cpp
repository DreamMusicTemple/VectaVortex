#include "AuthoringTools.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "CurveEditor.h"
#include "Assets.h"

namespace vv { namespace authoring {

namespace {
juce::var roundedFrame(const std::vector<float>& f) { juce::Array<juce::var> a; a.ensureStorageAllocated((int) f.size()); for (float v : f) a.add(std::round((double) v * 1e6) / 1e6); return juce::var(a); }
}

TableList tableChoices(Store& s) {
    TableList t; for (auto& n : assets::wavetableNames()) { t.ids.add(n); t.labels.add(n); }
    auto cw = s.get("customWavetables"); if (cw.isArray()) for (int i = 0; i < cw.size(); ++i) { t.ids.add(cw[i].getProperty("id", {}).toString()); t.labels.add(cw[i].getProperty("name", "custom").toString() + " (custom)"); }
    return t;
}

void putCustomTable(Store& s, const juce::String& oscPath, const juce::String& idKey, const juce::String& name, const std::vector<std::vector<float>>& frames) {
    juce::Array<juce::var> fr; for (auto& f : frames) fr.add(roundedFrame(f));
    auto* e = new juce::DynamicObject(); juce::String id = s.str(oscPath + "." + idKey, {});
    auto cw = s.get("customWavetables"); int idx = -1; if (id.isNotEmpty() && cw.isArray()) for (int i = 0; i < cw.size(); ++i) if (cw[i].getProperty("id", {}).toString() == id) idx = i;
    if (idx < 0) id = "custom:" + juce::String(juce::Time::currentTimeMillis()) + ":" + juce::String(juce::Random::getSystemRandom().nextInt(1000000));
    e->setProperty("id", id); e->setProperty("name", name); e->setProperty("frames", juce::var(fr));
    if (idx >= 0) s.setMany({ { "customWavetables." + juce::String(idx), juce::var(e) }, { oscPath + ".table", id } });
    else { s.append("customWavetables", juce::var(e)); s.setMany({ { oscPath + "." + idKey, id }, { oscPath + ".table", id } }); }
}

// ------------------------------------------------------------------ small dialogs
namespace {
struct Dialog : juce::Component {
    explicit Dialog(int w, int h) { setSize(w, h); }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff111418)); }
    static void show(juce::Component& anchor, Dialog* d) { juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(d), anchor.getScreenBounds(), nullptr); }
};

struct HarmonicsDialog : Dialog {
    HarmonicsDialog(Store& s, juce::String osc, std::function<void()> ch) : Dialog(560, 250), store(s), oscPath(std::move(osc)), changed(std::move(ch)) {
        auto saved = s.get(oscPath + "._harmAmps"); amps.assign(16, 0.0); amps[0] = 1.0; if (saved.isArray()) for (int i = 0; i < 16 && i < saved.size(); ++i) amps[(size_t) i] = (double) saved[i];
        for (int i = 0; i < 16; ++i) { auto& sl = sliders[i]; sl.setSliderStyle(juce::Slider::LinearBarVertical); sl.setRange(-1, 1, 0.01); sl.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); sl.setDoubleClickReturnValue(true, i == 0 ? 1.0 : 0.0);
            sl.setValue(amps[(size_t) i], juce::dontSendNotification); sl.onValueChange = [this, i] { amps[(size_t) i] = sliders[i].getValue(); startTimer(120); }; addAndMakeVisible(sl); }
        title.setText("Edit harmonics - 16 partials, bipolar (double-click resets). The table updates as you drag.", juce::dontSendNotification); title.setColour(juce::Label::textColourId, col::dim); addAndMakeVisible(title);
        resized(); apply();
    }
    void resized() override { auto r = getLocalBounds().reduced(8); title.setBounds(r.removeFromTop(22)); const int w = r.getWidth() / 16; for (int i = 0; i < 16; ++i) sliders[i].setBounds(r.removeFromLeft(w).reduced(2, 0)); }
    void apply() {
        float out[1024]; std::vector<float> a(amps.begin(), amps.end()); vv_wt_harmonics(a.data(), 16, out);
        juce::Array<juce::var> sv; for (double d : amps) sv.add(d); store.set(oscPath + "._harmAmps", juce::var(sv));
        putCustomTable(store, oscPath, "_harmCustomId", "Harmonic Wavetable", { std::vector<float>(out, out + 1024) }); if (changed) changed();
    }
    void timerFire() { stopTimer(); apply(); }
    struct T : juce::Timer { std::function<void()> f; void timerCallback() override { f(); } } tm;
    void startTimer(int ms) { tm.f = [this] { timerFire(); }; tm.startTimer(ms); } void stopTimer() { tm.stopTimer(); }
    Store& store; juce::String oscPath; std::function<void()> changed; std::vector<double> amps; juce::Slider sliders[16]; juce::Label title;
};

struct FormulaDialog : Dialog {
    FormulaDialog(Store& s, juce::String osc, std::function<void()> ch) : Dialog(560, 220), store(s), oscPath(std::move(osc)), changed(std::move(ch)) {
        help.setText("Expression in x (phase 0..1 across one cycle) and, if used, n (position 0..1 across the table's frames), e.g. Math.sin(2*Math.PI*x*(1+6*n)). "
                     "Supports + - * / % **, comparisons, && || ?: and every Math.* function; output is normalised per frame.", juce::dontSendNotification);
        help.setMinimumHorizontalScale(1.f); help.setColour(juce::Label::textColourId, col::dim); help.setFont(VVLookAndFeel::mono(11.f)); addAndMakeVisible(help);
        auto saved = s.str(oscPath + "._wtFormula", "Math.sin(2*Math.PI*x) + 0.3*Math.sin(2*Math.PI*x*3)");
        input.setText(saved); input.setFont(VVLookAndFeel::mono(13.f)); addAndMakeVisible(input);
        for (int n : { 8, 16, 32, 64, 128, 256 }) frames.addItem(juce::String(n), n); frames.setSelectedId((int) s.num(oscPath + "._wtFormulaFrames", 32), juce::dontSendNotification); if (frames.getSelectedId() == 0) frames.setSelectedId(32, juce::dontSendNotification);
        framesL.setText("Frames (if formula uses n)", juce::dontSendNotification); addAndMakeVisible(framesL); addAndMakeVisible(frames);
        err.setColour(juce::Label::textColourId, juce::Colour(0xffe05a5a)); addAndMakeVisible(err);
        applyB.setButtonText("APPLY"); applyB.onClick = [this] { apply(); }; addAndMakeVisible(applyB); resized();
    }
    void resized() override { auto r = getLocalBounds().reduced(10); help.setBounds(r.removeFromTop(54)); input.setBounds(r.removeFromTop(30)); r.removeFromTop(6);
        auto row = r.removeFromTop(28); framesL.setBounds(row.removeFromLeft(200)); frames.setBounds(row.removeFromLeft(90)); r.removeFromTop(4); err.setBounds(r.removeFromTop(40)); applyB.setBounds(r.removeFromBottom(30).removeFromRight(110)); }
    void apply() {
        std::vector<float> buf((size_t) 1024 * 256); char e[200] = {0};
        const int n = vv_wt_formula(input.getText().toRawUTF8(), frames.getSelectedId(), buf.data(), 256, e, 200);
        if (n <= 0) { err.setText(juce::String::fromUTF8(e), juce::dontSendNotification); return; }
        err.setText({}, juce::dontSendNotification);
        std::vector<std::vector<float>> fr; for (int i = 0; i < n; ++i) fr.emplace_back(buf.begin() + (size_t) i * 1024, buf.begin() + (size_t) (i + 1) * 1024);
        store.setMany({ { oscPath + "._wtFormula", input.getText() }, { oscPath + "._wtFormulaFrames", frames.getSelectedId() } });
        putCustomTable(store, oscPath, "_formulaCustomId", "Formula Wavetable", fr); if (changed) changed();
    }
    Store& store; juce::String oscPath; std::function<void()> changed; juce::Label help, framesL, err; juce::TextEditor input; juce::ComboBox frames; juce::TextButton applyB;
};

struct DrawDialog : Dialog, private Store::Listener {
    DrawDialog(Store& s, juce::String osc, std::function<void()> ch) : Dialog(620, 330), store(s), oscPath(std::move(osc)), changed(std::move(ch)), ptsPath(oscPath + "._wtDrawPoints") {
        if (readCurve(store, ptsPath).size() < 2) { std::vector<CurvePoint> p(5); const double xy[5][2] = { {0, 0.5}, {0.25, 1}, {0.5, 0.5}, {0.75, 0}, {1, 0.5} }; for (int i = 0; i < 5; ++i) { p[(size_t) i].x = xy[i][0]; p[(size_t) i].y = xy[i][1]; } ensureControls(p); writeCurve(store, ptsPath, p); }
        editor = std::make_unique<CurveEditor>(store, ptsPath); addAndMakeVisible(*editor); store.addListener(this); resized(); apply();
    }
    ~DrawDialog() override { store.removeListener(this); }
    void resized() override { editor->setBounds(getLocalBounds().withSizeKeepingCentre(editor->getWidth(), editor->getHeight())); }
    void storeChanged() override { if (!applying) { tm.f = [this] { tm.stopTimer(); apply(); }; tm.startTimer(150); } }
    void apply() {
        auto pts = readCurve(store, ptsPath); if (pts.size() < 2) return; ensureControls(pts); std::vector<float> flat; for (auto& p : pts) { flat.push_back((float) p.x); flat.push_back((float) p.y); flat.push_back((float) p.cx); flat.push_back((float) p.cy); }
        if (flat == lastFlat) return;   // the store notifies for every edit (including the table we write below): only regenerate when the drawing changed
        lastFlat = flat; float out[1024]; if (!vv_wt_curve(flat.data(), (int) pts.size(), out)) return;
        applying = true; putCustomTable(store, oscPath, "_drawCustomId", "Drawn Wavetable", { std::vector<float>(out, out + 1024) }); applying = false; if (changed) changed();
    }
    struct T : juce::Timer { std::function<void()> f; void timerCallback() override { f(); } } tm;
    Store& store; juce::String oscPath; std::function<void()> changed; juce::String ptsPath; std::unique_ptr<CurveEditor> editor; bool applying = false; std::vector<float> lastFlat;
};
}

std::unique_ptr<juce::Component> makeHarmonics(Store& s, const juce::String& p, std::function<void()> c) { return std::make_unique<HarmonicsDialog>(s, p, std::move(c)); }
std::unique_ptr<juce::Component> makeFormula(Store& s, const juce::String& p, std::function<void()> c) { return std::make_unique<FormulaDialog>(s, p, std::move(c)); }
std::unique_ptr<juce::Component> makeDraw(Store& s, const juce::String& p, std::function<void()> c) { return std::make_unique<DrawDialog>(s, p, std::move(c)); }
void openHarmonics(Store& s, const juce::String& p, juce::Component& a, std::function<void()> c) { Dialog::show(a, new HarmonicsDialog(s, p, std::move(c))); }
void openFormula(Store& s, const juce::String& p, juce::Component& a, std::function<void()> c) { Dialog::show(a, new FormulaDialog(s, p, std::move(c))); }
void openDraw(Store& s, const juce::String& p, juce::Component& a, std::function<void()> c) { Dialog::show(a, new DrawDialog(s, p, std::move(c))); }

// ------------------------------------------------------------------ audio helpers
namespace {
// decode any file JUCE knows into mono-or-stereo float audio
VVAudio* readAudioFile(const juce::File& f) {
    juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f)); if (!r || r->lengthInSamples <= 0 || r->lengthInSamples > 48000LL * 600) return nullptr;
    const int len = (int) r->lengthInSamples, nCh = (int) juce::jmin(2u, r->numChannels); juce::AudioBuffer<float> buf(nCh, len); r->read(&buf, 0, len, 0, true, nCh > 1);
    return vv_audio_create((int) r->sampleRate, nCh, len, buf.getReadPointer(0), nCh > 1 ? buf.getReadPointer(1) : nullptr);
}
juce::MemoryBlock wavOf(const VVAudio* a) { juce::MemoryBlock mb; const size_t n = vv_audio_to_wav16(a, nullptr, 0); mb.setSize(n); vv_audio_to_wav16(a, (uint8_t*) mb.getData(), n); return mb; }
}

juce::MemoryBlock sampleWav(Store& s, const juce::String& name) {
    juce::MemoryBlock mb; if (name.isEmpty()) return mb;
    auto lib = s.get("sampleLibrary"); if (lib.isArray()) for (int i = 0; i < lib.size(); ++i) if (lib[i].getProperty("name", {}).toString() == name) { juce::MemoryOutputStream o(mb, false); juce::Base64::convertFromBase64(o, lib[i].getProperty("base64", {}).toString()); return mb; }
    auto list = juce::JSON::parse(assets::root().getChildFile("builtin_samples.json"));
    if (auto* a = list.getArray()) for (auto& e : *a) if (e.getProperty("name", {}).toString() == name) assets::root().getChildFile("Samples").getChildFile(e.getProperty("file", {}).toString()).loadFileAsData(mb);
    return mb;
}
juce::String uniqueSampleName(Store& s, const juce::String& base) {
    auto taken = [&](const juce::String& n) { auto lib = s.get("sampleLibrary"); if (lib.isArray()) for (int i = 0; i < lib.size(); ++i) if (lib[i].getProperty("name", {}).toString() == n) return true; return assets::sampleNames().contains(n); };
    juce::String n = base; int k = 2; while (taken(n)) n = base + " " + juce::String(k++); return n;
}
void putSample(Store& s, const juce::String& name, const juce::MemoryBlock& wav) {
    auto* e = new juce::DynamicObject(); e->setProperty("name", name); e->setProperty("base64", juce::Base64::toBase64(wav.getData(), wav.getSize()));
    auto lib = s.get("sampleLibrary"); int idx = -1; if (lib.isArray()) for (int i = 0; i < lib.size(); ++i) if (lib[i].getProperty("name", {}).toString() == name) idx = i;
    if (idx >= 0) s.set("sampleLibrary." + juce::String(idx), juce::var(e)); else s.append("sampleLibrary", juce::var(e));
}

void importWavetableAudio(Store& s, const juce::String& oscPath, std::function<void()> changed) {
    auto chooser = std::make_shared<juce::FileChooser>("Import audio as a wavetable", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [chooser, &s, oscPath, changed](const juce::FileChooser& fc) {
        auto f = fc.getResult(); if (!f.existsAsFile()) return;
        VVAudio* a = readAudioFile(f); if (!a) { juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Import", "Could not read that file as a wavetable - try a short audio file (WAV, AIFF, FLAC, OGG or MP3)."); return; }
        const int len = vv_audio_length(a); std::vector<float> mono((size_t) len); vv_audio_read(a, 0, mono.data()); vv_audio_free(a);
        std::vector<float> buf((size_t) 1024 * 256); const int n = vv_wt_slice(mono.data(), len, 256, buf.data(), 256); if (n <= 0) return;
        std::vector<std::vector<float>> fr; for (int i = 0; i < n; ++i) fr.emplace_back(buf.begin() + (size_t) i * 1024, buf.begin() + (size_t) (i + 1) * 1024);
        putCustomTable(s, oscPath, "_importCustomId", f.getFileName(), fr); if (changed) changed(); });
}

// ------------------------------------------------------------------ sampler slot tools
namespace {
struct WarpEditor : juce::Component {
    // waveform of the original with draggable warp markers: click the waveform to add a marker, drag its dot on the ruler to move where that moment should play
    std::vector<float> peaks; struct M { double orig, now; }; std::vector<M> markers; int drag = -1; std::function<void()> onChange;
    void setAudio(const VVAudio* a) { peaks.clear(); if (!a) { repaint(); return; } const int len = vv_audio_length(a); std::vector<float> d((size_t) len); vv_audio_read(a, 0, d.data()); const int n = 600; peaks.assign((size_t) n, 0.f);
        for (int i = 0; i < n; ++i) { const int s0 = (int) ((double) i / n * len), s1 = juce::jmax(s0 + 1, (int) ((double) (i + 1) / n * len)); float m = 0; for (int k = s0; k < s1 && k < len; ++k) m = juce::jmax(m, std::fabs(d[(size_t) k])); peaks[(size_t) i] = m; } repaint(); }
    juce::Rectangle<float> wave() const { return getLocalBounds().toFloat().withTrimmedBottom(26); }
    float X(double f) const { return wave().getX() + (float) f * wave().getWidth(); }
    void paint(juce::Graphics& g) override {
        auto w = wave(); g.setColour(juce::Colours::black); g.fillRect(w); g.setColour(col::cyan.withAlpha(0.8f));
        if (!peaks.empty()) for (size_t i = 0; i < peaks.size(); ++i) { const float x = w.getX() + w.getWidth() * (float) i / (float) peaks.size(), h = peaks[i] * w.getHeight() * 0.48f; g.drawVerticalLine((int) x, w.getCentreY() - h, w.getCentreY() + h); }
        g.setColour(juce::Colour(0xff333333)); g.drawHorizontalLine((int) w.getCentreY(), w.getX(), w.getRight());
        auto ruler = getLocalBounds().toFloat().removeFromBottom(26); g.setColour(juce::Colour(0xff1b1e23)); g.fillRect(ruler);
        for (auto& m : markers) { g.setColour(juce::Colour(0x88ff9a3c)); g.drawVerticalLine((int) X(m.orig), w.getY(), w.getBottom()); g.setColour(juce::Colour(0xffff9a3c)); g.drawLine(X(m.orig), w.getBottom(), X(m.now), ruler.getCentreY(), 1.f); g.fillEllipse(X(m.now) - 6, ruler.getCentreY() - 6, 12, 12); }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const double f = juce::jlimit(0.0, 1.0, (double) ((e.position.x - wave().getX()) / wave().getWidth())); drag = -1;
        for (size_t i = 0; i < markers.size(); ++i) if (std::abs(X(markers[i].now) - e.position.x) < 9 && e.position.y > wave().getBottom() - 2) drag = (int) i;
        if (drag < 0 && e.position.y < wave().getBottom()) { if (e.mods.isPopupMenu()) { for (size_t i = 0; i < markers.size(); ++i) if (std::abs(X(markers[i].orig) - e.position.x) < 9) { markers.erase(markers.begin() + (long) i); repaint(); if (onChange) onChange(); return; } }
            else { markers.push_back({ f, f }); std::sort(markers.begin(), markers.end(), [](const M& a, const M& b) { return a.orig < b.orig; }); repaint(); if (onChange) onChange(); } }
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (drag < 0) return; const double lo = drag > 0 ? markers[(size_t) drag - 1].now + 0.002 : 0.002, hi = drag + 1 < (int) markers.size() ? markers[(size_t) drag + 1].now - 0.002 : 0.998;
        markers[(size_t) drag].now = juce::jlimit(lo, juce::jmax(lo, hi), (double) ((e.position.x - wave().getX()) / wave().getWidth())); repaint(); if (onChange) onChange();
    }
    void mouseUp(const juce::MouseEvent&) override { drag = -1; }
};

struct SampleToolsDialog : Dialog {
    SampleToolsDialog(Store& s, int slot, std::function<void()> ch) : Dialog(640, 560), store(s), path("layers.SMP.oscs." + juce::String(slot)), changed(std::move(ch)) {
        auto label = [this](juce::Label& l, const juce::String& t) { l.setText(t, juce::dontSendNotification); l.setColour(juce::Label::textColourId, col::dim); l.setFont(VVLookAndFeel::mono(11.f)); addAndMakeVisible(l); };
        label(title, {}); label(status, {}); status.setColour(juce::Label::textColourId, col::cyan);
        importB.setButtonText("IMPORT AUDIO..."); importB.onClick = [this] { doImport(); }; revertB.setButtonText("REVERT TO ORIGINAL"); revertB.onClick = [this] { revert(); };
        for (auto* b : { &importB, &revertB, &stretchB, &warpB, &specB, &clearMk, &resetB }) addAndMakeVisible(*b);
        label(stL, "TIME STRETCH (WSOLA, keeps pitch)"); stMode.addItem("Time ratio", 1); stMode.addItem("Pitch shift (keep length)", 2); stMode.addItem("Fit to beats", 3); stMode.setSelectedId(1, juce::dontSendNotification);
        stQual.addItem("Percussive", 1); stQual.addItem("Normal", 2); stQual.addItem("Smooth", 3); stQual.setSelectedId(2, juce::dontSendNotification);
        stVal.setSliderStyle(juce::Slider::LinearBar); stVal.setRange(0.1, 8, 0.01); stVal.setValue(1.0, juce::dontSendNotification);
        stMode.onChange = [this] { const int m = stMode.getSelectedId(); if (m == 1) { stVal.setRange(0.1, 8, 0.01); stVal.setValue(1.0, juce::dontSendNotification); } else if (m == 2) { stVal.setRange(-24, 24, 0.1); stVal.setValue(0, juce::dontSendNotification); } else { stVal.setRange(1, 32, 0.25); stVal.setValue(4, juce::dontSendNotification); } };
        stretchB.setButtonText("APPLY STRETCH"); stretchB.onClick = [this] { doStretch(); };
        for (auto* c : std::initializer_list<juce::Component*>{ &stMode, &stQual, &stVal }) addAndMakeVisible(*c);
        label(wpL, "AUDIO WARP: click the waveform to drop a marker; drag its dot on the ruler to move that moment on the output timeline; right-click a marker to remove it"); warpB.setButtonText("APPLY WARP"); warpB.onClick = [this] { doWarp(); };
        clearMk.setButtonText("CLEAR MARKERS"); clearMk.onClick = [this] { warp.markers.clear(); warp.repaint(); }; addAndMakeVisible(warp);
        label(spL, "SPECTRAL / RESYNTHESIS (STFT band shaping, low to high)"); spEng.addItem("Spectral (STFT resynthesis)", 1); spEng.addItem("Granular (grain-jittered)", 2); spEng.setSelectedId(1, juce::dontSendNotification);
        spScale.addItem("Linear", 1); spScale.addItem("Logarithmic", 2); spScale.setSelectedId(1, juce::dontSendNotification); spWin.addItem("Hann", 1); spWin.addItem("Hamming", 2); spWin.addItem("Blackman", 3); spWin.addItem("Rectangular", 4); spWin.setSelectedId(1, juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::Component*>{ &spEng, &spScale, &spWin }) addAndMakeVisible(*c);
        for (int i = 0; i < 8; ++i) { band[i].setSliderStyle(juce::Slider::LinearBarVertical); band[i].setRange(0, 2, 0.01); band[i].setValue(1, juce::dontSendNotification); band[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); band[i].setDoubleClickReturnValue(true, 1.0); addAndMakeVisible(band[i]); }
        specB.setButtonText("APPLY SPECTRAL"); specB.onClick = [this] { doSpectral(); }; resetB.setButtonText("RESET BANDS"); resetB.onClick = [this] { for (auto& b : band) b.setValue(1); };
        loadOriginal(); resized();
    }
    ~SampleToolsDialog() override { if (orig) vv_audio_free(orig); }
    juce::String origName() const { auto o = store.str(path + ".origName", {}); return o.isNotEmpty() ? o : store.str(path + ".name", {}); }
    void loadOriginal() {
        if (orig) { vv_audio_free(orig); orig = nullptr; }
        const auto name = origName(); auto mb = sampleWav(store, name);
        if (mb.getSize()) orig = vv_audio_from_wav((const uint8_t*) mb.getData(), mb.getSize());
        title.setText("Slot sample: " + (name.isEmpty() ? juce::String("(none - import audio or pick a sample first)") : name) + (store.str(path + ".origName", {}).isNotEmpty() ? "   [processed: " + store.str(path + ".name", {}) + "]" : ""), juce::dontSendNotification);
        warp.setAudio(orig);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(10); auto top = r.removeFromTop(26); importB.setBounds(top.removeFromLeft(150)); top.removeFromLeft(6); revertB.setBounds(top.removeFromLeft(170)); title.setBounds(r.removeFromTop(20));
        r.removeFromTop(4); stL.setBounds(r.removeFromTop(18)); auto st = r.removeFromTop(28); stMode.setBounds(st.removeFromLeft(190)); st.removeFromLeft(6); stVal.setBounds(st.removeFromLeft(150)); st.removeFromLeft(6); stQual.setBounds(st.removeFromLeft(120)); st.removeFromLeft(6); stretchB.setBounds(st.removeFromLeft(140));
        r.removeFromTop(8); wpL.setBounds(r.removeFromTop(30)); warp.setBounds(r.removeFromTop(120)); auto wb = r.removeFromTop(30).reduced(0, 2); warpB.setBounds(wb.removeFromLeft(130)); wb.removeFromLeft(6); clearMk.setBounds(wb.removeFromLeft(140));
        r.removeFromTop(6); spL.setBounds(r.removeFromTop(18)); auto sp = r.removeFromTop(28); spEng.setBounds(sp.removeFromLeft(210)); sp.removeFromLeft(6); spScale.setBounds(sp.removeFromLeft(120)); sp.removeFromLeft(6); spWin.setBounds(sp.removeFromLeft(130));
        auto status_ = r.removeFromBottom(20); status.setBounds(status_); auto bb = r.removeFromBottom(30); specB.setBounds(bb.removeFromLeft(160)); bb.removeFromLeft(6); resetB.setBounds(bb.removeFromLeft(130));
        auto bands = r.reduced(0, 2); const int w = bands.getWidth() / 8; for (int i = 0; i < 8; ++i) band[i].setBounds(bands.removeFromLeft(w).reduced(8, 0));
    }
    juce::String baseName() const { return origName(); }
    void commit(VVAudio* res, const juce::String& suffix, const juce::var& cfgKey, const juce::var& cfg) {
        if (!res) { status.setText("Processing failed.", juce::dontSendNotification); return; }
        const auto base = baseName(); const auto nm = uniqueSampleName(store, base + " (" + suffix + ")"); putSample(store, nm, wavOf(res)); vv_audio_free(res);
        store.setMany({ { path + ".origName", base }, { path + ".name", nm }, { path + "." + cfgKey.toString(), cfg } });
        status.setText("Done: " + nm, juce::dontSendNotification); loadOriginal(); if (changed) changed();
    }
    bool need() { if (orig) return true; status.setText("Load or import a sample into this slot first.", juce::dontSendNotification); return false; }
    void doStretch() {
        if (!need()) return; const int m = stMode.getSelectedId() - 1; const double v = stVal.getValue(); const double bpm = store.num("bpm", 120);
        auto* o = new juce::DynamicObject(); o->setProperty("mode", m == 0 ? "ratio" : m == 1 ? "pitch" : "fit"); o->setProperty("value", v); o->setProperty("quality", stQual.getSelectedId() - 1);
        juce::MouseCursor::showWaitCursor(); auto* res = vv_audio_stretch(orig, m, m == 0 ? v : 1, m == 1 ? v : 0, m == 2 ? v : 4, bpm, stQual.getSelectedId() - 1); juce::MouseCursor::hideWaitCursor();
        commit(res, m == 0 ? "stretched" : m == 1 ? "pitched" : "fit", "stretch", juce::var(o));
    }
    void doWarp() {
        if (!need()) return; std::vector<double> mk; for (auto& m : warp.markers) { mk.push_back(m.orig); mk.push_back(m.now); } if (mk.empty()) { status.setText("Add at least one warp marker first.", juce::dontSendNotification); return; }
        auto* o = new juce::DynamicObject(); juce::Array<juce::var> a; for (auto& m : warp.markers) { auto* e = new juce::DynamicObject(); e->setProperty("orig", m.orig); e->setProperty("now", m.now); a.add(juce::var(e)); } o->setProperty("markers", juce::var(a));
        juce::MouseCursor::showWaitCursor(); auto* res = vv_audio_warp(orig, mk.data(), (int) mk.size() / 2); juce::MouseCursor::hideWaitCursor(); commit(res, "warped", "warp", juce::var(o));
    }
    void doSpectral() {
        if (!need()) return; double b[8]; auto* o = new juce::DynamicObject(); juce::Array<juce::var> a; for (int i = 0; i < 8; ++i) { b[i] = band[i].getValue(); a.add(b[i]); }
        o->setProperty("engine", spEng.getSelectedId() == 2 ? "granular" : "spectral"); o->setProperty("scale", spScale.getSelectedId() == 2 ? "log" : "linear"); o->setProperty("window", spWin.getSelectedId() - 1); o->setProperty("bands", juce::var(a));
        juce::MouseCursor::showWaitCursor(); auto* res = vv_audio_spectral(orig, spEng.getSelectedId() == 2, spScale.getSelectedId() == 2, spWin.getSelectedId() - 1, b, 8); juce::MouseCursor::hideWaitCursor(); commit(res, "spectral", "spectral", juce::var(o));
    }
    void doImport() {
        chooser = std::make_shared<juce::FileChooser>("Import audio into this slot", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, alive = juce::Component::SafePointer<SampleToolsDialog>(this)](const juce::FileChooser& fc) {
            if (!alive) return; auto f = fc.getResult(); if (!f.existsAsFile()) return; VVAudio* a = readAudioFile(f); if (!a) { status.setText("Could not read that file.", juce::dontSendNotification); return; }
            const auto nm = uniqueSampleName(store, f.getFileNameWithoutExtension()); putSample(store, nm, wavOf(a)); vv_audio_free(a);
            store.setMany({ { path + ".name", nm }, { path + ".origName", juce::var() } }); status.setText("Imported " + nm, juce::dontSendNotification); loadOriginal(); if (changed) changed(); });
    }
    void revert() { const auto o = store.str(path + ".origName", {}); if (o.isEmpty()) { status.setText("Nothing to revert.", juce::dontSendNotification); return; } store.setMany({ { path + ".name", o }, { path + ".origName", juce::var() }, { path + ".stretch", juce::var() }, { path + ".warp", juce::var() }, { path + ".spectral", juce::var() } });
        status.setText("Original restored.", juce::dontSendNotification); loadOriginal(); if (changed) changed(); }
    Store& store; juce::String path; std::function<void()> changed; VVAudio* orig = nullptr; std::shared_ptr<juce::FileChooser> chooser;
    juce::Label title, status, stL, wpL, spL; juce::TextButton importB, revertB, stretchB, warpB, specB, clearMk, resetB; juce::ComboBox stMode, stQual, spEng, spScale, spWin; juce::Slider stVal, band[8]; WarpEditor warp;
};
}

std::unique_ptr<juce::Component> makeSampleTools(Store& s, int slot, std::function<void()> c) { return std::make_unique<SampleToolsDialog>(s, slot, std::move(c)); }
void openSampleTools(Store& s, int slot, juce::Component& a, std::function<void()> c) { Dialog::show(a, new SampleToolsDialog(s, slot, std::move(c))); }


// ------------------------------------------------------------------ RECORD (master output or external input -> slot) and IMAGE -> AUDIO
namespace {
struct RecordDialog : Dialog, juce::Timer {
    RecordDialog(Store& s, int slot, std::function<void()> ch) : Dialog(460, 250), store(s), path("layers.SMP.oscs." + juce::String(slot)), changed(std::move(ch)) {
        auto lab = [this](juce::Label& l, const juce::String& t) { l.setText(t, juce::dontSendNotification); l.setColour(juce::Label::textColourId, col::dim); l.setFont(VVLookAndFeel::mono(11.f)); addAndMakeVisible(l); };
        lab(srcL, "SOURCE"); lab(timeL, "0:00.0 / max 2:00"); lab(status, {}); status.setColour(juce::Label::textColourId, col::cyan); lab(nameL, "NAME");
        src.addItem("Master output (resample)", 1); src.addItem("External input (mic / host input)", 2); src.setSelectedId(1, juce::dontSendNotification);
        armT.setButtonText("Arm: start on sound"); armT.setToggleState(true, juce::dontSendNotification); loadT.setButtonText("Load into slot"); loadT.setToggleState(true, juce::dontSendNotification);
        goB.setButtonText("RECORD"); stopB.setButtonText("STOP"); saveB.setButtonText("SAVE"); playB.setButtonText("PLAY"); stopB.setEnabled(false); saveB.setEnabled(false); playB.setEnabled(false);
        name.setText("Recording", juce::dontSendNotification); name.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1c2026));
        for (auto* c : std::initializer_list<juce::Component*>{ &src, &armT, &loadT, &goB, &stopB, &saveB, &playB, &name }) addAndMakeVisible(*c);
        goB.onClick = [this] { start(); }; stopB.onClick = [this] { stop(); }; saveB.onClick = [this] { save(); };
        playB.onClick = [this] { if (result) { status.setText("Preview is not available here: save it into the slot and play a note.", juce::dontSendNotification); } };
        playB.setVisible(false);
        resized();
    }
    ~RecordDialog() override { stopTimer(); if (auto* e = store.engine()) vv_engine_rec_stop(e); if (result) vv_audio_free(result); }
    void resized() override {
        auto r = getLocalBounds().reduced(10); auto a = r.removeFromTop(24); srcL.setBounds(a.removeFromLeft(60)); src.setBounds(a.removeFromLeft(260));
        r.removeFromTop(6); auto b = r.removeFromTop(24); armT.setBounds(b.removeFromLeft(190)); loadT.setBounds(b.removeFromLeft(150)); r.removeFromTop(6);
        auto c = r.removeFromTop(30); goB.setBounds(c.removeFromLeft(110)); c.removeFromLeft(6); stopB.setBounds(c.removeFromLeft(90)); c.removeFromLeft(14); timeL.setBounds(c);
        r.removeFromTop(8); auto d = r.removeFromTop(26); nameL.setBounds(d.removeFromLeft(50)); name.setBounds(d.removeFromLeft(240)); d.removeFromLeft(8); saveB.setBounds(d.removeFromLeft(90));
        r.removeFromTop(8); status.setBounds(r.removeFromTop(40));
    }
    void paint(juce::Graphics& g) override { Dialog::paint(g); const float lv = juce::jlimit(0.f, 1.f, std::sqrt(level)); g.setColour(juce::Colour(0xff222831)); g.fillRect(10, getHeight() - 30, getWidth() - 20, 10);
        g.setColour(level > 0.95f ? juce::Colours::red : juce::Colour(0xff33cc99)); g.fillRect(10.f, (float) getHeight() - 30.f, (getWidth() - 20) * lv, 10.f); }
    double rate() const { auto* e = store.engine(); return e ? vv_engine_sample_rate(e) : 48000.0; }
    void start() {
        auto* e = store.engine(); if (!e) return; if (result) { vv_audio_free(result); result = nullptr; }
        vv_engine_rec_prepare(e, (int) rate(), 120.0, src.getSelectedId() == 2 ? 1 : 0, armT.getToggleState() ? 1 : 0, 0.02); vv_engine_rec_start(e);
        goB.setEnabled(false); stopB.setEnabled(true); saveB.setEnabled(false);
        status.setText(armT.getToggleState() ? "Armed: waiting for sound..." : "Recording...", juce::dontSendNotification); startTimerHz(15);
    }
    void stop() { if (auto* e = store.engine()) vv_engine_rec_stop(e); finish(); }
    void finish() {
        stopTimer(); auto* e = store.engine(); goB.setEnabled(true); stopB.setEnabled(false); if (!e) return;
        result = vv_engine_rec_take(e, (int) rate()); if (!result) { status.setText("Nothing was recorded.", juce::dontSendNotification); return; }
        saveB.setEnabled(true); status.setText("Recorded " + juce::String(vv_audio_length(result) / rate(), 1) + " s. Name it, then save.", juce::dontSendNotification);
        const auto t = juce::Time::getCurrentTime(); name.setText("Recording " + t.formatted("%Y-%m-%d %H.%M"), juce::dontSendNotification);
    }
    void timerCallback() override {
        auto* e = store.engine(); if (!e) return; level = vv_engine_rec_peak(e); const int st = vv_engine_rec_state(e), fr = vv_engine_rec_frames(e);
        const double sec = fr / rate(); timeL.setText(juce::String((int) sec / 60) + ":" + juce::String(sec - 60 * ((int) sec / 60), 1).paddedLeft('0', 4) + " / max 2:00", juce::dontSendNotification);
        if (st == 2 && status.getText().startsWith("Armed")) status.setText("Recording...", juce::dontSendNotification);
        if (st == 3) finish(); repaint();
    }
    void save() {
        if (!result) return; auto nm = uniqueSampleName(store, name.getText().trim().isEmpty() ? juce::String("Recording") : name.getText().trim());
        putSample(store, nm, wavOf(result)); vv_audio_free(result); result = nullptr; saveB.setEnabled(false);
        if (loadT.getToggleState()) store.setMany({ { path + ".name", nm }, { path + ".origName", juce::var() } });
        status.setText("Saved \"" + nm + "\" to the preset's sample library.", juce::dontSendNotification); if (changed) changed();
    }
    Store& store; juce::String path; std::function<void()> changed; VVAudio* result = nullptr; float level = 0;
    juce::Label srcL, timeL, status, nameL; juce::ComboBox src; juce::ToggleButton armT, loadT; juce::TextButton goB, stopB, saveB, playB; juce::TextEditor name;
};

struct ImageAudioDialog : Dialog {
    ImageAudioDialog(Store& s, int slot, std::function<void()> ch) : Dialog(520, 330), store(s), path("layers.SMP.oscs." + juce::String(slot)), changed(std::move(ch)) {
        auto lab = [this](juce::Label& l, const juce::String& t) { l.setText(t, juce::dontSendNotification); l.setColour(juce::Label::textColourId, col::dim); l.setFont(VVLookAndFeel::mono(11.f)); addAndMakeVisible(l); };
        lab(info, "Each image column is a moment in time and each row a harmonic of the base note (top row = highest). Brightness sets the harmonic's loudness."); lab(status, {}); status.setColour(juce::Label::textColourId, col::cyan);
        lab(fileL, "(no image)"); lab(modeL, "COLOUR"); lab(durL, "DURATION s"); lab(noteL, "BASE NOTE"); lab(harL, "HARMONICS"); lab(tiltL, "HIGH TILT"); lab(gamL, "CONTRAST");
        chooseB.setButtonText("CHOOSE IMAGE..."); renderB.setButtonText("RENDER INTO SLOT"); renderB.setEnabled(false);
        mode.addItem("Brightness only (mono)", 1); mode.addItem("RGB: red/blue = L/R, green = phase", 2); mode.setSelectedId(1, juce::dontSendNotification);
        const char* nn[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; for (int m = 24; m <= 84; ++m) note.addItem(juce::String(nn[m % 12]) + juce::String(m / 12 - 1), m); note.setSelectedId(36, juce::dontSendNotification);
        for (int h : { 32, 64, 96, 128, 192, 256 }) har.addItem(juce::String(h), h); har.setSelectedId(96, juce::dontSendNotification);
        dur.setSliderStyle(juce::Slider::LinearBar); dur.setRange(0.5, 30, 0.5); dur.setValue(6); tilt.setSliderStyle(juce::Slider::LinearBar); tilt.setRange(0, 1.5, 0.05); tilt.setValue(0.4);
        gam.setSliderStyle(juce::Slider::LinearBar); gam.setRange(0.4, 3, 0.05); gam.setValue(1.2); inv.setButtonText("invert"); loop.setButtonText("seamless loop"); loop.setToggleState(true, juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::Component*>{ &chooseB, &renderB, &mode, &note, &har, &dur, &tilt, &gam, &inv, &loop }) addAndMakeVisible(*c);
        chooseB.onClick = [this] { choose(); }; renderB.onClick = [this] { render(); }; resized();
    }
    ~ImageAudioDialog() override {}
    void resized() override {
        auto r = getLocalBounds().reduced(10); info.setBounds(r.removeFromTop(34)); auto a = r.removeFromTop(26); chooseB.setBounds(a.removeFromLeft(150)); a.removeFromLeft(8); fileL.setBounds(a); r.removeFromTop(6);
        auto row = [&](juce::Label& l, juce::Component& c, int w) { auto q = r.removeFromTop(26); l.setBounds(q.removeFromLeft(90)); c.setBounds(q.removeFromLeft(w)); r.removeFromTop(4); };
        row(modeL, mode, 280); row(durL, dur, 200); row(noteL, note, 90); row(harL, har, 90); row(tiltL, tilt, 200); row(gamL, gam, 200);
        auto q = r.removeFromTop(26); inv.setBounds(q.removeFromLeft(90)); loop.setBounds(q.removeFromLeft(150)); q.removeFromLeft(8); renderB.setBounds(q.removeFromLeft(170)); status.setBounds(r.removeFromTop(30));
    }
    void paint(juce::Graphics& g) override { Dialog::paint(g); if (img.isValid()) g.drawImageWithin(img, getWidth() - 140, 44, 130, 120, juce::RectanglePlacement::centred); }
    void choose() {
        chooser = std::make_shared<juce::FileChooser>("Choose an image", juce::File(), "*.png;*.jpg;*.jpeg;*.gif;*.bmp");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [alive = juce::Component::SafePointer<ImageAudioDialog>(this)](const juce::FileChooser& fc) {
            if (!alive) return; auto f = fc.getResult(); if (!f.existsAsFile()) return; auto im = juce::ImageFileFormat::loadFrom(f);
            if (!im.isValid()) { alive->status.setText("Could not read that image.", juce::dontSendNotification); return; }
            alive->img = im; alive->imgName = f.getFileNameWithoutExtension(); alive->fileL.setText(f.getFileName() + "  " + juce::String(im.getWidth()) + " x " + juce::String(im.getHeight()), juce::dontSendNotification);
            alive->renderB.setEnabled(true); alive->repaint(); });
    }
    void render() {
        if (!img.isValid()) return; const int w = img.getWidth(), h = img.getHeight(); std::vector<uint8_t> px((size_t) w * h * 4); juce::Image::BitmapData bd(img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { const auto c = bd.getPixelColour(x, y); uint8_t* q = &px[((size_t) y * w + x) * 4]; q[0] = c.getRed(); q[1] = c.getGreen(); q[2] = c.getBlue(); q[3] = c.getAlpha(); }
        const int n = note.getSelectedId(); auto* e = store.engine(); const int sr = (int) (e ? vv_engine_sample_rate(e) : 48000.0);
        juce::MouseCursor::showWaitCursor(); VVAudio* a = vv_audio_from_image(px.data(), w, h, sr, dur.getValue(), n, har.getSelectedId(), tilt.getValue(), gam.getValue(), inv.getToggleState() ? 1 : 0, loop.getToggleState() ? 1 : 0, mode.getSelectedId() == 2 ? 1 : 0);
        juce::MouseCursor::hideWaitCursor(); if (!a) { status.setText("Could not render this image.", juce::dontSendNotification); return; }
        const auto nm = uniqueSampleName(store, "Image: " + imgName); putSample(store, nm, wavOf(a)); const double secs = vv_audio_length(a) / (double) sr; vv_audio_free(a);
        store.setMany({ { path + ".name", nm }, { path + ".origName", juce::var() }, { path + ".root", n }, { path + ".loop", loop.getToggleState() } });
        status.setText("Rendered " + juce::String(secs, 2) + " s into the slot (root note " + note.getText() + ").", juce::dontSendNotification); if (changed) changed();
    }
    Store& store; juce::String path, imgName; std::function<void()> changed; juce::Image img; std::shared_ptr<juce::FileChooser> chooser;
    juce::Label info, status, fileL, modeL, durL, noteL, harL, tiltL, gamL; juce::TextButton chooseB, renderB; juce::ComboBox mode, note, har; juce::Slider dur, tilt, gam; juce::ToggleButton inv, loop;
};
}
void openRecord(Store& s, int slot, juce::Component& a, std::function<void()> c) { Dialog::show(a, new RecordDialog(s, slot, std::move(c))); }
void openImageAudio(Store& s, int slot, juce::Component& a, std::function<void()> c) { Dialog::show(a, new ImageAudioDialog(s, slot, std::move(c))); }
std::unique_ptr<juce::Component> makeRecord(Store& s, int slot, std::function<void()> c) { return std::make_unique<RecordDialog>(s, slot, std::move(c)); }
std::unique_ptr<juce::Component> makeImageAudio(Store& s, int slot, std::function<void()> c) { return std::make_unique<ImageAudioDialog>(s, slot, std::move(c)); }

}}
