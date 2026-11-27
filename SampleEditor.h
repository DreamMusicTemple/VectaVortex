// Sampler waveform editor (the web app's slot waveform + FULL SCREEN EDIT): start/end markers, fade shading, loop markers, up to 16 numbered
// transient (slice) markers with per-slice reverse / pan, zoom + pan (wheel, pinch, drag), live playhead, CROP / CUT (silence) / NORMALIZE /
// DETECT TRANSIENTS, snap-to-zero-crossing, and a full-screen overlay with the slice list and slice audition.
// Edits that change the audio write a processed sample into the preset's sampleLibrary (like the web's in-place buffer edit, but undoable).
#pragma once
#include "Widgets.h"

namespace vv {

struct SliceMark { double pos = 0, pan = 0; bool reverse = false; };

class SampleEditorPanel : public juce::Component, private Store::Listener, private juce::Timer {
public:
    // rebuild: called (by the owner, asynchronously) when the slot's sample name changed so the page can refresh its controls.
    SampleEditorPanel(Store& s, int slot, bool full, std::function<void()> rebuild, std::function<void()> openFull = {});
    ~SampleEditorPanel() override;
    void resized() override;
    void paint(juce::Graphics&) override;

    // actions (also driven by the tests)
    void cropToSelection(); void cutSilence(); void normalizeAudio(); void detectTransients(); void sendTransientsToMpc(); void openWarp();
    void zoomBy(double factor); void resetView();
    void setOpenFull(std::function<void()> f) { openFullCb = std::move(f); }
    juce::String status() const { return statusText; }
    std::vector<SliceMark> slices() const;
    void setSlices(std::vector<SliceMark> v);
    int loadedLength() const { return (int) data.size(); }
    double viewStart() const { return vs; }
    double viewEnd() const { return ve; }
    bool snapEnabled() const { return snapZc.getToggleState(); }
    juce::Component& waveComponent();
    static constexpr int kMaxSlices = 16;

    class Wave;
private:
    friend class Wave; friend class SliceList;
    void storeChanged() override { refresh(); }
    void timerCallback() override;
    void refresh();
    bool loadAudio();
    bool readAll(std::vector<std::vector<float>>& ch, int& rate) const;
    void applyEdit(const std::vector<std::vector<float>>& ch, int rate, const juce::String& suffix, bool resetMarkers);
    void clampView(); void zoomAt(double centre, double factor);
    Store& store; const int slot; const bool fullScreen; const juce::String path;
    std::function<void()> rebuildCb, openFullCb;
    std::vector<float> data; double sr = 0; juce::String loadedName, statusText; int dataRev = 0;
    double vs = 0, ve = 1; float head = -1.f;
    std::unique_ptr<Wave> wave; std::unique_ptr<juce::Component> list;
    juce::OwnedArray<juce::TextButton> buttons; juce::ToggleButton snapZc; juce::Label statusLabel;
    int buttonRows = 1; void layoutButtons(juce::Rectangle<int> area, bool apply);
};

// Full-screen overlay: covers the top-level component (editor window / AUv3 view) until closed.
void openSampleEditorFullScreen(juce::Component& anchor, Store& s, int slot, std::function<void()> onClose);

} // namespace vv
