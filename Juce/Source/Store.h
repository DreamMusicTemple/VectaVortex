// Store: the UI's mirror of the engine's live preset (JSON). Every control reads/writes a dot path ("layers.WT.oscs.0.level");
// writes update the mirror and go straight to the engine with vv_engine_set_value (no audio-thread work in the UI).
// Message-thread only.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <map>
#include "vv_engine.h"

namespace vv {

class Store : private juce::AsyncUpdater {
public:
    struct Listener { virtual ~Listener() = default; virtual void storeChanged() = 0; };

    explicit Store(VVEngine* engine) : engine_(engine) {}
    VVEngine* engine() const { return engine_; }

    // ---- reading
    juce::var get(const juce::String& path) const;
    double num(const juce::String& path, double def = 0) const;
    bool flag(const juce::String& path, bool def = false) const;
    juce::String str(const juce::String& path, const juce::String& def = {}) const;
    int count(const juce::String& path) const;
    bool has(const juce::String& path) const { return !get(path).isVoid(); }

    // ---- writing (mirror + engine)
    void set(const juce::String& path, const juce::var& v);
    void setMany(const std::vector<std::pair<juce::String, juce::var>>& kv);   // one engine update for several paths
    void append(const juce::String& path, const juce::var& item);
    void remove(const juce::String& path, int index);
    void move(const juce::String& path, int from, int to);

    // ---- instrument-wide settings that are not part of a preset. Paths starting with '@' address them ("@masterTone", "@mpe"):
    // every Knob/Toggle/Choice works on them transparently. Persisted by the processor's state.
    double global(const juce::String& name, double def = 0) const { auto it = globals_.find(name); return it == globals_.end() ? def : it->second; }
    void setGlobal(const juce::String& name, double v);
    void pushLimiter();
    void applyGlobals();                            // push all stored globals to the engine (after load / prepare)
    const std::map<juce::String, double>& globals() const { return globals_; }
    std::map<juce::String, double>& globalsMutable() { return globals_; }

    // ---- whole presets
    void reload();                                  // re-read the engine's preset (after a load or host automation)
    juce::String exportSinglePresetFile(const juce::String& name, const juce::String& category = "Misc") const;   // web-compatible .vvpreset.json
    juce::String exportJSON() const;                // engine preset only
    juce::String exportPresetFile() const;          // + globals + notes: what is written to preset files
    juce::String notes() const { return notes_; }
    void setNotes(const juce::String& n) { notes_ = n; if (onEdited) onEdited(); }
    bool loadJSON(const juce::String& text);        // engine load + reload + host-parameter sync
    int revision() const { return revision_; }

    // ---- undo / redo (web top bar). Edits within 700 ms of each other form one undo step; preset loads are always a step of their own.
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool undo();
    bool redo();
    void clearHistory() { undo_.clear(); redo_.clear(); }
    // Preset generator (web: magic wand GENERATE + MODIFY). Runs in the engine, refreshes the mirror, then files the result in userDir
    // as "<Category> Gen N" / "<name> (mod)" like the web app does. Returns the saved name, or empty on failure.
    juce::String generatePreset(const juce::String& category, const juce::File& userDir);
    juce::String modifyPreset(const juce::File& userDir);
    juce::String presetName() const { return presetName_; }
    void setPresetName(const juce::String& n) { presetName_ = n; }

    void addListener(Listener* l) { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }
    std::function<void()> onPresetLoaded;           // processor refreshes its host parameters here
    std::function<void()> onEdited;                 // after UI edits (debounced by the owner)

private:
    bool assign(const juce::String& path, const juce::var& v);   // mirror only
    void handleAsyncUpdate() override { listeners_.call([](Listener& l) { l.storeChanged(); }); }
    void notify() { ++revision_; triggerAsyncUpdate(); }
    void captureUndo(bool force = false);
    bool replay(std::vector<juce::String>& from, std::vector<juce::String>& to);
    std::vector<juce::String> undo_, redo_; juce::uint32 lastEditMs_ = 0; bool replaying_ = false; juce::String presetName_;
    VVEngine* engine_;
    juce::var doc_;
    std::map<juce::String, double> globals_;
    int revision_ = 0;
    juce::String notes_;
    juce::ListenerList<Listener> listeners_;
};

} // namespace vv
