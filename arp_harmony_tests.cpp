// Arp-aware RANDOMISE: parity with the web implementation (golden dump) + musical / safety properties + the lock-free snapshot through Performance.
#include "vv/arp_harmony.hpp"
#include "vv/json.hpp"
#include "vv/performance.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace vv;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
int main() {
    std::ifstream f("tests/data/arp_harmony_golden.json"); if (!f) f.open("../tests/data/arp_harmony_golden.json"); if (!f) { printf("FAIL golden file missing\n"); return 1; }
    std::stringstream ss; ss << f.rdbuf(); Json g = JsonParser::parse(ss.str()); int n = 0;
    for (auto& c : g.a) {
        std::vector<int> held; for (auto& x : c.get("held")->a) held.push_back((int)x.asNum());
        const harmony::Palette p = harmony::analyse(held.data(), (int)held.size(), (int)c.num("tr", 0), (int)c.num("adv", 0), (int)c.num("kb", 0)); const Json* pj = c.get("pal");
        CHECK(p.chord == (int)pj->num("chord", -1) && p.scale == (int)pj->num("scale", -1) && p.root == (int)pj->num("root", -1) && std::string(p.name) == pj->str("name", ""), "palette case %d: %s vs %s", n, p.name, pj->str("name", "").c_str());
        harmony::Rng rng{(uint32_t)(1000 + n)}; int prev = -1; int bad = 0; const Json* pk = c.get("picks");
        for (int i = 0; i < 40; i++) { prev = harmony::pick(p, prev, i % 4 == 0, rng, 48, 76); if (prev != (int)pk->a[(size_t)i].asNum()) bad++; }
        CHECK(bad == 0, "pick sequence case %d: %d differ", n, bad); n++;
    }
    printf("harmony golden cases: %d\n", n);
    // every pick is inside the palette (scale or chord) and the range; chord tones dominate strong beats
    { int held[3] = {60, 64, 67}; const auto p = harmony::analyse(held, 3, 0, 0, 0); harmony::Rng rng{42}; int prev = -1, outside = 0, chordStrong = 0, strongN = 0, range = 0;
      for (int i = 0; i < 4000; i++) { const bool strong = i % 4 == 0; prev = harmony::pick(p, prev, strong, rng, 48, 76); const int pc = prev % 12; if (!(((p.scale | p.chord) >> pc) & 1)) outside++; if (prev < 48 || prev > 76) range++; if (strong) { strongN++; if ((p.chord >> pc) & 1) chordStrong++; } }
      CHECK(outside == 0 && range == 0, "picks stay in key (%d outside) and range (%d)", outside, range); CHECK(chordStrong > strongN * 0.6, "chord tones dominate strong beats (%d of %d)", chordStrong, strongN); }
    // transposition moves the harmony with the arp
    { int held[3] = {60, 64, 67}; const auto p = harmony::analyse(held, 3, 2, 0, 0); CHECK(p.root == 2 && std::string(p.name) == "D major", "transpose +2 gives %s", p.name); }
    // no scale contains a chromatic cluster -> only chord tones + 4th/5th, never crashes
    { int held[4] = {60, 61, 62, 63}; const auto p = harmony::analyse(held, 4, 0, 0, 0); harmony::Rng rng{7}; int prev = -1, outside = 0; for (int i = 0; i < 500; i++) { prev = harmony::pick(p, prev, i % 2 == 0, rng, 36, 84); if (!(((p.scale | p.chord) >> (prev % 12)) & 1)) outside++; } CHECK(p.active && outside == 0, "cluster palette safe (%s, %d outside)", p.name, outside); }
    // empty / edge inputs
    { const auto p = harmony::analyse(nullptr, 0, 0, 0, 0); CHECK(!p.active, "no notes -> inactive"); harmony::Palette q; q.active = true; harmony::Rng r{1}; CHECK(harmony::pick(q, -1, true, r, 60, 60) == 60, "degenerate range"); }
    // Performance: snapshot is published from the audio side and read lock-free; fit only when the arp is playing
    { Synth syn; Performance perf; syn.prepare(48000);
      auto pr = parsePresetText("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8}]}},\"bpm\":120,\"arp\":{\"on\":true,\"order\":\"UP\",\"transpose\":0}}");
      syn.tickSeconds = 1e9; syn.setPreset(pr); perf.attach(&syn); perf.setPreset(pr); int out[16]; uint8_t strong[16]; for (int i = 0; i < 16; i++) strong[i] = i % 4 == 0;
      std::vector<float> L(128), R(128);
      perf.advance(syn.clock(), 128); CHECK(!perf.arpFitPitches(16, strong, 0, 1, 48, 76, out), "no held notes -> not fitted");
      perf.noteDown(57, 0.8); perf.noteDown(60, 0.8); perf.noteDown(64, 0.8); perf.advance(syn.clock(), 128); syn.process(L.data(), R.data(), 128);
      harmony::Palette pal; const bool ok = perf.arpFitPitches(16, strong, 0, 5, 48, 76, out, &pal); CHECK(ok && std::string(pal.name) == "A minor", "fits held arp chord (%s)", pal.name);
      int outside = 0; for (int i = 0; i < 16; i++) if (!(((pal.scale | pal.chord) >> (out[i] % 12)) & 1)) outside++; CHECK(outside == 0, "fitted notes in key");
      perf.setArpOn(false); perf.advance(syn.clock(), 128); CHECK(!perf.arpFitPitches(16, strong, 0, 5, 48, 76, out), "arp off -> not fitted"); }
    printf(failures ? "arp harmony tests: %d FAILED\n" : "arp harmony tests: all passed\n", failures); return failures ? 1 : 0;
}
