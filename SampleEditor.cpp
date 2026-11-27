#include "SampleEditor.h"
#include "PowerSettings.h"
#include "AuthoringTools.h"
#include "vv_engine.h"
#include <algorithm>
#include <cmath>

namespace vv {

namespace {
const juce::Colour kWaveCol(0xff7fbf7f), kStartCol(0xff5fd97f), kEndCol(0xffe06a6a), kSliceCol(0xffe2d84a), kRevCol(0xffff6b6b), kLoopCol(0xffe8c84a);

// energy-onset detector - the web app's detectTransients(buffer, maxCount), capped at 16 slices
std::vector<SliceMark> detect(const std::vector<float>& d, double sr, int maxCount) {
    std::vector<SliceMark> out; const int n = (int) d.size(); if (n < 64 || sr <= 0) return out;
    const int win = std::max(8, (int) std::floor(sr * 0.01)), hop = std::max(4, (int) std::floor(sr * 0.005));
    struct E { int pos; double rms; }; std::vector<E> en;
    for (int i = 0; i + win < n; i += hop) { double sum = 0; for (int j = 0; j < win; ++j) { const double v = d[(size_t) (i + j)]; sum += v * v; } en.push_back({ i, std::sqrt(sum / win) }); }
    struct On { int pos; double strength; }; std::vector<On> on;
    for (size_t k = 1; k < en.size(); ++k) { const double delta = en[k].rms - en[k - 1].rms; if (delta > 0.015) on.push_back({ en[k].pos, delta }); }
    const double minGap = sr * 0.05; std::vector<On> merged;
    for (auto& o : on) { if (merged.empty() || o.pos - merged.back().pos > minGap) merged.push_back(o); else if (o.strength > merged.back().strength) merged.back() = o; }
    std::stable_sort(merged.begin(), merged.end(), [](const On& a, const On& b) { return a.strength > b.strength; });
    std::vector<On> top(merged.begin(), merged.begin() + std::min<size_t>(merged.size(), (size_t) maxCount));
    if ((int) top.size() < std::min(4, maxCount)) { top.clear(); const int need = std::min(8, maxCount); for (int i = 0; i < need; ++i) top.push_back({ (int) std::floor((double) i / need * n), 0 }); }
    std::sort(top.begin(), top.end(), [](const On& a, const On& b) { return a.pos < b.pos; });
    for (auto& o : top) { SliceMark m; m.pos = juce::jlimit(0.0, 0.99, (double) o.pos / n); out.push_back(m); }
    return out;
}

int nearestZeroCrossing(const std::vector<float>& d, int idx) {
    int best = idx, bestDist = 1 << 30; const int n = (int) d.size();
    for (int k = -200; k <= 200; ++k) { const int i = idx + k; if (i < 1 || i >= n) continue; const bool cross = d[(size_t) i] == 0.f || ((d[(size_t) i - 1] < 0) != (d[(size_t) i] < 0)); if (cross && std::abs(k) < bestDist) { bestDist = std::abs(k); best = i; } }
    return best;
}
}

// ---------------------------------------------------------------------------------------------------------------- waveform canvas
class SampleEditorPanel::Wave : public juce::Component {
public:
    explicit Wave(SampleEditorPanel& o) : ed(o) { setWantsKeyboardFocus(false); }
    void paint(juce::Graphics& g) override {
        const int W = getWidth(), H = getHeight(); g.fillAll(juce::Colours::black);
        auto& s = ed.store; const juce::String p = ed.path;
        if (ed.data.empty()) { g.setColour(col::dim); g.setFont(VVLookAndFeel::mono(12.f)); g.drawText(s.str(p + ".name").isEmpty() ? "No sample in this slot - pick one, IMPORT, RECORD or IMAGE > AUDIO" : "Sample not found in the library", getLocalBounds(), juce::Justification::centred); return; }
        const double st = s.num(p + ".start", 0), en = s.num(p + ".end", 1), dur = (double) ed.data.size() / std::max(1.0, ed.sr);
        const double fi = s.num(p + ".fadeIn", 0), fo = s.num(p + ".fadeOut", 0);
        const float sx = X(st), ex = X(en), fix = X(st + fi / dur), fox = X(en - fo / dur);
        g.setColour(juce::Colour(0x477a8a4a)); g.fillRect(juce::Rectangle<float>(sx, 0, std::max(0.f, fix - sx), (float) H));
        g.setColour(juce::Colour(0x478a4a4a)); g.fillRect(juce::Rectangle<float>(std::min(fox, ex), 0, std::max(0.f, ex - std::min(fox, ex)), (float) H));
        // shade what start/end exclude
        g.setColour(juce::Colour(0x66000000)); g.fillRect(juce::Rectangle<float>(0, 0, std::max(0.f, sx), (float) H)); g.fillRect(juce::Rectangle<float>(std::min((float) W, ex), 0, std::max(0.f, W - ex), (float) H));
        // min/max columns (cached per view + width: repainting the playhead stays cheap even for long recordings)
        if (cacheW != W || cacheVs != ed.vs || cacheVe != ed.ve || cacheRev != ed.dataRev) rebuildCache(W);
        g.setColour(kWaveCol); const float mid = H * 0.5f;
        for (int x = 0; x < W; ++x) { const auto& c = cols[(size_t) x]; if (c.first > c.second) continue; g.drawVerticalLine(x, mid - c.second * mid, std::max(mid - c.second * mid + 1.f, mid - c.first * mid)); }
        g.setColour(juce::Colour(0x33ffffff)); g.drawHorizontalLine((int) mid, 0.f, (float) W);
        const double ls = s.num(p + ".loopStartFrac", -1), le = s.num(p + ".loopEndFrac", -1);
        if (s.flag(p + ".loop", false) && ls >= 0 && le >= 0) { g.setColour(kLoopCol); for (double f : { ls, le }) { const float x = X(f); for (float y = 0; y < H; y += 7) g.drawVerticalLine((int) x, y, std::min((float) H, y + 4)); } }
        g.setColour(juce::Colour(0xff4a8a4a)); g.fillRect(sx - 1, 0.f, 2.f, (float) H); g.setColour(kStartCol); { juce::Path t; t.addTriangle(sx, 0, sx + 12, 0, sx, 14); g.fillPath(t); }
        g.setColour(juce::Colour(0xffc94a4a)); g.fillRect(ex - 1, 0.f, 2.f, (float) H); g.setColour(kEndCol); { juce::Path t; t.addTriangle(ex, 0, ex - 12, 0, ex, 14); g.fillPath(t); }
        const auto sl = ed.slices(); g.setFont(VVLookAndFeel::mono(11.f));
        for (size_t i = 0; i < sl.size(); ++i) { const float x = X(sl[i].pos); const auto c = sl[i].reverse ? kRevCol : kSliceCol; g.setColour(c); g.fillRect(x - (dragKind == Slice && dragIdx == (int) i ? 1.5f : 0.75f), 0.f, dragKind == Slice && dragIdx == (int) i ? 3.f : 1.5f, (float) H);
            g.drawText(juce::String((int) i + 1), (int) x + 3, H - 16, 24, 14, juce::Justification::left);
            if (std::abs(sl[i].pan) > 0.02) { const float a = std::abs((float) sl[i].pan); g.fillRect(x + 3, 4.f, 3.f + 14.f * a, 2.f); } }
        // live playhead (engine voice position) + the slice it is inside
        if (ed.head >= 0.f) {
            int gi = -1; for (int i = (int) sl.size() - 1; i >= 0; --i) if (sl[(size_t) i].pos <= ed.head) { gi = i; break; }
            if (gi >= 0) { const float x0 = X(sl[(size_t) gi].pos), x1 = X(gi + 1 < (int) sl.size() ? sl[(size_t) gi + 1].pos : en); g.setColour(juce::Colour(0x47ffec6e)); g.fillRect(x0, 0.f, std::max(2.f, x1 - x0), (float) H); }
            g.setColour(juce::Colours::white); g.fillRect(X(ed.head) - 1.f, 0.f, 2.f, (float) H);
        }
        g.setColour(col::line); g.drawRect(getLocalBounds());
    }
    // view <-> pixel
    float X(double f) const { return (float) ((f - ed.vs) / (ed.ve - ed.vs) * getWidth()); }
    double F(float x) const { return ed.vs + juce::jlimit(0.0, 1.0, (double) x / std::max(1, getWidth())) * (ed.ve - ed.vs); }

