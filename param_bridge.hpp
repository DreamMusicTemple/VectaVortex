// ParamBridge: maps the host-visible AUv3 parameters (Core/param_ids.json) onto the live preset / global controls.
// Real-time contract: Engine::setParam() (any thread, including the render thread) only stores the value and sets a dirty flag.
// pump() runs on a NON-realtime thread (Swift timer), coalesces the dirty parameters and applies them with FullEngine::setValues()
// (one preset re-parse per pump) or the lock-free command ring. syncFromPreset() does the reverse after a preset load so the
// host's controls follow the sound.
// Scaling follows the web app's AUv3 bridge exactly (n01 ranges in `tools`/full-app.html READ table).
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <map>
#include <cstdio>
#include "full_engine.hpp"
#include "params_generated.hpp"
#include "ir.hpp"

namespace vv {

struct ParamRule {
    enum Kind { None, Path, Macro, ModWheel, MasterVol, Tempo, Global, FxMixAll, ReverbAlgo, FxParam, PhaserOn, StagePar, StageOn, IrFactory } kind = None;
    std::string path;          // Path: dot path into the preset
    double lo = 0, hi = 1;     // Path scaling when `norm`
    bool norm = false;         // host value is 0..1 and maps to lo..hi
    int idx = 0;               // Macro index
    std::string name;          // Global name, fx type, or fx param key
    int clampOther = 0;        // sampler start(1)/end(2): keep start < end
    std::string otherPath;
};

inline const std::vector<ParamRule>& paramRules() {
    static const std::vector<ParamRule> rules = [] {
        std::vector<ParamRule> r(kParamIdLimit);
        auto lay = [](const std::string& a) -> const char* { return a == "wavetable" ? "WT" : a == "fm" ? "FM" : a == "sampler" ? "SMP" : a == "procedural" ? "PRO" : nullptr; };
        for (int i = 0; i < kParamCount; i++) {
            const std::string a = kParams[i].address; ParamRule x;
            auto path = [&](const std::string& p, bool norm = false, double lo = 0, double hi = 1) { x.kind = ParamRule::Path; x.path = p; x.norm = norm; x.lo = lo; x.hi = hi; };
            int n = 0; char s1[32] = {0}, s2[32] = {0};
            if (a == "mod_wheel") x.kind = ParamRule::ModWheel;
            else if (a == "master_volume") x.kind = ParamRule::MasterVol;
            else if (a == "tempo") x.kind = ParamRule::Tempo;
            else if (a == "master_tone") { x.kind = ParamRule::Global; x.name = "masterTone"; }
            else if (a == "wt_fm_mix") { x.kind = ParamRule::Global; x.name = "wtFmMix"; }
            else if (std::sscanf(a.c_str(), "macro_%d", &n) == 1) { x.kind = ParamRule::Macro; x.idx = n - 1; }
            else if (std::sscanf(a.c_str(), "lfo_%d_rate_hz", &n) == 1) path("lfo.lfo" + std::to_string(n) + ".hz", true, 0.1, 20.0);
            else if (a == "reverb_mix_all_reverbs") { x.kind = ParamRule::FxMixAll; x.name = "reverb"; }
            else if (a == "delay_mix_all_delays") { x.kind = ParamRule::FxMixAll; x.name = "delay"; }
            else if (a == "saturation_mix_all_saturation") { x.kind = ParamRule::FxMixAll; x.name = "saturation"; }
            else if (a == "reverb_algorithm") x.kind = ParamRule::ReverbAlgo;
            else if (a.rfind("reverb_", 0) == 0) { x.kind = ParamRule::FxParam; x.idx = 0; x.name = a.substr(7); }
            else if (a == "fxeq_on" || a == "fxir_on") { x.kind = ParamRule::StageOn; x.name = a.substr(2, a.find('_') - 2); }
            else if (a == "fxir_factory") x.kind = ParamRule::IrFactory;
            else if (a.rfind("fxeq_", 0) == 0 || a.rfind("fxir_", 0) == 0) {
                x.kind = ParamRule::StagePar; x.name = a.substr(2, a.find('_') - 2); std::string k = a.substr(a.find('_') + 1);
                static const std::pair<const char*, const char*> mp[] = {{"band1_freq","f1"},{"band2_freq","f2"},{"band3_freq","f3"},{"band4_freq","f4"},{"band1_gain","g1"},{"band2_gain","g2"},{"band3_gain","g3"},{"band4_gain","g4"},{"output","out"},
                    {"length","lenPct"},{"early_late","earlyLate"},{"input_level","inLevel"},{"output_level","outLevel"},{"low_cut","lowCut"},{"high_cut","highCut"},{"gain","irGain"}};
                for (auto& q : mp) if (k == q.first) { k = q.second; break; }
                x.path = k; }
            else if (a == "phaser_on") x.kind = ParamRule::PhaserOn;
            else if (a == "phaser_mode") x.kind = ParamRule::None;
            else if (a.rfind("phaser_", 0) == 0) { x.kind = ParamRule::FxParam; x.idx = 1; x.name = a.substr(7); }
            // layer filter / envelope: <layer>_filter_{cutoff,resonance,keytrack}, <layer>_amp_{attack,decay,sustain,release}
            else if (std::sscanf(a.c_str(), "%31[a-z]_filter_%31s", s1, s2) == 2 && lay(s1)) {
                const std::string L = lay(s1), k = s2; const bool pro = (L == "PRO");
                if (k == "cutoff") path("layers." + L + ".filter.cutoff", !pro, 20, 18000);
                else if (k == "resonance") path("layers." + L + ".filter.reso", !pro, 0, 130);
                else if (k == "keytrack") path("layers." + L + ".filter.keytrack", !pro, 0, 200);
            }
            else if (std::sscanf(a.c_str(), "%31[a-z]_amp_%31s", s1, s2) == 2 && lay(s1)) {
                const std::string L = lay(s1), k = s2; const bool pro = (L == "PRO");
                const char* key = k == "attack" ? "a" : k == "decay" ? "d" : k == "sustain" ? "s" : "r";
                if (k == "sustain" || pro) path("layers." + L + ".env." + key); else path("layers." + L + ".env." + key, true, 0.001, 3.001);
            }
            else if (std::sscanf(a.c_str(), "wt_osc%d_%31s", &n, s2) == 2) {
                std::string k = s2; if (k == "fine") k = "fineTune";
                path("layers.WT.oscs." + std::to_string(n - 1) + "." + k);
            }
            else if (std::sscanf(a.c_str(), "fm_op%d_%31s", &n, s2) == 2) path("layers.FM.oscs." + std::to_string(n - 1) + "." + s2);
            else if (std::sscanf(a.c_str(), "smp_slot%d_%31s", &n, s2) == 2) {
                std::string k = s2; if (k == "fine") k = "fineTune";
                const std::string base = "layers.SMP.oscs." + std::to_string(n - 1) + ".";
                path(base + k);
                if (k == "start") { x.clampOther = 1; x.otherPath = base + "end"; } else if (k == "end") { x.clampOther = 2; x.otherPath = base + "start"; }
            }
            else if (std::sscanf(a.c_str(), "procedural_gen%c_%31s", s1, s2) == 2 && (s1[0] == 'a' || s1[0] == 'b')) {
                std::string k = s2; if (k == "tone") k = "bright"; else if (k == "damping") k = "damp";
                path(std::string("layers.PRO.oscs.") + (s1[0] == 'a' ? "0" : "1") + "." + k);
            }
            else if (std::sscanf(a.c_str(), "sid_voice%d_%31s", &n, s2) == 2 && n >= 1 && n <= 3) {
                const std::string k = s2, base = "layers.SID.oscs." + std::to_string(n - 1) + ".";
                if (k == "on") path(base + "enabled"); else if (k == "level") path(base + "level"); else if (k == "pan") path(base + "pan");
                else if (k == "pulsewidth") path(base + "pulseWidth"); else if (k == "ring") path(base + "ringModOn"); else if (k == "sync") path(base + "syncOn");
                else if (k == "wave") { x.kind = ParamRule::Path; x.path = base + "wave"; x.name = "sidwave"; }
            }
            else if (a == "fm_sixop_on") path("layers.FM.sixOp");
            else if (a == "fm_sixop_voice_on") path("layers.FM.sixOpEnabled");
            else if (a == "fm_sixop_trim") path("layers.FM.dx7OutputTrim");
            else if (a == "fm_sixop_pan") path("layers.FM.dx7Pan");
            else if (a.compare(0, 6, "mixer_") == 0 && a.size() > 11 && a.compare(a.size() - 5, 5, "_gain") == 0) {
                std::string k = a.substr(6, a.size() - 11); for (auto& ch : k) ch = (char)std::toupper((unsigned char)ch);
                if (mixIndex(k) >= 0) path("mixerGains." + k);
            }
            else if (a == "fm_sixop_transpose") path("layers.FM.dx7Transpose");
            else if (a == "chaos_lorenz_rate") path("chaos.lorenz.rate");
            else if (a == "chaos_rossler_rate") path("chaos.rossler.rate");
            else if (std::sscanf(a.c_str(), "lfo_%d_%31s", &n, s2) == 2 && (std::string(s2) == "morph" || std::string(s2) == "smoothing")) path("lfo.lfo" + std::to_string(n) + "." + s2);
            r[kParams[i].id] = x;
        }
        return r;
    }();
    return rules;
}

inline std::string numJson(double v) { char b[48]; std::snprintf(b, sizeof b, "%.9g", v); return b; }

class ParamBridge {
public:
    static const char* reverbAlgo(int i) { static const char* n[] = {"", "hall", "plate ambient", "kristal", "flow", "mothership", "formant"}; return n[i < 0 ? 0 : i > 6 ? 6 : i]; }
    static int reverbAlgoIndex(const std::string& s) { for (int i = 1; i <= 6; i++) if (s == reverbAlgo(i)) return i; return 1; }

