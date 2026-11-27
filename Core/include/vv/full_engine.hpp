// FullEngine: the complete native instrument = Synth (voices, FX chain, modulation matrix) + Performance (held notes, polyphony,
// arpeggiator, step sequencer). Control calls from the UI/main thread are staged under a mutex that the audio thread only try_locks,
// so the audio thread never blocks (if the lock is busy the staged work simply waits for the next block).
// Audio-thread entry points: render(), noteOn/noteOff/midi (when called from the render callback's MIDI handling).
#pragma once
#include <mutex>
#include <chrono>
#include <algorithm>
#include <functional>
#include <atomic>
#include <memory>
#include "synth.hpp"
#include "performance.hpp"
#include "state_migrate.hpp"
#include "assets.hpp"
#include "base64.hpp"
#include <map>
#include "presetgen.hpp"
#include "clips.hpp"

namespace vv {

class FullEngine {
public:
    FullEngine() { retired_.reserve(160); for (int i = 0; i < 128; i++) { ccVal_[i].store(0.f); ccDirty_[i].store(false); } }
    void feedInput(const float* l, const float* r, int n) { synth_.feedInput(l, r, n); }
    void prepare(double sr) { std::lock_guard<std::mutex> lk(m_); sr_ = sr; synth_.prepare(sr); perf_.attach(&synth_); synth_.tickSeconds = 0.1; ready_ = true; if (preset_) { synth_.setPreset(preset_); perf_.setPreset(preset_); } }
    bool ready() const { return ready_; }
    void setSoloMute(int s, int m) { synth_.setSoloMute(s, m); }
    void activeNoteMask(uint64_t o[2]) const { synth_.activeNoteMask(o); }
    float mixerLevel(int i) const { return synth_.mixerLevel(i); }
    float lfoPhaseSnap(int k) const { return synth_.lfoPhaseSnap(k); } float lfoRateHz(int k) const { return synth_.lfoRateHz(k); }
    void setScopeTapMask(int layer, uint32_t groups) { synth_.setScopeTapMask(layer, groups); }   // scope lanes: which oscillators (filter groups) of a layer are captured
    bool scopeTapRead(int layer, int group, float* out, int n) const { return synth_.scopeTapRead(layer, group, out, n); }
    float sampleHead(int slot) const { return synth_.sampleHead(slot); }
    std::vector<std::string> loadWarnings() { std::lock_guard<std::mutex> lk(m_); return loadWarnings_; }
    bool hasPreset() const { return hasPreset_.load(std::memory_order_acquire); }

