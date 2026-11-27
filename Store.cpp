#include "Store.h"
#include "Scales.h"

namespace vv {

static juce::StringArray splitPath(const juce::String& p) { return juce::StringArray::fromTokens(p, ".", ""); }

static bool isIndex(const juce::String& s) { return s.isNotEmpty() && s.containsOnly("0123456789"); }

// Returns a pointer to the var at `keys` (a stable pointer into the DynamicObject/Array storage is not available in JUCE, so we
// copy-walk: arrays and objects are reference counted, so mutating the child through a copied var mutates the shared object).
static juce::var getAt(const juce::var& root, const juce::StringArray& keys) {
    juce::var cur = root;
    for (auto& k : keys) {
        if (isIndex(k)) { if (!cur.isArray()) return {}; const int i = k.getIntValue(); if (i >= cur.size()) return {}; cur = cur[i]; }
        else { if (auto* o = cur.getDynamicObject()) { if (!o->hasProperty(juce::Identifier(k))) return {}; cur = o->getProperty(juce::Identifier(k)); } else return {}; }
    }
    return cur;
}

juce::var Store::get(const juce::String& path) const {
    if (path.startsWithChar('@')) { auto it = globals_.find(path.substring(1)); return it == globals_.end() ? juce::var() : juce::var(it->second); }
    return path.isEmpty() ? doc_ : getAt(doc_, splitPath(path));
}
double Store::num(const juce::String& path, double def) const { auto v = get(path); return (v.isDouble() || v.isInt() || v.isInt64() || v.isBool()) ? (double) v : def; }
bool Store::flag(const juce::String& path, bool def) const { auto v = get(path); return v.isVoid() || v.isUndefined() ? def : (bool) v; }
juce::String Store::str(const juce::String& path, const juce::String& def) const { auto v = get(path); return v.isString() ? v.toString() : def; }
int Store::count(const juce::String& path) const { auto v = get(path); return v.isArray() ? v.size() : 0; }

bool Store::assign(const juce::String& path, const juce::var& v) {
    auto keys = splitPath(path);
    if (keys.isEmpty()) return false;
    // create missing containers down to the parent
    juce::var cur = doc_;
    for (int i = 0; i < keys.size() - 1; ++i) {
        const auto& k = keys[i]; const bool nextIdx = isIndex(keys[i + 1]);
        juce::var child;
        if (isIndex(k)) {
            if (!cur.isArray()) return false;
            const int idx = k.getIntValue(); auto* arr = cur.getArray();
            while (arr->size() <= idx) arr->add(juce::var());
            child = (*arr)[idx];
            if (!(child.isArray() || child.getDynamicObject())) { child = nextIdx ? juce::var(juce::Array<juce::var>()) : juce::var(new juce::DynamicObject()); arr->set(idx, child); }
        } else {
            auto* o = cur.getDynamicObject(); if (!o) return false;
            child = o->getProperty(juce::Identifier(k));
            if (!(child.isArray() || child.getDynamicObject())) { child = nextIdx ? juce::var(juce::Array<juce::var>()) : juce::var(new juce::DynamicObject()); o->setProperty(juce::Identifier(k), child); }
        }
        cur = child;
    }
    const auto& last = keys[keys.size() - 1];
    if (isIndex(last)) { if (!cur.isArray()) return false; auto* arr = cur.getArray(); const int idx = last.getIntValue(); while (arr->size() <= idx) arr->add(juce::var()); arr->set(idx, v); }
    else if (auto* o = cur.getDynamicObject()) o->setProperty(juce::Identifier(last), v);
    else return false;
    return true;
}

void Store::set(const juce::String& path, const juce::var& v) {
    if (path.startsWithChar('@')) { setGlobal(path.substring(1), (double) v); return; }
    captureUndo();
    if (!assign(path, v)) return;
    if (engine_) vv_engine_set_value(engine_, path.toRawUTF8(), juce::JSON::toString(v, true).toRawUTF8());
    notify();
    if (onEdited) onEdited();
}

void Store::append(const juce::String& path, const juce::var& item) {
    juce::Array<juce::var> a; auto cur = get(path); if (cur.isArray()) a = *cur.getArray();
    a.add(item); set(path, juce::var(a));
}
void Store::remove(const juce::String& path, int index) {
    auto cur = get(path); if (!cur.isArray() || index < 0 || index >= cur.size()) return;
    juce::Array<juce::var> a = *cur.getArray(); a.remove(index); set(path, juce::var(a));
}
void Store::move(const juce::String& path, int from, int to) {
    auto cur = get(path); if (!cur.isArray() || from < 0 || to < 0 || from >= cur.size() || to >= cur.size()) return;
    juce::Array<juce::var> a = *cur.getArray(); a.move(from, to); set(path, juce::var(a));
}

juce::String Store::exportJSON() const {
    if (!engine_) return {};
    const size_t n = vv_engine_get_value(engine_, "", nullptr, 0); if (n == 0) return {};
    std::vector<char> buf(n + 1, 0); vv_engine_get_value(engine_, "", buf.data(), buf.size());
    return juce::String::fromUTF8(buf.data(), (int) n);
}

// A saved preset = engine preset + the instrument-wide "globals" (master tone, mixers, bend range, MPE) + the user's notes.
juce::String Store::exportPresetFile() const {
    const auto base = exportJSON(); auto parsed = juce::JSON::parse(base);
    auto* o = parsed.getDynamicObject(); if (!o) return base;
    auto* g = new juce::DynamicObject(); for (auto& kv : globals_) g->setProperty(kv.first, kv.second);
    o->setProperty("globals", juce::var(g)); o->setProperty("notes", notes_);
    return juce::JSON::toString(parsed, false);
}

void Store::reload() {
    auto text = exportJSON(); if (text.isEmpty()) return;
    auto parsed = juce::JSON::parse(text);
    if (parsed.getDynamicObject()) { doc_ = parsed; notify(); }
}

juce::String Store::exportSinglePresetFile(const juce::String& name, const juce::String& category) const {
    // the web app's single-preset file ({vectaVortexPreset:1, name, category, data}); .vvpreset.json
    auto* o = new juce::DynamicObject(); o->setProperty("vectaVortexPreset", 1); o->setProperty("name", name); o->setProperty("category", category);
    o->setProperty("data", juce::JSON::parse(exportPresetFile())); return juce::JSON::toString(juce::var(o), false);
}

bool Store::loadJSON(const juce::String& textIn) {
    if (!engine_) return false;
    captureUndo(true);
    juce::String text = textIn;
    { const auto w = juce::JSON::parse(textIn);   // unwrap the web app's single-preset file
      if (w.getProperty("vectaVortexPreset", juce::var()).toString().isNotEmpty() && w.getProperty("data", juce::var()).getDynamicObject()) text = juce::JSON::toString(w.getProperty("data", juce::var()), false); }
    const auto utf8 = text.toUTF8();
    if (vv_engine_load_preset_json(engine_, utf8.getAddress(), utf8.sizeInBytes() - 1) != 1) return false;
    vv_engine_sync_params_from_preset(engine_);
    {   // notes and globals travel with the preset (also understands the web app's "extra" block)
        const auto parsed = juce::JSON::parse(text);
        if (!replaying_ || parsed.hasProperty("notes")) notes_ = parsed.getProperty("notes", juce::var()).toString();
        auto setG = [this](const juce::String& k, const juce::var& v) { if (!v.isVoid() && !v.isUndefined()) globals_[k] = (double) v; };
        if (auto* g = parsed.getProperty("globals", juce::var()).getDynamicObject()) for (auto& kv : g->getProperties()) setG(kv.name.toString(), kv.value);
        else if (auto* x = parsed.getProperty("extra", juce::var()).getDynamicObject()) {
            setG("masterTone", x->getProperty("masterTone")); setG("wtFmMix", x->getProperty("wtFmMix")); setG("bendRange", x->getProperty("bendRangeSemitones"));
            const auto sm = x->getProperty("sampleMix"); if (sm.isObject()) { setG("sampleMix", sm.getProperty("pos", 0)); globals_["sampleMixOn"] = (bool) sm.getProperty("on", false) ? 1.0 : 0.0; }
        }
        applyGlobals();
    }
    reload();
    if (onPresetLoaded) onPresetLoaded();
    return true;
}

} // namespace vv

