// AUTOMATION screen (web: #page-automation): draw MIDI-CC style lanes for every matrix destination and source. See ScreensAuto.cpp.
#pragma once
#include "Widgets.h"
struct VVEngine;
namespace vv {
std::vector<std::pair<juce::String, juce::StringArray>> modDestGroups();   // ScreensMod.cpp: the matrix vocabulary
juce::StringArray modSourceList();

class AutomationScreen : public juce::Component, private Store::Listener, private juce::Timer {
public:
    explicit AutomationScreen(Store&, VVEngine* engine = nullptr);
    ~AutomationScreen() override;
    void setChannel(const juce::String& mixerChannel);   // mixer E button; empty = all lanes
    void resized() override;
    class Canvas;
private:
    void storeChanged() override { dirty_ = true; }
    void timerCallback() override;
    void rebuild();
    Store& store; VVEngine* engine; bool dirty_ = true; juce::String channel_;
    juce::ComboBox destBox, srcBox, barsBox, snapBox; juce::OwnedArray<juce::TextButton> toolBtns; juce::TextButton zoomIn, zoomOut, laneOn, clearB, chanChip; juce::Label info;
    juce::OwnedArray<juce::TextButton> chips; juce::Viewport view; std::unique_ptr<Canvas> canvas;
    juce::StringArray destIds_, srcIds_;
    friend class Canvas;
};
}
