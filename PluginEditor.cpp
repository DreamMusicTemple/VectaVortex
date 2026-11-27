#include "PluginEditor.h"
#include "NavIcons.h"
#include "DawRecorderUi.h"
#include "Screens.h"
#include "ScreensMixer.h"
#include "XYPads.h"
#include "Scales.h"
#include "Assets.h"
#include "Appearance.h"
#include "MidiLearn.h"
#include "XfxEditor.h"
#include <deque>
#include <array>
#include <juce_dsp/juce_dsp.h>

namespace vv {

// ------------------------------------------------------------------ scope: waveform / X-Y / radial / spectrogram (2D + 3D), with the web app's line + glow style, fed by the processor's tap
class ScopeView : public juce::Component, private juce::Timer {
public:
    enum Mode { Wave = 0, XY = 1, Radial = 2, Spectrogram = 3, Spectrogram3D = 4 };
    explicit ScopeView(Processor& p) : proc(p) {
        send.setButtonText("SEND TO VEKTUR SONIC"); send.setClickingTogglesState(true); send.setToggleState(p.sendToVectaSonic.load(), juce::dontSendNotification);
        send.setTooltip("Send Left and Right as two separate mono signals for Vektur Sonic (load it as an insert after Vektur Vortex, e.g. in AUM)");
        send.onClick = [this] { proc.sendToVectaSonic.store(send.getToggleState()); updateLabels(); };
        const struct { juce::TextButton* b; const char* t; int mode; } ms[] = { { &xyBtn, "XY", XY }, { &radialBtn, "RADIAL", Radial }, { &specBtn, "SPEC", Spectrogram }, { &spec3dBtn, "3D", Spectrogram3D } };
        for (auto& m : ms) { m.b->setButtonText(m.t); m.b->setClickingTogglesState(true); const int md = m.mode; auto* bp = m.b;
            m.b->onClick = [this, md, bp] { proc.scopeMode.store(bp->getToggleState() ? md : Wave); syncButtons(); }; addAndMakeVisible(*m.b); }
        optsBtn.setButtonText("OPTIONS"); optsBtn.setTooltip("Spectrogram options: FFT size, window, palette, band scale, source, peak hold"); optsBtn.onClick = [this] { showSpecOptions(); }; addAndMakeVisible(optsBtn);
        styleBtn.setButtonText("STYLE"); styleBtn.onClick = [this] { auto* c = new StylePanel(proc.store()); juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(c), styleBtn.getScreenBounds(), nullptr); };
        fullBtn.setButtonText("FULLSCREEN"); fullBtn.setClickingTogglesState(true); fullBtn.onClick = [this] { if (onFullscreen) onFullscreen(fullBtn.getToggleState()); };
        powerBtn.setButtonText("OFF"); powerBtn.setClickingTogglesState(true); powerBtn.setTooltip("Scope power. OFF: the scope draws nothing and uses no processing or memory. ON: the scope runs.");
        powerBtn.onClick = [this] { proc.store().setGlobal("scopePower", powerBtn.getToggleState() ? 1.0 : 0.0); applyPower(); };
        addAndMakeVisible(powerBtn); addAndMakeVisible(send); addAndMakeVisible(styleBtn); addAndMakeVisible(fullBtn); syncButtons(); updateLabels(); applyPower();
    }
    ~ScopeView() override { proc.scopeWantMask.fetch_and(~1); if (isSpec_) proc.scopeWantMask.fetch_and(~2); }
    // Scope power: OFF by default (global "scopePower"). Off stops the timer, releases every per-oscillator tap and stops the processor's tap copy.
    bool powered() const { return isSpec_ || proc.store().global("scopePower", 0.0) != 0.0; }
    void setSpectrogramTab(bool b) { isSpec_ = b; applyPower(); }
    void applyPower() {
        const bool on = powered(), pw = proc.store().global("scopePower", 0.0) != 0.0;
        powerBtn.setVisible(!isSpec_); powerBtn.setToggleState(pw, juce::dontSendNotification); powerBtn.setButtonText(pw ? "ON" : "OFF");
        powerBtn.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff7dffb0)); powerBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffff8a8a));
        for (auto* b : { &send, &xyBtn, &radialBtn, &specBtn, &spec3dBtn, &styleBtn, &optsBtn }) b->setVisible(on);
        if (on) { if (isSpec_ ? isVisible() : true) proc.scopeWantMask.fetch_or(isSpec_ ? 2 : 1); startTimerHz(30); }
        else {
            stopTimer(); proc.scopeWantMask.fetch_and(~1); hist.clear();
            for (int l = 0; l < 5; ++l) if (tapMask[l] != 0) { tapMask[l] = 0; if (proc.engine()) vv_engine_scope_tap_mask(proc.engine(), l, 0u); }
        }
        resized(); repaint();
    }
    void visibilityChanged() override { if (isSpec_) { if (isVisible()) proc.scopeWantMask.fetch_or(2); else proc.scopeWantMask.fetch_and(~2); } }
    // Mono / silence check on the last ~1000 samples; test hooks read these.
    enum Warn { None, Mono, Silent };
    Warn warning() const { return warn; }
    bool sending() const { return proc.sendToVectaSonic.load(); }
    std::function<void(bool)> onFullscreen;
    void showMode(int m) { proc.scopeMode.store(m); syncButtons(); repaint(); }   // SPECTROGRAM tab
    int mode() const { return proc.scopeMode.load(); }
    // the web's line / glow style: thickness, glow, and the two colours (instrument-wide settings)
    juce::Colour lineCol() const { return juce::Colour((juce::uint32) proc.store().global("scopeLineColor", (double) col::cyan.getARGB())); }
    juce::Colour glowCol() const { return juce::Colour((juce::uint32) proc.store().global("scopeGlowColor", (double) col::cyan.getARGB())); }
    float thick() const { return (float) proc.store().global("scopeThick", 1.4); }
    float glow() const { return (float) proc.store().global("scopeGlow", 0.25); }
    struct StylePanel : juce::Component {
        explicit StylePanel(Store& st) : store(st) {
            auto mk = [this](juce::Slider& s, juce::Label& l, const char* t, double lo, double hi, const char* key, double def) { l.setText(t, juce::dontSendNotification); addAndMakeVisible(l);
                s.setSliderStyle(juce::Slider::LinearHorizontal); s.setRange(lo, hi, 0); s.setValue(store.global(key, def), juce::dontSendNotification); s.onValueChange = [this, &s, key] { store.setGlobal(key, s.getValue()); }; addAndMakeVisible(s); };
            mk(thickS, thickL, "Line thickness", 0.5, 6, "scopeThick", 1.4); mk(glowS, glowL, "Glow", 0, 1, "scopeGlow", 0.25);
            lineB.setButtonText("Line colour"); glowB.setButtonText("Glow colour"); resetB.setButtonText("RESET");
            auto pick = [this](const char* key, juce::Component& anchor) { auto* cs = new juce::ColourSelector(juce::ColourSelector::showColourspace | juce::ColourSelector::showSliders); cs->setSize(240, 220);
                cs->setCurrentColour(juce::Colour((juce::uint32) store.global(key, (double) col::cyan.getARGB()))); struct L : juce::ChangeListener { L(Store& s, juce::ColourSelector& c, juce::String k) : st(s), cs(c), key(k) {} void changeListenerCallback(juce::ChangeBroadcaster*) override { st.setGlobal(key, (double) cs.getCurrentColour().getARGB()); } Store& st; juce::ColourSelector& cs; juce::String key; };
                auto* l = new L(store, *cs, key); cs->addChangeListener(l); cs->getProperties().set("l", 0); struct Owner : juce::Component { Owner(juce::ColourSelector* c, L* l_) : cs(c), l(l_) { addAndMakeVisible(*cs); setSize(240, 220); } ~Owner() override { cs->removeChangeListener(l); delete l; delete cs; } void resized() override { cs->setBounds(getLocalBounds()); } juce::ColourSelector* cs; L* l; };
                juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(new Owner(cs, l)), anchor.getScreenBounds(), nullptr); };
            lineB.onClick = [this, pick] { pick("scopeLineColor", lineB); }; glowB.onClick = [this, pick] { pick("scopeGlowColor", glowB); };
            resetB.onClick = [this] { store.setGlobal("scopeThick", 1.4); store.setGlobal("scopeGlow", 0.25); store.setGlobal("scopeLineColor", (double) col::cyan.getARGB()); store.setGlobal("scopeGlowColor", (double) col::cyan.getARGB()); thickS.setValue(1.4, juce::dontSendNotification); glowS.setValue(0.25, juce::dontSendNotification); };
            addAndMakeVisible(lineB); addAndMakeVisible(glowB); addAndMakeVisible(resetB); setSize(280, 160);
        }
        void resized() override { auto r = getLocalBounds().reduced(8); auto row = r.removeFromTop(26); thickL.setBounds(row.removeFromLeft(100)); thickS.setBounds(row); row = r.removeFromTop(26); glowL.setBounds(row.removeFromLeft(100)); glowS.setBounds(row); r.removeFromTop(6);
            auto br = r.removeFromTop(30); lineB.setBounds(br.removeFromLeft(90)); glowB.setBounds(br.removeFromLeft(96).translated(4, 0)); resetB.setBounds(br.removeFromRight(70)); }
        Store& store; juce::Slider thickS, glowS; juce::Label thickL, glowL; juce::TextButton lineB, glowB, resetB;
    };
    // ---- oscillator lanes (Zones tab SCOPE pill -> layers.<L>.oscs.<n>.scope)
    struct Lane { juce::String label; int layer, group; juce::Colour col; };
    std::vector<Lane> laneList() {
        std::vector<Lane> out; Store& st = proc.store();
        static const char* keys[] = { "WT", "FM", "SMP", "PRO", "SID" }; static const juce::uint32 cols[] = { 0xff7fd7ff, 0xffff8fc8, 0xff9dff9d, 0xffffd35c, 0xffc9a0ff };
        for (int l = 0; l < 5; ++l) { const int n = st.count(juce::String("layers.") + keys[l] + ".oscs");
            for (int o = 0; o < n && o < 16; ++o) {
                if (l == 1 && o != 0 && o != 4) continue;   // FM: one lane per engine (A = operators 1-4, B = 5-8)
                if (st.num(juce::String("layers.") + keys[l] + ".oscs." + juce::String(o) + ".scope", 0) == 0) continue;
                const int grp = l == 1 ? o / 4 : o; out.push_back({ juce::String(keys[l]) + " " + (l == 1 ? juce::String(o == 0 ? "A" : "B") : juce::String(o + 1)), l, grp, juce::Colour(cols[l]) }); } }
        return out;
    }
    void syncTapMasks(const std::vector<Lane>& lanes) {
        unsigned m[5] = {}; for (auto& ln : lanes) m[ln.layer] |= 1u << ln.group;
        for (int l = 0; l < 5; ++l) if (m[l] != tapMask[l]) { tapMask[l] = m[l]; if (proc.engine()) vv_engine_scope_tap_mask(proc.engine(), l, m[l]); }
    }
    void paintLanes(juce::Graphics& g, juce::Rectangle<float> area) {
        const auto lanes = laneList(); const int N = (int) lanes.size(); syncTapMasks(lanes); const float laneH = area.getHeight() / (float) N;
        float buf[2048];
        for (int v = 0; v < N; ++v) {
            auto lr = area.withTop(area.getY() + laneH * v).withHeight(laneH); const auto& ln = lanes[(size_t) v];
            if (v > 0) { g.setColour(juce::Colours::white.withAlpha(0.1f)); g.drawHorizontalLine((int) lr.getY(), lr.getX(), lr.getRight()); }
            g.setColour(juce::Colours::white.withAlpha(0.45f)); g.setFont(VVLookAndFeel::mono(juce::jlimit(9.f, 13.f, laneH * 0.14f))); g.drawText(ln.label, lr.getX() + 6, lr.getY() + 3, 90, 16, juce::Justification::centredLeft);
            if (!proc.engine() || !vv_engine_scope_tap_read(proc.engine(), ln.layer, ln.group, buf, 2048)) continue;
            float pk = 0; for (int i = 0; i < 2048; ++i) pk = juce::jmax(pk, std::abs(buf[i]));
            float& hold = lanePk[ln.layer * 16 + ln.group]; hold = pk > hold ? pk : hold * 0.97f + pk * 0.03f;
            const float gain = pk < 0.0008f ? 0.f : juce::jmin(40.f, 0.9f / juce::jmax(hold, 0.02f)), thr = juce::jmin(0.02f, pk * 0.1f);
            int t0 = 0; for (int i = 1; i < 1024; ++i) if (buf[i - 1] < -thr && buf[i] >= -thr && buf[i + 1] > buf[i]) { t0 = i; break; }
            juce::Path p; const float mid = lr.getCentreY();
            for (int i = 0; i < 1024; ++i) { const float px = lr.getX() + lr.getWidth() * (float) i / 1023.f, py = mid - juce::jlimit(-1.f, 1.f, buf[t0 + i] * gain) * laneH * 0.4f; if (i == 0) p.startNewSubPath(px, py); else p.lineTo(px, py); }
            g.setColour(ln.col.withAlpha(glow())); g.strokePath(p, juce::PathStrokeType(thick() + 2.6f)); g.setColour(ln.col); g.strokePath(p, juce::PathStrokeType(thick()));
        }
    }
    unsigned tapMask[5] = {}; float lanePk[80] = {};
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat(); g.setColour(juce::Colour(0xff060708)); g.fillRoundedRectangle(r, 8.f); g.setColour(col::line); g.drawRoundedRectangle(r.reduced(0.5f), 8.f, 1.f);
        if (!powered()) {   // Scope OFF: nothing is drawn or computed
            g.setColour(col::dim); g.setFont(VVLookAndFeel::mono(15.f)); g.drawText("SCOPE OFF", getLocalBounds().withTrimmedBottom(26), juce::Justification::centred);
            g.setFont(VVLookAndFeel::mono(11.f)); g.drawText("The scope is switched off and uses no processing or memory. Press ON to start it.", getLocalBounds().withTrimmedTop(26), juce::Justification::centred); return; }
        const int w = proc.scopeWrite.load(std::memory_order_acquire), N = Processor::kScope;
        const int md = mode(); juce::Path path;
        auto area = r.withTrimmedTop(52.f);
        auto trigStart = [&] { int start = (w - 2048 + N) & (N - 1); for (int i = 0; i < 1024; ++i) { const int a = (start + i) & (N - 1), b = (a + 1) & (N - 1); if (proc.scopeL[a] <= 0 && proc.scopeL[b] > 0) { start = a; break; } } return start; };
        if (md == Spectrogram || md == Spectrogram3D) { paintSpectrogram(g, area, md == Spectrogram3D); }
        else {
            if (md == XY) {   // Left = X, Right = Y: exactly what Vektur Sonic's oscilloscope receives
                const float s = juce::jmin(area.getWidth(), area.getHeight()) * 0.44f, cx = area.getCentreX(), cy = area.getCentreY();
                for (int i = 0; i < 1024; ++i) { const int k = (w - 1024 + i + N) & (N - 1); const float L = proc.scopeL[k], R = proc.scopeR[k];
                    const float px = cx + L * s, py = cy - R * s; if (i == 0) path.startNewSubPath(px, py); else path.lineTo(px, py); }
            } else if (md == Radial) {   // the waveform wrapped around a circle
                const float s = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f, cx = area.getCentreX(), cy = area.getCentreY(); const int start = trigStart();
                for (int i = 0; i <= 1024; ++i) { const int k = (start + (i % 1024)) & (N - 1); const float rad = s * (0.55f + proc.scopeL[k] * 0.4f), a = juce::MathConstants<float>::twoPi * (float) i / 1024.f;
                    const float px = cx + std::cos(a) * rad, py = cy + std::sin(a) * rad; if (i == 0) path.startNewSubPath(px, py); else path.lineTo(px, py); }
            } else if (!laneList().empty()) {   // Zones-tab SCOPE pills: one stabilised trace per switched-on oscillator, the Scope divided into equal lanes (web parity)
                paintLanes(g, area);
            } else {
                const int start = trigStart();   // trigger on a rising zero crossing so periodic sounds hold still
                for (int i = 0; i < 1024; ++i) { const int k = (start + i) & (N - 1); const float px = area.getX() + area.getWidth() * (float) i / 1023.f, py = area.getCentreY() - proc.scopeL[k] * area.getHeight() * 0.45f; if (i == 0) path.startNewSubPath(px, py); else path.lineTo(px, py); }
            }
            const float th = thick(); g.setColour(glowCol().withAlpha(glow())); g.strokePath(path, juce::PathStrokeType(th + 2.6f)); g.setColour(lineCol()); g.strokePath(path, juce::PathStrokeType(th));
        }
        static const char* names[] = { "SCOPE", "X-Y  (L = X, R = Y)", "RADIAL", "SPECTROGRAM", "SPECTROGRAM 3D" };
        g.setColour(col::dim); g.setFont(VVLookAndFeel::mono(11.f)); g.drawText(names[juce::jlimit(0, 4, md)], getLocalBounds().reduced(8, 6), juce::Justification::topLeft);
        juce::String msg;
        if (proc.outputIsMono.load() && sending()) msg = "HOST OUTPUT IS MONO - Vektur Sonic cannot receive two signals. Set the output to stereo.";
        else if (warn == Mono && sending()) msg = "MONO SIGNAL - Left and Right are identical, so Vektur Sonic will only draw a diagonal line. Pan, detune or use stereo FX.";
        else if (warn == Silent && sending()) msg = "NO SIGNAL - nothing is being sent to Vektur Sonic. Play a note.";
        if (msg.isNotEmpty()) { auto b = getLocalBounds().reduced(8).removeFromBottom(34).toFloat(); g.setColour(juce::Colour(0xe63c2600)); g.fillRoundedRectangle(b, 6.f); g.setColour(juce::Colour(0xffffb347)); g.drawRoundedRectangle(b, 6.f, 1.f);
            g.setFont(VVLookAndFeel::mono(11.f)); g.drawFittedText(juce::String::fromUTF8("\xe2\x9a\xa0 ") + msg, b.toNearestInt().reduced(8, 2), juce::Justification::centredLeft, 2); }
    }
    void resized() override {
        auto r = getLocalBounds().reduced(6, 4); auto r1 = r.removeFromTop(22); powerBtn.setBounds(r1.removeFromLeft(64).reduced(1, 0)); send.setBounds(r1.removeFromRight(190)); r.removeFromTop(2); auto r2 = r.removeFromTop(22);
        const int bw = juce::jlimit(36, 92, r2.getWidth() / 7); for (auto* b : { &xyBtn, &radialBtn, &specBtn, &spec3dBtn, &optsBtn, &styleBtn, &fullBtn }) b->setBounds(r2.removeFromLeft(bw).reduced(1, 0));
    }
