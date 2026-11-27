// Shared look + the small set of path-bound controls every screen is built from (knob, toggle, choice, stepper) and the
// Panel / Page containers that flow them into a scrollable layout. Message-thread only.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Store.h"

namespace vv {

namespace col {
// Vektur Vocal tokens (same values as the web app's vk-skin): well #0A0B0F, panel #12141B, control #1B1F2A, border #232733, track #2A2F3D, silk #B3BAC9
const juce::Colour bg(0xff0a0b0f), panel(0xff12141b), panelTop(0xff181b23), panel2(0xff1b1f2a), line(0xff232733), track(0xff2a2f3d), silk(0xffb3baC9), edge(0xff000000), ink(0xffe6edf3), dim(0xff8a97a3),
                   amber(0xffefb238), red(0xffe2704a), violet(0xff7f77dd), green(0xff5dcaa5);
// The accent colour follows the chosen colour scheme (Settings > Appearance), so these two are mutable.
inline juce::Colour cyan(0xff26d9e6), cyanDim(0xff12747b);
}

class VVLookAndFeel : public juce::LookAndFeel_V4 {
public:
    VVLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;   // tab-bar buttons draw their icon (property "navIcon") instead of text
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void refreshAccent();                       // re-applies the accent-based colours after a colour-scheme change
    static inline int potStyle = 2, ddStyle = 0; // 0 classic, 1 simple, 2 future, 3 rustic / 0 rounded, 1 rectangle, 2 cyan rectangle
    juce::Font getTextButtonFont(juce::TextButton& b, int) override { return mono(b.getProperties().contains("tabBar") ? 10.5f : 13.f); }
    juce::Font getLabelFont(juce::Label&) override { return mono(12.f); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return mono(12.f); }
    juce::Font getPopupMenuFont() override { return mono(13.f); }
    static juce::Font mono(float h) { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain)); }
};

// Base of every bound control: listens to the store and re-reads its value when the preset changes underneath it.
class Control : public juce::Component, protected Store::Listener {
public:
    Control(Store& s, juce::String path) : store(s), path_(std::move(path)) { store.addListener(this); }
    ~Control() override { store.removeListener(this); }
    void storeChanged() override { refresh(); }
    virtual void refresh() = 0;
    virtual juce::Point<int> preferredSize() const = 0;
protected:
    Store& store;
    juce::String path_;
    bool updating_ = false;
};

struct KnobOpts { juce::String fmt = "%.2f", unit; bool log = false; int decimals = 2; float scale = 1.f; };

class Knob : public Control {
public:
    Knob(Store&, const juce::String& label, const juce::String& path, double lo, double hi, double def, KnobOpts o = {});
    void refresh() override;
    void resized() override;
    juce::Point<int> preferredSize() const override { return { juce::roundToInt(88 * o_.scale), juce::roundToInt(104 * o_.scale) }; }
    juce::Slider slider;
    const juce::String& path() const { return path_; }
    // LONG-PRESS MODULATION (web: hold a dial to pick a modulation source). dest is the matrix destination key, e.g. "WT:osc1PwmRate".
    // A held press (550 ms, without dragging) opens the source menu; a dot in the corner shows the dial has routes.
    Knob& setModDest(const juce::String& dest) { modDest_ = dest; refresh(); return *this; }
    const juce::String& modDest() const { return modDest_; }
    void openModMenu();
    bool hasModRoutes() const { return modOn_; }
    void paintOverChildren(juce::Graphics&) override;
    // MIDI LEARN: while learnMode is on a click on any dial reports it through onLearnPick instead of turning it.
    static inline bool learnMode = false;
    static inline std::function<void(Knob&)> onLearnPick;
private:
    struct Shield : juce::Component {
        explicit Shield(Knob& k) : owner(k) { setInterceptsMouseClicks(true, false); }
        bool hitTest(int, int) override { return Knob::learnMode; }
        void paint(juce::Graphics& g) override { if (Knob::learnMode) { g.setColour(col::cyan.withAlpha(0.18f)); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.f), 6.f); g.setColour(col::cyan); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.f), 6.f, 1.5f); } }
        void mouseDown(const juce::MouseEvent&) override { if (Knob::onLearnPick) Knob::onLearnPick(owner); }
        Knob& owner;
    } shield_{ *this };
    struct LongPress : juce::MouseListener, juce::Timer {
        explicit LongPress(Knob& k) : owner(k) {}
        void mouseDown(const juce::MouseEvent& e) override { if (owner.modDest_.isNotEmpty() && !Knob::learnMode && !e.mods.isPopupMenu()) { moved = false; startTimer(550); } }
        void mouseDrag(const juce::MouseEvent& e) override { if (e.getDistanceFromDragStart() > 4) { moved = true; stopTimer(); } }
        void mouseUp(const juce::MouseEvent&) override { stopTimer(); }
        void timerCallback() override { stopTimer(); if (!moved) owner.openModMenu(); }
        Knob& owner; bool moved = false;
    } longPress_{ *this };
    juce::String modDest_; bool modOn_ = false;
    juce::Label name_; juce::String label_; bool twoLine_ = false;
    double def_; KnobOpts o_;
public:
    // Two-word dial labels stack (second word under the first) when they do not fit the dial's width on one line; Panel::layout bottom-aligns the dials of such a row.
    bool labelNeedsTwoLines() const;
    void setTwoLine(bool b);
};

