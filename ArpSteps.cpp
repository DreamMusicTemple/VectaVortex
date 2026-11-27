#include "ArpSteps.h"
namespace vv {
static juce::var stepVar() { auto* o = new juce::DynamicObject(); o->setProperty("wave", "triangle"); o->setProperty("mode", "relative"); o->setProperty("value", 0); o->setProperty("gate", true);
    o->setProperty("ringMod", false); o->setProperty("sync", false); o->setProperty("lengthTicks", 1); o->setProperty("tempoSync", false); o->setProperty("syncDivision", 0.25); return juce::var(o); }

ArpSteps::Row::Row(ArpSteps& o, int i) : owner(o), idx(i) {
    no.setText(juce::String(i + 1), juce::dontSendNotification); addAndMakeVisible(no);
    int id = 1; for (auto* w : { "triangle", "sawtooth", "pulse", "noise", "tri_saw", "tri_pulse", "saw_pulse" }) wave.addItem(w, id++);
    mode.addItem("relative", 1); mode.addItem("fixed", 2);
    for (auto* c : { &wave, &mode }) addAndMakeVisible(*c);
    value.setSliderStyle(juce::Slider::LinearBar); value.setRange(-48, 127, 1); ticks.setSliderStyle(juce::Slider::LinearBar); ticks.setRange(1, 32, 1); ticks.setTextValueSuffix(" ticks");
    gate.setButtonText("Gate"); ring.setButtonText("Ring"); sync.setButtonText("Sync");
    for (juce::Component* c : std::initializer_list<juce::Component*>{ &value, &ticks, &gate, &ring, &sync }) addAndMakeVisible(*c);
    wave.onChange = [this] { if (!loading) owner.store.set(p("wave"), wave.getText()); };
    mode.onChange = [this] { if (!loading) owner.store.set(p("mode"), mode.getText()); };
    value.onValueChange = [this] { if (!loading) owner.store.set(p("value"), value.getValue()); };
    ticks.onValueChange = [this] { if (!loading) owner.store.set(p("lengthTicks"), ticks.getValue()); };
    gate.onClick = [this] { owner.store.set(p("gate"), gate.getToggleState() ? 1.0 : 0.0); }; ring.onClick = [this] { owner.store.set(p("ringMod"), ring.getToggleState() ? 1.0 : 0.0); }; sync.onClick = [this] { owner.store.set(p("sync"), sync.getToggleState() ? 1.0 : 0.0); };
    load();
}
void ArpSteps::Row::load() {
    loading = true; Store& s = owner.store;
    wave.setText(s.str(p("wave")).isEmpty() ? "triangle" : s.str(p("wave")), juce::dontSendNotification); mode.setText(s.str(p("mode")).isEmpty() ? "relative" : s.str(p("mode")), juce::dontSendNotification);
    value.setValue(s.num(p("value"), 0), juce::dontSendNotification); ticks.setValue(s.num(p("lengthTicks"), 1), juce::dontSendNotification);
    gate.setToggleState(s.num(p("gate"), 1) != 0, juce::dontSendNotification); ring.setToggleState(s.num(p("ringMod"), 0) != 0, juce::dontSendNotification); sync.setToggleState(s.num(p("sync"), 0) != 0, juce::dontSendNotification);
    loading = false;
}
void ArpSteps::Row::resized() { auto r = getLocalBounds().reduced(0, 2); no.setBounds(r.removeFromLeft(24)); wave.setBounds(r.removeFromLeft(110)); r.removeFromLeft(4); mode.setBounds(r.removeFromLeft(90)); r.removeFromLeft(4);
    value.setBounds(r.removeFromLeft(90)); r.removeFromLeft(4); ticks.setBounds(r.removeFromLeft(90)); r.removeFromLeft(4); gate.setBounds(r.removeFromLeft(60)); ring.setBounds(r.removeFromLeft(60)); sync.setBounds(r.removeFromLeft(60)); }

ArpSteps::ArpSteps(Store& s) : Control(s, "layers.SID.wtArp") {
    on.setButtonText("Arp on"); hard.setButtonText("Hard restart"); speedL.setText("Speed", juce::dontSendNotification); speed.setSliderStyle(juce::Slider::LinearBar); speed.setRange(25, 240, 1); speed.setTextValueSuffix(" Hz");
    addB.setButtonText("+ STEP"); delB.setButtonText("- STEP");
    on.onClick = [this] { ensureArp(); store.set("layers.SID.wtArp.on", on.getToggleState() ? 1.0 : 0.0); };
    hard.onClick = [this] { ensureArp(); store.set("layers.SID.wtArp.hardRestart", hard.getToggleState() ? 1.0 : 0.0); };
    speed.onValueChange = [this] { if (!building) { ensureArp(); store.set("layers.SID.wtArp.speedHz", speed.getValue()); } };
    addB.onClick = [this] { ensureArp(); store.append("layers.SID.wtArp.steps", stepVar()); };
    delB.onClick = [this] { const int n = store.count("layers.SID.wtArp.steps"); if (n > 1) store.remove("layers.SID.wtArp.steps", n - 1); };
    for (juce::Component* c : std::initializer_list<juce::Component*>{ &on, &hard, &speed, &speedL, &addB, &delB }) addAndMakeVisible(*c);
    rebuild();
}
void ArpSteps::ensureArp() {   // presets saved before the arp existed carry no wtArp object: create the web default (3 steps 0 / +3 / +7)
    if (store.count("layers.SID.wtArp.steps") > 0) return;
    for (int i : { 0, 3, 7 }) { auto v = stepVar(); v.getDynamicObject()->setProperty("value", i); store.append("layers.SID.wtArp.steps", v); }
}
void ArpSteps::rebuild() {
    building = true; const int n = store.count("layers.SID.wtArp.steps");
    if (n != rows.size()) { rows.clear(); for (int i = 0; i < n; ++i) { auto* r = rows.add(new Row(*this, i)); addAndMakeVisible(r); } if (auto* par = getParentComponent()) par->resized(); setSize(getWidth(), preferredSize().y); resized(); }
    for (auto* r : rows) r->load();
    on.setToggleState(store.num("layers.SID.wtArp.on", 0) != 0, juce::dontSendNotification); hard.setToggleState(store.num("layers.SID.wtArp.hardRestart", 0) != 0, juce::dontSendNotification);
    if (!speed.isMouseButtonDown()) speed.setValue(store.num("layers.SID.wtArp.speedHz", 50), juce::dontSendNotification);
    building = false;
}
void ArpSteps::refresh() { rebuild(); }
void ArpSteps::resized() {
    auto r = getLocalBounds().reduced(4); auto top = r.removeFromTop(30); on.setBounds(top.removeFromLeft(90)); hard.setBounds(top.removeFromLeft(120)); speedL.setBounds(top.removeFromLeft(50)); speed.setBounds(top.removeFromLeft(110).reduced(0, 2));
    top.removeFromLeft(10); addB.setBounds(top.removeFromLeft(80).reduced(0, 2)); delB.setBounds(top.removeFromLeft(80).reduced(2, 2)); r.removeFromTop(6);
    for (auto* row : rows) row->setBounds(r.removeFromTop(30));
}
}