    // Applies the changed host parameters. `get(id)` returns the current host value, `dirty` the ids to apply.
    template <class Getter>
    static void apply(FullEngine& fe, const std::vector<uint32_t>& dirty, Getter get) {
        if (!fe.hasPreset()) return;
        const auto& R = paramRules();
        std::vector<std::pair<std::string, std::string>> edits;
        // structural queries need the working preset
        auto stageIndex = [&](const std::string& type) -> int {
            return fe.withWorking([&](Json& w) -> int { const Json* st = w.get("fxStages"); if (!st || !st->isArr()) return -1; for (size_t i = 0; i < st->size(); i++) if (st->at(i).str("type") == type) return (int)i; return -1; });
        };
        auto stageCount = [&]() -> int { return fe.withWorking([&](Json& w) -> int { const Json* st = w.get("fxStages"); return st && st->isArr() ? (int)st->size() : 0; }); };
        std::map<std::string, int> pending;
        auto ensureStage = [&](const std::string& type, const std::string& paramsJson) -> int {
            int k = stageIndex(type); if (k >= 0) return k;
            auto pend = pending.find(type); if (pend != pending.end()) return pend->second;   // created earlier in this same batch (edits apply at the end)
            k = stageCount() + (int)pending.size(); pending[type] = k;
            edits.emplace_back("fxStages." + std::to_string(k), "{\"type\":\"" + type + "\",\"enabled\":true,\"params\":" + paramsJson + ",\"sends\":{\"WT\":true,\"FM\":true,\"SMP\":true,\"PRO\":true}}");
            return k;
        };
        const bool phaserIsFlanger = get(kP_phaser_mode) >= 0.5;
        for (uint32_t id : dirty) {
            if (id >= R.size()) continue; const ParamRule& r = R[id]; const double v = get(id);
            switch (r.kind) {
                case ParamRule::None: break;
                case ParamRule::MasterVol: fe.setMasterVolume(v); break;
                case ParamRule::ModWheel: fe.setModWheel(v); break;
                case ParamRule::Macro: fe.setMacro(r.idx, v); break;
                case ParamRule::Tempo: fe.setBpm(v); edits.emplace_back("bpm", numJson(v)); break;
                case ParamRule::Global: fe.setGlobal(r.name, v); break;
                case ParamRule::Path: {
                    double x = r.norm ? r.lo + v * (r.hi - r.lo) : v;
                    if (r.clampOther) {
                        const std::string o = fe.getValue(r.otherPath); const double ov = o.empty() ? (r.clampOther == 1 ? 1 : 0) : std::atof(o.c_str());
                        x = r.clampOther == 1 ? std::min(x, ov - 0.001) : std::max(x, ov + 0.001);
                    }
                    if (r.name == "sidwave") { static const char* wn[] = {"triangle", "sawtooth", "pulse", "noise", "tri_saw", "tri_pulse", "saw_pulse"}; const int wi = (int)std::floor(x + 0.5); edits.emplace_back(r.path, std::string("\"") + wn[wi < 0 ? 0 : wi > 6 ? 6 : wi] + "\""); break; }
                    edits.emplace_back(r.path, numJson(x)); break;
                }
                case ParamRule::FxMixAll: {
                    const int n = stageCount();
                    for (int k = 0; k < n; k++) {
                        const bool match = fe.withWorking([&](Json& w) { return w.get("fxStages")->at(k).str("type") == r.name; });
                        if (match) edits.emplace_back("fxStages." + std::to_string(k) + ".params.mix", numJson(v));
                    }
                    break;
                }
                case ParamRule::ReverbAlgo: {
                    const int a = (int)std::lround(v); int k = stageIndex("reverb");
                    if (a <= 0) { if (k >= 0) edits.emplace_back("fxStages." + std::to_string(k) + ".enabled", "false"); break; }
                    if (k < 0) k = ensureStage("reverb", "{\"algo\":\"hall\",\"decay\":0.5,\"size\":0.5,\"predelay\":0.02,\"mix\":0.3,\"mod\":0.4,\"colour\":0.5}");
                    else edits.emplace_back("fxStages." + std::to_string(k) + ".enabled", "true");
                    edits.emplace_back("fxStages." + std::to_string(k) + ".params.algo", std::string("\"") + reverbAlgo(a) + "\""); break;
                }
                case ParamRule::PhaserOn: {
                    const char* mine = phaserIsFlanger ? "flanger" : "phaser"; const char* other = phaserIsFlanger ? "phaser" : "flanger";
                    const int ko = stageIndex(other); if (ko >= 0) edits.emplace_back("fxStages." + std::to_string(ko) + ".enabled", "false");
                    int k = stageIndex(mine);
                    if (v >= 0.5) {
                        if (k < 0) k = ensureStage(mine, phaserIsFlanger ? "{\"rate\":0.25,\"depth\":0.5,\"feedback\":0.3,\"mix\":0.5}" : "{\"rate\":0.4,\"depth\":0.85,\"feedback\":0.6,\"mix\":0.5,\"stages\":8}");
                        else edits.emplace_back("fxStages." + std::to_string(k) + ".enabled", "true");
                    } else if (k >= 0) edits.emplace_back("fxStages." + std::to_string(k) + ".enabled", "false");
                    break;
                }
                case ParamRule::StageOn: {
                    const int k = stageIndex(r.name);
                    if (v >= 0.5) {
                        if (k < 0) ensureStage(r.name, r.name == "eq" ? "{\"f1\":120,\"g1\":0,\"f2\":600,\"g2\":0,\"f3\":2500,\"g3\":0,\"f4\":8000,\"g4\":0,\"out\":0,\"mix\":1}" : "{\"irId\":\"f:hall-concert\",\"irName\":\"Concert Hall\",\"mix\":0.3}");
                        else edits.emplace_back("fxStages." + std::to_string(k) + ".enabled", "true");
                    } else if (k >= 0) edits.emplace_back("fxStages." + std::to_string(k) + ".enabled", "false");
                    break;
                }
                case ParamRule::StagePar: {   // edits an existing stage only (the "on" parameter creates it)
                    const int k = stageIndex(r.name); if (k < 0) break;
                    edits.emplace_back("fxStages." + std::to_string(k) + ".params." + r.path, numJson(v)); break;
                }
                case ParamRule::IrFactory: {
                    const int k = stageIndex("ir"); if (k < 0) break; const int i = std::max(0, std::min(ir::kFactoryCount - 1, (int)std::lround(v)));
                    edits.emplace_back("fxStages." + std::to_string(k) + ".params.irId", std::string("\"f:") + ir::kFactory[i].id + "\"");
                    edits.emplace_back("fxStages." + std::to_string(k) + ".params.irName", std::string("\"") + ir::kFactory[i].name + "\""); break;
                }
                case ParamRule::FxParam: {
                    const std::string type = r.idx == 0 ? "reverb" : (phaserIsFlanger ? "flanger" : "phaser");
                    if (type == "flanger" && r.name == "stages") break;
                    const int k = stageIndex(type); if (k < 0) break;   // parameters only edit an existing stage; on/algorithm create it
                    edits.emplace_back("fxStages." + std::to_string(k) + ".params." + r.name, numJson(r.name == "stages" ? std::lround(v) : v)); break;
                }
            }
        }
        fe.setValues(edits);
    }