// Vertical fader (amp-envelope A / D / S / R, XFX editor). Same Store binding as Knob; double-click returns to the default.
class VFader : public Control {
public:
    VFader(Store&, const juce::String& label, const juce::String& path, double lo, double hi, double def, KnobOpts o = {});
    void refresh() override;
    void resized() override;
    juce::Point<int> preferredSize() const override { return { 58, 150 }; }
    juce::Slider slider;
private:
    juce::Label name_; double def_; KnobOpts o_;
};

class Toggle : public Control {
public:
    Toggle(Store&, const juce::String& label, const juce::String& path, bool def = false);
    void refresh() override;
    void resized() override { button.setBounds(getLocalBounds().reduced(2)); }
    juce::Point<int> preferredSize() const override { return { juce::jmax(132, 64 + juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(12.0f), button.getButtonText())), 34 }; }
    juce::ToggleButton button;
    std::function<void(bool)> onChanged;   // extra action after the preset value is written
private:
    bool def_;
};

class Choice : public Control {
public:
    // string-valued path
    Choice(Store&, const juce::String& label, const juce::String& path, const juce::StringArray& options, const juce::String& def = {});
    // number-valued path with named choices
    Choice(Store&, const juce::String& label, const juce::String& path, std::vector<std::pair<juce::String, double>> numeric, double def);
    void refresh() override;
    void resized() override;
    juce::Point<int> preferredSize() const override { return { 168, 54 }; }
    void setLabels(const juce::StringArray& displayLabels);   // display text per option (stored value stays the option id)
    juce::ComboBox box;
    std::function<void(const juce::String& previous, const juce::String& now)> onChosen;   // after the user picks an option (string-valued paths)
private:
    void init(const juce::String& label);
    juce::Label name_;
    juce::StringArray options_; juce::String defStr_;
    std::vector<std::pair<juce::String, double>> numeric_; double defNum_ = 0; bool isNumeric_ = false;
};

class Stepper : public Control {
public:
    Stepper(Store&, const juce::String& label, const juce::String& path, int lo, int hi, int def);
    void refresh() override;
    void resized() override;
    juce::Point<int> preferredSize() const override { return { 150, 54 }; }
private:
    juce::Label name_, value_; juce::TextButton minus_{"-"}, plus_{"+"};
    int lo_, hi_, def_;
};

// A titled group of controls that wraps like text.
class Panel : public juce::Component {
public:
    Panel(Store& s, juce::String title) : store(s), title_(std::move(title)) {}
    Knob& knob(const juce::String& label, const juce::String& path, double lo, double hi, double def, KnobOpts o = {});
    Toggle& toggle(const juce::String& label, const juce::String& path, bool def = false);
    VFader& vfader(const juce::String& label, const juce::String& path, double lo, double hi, double def, KnobOpts o = {});
    Choice& choice(const juce::String& label, const juce::String& path, const juce::StringArray& options, const juce::String& def = {});
    Choice& choiceNum(const juce::String& label, const juce::String& path, std::vector<std::pair<juce::String, double>> c, double def);
    Stepper& stepper(const juce::String& label, const juce::String& path, int lo, int hi, int def);
    // any custom component with an explicit size; w = -1 means "full row"
    void custom(juce::Component* c, int w, int h);
    void customOwned(juce::Component* c, int w, int h) { owned_.add(c); custom(c, w, h); }   // the panel deletes it
    void newRow() { rowBreaks_.add(items_.size()); }
    Panel& setKnobScale(float f) { knobScale_ = f; return *this; }   // dials added after this call are drawn at this fraction of the standard size
    int heightForWidth(int w) const;
    void paint(juce::Graphics&) override;
    void resized() override;
    Store& store;
    static constexpr int kHeader = 26, kPad = 8;
private:
    struct Item { juce::Component* c; int w, h; };
    template <class T, class... A> T& make(A&&... a);
    juce::OwnedArray<juce::Component> owned_;
    std::vector<Item> items_;
    juce::Array<int> rowBreaks_;
    juce::String title_;
    float knobScale_ = 1.f;
    int layout(int w, bool apply) const;
};

// A vertical stack of panels inside a viewport.
class Page : public juce::Component {
public:
    explicit Page(Store& s) : store(s) { view.setViewedComponent(&content, false); view.setScrollBarsShown(true, false); addAndMakeVisible(view); }
    Panel& panel(const juce::String& title) { auto* p = panels_.add(new Panel(store, title)); content.addAndMakeVisible(p); return *p; }
    void clear() { content.removeAllChildren(); panels_.clear(); }
    void resized() override { view.setBounds(getLocalBounds()); layoutContent(); }
    void layoutContent();
    Store& store;
    juce::Viewport view;
private:
    struct Content : juce::Component {} content;
    juce::OwnedArray<Panel> panels_;
};

// ADSR editor / filter editor / oscillator common / EQ builders shared by Sound, Seq and FX screens.
std::vector<std::pair<juce::String, double>> kEnvRepeatChoices();   // Off, 1..8 repeats, Continuous (-1)
void addEnv(Panel&, const juce::String& prefix, bool fine = true);
void addFilter(Panel&, const juce::String& prefix, const juce::String& envPrefix, double defCutoff = 4000);
void addOscCommon(Panel&, const juce::String& prefix, bool drift = true);
extern const juce::StringArray kFilterTypes, kFormantTables, kFormantVowels;

} // namespace vv
