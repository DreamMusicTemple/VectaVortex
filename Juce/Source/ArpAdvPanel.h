// ADVANCED arpeggiator: 35 seeded generative algorithms (web ARP_ALGOS, engine Core/include/vv/arp_adv.hpp), shown only when arp.mode == "advanced".
// Algorithm menu with description, seed + NEW SEED, the scale (Scale Walk) and just the parameters the chosen algorithm uses. Untested (no JUCE build here).
#pragma once
#include "Widgets.h"
#include "ArpAdvInfo.h"

namespace vv {

class ArpAdvPanel : public juce::Component, private Store::Listener {
public:
    explicit ArpAdvPanel(Store& s) : store(s) {
        int id = 1; for (auto& a : arpadv::algos()) algo.addItem(juce::String(id) + ". " + a.name + "  -  " + a.group, id), ++id;
        algo.onChange = [this] { if (!loading) store.set("arp.adv.algo", arpadv::algos()[(size_t) juce::jmax(0, algo.getSelectedId() - 1)].id); };
        desc.setFont(VVLookAndFeel::mono(11.f)); desc.setColour(juce::Label::textColourId, col::dim); desc.setJustificationType(juce::Justification::topLeft);
        seedL.setFont(VVLookAndFeel::mono(12.f)); newSeed.setButtonText("NEW SEED");
        newSeed.onClick = [this] { store.set("arp.adv.seed", (double) juce::Random::getSystemRandom().nextInt(1000000)); };   // same seed = same pattern
        auto edit = [this](std::function<void(juce::DynamicObject&)> f) { auto cur = store.get("arp.adv"); auto* o = cur.getDynamicObject() ? cur.getDynamicObject()->clone().release() : new juce::DynamicObject(); f(*o); store.set("arp.adv", juce::var(o)); };
        auto rnd = [](double a, double b) { return a + juce::Random::getSystemRandom().nextDouble() * (b - a); };
        auto reseed = [](juce::DynamicObject& o) { o.setProperty("seed", (double) juce::Random::getSystemRandom().nextInt(1000000)); o.setProperty("mutSalt", 0.0); o.setProperty("mutAmt", 0.0); o.setProperty("varSeed", 0.0); };
        const char* names[] = { "GENERATE", "RANDOMISE", "MUTATE", "REGEN VARIATION", "RESET" };
        for (int i = 0; i < 5; ++i) { patBtn[i].setButtonText(names[i]); addAndMakeVisible(patBtn[i]); }
        patBtn[0].onClick = [edit, reseed] { edit([&](juce::DynamicObject& o) { reseed(o); }); };
        patBtn[1].onClick = [edit, reseed, rnd] { edit([&](juce::DynamicObject& o) { reseed(o); o.setProperty("complexity", rnd(0.1, 1)); o.setProperty("density", rnd(0.3, 0.9)); o.setProperty("mutation", rnd(0, 0.5)); o.setProperty("variation", rnd(0, 0.4)); o.setProperty("randomness", rnd(0, 0.2)); o.setProperty("octSpread", rnd(0, 0.4)); o.setProperty("ratchet", rnd(0, 0.3)); o.setProperty("accent", rnd(0, 0.7)); o.setProperty("velVar", rnd(0, 0.3)); o.setProperty("gateVar", rnd(0, 0.3)); const int L[] = { 8, 12, 16, 24, 32 }; o.setProperty("patLen", (double) L[juce::Random::getSystemRandom().nextInt(5)]); }); };
        patBtn[2].onClick = [edit, this] { edit([&](juce::DynamicObject& o) { o.setProperty("mutSalt", (double) o.getProperty("mutSalt") + 1.0); o.setProperty("mutAmt", juce::jmax(0.15, store.num("arp.adv.mutation", 0.2))); }); };   // re-rolls some steps
        patBtn[3].onClick = [edit] { edit([&](juce::DynamicObject& o) { o.setProperty("varSeed", (double) o.getProperty("varSeed") + 1.0); }); };   // re-rolls only the variation layer
        patBtn[4].onClick = [this] { auto* o = new juce::DynamicObject(); o->setProperty("algo", store.str("arp.adv.algo", "fractal")); store.set("arp.adv", juce::var(o)); };
        for (auto& p : arpadv::params()) {
            auto r = std::make_unique<Row>(); r->p = &p; r->name.setText(p.label, juce::dontSendNotification); r->name.setFont(VVLookAndFeel::mono(11.f));
            r->s.setSliderStyle(juce::Slider::LinearBar); r->s.setRange(p.lo, p.hi, p.step); r->s.setDoubleClickReturnValue(true, p.def);
            if (p.percent) r->s.textFromValueFunction = [](double v) { return juce::String((int) std::round(v * 100)) + " %"; };
            r->s.onValueChange = [this, rp = r.get()] { if (!loading) store.set("arp.adv." + juce::String(rp->p->key), rp->s.getValue()); };
            addChildComponent(r->name); addChildComponent(r->s); rows.push_back(std::move(r));
        }
        // ---- Round 26 (stage 3): A/B pattern slots, morph, pattern library (same preset keys as the web app: arp.ab {a,b,cur}, arp.morph {on,amt}, arp.lib [{name, adv}])
        abA.setButtonText("A"); abB.setButtonText("B"); copyAb.setButtonText("COPY > OTHER"); saveB.setButtonText("SAVE"); loadB.setButtonText("LOAD"); delB.setButtonText("DELETE");
        for (auto* b : { &abA, &abB }) { b->setClickingTogglesState(false); b->setRadioGroupId(77); }
        abA.onClick = [this] { switchTo("a"); }; abB.onClick = [this] { switchTo("b"); }; copyAb.onClick = [this] { copyToOther(); };
        morphOn.setButtonText("Morph A <> B"); morphOn.onClick = [this] { if (!loading) setMorph(morphOn.getToggleState(), morphAmt.getValue() / 100.0); };
        morphAmt.setSliderStyle(juce::Slider::LinearBar); morphAmt.setRange(0, 100, 1); morphAmt.setTextValueSuffix(" %"); morphAmt.onValueChange = [this] { if (!loading) setMorph(morphOn.getToggleState(), morphAmt.getValue() / 100.0); };
        abL.setText("A/B", juce::dontSendNotification); abL.setFont(VVLookAndFeel::mono(11.f));
        saveB.onClick = [this] { saveToLib(); }; loadB.onClick = [this] { loadFromLib(); }; delB.onClick = [this] { deleteFromLib(); };
        for (juce::Component* c : std::initializer_list<juce::Component*>{ &abA, &abB, &copyAb, &morphOn, &morphAmt, &abL, &libBox, &saveB, &loadB, &delB }) addAndMakeVisible(*c);
        scale.addItemList(arpadv::scales(), 1); scale.onChange = [this] { if (!loading) store.set("arp.adv.scale", scale.getText()); }; scaleL.setText("Scale", juce::dontSendNotification); scaleL.setFont(VVLookAndFeel::mono(11.f));
        for (juce::Component* c : std::initializer_list<juce::Component*>{ &algo, &desc, &seedL, &newSeed, &scale, &scaleL, &standardNote }) addAndMakeVisible(*c);
        standardNote.setText("Standard mode: set Mode to advanced to use the generative algorithms.", juce::dontSendNotification); standardNote.setFont(VVLookAndFeel::mono(11.f)); standardNote.setColour(juce::Label::textColourId, col::dim);
        store.addListener(this); refresh();
    }
    ~ArpAdvPanel() override { store.removeListener(this); }
    void resized() override {
        auto r = getLocalBounds().reduced(2); standardNote.setBounds(r.removeFromTop(22));
        algo.setBounds(r.removeFromTop(28)); r.removeFromTop(2); desc.setBounds(r.removeFromTop(34)); { auto pb = r.removeFromTop(28); const int w = pb.getWidth() / 5; for (int i = 0; i < 5; ++i) patBtn[i].setBounds(pb.removeFromLeft(w).reduced(2, 0)); }
        { auto ab = r.removeFromTop(28); abL.setBounds(ab.removeFromLeft(34)); abA.setBounds(ab.removeFromLeft(40).reduced(2)); abB.setBounds(ab.removeFromLeft(40).reduced(2)); copyAb.setBounds(ab.removeFromLeft(120).reduced(2));
          morphOn.setBounds(ab.removeFromLeft(130)); morphAmt.setBounds(ab.removeFromLeft(140).reduced(2)); }
        { auto lb = r.removeFromTop(28); libBox.setBounds(lb.removeFromLeft(220).reduced(2)); for (auto* b : { &saveB, &loadB, &delB }) b->setBounds(lb.removeFromLeft(80).reduced(2)); }
        auto sd = r.removeFromTop(28); seedL.setBounds(sd.removeFromLeft(150)); newSeed.setBounds(sd.removeFromLeft(110));
        scaleL.setBounds(sd.removeFromLeft(50)); scale.setBounds(sd.removeFromLeft(130)); r.removeFromTop(4);
        const int colW = juce::jmax(160, r.getWidth() / 2); int n = 0;
        for (auto& row : rows) if (row->s.isVisible()) { const int c = n % 2, y = (n / 2) * 26; auto b = juce::Rectangle<int>(r.getX() + c * colW, r.getY() + y, colW - 8, 24); row->name.setBounds(b.removeFromLeft(juce::jmin(120, b.getWidth() / 2))); row->s.setBounds(b); ++n; }
    }
    static int heightFor(int visibleParams) { return 2 + 22 + 30 + 34 + 28 + 28 + 28 + 28 + 4 + ((visibleParams + 1) / 2) * 26 + 6; }
private:
    struct Row { const arpadv::Param* p = nullptr; juce::Label name; juce::Slider s; };
    static juce::var clone(const juce::var& v) { return juce::JSON::parse(juce::JSON::toString(v, true)); }
    juce::DynamicObject* abObj(juce::var& holder) { auto cur = store.get("arp.ab"); holder = cur.getDynamicObject() ? clone(cur) : juce::var(new juce::DynamicObject()); return holder.getDynamicObject(); }
    void switchTo(const juce::String& X) {
        juce::var h; auto* ab = abObj(h); const juce::String cur = ab->hasProperty("cur") ? ab->getProperty("cur").toString() : "a"; if (cur == X) return;
        ab->setProperty(cur, clone(store.get("arp.adv"))); const auto nxt = ab->getProperty(X).getDynamicObject() ? clone(ab->getProperty(X)) : clone(store.get("arp.adv")); ab->setProperty("cur", X);
        store.setMany({ { "arp.ab", h }, { "arp.adv", nxt } });
    }
    void copyToOther() { juce::var h; auto* ab = abObj(h); const juce::String cur = ab->hasProperty("cur") ? ab->getProperty("cur").toString() : "a"; ab->setProperty("cur", cur); ab->setProperty(cur == "b" ? "a" : "b", clone(store.get("arp.adv"))); store.set("arp.ab", h); }
    void setMorph(bool on, double amt) { auto* o = new juce::DynamicObject(); o->setProperty("on", on); o->setProperty("amt", amt); store.set("arp.morph", juce::var(o)); }
    juce::Array<juce::var> libItems() const { juce::Array<juce::var> a; const auto lib = store.get("arp.lib"); if (auto* arr = lib.getArray()) for (auto& v : *arr) a.add(v); return a; }
    void saveToLib() {
        auto items = libItems(); const auto id = store.str("arp.adv.algo", "fractal"); juce::String nm = id;
        for (auto& a : arpadv::algos()) if (id == a.id) nm = a.name; auto* o = new juce::DynamicObject(); o->setProperty("name", nm + " #" + juce::String((int) store.num("arp.adv.seed", 1234))); o->setProperty("adv", clone(store.get("arp.adv")));
        items.add(juce::var(o)); store.set("arp.lib", juce::var(items)); selectLib = items.size() - 1;
    }
    void loadFromLib() { const auto items = libItems(); const int n = libBox.getSelectedId() - 1; if (n < 0 || n >= items.size()) return; store.set("arp.adv", clone(items[n].getProperty("adv", juce::var()))); }
    void deleteFromLib() { auto items = libItems(); const int n = libBox.getSelectedId() - 1; if (n < 0 || n >= items.size()) return; items.remove(n); selectLib = juce::jmax(0, n - 1); store.set("arp.lib", juce::var(items)); }
    void storeChanged() override { refresh(); }
    void refresh() {
        loading = true; const bool adv = store.str("arp.mode", "standard") == "advanced"; const auto id = store.str("arp.adv.algo", "fractal");
        int idx = 0; for (size_t i = 0; i < arpadv::algos().size(); ++i) if (id == arpadv::algos()[i].id) idx = (int) i; const auto& a = arpadv::algos()[(size_t) idx];
        algo.setSelectedId(idx + 1, juce::dontSendNotification); desc.setText(a.desc, juce::dontSendNotification); seedL.setText("Seed: " + juce::String((int) store.num("arp.adv.seed", 1234)), juce::dontSendNotification);
        scale.setText(store.str("arp.adv.scale", "major"), juce::dontSendNotification);
        juce::StringArray keys = a.keys; keys.addArray(arpadv::coreKeys()); const bool showScale = a.keys.contains("scale"); scale.setVisible(adv && showScale); scaleL.setVisible(adv && showScale);
        for (auto& r : rows) { const bool show = adv && keys.contains(r->p->key); r->s.setVisible(show); r->name.setVisible(show); r->s.setValue(store.num("arp.adv." + juce::String(r->p->key), r->p->def), juce::dontSendNotification); }
        for (juce::Component* c : std::initializer_list<juce::Component*>{ &algo, &desc, &seedL, &newSeed, &patBtn[0], &patBtn[1], &patBtn[2], &patBtn[3], &patBtn[4] }) c->setVisible(adv); standardNote.setVisible(!adv);
        { const juce::String cur = store.str("arp.ab.cur", "a"); abA.setToggleState(cur != "b", juce::dontSendNotification); abB.setToggleState(cur == "b", juce::dontSendNotification);
          morphOn.setToggleState(store.flag("arp.morph.on", false), juce::dontSendNotification); morphAmt.setValue(store.num("arp.morph.amt", 0.5) * 100.0, juce::dontSendNotification);
          const auto items = libItems(); libBox.clear(juce::dontSendNotification); int k = 1; for (auto& it : items) libBox.addItem(it.getProperty("name", "pattern").toString(), k++);
          if (items.isEmpty()) libBox.setTextWhenNothingSelected("(no saved patterns)"); else libBox.setSelectedId(juce::jlimit(0, items.size() - 1, selectLib) + 1, juce::dontSendNotification); }
        for (juce::Component* c : std::initializer_list<juce::Component*>{ &abA, &abB, &copyAb, &morphOn, &morphAmt, &abL, &libBox, &saveB, &loadB, &delB }) c->setVisible(adv);
        loading = false; resized();
    }
    Store& store; bool loading = false; juce::ComboBox algo, scale; juce::Label desc, seedL, scaleL, standardNote; juce::TextButton newSeed, patBtn[5], abA, abB, copyAb, saveB, loadB, delB; juce::ToggleButton morphOn; juce::Slider morphAmt; juce::Label abL; juce::ComboBox libBox; int selectLib = 0; std::vector<std::unique_ptr<Row>> rows;
};

}   // namespace vv