    void mouseDown(const juce::MouseEvent& e) override {
        if (ed.data.empty()) return;
        auto& s = ed.store; const juce::String p = ed.path; const auto sl = ed.slices(); dragKind = None; dragIdx = -1;
        if (e.mods.isShiftDown() || e.mods.isMiddleButtonDown()) { dragKind = Pan; pan0 = { ed.vs, ed.ve }; return; }
        const bool onHandle = e.position.y < 16.f && (std::abs(X(s.num(p + ".start", 0)) - e.position.x) < 9.f || std::abs(X(s.num(p + ".end", 1)) - e.position.x) < 9.f);   // the triangle handles win over a slice marker beneath them
        int ti = -1; float best = onHandle ? 0.f : 7.f; for (size_t i = 0; i < sl.size(); ++i) { const float d = std::abs(X(sl[i].pos) - e.position.x); if (d < best) { best = d; ti = (int) i; } }
        if (e.mods.isPopupMenu()) { if (ti >= 0) sliceMenu(ti); return; }
        if (e.mods.isAltDown()) {   // alt-click adds a slice marker
            if ((int) sl.size() < kMaxSlices) { auto v = sl; SliceMark m; m.pos = snapped(F(e.position.x)); v.push_back(m); ed.setSlices(v); ed.statusText = juce::String(v.size()) + " slices"; ed.statusLabel.setText(ed.statusText, juce::dontSendNotification); }
            return; }
        if (ti >= 0) { dragKind = Slice; dragIdx = ti; repaint(); return; }
        const double ls = s.num(p + ".loopStartFrac", -1), le = s.num(p + ".loopEndFrac", -1);
        if (s.flag(p + ".loop", false) && ls >= 0 && le >= 0) { if (std::abs(X(ls) - e.position.x) < 6.f) { dragKind = LoopS; return; } if (std::abs(X(le) - e.position.x) < 6.f) { dragKind = LoopE; return; } }
        const float sx = X(s.num(p + ".start", 0)), ex = X(s.num(p + ".end", 1));
        dragKind = std::abs(e.position.x - sx) <= std::abs(e.position.x - ex) ? Start : End; mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (ed.data.empty() || dragKind == None) return;
        auto& s = ed.store; const juce::String p = ed.path;
        if (dragKind == Pan) { const double dx = -(double) e.getDistanceFromDragStartX() / std::max(1, getWidth()) * (pan0.second - pan0.first); ed.vs = pan0.first + dx; ed.ve = pan0.second + dx; ed.clampView(); repaint(); return; }
        const double t = snapped(F(e.position.x));
        if (dragKind == Start) s.set(p + ".start", juce::jlimit(0.0, s.num(p + ".end", 1) - 0.01, t));
        else if (dragKind == End) s.set(p + ".end", juce::jlimit(s.num(p + ".start", 0) + 0.01, 1.0, t));
        else if (dragKind == LoopS) s.set(p + ".loopStartFrac", juce::jlimit(0.0, s.num(p + ".loopEndFrac", 1) - 0.001, t));
        else if (dragKind == LoopE) s.set(p + ".loopEndFrac", juce::jlimit(s.num(p + ".loopStartFrac", 0) + 0.001, 1.0, t));
        else if (dragKind == Slice) { auto v = ed.slices(); if (dragIdx >= 0 && dragIdx < (int) v.size()) { v[(size_t) dragIdx].pos = juce::jlimit(0.0, 1.0, F(e.position.x)); const double np = v[(size_t) dragIdx].pos; ed.setSlices(v); const auto after = ed.slices(); for (size_t i = 0; i < after.size(); ++i) if (std::abs(after[i].pos - np) < 1e-9) dragIdx = (int) i; } }
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { dragKind = None; dragIdx = -1; repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { ed.resetView(); }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        if (std::abs(w.deltaX) > std::abs(w.deltaY)) { const double d = -w.deltaX * (ed.ve - ed.vs) * 0.5; ed.vs += d; ed.ve += d; ed.clampView(); repaint(); return; }
        ed.zoomAt(F(e.position.x), std::exp(w.deltaY * (w.isReversed ? -1.0 : 1.0) * 1.6));
    }
    void mouseMagnify(const juce::MouseEvent& e, float scale) override { ed.zoomAt(F(e.position.x), scale); }

    double snapped(double t) const { if (!ed.snapZc.getToggleState() || ed.data.empty()) return juce::jlimit(0.0, 1.0, t); const int n = (int) ed.data.size(); return (double) nearestZeroCrossing(ed.data, (int) std::floor(t * n)) / n; }
    void invalidate() { cacheRev = -1; repaint(); }
    int dragIdxNow() const { return dragIdx; }

private:
    enum Kind { None, Start, End, Slice, LoopS, LoopE, Pan };
    void sliceMenu(int ti) {
        juce::PopupMenu m; const auto v = ed.slices(); m.addItem(1, v[(size_t) ti].reverse ? "Play forwards" : "Reverse this slice"); m.addItem(2, "Centre pan"); m.addItem(3, "Delete slice " + juce::String(ti + 1));
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this, ti](int r) { auto v2 = ed.slices(); if (ti >= (int) v2.size() || r == 0) return;
            if (r == 1) v2[(size_t) ti].reverse = !v2[(size_t) ti].reverse; else if (r == 2) v2[(size_t) ti].pan = 0; else v2.erase(v2.begin() + ti); ed.setSlices(v2); repaint(); });
    }
    void rebuildCache(int W) {
        cacheW = W; cacheVs = ed.vs; cacheVe = ed.ve; cacheRev = ed.dataRev; cols.assign((size_t) std::max(0, W), { 1.f, -1.f });
        const int n = (int) ed.data.size(); const double i0 = ed.vs * n, span = (ed.ve - ed.vs) * n; if (n == 0 || W <= 0) return;
        for (int x = 0; x < W; ++x) {
            const long a = (long) std::floor(i0 + span * x / W), b = (long) std::floor(i0 + span * (x + 1) / W); float mn = 1.f, mx = -1.f;
            for (long i = std::max(0L, a); i <= std::min((long) n - 1, std::max(a, b)); ++i) { const float v = ed.data[(size_t) i]; mn = std::min(mn, v); mx = std::max(mx, v); }
            if (mn <= mx) cols[(size_t) x] = { mn, mx };
        }
    }
    SampleEditorPanel& ed; Kind dragKind = None; int dragIdx = -1; std::pair<double, double> pan0{ 0, 1 };
    std::vector<std::pair<float, float>> cols; int cacheW = -1, cacheRev = -1; double cacheVs = -1, cacheVe = -1;
};

