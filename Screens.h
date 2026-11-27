// The screens (one Component per tab). Each is built from path-bound controls; see Widgets.h.
#pragma once
#include "Widgets.h"

namespace vv {

// A row of mutually exclusive buttons that shows one child page at a time.
class SubTabs : public juce::Component {
public:
    void add(const juce::String& name, juce::Component* page);   // page is owned by the caller
    void select(int i);
    int selected() const { return sel_; }
    void resized() override;
    std::function<void(int)> onChange;
private:
    juce::OwnedArray<juce::TextButton> buttons_; juce::Array<juce::Component*> pages_; int sel_ = -1;
};

class AutomationScreen;
class SoundScreen : public juce::Component {
public:
    explicit SoundScreen(Store&);
    void selectLayer(int i) { tabs.select(i); }   // 0 WT, 1 FM, 2 SMP, 3 PRO, 4 SID (footer VIEW buttons), 5 AUTOMATION
    AutomationScreen* automation() const { return automation_; }
    void showAutomation(const juce::String& mixerChannel);   // mixer E button: open the automation screen on that channel
    void resized() override { tabs.setBounds(getLocalBounds()); }
private:
    SubTabs tabs; juce::OwnedArray<juce::Component> pages_; AutomationScreen* automation_ = nullptr;
};

class ModScreen : public juce::Component {
public:
    // which < 0: all four pages behind sub-tabs; 0..3 = a single page (web top-level tabs LFO / MATRIX / CROSS MOD / VOCODER)
    explicit ModScreen(Store&, int which = -1);
    void resized() override { tabs.setBounds(getLocalBounds()); if (single_) single_->setBounds(getLocalBounds()); }
private:
    SubTabs tabs; juce::OwnedArray<juce::Component> pages_; juce::Component* single_ = nullptr;
};

class SeqScreen : public juce::Component {
public:
    explicit SeqScreen(Store&);
    void resized() override { tabs.setBounds(getLocalBounds()); }
private:
    SubTabs tabs; juce::OwnedArray<juce::Component> pages_;
};

juce::Component* makeFxScreen(Store&);
juce::Component* makeSettingsScreen(Store&);
juce::Component* makePresetsScreen(Store&, const juce::File& userDir);
/// Top-bar preset name menu: Factory / User Presets / Banks -> bank -> category -> preset. `onLoaded(name)` runs after a preset was loaded.
void showPresetTree(juce::Component& target, Store& store, const juce::File& userDir, std::function<void(const juce::String&)> onLoaded);
juce::Array<juce::File> allPresetFiles(const juce::File& userDir);   // factory then user presets, each sorted by name (top-bar prev / next)

} // namespace vv