private:
    static constexpr int kBins = 128, kHist = 160;
    void syncButtons() { const int m = mode(); xyBtn.setToggleState(m == XY, juce::dontSendNotification); radialBtn.setToggleState(m == Radial, juce::dontSendNotification); specBtn.setToggleState(m == Spectrogram || m == Spectrogram3D, juce::dontSendNotification); spec3dBtn.setToggleState(m == Spectrogram3D, juce::dontSendNotification); spec3dBtn.setEnabled(m >= Spectrogram); }
    void updateLabels() { send.setButtonText(proc.sendToVectaSonic.load() ? "SENDING L/R" : "SEND TO VEKTUR SONIC"); send.setColour(juce::TextButton::textColourOnId, col::cyan); }
    // Spectrogram options (round 26, stage 3), kept as globals so they are saved with the plugin state: specFft 0/1/2 = 1024/2048/4096, specWin 0..3 = Hann / Hamming / Blackman / Rectangular,
    // specLin 0 log / 1 linear band scale, specPal 0..3 = Heat / Ice / Fire / Mono, specSrc 0..4 = L+R / L / R / Mid / Side, specPeak 0/1 = peak-hold markers on the newest column.
    juce::dsp::FFT& fftFor(int n) { return n == 1024 ? fft10 : n == 4096 ? fft12 : fft11; }
    void pushSpectrum() {
        auto& st = proc.store(); const int N2 = 1024 << juce::jlimit(0, 2, (int) st.global("specFft", 1)), win = (int) st.global("specWin", 0), src = (int) st.global("specSrc", 0); const bool lin = st.global("specLin", 0) != 0;
        const int w = proc.scopeWrite.load(std::memory_order_acquire), N = Processor::kScope; std::fill(fftBuf.begin(), fftBuf.end(), 0.f);
        for (int i = 0; i < N2; ++i) {
            const int k = (w - N2 + i + N) & (N - 1); const float t = (float) i / (float) (N2 - 1), tp = juce::MathConstants<float>::twoPi * t; float wv = 1.f;
            if (win == 0) wv = 0.5f - 0.5f * std::cos(tp); else if (win == 1) wv = 0.54f - 0.46f * std::cos(tp); else if (win == 2) wv = 0.42f - 0.5f * std::cos(tp) + 0.08f * std::cos(2.f * tp);
            const float L = proc.scopeL[k], R = proc.scopeR[k]; const float x = src == 1 ? L : src == 2 ? R : src == 3 ? 0.5f * (L + R) : src == 4 ? 0.5f * (L - R) : 0.5f * (L + R);
            fftBuf[(size_t) i] = x * wv;
        }
        fftFor(N2).performFrequencyOnlyForwardTransform(fftBuf.data()); std::array<float, kBins> col{}; const int half = N2 / 2;
        for (int b = 0; b < kBins; ++b) {
            const int lo = lin ? b * half / kBins : (int) std::pow((double) (half - 1), (double) b / kBins), hi = juce::jmax(lo + 1, lin ? (b + 1) * half / kBins : (int) std::pow((double) (half - 1), (double) (b + 1) / kBins));
            float m = 0; for (int q = lo; q < hi && q < half; ++q) m = juce::jmax(m, fftBuf[(size_t) q]);
            const float db = 20.f * std::log10(m / ((float) N2 / 4.f) + 1e-6f); col[(size_t) b] = juce::jlimit(0.f, 1.f, (db + 90.f) / 90.f);
            peakHold[(size_t) b] = juce::jmax(col[(size_t) b], peakHold[(size_t) b] - 0.004f);
        }
        hist.push_back(col); while ((int) hist.size() > kHist) hist.pop_front();
    }
    juce::Colour heat(float v) const {
        const int pal = (int) proc.store().global("specPal", 0); const float c = juce::jlimit(0.f, 1.f, v * 1.6f);
        switch (pal) { case 1: return juce::Colour::fromHSV(0.62f - 0.12f * v, 0.55f * (1.f - v * 0.6f) + 0.2f, c, 1.f);          // ice: blue to white
                       case 2: return juce::Colour::fromHSV(0.0f + 0.13f * v, 0.95f - 0.4f * v, c, 1.f);                      // fire: red to yellow
                       case 3: return juce::Colour::fromHSV(0.f, 0.f, c, 1.f);                                                  // mono
                       default: return juce::Colour::fromHSV(0.66f - 0.66f * v, 0.9f, c, 1.f); }
    }
    void showSpecOptions() {
        auto& st = proc.store(); juce::PopupMenu m, fftM, winM, palM, srcM;
        const char* fftN[] = { "1024", "2048", "4096" }; for (int i = 0; i < 3; ++i) fftM.addItem(100 + i, fftN[i], true, (int) st.global("specFft", 1) == i);
        const char* winN[] = { "Hann", "Hamming", "Blackman", "Rectangular" }; for (int i = 0; i < 4; ++i) winM.addItem(110 + i, winN[i], true, (int) st.global("specWin", 0) == i);
        const char* palN[] = { "Heat", "Ice", "Fire", "Mono" }; for (int i = 0; i < 4; ++i) palM.addItem(120 + i, palN[i], true, (int) st.global("specPal", 0) == i);
        const char* srcN[] = { "Left + Right", "Left", "Right", "Mid", "Side" }; for (int i = 0; i < 5; ++i) srcM.addItem(130 + i, srcN[i], true, (int) st.global("specSrc", 0) == i);
        m.addSubMenu("FFT size", fftM); m.addSubMenu("Window", winM); m.addSubMenu("Palette", palM); m.addSubMenu("Source", srcM);
        m.addItem(140, "Logarithmic band scale", true, st.global("specLin", 0) == 0); m.addItem(141, "Linear band scale", true, st.global("specLin", 0) != 0); m.addItem(142, "Peak-hold markers", true, st.global("specPeak", 0) != 0);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&optsBtn), [this](int r) {
            auto& s2 = proc.store(); if (r >= 100 && r < 110) s2.setGlobal("specFft", r - 100); else if (r >= 110 && r < 120) s2.setGlobal("specWin", r - 110); else if (r >= 120 && r < 130) s2.setGlobal("specPal", r - 120);
            else if (r >= 130 && r < 140) s2.setGlobal("specSrc", r - 130); else if (r == 140) s2.setGlobal("specLin", 0); else if (r == 141) s2.setGlobal("specLin", 1); else if (r == 142) s2.setGlobal("specPeak", s2.global("specPeak", 0) != 0 ? 0 : 1);
            hist.clear(); });
    }
    void paintSpectrogram(juce::Graphics& g, juce::Rectangle<float> area, bool threeD) {
        if (hist.empty()) return; const int n = (int) hist.size();
        if (!threeD) { const float cw = area.getWidth() / (float) kHist, bh = area.getHeight() / (float) kBins;
            for (int x = 0; x < n; ++x) for (int b = 0; b < kBins; ++b) { const float v = hist[(size_t) x][(size_t) b]; if (v < 0.02f) continue; g.setColour(heat(v)); g.fillRect(area.getX() + (float) (x + kHist - n) * cw, area.getBottom() - (float) (b + 1) * bh, cw + 0.6f, bh + 0.6f); } return; }
        if (!threeD && proc.store().global("specPeak", 0) != 0) { const float bh2 = area.getHeight() / (float) kBins; g.setColour(juce::Colours::white.withAlpha(0.85f)); for (int bb = 0; bb < kBins; ++bb) { const float pv = peakHold[(size_t) bb]; if (pv > 0.05f) g.fillRect(area.getRight() - 4.f, area.getBottom() - (float) (bb + 1) * bh2, 4.f, juce::jmax(1.f, bh2 * 0.6f)); } }
        const int rows = juce::jmin(n, 48); const float dx = 0.28f * area.getWidth() / 48.f, dy = 0.34f * area.getHeight() / 48.f, w = area.getWidth() * 0.68f, h = area.getHeight() * 0.5f;
        for (int r = 0; r < rows; ++r) { const auto& col = hist[(size_t) (n - 1 - r)]; juce::Path p; const float ox = area.getX() + 10.f + (float) r * dx, oy = area.getBottom() - 14.f - (float) r * dy;
            p.startNewSubPath(ox, oy); for (int b = 0; b < kBins; ++b) p.lineTo(ox + w * (float) b / (kBins - 1), oy - col[(size_t) b] * h); p.lineTo(ox + w, oy); p.closeSubPath();
            g.setColour(juce::Colour(0xff060708)); g.fillPath(p); g.setColour(lineCol().withAlpha(juce::jmap((float) r, 0.f, (float) rows, 1.f, 0.25f))); g.strokePath(p, juce::PathStrokeType(thick() * 0.7f)); }
    }
    void timerCallback() override {
        if (!isShowing() || !powered()) return;
        if (mode() >= Spectrogram) pushSpectrum();
        if (++tick_ % 9 == 0) {   // ~3.3 Hz
            const int w = proc.scopeWrite.load(std::memory_order_acquire), N = Processor::kScope; float pk = 0, diff = 0, sum = 0;
            for (int i = 0; i < 1024; ++i) { const int k = (w - 1024 + i + N) & (N - 1); const float L = proc.scopeL[k], R = proc.scopeR[k]; pk = juce::jmax(pk, std::abs(L), std::abs(R)); diff += std::abs(L - R); sum += std::abs(L) + std::abs(R); }
            if (pk < 0.004f) warn = ++silent_ > 6 ? Silent : None; else { silent_ = 0; warn = (sum > 0 && diff / sum < 0.004f) ? Mono : None; }
            syncButtons();
        }
        repaint();
    }
    Processor& proc; Warn warn = None; int tick_ = 0, silent_ = 0;
    juce::TextButton powerBtn, send, xyBtn, radialBtn, specBtn, spec3dBtn, styleBtn, fullBtn; bool isSpec_ = false;
    juce::dsp::FFT fft10{ 10 }, fft11{ 11 }, fft12{ 12 }; std::array<float, 8192> fftBuf{}; std::array<float, 128> peakHold{}; juce::TextButton optsBtn; std::deque<std::array<float, 128>> hist;
};

