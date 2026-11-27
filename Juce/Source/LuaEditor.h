// LUA oscillator layer editor (JUCE port of LuaEditorView.swift / web buildLuaOscUI): four script slots; each slot's source, seed and parameter values live in the preset at
// layers.LUA.oscs.N.lua.* and reach the engine through Store::set, like every other control. RUN compiles on a copy and installs only on success (the previous script keeps
// playing after an error); VALIDATE only checks; LIBRARY loads a factory script; the script's own parameters are built from the compiled script's info.
// NOT COMPILED here (no JUCE build available).
#pragma once
#include "Widgets.h"
#include "vv_engine.h"

namespace vv {

class LuaEditor : public juce::Component, private juce::Timer {
public:
    explicit LuaEditor(Store& s) : store(s) {
        for (int i = 0; i < 4; ++i) slotBox.addItem("LUA " + juce::String(i + 1), i + 1); slotBox.setSelectedId(1, juce::dontSendNotification);
        slotBox.onChange = [this] { slot = slotBox.getSelectedId() - 1; reload(); }; addAndMakeVisible(slotBox);
        enabledT.setButtonText("Enabled"); enabledT.onClick = [this] { store.set(base() + ".enabled", enabledT.getToggleState()); }; addAndMakeVisible(enabledT);
        auto mk = [this](juce::TextButton& b, const char* t, std::function<void()> f) { b.setButtonText(t); b.onClick = std::move(f); addAndMakeVisible(b); };
        mk(runB, "RUN", [this] { apply(code.getText()); }); mk(valB, "VALIDATE", [this] { validateOnly(); }); mk(libB, "LIBRARY", [this] { showLibrary(); });
        code.setMultiLine(true, false); code.setReturnKeyStartsNewLine(true); code.setTabKeyUsedAsCharacter(true); code.setFont(VVLookAndFeel::mono(12.f)); code.setScrollbarsShown(true); addAndMakeVisible(code);
        for (auto* l : { &report, &statsL, &header, &desc }) { l->setFont(VVLookAndFeel::mono(11.f)); l->setJustificationType(juce::Justification::topLeft); addAndMakeVisible(*l); }
        statsL.setColour(juce::Label::textColourId, col::dim); header.setFont(VVLookAndFeel::mono(12.f).boldened()); desc.setColour(juce::Label::textColourId, col::dim);
        reload(); startTimer(500);
    }
    void visibilityChanged() override { if (isVisible()) reload(); }
    void resized() override {
        auto r = getLocalBounds().reduced(4); auto top = r.removeFromTop(28);
        slotBox.setBounds(top.removeFromLeft(100).reduced(2)); enabledT.setBounds(top.removeFromLeft(100)); for (auto* b : { &runB, &valB, &libB }) b->setBounds(top.removeFromLeft(100).reduced(2));
        code.setBounds(r.removeFromTop(240)); report.setBounds(r.removeFromTop(50)); statsL.setBounds(r.removeFromTop(34)); header.setBounds(r.removeFromTop(20)); desc.setBounds(r.removeFromTop(34));
        int y = r.getY(); for (auto& pr : params) { pr->layout(r.getX(), y, r.getWidth()); y += 26; }
    }
private:
    struct ParamRow { juce::Label name; juce::Slider s; juce::ComboBox opts; juce::ToggleButton tg; juce::String id; bool isOpts = false, isToggle = false; double lo = 0, hi = 1, def = 0;
        void layout(int x, int y, int w) { juce::Rectangle<int> b(x, y, w, 24); name.setBounds(b.removeFromLeft(juce::jmin(200, w / 3))); if (isOpts) opts.setBounds(b.removeFromLeft(220)); else if (isToggle) tg.setBounds(b.removeFromLeft(60)); else s.setBounds(b.removeFromLeft(juce::jmin(360, b.getWidth()))); } };
    juce::String base() const { return "layers.LUA.oscs." + juce::String(slot); }
    static juce::var parseText(char* buf, int n) { return n > 0 ? juce::JSON::parse(juce::String::fromUTF8(buf, n)) : juce::var(); }
    juce::var info() { auto* e = store.engine(); if (!e) return {}; std::vector<char> b(1 << 16); return parseText(b.data(), vv_engine_lua_info(e, slot, b.data(), (int) b.size())); }
    juce::var validate(const juce::String& src) { auto* e = store.engine(); if (!e) return {}; std::vector<char> b(1 << 16); return parseText(b.data(), vv_engine_lua_validate(e, src.toRawUTF8(), (int) store.num(base() + ".lua.seed", 1), b.data(), (int) b.size())); }
    void say(const juce::String& t, bool err) { report.setText(t, juce::dontSendNotification); report.setColour(juce::Label::textColourId, err ? juce::Colours::red : juce::Colours::lightgreen); }
    void validateOnly() { const auto r = validate(code.getText()); if ((bool) r["ok"]) say("ok: syntax OK, setup ran (" + r["setupInstr"].toString() + " instructions)", false); else say("error: line " + r["line"].toString() + ": " + r["error"].toString(), true); }
    void apply(const juce::String& src) {
        const auto r = validate(src);
        if ((bool) r["ok"]) { int n = 0; if (auto* a = r["params"].getArray()) n = a->size(); say("ok: compiled - " + juce::String(n) + " parameter(s), setup " + r["setupInstr"].toString() + " instructions", false);
            store.setMany({ { base() + ".lua.src", src }, { base() + ".enabled", true } }); }
        else say("error: line " + r["line"].toString() + ", col " + r["col"].toString() + ": " + r["error"].toString() + "  (the previous script keeps playing)", true);
        rebuildParams();
    }
    void showLibrary() {
        juce::PopupMenu m; for (int i = 0; i < vv_engine_lua_factory_count(); ++i) m.addItem(i + 1, juce::String(vv_engine_lua_factory_id(i)).replaceCharacter('_', ' '));
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&libB), [this](int r) { if (r <= 0) return; const juce::String src = vv_engine_lua_factory_src(r - 1); code.setText(src, false); apply(src); });
    }
    void reload() { code.setText(store.str(base() + ".lua.src", ""), false); enabledT.setToggleState(store.flag(base() + ".enabled", false), juce::dontSendNotification); rebuildParams(); }
    void rebuildParams() {
        params.clear(); const auto in = info(); header.setText(in["name"].toString().isEmpty() ? juce::String() : in["name"].toString() + (in["author"].toString().isEmpty() ? juce::String() : " - " + in["author"].toString()), juce::dontSendNotification);
        desc.setText(in["desc"].toString(), juce::dontSendNotification);
        if (auto* arr = in["params"].getArray()) for (auto& p : *arr) {
            auto row = std::make_unique<ParamRow>(); row->id = p["id"].toString(); row->name.setText(p["label"].toString().isEmpty() ? row->id : p["label"].toString(), juce::dontSendNotification); row->name.setFont(VVLookAndFeel::mono(11.f));
            row->lo = p.hasProperty("min") ? (double) p["min"] : 0.0; row->hi = juce::jmax(row->lo + 1e-6, p.hasProperty("max") ? (double) p["max"] : 1.0); row->def = p.hasProperty("def") ? (double) p["def"] : row->lo;
            const juce::String path = base() + ".lua.params." + row->id; auto* rp = row.get();
            if (auto* o = p["options"].getArray(); o && o->size() > 0) { rp->isOpts = true; int k = 1; for (auto& t : *o) rp->opts.addItem(t.toString(), k++); rp->opts.setSelectedId((int) store.num(path, rp->def) + 1, juce::dontSendNotification);
                rp->opts.onChange = [this, rp, path] { store.set(path, (double) (rp->opts.getSelectedId() - 1)); }; addAndMakeVisible(rp->opts); }
            else if (p["kind"].toString() == "toggle" || p["kind"].toString() == "bool") { rp->isToggle = true; rp->tg.setToggleState(store.num(path, rp->def) != 0, juce::dontSendNotification); rp->tg.onClick = [this, rp, path] { store.set(path, rp->tg.getToggleState() ? 1.0 : 0.0); }; addAndMakeVisible(rp->tg); }
            else { rp->s.setSliderStyle(juce::Slider::LinearBar); rp->s.setRange(rp->lo, rp->hi, p.hasProperty("step") ? (double) p["step"] : 0.0); rp->s.setDoubleClickReturnValue(true, rp->def); rp->s.setValue(store.num(path, rp->def), juce::dontSendNotification);
                rp->s.onValueChange = [this, rp, path] { store.set(path, rp->s.getValue()); }; addAndMakeVisible(rp->s); }
            addAndMakeVisible(rp->name); params.push_back(std::move(row)); }
        resized();
    }
    void timerCallback() override {
        auto* e = store.engine(); if (!e || !isShowing()) return; std::vector<char> b(1 << 14); const auto st = parseText(b.data(), vv_engine_lua_stats(e, slot, b.data(), (int) b.size())); if (!st.isObject()) return;
        juce::String t = "voices " + st["voices"].toString() + "  |  block " + st["instr"].toString() + "/15000 instr  |  faults " + st["faults"].toString();
        if (st["error"].toString().isNotEmpty()) t = "runtime: " + st["error"].toString() + "\n" + t; statsL.setText(t, juce::dontSendNotification);
    }
    Store& store; int slot = 0; juce::ComboBox slotBox; juce::ToggleButton enabledT; juce::TextButton runB, valB, libB; juce::TextEditor code; juce::Label report, statsL, header, desc; std::vector<std::unique_ptr<ParamRow>> params;
};

} // namespace vv