// ---------------------------------------------------------------------------------------------------------------- slice list (full screen)
class SliceList : public juce::Component {
public:
    explicit SliceList(SampleEditorPanel& o) : ed(o) { addAndMakeVisible(view); view.setViewedComponent(&content, false); view.setScrollBarsShown(true, false); }
    void resized() override { view.setBounds(getLocalBounds()); layoutRows(); }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff0d0f13)); if (rows.isEmpty()) { g.setColour(col::dim); g.setFont(VVLookAndFeel::mono(12.f)); g.drawText("No slices yet - DETECT TRANSIENTS, or alt-click the waveform to add one (up to 16).", getLocalBounds(), juce::Justification::centred); } }
    void sync() {
        const auto v = ed.slices();
        if ((int) v.size() != rows.size()) { rows.clear(); for (int i = 0; i < (int) v.size(); ++i) rows.add(new Row(*this, i)); content.removeAllChildren(); for (auto* r : rows) content.addAndMakeVisible(r); layoutRows(); repaint(); }
        for (int i = 0; i < rows.size(); ++i) rows[i]->set(v[(size_t) i]);
    }
private:
    struct Row : juce::Component {
        Row(SliceList& o, int i) : owner(o), idx(i) {
            auto lbl = [this](juce::Label& l, const juce::String& t) { l.setText(t, juce::dontSendNotification); l.setColour(juce::Label::textColourId, col::dim); l.setFont(VVLookAndFeel::mono(11.f)); addAndMakeVisible(l); };
            lbl(num, "#" + juce::String(i + 1)); lbl(posL, "pos"); lbl(panL, "pan");
            for (auto* s : { &pos, &pan }) { s->setSliderStyle(juce::Slider::LinearBar); s->setTextBoxIsEditable(true); addAndMakeVisible(*s); }
            pos.setRange(0, 1, 0.0001); pos.setNumDecimalPlacesToDisplay(4); pan.setRange(-1, 1, 0.01); pan.setDoubleClickReturnValue(true, 0);
            rev.setButtonText("REVERSE"); addAndMakeVisible(rev); play.setButtonText("PLAY"); del.setButtonText("DEL"); addAndMakeVisible(play); addAndMakeVisible(del);
            pos.onValueChange = [this] { commit(); }; pan.onValueChange = [this] { commit(); }; rev.onClick = [this] { commit(); };
            del.onClick = [this] { auto v = owner.ed.slices(); if (idx < (int) v.size()) { v.erase(v.begin() + idx); owner.ed.setSlices(v); } };
            play.onStateChange = [this] { auto* eng = owner.ed.store.engine(); if (!eng) return; if (play.isDown() && !playing) { playing = true; vv_engine_audition_slice(eng, owner.ed.slot, idx + 1, 0.9f); } else if (!play.isDown() && playing) { playing = false; vv_engine_audition_off(eng); } };
        }
        void set(const SliceMark& m) { updating = true; pos.setValue(m.pos, juce::dontSendNotification); pan.setValue(m.pan, juce::dontSendNotification); rev.setToggleState(m.reverse, juce::dontSendNotification); updating = false; }
        void commit() { if (updating) return; auto v = owner.ed.slices(); if (idx >= (int) v.size()) return; v[(size_t) idx].pos = pos.getValue(); v[(size_t) idx].pan = pan.getValue(); v[(size_t) idx].reverse = rev.getToggleState(); owner.ed.setSlices(v); }
        void resized() override { auto r = getLocalBounds().reduced(2); num.setBounds(r.removeFromLeft(34)); del.setBounds(r.removeFromRight(54)); r.removeFromRight(4); play.setBounds(r.removeFromRight(60)); r.removeFromRight(4);
            rev.setBounds(r.removeFromRight(96)); r.removeFromRight(4); const int w = (r.getWidth() - 70) / 2; posL.setBounds(r.removeFromLeft(30)); pos.setBounds(r.removeFromLeft(w)); r.removeFromLeft(6); panL.setBounds(r.removeFromLeft(30)); pan.setBounds(r.removeFromLeft(w)); }
        SliceList& owner; int idx; bool updating = false, playing = false; juce::Label num, posL, panL; juce::Slider pos, pan; juce::ToggleButton rev; juce::TextButton play, del;
    };
    void layoutRows() { const int w = std::max(200, view.getMaximumVisibleWidth()); content.setSize(w, std::max(1, rows.size()) * 32); for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds(0, i * 32, w, 30); }
    SampleEditorPanel& ed; juce::Viewport view; juce::Component content; juce::OwnedArray<Row> rows;
};

