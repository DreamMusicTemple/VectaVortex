// Extended Granular FX: behaviour, stability, determinism and parity with the web worklet (see granular_x_parity.js).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <vv/fx_chain.hpp>
using namespace vv;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

struct Scn { const char* name; GranularX::P x; GranularX::Base b; };
static std::vector<Scn> scenarios() {
    std::vector<Scn> v; GranularX::P d; GranularX::Base b;
    for (int m = 1; m <= 13; m++) { GranularX::P x = d; x.mode = m; char* n = new char[24]; std::snprintf(n, 24, "mode%d", m); v.push_back({ n, x, b }); }
    { GranularX::P x = d; x.fine = 10; v.push_back({ "fine", x, b }); }
    { GranularX::P x = d; x.window = 2; x.atk = 0.1; x.rel = 0.2; x.symmetry = 0.7; x.curve = 0.3; v.push_back({ "window", x, b }); }
    { GranularX::P x = d; x.fb = 0.8; x.fbDamp = 0.5; x.fbLow = 0.2; x.fbSat = 0.4; x.fbDiff = 0.7; x.pingPong = 0.6; v.push_back({ "feedback", x, b }); }
    for (int t = 0; t <= 5; t++) { GranularX::P x = d; x.rndType = t; x.pitchRand = 5; x.volRand = 0.5; x.sizeRand = 0.4; x.posRand = 3; x.jitter = 0.4; x.rndRate = 4; x.seed = 7; char* n = new char[24]; std::snprintf(n, 24, "rnd%d", t); v.push_back({ n, x, b }); }
    { GranularX::P x = d; x.sync = 5; x.delaySync = 4; x.lrSpread = 12; x.width = 1.5; x.drift = 0.5; x.autoGain = true; GranularX::Base bb = b; bb.bpm = 133; v.push_back({ "sync", x, bb }); }
    { GranularX::P x = d; x.freeze = true; x.fine = 1; v.push_back({ "freeze", x, b }); }
    return v;
}
static void makeInput(std::vector<float>& in, int n, double sr) {
    in.resize(n); uint32_t s = 77; for (int i = 0; i < n; i++) { s = s * 1664525u + 1013904223u; const double nz = (double)s / 4294967296.0 * 2 - 1;
        in[i] = (float)(0.35 * std::sin(6.283185307179586 * 220 * i / sr) * (i % 24000 < 12000 ? 1 : 0.4) + 0.12 * std::sin(6.283185307179586 * 1375.3 * i / sr) + 0.05 * nz); }
}
static void runX(GranularX& g, const GranularX::P& x, const GranularX::Base& b, const std::vector<float>& in, std::vector<float>& L, std::vector<float>& R) {
    g.set(x); const int n = (int)in.size(); L.assign(n, 0); R.assign(n, 0);
    for (int i = 0; i < n; i += 128) { const int k = std::min(128, n - i); if (g.busy()) g.process(&in[i], &L[i], &R[i], k, b); else g.prime(&in[i], k); }
}