// A labelled horizontal fader bound to a global (WT/FM mixer, Sample-position fader). notches > 0 draws numbered sample notches.
class MixerFader : public Control {
public:
    MixerFader(Store& s, const juce::String& label, const juce::String& path, double lo, double hi, double def, int notches, const juce::String& toggleLabel = {}, const juce::String& togglePath = {})
        : Control(s, path), lo_(lo), hi_(hi), def_(def), notches_(notches), togglePath_(togglePath) {
        title.setText(label, juce::dontSendNotification); title.setFont(VVLookAndFeel::mono(11.f)); title.setColour(juce::Label::textColourId, col::dim); addAndMakeVisible(title);
        sl.setSliderStyle(juce::Slider::LinearHorizontal); sl.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); sl.setRange(lo, hi, 0); sl.setDoubleClickReturnValue(true, def);
        sl.onValueChange = [this] { if (!updating_) store.set(path_, sl.getValue()); }; addAndMakeVisible(sl);
        if (toggleLabel.isNotEmpty()) { tg.setButtonText(toggleLabel); tg.setClickingTogglesState(true); tg.onClick = [this] { store.set(togglePath_, tg.getToggleState()); }; addAndMakeVisible(tg); }
        refresh();
    }
    void refresh() override { updating_ = true; sl.setValue(store.num(path_, def_), juce::dontSendNotification); if (togglePath_.isNotEmpty()) tg.setToggleState(store.flag(togglePath_), juce::dontSendNotification); updating_ = false; }
    void resized() override { auto r = getLocalBounds(); auto top = r.removeFromTop(18); if (tg.isVisible()) tg.setBounds(top.removeFromRight(86)); title.setBounds(top); sl.setBounds(r.removeFromTop(24)); notchArea = r; }
    void paint(juce::Graphics& g) override {
        if (notches_ <= 0) return; g.setColour(col::dim); g.setFont(VVLookAndFeel::mono(11.f));
        const float w = (float) notchArea.getWidth() / (float) notches_; for (int i = 0; i < notches_; ++i) g.drawText(juce::String(i + 1), (int) (notchArea.getX() + w * i), notchArea.getY(), (int) w, 14, juce::Justification::centred);
    }
    juce::Point<int> preferredSize() const override { return { notches_ ? 315 : 169, notches_ ? 64 : 48 }; }
private:
    double lo_, hi_, def_; int notches_; juce::String togglePath_; juce::Label title; juce::Slider sl; juce::TextButton tg; juce::Rectangle<int> notchArea;
};

// ------------------------------------------------------------------ keyboard with the web app's scale highlight / scale lock / invert / note labels
struct ScaleKeyboard : juce::MidiKeyboardComponent {
    ScaleKeyboard(Store& s, juce::MidiKeyboardState& st) : juce::MidiKeyboardComponent(st, juce::MidiKeyboardComponent::horizontalKeyboard), store(s) {}
    int idx() const { return (int) store.global("scaleIdx", 0); }
    int root() const { return (int) store.global("scaleRoot", 0); }
    bool locked() const { return store.global("scaleLock", 0) != 0 && idx() > 0; }
    bool inScale(int n) const { return kbInScale(idx(), root(), n); }
    bool allowed(int n) const { return !locked() || inScale(n); }
    bool mouseDownOnKey(int n, const juce::MouseEvent&) override { return allowed(n); }
    bool mouseDraggedToKey(int n, const juce::MouseEvent&) override { return allowed(n); }
    void drawWhiteNote(int n, juce::Graphics& g, juce::Rectangle<float> a, bool down, bool over, juce::Colour line, juce::Colour) override {
        const bool inv = store.global("invertKeys", 0) != 0; juce::Colour c = inv ? juce::Colour(0xff0a0b0f) : juce::Colour(0xffdbdee5);   // Vektur key colours
        if (locked()) c = inScale(n) ? juce::Colour(0xffb8f0a8) : juce::Colour(0xff4a4a4a); else if (inScale(n)) c = c.interpolatedWith(juce::Colour(0xff7be0a0), 0.45f);
        if (down) c = col::cyan.withAlpha(0.85f); else if (over && allowed(n)) c = c.brighter(0.15f);
        g.setColour(c); g.fillRect(a); g.setColour(line); g.drawRect(a, 1.f); label(g, n, a, inv ? juce::Colours::white : juce::Colours::black, true);
    }
    void drawBlackNote(int n, juce::Graphics& g, juce::Rectangle<float> a, bool down, bool over, juce::Colour) override {
        const bool inv = store.global("invertKeys", 0) != 0; juce::Colour c = inv ? juce::Colour(0xffeef0f4) : juce::Colour(0xff1b1f2a);
        if (locked()) c = inScale(n) ? juce::Colour(0xff1a5c2a) : juce::Colour(0xff4a4a4a); else if (inScale(n)) c = c.interpolatedWith(juce::Colour(0xff2f9a55), 0.5f);
        if (down) c = col::cyan.withAlpha(0.9f); else if (over && allowed(n)) c = c.brighter(0.15f);
        g.setColour(c); g.fillRect(a); label(g, n, a, inv ? juce::Colours::black : juce::Colours::white, false);
    }
    void label(juce::Graphics& g, int n, juce::Rectangle<float> a, juce::Colour tc, bool white) {
        static const char* nn[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        if (store.global("keyLabels", 0) != 0 || (white && n % 12 == 0)) { g.setColour(tc.withAlpha(0.7f)); g.setFont(VVLookAndFeel::mono(10.f)); g.drawText(juce::String(nn[n % 12]) + juce::String(n / 12 - 1), a.withTrimmedBottom(3), juce::Justification::centredBottom); }
    }
    Store& store;
};

// ------------------------------------------------------------------ pitch-bend targets (web: TARGETS next to the pitch wheel)
struct BendTargetsPanel : juce::Component {
    explicit BendTargetsPanel(Store& st) : store(st) {
        struct G { const char* key; const char* label; int n; } gs[] = { { "WT", "Wavetable", 2 }, { "FM", "FM engine", 2 }, { "SMP", "Sampler", 16 }, { "PRO", "Procedural", 2 } };
        for (auto& g : gs) for (int i = 0; i < g.n; ++i) {
            auto* t = toggles.add(new juce::ToggleButton(juce::String(g.key) + " " + (juce::String(g.key) == "FM" ? juce::String(i == 0 ? "A" : "B") : juce::String(i + 1))));
            const juce::String path = juce::String("extra.pitchBendTargets.") + g.key + "." + juce::String(i);
            t->setToggleState(st.flag(path, true), juce::dontSendNotification); t->onClick = [this, t, path] { store.set(path, t->getToggleState()); };
            addAndMakeVisible(t); rows.add(g.n == 16 ? 2 : 0);
        }
        setSize(4 * 92 + 16, 8 * 26 + 16);
    }
    void resized() override {
        int idx = 0; int col = 0; const int w = 92;
        const int counts[] = { 2, 2, 16, 2 };
        for (int g = 0; g < 4; ++g) { int y = 8; for (int i = 0; i < counts[g]; ++i, ++idx) { const int c = g == 2 ? col + (i / 8) : col; const int yy = g == 2 ? 8 + (i % 8) * 26 : y; toggles[idx]->setBounds(8 + c * w, yy, w - 4, 24); y += 26; }
            col += g == 2 ? 2 : 1; }
    }
    Store& store; juce::OwnedArray<juce::ToggleButton> toggles; juce::Array<int> rows;
};

// ------------------------------------------------------------------ PLAY screen
// ------------------------------------------------------------------ MPC pads: 16 pads (12 sample pads + 4 bottom-row pads that are samples or WT / FM synth pads), with LOCK
class MpcOverlay : public juce::Component {
public:
    MpcOverlay(Processor& p, std::function<void()> onClose) : proc(p), close_(std::move(onClose)) {
        for (int i = 0; i < 16; ++i) { auto* pd = pads.add(new Pad(*this, i)); addAndMakeVisible(pd); auto* c = sels.add(new juce::ComboBox()); c->onChange = [this, i] { proc.store().setGlobal("mpcPad" + juce::String(i), sels[i]->getSelectedId() - 1); { auto sl = proc.store().get("extra"); if (sl.isObject() && sl.getProperty("mpcPadSlice", juce::var()).isObject()) { sl.getProperty("mpcPadSlice", juce::var()).getDynamicObject()->removeProperty(juce::Identifier(juce::String(i))); proc.store().set("extra", sl); } } pads[i]->repaint(); }; addAndMakeVisible(c); }
        for (auto* b : { &modeB, &lockB, &closeB }) addAndMakeVisible(*b);
        modeB.onClick = [this] { if (locked()) return; proc.store().setGlobal("mpcMode", mode() ? 0 : 1); rebuild(); };
        lockB.onClick = [this] { proc.store().setGlobal("mpcLocked", locked() ? 0 : 1); rebuild(); };
        closeB.setButtonText("CLOSE"); closeB.onClick = [this] { close_(); };
        title.setText("MPC PADS", juce::dontSendNotification); title.setFont(VVLookAndFeel::mono(15.f).boldened()); title.setColour(juce::Label::textColourId, col::cyan); addAndMakeVisible(title);
        rebuild();
    }
    bool locked() const { return proc.store().global("mpcLocked", 0) != 0; }
    bool mode() const { return proc.store().global("mpcMode", 0) != 0; }   // false = samples, true = WT / FM on the bottom row
    int slotFor(int pad) const { return juce::jlimit(0, 15, (int) proc.store().global("mpcPad" + juce::String(pad), pad)); }
    bool synthPad(int pad) const { return pad >= 12 && mode(); }
    void rebuild() {
        modeB.setButtonText(juce::String("BOTTOM ROW: ") + (mode() ? "WT / FM" : "SAMPLES")); modeB.setEnabled(!locked()); lockB.setButtonText(locked() ? "UNLOCK" : "LOCK");
        for (int i = 0; i < 16; ++i) { auto& c = *sels[i]; c.clear(juce::dontSendNotification);
            for (int s = 0; s < 16; ++s) { auto nm = proc.store().str("layers.SMP.oscs." + juce::String(s) + ".name", ""); c.addItem(juce::String(s + 1) + ": " + (nm.isEmpty() ? "empty" : nm), s + 1); }
            c.setSelectedId(slotFor(i) + 1, juce::dontSendNotification); c.setVisible(!synthPad(i)); c.setEnabled(!locked()); }
        resized(); repaint();
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff0a0a0a)); }
    void resized() override {
        auto r = getLocalBounds().reduced(14); auto head = r.removeFromTop(34); title.setBounds(head.removeFromLeft(140));
        closeB.setBounds(head.removeFromRight(90).reduced(2)); lockB.setBounds(head.removeFromRight(100).reduced(2)); modeB.setBounds(head.removeFromRight(240).reduced(2)); r.removeFromTop(8);
        const int cw = r.getWidth() / 4, ch = r.getHeight() / 4;
        for (int i = 0; i < 16; ++i) { auto cell = juce::Rectangle<int>(r.getX() + (i % 4) * cw, r.getY() + (i / 4) * ch, cw, ch).reduced(6); sels[i]->setBounds(cell.removeFromBottom(24)); pads[i]->setBounds(cell.withTrimmedBottom(4)); }
    }
    void padOn(int i) {
        if (!proc.engine()) return;
        if (!synthPad(i)) {
            const auto sl = proc.store().get("extra.mpcPadSlice." + juce::String(i));   // "Send Transients to MPC": this pad plays one transient of a slot
            if (sl.isObject() && (int) sl.getProperty("ti", -1) >= 0 && (int) sl.getProperty("ti", -1) < proc.store().count("layers.SMP.oscs." + juce::String((int) sl.getProperty("osc", 0)) + ".transients"))
                vv_engine_pad_slice_on(proc.engine(), (int) sl.getProperty("osc", 0), (int) sl.getProperty("ti", 0) + 1, i, 0.9f);
            else vv_engine_pad_on(proc.engine(), 2, slotFor(i), i, 60, 0.9f); }
        else { static const int notes[4] = { 60, 72, 60, 67 }; vv_engine_pad_on(proc.engine(), i >= 14 ? 1 : 0, -1, i, notes[i - 12], 0.9f); }
    }
    void padOff(int i) { if (proc.engine()) vv_engine_pad_off(proc.engine(), synthPad(i) ? (i >= 14 ? 1 : 0) : 2, i); }
