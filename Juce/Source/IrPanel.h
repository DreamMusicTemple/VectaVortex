// Convolution (IR) reverb panel for the effects-rack "ir" slot - JUCE port of IRReverbView.swift: factory IRs by category, user import
// (WAV / AIFF), replace, remove, trim silence, IR envelope with start / end markers. The IR library is process-wide in the C core (vv_ir_*).
// Imported files are copied into <app data>/VektorVortex/IRs as <hex>__<name>.<ext> and re-scanned when the panel is created, so a preset's user IR
// is found again after a restart. No audition button: a plugin has no audio device of its own - the IR is heard through the effect itself.
// NOT COMPILED (no JUCE build available where this was written) - build on a machine with JUCE.
#pragma once
#include "Widgets.h"
#include "vv_engine.h"

namespace vv {

class IrPanel : public juce::Component, private Store::Listener {
public:
    IrPanel(Store& s, const juce::String& paramsBase) : store(s), base(paramsBase), env(s, paramsBase) {
        vv_ir_scan_dir(folder().getFullPathName().toRawUTF8());
        for (int i = 0; i < vv_ir_category_count(); ++i) cats.add(vv_ir_category_name(i)); cats.add("Imported");
        catBox.addItemList(cats, 1); catBox.onChange = [this] { category = catBox.getText(); fillIrs(); }; addAndMakeVisible(catBox);
        irBox.onChange = [this] { const int id = irBox.getSelectedId(); if (id > 0 && id <= ids.size()) pick(ids[id - 1], irBox.getText()); }; addAndMakeVisible(irBox);
        auto mk = [this](juce::TextButton& b, const char* t, std::function<void()> f) { b.setButtonText(t); b.onClick = std::move(f); addAndMakeVisible(b); };
        mk(importB, "Import...", [this] { chooseFile(false); }); mk(replaceB, "Replace...", [this] { chooseFile(true); }); mk(removeB, "Remove", [this] { removeCurrent(); });
        mk(trimB, "Trim silence", [this] { double a = 0, b = 1; vv_ir_auto_trim(irId().toRawUTF8(), 48000, &a, &b); store.setMany({ { base + ".start", a }, { base + ".end", b } }); });
        msg.setColour(juce::Label::textColourId, col::dim); msg.setFont(VVLookAndFeel::mono(11.f)); addAndMakeVisible(msg);
        warn.setColour(juce::Label::textColourId, juce::Colours::orange); warn.setFont(VVLookAndFeel::mono(11.f)); addAndMakeVisible(warn);
        addAndMakeVisible(env);
        store.addListener(this); sync();
    }
    ~IrPanel() override { store.removeListener(this); }
    void storeChanged() override { if (irId() != lastId) sync(); }
    void resized() override {
        auto r = getLocalBounds(); auto row = r.removeFromTop(28);
        catBox.setBounds(row.removeFromLeft(150).reduced(2)); irBox.setBounds(row.reduced(2));
        env.setBounds(r.removeFromTop(86).reduced(2));
        auto b = r.removeFromTop(28); for (auto* c : { &importB, &replaceB, &removeB, &trimB }) c->setBounds(b.removeFromLeft(120).reduced(2));
        warn.setBounds(r.removeFromTop(18)); msg.setBounds(r.removeFromTop(18));
    }
private:
    struct Env : juce::Component {
        Env(Store& s, const juce::String& b) : st(s), base(b) {}
        void paint(juce::Graphics& g) override {
            g.setColour(juce::Colours::black.withAlpha(0.6f)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.f);
            const auto id = st.str(base + ".irId", "f:hall-concert"); float v[160] = {}; const double sec = vv_ir_envelope(id.toRawUTF8(), v, 160, 48000);
            const double s0 = st.num(base + ".start", 0), e0 = st.num(base + ".end", 1); const float w = (float) getWidth() / 160.f, H = (float) getHeight();
            for (int i = 0; i < 160; ++i) { const double t = i / 160.0; const float h = v[i] * (H - 6.f); g.setColour(col::cyan.withAlpha(t >= s0 && t <= e0 ? 0.9f : 0.25f)); g.fillRect((float) i * w, H - h, juce::jmax(1.f, w - 0.5f), h); }
            g.setColour(juce::Colours::orange); for (double t : { s0, e0 }) g.drawVerticalLine((int) (getWidth() * t), 0.f, H);
            g.setColour(col::dim); g.setFont(VVLookAndFeel::mono(10.f)); g.drawText(juce::String(sec, 2) + " s", getLocalBounds().reduced(4), juce::Justification::topRight);
        }
        Store& st; juce::String base;
    };
    static juce::File folder() { auto d = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("VektorVortex").getChildFile("IRs"); d.createDirectory(); return d; }
    juce::String irId() const { return store.str(base + ".irId", "f:hall-concert"); }
    void pick(const juce::String& id, const juce::String& name) { store.setMany({ { base + ".irId", id }, { base + ".irName", name } }); }
    void fillIrs() {
        irBox.clear(juce::dontSendNotification); ids.clear(); int sel = 0;
        auto add = [&](const juce::String& id, const juce::String& name) { ids.add(id); irBox.addItem(name, ids.size()); if (id == irId()) sel = ids.size(); };
        if (category == "Imported") { auto arr = juce::JSON::parse(userJson()); if (auto* a = arr.getArray()) for (auto& e : *a) add(e["id"].toString(), e["name"].toString()); }
        else for (int i = 0; i < vv_ir_factory_count(); ++i) if (juce::String(vv_ir_factory_category(i)) == category) add(vv_ir_factory_id(i), vv_ir_factory_name(i));
        if (sel) irBox.setSelectedId(sel, juce::dontSendNotification); else irBox.setText(store.str(base + ".irName", irId()), juce::dontSendNotification);
    }
    static juce::String userJson() { std::vector<char> b(1 << 14); size_t n = vv_ir_user_json(b.data(), b.size()); if (n >= b.size()) { b.resize(n + 1); n = vv_ir_user_json(b.data(), b.size()); } return juce::String::fromUTF8(b.data(), (int) n); }
    static juce::String missingJson() { std::vector<char> b(1 << 12); size_t n = vv_ir_missing_json(b.data(), b.size()); if (n >= b.size()) { b.resize(n + 1); n = vv_ir_missing_json(b.data(), b.size()); } return juce::String::fromUTF8(b.data(), (int) n); }
    void sync() {
        lastId = irId(); const bool user = lastId.startsWith("u:");
        if (user) category = "Imported"; else for (int i = 0; i < vv_ir_factory_count(); ++i) if (lastId == vv_ir_factory_id(i)) category = vv_ir_factory_category(i);
        if (category.isEmpty()) category = cats[0];
        catBox.setText(category, juce::dontSendNotification); fillIrs();
        replaceB.setEnabled(user); removeB.setEnabled(user);
        warn.setText(missingJson().contains("\"" + lastId + "\"") ? "IR \"" + store.str(base + ".irName", lastId) + "\" is missing - the default hall plays instead. Import it again (Replace) or choose another." : juce::String(), juce::dontSendNotification);
        env.repaint();
    }
    void chooseFile(bool replacing) {
        chooser = std::make_unique<juce::FileChooser>(replacing ? "Replace the IR with..." : "Import an impulse response", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, replacing](const juce::FileChooser& fc) {
            const auto f = fc.getResult(); if (!f.existsAsFile()) return;
            char id[64] = {}, m[256] = {}; const auto name = f.getFileNameWithoutExtension();
            const juce::String old = replacing ? irId() : juce::String();
            if (!vv_ir_import_file(f.getFullPathName().toRawUTF8(), name.toRawUTF8(), old.toRawUTF8(), id, 64, m, 256)) { msg.setText(m, juce::dontSendNotification); return; }
            const juce::String nid(id), hex = nid.replace("u:", "");
            f.copyFileTo(folder().getChildFile(hex + "__" + name + f.getFileExtension().toLowerCase()));   // persistent copy: the scan at the next start re-registers it
            if (replacing && old != nid) dropFiles(old);
            msg.setText(m, juce::dontSendNotification); pick(nid, name); sync();
        });
    }
    void dropFiles(const juce::String& id) { const auto hex = id.replace("u:", ""); for (auto& f : folder().findChildFiles(juce::File::findFiles, false, hex + "__*")) f.deleteFile(); vv_ir_remove(id.toRawUTF8()); }
    void removeCurrent() { const auto old = irId(); if (!old.startsWith("u:")) return; pick("f:hall-concert", "Concert Hall"); dropFiles(old); sync(); }

    Store& store; juce::String base, lastId, category; Env env;
    juce::StringArray cats, ids; juce::ComboBox catBox, irBox; juce::TextButton importB, replaceB, removeB, trimB; juce::Label msg, warn;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace vv
