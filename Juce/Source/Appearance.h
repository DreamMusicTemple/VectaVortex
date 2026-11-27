// Settings > Appearance: colour scheme, potentiometer style, dropdown style and background image (web parity).
// Choices live in Store globals ("scheme", "potStyle", "ddStyle", "bgImage") so they persist with the plugin state.
#pragma once
#include "Widgets.h"
#include "Assets.h"

namespace vv::look {

struct Scheme { const char* name; juce::uint32 accent, dim; int bg; };   // bg: background index shown for "Scheme default" (6 = Brushed metal, the app default)
inline const Scheme kSchemes[] = { { "Gold", 0xffc99a52, 0xff5a4315, 6 }, { "Cyan", 0xff26d9e6, 0xff12747b, 6 }, { "Magenta", 0xffd94a9e, 0xff4d1a3c, 6 },
                                   { "Cream", 0xffd97f2a, 0xff8a4a12, 6 }, { "Future", 0xff5ad1e6, 0xff1a5a66, 1 }, { "Rustic", 0xffc96b4a, 0xff6b3520, 2 } };
inline const char* kBackgrounds[] = { "Scheme default (Brushed metal)", "Future (cyan wireframe)", "Rustic", "Ocean", "Nebula", "Vektur Vortex", "Brushed metal", "None" };
inline const char* kBgFiles[] = { "", "future", "rustic", "ocean", "nebula", "vortex", "brushed", "" };   // Assets/Backgrounds/<name>.jpg
inline const int kNumSchemes = 6, kNumBackgrounds = 8;

inline int effectiveBackground(Store& s) {   // "Scheme default" follows the scheme (Future / Rustic have their own picture)
    const int b = (int) s.global("bgImage", 0); if (b != 0) return b;
    const int sc = juce::jlimit(0, kNumSchemes - 1, (int) s.global("scheme", 1)); return kSchemes[sc].bg > 0 ? kSchemes[sc].bg : 6;
}
inline juce::Image loadBackground(int idx) {
    static juce::Image cache[8]; if (idx <= 0 || idx >= 7) return {};
    if (!cache[idx].isValid()) cache[idx] = juce::ImageFileFormat::loadFrom(assets::root().getChildFile("Backgrounds").getChildFile(juce::String(kBgFiles[idx]) + ".jpg"));
    return cache[idx];
}
// Apply the stored choices to the shared look (accent colour + widget styles). Returns true when anything changed.
inline bool apply(Store& s, VVLookAndFeel& laf) {
    const int sc = juce::jlimit(0, kNumSchemes - 1, (int) s.global("scheme", 1)), pot = (int) s.global("potStyle", 2), dd = 0;   // dropdown style option removed (web parity): charcoal rounded is fixed
    const bool changed = col::cyan != juce::Colour(kSchemes[sc].accent) || VVLookAndFeel::potStyle != pot || VVLookAndFeel::ddStyle != dd;
    col::cyan = juce::Colour(kSchemes[sc].accent); col::cyanDim = juce::Colour(kSchemes[sc].dim); VVLookAndFeel::potStyle = pot; VVLookAndFeel::ddStyle = dd; laf.refreshAccent();
    return changed;
}

struct AppearanceControls : juce::Component {
    explicit AppearanceControls(Store& s) : store(s) {
        auto mk = [this](juce::Label& l, juce::ComboBox& c, const char* t, juce::StringArray items, const char* key, int def, bool schemeToo) {
            l.setText(t, juce::dontSendNotification); l.setColour(juce::Label::textColourId, col::dim); addAndMakeVisible(l);
            for (int i = 0; i < items.size(); ++i) c.addItem(items[i], i + 1); c.setSelectedId((int) store.global(key, def) + 1, juce::dontSendNotification);
            c.onChange = [this, &c, key, schemeToo] { store.setGlobal(key, c.getSelectedId() - 1);
                if (schemeToo && c.getSelectedId() > 1) { store.setGlobal("scheme", 4); sync(); }   // picking a background image switches the scheme to Future, like the web
            }; addAndMakeVisible(c); };
        juce::StringArray sn; for (auto& x : kSchemes) sn.add(x.name); juce::StringArray bn; for (auto* x : kBackgrounds) bn.add(x);
        mk(l1, scheme, "Colour scheme", sn, "scheme", 1, false);
        mk(l2, pot, "Potentiometer style", { "Classic", "Simple", "Future", "Rustic" }, "potStyle", 2, false);
        mk(l4, bg, "Background image", bn, "bgImage", 0, true);
        schemeCombo = &scheme;
    }
    void sync() { scheme.setSelectedId((int) store.global("scheme", 1) + 1, juce::dontSendNotification); }
    void resized() override { auto r = getLocalBounds().reduced(4); for (auto [l, c] : { std::pair<juce::Label*, juce::ComboBox*>{ &l1, &scheme }, { &l2, &pot }, { &l4, &bg } }) { auto row = r.removeFromTop(32); l->setBounds(row.removeFromLeft(160)); c->setBounds(row.removeFromLeft(260)); r.removeFromTop(4); } }
    Store& store; juce::Label l1, l2, l4; juce::ComboBox scheme, pot, bg; juce::ComboBox* schemeCombo = nullptr;
};

} // namespace vv::look