namespace vv {
void Store::setGlobal(const juce::String& name, double v) {
    globals_[name] = v;
    if (engine_) { if (name == "mpe") vv_engine_set_mpe(engine_, v != 0 ? 1 : 0); else if (name == "midiChannel") vv_engine_set_midi_channel(engine_, (int) v); else if (name.startsWith("lim")) pushLimiter(); else if (name == "noSleep") juce::Desktop::getInstance().setScreenSaverEnabled(v == 0); else if (name == "bgAudio") {} else vv_engine_set_global(engine_, name.toRawUTF8(), v); }
    notify(); if (onEdited) onEdited();
}
void Store::pushLimiter() {   // master limiter globals -> engine (web: MASTER_LIM / masterLimiterPush)
    if (!engine_) return;
    vv_engine_set_master_limiter(engine_, global("limOn", 0) != 0 ? 1 : 0, global("limThr", -1), global("limCeil", -1), global("limRel", 100), global("limKnee", 0), global("limSoft", 0));
}
void Store::applyGlobals() {
    if (!engine_) return;
    pushLimiter();
    const std::pair<const char*, double> defs[] = { { "masterTone", 0 }, { "wtFmMix", 0.5 }, { "sampleMix", 0 }, { "sampleMixOn", 0 }, { "bendRange", 2 } };
    for (auto& d : defs) vv_engine_set_global(engine_, d.first, global(d.first, d.second));
    vv_engine_set_mpe(engine_, global("mpe", 0) != 0 ? 1 : 0);
    vv_engine_set_midi_channel(engine_, (int) global("midiChannel", 0));
    juce::Desktop::getInstance().setScreenSaverEnabled(global("noSleep", 0) == 0);
}
}

