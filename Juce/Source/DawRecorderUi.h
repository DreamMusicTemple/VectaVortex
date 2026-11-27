// DAW-style RECORDER block of the web app's footer: REC, STOP, elapsed time, settings (bit depth, sample rate, click track, list of takes).
// Needed for standalone use. The engine side is vv_daw_* (Core/include/vv/daw_recorder.hpp).
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Store.h"
namespace vv {
class DawBlock : public juce::Component, private juce::Timer {
public:
    explicit DawBlock(Store& s);
    ~DawBlock() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void startRecording();      // also used by tests
    void stopRecording();
    bool isRecording() const;
    int numTakes() const;
    juce::File takeFile(int i) const;
    static juce::File recordingsFolder();
private:
    struct Impl; std::unique_ptr<Impl> impl;
    void timerCallback() override;
};
}