    // Reads the host-visible value of a parameter back from the preset (for controls to follow a loaded preset). Returns false when
    // the parameter has no preset-backed value (macros, master volume, globals).
    static bool read(FullEngine& fe, uint32_t id, double& out) {
        const auto& R = paramRules(); if (id >= R.size()) return false; const ParamRule& r = R[id];
        auto num = [&](const std::string& path, double& o) { const std::string s = fe.getValue(path); if (s.empty() || s == "null") return false; o = std::atof(s.c_str()); return true; };
        auto fx = [&](const std::string& type) -> int { return fe.withWorking([&](Json& w) -> int { const Json* st = w.get("fxStages"); if (!st || !st->isArr()) return -1; for (size_t i = 0; i < st->size(); i++) if (st->at(i).str("type") == type) return (int)i; return -1; }); };
        switch (r.kind) {
            case ParamRule::Path: {
                if (r.name == "sidwave") { const std::string w = fe.getValue(r.path); static const char* wn[] = {"\"triangle\"", "\"sawtooth\"", "\"pulse\"", "\"noise\"", "\"tri_saw\"", "\"tri_pulse\"", "\"saw_pulse\""}; out = 0; for (int i = 0; i < 7; i++) if (w == wn[i]) out = i; return true; }
                double x; if (!num(r.path, x)) return false; out = r.norm ? std::min(1.0, std::max(0.0, (x - r.lo) / (r.hi - r.lo))) : x; return true; }
            case ParamRule::Tempo: return num("bpm", out);
            case ParamRule::FxMixAll: { const int k = fx(r.name); return k >= 0 && num("fxStages." + std::to_string(k) + ".params.mix", out); }
            case ParamRule::ReverbAlgo: {
                const int k = fx("reverb"); if (k < 0) { out = 0; return true; }
                double en = 1; num("fxStages." + std::to_string(k) + ".enabled", en);
                const std::string a = fe.getValue("fxStages." + std::to_string(k) + ".params.algo");
                out = en >= 0.5 ? reverbAlgoIndex(a.size() > 1 ? a.substr(1, a.size() - 2) : a) : 0; return true; }
            case ParamRule::FxParam: {
                const std::string type = r.idx == 0 ? "reverb" : "phaser"; int k = fx(type); if (k < 0 && r.idx == 1) k = fx("flanger");
                return k >= 0 && num("fxStages." + std::to_string(k) + ".params." + r.name, out); }
            case ParamRule::StageOn: { const int k = fx(r.name); if (k < 0) { out = 0; return true; } double en = 1; num("fxStages." + std::to_string(k) + ".enabled", en); out = en >= 0.5 ? 1 : 0; return true; }
            case ParamRule::StagePar: { const int k = fx(r.name); return k >= 0 && num("fxStages." + std::to_string(k) + ".params." + r.path, out); }
            case ParamRule::IrFactory: {
                const int k = fx("ir"); if (k < 0) return false; std::string id = fe.getValue("fxStages." + std::to_string(k) + ".params.irId"); if (id.size() > 1 && id[0] == '"') id = id.substr(1, id.size() - 2);
                for (int i = 0; i < ir::kFactoryCount; i++) if (id == std::string("f:") + ir::kFactory[i].id) { out = i; return true; } return false; }
            case ParamRule::PhaserOn: {
                int k = fx("phaser"); int k2 = fx("flanger"); double a = 0, b = 0;
                if (k >= 0) { a = 1; num("fxStages." + std::to_string(k) + ".enabled", a); }
                if (k2 >= 0) { b = 1; num("fxStages." + std::to_string(k2) + ".enabled", b); }
                out = (a >= 0.5 || b >= 0.5) ? 1 : 0; return true; }
            default: return false;
        }
    }
};

} // namespace vv
