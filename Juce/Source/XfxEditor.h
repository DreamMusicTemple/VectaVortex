// XFX MODE editor (JUCE): a different sound on every key. Header-only; opened from the XFX button beside the Samples mixer.
// State lives in the preset (extra.xfx, the web app's own JSON); every edit goes through vv_engine_xfx_action and the Store then reloads its mirror.
// Covers: on/off, 8 banks (rename / clear), key picker (keyboard), per-key layer switches, and per-key parameter overrides (curated list + any registry path).
// NOT compiled or run here (no JUCE toolchain in the build sandbox) - first Xcode/CMake build may need small fixes. The engine side is tested (Core/tests/xfx_env_tests.cpp).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Store.h"
#include "Widgets.h"

namespace vv {

class XfxEditor : public juce::Component, private Store::Listener, private juce::MidiKeyboardState::Listener {
public:
    explicit XfxEditor(Store& s) : store(s), keys(kbState, juce::MidiKeyboardComponent::horizontalKeyboard) {
        store.addListener(this);
        enableT.setButtonText("XFX MODE ON"); enableT.onClick = [this] { act("enable", "{\"on\":" + juce::String(enableT.getToggleState() ? "true" : "false") + "}"); };
        for (int i = 0; i < 8; ++i) bankC.addItem("Bank " + juce::String(i + 1), i + 1);
        bankC.onChange = [this] { if (!syncing_) act("bank", "{\"index\":" + juce::String(bankC.getSelectedId() - 1) + "}"); };
        renameB.setButtonText("Rename"); renameB.onClick = [this] { askName(); };
        clearBankB.setButtonText("Clear bank"); clearBankB.onClick = [this] { act("clearBank", "{}"); };
        resetB.setButtonText("Reset XFX"); resetB.onClick = [this] { act("reset", "{}"); };
        for (auto* c : { &enableT, &bankC, &renameB, &clearBankB, &resetB }) addAndMakeVisible(c);
        keys.setAvailableRange(24, 108); keys.setKeyWidth(14.f); kbState.addListener(this); addAndMakeVisible(keys);
        keyL.setFont(VVLookAndFeel::mono(12.f)); addAndMakeVisible(keyL); progL.setFont(VVLookAndFeel::mono(10.f)); progL.setColour(juce::Label::textColourId, col::dim); addAndMakeVisible(progL);
        static const char* ln[kL] = { "Wavetable", "FM", "Samples", "Procedural", "SID" };
        for (int i = 0; i < kL; ++i) { layerC[i].addItem(juce::String(ln[i]) + ": inherit", 1); layerC[i].addItem(juce::String(ln[i]) + ": ON", 2); layerC[i].addItem(juce::String(ln[i]) + ": OFF", 3);
            layerC[i].onChange = [this, i] { if (syncing_) return; const int id = layerC[i].getSelectedId();
                act("layer", "{\"note\":" + juce::String(key_) + ",\"layer\":\"" + juce::String(kKeys[i]) + "\",\"state\":" + (id == 1 ? "null" : id == 2 ? "true" : "false") + "}"); };
            addAndMakeVisible(layerC[i]); }
        pathC.setEditableText(true);   // any registry path ("WT.oscs.0.filter.cutoff") can be typed; the list holds the common ones
        for (auto p : kCommon) pathC.addItem(p, pathC.getNumItems() + 1);
        pathC.setText("WT.env.rep", juce::dontSendNotification); valueE.setText("3"); valueE.setInputRestrictions(24, "0123456789.-eE");
        setB.setButtonText("Set override"); setB.onClick = [this] { setOverride(); };
        clearKeyB.setButtonText("Clear key"); clearKeyB.onClick = [this] { act("clearKey", "{\"note\":" + juce::String(key_) + "}"); };
        copyB.setButtonText("Copy to next key"); copyB.onClick = [this] { if (key_ < 127) { act("copyKey", "{\"from\":" + juce::String(key_) + ",\"to\":" + juce::String(key_ + 1) + "}"); selectKey(key_ + 1); } };
        for (auto* c : { (juce::Component*) &pathC, (juce::Component*) &valueE, (juce::Component*) &setB, (juce::Component*) &clearKeyB, (juce::Component*) &copyB }) addAndMakeVisible(c);
        ovL.setFont(VVLookAndFeel::mono(11.f)); ovL.setJustificationType(juce::Justification::topLeft); addAndMakeVisible(ovL);
        ovList.setModel(&model_); ovList.setRowHeight(24); addAndMakeVisible(ovList);
        selectKey(60); refresh();
    }
    ~XfxEditor() override { store.removeListener(this); kbState.removeListener(this); }
    void resized() override {
        auto r = getLocalBounds().reduced(8); auto top = r.removeFromTop(28);
        enableT.setBounds(top.removeFromLeft(130)); bankC.setBounds(top.removeFromLeft(110).reduced(2)); renameB.setBounds(top.removeFromLeft(80).reduced(2));
        clearBankB.setBounds(top.removeFromLeft(100).reduced(2)); resetB.setBounds(top.removeFromLeft(100).reduced(2));
        r.removeFromTop(6); keys.setBounds(r.removeFromTop(86)); r.removeFromTop(4);
        keyL.setBounds(r.removeFromTop(20)); progL.setBounds(r.removeFromTop(30)); r.removeFromTop(4);
        auto lr = r.removeFromTop(28); const int lw = lr.getWidth() / kL; for (int i = 0; i < kL; ++i) layerC[i].setBounds(lr.removeFromLeft(lw).reduced(2));
        r.removeFromTop(6); auto ar = r.removeFromTop(28);
        pathC.setBounds(ar.removeFromLeft(juce::jmax(200, ar.getWidth() - 330)).reduced(2)); valueE.setBounds(ar.removeFromLeft(80).reduced(2)); setB.setBounds(ar.removeFromLeft(120).reduced(2));
        auto br = r.removeFromTop(28); clearKeyB.setBounds(br.removeFromLeft(120).reduced(2)); copyB.setBounds(br.removeFromLeft(170).reduced(2));
        r.removeFromTop(4); ovL.setBounds(r.removeFromTop(18)); ovList.setBounds(r);
    }
    void paint(juce::Graphics& g) override { g.fillAll(col::bg); }
private:
    static constexpr int kL = 5;
    static inline const char* kKeys[kL] = { "WT", "FM", "SMP", "PRO", "SID" };
    static inline const char* kCommon[] = { "WT.env.a", "WT.env.d", "WT.env.s", "WT.env.r", "WT.env.rep", "WT.oscs.0.level", "WT.oscs.0.oct", "WT.oscs.0.pan", "WT.oscs.0.hold",
        "WT.oscs.0.filter.cutoff", "WT.oscs.0.filter.reso", "WT.oscs.0.filter.type", "FM.env.a", "FM.env.d", "FM.env.s", "FM.env.r", "FM.env.rep", "FM.oscs.0.level", "SMP.oscs.0.level", "SMP.oscs.0.env.rep",
        "SMP.globalAdsr.rep", "PRO.env.a", "PRO.env.r", "PRO.env.rep", "SID.oscs.0.env.rep", "SID.oscs.0.level" };
    void act(const juce::String& a, const juce::String& args) { if (store.engine() && vv_engine_xfx_action(store.engine(), a.toRawUTF8(), args.toRawUTF8())) store.reload(); refresh(); }
    void storeChanged() override { refresh(); }
    void handleNoteOn(juce::MidiKeyboardState*, int, int note, float) override { selectKey(note); if (store.engine()) vv_engine_note_on(store.engine(), note, 0.8f); }
    void handleNoteOff(juce::MidiKeyboardState*, int, int note, float) override { if (store.engine()) vv_engine_note_off(store.engine(), note); }
    static juce::String noteName(int n) { static const char* nn[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String(nn[n % 12]) + juce::String(n / 12 - 1); }
    juce::var xfx() const {
        if (!store.engine()) return {}; const size_t n = vv_engine_xfx_export(store.engine(), nullptr, 0); std::vector<char> b(n + 1, 0);
        vv_engine_xfx_export(store.engine(), b.data(), n + 1); return juce::JSON::parse(juce::String::fromUTF8(b.data()));
    }
    juce::var activeBank(const juce::var& x) const { const int a = juce::jlimit(0, 7, (int) x.getProperty("active", 0)); auto* arr = x.getProperty("banks", {}).getArray(); return arr && a < arr->size() ? (*arr)[a] : juce::var(); }
    void selectKey(int n) { key_ = juce::jlimit(0, 127, n); refresh(); }
    void refresh() {
        syncing_ = true; const juce::var x = xfx(); const juce::var bank = activeBank(x);
        enableT.setToggleState(x.isObject() ? (bool) x.getProperty("enabled", true) : false, juce::dontSendNotification);
        bankC.setSelectedId(1 + juce::jlimit(0, 7, (int) x.getProperty("active", 0)), juce::dontSendNotification);
        const juce::var rec = bank.getProperty("keys", {}).getProperty(juce::String(key_), {});
        keyL.setText("Key " + juce::String(key_) + "  (" + noteName(key_) + ")  -  " + (rec.isObject() ? "programmed" : "plays the normal sound"), juce::dontSendNotification);
        juce::StringArray prog; if (auto* ko = bank.getProperty("keys", {}).getDynamicObject()) for (auto& p : ko->getProperties()) prog.add(noteName(p.name.toString().getIntValue()));
        progL.setText(prog.isEmpty() ? "No programmed keys in this bank." : "Programmed keys: " + prog.joinIntoString(" "), juce::dontSendNotification);
        for (int i = 0; i < kL; ++i) { const juce::var e = rec.getProperty("en", {}).getProperty(kKeys[i], {}); layerC[i].setSelectedId(e.isVoid() ? 1 : ((bool) e ? 2 : 3), juce::dontSendNotification); }
        rows_.clear(); if (auto* oo = rec.getProperty("o", {}).getDynamicObject()) for (auto& p : oo->getProperties()) rows_.push_back({ p.name.toString(), p.value.toString() });
        ovL.setText("Overrides on this key (" + juce::String((int) rows_.size()) + ") - click a row to remove it", juce::dontSendNotification); ovList.updateContent(); ovList.repaint();
        syncing_ = false;
    }
    void setOverride() {
        const juce::String path = pathC.getText().trim(); if (path.isEmpty()) return;
        act("setOverride", "{\"note\":" + juce::String(key_) + ",\"path\":\"" + path.replace("\"", "") + "\",\"value\":" + juce::String(valueE.getText().getDoubleValue()) + "}");
    }
    void askName() {
        auto* w = new juce::AlertWindow("Rename bank", "Name for this XFX bank", juce::MessageBoxIconType::NoIcon); w->addTextEditor("n", "", "");
        w->addButton("OK", 1); w->addButton("Cancel", 0);
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) { if (r == 1) act("rename", "{\"name\":\"" + w->getTextEditorContents("n").replace("\"", "") + "\"}"); delete w; }), false);
    }
    struct Row { juce::String path, value; };
    struct Model : juce::ListBoxModel {
        explicit Model(XfxEditor& o) : owner(o) {}
        int getNumRows() override { return (int) owner.rows_.size(); }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel) override {
            if (row < 0 || row >= (int) owner.rows_.size()) return; if (sel) g.fillAll(col::cyan.withAlpha(0.2f));
            g.setColour(juce::Colours::white); g.setFont(VVLookAndFeel::mono(12.f)); g.drawText(owner.rows_[(size_t) row].path + "  =  " + owner.rows_[(size_t) row].value, 6, 0, w - 12, h, juce::Justification::centredLeft);
        }
        void listBoxItemClicked(int row, const juce::MouseEvent&) override {
            if (row < 0 || row >= (int) owner.rows_.size()) return;
            owner.act("clearOverride", "{\"note\":" + juce::String(owner.key_) + ",\"path\":\"" + owner.rows_[(size_t) row].path + "\"}");
        }
        XfxEditor& owner;
    };
    Store& store; juce::MidiKeyboardState kbState; juce::MidiKeyboardComponent keys;
    juce::ToggleButton enableT; juce::ComboBox bankC, layerC[kL]; juce::TextButton renameB, clearBankB, resetB, setB, clearKeyB, copyB;
    juce::Label keyL, progL, ovL; juce::ComboBox pathC; juce::TextEditor valueE; juce::ListBox ovList; Model model_{ *this }; std::vector<Row> rows_;
    int key_ = 60; bool syncing_ = false;
public:
    // Opens the editor in a floating window (call from the XFX button).
    static void show(Store& s) {
        auto* ed = new XfxEditor(s); ed->setSize(760, 560);
        juce::DialogWindow::LaunchOptions o; o.content.setOwned(ed); o.dialogTitle = "XFX MODE"; o.dialogBackgroundColour = col::bg; o.escapeKeyTriggersCloseButton = true; o.useNativeTitleBar = true; o.resizable = true;
        o.launchAsync();
    }
};

}  // namespace vv
