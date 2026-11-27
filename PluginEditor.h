#pragma once
#include "PluginProcessor.h"
#include "Widgets.h"

namespace vv {

class Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(Processor&);
    ~Editor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void selectTab(int i);
    void setFooterVisible(bool on);
    int getCurrentTab() const { return sel_; }
    void viewLayer(int layer);   // footer VIEW buttons: OSCILLATORS tab + layer sub-tab
    int numTabs() const { return tabs_.size(); }
    juce::Component& tabPage(int i) { return *pages_[i]; }
private:
    void timerCallback() override;
    Processor& proc_;
    VVLookAndFeel laf_; int lastBg_ = -1; bool learnWatch_ = false;
    juce::Image logo_; juce::Rectangle<int> logoR_;
    juce::OwnedArray<juce::TextButton> tabs_; juce::StringArray tabNames_; std::unique_ptr<juce::Component> footer_; std::function<int(int)> footerH_;
    juce::OwnedArray<juce::Component> pages_;
    void stepPreset(int dir);
    void quickSave();
    void flash(const juce::String& m);
    void showHint(int tab);   // brief name pill under a tapped tab (touch has no hover tooltip)
    juce::TooltipWindow tooltips_{ this, 500 };
    juce::Label hint_; juce::uint32 hintUntil_ = 0;
    juce::Label title_, status_, presetName_;
    struct NameClick : juce::MouseListener { std::function<void()> f; void mouseUp(const juce::MouseEvent& e) override { if (f && !e.mouseWasDraggedSinceMouseDown()) f(); } } nameClick_;   // click the preset name: open the Factory / User / Banks tree
    juce::TextButton prevB_, nextB_, quickSaveB_, saveAsB_, loadB_, initB_, undoB_, redoB_, genB_;
    int sel_ = 0; juce::uint32 flashUntil_ = 0;
};

} // namespace vv