    // ---- non-realtime loading (any non-audio thread): staged and applied on the next render()
    bool loadPresetJson(const std::string& text) {
        std::shared_ptr<Preset> p; Json j; state::Result mig;
        try { j = JsonParser::parse(text); mig = state::migrate(j); if (mig.ok) p = parsePreset(j); } catch (...) { mig.ok = false; mig.warnings.assign(1, "The preset could not be read (nothing was loaded)."); }
        { std::lock_guard<std::mutex> lk(m_); loadWarnings_ = mig.warnings; }   // the caller shows these; a refused load leaves the current sound untouched
        if (!mig.ok || !p) return false;
        synth_.loadLuaFromPreset(*p);   // LUA slot scripts compile here, on this non-audio thread
        std::lock_guard<std::mutex> lk(m_); working_ = std::move(j); registerEmbedded_(*p); pendingPreset_ = p; pendingUpdate_.reset();
        const Json* cj = jsonPathPtr(working_, "extra.clips", false); if (!(cj && clipModel_.load(*cj) >= 0)) clipModel_.reset();   // no / unusable clip data: factory default set (a damaged clip is skipped, never a crash)
        clipPublish_(); return true;
    }
    // Live parameter edit by dot path ("layers.WT.oscs.0.level"), value as JSON text ("0.5", "true", "\"sawBright\"", "[1,2]").
    // The edited copy of the preset is staged and swapped in without resetting the sound (see Synth::updatePreset).
    bool setValue(const std::string& path, const std::string& valueJson) {
        Json v; try { v = JsonParser::parse(valueJson); } catch (...) { return false; }
        std::lock_guard<std::mutex> lk(m_);
        if (working_.type != Json::Obj) return false;
        Json trial = working_; Json* t = jsonPathPtr(trial, path, true); if (!t) return false; *t = std::move(v);
        std::shared_ptr<Preset> p; try { p = parsePreset(trial); } catch (...) { return false; }
        if (!p) return false;
        working_ = std::move(trial); trimRetired_();   // freed here (main thread), never on the audio thread
        luaTouched_(path, *p);
        registerEmbedded_(*p); if (pendingPreset_) pendingPreset_ = p; else pendingUpdate_ = p; return true;
    }
    // Batch edit: all paths applied to one copy and parsed once (used by the host-parameter pump).
    bool setValues(const std::vector<std::pair<std::string, std::string>>& kv) {
        if (kv.empty()) return true;
        std::lock_guard<std::mutex> lk(m_);
        if (working_.type != Json::Obj) return false;
        Json trial = working_;
        for (auto& e : kv) { Json v; try { v = JsonParser::parse(e.second); } catch (...) { continue; } Json* t = jsonPathPtr(trial, e.first, true); if (t) *t = std::move(v); }
        std::shared_ptr<Preset> p; try { p = parsePreset(trial); } catch (...) { return false; }
        if (!p) return false;
        for (auto& e : kv) luaTouched_(e.first, *p);
        working_ = std::move(trial); trimRetired_();
        registerEmbedded_(*p); if (pendingPreset_) pendingPreset_ = p; else pendingUpdate_ = p; return true;
    }
    // LUA layer: an edit under layers.LUA.oscs.N.lua.* reloads that slot's script (src / seed) or updates a live script parameter (params.<id>); called with m_ held, never from the audio thread.
    void luaTouched_(const std::string& path, const Preset& p) {
        if (path.compare(0, 10, "layers.LUA") != 0) return; const size_t k = path.find("oscs."); if (k == std::string::npos) return;
        const int slot = std::atoi(path.c_str() + k + 5); if (slot < 0 || slot >= LuaHost::kSlots || slot >= (int)p.layers[L_LUA].oscs.size()) return;
        const OscCfg& o = p.layers[L_LUA].oscs[slot];
        if (path.find(".lua.src") != std::string::npos || path.find(".lua.seed") != std::string::npos) { if (o.luaSrc.empty()) synth_.luaHost().clear(slot); else synth_.luaHost().load(slot, o.luaSrc, o.luaSeed, o.luaParams); return; }
        const size_t q = path.find(".lua.params."); if (q != std::string::npos) { const int idx = synth_.luaHost().paramIndex(slot, path.c_str() + q + 12); if (idx >= 0) for (const auto& kv : o.luaParams) if (kv.first == path.substr(q + 12)) synth_.luaHost().setParam(slot, idx, (float)kv.second); }
    }
    // ---- preset generator: "generate" (any category in the recipe set: Arp Bass Key Lead Pad Drum FX XFX) and "modify" (~10 % variation of the loaded sound). seed 0 = fresh random.
    bool presetAction(const std::string& action, const std::string& category, uint32_t seed) {
        if (action == "generate") return presetGenerate(category, seed, false, 0, 0, nullptr, nullptr);
        std::lock_guard<std::mutex> lk(m_);
        if (working_.type != Json::Obj) return false;
        if (seed == 0) seed = (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count() | 1u;
        presetgen::Rng rng(seed); Json trial = working_; bool ok = false;
        if (action == "modify") ok = presetgen::modify(trial, rng);
        if (!ok) return false;
        std::shared_ptr<Preset> p; try { p = parsePreset(trial); } catch (...) { return false; }
        if (!p) return false;
        working_ = std::move(trial); trimRetired_();
        registerEmbedded_(*p); if (pendingPreset_) pendingPreset_ = p; else pendingUpdate_ = p; return true;
    }
    // GENERATE with the keyboard's scale: scaleOn = Scale lock is on and a scale is chosen; scaleMask has bit i set when interval i (0-11 above the root) is in the scale.
    // The whole sound is replaced (like the web app's loadPresetData of a generated preset); settings the generator does not own are kept.
    bool presetGenerate(const std::string& category, uint32_t seed, bool scaleOn, int scaleRoot, uint32_t scaleMask, std::string* outName, std::string* outView, const char* scaleLabel = "") {
        std::lock_guard<std::mutex> lk(m_);
        if (working_.type != Json::Obj) return false;
        if (seed == 0) { static uint32_t ctr = 0x2468ACEu; ctr = ctr * 1664525u + 1013904223u; seed = (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count() ^ ctr; if (seed == 0) seed = 1; }
        presetgen::ScaleCtx sc; sc.on = scaleOn && scaleMask != 0; sc.root = ((scaleRoot % 12) + 12) % 12; sc.label = scaleLabel ? scaleLabel : "";
        if (sc.on) for (int i = 0; i < 12; i++) if (scaleMask & (1u << i)) sc.ints.push_back(i);
        Json gen; presetgen::GenInfo info;
        if (!presetgen::build(gen, category, seed, sc, &info)) return false;
        Json trial = working_;
        for (auto& kv : gen.o) { Json* t = jsonPathPtr(trial, kv.first, true); if (t) *t = std::move(kv.second); }
        for (const char* k : { "xmod", "chordMaker" }) { auto& o = trial.o; for (size_t i = 0; i < o.size(); i++) if (o[i].first == k) { o.erase(o.begin() + (long)i); break; } }
        if (!gen.has("extra")) { auto& o = trial.o; for (size_t i = 0; i < o.size(); i++) if (o[i].first == "extra") { o.erase(o.begin() + (long)i); break; } }
        std::shared_ptr<Preset> p; try { p = parsePreset(trial); } catch (...) { return false; }
        if (!p) return false;
        working_ = std::move(trial); trimRetired_();
        registerEmbedded_(*p); if (pendingPreset_) pendingPreset_ = p; else pendingUpdate_ = p;
        if (outName) *outName = info.name; if (outView) *outView = info.view; return true;
    }
    // ---- chord maker: Generate / Mutate / Re-voice / Rotate / Reverse / Fit length / Lock / Re-roll on the preset's chordMaker block.
    // actions: "generate" "mutate" "revoice" "rotl" "rotr" "reverse" "fit" "lock" (index) "reroll" (index). seed 0 = pick a fresh random seed.
    bool chordAction(const std::string& action, int index, uint32_t seed) {
        std::lock_guard<std::mutex> lk(m_);
        if (working_.type != Json::Obj) return false;
        Json trial = working_; Json* node = jsonPathPtr(trial, "chordMaker", true); if (!node) return false;
        if (node->type != Json::Obj) { *node = Json(); node->type = Json::Obj; }
        cm::Cfg c = modelio::chordCfg(node);
        if (seed == 0) { static uint32_t ctr = 0x1234567u; ctr = ctr * 1664525u + 1013904223u; seed = (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count() ^ ctr ^ 0x9E3779B9u; if (seed == 0) seed = 1; }
        cm::Rng rnd(seed); auto remember = [&] { c.history.push_back(cm::signature(c.prog)); while (c.history.size() > 16) c.history.erase(c.history.begin()); };
        if (action == "generate" || c.prog.empty()) { c.seed = seed; c.prog = cm::generate(c, rnd, c.history); remember(); }
        else if (action == "mutate") { cm::mutate(c.prog, c, rnd, 0.35); remember(); }
        else if (action == "revoice") cm::revoice(c.prog, c, rnd);
        else if (action == "rotl") { if (c.prog.size() > 1) std::rotate(c.prog.begin(), c.prog.begin() + 1, c.prog.end()); }
        else if (action == "rotr") { if (c.prog.size() > 1) std::rotate(c.prog.rbegin(), c.prog.rbegin() + 1, c.prog.rend()); }
        else if (action == "reverse") std::reverse(c.prog.begin(), c.prog.end());
        else if (action == "fit") {
            const size_t count = (size_t)cm::chordCount(c); while (c.prog.size() > count) c.prog.pop_back();
            while (c.prog.size() < count) { cm::Chord ch; ch.tones = {0, 2, 4}; c.prog.push_back(ch); cm::rerollChord(c.prog, c.prog.size() - 1, c, rnd); }
        }
        else if (action == "lock") { if (index >= 0 && index < (int)c.prog.size()) c.prog[(size_t)index].locked = !c.prog[(size_t)index].locked; }
        else if (action == "reroll") cm::rerollChord(c.prog, (size_t)index, c, rnd);
        else return false;
        *node = modelio::chordToJson(c, node);
        std::shared_ptr<Preset> p; try { p = parsePreset(trial); } catch (...) { return false; }
        if (!p) return false;
        working_ = std::move(trial); trimRetired_();
        registerEmbedded_(*p); if (pendingPreset_) pendingPreset_ = p; else pendingUpdate_ = p; return true;
    }
    // ---- XFX MODE (a different sound on every key): edits extra.xfx of the working preset (same JSON the web app saves, so presets travel both ways).
    // action: enable {on} | bank {index} | rename {name} | setOverride {note,path,value} | clearOverride {note,path} | layer {note,layer,state(true|false|null)}
    //         clearKey {note} | copyKey {from,to} | clearBank {} | reset {}.   path = "<LAYER>.<param path>", e.g. "WT.env.rep" or "WT.oscs.0.filter.cutoff".
    bool xfxAction(const std::string& action, const std::string& argsJson) {
        Json a; try { a = JsonParser::parse(argsJson.empty() ? "{}" : argsJson); } catch (...) { return false; }
        std::lock_guard<std::mutex> lk(m_);
        if (working_.type != Json::Obj) return false;
        Json trial = working_;
        auto obj = [](Json& j) -> Json& { if (j.type != Json::Obj) { j = Json(); j.type = Json::Obj; } return j; };
        auto child = [&](Json& j, const char* k) -> Json& { obj(j); for (auto& kv : j.o) if (kv.first == k) return kv.second; j.o.emplace_back(k, Json()); return j.o.back().second; };
        auto num = [](double v) { Json j; j.type = Json::Num; j.n = v; return j; };
        auto boo = [](bool v) { Json j; j.type = Json::Bool; j.b = v; return j; };
        auto str = [](const std::string& v) { Json j; j.type = Json::Str; j.s = v; return j; };
        Json& ex = child(trial, "extra"); Json& x = child(ex, "xfx");
        if (action == "reset") { Json* e = jsonPathPtr(trial, "extra", false); if (e) { for (size_t i = 0; i < e->o.size(); i++) if (e->o[i].first == "xfx") { e->o.erase(e->o.begin() + (long)i); break; } } }
        else {
            if (x.type != Json::Obj || !x.get("banks") || x.get("banks")->a.size() != 8) {   // create the 8 empty banks
                Json nx; nx.type = Json::Obj; nx.o.emplace_back("v", num(1)); nx.o.emplace_back("enabled", boo(true)); nx.o.emplace_back("active", num(0));
                Json bs; bs.type = Json::Arr; for (int i = 0; i < 8; i++) { Json b; b.type = Json::Obj; b.o.emplace_back("name", str("Bank " + std::to_string(i + 1))); Json k; k.type = Json::Obj; b.o.emplace_back("keys", k); bs.a.push_back(b); }
                nx.o.emplace_back("banks", bs); x = nx;
            }
            Json& banks = child(x, "banks"); const int act = std::max(0, std::min(7, (int)x.num("active", 0))); Json& bank = banks.a[(size_t)act]; Json& keys = child(bank, "keys");
            const int note = (int)a.num("note", -1); const std::string nk = std::to_string(note);
            auto rec = [&](int n) -> Json& { Json& r = child(keys, std::to_string(n).c_str()); obj(r); child(r, "en"); child(r, "o"); return r; };
            auto prune = [&](int n) { for (size_t i = 0; i < keys.o.size(); i++) if (keys.o[i].first == std::to_string(n)) { Json& r = keys.o[i].second; const Json* en = r.get("en"); const Json* o = r.get("o");
                if ((!en || en->o.empty()) && (!o || o->o.empty())) keys.o.erase(keys.o.begin() + (long)i); return; } };
            auto setKey = [](Json& j, const std::string& k, const Json& v) { if (j.type != Json::Obj) { j = Json(); j.type = Json::Obj; } for (auto& kv : j.o) if (kv.first == k) { kv.second = v; return; } j.o.emplace_back(k, v); };
            auto delKey = [](Json& j, const std::string& k) { for (size_t i = 0; i < j.o.size(); i++) if (j.o[i].first == k) { j.o.erase(j.o.begin() + (long)i); return; } };
            if (action == "enable") child(x, "enabled") = boo(a.boolean("on", true));
            else if (action == "hold") child(x, "hold") = boo(a.boolean("on", false));   // HOLD XFX footer button
            else if (action == "bank") child(x, "active") = num(std::max(0, std::min(7, (int)a.num("index", 0))));
            else if (action == "rename") bank.o.empty() ? void() : setKey(bank, "name", str(a.str("name", "Bank")));
            else if (action == "clearBank") keys = [] { Json k; k.type = Json::Obj; return k; }();
            else if (note < 0 || note > 127) { if (action != "copyKey") return false; }
            if (action == "setOverride") { const Json* v = a.get("value"); if (!v || a.str("path").empty()) return false; setKey(child(rec(note), "o"), a.str("path"), *v); }
            else if (action == "clearOverride") { delKey(child(rec(note), "o"), a.str("path")); prune(note); }
            else if (action == "layer") { const Json* st = a.get("state"); Json& en = child(rec(note), "en"); const std::string L = a.str("layer"); if (!st || st->isNull()) delKey(en, L); else setKey(en, L, boo(st->type == Json::Bool ? st->b : st->asNum() != 0)); prune(note); }
            else if (action == "clearKey") delKey(keys, nk);
            else if (action == "copyKey") { const int f = (int)a.num("from", -1), t = (int)a.num("to", -1); if (f < 0 || f > 127 || t < 0 || t > 127) return false; const Json* src = keys.get(std::to_string(f).c_str()); if (src) { Json cp = *src; setKey(keys, std::to_string(t), cp); } else delKey(keys, std::to_string(t)); }
            else if (action != "enable" && action != "hold" && action != "bank" && action != "rename" && action != "clearBank") return false;
        }
        std::shared_ptr<Preset> p; try { p = parsePreset(trial); } catch (...) { return false; }
        if (!p) return false;
        working_ = std::move(trial); trimRetired_();
        registerEmbedded_(*p); if (pendingPreset_) pendingPreset_ = p; else pendingUpdate_ = p; return true;
    }
    std::string xfxExport() { std::lock_guard<std::mutex> lk(m_); Json* t = jsonPathPtr(working_, "extra.xfx", false); return t ? jsonDump(*t) : std::string("null"); }
    // JSON array describing the current progression for the UI: [{"name":"Cmaj7","deg":1,"n":4,"rest":false,"locked":false}, ...]
    std::string chordInfo() {
        std::lock_guard<std::mutex> lk(m_); Json* node = jsonPathPtr(working_, "chordMaker", false); if (!node) return "[]";
        const cm::Cfg c = modelio::chordCfg(node); Json arr; arr.type = Json::Arr;
        for (auto& ch : c.prog) { Json o; o.type = Json::Obj; Json nm; nm.type = Json::Str; nm.s = cm::chordName(ch, c); o.o.push_back({"name", nm});
            Json d; d.type = Json::Num; d.n = ch.deg + 1; o.o.push_back({"deg", d}); Json n; n.type = Json::Num; n.n = (double)ch.tones.size(); o.o.push_back({"n", n});
            Json r; r.type = Json::Bool; r.b = ch.rest; o.o.push_back({"rest", r}); Json l; l.type = Json::Bool; l.b = ch.locked; o.o.push_back({"locked", l}); arr.a.push_back(o); }
        return jsonDump(arr);
    }
    int chordPlaying() const { return perf_.chordPlaying(); }
    // RANDOMISE fit: reads the arp's published note snapshot (lock-free); false = the arp is not playing.
    // piano-roll RECORD (see roll_rec.hpp): arm / disarm from any thread; drain from the UI thread only (single consumer)
    void rollRecArm(bool on) { perf_.recArm(on); }
    bool rollRecArmed() const { return perf_.recArmed(); }
    bool rollRecPop(rollrec::Ev& e) { return perf_.recRing().pop(e); }
    bool arpFitPitches(int count, const uint8_t* strong, int kbMask, uint32_t seed, int lo, int hi, int* out, harmony::Palette* pal) const { return perf_.arpFitPitches(count, strong, kbMask, seed, lo, hi, out, pal); }
    // Direct read access for the parameter bridge (main thread).
    template <class F> auto withWorking(F f) { std::lock_guard<std::mutex> lk(m_); return f(working_); }
    std::string getValue(const std::string& path) { std::lock_guard<std::mutex> lk(m_); Json* t = jsonPathPtr(working_, path, false); return t ? jsonDump(*t) : std::string(); }
    std::string exportPreset() { std::lock_guard<std::mutex> lk(m_); return jsonDump(working_); }
    void addWavetable(const std::string& name, std::shared_ptr<const WtTable> t) { std::lock_guard<std::mutex> lk(m_); work_.push_back([=] { synth_.setWavetable(name, t); }); }
    void addSample(const std::string& name, std::shared_ptr<const SampleBuf> b) { std::lock_guard<std::mutex> lk(m_); work_.push_back([=] { synth_.setSample(name, b); }); }
    // Loads every .vvwt in a folder (wavetable name = file stem). Returns the number loaded.
    int loadWavetableFolder(const std::string& dir, const std::vector<std::string>& names) {
        int n = 0; for (auto& nm : names) { auto t = loadWavetableFile(dir + "/" + nm + ".vvwt"); if (t) { addWavetable(nm, t); n++; } } return n;
    }

    // ---- control (any thread; applied at the start of the next render())
    void noteOn(int note, float vel) { push(C_NoteOn, note, 0, vel); }
    void noteOff(int note) { push(C_NoteOff, note); }
    void allNotesOff() { push(C_Panic); }
    void setMacro(int i, double v) { push(C_Macro, i, 0, v); }
    void setXy(int i, double v) { push(C_Xy, i, 0, v); }   // XY pad position as a modulation source (index = pad * 2 + axis)
    void setModWheel(double v) { push(C_ModWheel, 0, 0, v); }
    void setPitchBend(double v) { push(C_Bend, 0, 0, v); }
    void setBendRange(double st) { push(C_BendRange, 0, 0, st); }
    void setMasterVolume(double v) { push(C_MasterVol, 0, 0, v); }
    // Global (non-preset) settings by name: masterVolume, masterTone, wtFmMix, sampleMix (0..15), sampleMixOn (0/1), bendRange (semitones)
    bool setGlobal(const std::string& name, double v) {
        if (name == "masterVolume") setMasterVolume(v);
        else if (name == "masterTone") push(C_MasterTone, 0, 0, v);
        else if (name == "wtFmMix") push(C_WtFm, 0, 0, v);
        else if (name == "sampleMix") push(C_SampleMixPos, 0, 0, v);
        else if (name == "sampleMixOn") push(C_SampleMixOn, 0, 0, v);
        else if (name == "bendRange") setBendRange(v);
        else return false;
        return true;
    }
    void setBpm(double bpm) { push(C_Bpm, 0, 0, bpm); }
    void setArpOn(bool on) { push(C_ArpOn, on); }
    void setLatch(bool on) { push(C_Latch, on); }
    void setTransport(bool playing) { push(C_Transport, playing); }
    int smpRrLast(int slot) const { return synth_.rrLastTake(slot); }
    void smpRrReset() { push(C_RrReset); }
    // ---- clip launcher (UI thread). The model lives here; every edit is mirrored into working_["extra"]["clips"] (so exportPreset / AUv3 state / presets carry it)
    // and published to the audio thread as an immutable snapshot through the same staged-work queue the preset swap uses.
    std::string clipsJson() { std::lock_guard<std::mutex> lk(m_); return clipModel_.json(); }
    void clipLaunch(int slot) { push(C_ClipLaunch, slot); }
    void clipStop(int slot, bool immediate) { push(C_ClipStop, slot, immediate); }
    void clipStopAll() { push(C_ClipStopAll); }
    uint64_t clipPlayingMask() const { return perf_.clipPlayerConst().playingMaskUI(); }
    uint64_t clipQueuedStartMask() const { return perf_.clipPlayerConst().queuedStartMaskUI(); }
    uint64_t clipQueuedStopMask() const { return perf_.clipPlayerConst().queuedStopMaskUI(); }
    int clipRecordingSlot() const { return perf_.clipPlayerConst().recordingSlotUI(); }
    float clipPhase(int slot) const { return perf_.clipPlayerConst().phaseUI(slot); }
    // Pull notes the audio thread recorded and merge them into the clip(s). Call from a UI timer (~10 Hz) while recording and once after it stops. Returns notes merged.
    int clipDrainRecording() {
        clips::RecNote buf[256]; int total = 0;
        for (int guard = 0; guard < 16; guard++) { const int n = perf_.clipPlayerMut().drainRec(buf, 256); if (n <= 0) break; std::lock_guard<std::mutex> lk(m_); clipModel_.mergeRec(buf, n); total += n; if (n < 256) break; }
        if (total) { std::lock_guard<std::mutex> lk(m_); clipPublish_(); }
        return total;
    }
    // Generic edit: op name + JSON args. Returns 1 on success, 0 when the op did nothing / was refused. See Bridge/vv_engine.h for the op list.
    int clipOp(const std::string& op, const std::string& argsJson, std::string* out = nullptr) {
        Json a; try { a = argsJson.empty() ? Json() : JsonParser::parse(argsJson); } catch (...) { return 0; }
        const int slot = (int)a.num("slot", -1); bool ok = false, publish = true; std::lock_guard<std::mutex> lk(m_);
        if (op == "new") ok = clipModel_.newClip(slot, (int)a.num("layer", 0), (int)a.num("bars", 4)) > 0;
        else if (op == "duplicate") ok = clipModel_.duplicate(slot) >= 0;
        else if (op == "delete") ok = clipModel_.remove(slot);
        else if (op == "move") ok = clipModel_.move((int)a.num("from", -1), (int)a.num("to", -1), (int)a.num("mode", 0));
        else if (op == "paste") ok = clipModel_.paste(slot, a.str("json"));
        else if (op == "export") { const std::string t = clipModel_.exportClip(slot); if (out) *out = t; ok = !t.empty(); publish = false; }
        else if (op == "rename") ok = clipModel_.setName(slot, a.str("name"));
        else if (op == "bankName") ok = clipModel_.setBankName((int)a.num("bank", -1), a.str("name"));
        else if (op == "bars") ok = clipModel_.setBars(slot, (int)a.num("bars", 4));
        else if (op == "layer") ok = clipModel_.setLayer(slot, (int)a.num("layer", 0));
        else if (op == "color") ok = clipModel_.setColor(slot, a.str("color"));
        else if (op == "transpose") ok = clipModel_.transpose(slot, (int)a.num("delta", 0));
        else if (op == "setTranspose") ok = clipModel_.setTranspose(slot, (int)a.num("value", 0));
        else if (op == "quantiseNotes") ok = clipModel_.quantiseNotes(slot);
        else if (op == "clearAuto") ok = clipModel_.clearAuto(slot);
        else if (op == "quant") { clipModel_.setQuant((int)a.num("q", 5)); ok = true; }
        else if (op == "mode") { clipModel_.setMode((int)a.num("m", 0)); ok = true; }
        else if (op == "select") { clipModel_.select(slot); ok = true; }
        else if (op == "bank") { clipModel_.setBank((int)a.num("bank", 0)); ok = true; }
        else if (op == "factory") { clipModel_.restoreFactory((int)a.num("bank", 0)); ok = true; }
        else if (op == "undo") ok = clipModel_.undo();
        else if (op == "occupied") { ok = clipModel_.occupied(slot); publish = false; }
        else if (op == "fromRoll") ok = clipFromRoll_(slot, (int)a.num("layer", 0));
        else if (op == "toRoll") { ok = clipToRoll_(slot); publish = false; }
        else if (op == "recordBegin") { if (clipModel_.occupied(slot)) { clipModel_.beginRecord(slot); push(C_ClipArm, slot); ok = true; } }
        else if (op == "recordEnd") { push(C_ClipArm, -1); ok = true; publish = false; }
        else if (op == "autoWrite") { clipModel_.recordAuto(slot, a.str("key"), a.num("phase", 0), a.num("value", 0.5)); ok = true; publish = false; clipAutoDirty_ = true; }
        else if (op == "autoEnd") { clipModel_.endAuto(slot, a.str("key")); ok = true; publish = clipAutoDirty_; clipAutoDirty_ = false; }
        else if (op == "load") { ok = clipModel_.loadText(a.str("json")) >= 0; }
        if (ok && publish) clipPublish_();
        return ok ? 1 : 0;
    }
    // AUv3 / plug-in host: the host owns play state, tempo and playhead (piano roll, automation lanes, arp and sequencers follow it). Call once per render block from the render thread;
    // valid = false hands control back to the standalone transport.
    void setHostTransport(bool valid, bool playing, double beat, double bpm) {
        push(C_HostTransport, valid, 0, beat); if (valid && bpm > 0) push(C_Bpm, 0, 0, bpm);
        if (valid && playing != hostPlaying_) { hostPlaying_ = playing; push(C_Transport, playing); } else if (!valid) hostPlaying_ = false;
    }
    void setPolyphony(int n, bool legato) { push(C_Poly, n, legato); }
    void setSwing(bool on, double amount) { push(C_Swing, on, 0, amount); }
    void releaseLayerHold(int layer) { push(C_ReleaseHold, layer); }
    // MPC pads (web: openMpcOverlay). slot >= 0 plays that sampler slot once at its original pitch (zones ignored); slot < 0 plays the layer at `pitchNote`.
    // `pad` (0..15) only makes the voice identity unique so several pads can sound together.
    void padOn(int layer, int slot, int pad, int pitchNote, float vel) { push(C_PadOn, layer * 256 + (slot + 1), pad * 256 + pitchNote, vel); }
    void padOff(int layer, int pad) { push(C_PadOff, layer, pad); }
    void padSliceOn(int slot, int slice, int pad, float vel) { push(C_PadSlice, slot, slice + 256 * pad, vel); }   // MPC pad plays transient `slice` (1-based, 0 = whole sample) of sampler slot `slot`; release with padOff(L_SMP, pad)
    void auditionSlice(int slot, int slice, float vel) { push(C_PadSlice, slot, slice, vel); }
    void auditionOff() { push(C_PadOff, L_SMP, 16); }
    void setMpe(bool on) { push(C_Mpe, on); }
    void mpeGlide(int note, double semis) { push(C_MpeGlide, note, 0, semis); }
    // App MIDI channel: 0 = Omni, 1..16 = only that channel's messages are used (ignored while MPE is on). Atomic so the UI thread can set it.
    // Master limiter + loudness meter (lock-free; web: Settings "Master output - limiter & loudness")
    void setMasterLimiter(const MasterOut::Params& p) { synth_.masterOut().setParams(p); }
    const MasterOut& masterMeter() const { return const_cast<FullEngine*>(this)->synth_.masterOut(); }
    void resetLoudness() { synth_.masterOut().resetIntegrated(); }
    void setMidiChannel(int ch) { midiChannel_.store(ch < 0 || ch > 16 ? 0 : ch, std::memory_order_relaxed); }
    int midiChannel() const { return midiChannel_.load(std::memory_order_relaxed); }
    // MPE polyphony: up to kMpeMaxPoly (10) simultaneous per-channel notes, each with its own bend / timbre / pressure / glide. An 11th note steals the oldest.
    static constexpr int kMpeMaxPoly = 10;
    int mpeNoteOfCh_[16] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}; uint32_t mpeAgeOfCh_[16] = {}; uint32_t mpeTick_ = 0;
    void mpeVoiceOn(int ch, int note) {
        if (!synth_.mpeEnabled || ch < 0 || ch > 15) return;
        int n = 0, oldest = -1; for (int i = 0; i < 16; i++) if (mpeNoteOfCh_[i] >= 0 && i != ch) { n++; if (oldest < 0 || mpeAgeOfCh_[i] < mpeAgeOfCh_[oldest]) oldest = i; }
        if (n >= kMpeMaxPoly && oldest >= 0) { const int on = mpeNoteOfCh_[oldest]; synth_.mpeNoteOff(oldest, on); perf_.noteUp(on); mpeNoteOfCh_[oldest] = -1; }
        mpeNoteOfCh_[ch] = note; mpeAgeOfCh_[ch] = ++mpeTick_;
    }
    void mpeVoiceOff(int ch) { if (ch >= 0 && ch < 16) mpeNoteOfCh_[ch] = -1; }
    int mpeActiveCount() const { int n = 0; for (int i = 0; i < 16; i++) if (mpeNoteOfCh_[i] >= 0) n++; return n; }
    // Raw MIDI from the host. Channel matters only in MPE mode (channels 2..16 carry per-note expression).
    void midi(const uint8_t* b, int len) {
        if (len < 2) return; const int st = b[0] & 0xF0, ch = b[0] & 15, d1 = b[1], d2 = len > 2 ? b[2] : 0;
        { const int sel = midiChannel_.load(std::memory_order_relaxed); if (sel > 0 && st >= 0x80 && st <= 0xE0 && !synth_.mpeEnabled && ch != sel - 1) return; }   // web: handleMidiMessage channel filter
        if (st == 0x90 && d2 > 0) { const float v = d2 / 127.f; push(C_MpeNoteOn, ch, d1, v); }
        else if (st == 0x80 || st == 0x90) push(C_MpeNoteOff, ch, d1);
        else if (st == 0xE0) { const double v = (((d2 << 7) | d1) - 8192) / 8192.0; push(C_MpeCtl, ch, 0, v); }
        else if (st == 0xD0) { const double v = d1 / 127.0; push(C_MpeCtl, ch, 2, v); }
        else if (st == 0xB0) {
            const double v = d2 / 127.0;
            if (d1 == 74 && ch != 0) { push(C_MpeCtl, ch, 1, v); return; }   // MPE timbre (per-note channels); CC74 on channel 1 is the Wavetable filter cutoff, like the web
            if (d1 == 123 || d1 == 120) { allNotesOff(); return; }
            ccAny_.store(d1, std::memory_order_relaxed);
            if (d1 == 1) setModWheel(v);
            else if (d1 == 7) setMasterVolume(v);
            else if (d1 >= 102 && d1 <= 105) setMacro(d1 - 102, v);
            else if (d1 == 87) push(C_MasterTone, 0, 0, v);
            else if (d1 == 88) push(C_WtFm, 0, 0, v);
            ccVal_[d1 & 127].store((float)v, std::memory_order_relaxed); ccDirty_[d1 & 127].store(true, std::memory_order_release);   // everything else is applied by pollCc() (not real-time safe: it edits the preset)
        }
    }
    // ---- MIDI CC to parameter (web CC_MAP + MIDI LEARN). midi() only records the value; call pollCc() from a normal (non-audio) thread, ~30 Hz.
    // Returns the number of CCs handled. A learned mapping wins over the built-in map, like the web.
    int pollCc() {
        bool any = false; for (int i = 0; i < 128; i++) if (ccDirty_[i].load(std::memory_order_acquire)) { any = true; break; }
        if (!any) return 0;
        int n = 0; std::vector<std::pair<std::string, std::string>> kv;
        auto num = [](double v) { char b[40]; snprintf(b, sizeof b, "%.6g", v); return std::string(b); };
        std::vector<std::pair<int, double>> fxMix;   // (fx type id 0 reverb 1 delay 2 saturation, value)
        {
            std::lock_guard<std::mutex> lk(m_);
            for (int cc = 0; cc < 128; cc++) {
                if (!ccDirty_[cc].exchange(false, std::memory_order_acq_rel)) continue;
                const double v = ccVal_[cc].load(std::memory_order_relaxed); n++;
                if (learnArmed_ && cc != 1 && cc != 7) { ccLearn_[cc] = CcLearn{ learnPath_, learnLo_, learnHi_, true }; learnArmed_ = false; learnedCc_ = cc; continue; }
                if (ccLearn_[cc].used) { kv.push_back({ ccLearn_[cc].path, num(ccLearn_[cc].lo + v * (ccLearn_[cc].hi - ccLearn_[cc].lo)) }); continue; }
                const char* L = nullptr;
                switch (cc) {
                    case 74: kv.push_back({ "layers.WT.filter.cutoff", num(20 + v * 17980) }); break;
                    case 71: kv.push_back({ "layers.WT.filter.reso", num(v * 130) }); break;
                    case 20: kv.push_back({ "layers.FM.filter.cutoff", num(20 + v * 17980) }); break;
                    case 21: kv.push_back({ "layers.FM.filter.reso", num(v * 130) }); break;
                    case 22: kv.push_back({ "layers.SMP.filter.cutoff", num(20 + v * 17980) }); break;
                    case 23: kv.push_back({ "layers.SMP.filter.reso", num(v * 130) }); break;
                    case 24: kv.push_back({ "layers.WT.filter.keytrack", num(v * 200) }); break;
                    case 25: kv.push_back({ "layers.FM.filter.keytrack", num(v * 200) }); break;
                    case 26: kv.push_back({ "layers.SMP.filter.keytrack", num(v * 200) }); break;
                    case 73: kv.push_back({ "layers.WT.env.a", num(0.001 + v * 3) }); break;
                    case 75: kv.push_back({ "layers.WT.env.d", num(0.001 + v * 3) }); break;
                    case 79: kv.push_back({ "layers.WT.env.s", num(v) }); break;
                    case 72: kv.push_back({ "layers.WT.env.r", num(0.001 + v * 3) }); break;
                    case 28: kv.push_back({ "layers.FM.env.a", num(0.001 + v * 3) }); break;
                    case 29: kv.push_back({ "layers.FM.env.d", num(0.001 + v * 3) }); break;
                    case 30: kv.push_back({ "layers.FM.env.s", num(v) }); break;
                    case 31: kv.push_back({ "layers.FM.env.r", num(0.001 + v * 3) }); break;
                    case 33: kv.push_back({ "layers.SMP.env.a", num(0.001 + v * 3) }); break;
                    case 34: kv.push_back({ "layers.SMP.env.d", num(0.001 + v * 3) }); break;
                    case 35: kv.push_back({ "layers.SMP.env.s", num(v) }); break;
                    case 36: kv.push_back({ "layers.SMP.env.r", num(0.001 + v * 3) }); break;
                    case 115: kv.push_back({ "layers.PRO.filter.cutoff", num(20 + v * 17980) }); break;
                    case 116: kv.push_back({ "layers.PRO.filter.reso", num(v * 130) }); break;
                    case 117: kv.push_back({ "layers.PRO.filter.keytrack", num(v * 200) }); break;
                    case 118: kv.push_back({ "layers.PRO.env.a", num(0.001 + v * 3) }); break;
                    case 119: kv.push_back({ "layers.PRO.env.d", num(0.001 + v * 3) }); break;
                    case 85: kv.push_back({ "layers.PRO.env.s", num(v) }); break;
                    case 86: kv.push_back({ "layers.PRO.env.r", num(0.001 + v * 3) }); break;
                    case 87: case 88: break;   // master tone / WT-FM mix are applied in midi() on the audio thread
                    case 113: L = "lfo5"; break; case 114: L = "lfo6"; break;
                    case 106: L = "lfo1"; break; case 107: L = "lfo2"; break; case 108: L = "lfo3"; break; case 112: L = "lfo4"; break;
                    case 109: fxMix.push_back({ 0, v }); break; case 110: fxMix.push_back({ 1, v }); break; case 111: fxMix.push_back({ 2, v }); break;
                    default: n--; break;
                }
                if (L) kv.push_back({ std::string("lfo.") + L + ".hz", num(0.1 + v * 19.9) });
            }
            if (!fxMix.empty() && working_.type == Json::Obj) {   // every stage of that effect type gets the new mix
                static const char* names[3] = { "reverb", "delay", "saturation" };
                Json* st = jsonPathPtr(working_, "fxStages", false);
                if (st && st->type == Json::Arr) for (size_t i = 0; i < st->a.size(); i++) { const Json* t = jsonPathPtr(st->a[i], "type", false);
                    if (!t || t->type != Json::Str) continue;
                    for (auto& m : fxMix) if (t->s == names[m.first]) kv.push_back({ "fxStages." + std::to_string(i) + ".params.mix", num(m.second) }); }
            }
        }
        if (!kv.empty()) setValues(kv);
        return n;
    }
    // MIDI LEARN: the next CC (other than 1 / 7 / 123) maps to `path`, scaled lo..hi. Returns immediately; learnedCc() reports the result.
    void armCcLearn(const std::string& path, double lo, double hi) { std::lock_guard<std::mutex> lk(m_); learnPath_ = path; learnLo_ = lo; learnHi_ = hi; learnArmed_ = true; learnedCc_ = -1; }
    void cancelCcLearn() { std::lock_guard<std::mutex> lk(m_); learnArmed_ = false; }
    int learnedCc() { std::lock_guard<std::mutex> lk(m_); const int c = learnedCc_; return c; }   // last CC that learn mapped, -1 = none yet
    int lastCc() const { return ccAny_.load(std::memory_order_relaxed); }
    void clearCcLearn() { std::lock_guard<std::mutex> lk(m_); for (auto& l : ccLearn_) l = CcLearn{}; learnArmed_ = false; learnedCc_ = -1; }
    // Learned mappings as JSON {"<cc>":{"path":"...","lo":0,"hi":1}, ...} (for the UI to persist per profile).
    std::string ccLearnJson() {
        std::lock_guard<std::mutex> lk(m_); Json o; o.type = Json::Obj;
        for (int i = 0; i < 128; i++) if (ccLearn_[i].used) { Json e; e.type = Json::Obj; Json p; p.type = Json::Str; p.s = ccLearn_[i].path; e.o.push_back({"path", p});
            Json lo; lo.type = Json::Num; lo.n = ccLearn_[i].lo; e.o.push_back({"lo", lo}); Json hi; hi.type = Json::Num; hi.n = ccLearn_[i].hi; e.o.push_back({"hi", hi}); o.o.push_back({std::to_string(i), e}); }
        return jsonDump(o);
    }
    bool setCcLearnJson(const std::string& text) {
        Json j; try { j = JsonParser::parse(text); } catch (...) { return false; }
        if (j.type != Json::Obj) return false;
        std::lock_guard<std::mutex> lk(m_); for (auto& l : ccLearn_) l = CcLearn{};
        for (auto& kv : j.o) { const int cc = atoi(kv.first.c_str()); if (cc < 0 || cc > 127 || kv.second.type != Json::Obj) continue;
            const Json* p = jsonPathPtr(const_cast<Json&>(kv.second), "path", false); if (!p || p->type != Json::Str) continue;
            const Json* lo = jsonPathPtr(const_cast<Json&>(kv.second), "lo", false); const Json* hi = jsonPathPtr(const_cast<Json&>(kv.second), "hi", false);
            ccLearn_[cc] = CcLearn{ p->s, lo ? lo->n : 0, hi ? hi->n : 1, true }; }
        return true;
    }

    // ---- audio thread
    void render(float* L, float* R, int frames) {
        if (m_.try_lock()) {
            if (pendingPreset_) { preset_ = std::move(pendingPreset_); pendingPreset_.reset(); synth_.setPreset(preset_); perf_.setPreset(preset_); hasPreset_.store(true, std::memory_order_release); }
            if (pendingUpdate_) { if (retired_.size() < retired_.capacity()) retired_.push_back(preset_); else synth_.dropXfxRefs(); preset_ = std::move(pendingUpdate_); pendingUpdate_.reset(); synth_.updatePreset(preset_); perf_.updatePreset(preset_); }
            for (auto& w : work_) w(); work_.clear();
            m_.unlock();
        }
        { Cmd c; while (pop(c)) apply(c); }
        int done = 0;
        while (done < frames) {   // Performance schedules per 128-frame quantum, right before the synth renders it
            if (synth_.atQuantumBoundary()) { perf_.advance(synth_.clock(), 128); synth_.setLuaTransport(perf_.running(), perf_.beatNow(), perf_.bpm); }
            const int chunk = std::min(frames - done, synth_.quantumRemaining());
            synth_.process(L + done, R + done, chunk);
            done += chunk;
        }
    }
    int seqStep(int layer) const { return (layer >= 0 && layer < kNumLayers) ? perf_.stepHeard(layer) : 0; }   // playheads are shown latency-compensated (what is audible now)
    void setDisplayLatency(int samples) { perf_.setDisplayLatency(samples); }
    int seqSlot(int layer) const { return (layer >= 0 && layer < kNumLayers) ? perf_.currentSlot(layer) : 0; }
    int drumStep(int lane) const { return perf_.drumStepHeard(lane); }
    bool seqRunning() const { return perf_.transportRunning(); }
    double transportBeat() const { return perf_.beatHeard(); }   // playhead (quarter notes) for the roll / automation displays; benign unsynchronised read
    Synth& synth() { return synth_; }
    Performance& performance() { return perf_; }

private:
    // ---- lock-free command ring (bounded MPMC, Vyukov): any thread may enqueue (UI, MIDI, host parameter events on the render
    // thread); only the audio thread drains. No allocation, no locks, so it is real-time safe on both ends.
    enum Op : int { C_NoteOn, C_NoteOff, C_Panic, C_Macro, C_ModWheel, C_Bend, C_BendRange, C_MasterVol, C_MasterTone, C_WtFm, C_SampleMixPos,
                    C_SampleMixOn, C_Bpm, C_ArpOn, C_Latch, C_Transport, C_Poly, C_Swing, C_ReleaseHold, C_Mpe, C_MpeNoteOn, C_MpeNoteOff, C_MpeCtl, C_MpeGlide, C_PadOn, C_PadOff, C_Xy, C_PadSlice, C_HostTransport, C_ClipLaunch, C_ClipStop, C_ClipStopAll, C_ClipArm, C_RrReset };
    struct CcLearn { std::string path; double lo = 0, hi = 1; bool used = false; };
    CcLearn ccLearn_[128]; bool learnArmed_ = false; std::string learnPath_; double learnLo_ = 0, learnHi_ = 1; int learnedCc_ = -1;
    std::atomic<float> ccVal_[128]; std::atomic<bool> ccDirty_[128]; std::atomic<int> ccAny_{ -1 }; std::atomic<int> midiChannel_{ 0 };
    bool hostPlaying_ = false;
    struct Cmd { int op = 0, a = 0, b = 0; double x = 0; };
    struct Slot { std::atomic<size_t> seq; Cmd c; };
    static constexpr size_t kRing = 2048;
    std::unique_ptr<Slot[]> ring_{new Slot[kRing]};
    std::atomic<size_t> head_{0}, tail_{0};
    struct RingInit { RingInit(Slot* r) { for (size_t i = 0; i < kRing; i++) r[i].seq.store(i, std::memory_order_relaxed); } } ringInit_{ring_.get()};
    void push(int op, int a = 0, int b = 0, double x = 0) {
        size_t pos = head_.load(std::memory_order_relaxed);
        for (;;) {
            Slot& s = ring_[pos % kRing]; const size_t seq = s.seq.load(std::memory_order_acquire); const intptr_t dif = (intptr_t)seq - (intptr_t)pos;
            if (dif == 0) { if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) { s.c = Cmd{op, a, b, x}; s.seq.store(pos + 1, std::memory_order_release); return; } }
            else if (dif < 0) return;   // full: drop (2048 pending commands per block is far beyond any real load)
            else pos = head_.load(std::memory_order_relaxed);
        }
    }
    bool pop(Cmd& out) {   // single consumer: the audio thread
        const size_t pos = tail_.load(std::memory_order_relaxed); Slot& s = ring_[pos % kRing];
        if ((intptr_t)s.seq.load(std::memory_order_acquire) - (intptr_t)(pos + 1) < 0) return false;
        out = s.c; s.seq.store(pos + kRing, std::memory_order_release); tail_.store(pos + 1, std::memory_order_relaxed); return true;
    }
    void apply(const Cmd& c) {
        switch (c.op) {
            case C_NoteOn: perf_.recPush(0, c.a, c.x); perf_.noteDown(c.a, (float)c.x); break;
            case C_NoteOff: perf_.recPush(1, c.a, 0); perf_.noteUp(c.a); break;
            case C_PadOn: perf_.padDown(c.a / 256, c.a % 256 - 1, 1000 + c.b / 256, c.b % 256, c.x); break;
            case C_PadOff: perf_.padUp(c.a, 1000 + c.b); break;
            case C_PadSlice: perf_.padSlice(c.a, c.b & 255, 1000 + ((c.b >> 8) & 15), c.x); break;   // c.b = slice(1-based, 0 = whole) + 256 * pad
            case C_Panic: perf_.panic(); break;
            case C_Macro: synth_.setMacro(c.a, c.x); break;
            case C_Xy: synth_.setXy(c.a, c.x); break;
            case C_ModWheel: perf_.recPush(3, 0, c.x); synth_.modwheel = c.x; break;
            case C_Bend: perf_.recPush(2, 0, c.x); synth_.pitchBend = c.x; break;
            case C_BendRange: synth_.bendRange = c.x; break;
            case C_MasterVol: synth_.masterVolume = c.x; break;
            case C_MasterTone: synth_.masterTone = std::max(0.0, std::min(1.0, c.x)); break;
            case C_WtFm: synth_.setWtFmMix(c.x); break;
            case C_SampleMixPos: synth_.setSampleMix(synth_.sampleMixOn, c.x); break;
            case C_SampleMixOn: synth_.setSampleMix(c.x != 0, synth_.sampleMixPos); break;
            case C_Bpm: perf_.bpm = c.x; break;
            case C_ArpOn: perf_.setArpOn(c.a != 0); break;
            case C_Latch: perf_.setLatch(c.a != 0); break;
            case C_Transport: if (c.a) perf_.startTransport(); else perf_.stopTransport(); break;
            case C_HostTransport: perf_.setHostTransport(c.a != 0, c.x, perf_.bpm); break;
            case C_ClipLaunch: if (!perf_.running() && !perf_.hostControlled()) perf_.startTransport(); perf_.clipLaunch(c.a); break;
            case C_ClipStop: perf_.clipStop(c.a, c.b != 0); break;
            case C_ClipStopAll: perf_.clipStopAll(); break;
            case C_ClipArm: perf_.clipArm(c.a); break;
            case C_RrReset: synth_.rrReset(); break;
            case C_Poly: perf_.maxPolyphony = c.a; perf_.legato = c.b != 0; break;
            case C_Swing: perf_.swingOn = c.a != 0; perf_.swingAmount = c.x; break;
            case C_ReleaseHold: perf_.releaseLayerHold(c.a); break;
            case C_Mpe: synth_.mpeEnabled = c.a != 0; break;
            case C_MpeNoteOn: mpeVoiceOn(c.a, c.b); synth_.mpeNoteOn(c.a, c.b); perf_.recPush(0, c.b, c.x); perf_.noteDown(c.b, (float)c.x); break;
            case C_MpeNoteOff: mpeVoiceOff(c.a); synth_.mpeNoteOff(c.a, c.b); perf_.recPush(1, c.b, 0); perf_.noteUp(c.b); break;
            case C_MpeGlide: synth_.mpeGlide(c.a, c.x); break;
            case C_MpeCtl: if (!synth_.mpeController(c.a, c.b, c.x) && c.b == 0) { perf_.recPush(2, 0, c.x); synth_.pitchBend = c.x; } break;
        }
    }
    // Retired presets are freed on the main thread only, and never while a sounding XFX voice may still point into one of them.
    void trimRetired_() { if (retired_.size() > 48 && (!synth_.anyXfxVoice() || retired_.size() > 128)) retired_.erase(retired_.begin(), retired_.begin() + 24); }
    // clip model internals (m_ held by the callers)
    clips::ClipModel clipModel_; std::shared_ptr<const clips::ClipSet> clipLast_; std::vector<std::shared_ptr<const clips::ClipSet>> clipRetired_; bool clipAutoDirty_ = false;
    void clipPublish_() {
        auto snap = clipModel_.snapshot(); if (clipLast_) { clipRetired_.push_back(clipLast_); if (clipRetired_.size() > 10) clipRetired_.erase(clipRetired_.begin(), clipRetired_.begin() + 4); }   // freed here on the UI thread, never on the audio thread
        clipLast_ = snap; work_.push_back([this, snap] { synth_.setClipSet(snap); perf_.setClipSet(snap); });
        Json* t = jsonPathPtr(working_, "extra.clips", true); if (t) { try { *t = JsonParser::parse(clipModel_.json()); } catch (...) {} }
    }
    /* Round 6: the XFX pseudo layer keeps its patterns in extra.xfxSeq (web state.xfxSeq), every other layer in layers.<KEY>. */
    static std::string seqPath_(const std::string& L) { return L == "XFX" ? std::string("extra.xfxSeq") : "layers." + L; }
    void ensureXfxSeq_() {
        if (jsonPathPtr(working_, "extra.xfxSeq.patterns", false)) return;
        std::string j = "{\"currentSlot\":0,\"chainMode\":\"off\",\"chainOrder\":[],\"gate\":0.7,\"patterns\":["; for (int i = 0; i < 8; i++) j += std::string(i ? "," : "") + "{\"length\":16,\"steps\":[],\"roll\":{\"bars\":4,\"loopOn\":true,\"loopStart\":0,\"loopEnd\":4,\"notes\":[]}}"; j += "]}";
        Json* t = jsonPathPtr(working_, "extra.xfxSeq", true); if (t) { try { *t = JsonParser::parse(j); } catch (...) {} }
    }
    bool clipFromRoll_(int slot, int layer) {
        if (layer < 0 || layer >= clips::kSeqLayers || working_.type != Json::Obj) return false; const std::string L = clips::kSeqLayerNames[layer], LP = seqPath_(L);
        const Json* cs = jsonPathPtr(working_, LP + ".currentSlot", false); const int cur = cs ? (int)cs->asNum(0) : 0;
        const Json* roll = jsonPathPtr(working_, LP + ".patterns." + std::to_string(cur) + ".roll", false); if (!roll || !roll->isObj()) return false;
        std::vector<clips::Note> ns; const Json* nj = roll->get("notes"); if (nj) for (const Json& n : nj->a) { clips::Note x; x.t = n.num("t", 0); x.l = n.num("l", 0.25); x.p = (int)n.num("p", 60); x.v = n.num("v", 0.8); if (std::isfinite(x.t) && x.t >= 0) ns.push_back(x); }
        return clipModel_.fromRoll(slot, layer, (int)roll->num("bars", 4), ns);
    }
    bool clipToRoll_(int slot) {
        if (!clipModel_.occupied(slot)) return false; const clips::Clip& c = clipModel_.set().at(slot); const std::string L = clips::kSeqLayerNames[c.layer], LP = seqPath_(L); if (L == "XFX") ensureXfxSeq_();
        const Json* cs = jsonPathPtr(working_, LP + ".currentSlot", false); const int cur = cs ? (int)cs->asNum(0) : 0;
        std::string j = "{\"bars\":" + std::to_string(c.bars) + ",\"loopOn\":true,\"loopStart\":0,\"loopEnd\":" + std::to_string(c.bars) + ",\"notes\":[";
        for (size_t i = 0; i < c.notes.size(); i++) { const clips::Note& n = c.notes[i]; j += (i ? "," : ""); j += "{\"t\":" + clips::num(n.t) + ",\"l\":" + clips::num(n.l) + ",\"p\":" + std::to_string(std::max(0, std::min(127, n.p + c.tr))) + ",\"v\":" + clips::num(n.v) + ",\"prob\":100,\"rat\":1,\"tr\":0,\"pl\":{}}"; }
        j += "]}"; m_.unlock(); const bool ok = setValue(LP + ".patterns." + std::to_string(cur) + ".roll", j); m_.lock(); return ok;
    }
    Synth synth_; Performance perf_; std::shared_ptr<Preset> preset_, pendingPreset_, pendingUpdate_; std::vector<std::shared_ptr<Preset>> retired_; Json working_;
    // Preset-embedded samples (sampleLibrary) and custom wavetables are decoded / registered only when their content changed (called with m_ held)
    std::map<std::string, uint64_t> embedHash_;
    void registerEmbedded_(const Preset& P) {
        auto fnv = [](uint64_t h, const void* d, size_t n) { const unsigned char* c = (const unsigned char*)d; for (size_t i = 0; i < n; i++) { h ^= c[i]; h *= 1099511628211ull; } return h; };
        for (const SampleRef& r : P.samples) {
            const uint64_t h = fnv(14695981039346656037ull, r.b64.data(), r.b64.size()); auto it = embedHash_.find("s:" + r.name);
            if (it != embedHash_.end() && it->second == h) continue; embedHash_["s:" + r.name] = h;
            const auto bytes = base64Decode(r.b64); auto b = parseWav(bytes.data(), bytes.size()); if (!b) continue;
            std::shared_ptr<const SampleBuf> sb = b; const std::string nm = r.name; work_.push_back([=] { synth_.setSample(nm, sb); });
        }
        for (const CustomWt& t : P.customTables) {
            uint64_t h = 14695981039346656037ull; for (auto& f : t.frames) h = fnv(h, f.data(), f.size() * sizeof(float));
            auto it = embedHash_.find("t:" + t.id); if (it != embedHash_.end() && it->second == h) continue; embedHash_["t:" + t.id] = h;
            auto tab = std::make_shared<WtTable>(); tab->tableSize = (int)t.frames[0].size(); tab->frames = t.frames;
            std::shared_ptr<const WtTable> tb = tab; const std::string id = t.id; work_.push_back([=] { synth_.setWavetable(id, tb); });
        }
    }
    std::mutex m_; std::vector<std::string> loadWarnings_; std::vector<std::function<void()>> work_; double sr_ = 48000; bool ready_ = false; std::atomic<bool> hasPreset_{false};
};

} // namespace vv
