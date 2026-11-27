// Rotary Speaker / Amp + Cab / Tube Overdrive: golden parity with the real web worklets (tools/make_golden_rotamp.js) + safety properties.
#include "vv/fx_basic.hpp"
#include "vv/fx_chain.hpp"
#include "vv/json.hpp"
#include "vv/rotary_amp.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace vv;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
static double P(const Json& p, const char* k, double d) { return p.num(k, d); }
int main() {
    std::ifstream f("tests/data/rotary_amp_golden.json"); if (!f) f.open("../tests/data/rotary_amp_golden.json"); if (!f) { printf("FAIL golden missing\n"); return 1; }
    std::stringstream ss; ss << f.rdbuf(); const Json g = JsonParser::parse(ss.str()); const double sr = 48000;
    for (const Json& c : g.a) {
        const std::string proc = c.str("proc", ""), name = c.str("name", ""); const Json& p = *c.get("params"); const int frames = (int)c.num("frames", 0), keep = (int)c.num("keep", 16); const uint32_t seed = (uint32_t)c.num("seed", 1);
        std::vector<float> in((size_t)frames); { uint32_t s = seed; for (int i = 0; i < frames; i++) { s = s * 1664525u + 1013904223u; const double v = ((double)s / 4294967296.0 * 2 - 1) * 0.25 + 0.2 * std::sin(2 * 3.141592653589793 * 150 * i / sr); in[(size_t)i] = (float)v; } }
        std::vector<float> L((size_t)frames), R((size_t)frames);
        if (proc == "rotary-processor") { rotamp::Rotary r; r.prepare(sr); r.setParams(P(p, "speed", 0), P(p, "drive", 0.3), P(p, "balance", 0.5), P(p, "depth", 0.7), P(p, "width", 0.8), P(p, "accel", 1), P(p, "mix", 1), P(p, "level", 0)); for (int i = 0; i < frames; i++) { r.tick(in[(size_t)i]); L[(size_t)i] = (float)r.oL; R[(size_t)i] = (float)r.oR; } }
        else if (proc == "amp-processor") { rotamp::Amp a; a.prepare(sr); a.setParams(P(p, "model", 0), P(p, "gain", 0.4), P(p, "bass", 0.5), P(p, "mid", 0.5), P(p, "treble", 0.5), P(p, "presence", 0.4), P(p, "master", 0.5), P(p, "cab", 1), P(p, "chorus", 0.5), P(p, "mix", 1), P(p, "level", 0)); for (int i = 0; i < frames; i++) { a.tick(in[(size_t)i]); L[(size_t)i] = (float)a.oL; R[(size_t)i] = (float)a.oR; } }
        else { Saturation s; s.prepare(sr); Saturation::Params q; q.drive = P(p, "drive", 0.4); q.hard = (int)P(p, "hard", 0); q.tone = P(p, "tone", 6000); q.mix = P(p, "mix", 0.5); q.oversample = (int)P(p, "oversample", 0); s.process(in.data(), L.data(), frames, q); }
        const auto& chs = c.get("ch")->a; double worst = 0, peak = 0; int bad = 0;
        for (size_t ch = 0; ch < chs.size(); ch++) { const std::vector<float>& o = ch == 0 ? L : R; for (size_t k = 0; k < chs[ch].a.size(); k++) { const double want = chs[ch].a[k].asNum(), got = o[k * (size_t)keep]; peak = std::max(peak, std::fabs(want)); const double d = std::fabs(want - got); worst = std::max(worst, d); if (d > 2e-4 + 2e-4 * std::fabs(want)) bad++; } }
        CHECK(bad == 0, "%s: %d samples differ (worst %.3g, peak %.3f)", name.c_str(), bad, worst, peak);
        printf("%-16s worst diff %.2e (peak %.3f)\n", name.c_str(), worst, peak);
    }
    // safety: extreme settings never produce NaN / runaway, and silence in = (near) silence out for the amps
    { rotamp::Amp a; a.prepare(48000); a.setParams(3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 12); double pk = 0; bool ok = true; for (int i = 0; i < 96000; i++) { a.tick(std::sin(i * 0.31)); pk = std::max(pk, std::fabs(a.oL)); if (!std::isfinite(a.oL) || !std::isfinite(a.oR)) ok = false; } CHECK(ok && pk < 20, "amp extreme: finite=%d peak=%.2f", ok, pk); }
    for (int m = 0; m < 4; m++) { rotamp::Amp a; a.prepare(48000); a.setParams(m, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 1, 0.5, 1, 0); double pk = 0; for (int i = 0; i < 48000; i++) { a.tick(0); pk = std::max(pk, std::fabs(a.oL)); } CHECK(pk < 1e-6, "amp %d is silent with silence in (%.3g)", m, pk); }
    { rotamp::Rotary r; r.prepare(48000); r.setParams(1, 1, 0.5, 1, 1, 4, 1, 12); bool ok = true; for (int i = 0; i < 96000; i++) { r.tick(std::sin(i * 0.2) * 1.0); if (!std::isfinite(r.oL) || !std::isfinite(r.oR) || std::fabs(r.oL) > 20) ok = false; } CHECK(ok, "rotary extreme stays finite"); }
    // Stage level: configure from JSON, 128-sample quanta, unknown strings fall back to defaults
    { RotaryStage rs(48000); AmpStage as(48000); auto* st = &rs; auto* sa = &as;
      { { FxEnv e; Json pj = JsonParser::parse("{\"speed\":\"fast\",\"drive\":0.4}"), aj = JsonParser::parse("{\"model\":\"brit\",\"gain\":0.8,\"cab\":true}"); st->configure(pj, true, e); sa->configure(aj, true, e);
        std::vector<float> in(128), oL(128), oR(128); double e1 = 0, e2 = 0; for (int b = 0; b < 200; b++) { for (int i = 0; i < 128; i++) in[(size_t)i] = 0.3f * std::sin((float)(b * 128 + i) * 0.05f); st->process(in.data(), in.data(), oL.data(), oR.data(), 128); for (int i = 0; i < 128; i++) e1 += (double)oL[(size_t)i] * oL[(size_t)i] + (double)oR[(size_t)i] * oR[(size_t)i]; sa->process(in.data(), in.data(), oL.data(), oR.data(), 128); for (int i = 0; i < 128; i++) e2 += (double)oL[(size_t)i] * oL[(size_t)i]; }
        CHECK(e1 > 1 && e2 > 1 && std::isfinite(e1) && std::isfinite(e2), "stages make sound (%.2f %.2f)", e1, e2); } } }
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("rotary_amp tests: all passed\n"); return 0;
}
