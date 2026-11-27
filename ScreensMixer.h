// MIXER tab (web #mixerPanel): per-track pan / MOD / fader / meter. See ScreensMixer.cpp.
#pragma once
#include "Widgets.h"
struct VVEngine;
namespace vv {
class MixerScreen : public juce::Component, private juce::Timer {
public:
    MixerScreen(Store&, VVEngine*);
    ~MixerScreen() override;
    void resized() override;
    class Strip; class LimiterCol;
    std::function<void(const juce::String&)> onEditAutomation;   // E button on a strip: Editor opens the automation screen on that channel
private:
    void timerCallback() override; void layoutStrips(); void updateButton();
    Store& store; VVEngine* engine; bool expanded = false;
    juce::Viewport view; juce::Component content; juce::OwnedArray<Strip> strips; juce::TextButton expandB; Strip* masterStrip = nullptr; std::unique_ptr<LimiterCol> limiter;
};
}