// ---------------------------------------------------------------------------------------------------------------- panel
SampleEditorPanel::SampleEditorPanel(Store& s, int sl, bool full, std::function<void()> rebuild, std::function<void()> openFull)
    : store(s), slot(sl), fullScreen(full), path("layers.SMP.oscs." + juce::String(sl)), rebuildCb(std::move(rebuild)), openFullCb(std::move(openFull)) {
    wave = std::make_unique<Wave>(*this); addAndMakeVisible(*wave);
    auto add = [this](const juce::String& t, std::function<void()> fn) { auto* b = new juce::TextButton(t); b->setName(t); b->onClick = std::move(fn); buttons.add(b); addAndMakeVisible(b); return b; };
    add("CROP TO SELECTION", [this] { cropToSelection(); }); add("CUT (SILENCE)", [this] { cutSilence(); }); add("NORMALIZE", [this] { normalizeAudio(); });
    add("DETECT TRANSIENTS", [this] { detectTransients(); }); add("SEND TRANSIENTS TO MPC", [this] { sendTransientsToMpc(); }); add("WARP MARKERS...", [this] { openWarp(); });
    add("ZOOM -", [this] { zoomBy(1 / 1.6); }); add("ZOOM +", [this] { zoomBy(1.6); }); add("RESET VIEW", [this] { resetView(); });
    if (full) {
        add("AUDITION", [this] { if (auto* e = store.engine()) { vv_engine_audition_slice(e, slot, 0, 0.9f); juce::Timer::callAfterDelay(1500, [e] { vv_engine_audition_off(e); }); } });
        list = std::make_unique<SliceList>(*this); addAndMakeVisible(*list);
    } else add("FULL SCREEN EDIT", [this] { if (openFullCb) openFullCb(); });
    snapZc.setButtonText("Snap to zero-crossing"); addAndMakeVisible(snapZc);
    statusLabel.setFont(VVLookAndFeel::mono(11.f)); statusLabel.setColour(juce::Label::textColourId, col::cyan); addAndMakeVisible(statusLabel);
    store.addListener(this); loadAudio(); startTimerHz(30);
    if (auto* sl2 = dynamic_cast<SliceList*>(list.get())) sl2->sync();
}
SampleEditorPanel::~SampleEditorPanel() { store.removeListener(this); stopTimer(); if (auto* e = store.engine()) if (fullScreen) vv_engine_audition_off(e); }
juce::Component& SampleEditorPanel::waveComponent() { return *wave; }

