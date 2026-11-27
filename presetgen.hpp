// Preset generator (the web app's "magic wand" GENERATE) and MODIFY (~10 % variation), as edits of the preset's JSON tree.
//
// GENERATE is recipe-driven and is a line-for-line port of genBuild() in Web/full-app.html: the recipes (Core/include/vv/presetgen_recipes.hpp, generated from
// gen/recipes.json by tools/export_gen_recipes.py) describe 8 categories (Arp, Bass, Key, Lead, Pad, Drum, FX, XFX) x several variants each, built on the
// full INIT preset (all five layers, Core/include/vv/presetgen_init.hpp). The same seed gives the same preset here and in the web app (verified by
// Core/tests/presetgen_parity_tests.cpp against values produced by the web code). When a scale is chosen and Scale lock is on, step-sequencer pitches, SID
// wave-table-arp steps and the keys that receive XFX programming all follow that scale.
// MODIFY (unchanged) nudges the Wavetable and FM layers by up to +-10 %.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <cstdint>
#include "json.hpp"
#include "chordmaker.hpp"
#include "presetgen_init.hpp"
#include "presetgen_recipes.hpp"

namespace vv { namespace presetgen {

using Rng = cm::Rng;

struct ScaleCtx { bool on = false; int root = 0; std::vector<int> ints; std::string label; };
struct GenInfo { std::string name, view, category; uint32_t seed = 0; };

static const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
namespace detail {
struct Mul {   // mulberry32 - identical to genMulberry() in the web app
    uint32_t a;
    explicit Mul(uint32_t s) : a(s) {}
    double operator()() { a += 0x6D2B79F5u; uint32_t t = (a ^ (a >> 15)) * (1u | a); t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t; return (double)(t ^ (t >> 14)) / 4294967296.0; }
};
inline Json jnum(double v) { Json j; j.type = Json::Num; j.n = v; return j; }
inline Json jstr(const std::string& v) { Json j; j.type = Json::Str; j.s = v; return j; }
inline Json jbool(bool v) { Json j; j.type = Json::Bool; j.b = v; return j; }
inline Json jobj() { Json j; j.type = Json::Obj; return j; }
inline Json jarr() { Json j; j.type = Json::Arr; return j; }
inline double r4(double x) { return std::floor(x * 10000.0 + 0.5) / 10000.0; }   // Math.round(x*10000)/10000
inline const Json* only(const Json& spec, const char*& key) {   // a value spec is a one-key object whose value is an array
    static const char* keys[] = { "r", "ri", "rl", "p", "pu", "sd" };
    if (spec.type != Json::Obj || spec.o.size() != 1 || spec.o[0].second.type != Json::Arr) return nullptr;
    for (auto k : keys) if (spec.o[0].first == k) { key = k; return &spec.o[0].second; }
    return nullptr;
}
inline bool inScale(const ScaleCtx& sc, int note) { int rel = (((note - sc.root) % 12) + 12) % 12; for (int i : sc.ints) if (i == rel) return true; return false; }
inline std::vector<int> scaleOffsets(const ScaleCtx& sc, int lo, int hi) { std::vector<int> o; for (int p = lo; p <= hi; p++) if (inScale(sc, 60 + p)) o.push_back(p); return o; }

struct Interp {
    Mul rng; ScaleCtx sc; std::map<std::string, std::vector<std::string>> used; std::vector<std::string> warn;
    Interp(uint32_t seed, const ScaleCtx& s) : rng(seed), sc(s) {}

