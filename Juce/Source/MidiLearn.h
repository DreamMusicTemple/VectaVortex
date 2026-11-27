// MIDI LEARN profiles (web: four profiles of CC -> parameter mappings) and the Settings panel for them.
// The engine holds the active profile's map (vv_engine_cc_learn_*); the four profiles live in <Documents>/Vektur Vortex/midi_profiles.json.
#pragma once
#include "Widgets.h"
#include "vv_engine.h"

namespace vv::midi {

inline juce::File file() { return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Vektur Vortex").getChildFile("midi_profiles.json"); }
struct Profiles {
    juce::StringArray names{ "Profile 1", "Profile 2", "Profile 3", "Profile 4" }, maps{ "{}", "{}", "{}", "{}" };
    void load() { auto v = juce::JSON::parse(file()); if (auto* a = v.getProperty("profiles", {}).getArray()) for (int i = 0; i < juce::jmin(4, a->size()); ++i) { names.set(i, (*a)[i].getProperty("name", names[i]).toString()); maps.set(i, (*a)[i].getProperty("map", "{}").toString()); } }
    void save() const { juce::Array<juce::var> a; for (int i = 0; i < 4; ++i) { auto* o = new juce::DynamicObject(); o->setProperty("name", names[i]); o->setProperty("map", maps[i]); a.add(juce::var(o)); }
        auto* root = new juce::DynamicObject(); root->setProperty("profiles", a); file().getParentDirectory().createDirectory(); file().replaceWithText(juce::JSON::toString(juce::var(root))); }
};
inline Profiles& profiles() { static Profiles p; static bool loaded = false; if (!loaded) { loaded = true; p.load(); } return p; }
inline void applyActive(Store& s) { if (!s.engine()) return; const int i = juce::jlimit(0, 3, (int) s.global("midiProfile", 0)); vv_engine_cc_learn_set_json(s.engine(), profiles().maps[i].toRawUTF8()); }
inline void saveActive(Store& s) {   // after a learn: store the engine's current map into the active profile
    if (!s.engine()) return; const int i = juce::jlimit(0, 3, (int) s.global("midiProfile", 0)); std::vector<char> b(65536); vv_engine_cc_learn_json(s.engine(), b.data(), b.size()); profiles().maps.set(i, juce::String(b.data())); profiles().save();
}

// Built-in CC map (identical to the web's table).
inline const char* kCcTable =
    "CC 1 Mod wheel | CC 7 Master volume | CC 74 / 71 Wavetable filter cutoff / resonance | CC 20 / 21 FM filter cutoff / resonance | CC 22 / 23 Sampler filter cutoff / resonance\n"
    "CC 24 / 25 / 26 filter keytrack (WT / FM / SMP) | CC 73 / 75 / 79 / 72 Wavetable attack / decay / sustain / release | CC 28 / 29 / 30 / 31 FM A / D / S / R\n"
    "CC 33 / 34 / 35 / 36 Sampler A / D / S / R | CC 102-105 Macros 1-4 | CC 106 / 107 / 108 / 112 / 113 / 114 LFO 1-6 rate | CC 109 / 110 / 111 Reverb / Delay / Saturation mix\n"
    "CC 115 / 116 / 117 Procedural filter cutoff / resonance / keytrack | CC 118 / 119 / 85 / 86 Procedural A / D / S / R | CC 87 Master tone | CC 88 WT / FM mix | Pitch bend, Program change, MPE";

struct MidiPanel : juce::Component {
    explicit MidiPanel(Store& s) : store(s) {
        sel.addItemList({ "Profile 1", "Profile 2", "Profile 3", "Profile 4" }, 1); sel.setSelectedId(1 + (int) store.global("midiProfile", 0), juce::dontSendNotification);
        sel.onChange = [this] { store.setGlobal("midiProfile", sel.getSelectedId() - 1); applyActive(store); name.setText(profiles().names[sel.getSelectedId() - 1], false); };
        name.setText(profiles().names[sel.getSelectedId() - 1], false); name.onTextChange = [this] { profiles().names.set(sel.getSelectedId() - 1, name.getText()); profiles().save(); };
        clear.setButtonText("CLEAR PROFILE MAPPINGS"); clear.onClick = [this] { if (store.engine()) vv_engine_cc_learn_clear(store.engine()); saveActive(store); status.setText("Profile mappings cleared.", juce::dontSendNotification); };
        info.setText(juce::String("MIDI LEARN: use the MIDI LEARN button on the PLAY tab, click any dial, then move a hardware control.\n\n") + kCcTable, juce::dontSendNotification);
        info.setFont(VVLookAndFeel::mono(11.f)); info.setColour(juce::Label::textColourId, col::dim); info.setJustificationType(juce::Justification::topLeft); status.setColour(juce::Label::textColourId, col::cyan);
        for (juce::Component* c : std::initializer_list<juce::Component*>{ &sel, &name, &clear, &info, &status }) addAndMakeVisible(*c);
    }
    void resized() override { auto r = getLocalBounds().reduced(4); auto row = r.removeFromTop(30); sel.setBounds(row.removeFromLeft(130)); row.removeFromLeft(8); name.setBounds(row.removeFromLeft(200)); row.removeFromLeft(8); clear.setBounds(row.removeFromLeft(220));
        status.setBounds(r.removeFromTop(22)); info.setBounds(r); }
    Store& store; juce::ComboBox sel; juce::TextEditor name; juce::TextButton clear; juce::Label info, status;
};

} // namespace vv::midi