bool SampleEditorPanel::loadAudio() {
    const auto name = store.str(path + ".name", {}); loadedName = name; data.clear(); sr = 0; ++dataRev;
    if (name.isEmpty()) return false;
    auto mb = authoring::sampleWav(store, name); if (mb.getSize() == 0) return false;
    VVAudio* a = vv_audio_from_wav((const uint8_t*) mb.getData(), mb.getSize()); if (!a) return false;
    const int n = vv_audio_length(a); data.resize((size_t) std::max(0, n)); if (n > 0) vv_audio_read(a, 0, data.data()); sr = vv_audio_sample_rate(a); vv_audio_free(a);
    return !data.empty();
}
bool SampleEditorPanel::readAll(std::vector<std::vector<float>>& ch, int& rate) const {
    auto mb = authoring::sampleWav(store, store.str(path + ".name", {})); if (mb.getSize() == 0) return false;
    VVAudio* a = vv_audio_from_wav((const uint8_t*) mb.getData(), mb.getSize()); if (!a) return false;
    const int n = vv_audio_length(a), nc = std::max(1, std::min(2, vv_audio_channels(a))); rate = vv_audio_sample_rate(a); ch.assign((size_t) nc, std::vector<float>((size_t) n));
    for (int c = 0; c < nc; ++c) vv_audio_read(a, c, ch[(size_t) c].data()); vv_audio_free(a); return n > 0;
}
void SampleEditorPanel::refresh() {
    if (store.str(path + ".name", {}) != loadedName) { loadAudio(); resetView(); }
    if (auto* sl = dynamic_cast<SliceList*>(list.get())) sl->sync();
    wave->repaint();
}
void SampleEditorPanel::timerCallback() {
    auto* e = store.engine(); if (!e || !isShowing()) return; VV_UI_THROTTLE(30);
    const float h = vv_engine_sample_head(e, slot); if (h != head) { head = h; wave->repaint(); }
}