    Json resolve(const Json& spec) {
        const char* k = nullptr; const Json* arr = only(spec, k);
        if (!arr) return spec;
        const std::string key = k;
        if (key == "r")  return jnum(r4(arr->at(0).asNum() + rng() * (arr->at(1).asNum() - arr->at(0).asNum())));
        if (key == "ri") return jnum(arr->at(0).asNum() + std::floor(rng() * (arr->at(1).asNum() - arr->at(0).asNum() + 1)));
        if (key == "rl") { const double lo = std::log(arr->at(0).asNum()), hi = std::log(arr->at(1).asNum()); return jnum(r4(std::exp(lo + rng() * (hi - lo)))); }
        if (key == "p")  return arr->a[(size_t)std::floor(rng() * (double)arr->a.size())];
        if (key == "pu") {
            const std::string uk = jsonDump(*arr); auto& u = used[uk]; if (u.size() >= arr->a.size()) u.clear();
            Json v; int g = 0; bool dup;
            do { v = arr->a[(size_t)std::floor(rng() * (double)arr->a.size())]; g++; const std::string vd = jsonDump(v); dup = false; for (auto& x : u) if (x == vd) { dup = true; break; } } while (dup && g < 64);
            u.push_back(jsonDump(v)); return v;
        }
        // sd: a scale degree - from the active scale's intervals, else from the fallback list
        if (sc.on && !sc.ints.empty()) return jnum(sc.ints[(size_t)std::floor(rng() * (double)sc.ints.size())]);
        return arr->a[(size_t)std::floor(rng() * (double)arr->a.size())];
    }