private:
    struct Pad : juce::Component {
        Pad(MpcOverlay& o, int i) : owner(o), idx(i) {}
        void paint(juce::Graphics& g) override {
            const bool sp = owner.synthPad(idx); const auto border = !sp ? juce::Colour(0xff333333) : idx < 14 ? juce::Colour(0xff4a90e2) : idx == 14 ? juce::Colour(0xffe2704a) : juce::Colour(0xffe2504a);
            auto b = getLocalBounds().toFloat().reduced(1.5f); if (lit) { g.setColour(col::cyan.withAlpha(0.35f)); g.fillRoundedRectangle(b.expanded(3.f), 10.f); }   // glow
            g.setColour(lit ? juce::Colour(0xff2b5f6b) : juce::Colour(0xff1c1c1c)); g.fillRoundedRectangle(b, 8.f); g.setColour(border); g.drawRoundedRectangle(b, 8.f, 3.f);
            g.setColour(col::dim); g.setFont(VVLookAndFeel::mono(10.f)); g.drawText("PAD " + juce::String(idx + 1), getLocalBounds().removeFromTop(20), juce::Justification::centred);
            juce::String t, sub; if (!sp) { t = owner.proc.store().str("layers.SMP.oscs." + juce::String(owner.slotFor(idx)) + ".name", ""); if (t.isEmpty()) t = "- empty -"; }
            else { t = idx < 14 ? "WAVETABLE" : idx == 14 ? "FM A" : "FM B"; sub = idx == 12 ? "root" : idx == 13 ? "+octave" : idx == 14 ? "root" : "+5th"; }
            g.setColour(col::ink); g.setFont(VVLookAndFeel::mono(13.f)); g.drawText(t, getLocalBounds().reduced(4), juce::Justification::centred);
            if (sub.isNotEmpty()) { g.setColour(border); g.setFont(VVLookAndFeel::mono(11.f)); g.drawText(sub, getLocalBounds().withTrimmedTop(getHeight() / 2 + 10), juce::Justification::centredTop); }
        }
        void mouseDown(const juce::MouseEvent&) override { down = true; lit = true; repaint(); owner.padOn(idx); }   // highlight first, then the sound
        void mouseUp(const juce::MouseEvent&) override { down = false; owner.padOff(idx); juce::Component::SafePointer<Pad> sp(this); juce::Timer::callAfterDelay(110, [sp] { if (sp != nullptr && !sp->down) { sp->lit = false; sp->repaint(); } }); }   // a quick tap stays lit ~110 ms
        MpcOverlay& owner; int idx; bool down = false, lit = false;
    };
    Processor& proc; std::function<void()> close_; juce::OwnedArray<Pad> pads; juce::OwnedArray<juce::ComboBox> sels; juce::TextButton modeB, lockB, closeB; juce::Label title;
};

// ------------------------------------------------------------------ Keyboard zones (web: ZONES): every oscillator of every layer, FM as two engines (FM A / FM B)
class ZonesPanel : public juce::Component, private juce::Timer {
public:
    explicit ZonesPanel(Processor& p) : proc(p) {
        struct G { const char* layer; juce::String label; int first, count; juce::Colour c; };
        std::vector<G> gs; auto add = [&](const char* L, const char* head, juce::Colour c, int n, bool fm) { headers.add(head); hdrCols.add(c);
            if (fm) { gs.push_back({ L, "FM A", 0, 4, c }); gs.push_back({ L, "FM B", 4, 4, c }); } else for (int i = 0; i < n; ++i) gs.push_back({ L, juce::String(L) + " " + juce::String(i + 1), i, 1, c }); };
        add("WT", "Wavetable", juce::Colour(0xff4a90e2), p.store().count("layers.WT.oscs"), false); add("FM", "FM (two engines)", juce::Colour(0xffe2704a), 8, true);
        add("SMP", "Sampler", juce::Colour(0xff4ae29a), p.store().count("layers.SMP.oscs"), false); add("PRO", "Procedural", juce::Colour(0xffb06bff), p.store().count("layers.PRO.oscs"), false); add("SID", "SID voices", juce::Colour(0xffc9a0ff), p.store().count("layers.SID.oscs"), false);
        for (auto& g : gs) rows.add(new Row(*this, g.layer, g.label, g.first, g.count, g.c));
        for (auto* r : rows) addAndMakeVisible(r); startTimerHz(15); setSize(760, rows.size() * 64 + 40);
    }
    void paint(juce::Graphics& g) override { g.fillAll(col::panel); }
    void resized() override { int y = 6; for (auto* r : rows) { r->setBounds(6, y, getWidth() - 12, 60); y += 64; } }
    // LEARN: the next note played (keyboard or MIDI) becomes the bound
    int learnRow = -1; bool learnHigh = false; int lastSeen = -1;
private:
    void timerCallback() override {
        const int n = proc.lastNoteOn.load(); if (learnRow >= 0 && n >= 0 && n != lastSeen) { rows[learnRow]->setBound(learnHigh, n); learnRow = -1; for (auto* r : rows) r->resetLearn(); }
        lastSeen = n; for (auto* r : rows) r->repaint();
    }
    struct Row : juce::Component {
        Row(ZonesPanel& o, const char* layer, const juce::String& label, int first, int count, juce::Colour c) : owner(o), L(layer), name(label), f(first), cnt(count), col_(c) {
            scopeB.setButtonText("SCOPE"); scopeB.setClickingTogglesState(true); scopeB.setTooltip("Show this oscillator as its own trace in the Scope window");
            scopeB.onClick = [this] { for (int k = 0; k < cnt; ++k) st().set(path(k) + ".scope", scopeB.getToggleState()); };
            hold.setButtonText("Hold"); hold.onClick = [this] { for (int k = 0; k < cnt; ++k) st().set(path(k) + ".hold", hold.getToggleState()); };
            for (auto* ed : { &lo, &hi }) { ed->setInputRestrictions(3, "0123456789"); addAndMakeVisible(*ed); }
            lo.onTextChange = [this] { setBound(false, juce::jlimit(0, 127, lo.getText().getIntValue()), false); }; hi.onTextChange = [this] { setBound(true, juce::jlimit(0, 127, hi.getText().getIntValue()), false); };
            learnLo.setButtonText("LEARN LO"); learnHi.setButtonText("LEARN HI");
            learnLo.onClick = [this] { owner.lastSeen = owner.proc.lastNoteOn.load(); owner.learnRow = idx(); owner.learnHigh = false; for (auto* r : owner.rows) r->resetLearn(); learnLo.setButtonText("PLAY..."); };
            learnHi.onClick = [this] { owner.lastSeen = owner.proc.lastNoteOn.load(); owner.learnRow = idx(); owner.learnHigh = true; for (auto* r : owner.rows) r->resetLearn(); learnHi.setButtonText("PLAY..."); };
            glide.setRange(0, 2, 0.005); glide.setSliderStyle(juce::Slider::LinearHorizontal); glide.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
            key.setRange(0, 200, 1); key.setSliderStyle(juce::Slider::LinearHorizontal); key.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
            glide.onValueChange = [this] { if (!sync_) for (int k = 0; k < cnt; ++k) st().set(path(k) + ".glideTime", glide.getValue()); }; key.onValueChange = [this] { if (!sync_) for (int k = 0; k < cnt; ++k) st().set(path(k) + ".pitchKeytrack", key.getValue()); };
            for (juce::Component* c : std::initializer_list<juce::Component*>{ &scopeB, &hold, &learnLo, &learnHi, &glide, &key }) addAndMakeVisible(*c);
            read();
        }
        Store& st() { return owner.proc.store(); }
        juce::String path(int k) const { return "layers." + juce::String(L) + ".oscs." + juce::String(f + k); }
        int idx() const { return owner.rows.indexOf(this); }
        void resetLearn() { learnLo.setButtonText("LEARN LO"); learnHi.setButtonText("LEARN HI"); }
        void setBound(bool high, int v, bool refreshText = true) { for (int k = 0; k < cnt; ++k) st().set(path(k) + (high ? ".rangeHigh" : ".rangeLow"), v); if (refreshText) read(); repaint(); }
        void read() { sync_ = true; lo.setText(juce::String((int) st().num(path(0) + ".rangeLow", 0)), false); hi.setText(juce::String((int) st().num(path(0) + ".rangeHigh", 127)), false);
            hold.setToggleState(st().num(path(0) + ".hold", 0) != 0, juce::dontSendNotification); scopeB.setToggleState(st().num(path(0) + ".scope", 0) != 0, juce::dontSendNotification); glide.setValue(st().num(path(0) + ".glideTime", 0), juce::dontSendNotification); key.setValue(st().num(path(0) + ".pitchKeytrack", 100), juce::dontSendNotification); sync_ = false; }
        void paint(juce::Graphics& g) override {
            g.setColour(col_); g.setFont(VVLookAndFeel::mono(12.f).boldened()); g.drawText(name, 0, 0, 56, 26, juce::Justification::centredLeft);
            auto bar = getLocalBounds().removeFromBottom(12).toFloat(); g.setColour(juce::Colours::black); g.fillRect(bar);
            const float x0 = bar.getX() + (float) st().num(path(0) + ".rangeLow", 0) / 127.f * bar.getWidth(), x1 = bar.getX() + (float) st().num(path(0) + ".rangeHigh", 127) / 127.f * bar.getWidth();
            g.setColour(col_.withAlpha(0.75f)); g.fillRect(x0, bar.getY(), juce::jmax(1.f, x1 - x0), bar.getHeight()); g.setColour(col::line); g.drawRect(bar);
        }
        void resized() override { auto r = getLocalBounds(); auto top = r.removeFromTop(30); top.removeFromLeft(58); scopeB.setBounds(top.removeFromLeft(58).reduced(0, 3)); hold.setBounds(top.removeFromLeft(64)); lo.setBounds(top.removeFromLeft(44).reduced(0, 3)); top.removeFromLeft(4); learnLo.setBounds(top.removeFromLeft(80).reduced(0, 2));
            top.removeFromLeft(10); hi.setBounds(top.removeFromLeft(44).reduced(0, 3)); top.removeFromLeft(4); learnHi.setBounds(top.removeFromLeft(80).reduced(0, 2)); top.removeFromLeft(8);
            auto g2 = top.removeFromLeft(top.getWidth() / 2); glide.setBounds(g2); key.setBounds(top); }
        ZonesPanel& owner; const char* L; juce::String name; int f, cnt; juce::Colour col_; juce::ToggleButton hold; juce::TextEditor lo, hi; juce::TextButton scopeB, learnLo, learnHi; juce::Slider glide, key; bool sync_ = false;
    };
    Processor& proc; juce::OwnedArray<Row> rows; juce::StringArray headers; juce::Array<juce::Colour> hdrCols;
};