int main(int argc, char** argv) {
    const double sr = 48000; std::vector<float> in; makeInput(in, 48000 * 3, sr);
    // parity dump for the JavaScript comparison:  vv_granular_x_tests --dump <dir>
    if (argc > 2 && !std::strcmp(argv[1], "--dump")) {
        for (auto& s : scenarios()) { GranularX g; g.prepare(sr, 1234); std::vector<float> L, R; runX(g, s.x, s.b, in, L, R);
            const std::string fn = std::string(argv[2]) + "/" + s.name + ".f32"; FILE* f = std::fopen(fn.c_str(), "wb"); std::fwrite(L.data(), 4, L.size(), f); std::fwrite(R.data(), 4, R.size(), f); std::fclose(f); }
        std::printf("dumped\n"); return 0;
    }
    // 1) every scenario: finite, bounded, produces sound, no zipper clicks
    for (auto& s : scenarios()) { GranularX g; g.prepare(sr, 1234); std::vector<float> L, R; runX(g, s.x, s.b, in, L, R);
        double pk = 0, e = 0, jump = 0; bool bad = false; for (size_t i = 0; i < L.size(); i++) { if (!std::isfinite(L[i]) || !std::isfinite(R[i])) bad = true; pk = std::max(pk, (double)std::fabs(L[i])); e += (double)L[i] * L[i]; if (i) jump = std::max(jump, (double)std::fabs(L[i] - L[i - 1])); }
        CHECK(!bad && pk <= 2.01, "%s: non-finite or too loud (peak %.3f)", s.name, pk);
        CHECK(e > 1e-3 || !std::strcmp(s.name, "freeze") || !std::strcmp(s.name, "mode2"), "%s: silent", s.name);   // freezing an empty history is silent by design
        CHECK(jump < 1.2, "%s: discontinuity %.3f", s.name, jump); }
    // 2) determinism and seed
    { GranularX a, b2, c; a.prepare(sr, 5); b2.prepare(sr, 5); c.prepare(sr, 5); GranularX::P x; x.mode = 6; GranularX::Base bs; std::vector<float> L1, R1, L2, R2, L3, R3; runX(a, x, bs, in, L1, R1); runX(b2, x, bs, in, L2, R2); x.seed = 3; runX(c, x, bs, in, L3, R3);
      CHECK(L1 == L2, "same seed must reproduce exactly"); CHECK(L1 != L3, "different seed must change the pattern"); }
    // 3) feedback worst case stays bounded; tail dies
    { GranularX g; g.prepare(sr, 1); GranularX::P x; x.mode = 6; x.fb = 0.95; x.fbDiff = 1; x.pingPong = 1; GranularX::Base bs; bs.density = 100; bs.grainSize = 500; std::vector<float> n8; n8.resize(48000 * 8); uint32_t s = 3; for (auto& v : n8) { s = s * 1664525u + 1013904223u; v = (float)((s / 4294967296.0 * 2 - 1) * 0.5); }
      std::vector<float> L, R; runX(g, x, bs, n8, L, R); double pk = 0; bool bad = false; for (float v : L) { if (!std::isfinite(v)) bad = true; pk = std::max(pk, (double)std::fabs(v)); } CHECK(!bad && pk <= 2.01, "feedback worst case unstable %.3f", pk); }
    { GranularX g; g.prepare(sr, 1); GranularX::P x; x.fb = 0.6; x.fbDamp = 0.4; GranularX::Base bs; bs.density = 10; bs.grainSize = 100; std::vector<float> sig(48000 * 8, 0.f); for (int i = 0; i < 24000; i++) sig[i] = (float)(0.5 * std::sin(i * 0.05));
      std::vector<float> L, R; runX(g, x, bs, sig, L, R); double e = 0; for (size_t i = 48000 * 6; i < L.size(); i++) e += (double)L[i] * L[i]; CHECK(std::sqrt(e / 96000) < 1e-4, "feedback tail did not decay (%.2e)", std::sqrt(e / 96000)); }
    // freeze while sound is playing keeps it sounding after the input stops
    { GranularX g; g.prepare(sr, 1); GranularX::Base bs; GranularX::P x; x.fine = 1; std::vector<float> sig(48000 * 4, 0.f), L(48000 * 4), R(48000 * 4); for (int i = 0; i < 48000; i++) sig[i] = (float)(0.5 * std::sin(i * 0.044));
      g.set(x); for (int i = 0; i < 48000 * 4; i += 128) { if (i == 47616) { x.freeze = true; g.set(x); } g.process(&sig[i], &L[i], &R[i], 128, bs); }
      double e = 0; for (int i = 96000; i < 48000 * 4; i++) e += (double)L[i] * L[i]; CHECK(std::sqrt(e / 96000) > 0.05, "frozen sound should sustain (%.4f)", std::sqrt(e / 96000)); }
    // 4) width 0 gives mono; output level and defaults
    { GranularX g; g.prepare(sr, 1); GranularX::P x; x.fine = 1; x.width = 0; GranularX::Base bs; std::vector<float> L, R; runX(g, x, bs, in, L, R); double md = 0; for (size_t i = 0; i < L.size(); i++) md = std::max(md, (double)std::fabs(L[i] - R[i])); CHECK(md < 1e-6, "width 0 must be mono (%.2e)", md); }
    { GranularX::P d; CHECK(d.isDefault(), "defaults must be neutral"); GranularX::P x; x.sus = 0.9; CHECK(!x.isDefault(), "a changed control must switch the extended engine on"); }
    // 5) the stage: all-default controls are bit-identical to the original granular path
    { Json p = JsonParser::parse("{\"grainSize\":80,\"density\":20,\"pitch\":0,\"spread\":0.5,\"panCenter\":0,\"envShape\":0,\"followerAmount\":0,\"mix\":1}"); FxEnv env; GranularStage a(sr, 777), b2(sr, 777); a.configure(p, true, env);
      Json q = JsonParser::parse("{\"grainSize\":80,\"density\":20,\"pitch\":0,\"spread\":0.5,\"panCenter\":0,\"envShape\":0,\"followerAmount\":0,\"mix\":1,\"mode\":0,\"window\":-1,\"fb\":0,\"width\":1,\"outLevel\":0,\"posRand\":1}"); b2.configure(q, true, env);
      std::vector<float> aL(128), aR(128), bL(128), bR(128); bool same = true; for (int i = 0; i + 128 <= (int)in.size(); i += 128) { a.process(&in[i], &in[i], aL.data(), aR.data(), 128); b2.process(&in[i], &in[i], bL.data(), bR.data(), 128); if (aL != bL || aR != bR) same = false; }
      CHECK(same, "default extended controls must not change the original sound"); }
    std::printf(fails ? "granular_x: %d FAILED\n" : "granular_x: all passed\n", fails);
    return fails ? 1 : 0;
}