std::vector<SliceMark> SampleEditorPanel::slices() const {
    std::vector<SliceMark> out; auto t = store.get(path + ".transients");
    if (t.isArray()) for (int i = 0; i < t.size(); ++i) { SliceMark m; if (t[i].isObject()) { m.pos = (double) t[i].getProperty("pos", 0.0); m.pan = (double) t[i].getProperty("pan", 0.0); m.reverse = (bool) t[i].getProperty("reverse", false); } else m.pos = (double) t[i]; out.push_back(m); }
    return out;
}
void SampleEditorPanel::setSlices(std::vector<SliceMark> v) {
    std::stable_sort(v.begin(), v.end(), [](const SliceMark& a, const SliceMark& b) { return a.pos < b.pos; }); if ((int) v.size() > kMaxSlices) v.resize((size_t) kMaxSlices);
    juce::Array<juce::var> arr; for (auto& m : v) { auto* o = new juce::DynamicObject(); o->setProperty("pos", juce::jlimit(0.0, 1.0, m.pos)); o->setProperty("reverse", m.reverse); o->setProperty("pan", juce::jlimit(-1.0, 1.0, m.pan)); arr.add(juce::var(o)); }
    store.set(path + ".transients", juce::var(arr));
}

void SampleEditorPanel::clampView() {
    const double minSpan = std::max(0.0005, 64.0 / std::max<size_t>(64, data.size())); double span = juce::jlimit(minSpan, 1.0, ve - vs);
    double c = (vs + ve) * 0.5; vs = c - span / 2; ve = c + span / 2; if (vs < 0) { ve -= vs; vs = 0; } if (ve > 1) { vs -= ve - 1; ve = 1; } vs = std::max(0.0, vs); ve = std::min(1.0, ve); wave->repaint();
}
void SampleEditorPanel::zoomAt(double centre, double factor) { const double span = (ve - vs) / factor, f = (centre - vs) / std::max(1e-9, ve - vs); vs = centre - f * span; ve = vs + span; clampView(); }
void SampleEditorPanel::zoomBy(double factor) { zoomAt((vs + ve) * 0.5, factor); }
void SampleEditorPanel::resetView() { vs = 0; ve = 1; wave->repaint(); }

void SampleEditorPanel::applyEdit(const std::vector<std::vector<float>>& ch, int rate, const juce::String& suffix, bool resetMarkers) {
    if (ch.empty() || ch[0].empty()) return;
    VVAudio* a = vv_audio_create(rate, (int) ch.size(), (int) ch[0].size(), ch[0].data(), ch.size() > 1 ? ch[1].data() : nullptr); if (!a) return;
    juce::MemoryBlock mb; const size_t n = vv_audio_to_wav16(a, nullptr, 0); mb.setSize(n); vv_audio_to_wav16(a, (uint8_t*) mb.getData(), n); vv_audio_free(a);
    juce::String base = store.str(path + ".name", "sample"); for (auto sfx : { " (crop)", " (cut)", " (norm)" }) if (base.endsWith(sfx)) base = base.dropLastCharacters(juce::String(sfx).length());
    const auto nm = authoring::uniqueSampleName(store, base + suffix); authoring::putSample(store, nm, mb);
    std::vector<std::pair<juce::String, juce::var>> kv{ { path + ".name", nm }, { path + ".origName", juce::var() } };
    if (resetMarkers) { kv.push_back({ path + ".start", 0.0 }); kv.push_back({ path + ".end", 1.0 }); kv.push_back({ path + ".transients", juce::var(juce::Array<juce::var>()) }); kv.push_back({ path + ".loopStartFrac", juce::var() }); kv.push_back({ path + ".loopEndFrac", juce::var() }); }
    store.setMany(kv); loadAudio(); if (resetMarkers) resetView(); wave->invalidate();
    if (rebuildCb) rebuildCb();
}