juce::Component* makeZonesPanelForTest(Processor& p) { return new ZonesPanel(p); }   // test hook (a call-out window cannot open under Xvfb)

// Persistent bottom control area (web footer): macros, master, WT/FM + sample mixers, keyboard, wheels, arp/scale options, view/hold-style buttons. Shown under every tab.
class PlayFooter : public juce::Component, private juce::Timer {
public:
    explicit PlayFooter(Processor& p) : proc(p), keys(p.store(), p.keyboardState) {
        Store& s = p.store();
        addAndMakeVisible(keys);
        keys.setAvailableRange(24, 96); keys.setLowestVisibleKey(48); keys.setKeyWidth(46.f); keys.setVelocity(0.8f, true);
        keys.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, col::cyan.withAlpha(0.7f));
        keys.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, col::cyan.withAlpha(0.25f));
        auto knobFor = [&](const char* name) -> std::function<void()> { return nullptr; (void) name; };
        (void) knobFor;
        panel = std::make_unique<Panel>(s, "Performance");
        if (auto* mv = p.paramByAddress("master_volume")) { master = std::make_unique<HostKnob>(*mv, "Master volume"); panel->custom(master.get(), 88, 104); }
        if (auto* mt = p.paramByAddress("master_tone")) { masterTone = std::make_unique<HostKnob>(*mt, "Master tone"); panel->custom(masterTone.get(), 88, 104); }
        for (int i = 0; i < 4; ++i) {
            auto& k = panel->knob("Macro " + juce::String(i + 1), "macros." + juce::String(i), 0, 1, 0);
            k.slider.onValueChange = [this, &k, i] { const double v = k.slider.getValue(); proc.store().set("macros." + juce::String(i), v); vv_engine_set_macro(proc.engine(), i, (float) v); };
        }
        bend.setSliderStyle(juce::Slider::LinearVertical); bend.setRange(-1, 1, 0); bend.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); bend.setValue(0);
        bend.onValueChange = [this] { vv_engine_set_pitchbend(proc.engine(), (float) bend.getValue()); };
        bend.onDragEnd = [this] { bend.setValue(0, juce::sendNotificationSync); };
        wheel.setSliderStyle(juce::Slider::LinearVertical); wheel.setRange(0, 1, 0); wheel.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        wheel.onValueChange = [this] { vv_engine_set_modwheel(proc.engine(), (float) wheel.getValue()); };
        bendL.setText("BEND", juce::dontSendNotification); wheelL.setText("MOD", juce::dontSendNotification);
        for (auto* l : { &bendL, &wheelL }) { l->setJustificationType(juce::Justification::centred); l->setColour(juce::Label::textColourId, col::dim); l->setFont(VVLookAndFeel::mono(11.f)); addAndMakeVisible(l); }
        wtfm = std::make_unique<MixerFader>(s, "WT / FM mixer", "@wtFmMix", 0, 1, 0.5, 0);                                    // 169 px
        smp = std::make_unique<MixerFader>(s, "Samples", "@sampleMix", 0, 15, 0, 16, "SAMPLES", "@sampleMixOn");           // 315 px = 252 px (old 224 + 12.5 %) + 25 %
        panel->toggle("Global sampler ADSR", "layers.SMP.globalAdsrActive"); panel->toggle("Global swing", "globalSwingOn");
        panel->newRow(); panel->custom(wtfm.get(), 169, 48); panel->custom(smp.get(), 315, 64);
        xfxBtn.setButtonText("XFX"); xfxBtn.setTooltip("XFX MODE: give every key its own sound"); xfxBtn.onClick = [this] { XfxEditor::show(proc.store()); };   // web: XFX button beside the Samples mixer
        panel->custom(&xfxBtn, 72, 64);
        addAndMakeVisible(*panel); addAndMakeVisible(bend); addAndMakeVisible(wheel);
        octDn.setButtonText("OCT -"); octUp.setButtonText("OCT +");    // 66 px wide = the old 44 px + 50 %
        octDn.onClick = [this] { kbLow = juce::jmax(24, kbLow - 12); resized(); }; octUp.onClick = [this] { kbLow = juce::jmin(84, kbLow + 12); resized(); };
        addAndMakeVisible(octDn); addAndMakeVisible(octUp);
        {   // keyboard options (web: Labels / Scale / Root / Scale lock / Invert keys / Octaves / Hide keyboard)
            Store& st = p.store(); int nsc; const KbScale* sc = kbScales(nsc);
            for (int i = 0; i < nsc; ++i) scaleC.addItem(sc[i].label, i + 1); scaleC.setSelectedId(1 + (int) st.global("scaleIdx", 0), juce::dontSendNotification);
            const char* nn[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; for (int i = 0; i < 12; ++i) rootC.addItem(nn[i], i + 1); rootC.setSelectedId(1 + (int) st.global("scaleRoot", 0), juce::dontSendNotification);
            for (int i = 1; i <= 3; ++i) octC.addItem(juce::String(i) + (i == 1 ? " octave" : " octaves"), i); octC.setSelectedId((int) st.global("octaveCount", 2), juce::dontSendNotification);
            labelsT.setButtonText("Labels"); labelsT.setToggleState(st.global("keyLabels", 0) != 0, juce::dontSendNotification);
            lockT.setButtonText("Scale lock"); lockT.setToggleState(st.global("scaleLock", 0) != 0, juce::dontSendNotification);
            invertB.setButtonText("INVERT KEYS"); invertB.setClickingTogglesState(true); invertB.setToggleState(st.global("invertKeys", 0) != 0, juce::dontSendNotification);
            hideB.setButtonText("HIDE KEYBOARD"); hideB.setClickingTogglesState(true); hideB.setToggleState(st.global("hideKeyboard", 0) != 0, juce::dontSendNotification);
            scaleC.onChange = [this, &st] { st.setGlobal("scaleIdx", scaleC.getSelectedId() - 1); keys.repaint(); };
            rootC.onChange = [this, &st] { st.setGlobal("scaleRoot", rootC.getSelectedId() - 1); keys.repaint(); };
            { // web: Poly dropdown (1 mono, 1 Legato, 2..16, 24, 32, 48, 64) -> preset maxPolyphony + legatoMode
                polyL.setText("Poly", juce::dontSendNotification); polyL.setJustificationType(juce::Justification::centredRight); polyL.setFont(juce::Font(11.f));
                polyC.addItem("1 (mono)", 1); polyC.addItem("1 (Legato)", 2); for (int i = 2; i <= 16; ++i) polyC.addItem(juce::String(i), i + 1);
                polyC.addItem("24", 18); polyC.addItem("32", 19); polyC.addItem("48", 20); polyC.addItem("64", 21);
                auto syncPoly = [this, &st] { const int mx = (int) st.num("maxPolyphony", 64); const bool lg = st.flag("legatoMode", false);
                    polyC.setSelectedId(lg ? 2 : mx <= 16 ? (mx == 1 ? 1 : mx + 1) : mx == 24 ? 18 : mx == 32 ? 19 : mx == 48 ? 20 : 21, juce::dontSendNotification); };
                syncPoly();
                polyC.onChange = [this, &st] { const int id = polyC.getSelectedId(); const bool lg = id == 2;
                    const int mx = id <= 2 ? 1 : id <= 17 ? id - 1 : id == 18 ? 24 : id == 19 ? 32 : id == 20 ? 48 : 64;
                    st.set("maxPolyphony", mx); st.set("legatoMode", lg); };
            }
            octC.onChange = [this, &st] { st.setGlobal("octaveCount", octC.getSelectedId()); resized(); };
            labelsT.onClick = [this, &st] { st.setGlobal("keyLabels", labelsT.getToggleState()); keys.repaint(); };
            lockT.onClick = [this, &st] { st.setGlobal("scaleLock", lockT.getToggleState()); keys.repaint(); };
            invertB.onClick = [this, &st] { st.setGlobal("invertKeys", invertB.getToggleState()); keys.repaint(); };
            hideB.onClick = [this, &st] { st.setGlobal("hideKeyboard", hideB.getToggleState()); resized(); };
            for (juce::Component* c : std::initializer_list<juce::Component*>{ &labelsT, &lockT, &scaleC, &rootC, &octC, &invertB, &hideB, &polyL, &polyC }) addAndMakeVisible(*c);
        }
        initTransport();
        bendTgtB.setButtonText("BEND TARGETS"); modTgtB.setButtonText("MOD TARGETS");
        bendTgtB.onClick = [this] { auto* c = new BendTargetsPanel(proc.store()); juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(c), bendTgtB.getScreenBounds(), nullptr); };
        modTgtB.onClick = [this] { if (auto* ed = findParentComponentOfClass<Editor>()) ed->selectTab(2); };   // the mod-wheel's targets are its matrix connections
        addAndMakeVisible(bendTgtB); addAndMakeVisible(modTgtB);
        mpcB.setButtonText("MPC PADS"); mpcB.onClick = [this] { if (!mpc) { mpc = std::make_unique<MpcOverlay>(proc, [this] { mpc->setVisible(false); }); if (auto* ed = findParentComponentOfClass<Editor>()) ed->addChildComponent(*mpc); } mpc->rebuild(); if (auto* ed = findParentComponentOfClass<Editor>()) mpc->setBounds(ed->getLocalBounds()); mpc->setVisible(true); mpc->toFront(false); };
        addAndMakeVisible(mpcB);
        zonesB.setButtonText("ZONES"); zonesB.onClick = [this] { auto* z = new ZonesPanel(proc); auto* vp = new juce::Viewport(); vp->setViewedComponent(z, true); vp->setSize(780, juce::jmin(520, z->getHeight() + 8)); vp->setScrollBarsShown(true, false);
            juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(vp), zonesB.getScreenBounds(), nullptr); };
        addAndMakeVisible(zonesB);
        learnB.setButtonText("MIDI LEARN"); learnB.setClickingTogglesState(true);
        learnB.onClick = [this] { Knob::learnMode = learnB.getToggleState(); if (!Knob::learnMode && proc.engine()) vv_engine_cc_learn_cancel(proc.engine()); if (auto* t = getTopLevelComponent()) t->repaint(); };
        addAndMakeVisible(learnB);
        panic.setButtonText("Panic"); panic.onClick = [this] { vv_engine_all_notes_off(proc.engine()); }; addAndMakeVisible(panic);
    }
    // A knob bound to a host parameter (master volume)
    struct HostKnob : Control {
        HostKnob(juce::AudioParameterFloat& p, const juce::String& label) : Control(dummyStore(), "x"), param(p) {
            name.setText(label, juce::dontSendNotification); name.setJustificationType(juce::Justification::centred); name.setFont(VVLookAndFeel::mono(11.f)); name.setColour(juce::Label::textColourId, col::dim); addAndMakeVisible(name);
            sl.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); sl.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 84, 16); sl.setRange(0, 1, 0); sl.setDoubleClickReturnValue(true, 0.8);
            sl.setRotaryParameters(juce::degreesToRadians(225.f), juce::degreesToRadians(495.f), true); sl.setNumDecimalPlacesToDisplay(2);
            sl.onValueChange = [this] { param.setValueNotifyingHost((float) sl.getValue()); }; addAndMakeVisible(sl); sl.setValue(param.get(), juce::dontSendNotification); startTimerHz(10);
        }
        ~HostKnob() override { stopTimer(); }
        void refresh() override {}
        void resized() override { auto r = getLocalBounds(); name.setBounds(r.removeFromTop(16)); sl.setBounds(r); }
        juce::Point<int> preferredSize() const override { return { 88, 104 }; }
        static Store& dummyStore() { static Store s(nullptr); return s; }
        struct T : juce::Timer { std::function<void()> f; void timerCallback() override { f(); } } timer;
        void startTimerHz(int hz) { timer.f = [this] { if (!sl.isMouseButtonDown() && std::abs(sl.getValue() - param.get()) > 1e-4) sl.setValue(param.get(), juce::dontSendNotification); }; timer.startTimerHz(hz); }
        void stopTimer() { timer.stopTimer(); }
        juce::AudioParameterFloat& param; juce::Label name; juce::Slider sl;
    };
    // ---- transport row (web .transport): VIEW <layer>, PLAY, HOLD <layer>, SOLO <layer> (tap = solo, modifier-click = mute), BPM
    static constexpr const char* kL[5] = { "WT", "FM", "SMP", "PRO", "SID" };
    static constexpr int kOscCount[5] = { 2, 8, 16, 2, 3 };
    void initTransport() {
        for (int k = 0; k < 5; ++k) {
            viewB[k].setButtonText(juce::String("VIEW ") + kL[k]); viewB[k].onClick = [this, k] { if (auto* ed = findParentComponentOfClass<Editor>()) ed->viewLayer(k); };
            holdB[k].setButtonText(juce::String("HOLD ") + kL[k]); holdB[k].setClickingTogglesState(true);
            holdB[k].onClick = [this, k] { const bool v = holdB[k].getToggleState(); for (int o = 0; o < kOscCount[k]; ++o) proc.store().set(juce::String("layers.") + kL[k] + ".oscs." + juce::String(o) + ".hold", v ? 1.0 : 0.0);
                if (!v && proc.engine()) vv_engine_all_notes_off(proc.engine()); };   // web releases only this layer's held notes; native releases all voices (documented deviation)
            soloB[k].setButtonText(juce::String("SOLO ") + kL[k]);
            soloB[k].onClick = [this, k] { if ((juce::ModifierKeys::currentModifiers.isShiftDown() || juce::ModifierKeys::currentModifiers.isAltDown() || juce::ModifierKeys::currentModifiers.isCommandDown())) muted_ ^= 1 << k; else soloed_ ^= 1 << k; applySoloMute(); };
            for (auto* b : { &viewB[k], &holdB[k], &soloB[k] }) addAndMakeVisible(*b);
            viewB[k].setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff1f8a96));
            holdB[k].setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1c2a2d)); holdB[k].setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2a9d8f));   // HOLD: teal-green shades
            soloB[k].setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1c2233)); soloB[k].setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a6fc4));   // SOLO: blue shades
        }
        playB.setButtonText("PLAY"); playB.setClickingTogglesState(true); playB.onClick = [this] { proc.store().setGlobal("transportRunning", playB.getToggleState()); if (proc.engine()) vv_engine_start_sequencer(proc.engine(), playB.getToggleState() ? 1 : 0); playB.setButtonText(playB.getToggleState() ? "STOP" : "PLAY"); };
        playB.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1c3a26)); playB.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2fa855)); playB.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff7be0a0)); playB.setColour(juce::TextButton::textColourOnId, juce::Colours::white);   // green PLAY
        addAndMakeVisible(playB);
        bpmL.setText("BPM", juce::dontSendNotification); bpmL.setJustificationType(juce::Justification::centredRight); addAndMakeVisible(bpmL);
        bpm.setSliderStyle(juce::Slider::LinearBar); bpm.setRange(20, 300, 1); bpm.setValue(proc.store().num("bpm", 120), juce::dontSendNotification); bpm.onValueChange = [this] { proc.store().set("bpm", bpm.getValue()); }; addAndMakeVisible(bpm);
        {   // web footer row 2: MPE · GLOBAL SWING menu · GENERATE · OS · CHORD readout
            Store& st = proc.store();
            mpeB.setButtonText("MPE"); mpeB.setClickingTogglesState(true); mpeB.setToggleState(st.global("mpe", 0) != 0, juce::dontSendNotification);
            mpeB.onClick = [this] { proc.store().setGlobal("mpe", mpeB.getToggleState()); };
            swingB.setButtonText("GLOBAL SWING");
            swingB.onClick = [this] {
                struct Pop : juce::Component { Pop(Store& s) : st(s) { on.setButtonText("Swing on"); on.setToggleState(s.num("globalSwingOn", 0) != 0, juce::dontSendNotification); on.onClick = [this] { st.set("globalSwingOn", on.getToggleState() ? 1 : 0); };
                        amt.setSliderStyle(juce::Slider::LinearHorizontal); amt.setRange(50, 75, 1); amt.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20); amt.setTextValueSuffix(" %"); amt.setValue(s.num("globalSwingAmount", 50), juce::dontSendNotification);
                        amt.onValueChange = [this] { st.set("globalSwingAmount", amt.getValue()); }; addAndMakeVisible(on); addAndMakeVisible(amt); setSize(260, 70); }
                    void resized() override { auto r = getLocalBounds().reduced(8); on.setBounds(r.removeFromTop(26)); amt.setBounds(r); }
                    Store& st; juce::ToggleButton on; juce::Slider amt; };
                juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(new Pop(proc.store())), swingB.getScreenBounds(), nullptr); };
            quickGenB.setButtonText("GENERATE");
            quickGenB.onClick = [this] { juce::PopupMenu m; int i = 1; for (auto* c : { "Arp", "Bass", "Key", "Lead", "Pad", "Drum", "FX", "XFX" }) m.addItem(i++, c);
                m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&quickGenB), [this](int r) { if (r <= 0) return; const char* cs[] = { "Arp", "Bass", "Key", "Lead", "Pad", "Drum", "FX", "XFX" }; proc.store().generatePreset(cs[r - 1], proc.userPresetDir()); }); };
            osL.setText("OS", juce::dontSendNotification); osL.setJustificationType(juce::Justification::centredRight);
            osC.addItem("Normal", 1); osC.addItem("X2", 2); osC.addItem("X4", 3);
            { const auto cur = st.str("oversample", "normal"); osC.setSelectedId(cur == "4x" ? 3 : cur == "2x" ? 2 : 1, juce::dontSendNotification); }
            osC.onChange = [this] { const char* v[] = { "normal", "2x", "4x" }; proc.store().set("oversample", juce::String(v[juce::jlimit(0, 2, osC.getSelectedId() - 1)])); };
            chordT.setButtonText("CHORD"); chordT.onClick = [this] { chordL.setText("-", juce::dontSendNotification); };
            chordL.setText("-", juce::dontSendNotification); chordL.setFont(VVLookAndFeel::mono(13.f)); chordL.setColour(juce::Label::textColourId, col::cyan);
            for (juce::Component* c : std::initializer_list<juce::Component*>{ &mpeB, &swingB, &quickGenB, &osL, &osC, &chordT, &chordL, &daw }) addAndMakeVisible(*c);
        }
        startTimerHz(10);
    }
    void applySoloMute() {
        if (proc.engine()) vv_engine_set_solo_mute(proc.engine(), soloed_, muted_);
        for (int k = 0; k < 5; ++k) { const bool m = (muted_ >> k) & 1; soloB[k].setToggleState((soloed_ >> k) & 1 || m, juce::dontSendNotification); soloB[k].setButtonText(m ? "MUTED" : juce::String("SOLO ") + kL[k]); }
    }
    void timerCallback() override {   // keep HOLD / BPM / PLAY in step with the store (presets, undo, MIDI)
        Store& st = proc.store();
        for (int k = 0; k < 5; ++k) { bool all = true; for (int o = 0; o < kOscCount[k] && all; ++o) all = st.num(juce::String("layers.") + kL[k] + ".oscs." + juce::String(o) + ".hold", 0) != 0; if (all != holdB[k].getToggleState()) holdB[k].setToggleState(all, juce::dontSendNotification); }
        if (!bpm.isMouseButtonDown() && std::abs(bpm.getValue() - st.num("bpm", 120)) > 0.5) bpm.setValue(st.num("bpm", 120), juce::dontSendNotification);
        if (chordT.getToggleState() && proc.engine()) { char b[64]; vv_engine_chord_label(proc.engine(), b, 64); const juce::String t = juce::String::fromUTF8(b); if (t.isNotEmpty() && t != chordL.getText()) chordL.setText(t, juce::dontSendNotification); }
        if (mpeB.getToggleState() != (st.global("mpe", 0) != 0)) mpeB.setToggleState(st.global("mpe", 0) != 0, juce::dontSendNotification);
        const bool run = st.global("transportRunning", 0) != 0; if (run != playB.getToggleState()) { playB.setToggleState(run, juce::dontSendNotification); playB.setButtonText(run ? "STOP" : "PLAY"); }
    }
    int preferredHeight(int w) const { return 16 + panel->heightForWidth(w - 16) + 8 + 30 + 30 + 30 + 30 + (hideB.getToggleState() ? 40 : 190) + 4; }
    void resized() override {
        if (mpc && mpc->isVisible()) if (auto* ed = findParentComponentOfClass<Editor>()) mpc->setBounds(ed->getLocalBounds());
        auto r = getLocalBounds().reduced(8);
        const int reserve = 8 + 30 + 30 + 30 + 30 + (hideB.getToggleState() ? 40 : 190) + 4; const int ph = panel->heightForWidth(r.getWidth()); panel->setBounds(r.removeFromTop(juce::jmax(0, juce::jmin(r.getHeight() - reserve, ph))));
        r.removeFromTop(8);
        { auto tr = r.removeFromTop(juce::jmin(r.getHeight(), 30)).reduced(0, 2); const int bw = juce::jmax(40, (tr.getWidth() - 110) / 16);
          for (int k = 0; k < 5; ++k) viewB[k].setBounds(tr.removeFromLeft(bw).reduced(1, 0));
          playB.setBounds(tr.removeFromLeft(bw).reduced(1, 0));
          for (int k = 0; k < 5; ++k) holdB[k].setBounds(tr.removeFromLeft(bw).reduced(1, 0));
          for (int k = 0; k < 5; ++k) soloB[k].setBounds(tr.removeFromLeft(bw).reduced(1, 0));
          bpmL.setBounds(tr.removeFromLeft(36)); bpm.setBounds(tr.removeFromLeft(64).reduced(0, 1)); }
        { auto xr = r.removeFromTop(juce::jmin(r.getHeight(), 30)).reduced(0, 2);
          mpeB.setBounds(xr.removeFromLeft(54).reduced(1, 0)); swingB.setBounds(xr.removeFromLeft(110).reduced(1, 0)); quickGenB.setBounds(xr.removeFromLeft(96).reduced(1, 0));
          osL.setBounds(xr.removeFromLeft(34)); osC.setBounds(xr.removeFromLeft(84).reduced(2, 1)); xr.removeFromLeft(10);
          chordT.setBounds(xr.removeFromLeft(80)); chordL.setBounds(xr.removeFromLeft(180)); }
        const bool hidden = hideB.getToggleState();
        { auto kbrow = r.removeFromBottom(juce::jmin(r.getHeight(), 30)); const int w = juce::jmax(60, kbrow.getWidth() / 10);
          labelsT.setBounds(kbrow.removeFromLeft(w - 10)); scaleC.setBounds(kbrow.removeFromLeft(w * 2 - 20).reduced(2, 2)); rootC.setBounds(kbrow.removeFromLeft(w - 20).reduced(2, 2)); lockT.setBounds(kbrow.removeFromLeft(w + 10));
          octC.setBounds(kbrow.removeFromLeft(w).reduced(2, 2)); invertB.setBounds(kbrow.removeFromLeft(w + 14).reduced(2, 2)); hideB.setBounds(kbrow.removeFromLeft(w + 20).reduced(2, 2)); }
        auto bottom = r.removeFromBottom(juce::jmin(r.getHeight(), hidden ? 40 : 190)); keys.setVisible(!hidden); bend.setVisible(!hidden); wheel.setVisible(!hidden); bendL.setVisible(!hidden); wheelL.setVisible(!hidden);
        auto wheels = bottom.removeFromLeft(110); auto b = wheels.removeFromLeft(54), m = wheels;
        bendL.setBounds(b.removeFromTop(16)); bend.setBounds(b.reduced(8, 2)); wheelL.setBounds(m.removeFromTop(16)); wheel.setBounds(m.reduced(8, 2));
        { auto row = r.removeFromTop(30); octDn.setBounds(row.removeFromLeft(66)); row.removeFromLeft(6); octUp.setBounds(row.removeFromLeft(66)); row.removeFromLeft(6); bendTgtB.setBounds(row.removeFromLeft(120)); row.removeFromLeft(6); modTgtB.setBounds(row.removeFromLeft(120)); row.removeFromLeft(6); mpcB.setBounds(row.removeFromLeft(110)); row.removeFromLeft(6); zonesB.setBounds(row.removeFromLeft(90)); row.removeFromLeft(6); learnB.setBounds(row.removeFromLeft(110)); panic.setBounds(row.removeFromRight(90)); row.removeFromRight(6); daw.setBounds(row.removeFromRight(196)); row.removeFromRight(6); polyC.setBounds(row.removeFromRight(96).reduced(0, 2)); polyL.setBounds(row.removeFromRight(34)); }   // PANIC at the far right of the bottom row; the RECORDER sits immediately to its left (web parity)
        if (!hidden) { const int oc = juce::jlimit(1, 3, (int) store_().global("octaveCount", 2)); keys.setAvailableRange(juce::jmax(0, kbLow), juce::jmin(127, kbLow + 12 * oc)); keys.setLowestVisibleKey(kbLow);
            keys.setKeyWidth((float) bottom.getWidth() / (7.f * (float) oc)); }
        keys.setBounds(bottom.withTrimmedTop(2));   // 2 px gap above the keyboard
    }
