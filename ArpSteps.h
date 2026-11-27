// SID "wavetable" arpeggiator editor (web renderWtArpPanel): On / speed / hard restart, a variable-length step list
// (wave, relative|fixed note, gate, ring, sync, length in ticks), stored at layers.SID.wtArp.*. Voices follow it via their own "Follow arp" switch.
#pragma once
#include "Widgets.h"
namespace vv {
class ArpSteps : public Control {
public:
    explicit ArpSteps(Store& s);
    void refresh() override;
    juce::Point<int> preferredSize() const override { return { 640, 70 + 30 * juce::jmax(3, rows.size()) }; }
    void resized() override;
private:
    struct Row : juce::Component {
        Row(ArpSteps& o, int i); void load(); void resized() override;
        ArpSteps& owner; int idx; bool loading = false;
        juce::Label no; juce::ComboBox wave, mode; juce::Slider value, ticks; juce::ToggleButton gate, ring, sync;
        juce::String p(const char* f) const { return "layers.SID.wtArp.steps." + juce::String(idx) + "." + f; }
    };
    void rebuild(); void ensureArp();
    juce::ToggleButton on, hard; juce::Slider speed; juce::Label speedL; juce::TextButton addB, delB;
    juce::OwnedArray<Row> rows; bool building = false;
};
}