namespace vv {
void Store::setMany(const std::vector<std::pair<juce::String, juce::var>>& kv) {
    auto* o = new juce::DynamicObject(); bool any = false;
    for (auto& e : kv) {
        if (e.first.startsWithChar('@')) { setGlobal(e.first.substring(1), (double) e.second); continue; }
        if (assign(e.first, e.second)) { o->setProperty(juce::Identifier(e.first), e.second); any = true; }
    }
    if (any && engine_) vv_engine_set_values(engine_, juce::JSON::toString(juce::var(o), true).toRawUTF8());
    else delete o;
    if (any) { notify(); if (onEdited) onEdited(); }
}
}

namespace vv {
void Store::captureUndo(bool force) {
    if (!engine_ || replaying_) return;
    const auto now = juce::Time::getMillisecondCounter();
    if (!force && now - lastEditMs_ < 700 && !undo_.empty()) { lastEditMs_ = now; return; }
    lastEditMs_ = now; auto snap = exportJSON(); if (snap.isEmpty()) return;
    undo_.push_back(snap); if (undo_.size() > 60) undo_.erase(undo_.begin()); redo_.clear();
}
bool Store::replay(std::vector<juce::String>& from, std::vector<juce::String>& to) {
    if (from.empty() || !engine_) return false;
    auto target = from.back(); from.pop_back(); to.push_back(exportJSON());
    replaying_ = true; const bool ok = loadJSON(target); replaying_ = false; lastEditMs_ = 0; return ok;
}
bool Store::undo() { return replay(undo_, redo_); }
bool Store::redo() { return replay(redo_, undo_); }
}

namespace vv {
juce::String Store::generatePreset(const juce::String& category, const juce::File& userDir) {
    if (!engine_) return {};
    // GENERATE follows the keyboard scale when a scale is chosen and Scale lock is on (same globals the keyboard and sequencer use).
    int nsc = 0; const KbScale* tab = kbScales(nsc); const int idx = (int) global("scaleIdx", 0), root = (int) global("scaleRoot", 0);
    const bool scaleOn = global("scaleLock", 0) != 0 && idx > 0 && idx < nsc; uint32_t mask = 0;
    if (scaleOn) for (int i = 0; i < tab[idx].n; ++i) mask |= 1u << (((tab[idx].iv[i] % 12) + 12) % 12);
    char nm[96] = {}, vw[8] = {};
    captureUndo(true); replaying_ = true;
    const int ok = vv_engine_preset_generate(engine_, category.toRawUTF8(), 0, scaleOn ? 1 : 0, root, mask, scaleOn ? tab[idx].id : "", nm, (int) sizeof nm, vw, (int) sizeof vw);
    replaying_ = false;
    if (ok != 1) return {};
    reload(); if (onPresetLoaded) onPresetLoaded();
    userDir.createDirectory(); int n = 1; while (userDir.getChildFile(category + " Gen " + juce::String(n) + " - " + juce::String(nm) + ".json").existsAsFile() || userDir.getChildFile(category + " Gen " + juce::String(n) + ".json").existsAsFile()) ++n;
    const auto name = category + " Gen " + juce::String(n) + " - " + juce::String(nm);
    userDir.getChildFile(name + ".json").replaceWithText(exportPresetFile()); presetName_ = name; return name;
}
juce::String Store::modifyPreset(const juce::File& userDir) {
    if (!engine_) return {};
    captureUndo(true); replaying_ = true; const int ok = vv_engine_preset_action(engine_, "modify", "", 0); replaying_ = false;
    if (ok != 1) return {};
    reload(); if (onPresetLoaded) onPresetLoaded();
    const auto base = presetName_.isEmpty() ? juce::String("Preset") : presetName_.upToLastOccurrenceOf(" (mod)", false, false);
    userDir.createDirectory(); auto name = base + " (mod)"; int n = 2; while (userDir.getChildFile(name + ".json").existsAsFile()) name = base + " (mod " + juce::String(n++) + ")";
    userDir.getChildFile(name + ".json").replaceWithText(exportPresetFile()); presetName_ = name; return name;
}
}