private:
    Store& store_() const { return proc.store(); }
    juce::TextButton mpeB, swingB, quickGenB; juce::Label osL, chordL; juce::ComboBox osC; juce::ToggleButton chordT;
    juce::TextButton viewB[5], holdB[5], soloB[5], playB; juce::Label bpmL; juce::Slider bpm; int soloed_ = 0, muted_ = 0;
    Processor& proc; DawBlock daw{ proc.store() }; ScaleKeyboard keys; std::unique_ptr<Panel> panel; juce::ToggleButton labelsT, lockT; juce::ComboBox scaleC, rootC, octC, polyC; juce::Label polyL; juce::TextButton invertB, hideB, bendTgtB, modTgtB, mpcB, zonesB; juce::TextButton learnB; std::unique_ptr<MpcOverlay> mpc; int kbLow = 48; bool scopeFull = false; std::unique_ptr<HostKnob> master, masterTone;
    juce::Slider bend, wheel; juce::Label bendL, wheelL; juce::TextButton panic, octDn, octUp, xfxBtn; std::unique_ptr<MixerFader> wtfm, smp;
};

// SCOPE tab: the oscilloscope / spectrum view fills the page; its fullscreen toggle also hides the footer.
class ScopeScreen : public juce::Component {
public:
    explicit ScopeScreen(Processor& p, bool spectrogramTab = false) : scope(p), spec_(spectrogramTab) { scope.setSpectrogramTab(spectrogramTab); addAndMakeVisible(scope); scope.onFullscreen = [this](bool on) { if (auto* ed = findParentComponentOfClass<Editor>()) ed->setFooterVisible(!on); }; }
    void resized() override { scope.setBounds(getLocalBounds().reduced(8)); }
    void visibilityChanged() override { if (spec_ && isVisible() && scope.mode() < ScopeView::Spectrogram) scope.showMode(ScopeView::Spectrogram); }   // web SPECTROGRAM tab = the scope in its spectrogram view
private:
    ScopeView scope; bool spec_;
};

