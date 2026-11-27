// Clip launcher (JUCE port of ClipLauncherView.swift): 8 banks x 8 slots of loopable note + automation clips launched on the engine's quantised transport
// clock. Model, validation, undo, recording and playback all live in the C++ core (Core/include/vv/clips.hpp, tested by clips_tests.cpp); this is the view
// over the C ABI (vv_clip_*). Clip state is saved inside the preset JSON (extra.clips). NOT COMPILED here (no JUCE build available).
#pragma once
#include "Widgets.h"
#include "vv_engine.h"

namespace vv {

class ClipLauncher : public juce::Component, private juce::Timer {
public:
    explicit ClipLauncher(Store& s) : store(s) {
        for (int b = 0; b < 8; ++b) { auto* t = bankBtns.add(new juce::TextButton("BANK " + juce::String(b + 1))); t->setClickingTogglesState(true); t->setRadioGroupId(91);
            t->onClick = [this, b] { op("bank", obj({ { "bank", b } })); }; addAndMakeVisible(t); }
        quantBox.addItemList({ "Immediate", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" }, 1); quantBox.onChange = [this] { op("quant", obj({ { "q", quantBox.getSelectedId() - 1 } })); }; addAndMakeVisible(quantBox);
        modeBox.addItemList({ "Replace", "Overdub" }, 1); modeBox.onChange = [this] { op("mode", obj({ { "m", modeBox.getSelectedId() - 1 } })); say(modeBox.getSelectedId() == 1 ? "Replace: starting a recording clears the clip first (Undo restores)." : "Overdub: recording layers new notes on top of the clip."); }; addAndMakeVisible(modeBox);
        auto mk = [this](juce::TextButton& b, const char* t, std::function<void()> f) { b.setButtonText(t); b.onClick = std::move(f); addAndMakeVisible(b); };
        mk(stopAllB, "STOP ALL", [this] { if (store.engine()) vv_clip_stop_all(store.engine()); }); mk(undoB, "UNDO", [this] { if (!op("undo")) say("Nothing to undo."); });
        mk(factoryB, "RESTORE FACTORY", [this] { confirm("Restore the factory clips into this bank? Clips in slots 1-6 are replaced (Undo restores).", [this] { op("factory", obj({ { "bank", bank } })); say("Factory clips restored."); }); });
        msg.setColour(juce::Label::textColourId, col::dim); msg.setFont(VVLookAndFeel::mono(11.f)); addAndMakeVisible(msg);
        refresh(); startTimerHz(10);
    }
    void visibilityChanged() override { if (isVisible()) refresh(); }
    void resized() override {
        auto r = getLocalBounds().reduced(6); auto bar = r.removeFromTop(30);
        const int w = juce::jmin(90, bar.getWidth() / 8); for (auto* b : bankBtns) b->setBounds(bar.removeFromLeft(w).reduced(2));
        auto bar2 = r.removeFromTop(30); quantBox.setBounds(bar2.removeFromLeft(130).reduced(2)); modeBox.setBounds(bar2.removeFromLeft(110).reduced(2));
        for (auto* b : { &stopAllB, &undoB, &factoryB }) b->setBounds(bar2.removeFromLeft(b == &factoryB ? 170 : 100).reduced(2));
        msg.setBounds(r.removeFromBottom(20)); grid = r.reduced(0, 4);
    }
    void paint(juce::Graphics& g) override {
        for (int i = 0; i < 8; ++i) {
            const auto c = cell(i).toFloat().reduced(3.f); const auto& s = slots[bank * 8 + i]; const int slot = bank * 8 + i;
            const bool playing = isBit(playingMask, slot), qs = isBit(qStart, slot), qt = isBit(qStop, slot);
            const auto colr = s.present ? juce::Colour::fromString("ff" + s.color.trimCharactersAtStart("#")) : col::line;
            g.setColour(s.present ? colr.withAlpha(playing ? 0.55f : 0.22f) : juce::Colour(0xff0d1013)); g.fillRoundedRectangle(c, 8.f);
            g.setColour(selected == slot ? col::cyan : (s.present ? colr : col::line)); g.drawRoundedRectangle(c, 8.f, selected == slot ? 2.5f : 1.f);
            g.setColour(col::ink); g.setFont(VVLookAndFeel::mono(13.f).boldened());
            if (!s.present) { g.setColour(col::dim); g.drawText("+ NEW CLIP  (" + juce::String(i + 1) + ")", c.toNearestInt(), juce::Justification::centred); continue; }
            g.drawText(s.name, c.toNearestInt().removeFromTop(26).reduced(8, 0), juce::Justification::centredLeft);
            g.setColour(col::dim); g.setFont(VVLookAndFeel::mono(10.f));
            g.drawText(s.layer + " | " + juce::String(s.bars) + " bar" + (s.bars == 1 ? "" : "s") + " | " + juce::String(s.noteCount) + " notes" + (s.laneCount ? " | " + juce::String(s.laneCount) + " lanes" : juce::String()) + (s.transpose ? " | " + juce::String(s.transpose > 0 ? "+" : "") + juce::String(s.transpose) + " st" : juce::String()),
                       c.toNearestInt().withTrimmedTop(26).removeFromTop(18).reduced(8, 0), juce::Justification::centredLeft);
            juce::String st = recording == slot ? "REC" : playing ? "PLAYING" : qs ? "QUEUED" : qt ? "STOPPING" : "STOPPED";
            g.setColour(recording == slot ? col::red : playing ? col::amber : col::dim); g.drawText(st, c.toNearestInt().reduced(8, 6), juce::Justification::bottomLeft);
            if (playing && phases[slot] >= 0) { const float f = juce::jlimit(0.f, 1.f, phases[slot] / (float) juce::jmax(1, s.bars * 4)); g.setColour(colr); g.fillRect(c.getX() + 6, c.getBottom() - 8.f, (c.getWidth() - 12.f) * f, 3.f); }
        }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const int i = cellAt(e.getPosition()); if (i < 0) return; const int slot = bank * 8 + i; select(slot);
        if (e.mods.isPopupMenu() || e.mods.isCtrlDown()) { showMenu(slot); return; }
        if (!slots[slot].present) { op("new", obj({ { "slot", slot }, { "layer", 0 }, { "bars", 4 } })); return; }
        toggle(slot);
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override { const int i = cellAt(e.getPosition()); if (i >= 0 && slots[bank * 8 + i].present) showMenu(bank * 8 + i); }

private:
    struct Info { bool present = false; juce::String name, color = "#4ad4d9", layer = "WT"; int bars = 4, transpose = 0, noteCount = 0, laneCount = 0; };
    static juce::var obj(std::initializer_list<std::pair<const char*, juce::var>> kv) { auto* o = new juce::DynamicObject(); for (auto& p : kv) o->setProperty(p.first, p.second); return juce::var(o); }
    static bool isBit(uint64_t m, int i) { return ((m >> (uint64_t) i) & 1u) != 0; }
    juce::Rectangle<int> cell(int i) const { const int cw = grid.getWidth() / 4, ch = grid.getHeight() / 2; return { grid.getX() + (i % 4) * cw, grid.getY() + (i / 4) * ch, cw, ch }; }
    int cellAt(juce::Point<int> p) const { for (int i = 0; i < 8; ++i) if (cell(i).contains(p)) return i; return -1; }
    void say(const juce::String& t) { msg.setText(t, juce::dontSendNotification); }
    bool op(const char* name, const juce::var& args = juce::var(), juce::String* out = nullptr) {
        if (!store.engine()) return false; const auto js = args.isVoid() ? juce::String("{}") : juce::JSON::toString(args, true);
        std::vector<char> buf(out ? (1 << 20) : 1); const bool ok = vv_clip_op(store.engine(), name, js.toRawUTF8(), out ? buf.data() : nullptr, out ? buf.size() : 0) == 1;
        if (out && ok) *out = juce::String::fromUTF8(buf.data()); refresh(); return ok;
    }
    void confirm(const juce::String& text, std::function<void()> go) {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Clip launcher", text, "OK", "Cancel", this, juce::ModalCallbackFunction::create([go](int r) { if (r == 1) go(); }));
    }
    void select(int slot) { selected = slot; op("select", obj({ { "slot", slot } })); }
    void toggle(int slot) {
        auto* e = store.engine(); if (!e || !slots[slot].present) return;
        if (isBit(playingMask, slot)) vv_clip_stop(e, slot, 0); else if (isBit(qStart, slot)) vv_clip_stop(e, slot, 1); else { vv_clip_launch(e, slot); store.setGlobal("transportRunning", 1); }
    }
    void refresh() {
        auto* e = store.engine(); if (!e) return; const size_t n = vv_clip_state_json(e, nullptr, 0); if (n == 0) return;
        std::vector<char> buf(n + 1); vv_clip_state_json(e, buf.data(), buf.size()); const auto j = juce::JSON::parse(juce::String::fromUTF8(buf.data()));
        if (!j.isObject()) return;
        const juce::StringArray qn { "imm", "1/16", "1/8", "1/4", "1/2", "1bar", "2bar", "4bar" }; int qi = qn.indexOf(j["quant"].toString()); quantBox.setSelectedId((qi < 0 ? 5 : qi) + 1, juce::dontSendNotification);
        modeBox.setSelectedId(j["mode"].toString() == "overdub" ? 2 : 1, juce::dontSendNotification); bank = juce::jlimit(0, 7, (int) j["bank"]);
        for (int b = 0; b < 8; ++b) bankBtns[b]->setToggleState(b == bank, juce::dontSendNotification);
        if (j["sel"].isObject()) selected = (int) j["sel"]["b"] * 8 + (int) j["sel"]["s"];
        for (auto& s : slots) s = Info();
        if (auto* banks = j["banks"].getArray()) for (int b = 0; b < banks->size() && b < 8; ++b) { const auto& bk = (*banks)[b];
            if (bk["name"].toString().isNotEmpty()) bankBtns[b]->setButtonText(bk["name"].toString());
            if (auto* sl = bk["slots"].getArray()) for (int i = 0; i < sl->size() && i < 8; ++i) { const auto& c = (*sl)[i]; if (!c.isObject()) continue; auto& s = slots[b * 8 + i];
                s.present = true; s.name = c["name"].toString(); s.color = c["color"].toString().isEmpty() ? "#4ad4d9" : c["color"].toString(); s.layer = c["layer"].toString().isEmpty() ? "WT" : c["layer"].toString();
                s.bars = c["bars"].isVoid() ? 4 : (int) c["bars"]; s.transpose = (int) c["tr"]; if (auto* a = c["notes"].getArray()) s.noteCount = a->size();
                if (auto* o = c["auto"].getDynamicObject()) s.laneCount = o->getProperties().size(); } }
        repaint();
    }
    void timerCallback() override {
        auto* e = store.engine(); if (!e || !isShowing()) return;
        playingMask = vv_clip_playing_mask(e); qStart = vv_clip_queued_start_mask(e); qStop = vv_clip_queued_stop_mask(e); const int rec = vv_clip_recording_slot(e);
        for (int i = 0; i < 64; ++i) phases[i] = isBit(playingMask, i) ? vv_clip_phase(e, i) : -1.f;
        if (rec >= 0 || recWasOn) { if (vv_clip_drain_recording(e) > 0 || recWasOn != (rec >= 0)) refresh(); recWasOn = rec >= 0; }
        recording = rec; repaint();
    }
    void showMenu(int slot) {
        const auto& s = slots[slot]; if (!s.present) return; juce::PopupMenu m;
        m.addItem(1, recording == slot ? "Stop recording" : "Record into this clip"); m.addItem(2, "Rename..."); m.addItem(3, "Duplicate"); m.addItem(4, "Delete");
        juce::PopupMenu bars; int k = 10; for (int b : { 1, 2, 4, 8, 16, 32, 64 }) bars.addItem(k++, juce::String(b) + (b == 1 ? " bar" : " bars"), true, b == s.bars); m.addSubMenu("Length", bars);
        juce::PopupMenu lay; k = 20; for (auto* l : { "WT", "FM", "SMP", "PRO", "SID", "LUA", "XFX" }) lay.addItem(k++, l, true, s.layer == l); m.addSubMenu("Layer", lay);
        m.addItem(30, "Next colour"); m.addItem(31, "Transpose +1"); m.addItem(32, "Transpose -1"); m.addItem(33, "Transpose +12"); m.addItem(34, "Transpose -12"); m.addItem(35, "Reset transpose");
        m.addItem(36, "Quantise notes to 1/16"); m.addItem(37, "Remove automation"); m.addSeparator(); m.addItem(40, "ROLL to CLIP (capture the piano roll)"); m.addItem(41, "CLIP to ROLL"); m.addSeparator();
        m.addItem(42, "Copy"); m.addItem(43, "Paste", clipboard.isNotEmpty());
        m.showMenuAsync(juce::PopupMenu::Options(), [this, slot](int r) { act(slot, r); });
    }
    void act(int slot, int r) {
        if (r <= 0) return; const auto& s = slots[slot]; auto* e = store.engine();
        static const char* palette[] = { "#4ad4d9", "#e2a04a", "#d94a9e", "#b026ff", "#f0e020", "#6cff7a", "#ff5a5a", "#7aa7ff" };
        if (r == 1) { if (recording == slot) { op("recordEnd"); say("Recording stopped."); } else { op("recordBegin", obj({ { "slot", slot } })); if (e) vv_clip_launch(e, slot); store.setGlobal("transportRunning", 1); say("Recording: play notes or move dials when the clip starts."); } }
        else if (r == 2) { auto* w = new juce::AlertWindow("Rename clip", "Name:", juce::MessageBoxIconType::NoIcon); w->addTextEditor("n", s.name); w->addButton("OK", 1); w->addButton("Cancel", 0);
            w->enterModalState(true, juce::ModalCallbackFunction::create([this, slot, w](int res) { if (res == 1) op("rename", obj({ { "slot", slot }, { "name", w->getTextEditorContents("n") } })); }), true); }
        else if (r == 3) { if (!op("duplicate", obj({ { "slot", slot } }))) say("Bank is full: delete a clip or use another bank."); }
        else if (r == 4) confirm("Delete \"" + s.name + "\"? (Undo restores it)", [this, slot] { op("delete", obj({ { "slot", slot } })); });
        else if (r >= 10 && r < 20) { static const int L[] = { 1, 2, 4, 8, 16, 32, 64 }; op("bars", obj({ { "slot", slot }, { "bars", L[r - 10] } })); }
        else if (r >= 20 && r < 30) op("layer", obj({ { "slot", slot }, { "layer", r - 20 } }));
        else if (r == 30) { int i = 0; for (int p = 0; p < 8; ++p) if (s.color == palette[p]) i = p + 1; op("color", obj({ { "slot", slot }, { "color", palette[i % 8] } })); }
        else if (r >= 31 && r <= 34) { const int d[] = { 1, -1, 12, -12 }; op("transpose", obj({ { "slot", slot }, { "delta", d[r - 31] } })); }
        else if (r == 35) op("setTranspose", obj({ { "slot", slot }, { "value", 0 } }));
        else if (r == 36) { op("quantiseNotes", obj({ { "slot", slot } })); say("Notes snapped to 1/16 (Undo restores)."); }
        else if (r == 37) confirm("Remove all automation from \"" + s.name + "\"?", [this, slot] { op("clearAuto", obj({ { "slot", slot } })); });
        else if (r == 40) { static const juce::StringArray LK { "WT", "FM", "SMP", "PRO", "SID", "LUA" }; const int layer = juce::jmax(0, LK.indexOf(s.layer));
            auto go = [this, slot, layer] { say(op("fromRoll", obj({ { "slot", slot }, { "layer", layer } })) ? "Clip captured from the piano roll." : "That layer has no piano-roll notes to capture."); };
            if (s.noteCount > 0) confirm("Replace the notes of \"" + s.name + "\" with the " + s.layer + " piano roll?", go); else go(); }
        else if (r == 41) confirm("Replace the " + s.layer + " piano roll with this clip?", [this, slot] { if (op("toRoll", obj({ { "slot", slot } }))) { store.reload(); say("Clip copied to the piano roll."); } });
        else if (r == 42) { juce::String t; if (op("export", obj({ { "slot", slot } }), &t)) { clipboard = t; say("Copied \"" + s.name + "\"."); } }
        else if (r == 43) { const auto t = clipboard; auto go = [this, slot, t] { op("paste", obj({ { "slot", slot }, { "json", t } })); }; if (s.present) confirm("Replace \"" + s.name + "\" with the copied clip?", go); else go(); }
    }
    Store& store; Info slots[64]; int bank = 0, selected = 0, recording = -1; bool recWasOn = false; uint64_t playingMask = 0, qStart = 0, qStop = 0; float phases[64] = {};
    juce::OwnedArray<juce::TextButton> bankBtns; juce::ComboBox quantBox, modeBox; juce::TextButton stopAllB, undoB, factoryB; juce::Label msg; juce::Rectangle<int> grid;
    static inline juce::String clipboard;
};

} // namespace vv
