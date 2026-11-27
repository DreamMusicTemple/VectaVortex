// Chord maker: (1) the C++ generator matches the JavaScript reference bit for bit (golden file written by tools/test_chordmaker.js),
// (2) musical properties, (3) the engine API (generate / mutate / lock / fit via the C ABI).
#include <cstdio>
#include <fstream>
#include <sstream>
#include <set>
#include <cstring>
#include "vv/model.hpp"
#include "vv_engine.h"
using namespace vv;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static std::vector<cm::Chord> chordsOf(const Json& a) {
    std::vector<cm::Chord> v; for (auto& x : a.a) { cm::Chord c; c.deg = (int)x.num("deg", 0); c.rest = x.boolean("rest", false); c.locked = x.boolean("locked", false); if (auto t = x.get("tones")) for (auto& n : t->a) c.tones.push_back((int)n.asNum()); v.push_back(c); } return v;
}
static bool same(const std::vector<cm::Chord>& a, const std::vector<cm::Chord>& b) {
    if (a.size() != b.size()) return false; for (size_t i = 0; i < a.size(); i++) if (a[i].deg != b[i].deg || a[i].rest != b[i].rest || a[i].tones != b[i].tones) return false; return true;
}
static void testGolden() {
    std::ifstream f("tests/chordmaker_golden.json"); if (!f) { f.open("../tests/chordmaker_golden.json"); } if (!f) { failures++; printf("FAIL golden file missing\n"); return; }
    std::stringstream ss; ss << f.rdbuf(); Json g = JsonParser::parse(ss.str());
    int n = 0;
    for (auto& e : g.a) {
        cm::Cfg c = modelio::chordCfg(&*e.get("cfg")); const uint32_t seed = (uint32_t)e.num("seed", 0);
        cm::Rng r(seed); auto prog = cm::generate(c, r, {});
        CHECK(same(prog, chordsOf(*e.get("prog"))), "generate parity case %d (%s)", n, c.scale.c_str());
        auto m = prog; cm::Rng r2(99); cm::mutate(m, c, r2, 0.5); CHECK(same(m, chordsOf(*e.get("mutated"))), "mutate parity case %d", n);
        cm::Cfg c2 = c; c2.randomness = 0.8; cm::Rng r3(7); int notes[16]; const int nn = cm::playNotes(prog[0], c2, r3, notes, 16);
        const Json* pl = e.get("play"); bool ok = pl && (int)pl->a.size() == nn; if (ok) for (int i = 0; i < nn; i++) if ((int)pl->a[(size_t)i].asNum() != notes[i]) ok = false; CHECK(ok, "playNotes parity case %d", n);
        const Json* nm = e.get("names"); bool nok = nm && nm->a.size() == prog.size(); if (nok) for (size_t i = 0; i < prog.size(); i++) if (nm->a[i].s != cm::chordName(prog[i], c)) nok = false; CHECK(nok, "chord names parity case %d", n);
        n++;
    }
    printf("golden cases compared: %d\n", n);
}
static void testProperties() {
    // every note of every scale stays in key / mode
    int checked = 0;
    for (int si = 0; si < cm::kNumScales; si++) for (int key = 0; key < 12; key += 5) for (int seed = 1; seed <= 6; seed++) {
        cm::Cfg c; c.scale = cm::kScales[si].id; c.key = key; c.naturalness = (seed % 3) * 0.5; c.density = seed / 6.0; cm::Rng r((uint32_t)(seed * 7919 + key)); auto p = cm::generate(c, r, {});
        const auto& S = cm::kScales[si]; CHECK((int)p.size() == 8, "length");
        for (auto& ch : p) { if (ch.rest) continue; for (int t : ch.tones) { const int pc = (((cm::midiOf(S, c, t) - key) % 12) + 12) % 12; bool in = false; for (int k = 0; k < S.n; k++) if (S.iv[k] == pc) in = true; CHECK(in, "out of scale"); checked++; } }
        cm::Rng r2(5); c.randomness = 1; int notes[16]; const int nn = cm::playNotes(p[0], c, r2, notes, 16);
        for (int i = 0; i < nn; i++) { const int pc = (((notes[i] - key) % 12) + 12) % 12; bool in = false; for (int k = 0; k < S.n; k++) if (S.iv[k] == pc) in = true; CHECK(in, "random note out of scale"); }
    }
    // always original: a rolling history prevents repeats
    std::vector<std::string> hist; std::set<std::string> seen; cm::Cfg c;
    for (int s = 0; s < 100; s++) { cm::Rng r((uint32_t)(1000 + s * 3)); auto p = cm::generate(c, r, hist); std::string k; for (auto& ch : p) { k += std::to_string(ch.deg) + ":"; for (int t : ch.tones) k += std::to_string(t) + ","; k += ";"; } seen.insert(k); hist.push_back(cm::signature(p)); if (hist.size() > 16) hist.erase(hist.begin()); }
    CHECK(seen.size() >= 97, "originality %zu", seen.size());
    // euclid
    auto e = cm::euclid(3, 8, 0); const bool tres = e[0] && !e[1] && !e[2] && e[3] && !e[4] && !e[5] && e[6] && !e[7]; CHECK(tres, "E(3,8)");
    printf("musical property checks: %d notes in scale\n", checked);
}
static std::string fetchInfo(VVEngine* e) { std::vector<char> b(vv_engine_chord_info(e, nullptr, 0) + 1); vv_engine_chord_info(e, b.data(), b.size()); return std::string(b.data()); }
static void testEngineApi() {
    VVEngine* e = vv_engine_create(48000);
    std::vector<unsigned char> wt(8 + 256 * 4); { uint32_t fc = 1, fs = 256; memcpy(wt.data(), &fc, 4); memcpy(wt.data() + 4, &fs, 4); for (int i = 0; i < 256; i++) { float x = (float)std::sin(6.2831853 * i / 256); memcpy(wt.data() + 8 + i * 4, &x, 4); } }
    vv_engine_load_wavetable(e, "analogMorph", wt.data(), wt.size());
    const char* pj = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8}]}},\"bpm\":120,\"chordMaker\":{\"on\":true,\"scale\":\"hijaz\",\"key\":2,\"steps\":6}}";
    CHECK(vv_engine_load_preset_json(e, pj, strlen(pj)) == 1, "load");
    CHECK(vv_engine_chord_action(e, "generate", 0, 0) == 1, "generate");
    Json a = JsonParser::parse(fetchInfo(e)); CHECK(a.a.size() == 6, "6 chords (%zu)", a.a.size());
    const std::string first = fetchInfo(e); vv_engine_chord_action(e, "generate", 0, 0); CHECK(fetchInfo(e) != first, "a second Generate gives a different progression");
    vv_engine_chord_action(e, "lock", 2, 0); Json b0 = JsonParser::parse(fetchInfo(e)); const std::string lockedName = b0.a[2].str("name");
    CHECK(b0.a[2].boolean("locked", false), "locked flag");
    for (int i = 0; i < 5; i++) vv_engine_chord_action(e, "mutate", 0, 0);
    Json b1 = JsonParser::parse(fetchInfo(e)); CHECK(b1.a[2].str("name") == lockedName, "locked chord survives mutate (%s vs %s)", lockedName.c_str(), b1.a[2].str("name").c_str());
    // the stored progression round-trips through the preset JSON
    size_t n = vv_engine_get_value(e, "chordMaker.progression", nullptr, 0); CHECK(n > 20, "progression stored in the preset");
    vv_engine_set_value(e, "chordMaker.steps", "10"); vv_engine_chord_action(e, "fit", 0, 0); CHECK(JsonParser::parse(fetchInfo(e)).a.size() == 10, "fit to 10 chords");
    vv_engine_set_value(e, "chordMaker.euclid", "{\"on\":true,\"pulses\":3,\"steps\":8,\"rotate\":0}"); vv_engine_chord_action(e, "fit", 0, 0); CHECK(JsonParser::parse(fetchInfo(e)).a.size() == 3, "euclid: one chord per pulse");
    // plays: start the transport, render 3 s, chords must sound
    vv_engine_start_sequencer(e, 1); std::vector<float> L(480), R(480); double en = 0;
    for (int i = 0; i < 300; i++) { vv_engine_render(e, L.data(), R.data(), 480); for (float x : L) en += (double)x * x; }
    CHECK(en > 1.0, "chord maker produces sound on the transport (energy %.3f)", en);
    CHECK(vv_engine_chord_playing(e) >= 0, "a chord is marked as playing");
    vv_engine_destroy(e);
}
int main() { testGolden(); testProperties(); testEngineApi(); printf(failures ? "chordmaker tests: %d FAILED\n" : "chordmaker tests: all passed\n", failures); return failures ? 1 : 0; }