// ------------------------------------------------------------------ XY screen
class XYScreen : public juce::Component {
public:
    explicit XYScreen(Store& s) : pads(s) { view.setViewedComponent(&pads, false); addAndMakeVisible(view); }
    void resized() override { view.setBounds(getLocalBounds()); pads.setSize(juce::jmax(300, getWidth() - 14), pads.heightForWidth(getWidth() - 14)); }
private:
    juce::Viewport view; XYPadsView pads;
};

// ------------------------------------------------------------------ host parameters (generic, searchable)
class HostParamsScreen : public juce::Component {
public:
    explicit HostParamsScreen(Processor& p) : proc(p) {
        search.setTextToShowWhenEmpty("search the 200 host parameters", col::dim); search.onTextChange = [this] { rebuild(); }; addAndMakeVisible(search);
        view.setViewedComponent(&content, false); addAndMakeVisible(view); rebuild();
    }
    void resized() override { auto r = getLocalBounds().reduced(6); search.setBounds(r.removeFromTop(28)); view.setBounds(r); layoutRows(); }
private:
    struct Row : juce::Component {
        Row(juce::AudioParameterFloat& p) : param(p) { name.setText(p.getName(100), juce::dontSendNotification); name.setFont(VVLookAndFeel::mono(11.f)); addAndMakeVisible(name);
            auto r = p.getNormalisableRange(); sl.setSliderStyle(juce::Slider::LinearHorizontal); sl.setRange(r.start, r.end, 0); sl.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
            sl.setValue(p.get(), juce::dontSendNotification); sl.onValueChange = [this] { param.setValueNotifyingHost(param.convertTo0to1((float) sl.getValue())); }; addAndMakeVisible(sl); }
        void resized() override { auto r = getLocalBounds(); name.setBounds(r.removeFromLeft(juce::jmin(260, r.getWidth() / 2))); sl.setBounds(r); }
        juce::AudioParameterFloat& param; juce::Label name; juce::Slider sl;
    };
    void rebuild() {
        rows.clear(); const auto q = search.getText();
        for (auto* p : proc.hostParams) if (q.isEmpty() || p->getName(100).containsIgnoreCase(q) || p->paramID.containsIgnoreCase(q)) { rows.add(new Row(*p)); content.addAndMakeVisible(rows.getLast()); }
        layoutRows();
    }
    void layoutRows() { const int w = juce::jmax(300, view.getWidth() - 14); int y = 0; for (auto* r : rows) { r->setBounds(0, y, w, 30); y += 30; } content.setSize(w, y); }
    Processor& proc; juce::TextEditor search; juce::Viewport view; juce::Component content; juce::OwnedArray<Row> rows;
};