    void set(Json& root, const std::string& path, Json val) {
        std::vector<std::string> seg; { size_t i = 0; while (true) { size_t d = path.find('.', i); if (d == std::string::npos) { seg.push_back(path.substr(i)); break; } seg.push_back(path.substr(i, d - i)); i = d + 1; } }
        Json* o = &root;
        for (size_t i = 0; i + 1 < seg.size(); i++) {
            Json* n = nullptr;
            if (o->type == Json::Arr) { const size_t ix = (size_t)strtoul(seg[i].c_str(), nullptr, 10); if (ix < o->a.size()) n = &o->a[ix]; }
            else if (o->type == Json::Obj) { for (auto& kv : o->o) if (kv.first == seg[i]) { n = &kv.second; break; } }
            if (!n || (n->type != Json::Obj && n->type != Json::Arr)) { warn.push_back(path); return; }
            o = n;
        }
        const std::string& last = seg.back();
        if (o->type == Json::Arr) { const size_t ix = (size_t)strtoul(last.c_str(), nullptr, 10); if (ix >= o->a.size()) o->a.resize(ix + 1); o->a[ix] = std::move(val); }
        else { for (auto& kv : o->o) if (kv.first == last) { kv.second = std::move(val); return; } o->o.emplace_back(last, std::move(val)); }
    }
};
inline Json* child(Json& j, const char* k) { for (auto& kv : j.o) if (kv.first == k) return &kv.second; return nullptr; }
inline void touchPath(Json& layers, std::map<std::string, bool>& touched, const std::string& p) {
    std::vector<std::string> parts; std::string cur; for (char c : p) { if (c == '.' || c == ':') { parts.push_back(cur); cur.clear(); } else cur += c; } parts.push_back(cur);
    const size_t at = p.rfind("layers.", 0) == 0 ? 1 : 0; if (at < parts.size() && child(layers, parts[at].c_str())) touched[parts[at]] = true;
}

inline Json buildOne(const Json& recipe, const Json& fxDefaults, uint32_t seed, const ScaleCtx& sc, std::vector<std::string>* warnOut) {
    Interp in(seed, sc);
    Json data = JsonParser::parse(kInitPreset);
    Json& layers = *child(data, "layers"); std::map<std::string, bool> touched;
    if (const Json* set = recipe.get("set")) for (auto& pr : set->a) { touchPath(layers, touched, pr.at(0).s); in.set(data, pr.at(0).s, in.resolve(pr.at(1))); }
    if (const Json* conn = recipe.get("conn")) for (auto& c : conn->a) {
        touchPath(layers, touched, c.at(1).s);
        Json e = jobj(); e.o = { {"source", c.at(0)}, {"dest", c.at(1)}, {"amount", in.resolve(c.at(2))} }; child(data, "connections")->a.push_back(std::move(e));
    }
    if (const Json* fx = recipe.get("fx")) for (auto& f : fx->a) {
        Json params = jobj(); if (const Json* d = fxDefaults.get(f.str("type").c_str())) params = *d;
        if (const Json* pp = f.get("params")) for (auto& kv : pp->a) in.set(params, kv.at(0).s, in.resolve(kv.at(1)));
        Json sends = jobj(); for (const char* L : { "WT", "FM", "SMP", "PRO", "SID", "VOC", "LUA" }) sends.o.emplace_back(L, jbool(false));
        if (const Json* ss = f.get("sends")) for (auto& L : ss->a) { for (auto& kv : sends.o) if (kv.first == L.s) kv.second.b = true; touched[L.s] = true; }
        Json st = jobj(); st.o = { {"type", jstr(f.str("type"))}, {"enabled", jbool(true)}, {"params", params}, {"sends", sends} };
        child(data, "fxStages")->a.push_back(std::move(st));
    }
    if (const Json* q = recipe.get("seq")) {
        const std::string ln = q->str("layer"); Json& L = *child(layers, ln.c_str()); touched[ln] = true;
        Json& pat = child(L, "patterns")->a[0]; const int len = (int)q->num("len", 16);
        *child(pat, "length") = jnum(len);
        Json steps = jarr(); const int total = std::max(32, len);
        for (int i = 0; i < total; i++) { Json s = jobj(); s.o = { {"on", jbool(false)}, {"pitch", jnum(0)}, {"ratchet", jnum(1)}, {"prob", jnum(100)}, {"tempoRatio", jnum(1)}, {"plocks", jobj()}, {"transientIdx", jnum(0)} }; steps.a.push_back(std::move(s)); }
        *child(pat, "steps") = std::move(steps);
        const Json& cr = *q->get("count"); const int c0 = (int)cr.at(0).n, c1 = (int)cr.at(1).n;
        const int cnt = std::min(len, c0 + (int)std::floor(in.rng() * (double)(c1 - c0 + 1)));
        std::vector<int> idx; for (int i = 1; i < len; i++) idx.push_back(i);
        for (int i = 0; i + 1 < (int)idx.size(); i++) { const int j = i + (int)std::floor(in.rng() * (double)((int)idx.size() - i)); std::swap(idx[(size_t)i], idx[(size_t)j]); }
        std::vector<int> on = { 0 }; for (int i = 0; i < std::max(0, cnt - 1) && i < (int)idx.size(); i++) on.push_back(idx[(size_t)i]);
        const Json& fb = *q->get("pitches"); int lo = (int)fb.at(0).n, hi = lo; for (auto& v : fb.a) { lo = std::min(lo, (int)v.n); hi = std::max(hi, (int)v.n); }
        std::vector<int> pool; if (sc.on) pool = scaleOffsets(sc, lo, hi); if (pool.empty()) for (auto& v : fb.a) pool.push_back((int)v.n);
        const Json& rat = *q->get("ratchet");
        for (size_t k = 0; k < on.size(); k++) {
            Json& s = child(pat, "steps")->a[(size_t)on[k]]; *child(s, "on") = jbool(true);
            *child(s, "pitch") = jnum((k == 0 && sc.on) ? pool[0] : pool[(size_t)std::floor(in.rng() * (double)pool.size())]);
            *child(s, "ratchet") = rat.a[(size_t)std::floor(in.rng() * (double)rat.a.size())]; *child(s, "prob") = jnum(100);
        }
    }
    if (const Json* q = recipe.get("wtsteps")) {
        touched["SID"] = true; Json& arp = *child(*child(layers, "SID"), "wtArp");
        const Json& nr = *q->get("n"); const int n = (int)nr.at(0).n + (int)std::floor(in.rng() * (nr.at(1).n - nr.at(0).n + 1));
        Json spec = jobj(); spec.o.emplace_back("sd", *q->get("pitches"));
        const Json& waves = *q->get("waves"); Json steps = jarr();
        for (int i = 0; i < n; i++) {
            Json v = in.resolve(spec);
            Json s = jobj(); s.o = { {"wave", waves.a[(size_t)std::floor(in.rng() * (double)waves.a.size())]}, {"mode", jstr("relative")}, {"value", v}, {"gate", jbool(true)}, {"ringMod", jbool(false)}, {"sync", jbool(false)}, {"lengthTicks", jnum(1)}, {"tempoSync", jbool(false)}, {"syncDivision", jnum(0.25)} };
            steps.a.push_back(std::move(s));
        }
        *child(arp, "steps") = std::move(steps);
    }
    // LUA instrument (web genBuild): enables the oscillator in the given slot and loads the factory script into it.
    if (const Json* q = recipe.get("lua")) {
        touched["LUA"] = true; Json& o = child(*child(layers, "LUA"), "oscs")->a[(size_t)q->num("slot", 0)];
        auto put = [](Json& obj, const char* k, Json v) { if (Json* c = child(obj, k)) *c = std::move(v); else obj.o.emplace_back(k, std::move(v)); };
        put(o, "enabled", jbool(true)); put(o, "level", jnum(q->num("level", 0.8))); put(o, "mono", jbool(q->num("mono", 0) != 0));
        put(o, "glideTime", jnum(q->num("glide", 0))); put(o, "pan", jnum(q->num("pan", 0))); put(o, "oct", jnum(q->num("oct", 0)));
        Json params = jobj(); if (const Json* pp = q->get("params")) params = *pp;
        Json lua = jobj(); lua.o = { {"id", jstr("factory." + q->str("id"))}, {"name", jstr(q->str("name"))}, {"src", jstr(q->str("src"))}, {"params", params}, {"seed", jnum(q->num("seed", 1))}, {"seedLock", jbool(true)}, {"dirty", jbool(false)}, {"version", jnum(1)} };
        put(o, "lua", std::move(lua));
    }
    if (const Json* q = recipe.get("xfx")) {
        const Json& pal = *q->get("palette"); const std::string mode = q->str("mode"); const double fill = q->num("fill", 1);
        for (auto& e : pal.a) {
            if (const Json* en = e.get("en")) for (auto& kv : en->o) if (kv.second.b) touched[kv.first] = true;
            if (const Json* o = e.get("o")) for (auto& pr : o->a) { const std::string L = pr.at(0).s.substr(0, pr.at(0).s.find('.')); if (child(layers, L.c_str())) touched[L] = true; }
        }
        const Json& rg = *q->get("range"); const int lo = (int)rg.at(0).n, hi = (int)rg.at(1).n; int cyc = 0; Json keys = jobj();
        for (int n = lo; n <= hi; n++) {
            if (sc.on && !inScale(sc, n)) continue;
            if (in.rng() > fill) continue;
            size_t pi;
            if (mode == "cycle") pi = (size_t)(cyc++ % (int)pal.a.size());
            else if (mode == "band") pi = (size_t)std::min((int)pal.a.size() - 1, (int)std::floor((double)(n - lo) / (double)(hi - lo + 1) * (double)pal.a.size()));
            else pi = (size_t)std::floor(in.rng() * (double)pal.a.size());
            const Json& e = pal.a[pi]; Json rec = jobj(); Json en = jobj(), ov = jobj();
            if (const Json* ee = e.get("en")) for (auto& kv : ee->o) en.o.emplace_back(kv.first, jbool(kv.second.b));
            if (const Json* oo = e.get("o")) for (auto& pr : oo->a) { Json v = in.resolve(pr.at(1)); bool found = false; for (auto& kv : ov.o) if (kv.first == pr.at(0).s) { kv.second = v; found = true; break; } if (!found) ov.o.emplace_back(pr.at(0).s, std::move(v)); }
            rec.o = { {"en", en}, {"o", ov} }; keys.o.emplace_back(std::to_string(n), std::move(rec));
        }
        Json banks = jarr();
        for (int i = 0; i < 8; i++) { Json b = jobj(); b.o = { {"name", jstr(i == 0 ? q->str("bank") : "Bank " + std::to_string(i + 1))}, {"keys", i == 0 ? keys : jobj()}, {"saved", Json()} }; banks.a.push_back(std::move(b)); }
        Json x = jobj(); x.o = { {"v", jnum(1)}, {"enabled", jbool(true)}, {"active", jnum(0)}, {"banks", banks} };
        Json ex = jobj(); ex.o.emplace_back("xfx", std::move(x));
        if (Json* old = child(data, "extra")) { Json* ox = child(*old, "xfx"); if (ox) *ox = ex.o[0].second; else old->o.emplace_back("xfx", ex.o[0].second); } else data.o.emplace_back("extra", std::move(ex));
    }
    // Drum-sequencer lanes (web genBuild): a recipe with "drumSeq" turns the sequencer on and routes its lanes to the XFX keys; any other XFX recipe gets an empty, switched-off sequencer.
    if (const Json* dq = recipe.get("drumSeq"); dq || recipe.get("xfx")) {
        Json lanes = jarr();
        for (int i = 0; i < 16; i++) {
            int len = 16; std::string pat; if (dq) if (const Json* ls = dq->get("lanes"); ls && i < (int)ls->a.size()) { len = (int)ls->a[(size_t)i].num("len", 16); pat = ls->a[(size_t)i].str("steps", ""); }
            Json steps = jarr(); const int ml = std::max(1, len);
            for (int k = 0; k < 32; k++) steps.a.push_back(jbool(dq && !pat.empty() && pat[(size_t)(k % ml) % pat.size()] == 'x' && (size_t)(k % ml) < pat.size() && k < ml));
            Json lane = jobj(); lane.o = { {"length", jnum(len ? len : 16)}, {"steps", std::move(steps)} }; lanes.a.push_back(std::move(lane));
        }
        Json ds = jobj(); ds.o = { {"on", jbool(dq != nullptr)}, {"target", jstr(dq ? "xfx" : "smp")}, {"xfxBase", jnum(dq ? dq->num("xfxBase", 36) : 36)}, {"lanes", std::move(lanes)} };
        if (Json* old = child(data, "extra")) { Json* od = child(*old, "drumSeq"); if (od) *od = std::move(ds); else old->o.emplace_back("drumSeq", std::move(ds)); }
        else { Json ex = jobj(); ex.o.emplace_back("drumSeq", std::move(ds)); data.o.emplace_back("extra", std::move(ex)); }
    }
    // Clip-launcher sequences (web genBuild): "clips" fills Bank 1 with generated XFX clips (same random-walk melody generator and rng order as the web).
    if (const Json* q = recipe.get("clips")) {
        std::vector<int> pool; for (auto& v : q->get("pitches")->a) if (!sc.on || inScale(sc, (int)v.n)) pool.push_back((int)v.n);
        const Json& bars_ = *q->get("bars"); const Json& grid_ = *q->get("grid"); const Json& dens_ = *q->get("density"); const int count = (int)q->num("count", 8);
        Json slots = jarr(); for (int i = 0; i < 8; i++) slots.a.push_back(Json());
        const Json* names = q->get("names"); const double leap = q->num("leap", 0), ratchet = q->num("ratchet", 0);
        int last = pool.empty() ? 0 : pool[0];
        for (int ci = 0; ci < count && ci < 8; ci++) {
            const double bars = bars_.a[(size_t)ci % bars_.a.size()].n, grid = grid_.a[(size_t)ci % grid_.a.size()].n; const int cells = (int)std::floor(bars * 4.0 / grid + 0.5);
            Json notes = jarr(); const double dens = dens_.at(0).n + in.rng() * (dens_.at(1).n - dens_.at(0).n);
            last = pool[(size_t)std::floor(in.rng() * (double)pool.size())];
            for (int k = 0; k < cells; k++) {
                if (k != 0 && in.rng() > dens) continue;
                if (in.rng() < leap) last = pool[(size_t)std::floor(in.rng() * (double)pool.size())];
                else { int pos = 0; for (size_t z = 0; z < pool.size(); z++) if (pool[z] == last) { pos = (int)z; break; }
                       const double sgn = in.rng() < 0.5 ? -1 : 1; const int step = 1 + (int)std::floor(in.rng() * 2); const int ix = std::max(0, std::min((int)pool.size() - 1, pos + (int)sgn * step)); last = pool[(size_t)ix]; }
                const int run = in.rng() < ratchet ? 2 : 1;
                for (int r = 0; r < run; r++) { const double t = k * grid + r * grid / run; if (t >= bars * 4) break;
                    Json nt = jobj(); const double l = r4(grid / run * 0.8), v = r4(0.55 + in.rng() * 0.4); nt.o = { {"t", jnum(r4(t))}, {"l", jnum(l)}, {"p", jnum(last)}, {"v", jnum(v)} }; notes.a.push_back(std::move(nt)); }
            }
            Json sl = jobj(); const std::string nm = (names && (size_t)ci < names->a.size() && !names->a[(size_t)ci].s.empty()) ? names->a[(size_t)ci].s : "IDM " + std::to_string(ci + 1);
            sl.o = { {"id", jstr("c" + std::to_string(ci + 1))}, {"name", jstr(nm)}, {"color", jstr("#e2a04a")}, {"layer", jstr("XFX")}, {"bars", jnum(bars)}, {"notes", std::move(notes)}, {"tr", jnum(0)}, {"auto", jobj()}, {"factory", jbool(true)} };
            slots.a[(size_t)ci] = std::move(sl);
        }
        Json banks = jarr();
        for (int b = 0; b < 8; b++) { Json bk = jobj(); Json sl = jarr(); if (b == 0) sl = slots; else for (int i = 0; i < 8; i++) sl.a.push_back(Json());
            bk.o = { {"name", jstr(b == 0 ? "XFX CLIPS" : "BANK " + std::to_string(b + 1))}, {"color", jstr("#e2a04a")}, {"slots", std::move(sl)} }; banks.a.push_back(std::move(bk)); }
        Json sel = jobj(); sel.o = { {"b", jnum(0)}, {"s", jnum(0)} };
        Json cl = jobj(); cl.o = { {"v", jnum(1)}, {"quant", jstr(q->str("quant", "1/4"))}, {"mode", jstr("replace")}, {"bank", jnum(0)}, {"sel", std::move(sel)}, {"banks", std::move(banks)}, {"nid", jnum(count + 1)} };
        if (Json* old = child(data, "extra")) { if (Json* oc = child(*old, "clips")) *oc = std::move(cl); else old->o.emplace_back("clips", std::move(cl)); }
        else { Json ex = jobj(); ex.o.emplace_back("clips", std::move(cl)); data.o.emplace_back("extra", std::move(ex)); }
    }
    // The sound you hear must be the one generated: the default preset has Wavetable OSC1 sounding, so mute it when the recipe does not use that layer;
    // switch FM engine A on when the recipe uses FM.
    if (!touched["WT"]) *child(child(*child(layers, "WT"), "oscs")->a[0], "level") = jnum(0);
    if (touched["FM"]) { Json& ee = *child(*child(layers, "FM"), "engineEnabled"); if (!(ee.at(0).b || ee.at(1).b)) ee.a[0] = jbool(true); }
    std::string notes = recipe.str("desc"); if (sc.on) notes += " Pitches follow the " + sc.label + " scale (root " + std::string(sc.root >= 0 && sc.root < 12 ? kNoteNames[sc.root] : "C") + ") because Scale lock is on.";
    bool hasN = false; for (auto& kv : data.o) if (kv.first == "notes") { kv.second = jstr(notes); hasN = true; } if (!hasN) data.o.emplace_back("notes", jstr(notes));
    if (warnOut) *warnOut = in.warn;
    return data;
}
} // namespace detail

inline const Json& recipesRoot() { static Json r = JsonParser::parse(kGenRecipes); return r; }
inline std::vector<std::string> generatableCategories() { std::vector<std::string> c; if (const Json* cats = recipesRoot().get("cats")) for (auto& kv : cats->o) c.push_back(kv.first); return c; }
inline bool isGeneratable(const std::string& c) { const Json* cats = recipesRoot().get("cats"); return cats && cats->get(c.c_str()) && cats->get(c.c_str())->size() > 0; }
inline size_t variantIndex(size_t n, uint32_t seed) { detail::Mul m(seed ^ 0x9E3779B9u); return (size_t)std::floor(m() * (double)n); }

// Builds a complete preset (INIT + recipe) for `cat`. The variant is chosen from the seed. Returns false for an unknown category.
inline bool build(Json& out, const std::string& cat, uint32_t seed, const ScaleCtx& sc, GenInfo* info = nullptr, std::vector<std::string>* warn = nullptr, int forceVariant = -1) {
    const Json& R = recipesRoot(); const Json* cats = R.get("cats"); const Json* vs = cats ? cats->get(cat.c_str()) : nullptr;
    if (!vs || !vs->size()) return false;
    const size_t vi = forceVariant >= 0 ? (size_t)forceVariant % vs->size() : variantIndex(vs->size(), seed);
    const Json& v = vs->a[vi];
    out = detail::buildOne(v, *R.get("fxDefaults"), seed, sc, warn);
    if (info) { info->name = v.str("name"); info->view = v.str("view"); info->category = cat; info->seed = seed; }
    return true;
}
// The four fixed XFX presets shipped in the DMT CYMATICS bank (built by the same interpreter with a fixed seed).
inline size_t namedCount() { const Json* n = recipesRoot().get("named"); return n ? n->size() : 0; }
inline bool buildNamed(Json& out, size_t i, GenInfo* info = nullptr, std::vector<std::string>* warn = nullptr) {
    const Json& R = recipesRoot(); const Json* n = R.get("named"); if (!n || i >= n->size()) return false;
    const Json& v = n->a[i]; ScaleCtx off;
    out = detail::buildOne(v, *R.get("fxDefaults"), (uint32_t)v.num("seed", 1), off, warn);
    if (info) { info->name = v.str("name"); info->view = v.str("view"); info->category = v.str("cat"); info->seed = (uint32_t)v.num("seed", 1); }
    return true;
}

// MODIFY: nudges every sound-shape number in the Wavetable and FM layers by up to +-10 %.
inline bool skipKey(const std::string& k) {
    static const char* skip[] = { "rangeLow", "rangeHigh", "lastPlayedFreq", "fmAlgorithms", "chainOrder", "bankOrder", "chainPos", "currentSlot", "seqStepIdx", "seqClockAcc", "length", "transientIdx", "ratchet", "engineEnabled" };
    for (auto s : skip) if (k == s) return true; return false;
}
inline void nudge(Json& j, Rng& r) {
    if (j.type == Json::Num) { j.n *= 1.0 + (r() * 0.2 - 0.1); return; }
    if (j.type == Json::Arr) { for (auto& e : j.a) nudge(e, r); return; }
    if (j.type == Json::Obj) for (auto& kv : j.o) if (!skipKey(kv.first)) nudge(kv.second, r);
}
inline bool modify(Json& root, Rng& r) {
    bool any = false;
    for (const char* key : { "layers.FM", "layers.WT" }) if (Json* l = jsonPathPtr(root, key, false)) { nudge(*l, r); any = true; }
    // Advanced arp: also evolves the arp pattern - Mutate (re-rolls some steps) + the 0..1 generative settings nudged by up to +-10 % (mirrors the web modifyCurrentPreset).
    if (Json* arp = jsonPathPtr(root, "arp", false)) {
        const Json* on = arp->get("on"); const Json* mode = arp->get("mode");
        if (on && on->asNum(0) != 0 && mode && mode->type == Json::Str && mode->s == "advanced") if (Json* adv = jsonPathPtr(root, "arp.adv", false)) {
            auto field = [&](const char* k) -> Json* { for (auto& kv : adv->o) if (kv.first == k) return &kv.second; return nullptr; };
            auto setNum = [&](const char* k, double v) { if (Json* f = field(k)) { f->type = Json::Num; f->n = v; } else { Json n; n.type = Json::Num; n.n = v; adv->o.emplace_back(k, n); } };
            auto numOf = [&](const char* k, double d) { Json* f = field(k); return f && f->type == Json::Num ? f->n : d; };
            setNum("mutSalt", numOf("mutSalt", 0) + 1); setNum("mutAmt", std::max(0.15, numOf("mutation", 0.2)));
            static const char* ks[] = { "density", "complexity", "mutation", "variation", "randomness", "humanise", "octSpread", "ratchet", "velVar", "accent", "gateVar", "groove", "cycleVar", "skipProb", "rhythmDisp" };
            const double defs[] = { 0.6, 0.5, 0.2, 0, 0, 0, 0, 0, 0, 0.3, 0, 0.5, 0, 0, 0 };
            for (int i = 0; i < 15; i++) setNum(ks[i], std::max(0.0, std::min(1.0, numOf(ks[i], defs[i]) * (1.0 + (r() * 0.2 - 0.1)))));
            setNum("probability", std::max(0.5, std::min(1.0, numOf("probability", 1) * (1.0 + (r() * 0.2 - 0.1))))); any = true;
        }
    }
    return any;
}

} }
