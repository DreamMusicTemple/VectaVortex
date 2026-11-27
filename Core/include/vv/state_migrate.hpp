// Versioned preset / session state: migration and validation (Round 3, stage 5). Mirrors vvMigrateSnapshot() of the web app.
// schemaVersion: 1/absent = original snapshots, 2 = + clips / extras, 3 = + EQ and IR reverb rack slots, sampler Round Robin, piano-roll lengths.
// migrate() works on the parsed JSON BEFORE it is applied: a preset that is not usable is refused (the caller keeps its current sound),
// and anything repairable is repaired with a human-readable warning instead of an exception.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "json.hpp"

namespace vv { namespace state {

constexpr int kSchemaVersion = 3;
constexpr size_t kMaxFxStages = 24;

inline const char* const* knownFxTypes(size_t& n) {
    static const char* const t[] = {"reverb", "ir", "eq", "delay", "saturation", "wavefolder", "spectral", "filter", "granular", "interstellar", "tape", "phaser", "flanger", "resonator", "mbcomp", "glitch", "chorus", "spread", "echo", "panning", "rotary", "amp"};
    n = sizeof(t) / sizeof(t[0]); return t;
}
inline bool knownFx(const std::string& s) { size_t n; const char* const* t = knownFxTypes(n); for (size_t i = 0; i < n; i++) if (s == t[i]) return true; return false; }

inline Json* member(Json& o, const char* k) { for (auto& kv : o.o) if (kv.first == k) return &kv.second; return nullptr; }
inline void setNum(Json& o, const char* k, double v) { Json* m = member(o, k); if (!m) { o.o.emplace_back(k, Json()); m = &o.o.back().second; } m->type = Json::Num; m->n = v; m->s.clear(); }
inline void setStr(Json& o, const char* k, const std::string& v) { Json* m = member(o, k); if (!m) { o.o.emplace_back(k, Json()); m = &o.o.back().second; } m->type = Json::Str; m->s = v; }
inline double numOr(const Json& o, const char* k, double d, double lo, double hi) { const Json* m = o.get(k); double v = (m && m->type == Json::Num && std::isfinite(m->n)) ? m->n : d; return std::min(hi, std::max(lo, v)); }

struct Result { bool ok = true; int from = 1; std::vector<std::string> warnings; };

inline Result migrate(Json& j) {
    Result r;
    if (j.type == Json::Obj && !j.get("layers")) { if (Json* d = member(j, "data")) if (d->type == Json::Obj) return migrate(*d); }   // single-preset wrapper {"vectaVortexPreset":1,"data":{...}}
    if (j.type != Json::Obj) { r.ok = false; r.warnings.push_back("The preset is not a valid preset file (nothing was loaded)."); return r; }
    const Json* L = j.get("layers");
    if (!L || L->type != Json::Obj || L->o.empty()) {   // layers missing / not an object / empty (a preset may omit layers it does not use: the engine fills those with defaults)
        r.ok = false; r.warnings.push_back("The preset is damaged: its layers are missing (nothing was loaded)."); return r; }
    if (const Json* v = j.get("schemaVersion")) if (v->type == Json::Num) r.from = (int)v->n;
    if (r.from > kSchemaVersion) r.warnings.push_back("This preset was made by a newer version (schema " + std::to_string(r.from) + "); features this version does not know are ignored.");
    Json* st = member(j, "fxStages");
    if (st && st->type != Json::Arr) { r.warnings.push_back("The effects rack was damaged and was reset."); st->type = Json::Arr; st->a.clear(); }
    if (st) {
        std::vector<Json> keep;
        for (size_t i = 0; i < st->a.size(); i++) {
            Json& f = st->a[i];
            const std::string ty = f.type == Json::Obj ? f.str("type") : std::string();
            if (!knownFx(ty)) { r.warnings.push_back("Effect " + std::to_string(i + 1) + (ty.empty() ? "" : " (\"" + ty.substr(0, 24) + "\")") + " is not known to this version and was skipped."); continue; }
            Json* p = member(f, "params"); if (!p || p->type != Json::Obj) { f.o.emplace_back("params", Json()); p = &f.o.back().second; p->type = Json::Obj; }
            if (ty == "eq") { for (int b = 1; b <= 4; b++) { const std::string fk = "f" + std::to_string(b), gk = "g" + std::to_string(b); const double dflt[4] = {120, 600, 2500, 8000};
                setNum(*p, fk.c_str(), numOr(*p, fk.c_str(), dflt[b - 1], 20, 20000)); setNum(*p, gk.c_str(), numOr(*p, gk.c_str(), 0, -24, 24)); } }
            if (ty == "ir") {
                if (p->str("irId").empty()) { setStr(*p, "irId", "f:hall-concert"); setStr(*p, "irName", "Concert Hall"); }
                const double a = numOr(*p, "start", 0, 0, 0.98), b = std::max(numOr(*p, "end", 1, 0.02, 1), a + 0.02); setNum(*p, "start", a); setNum(*p, "end", std::min(1.0, b)); setNum(*p, "lenPct", numOr(*p, "lenPct", 100, 1, 100));
            }
            keep.push_back(std::move(f));
        }
        if (keep.size() > kMaxFxStages) { r.warnings.push_back("The effects rack held " + std::to_string(keep.size()) + " effects; only the first " + std::to_string(kMaxFxStages) + " were kept."); keep.resize(kMaxFxStages); }
        st->a = std::move(keep);
    }
    setNum(j, "schemaVersion", kSchemaVersion);
    return r;
}

}}  // namespace vv::state