// ------------------------------------------------------------------ editor shell
Editor::Editor(Processor& p) : juce::AudioProcessorEditor(p), proc_(p) {
    setLookAndFeel(&laf_);
    setResizable(true, true); setResizeLimits(420, 480, 2000, 1400); setSize(p.uiWidth.load(), p.uiHeight.load());
    Store& s = p.store();
    struct T { const char* name; juce::Component* page; };
    pages_.add(new ScopeScreen(p)); pages_.add(new SoundScreen(s)); pages_.add(new ModScreen(s, 1)); pages_.add(new SeqScreen(s));
    pages_.add(makeFxScreen(s)); pages_.add(new XYScreen(s)); pages_.add(makePresetsScreen(s, p.userPresetDir())); pages_.add(makeSettingsScreen(s)); pages_.add(new HostParamsScreen(p));
    pages_.add(new ModScreen(s, 0)); pages_.add(new ModScreen(s, 2)); pages_.add(new ModScreen(s, 3)); pages_.add(new MixerScreen(s, p.engine())); pages_.add(new ScopeScreen(p, true));   // LFO, CROSS MOD, VOCODER: top-level tabs as on the web
    const char* names[] = { "SCOPE", "OSCILLATORS", "MATRIX", "SEQUENCER", "FX", "XY PADS", "PRESETS", "SETTINGS", "HOST", "LFO", "CROSS MOD", "VOCODER", "MIXER", "SPECTROGRAM" };
    for (int i = 0; i < pages_.size(); ++i) { addChildComponent(pages_[i]); tabNames_.add(names[i]); }
    if (auto* mx = dynamic_cast<MixerScreen*>(pages_[12])) mx->onEditAutomation = [this](const juce::String& ch) { selectTab(1); if (auto* ss = dynamic_cast<SoundScreen*>(pages_[1])) ss->showAutomation(ch); };   // mixer E button
    { auto* f = new PlayFooter(p); footer_.reset(f); footerH_ = [f](int w) { return f->preferredHeight(w); }; addAndMakeVisible(*footer_); }
    // button order = the web tab bar; buttons are index-aligned with pages_ (display order is applied in resized())
    // Icon-only tab bar (the web app's section icons): the button text stays the section name (accessibility name, lookups) but the look-and-feel draws the icon instead.
    struct TabLook { const char* name; const char* label; navicon::Id icon; };
    static const TabLook looks[] = { { "SCOPE", "Scope", navicon::Scope }, { "OSCILLATORS", "Oscillators", navicon::Osc }, { "MATRIX", "Matrix", navicon::Matrix }, { "SEQUENCER", "Sequencer", navicon::Seq },
        { "FX", "FX", navicon::FX }, { "XY PADS", "XY Pads", navicon::XY }, { "PRESETS", "Presets", navicon::Presets }, { "SETTINGS", "Settings", navicon::Settings }, { "HOST", "Host parameters", navicon::Automation },
        { "LFO", "LFO", navicon::LFO }, { "CROSS MOD", "Cross Mod", navicon::CrossMod }, { "VOCODER", "Vocoder", navicon::Vocoder }, { "MIXER", "Mixer", navicon::Mixer }, { "SPECTROGRAM", "Spectrogram", navicon::Spectrogram } };
    for (int i = 0; i < pages_.size(); ++i) {
        auto* b = tabs_.add(new juce::TextButton(names[i])); b->getProperties().set("tabBar", true);
        for (const auto& l : looks) if (juce::String(l.name) == names[i]) { b->getProperties().set("navIcon", (int) l.icon); b->setTooltip(l.label); b->setTitle(l.label); b->setDescription(juce::String("Section: ") + l.label); }
        b->onClick = [this, i] { selectTab(i); showHint(i); }; addAndMakeVisible(b);
    }
    hint_.setJustificationType(juce::Justification::centred); hint_.setFont(VVLookAndFeel::mono(11.f).boldened()); hint_.setColour(juce::Label::textColourId, juce::Colour(0xffd7f7fb));
    hint_.setColour(juce::Label::backgroundColourId, juce::Colour(0xf0080c10)); hint_.setColour(juce::Label::outlineColourId, col::cyan.withAlpha(0.55f)); hint_.setInterceptsMouseClicks(false, false); addChildComponent(hint_);
    title_.setText("VEKTUR VORTEX", juce::dontSendNotification); title_.setFont(VVLookAndFeel::mono(18.f).boldened()); title_.setColour(juce::Label::textColourId, col::cyan); addAndMakeVisible(title_);
    status_.setColour(juce::Label::textColourId, col::dim); status_.setJustificationType(juce::Justification::centredRight); addAndMakeVisible(status_);
    {   // top preset bar (web: ◀ name ▶ | QUICK SAVE | SAVE AS | LOAD | INIT | UNDO | REDO)
        struct B { juce::TextButton* b; const char* t; std::function<void()> f; };
        auto presetsTab = [this] { for (int i = 0; i < tabs_.size(); ++i) if (tabs_[i]->getButtonText() == "PRESETS") { selectTab(i); break; } };
        auto findBtn = [this](const juce::String& t) -> juce::TextButton* { std::function<juce::TextButton*(juce::Component*)> f = [&](juce::Component* c) -> juce::TextButton* { if (auto* b = dynamic_cast<juce::TextButton*>(c)) if (b->getButtonText() == t) return b; for (auto* k : c->getChildren()) if (auto* r = f(k)) return r; return nullptr; }; return pages_.size() > 6 ? f(pages_[6]) : nullptr; };
        const B bs[] = { { &prevB_, "<", [this] { stepPreset(-1); } }, { &nextB_, ">", [this] { stepPreset(1); } }, { &quickSaveB_, "QUICK SAVE", [this] { quickSave(); } },
            { &saveAsB_, "SAVE AS", [presetsTab, findBtn] { presetsTab(); if (auto* b = findBtn("Save as...")) b->triggerClick(); } }, { &loadB_, "LOAD", [presetsTab, findBtn] { presetsTab(); if (auto* b = findBtn("Import .json")) b->triggerClick(); } },
            { &initB_, "INIT", [this, findBtn] { if (auto* b = findBtn("Initialise")) b->triggerClick(); flash("Initialised"); } },
            { &genB_, "GENERATE", [this] { juce::PopupMenu m; int i = 1; for (auto* c : { "Arp", "Bass", "Key", "Lead", "Pad", "Drum", "FX", "XFX" }) m.addItem(i++, c);
                m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&genB_), [this](int r) { if (r <= 0) return; const char* cs[] = { "Arp", "Bass", "Key", "Lead", "Pad", "Drum", "FX", "XFX" }; const auto n = proc_.store().generatePreset(cs[r - 1], proc_.userPresetDir()); flash(n.isNotEmpty() ? "Generated " + n : "Generate failed"); }); } },
            { &undoB_, "UNDO", [this] { if (proc_.store().undo()) flash("Undo"); } }, { &redoB_, "REDO", [this] { if (proc_.store().redo()) flash("Redo"); } } };
        for (auto& b : bs) { b.b->setButtonText(b.t); b.b->onClick = b.f; addAndMakeVisible(*b.b); }
        presetName_.setJustificationType(juce::Justification::centred); presetName_.setColour(juce::Label::textColourId, col::cyan); presetName_.setFont(VVLookAndFeel::mono(12.f).boldened()); addAndMakeVisible(presetName_);
        presetName_.setMouseCursor(juce::MouseCursor::PointingHandCursor); presetName_.setTooltip("Browse presets"); nameClick_.f = [this] { showPresetTree(presetName_, proc_.store(), proc_.userPresetDir(), [this](const juce::String& n) { flash("Loaded " + n); }); }; presetName_.addMouseListener(&nameClick_, false);
    }
    look::apply(p.store(), laf_); lastBg_ = look::effectiveBackground(p.store());
    midi::applyActive(p.store());
    Knob::onLearnPick = [this](Knob& k) { vv_engine_cc_learn_arm(proc_.engine(), k.path().toRawUTF8(), k.slider.getMinimum(), k.slider.getMaximum()); learnWatch_ = true; flash("Move a hardware control now to map " + k.path()); };
    selectTab(juce::jlimit(0, pages_.size() - 1, p.uiTab.load())); startTimerHz(4);
}
Editor::~Editor() { Knob::onLearnPick = nullptr; Knob::learnMode = false; setLookAndFeel(nullptr); }
void Editor::viewLayer(int layer) { selectTab(1); if (auto* ss = dynamic_cast<SoundScreen*>(pages_[1])) ss->selectLayer(layer); }
void Editor::setFooterVisible(bool on) { if (footer_) { footer_->setVisible(on); resized(); } }
void Editor::selectTab(int i) { sel_ = i; proc_.uiTab.store(i); for (int k = 0; k < pages_.size(); ++k) { pages_[k]->setVisible(k == i); tabs_[k]->setToggleState(k == i, juce::dontSendNotification); } resized(); }
void Editor::paint(juce::Graphics& g) {
    g.fillAll(col::bg);
    if (auto img = look::loadBackground(look::effectiveBackground(proc_.store())); img.isValid()) {   // cover-fit, dimmed so panels stay readable
        const float s = juce::jmax((float) getWidth() / (float) img.getWidth(), (float) getHeight() / (float) img.getHeight());
        const float w = img.getWidth() * s, h = img.getHeight() * s; g.drawImage(img, juce::Rectangle<float>(((float) getWidth() - w) * 0.5f, ((float) getHeight() - h) * 0.5f, w, h)); g.fillAll(juce::Colours::black.withAlpha(0.35f));
    }
    if (logo_.isNull()) { const auto f = assets::root().getChildFile("logo.png"); if (f.existsAsFile()) logo_ = juce::ImageFileFormat::loadFrom(f); }
    if (logo_.isValid() && !logoR_.isEmpty()) g.drawImage(logo_, logoR_.toFloat(), juce::RectanglePlacement::centred);
    g.setColour(col::line); g.drawHorizontalLine(52, 0.f, (float) getWidth()); g.drawHorizontalLine(84, 0.f, (float) getWidth()); }
void Editor::showHint(int i) {
    if (i < 0 || i >= tabs_.size()) return; const auto t = tabs_[i]->getTooltip(); if (t.isEmpty()) return;
    const int w = juce::jmax(60, (int) VVLookAndFeel::mono(11.f).boldened().getStringWidthFloat(t) + 22); auto b = tabs_[i]->getBounds();
    hint_.setText(t, juce::dontSendNotification); hint_.setBounds(juce::Rectangle<int>(w, 22).withCentre({ b.getCentreX(), b.getBottom() + 14 }).withX(juce::jlimit(2, juce::jmax(2, getWidth() - w - 2), b.getCentreX() - w / 2)));
    hint_.setVisible(true); hint_.toFront(false); hintUntil_ = juce::Time::getMillisecondCounter() + 1300;
}
void Editor::flash(const juce::String& m) { status_.setText(m, juce::dontSendNotification); flashUntil_ = juce::Time::getMillisecondCounter() + 2500; }
void Editor::stepPreset(int dir) {
    auto files = allPresetFiles(proc_.userPresetDir()); if (files.isEmpty()) return;
    int cur = -1; const auto nm = proc_.store().presetName(); for (int i = 0; i < files.size(); ++i) if (files[i].getFileNameWithoutExtension() == nm) { cur = i; break; }
    const int n = files.size(); const int next = cur < 0 ? (dir > 0 ? 0 : n - 1) : ((cur + dir) % n + n) % n;
    if (proc_.store().loadJSON(files[next].loadFileAsString())) { proc_.store().setPresetName(files[next].getFileNameWithoutExtension()); flash("Loaded " + files[next].getFileNameWithoutExtension()); }
}
void Editor::quickSave() {
    auto& st = proc_.store(); auto name = st.presetName();
    if (name.isEmpty()) name = "Quick save " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H%M%S");
    auto dir = proc_.userPresetDir(); dir.createDirectory();
    if (dir.getChildFile(name + ".json").replaceWithText(st.exportPresetFile())) { st.setPresetName(name); flash("Saved " + name); } else flash("Save failed");
}
void Editor::resized() {
    if (getWidth() >= 420 && getHeight() >= 480) { proc_.uiWidth.store(getWidth()); proc_.uiHeight.store(getHeight()); }
    auto r = getLocalBounds(); auto bar = r.removeFromTop(52).reduced(8, 8);
    {   // preset bar under the tabs
        auto pb = r.removeFromTop(32).reduced(6, 3); const int bw = juce::jlimit(54, 110, pb.getWidth() / 10);
        prevB_.setBounds(pb.removeFromLeft(34)); presetName_.setBounds(pb.removeFromLeft(juce::jmax(120, pb.getWidth() - 7 * bw - 40)).reduced(4, 0)); nextB_.setBounds(pb.removeFromLeft(34)); pb.removeFromLeft(6);
        for (auto* b : { &quickSaveB_, &saveAsB_, &loadB_, &initB_, &genB_, &undoB_, &redoB_ }) b->setBounds(pb.removeFromLeft(bw).reduced(2, 0));
    }
    const bool narrow = getWidth() < 760;
    logoR_ = bar.removeFromLeft(30).withSizeKeepingCentre(24, 24);   // small logo, top left, before the app name
    if (!narrow) title_.setBounds(bar.removeFromLeft(190)); else title_.setBounds({});
    status_.setBounds(bar.removeFromRight(narrow ? 0 : 160));
    const int n = tabs_.size(); if (n == 0) return; const int w = juce::jmin(112, bar.getWidth() / n);
    static const int order[] = { 1, 5, 2, 4, 3, 9, 10, 11, 0, 12, 13, 6, 7, 8 };   // OSCILLATORS, XY PADS, MATRIX, FX, SEQUENCER, LFO, CROSS MOD, VOCODER, SCOPE, PRESETS, SETTINGS, HOST
    for (int k : order) if (k < n) tabs_[k]->setBounds(bar.removeFromLeft(w).reduced(2, 0));
    if (footer_ && footer_->isVisible()) { const int fh = juce::jmin(footerH_(r.getWidth()), r.getHeight() * 75 / 100); footer_->setBounds(r.removeFromBottom(fh)); }
    for (auto* pg : pages_) pg->setBounds(r);
}
void Editor::timerCallback() {
    if (hint_.isVisible() && juce::Time::getMillisecondCounter() > hintUntil_) hint_.setVisible(false);
    if (learnWatch_) { const int c = vv_engine_cc_learned(proc_.engine()); if (c >= 0) { learnWatch_ = false; midi::saveActive(proc_.store()); flash("Mapped CC " + juce::String(c) + " - click another dial to continue, or turn MIDI LEARN off"); } }
    { const int bgNow = look::effectiveBackground(proc_.store()); const bool styleChanged = look::apply(proc_.store(), laf_);
      if (styleChanged) { sendLookAndFeelChange(); title_.setColour(juce::Label::textColourId, col::cyan); presetName_.setColour(juce::Label::textColourId, col::cyan); repaint(); }
      else if (bgNow != lastBg_) repaint();
      lastBg_ = bgNow; }
    presetName_.setText(proc_.store().presetName().isEmpty() ? "(none loaded)" : proc_.store().presetName(), juce::dontSendNotification);
    undoB_.setEnabled(proc_.store().canUndo()); redoB_.setEnabled(proc_.store().canRedo());
    if (juce::Time::getMillisecondCounter() < flashUntil_) return;
    status_.setText(proc_.assetsReady() ? (vv_engine_seq_running(proc_.engine()) ? "SEQ RUNNING" : "READY") : "LOADING...", juce::dontSendNotification);
}

} // namespace vv