void SampleEditorPanel::cropToSelection() {
    std::vector<std::vector<float>> ch; int rate = 0; if (!readAll(ch, rate)) { statusLabel.setText("No sample to crop.", juce::dontSendNotification); return; }
    const int len = (int) ch[0].size(); const int s = juce::jlimit(0, len, (int) std::floor(store.num(path + ".start", 0) * len)), e = juce::jlimit(0, len, (int) std::floor(store.num(path + ".end", 1) * len));
    if (e - s < 64) { statusLabel.setText("Selection too short to crop.", juce::dontSendNotification); return; }
    for (auto& c : ch) c = std::vector<float>(c.begin() + s, c.begin() + e);
    applyEdit(ch, rate, " (crop)", true); statusLabel.setText("Cropped to the selection.", juce::dontSendNotification);
}
void SampleEditorPanel::cutSilence() {   // the web's cutBufferToSilence: silences start..end in place with a 3 ms fade at each edge
    std::vector<std::vector<float>> ch; int rate = 0; if (!readAll(ch, rate)) return;
    const int len = (int) ch[0].size(); const int s = std::max(0, (int) std::floor(store.num(path + ".start", 0) * len)), e = std::min(len, (int) std::ceil(store.num(path + ".end", 1) * len)); if (e <= s) return;
    const int fade = std::min((e - s) / 2, (int) std::lround(rate * 0.003));
    for (auto& c : ch) for (int i = s; i < e; ++i) { double g = 1; if (fade > 0) { if (i - s < fade) g = 1.0 - (double) (i - s) / fade; else if (e - i < fade) g = 1.0 - (double) (e - i) / fade; else g = 0; } c[(size_t) i] = (float) (c[(size_t) i] * g); }
    applyEdit(ch, rate, " (cut)", false); statusLabel.setText("Selection silenced.", juce::dontSendNotification);
}
void SampleEditorPanel::normalizeAudio() {
    std::vector<std::vector<float>> ch; int rate = 0; if (!readAll(ch, rate)) return; float peak = 0; for (auto& c : ch) for (float v : c) peak = std::max(peak, std::abs(v));
    if (peak <= 0) { statusLabel.setText("Silent sample - nothing to normalize.", juce::dontSendNotification); return; }
    const float g = 1.f / peak; for (auto& c : ch) for (auto& v : c) v *= g; applyEdit(ch, rate, " (norm)", false); statusLabel.setText("Normalized to full scale.", juce::dontSendNotification);
}
// Transient k (in waveform order, up to 16) becomes the one-shot of MPC pad k+1; later pads go back to their whole sample. Stored with the preset at
// extra.mpcPadSlice.<pad> = { osc, ti } (same shape as the web app); the MPC pad resolves it against the live transients when hit.
void SampleEditorPanel::sendTransientsToMpc() {
    const int n = juce::jmin(16, (int) slices().size()); if (n == 0) return;
    if (!store.has("extra")) store.set("extra", juce::var(new juce::DynamicObject()));
    auto* o = new juce::DynamicObject();
    for (int k = 0; k < n; ++k) { auto* e = new juce::DynamicObject(); e->setProperty("osc", slot); e->setProperty("ti", k); o->setProperty(juce::Identifier(juce::String(k)), juce::var(e)); store.setGlobal("mpcPad" + juce::String(k), slot); }
    store.set("extra.mpcPadSlice", juce::var(o));
}
void SampleEditorPanel::detectTransients() {
    if (data.empty()) return; auto v = detect(data, sr, kMaxSlices); setSlices(v);
    statusText = juce::String((int) v.size()) + " found - drag any line to adjust"; statusLabel.setText(statusText, juce::dontSendNotification);
}
void SampleEditorPanel::openWarp() { authoring::openSampleTools(store, slot, *buttons[4], [this] { juce::MessageManager::callAsync([alive = juce::Component::SafePointer<SampleEditorPanel>(this)] { if (alive) { alive->loadAudio(); alive->wave->invalidate(); if (alive->rebuildCb) alive->rebuildCb(); } }); }); }

void SampleEditorPanel::layoutButtons(juce::Rectangle<int> area, bool apply) {
    const int h = 26, gap = 4; int x = area.getX(), y = area.getY(), rows = 1; auto font = VVLookAndFeel::mono(13.f);
    auto place = [&](juce::Component& c, int w) { if (x + w > area.getRight() && x > area.getX()) { x = area.getX(); y += h + gap; ++rows; } if (apply) c.setBounds(x, y, w, h); x += w + gap; };
    for (auto* b : buttons) place(*b, font.getStringWidth(b->getButtonText()) + 22);
    place(snapZc, 190); buttonRows = rows;
}
void SampleEditorPanel::resized() {
    auto r = getLocalBounds(); layoutButtons(r.withHeight(0), false);   // measure the wrapped height first
    const int btnH = buttonRows * 30; statusLabel.setBounds(r.removeFromBottom(18));
    auto listArea = juce::Rectangle<int>(); if (list) listArea = r.removeFromBottom(std::max(110, r.getHeight() * 36 / 100));
    auto btn = r.removeFromBottom(btnH); layoutButtons(btn, true);
    if (list) list->setBounds(listArea); wave->setBounds(r.withTrimmedBottom(4));
}
void SampleEditorPanel::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff0d0f13)); }

// ---------------------------------------------------------------------------------------------------------------- full-screen overlay
namespace {
struct Overlay : juce::Component, juce::ComponentListener {
    Overlay(juce::Component& top_, Store& s, int slot, std::function<void()> onClose_) : top(&top_), onClose(std::move(onClose_)),
        ed(s, slot, true, [this] { dirty = true; }) {
        title.setText("SAMPLE EDITOR - slot " + juce::String(slot + 1) + ": " + s.str("layers.SMP.oscs." + juce::String(slot) + ".name", "(no sample)"), juce::dontSendNotification);
        title.setFont(VVLookAndFeel::mono(13.f)); title.setColour(juce::Label::textColourId, col::cyan); addAndMakeVisible(title);
        help.setText("drag start (green) / end (red) / yellow slice lines  |  wheel or pinch = zoom, shift-drag = pan, double-click = reset  |  alt-click adds a slice, right-click a slice for reverse / delete", juce::dontSendNotification);
        help.setFont(VVLookAndFeel::mono(10.f)); help.setColour(juce::Label::textColourId, col::dim); addAndMakeVisible(help);
        close.setButtonText("X  CLOSE"); close.onClick = [this] { finish(); }; addAndMakeVisible(close); addAndMakeVisible(ed);
        top->addAndMakeVisible(this); top->addComponentListener(this); setBounds(top->getLocalBounds()); toFront(true);
    }
    ~Overlay() override { if (top) top->removeComponentListener(this); }
    void finish() { auto cb = onClose; juce::MessageManager::callAsync([alive = juce::Component::SafePointer<Overlay>(this), cb] { if (alive) { delete alive.getComponent(); } if (cb) cb(); }); setVisible(false); }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xf0080a0d)); }
    void resized() override { auto r = getLocalBounds().reduced(10); auto top2 = r.removeFromTop(28); close.setBounds(top2.removeFromRight(110)); title.setBounds(top2); help.setBounds(r.removeFromTop(18)); ed.setBounds(r); }
    void componentBeingDeleted(juce::Component&) override { top = nullptr; juce::MessageManager::callAsync([alive = juce::Component::SafePointer<Overlay>(this)] { if (alive) delete alive.getComponent(); }); }
    void componentMovedOrResized(juce::Component&, bool, bool) override { if (top) setBounds(top->getLocalBounds()); }
    bool keyPressed(const juce::KeyPress& k) override { if (k == juce::KeyPress::escapeKey) { finish(); return true; } return false; }
    juce::Component* top; std::function<void()> onClose; bool dirty = false; juce::Label title, help; juce::TextButton close; SampleEditorPanel ed;
};
}

void openSampleEditorFullScreen(juce::Component& anchor, Store& s, int slot, std::function<void()> onClose) {
    auto* top = anchor.getTopLevelComponent(); if (!top) return;
    for (int i = 0; i < top->getNumChildComponents(); ++i) if (dynamic_cast<Overlay*>(top->getChildComponent(i))) return;
    new Overlay(*top, s, slot, std::move(onClose));   // owned by the top-level component's child list; deleted in finish()
}

} // namespace vv
