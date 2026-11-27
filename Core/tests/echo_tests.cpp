// ECHO + reverse-reverb tail/transient: stability, behaviour and exact parity with the web DSP reference values.
#include <cmath>
#include <cstdio>
#include <vector>
#include <vv/echo.hpp>
#include <vv/fx_chain.hpp>
using namespace vv;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
    // 1) parity with the web implementation (same impulse, same parameters): energy and individual samples
    { echo::Core c(48000); echo::Params p; p.env = 5; p.mix = 1; p.feedback = 0.5; p.count = 10; p.density = 0.7; p.time = 0.6; c.setParams(p); c.snap();
      const int n = 48000 * 3; std::vector<float> x(n, 0.f), L(n), R(n); x[0] = 1; for (int i = 0; i < n; i += 128) c.process(&x[i], nullptr, &L[i], &R[i], 128);
      double e = 0; for (int i = 0; i < n; i++) e += (double)L[i] * L[i] + (double)R[i] * R[i];
      CHECK(std::fabs(e - 0.268443367) < 1e-6, "web parity energy %.9f", e);
      CHECK(std::fabs(L[7000] - -0.000027852) < 1e-8 && std::fabs(L[100000] - 0.000040717) < 1e-8, "web parity samples"); }
    // 2) stability at extreme settings with loud noise, every environment; no NaN, bounded
    for (int env = 0; env < 9; env++) {
        echo::Core c(48000); echo::Params p; p.env = env; p.feedback = 0.95; p.decay = 1; p.density = 1; p.count = 16; p.diffusion = 1; p.time = env % 2 ? 4.0 : 0.02; p.mix = 1; p.level = 12; c.setParams(p); c.snap();
        std::vector<float> x(128), L(128), R(128); uint32_t s = 9; double pk = 0; bool bad = false;
        for (int b = 0; b < 48000 * 12 / 128; b++) { for (auto& v : x) { s = s * 1664525u + 1013904223u; v = ((s / 4294967296.0) * 2 - 1) * 0.9; } c.process(x.data(), x.data(), L.data(), R.data(), 128);
            for (int i = 0; i < 128; i++) { if (!std::isfinite(L[i]) || !std::isfinite(R[i])) bad = true; pk = std::max(pk, (double)std::max(std::fabs(L[i]), std::fabs(R[i]))); } }
        CHECK(!bad && pk < 12.0, "env %d unstable (peak %.2f)", env, pk);
    }
    // 3) decays to silence and has no DC
    { echo::Core c(48000); echo::Params p; p.env = 8; p.feedback = 0.95; p.decay = 1; p.time = 2; p.mix = 1; p.count = 16; p.density = 1; c.setParams(p); c.snap();
      std::vector<float> x(48000 * 40, 0.f), L(x.size()), R(x.size()); x[0] = 1; for (size_t i = 0; i < x.size(); i += 128) c.process(&x[i], nullptr, &L[i], &R[i], 128);
      double e = 0, m = 0; for (size_t i = 48000 * 30; i < x.size(); i++) { e += L[i] * L[i]; m += L[i]; } e = std::sqrt(e / (48000.0 * 10)); m /= 48000.0 * 10;
      CHECK(e < 1e-3 && std::fabs(m) < 1e-6, "tail rms %.2e dc %.2e", e, m); }
    // 4) the stage inside the FX chain (layer send -> echo -> master): audible tail after the source stops; mix 0 is the dry signal
    for (int pass = 0; pass < 2; pass++) {
        FxChain chain; chain.prepare(48000); std::vector<FxStageCfg> cfg(1); cfg[0].id = 1; cfg[0].type = "echo"; cfg[0].sends[3] = true;
        cfg[0].params = JsonParser::parse(pass ? "{\"env\":2,\"time\":0.3,\"count\":8,\"mix\":0,\"feedback\":0.5}" : "{\"env\":2,\"time\":0.3,\"count\":8,\"mix\":0.6,\"feedback\":0.5}");
        chain.configure(cfg, 120, "normal", 1);
        std::vector<float> sL(128), sR(128), oL(128), oR(128), z(128, 0.f); const float* sendL[kFxLayers]; const float* sendR[kFxLayers]; double tail = 0, dryErr = 0;
        for (int b = 0; b < 48000 / 128 * 2; b++) { const bool on = b < 48000 / 128 / 2; for (int i = 0; i < 128; i++) sL[i] = sR[i] = on ? 0.3f * std::sin(6.2832f * 220.f * (b * 128 + i) / 48000.f) : 0.f;
            for (int l = 0; l < kFxLayers; l++) { sendL[l] = l == 3 ? sL.data() : z.data(); sendR[l] = l == 3 ? sR.data() : z.data(); }
            chain.process(sendL, sendR, oL.data(), oR.data(), 128);
            if (!on) for (int i = 0; i < 128; i++) tail += (double)oL[i] * oL[i]; else if (pass) for (int i = 0; i < 128; i++) dryErr += std::fabs(oL[i] - sL[i]); }
        if (!pass) CHECK(tail > 1e-3, "echo tail inaudible (%.6f)", tail); else CHECK(tail < 1e-9 && dryErr < 1.0, "mix 0 should be dry (tail %.3e err %.3f)", tail, dryErr);
    }
    // 5) reverse reverb: tail and transient controls change the output as designed
    { auto run = [&](double tail, double trans, std::vector<double>& envelope) {
          LegacyReverb rv; rv.prepare(48000, 1); LegacyReverb::Params p; p.algo = 5; p.decay = 0.8; p.size = 0.9; p.predelay = 0; p.mix = 1; p.rvTail = tail; p.rvTrans = trans;
          std::vector<float> in(128), out(128); uint32_t s = 7; double en = 0; envelope.assign(34, 0);
          for (int b = 0; b < 48000 * 3.4 / 128; b++) { for (int i = 0; i < 128; i++) { s = s * 1664525u + 1013904223u; in[i] = (b * 128 + i < 14400) ? (float)(((s / 4294967296.0) * 2 - 1) * 0.5) : 0.f; }
              rv.process(in.data(), out.data(), 128, p); for (int i = 0; i < 128; i++) { en += out[i] * out[i]; int bin = (b * 128 + i) / 4800; if (bin < 34) envelope[bin] += out[i] * out[i]; } }
          return en; };
      std::vector<double> e1, e2, e3, e4; const double full = run(1, 0.5, e1), shortT = run(0.1, 0.5, e2), soft = run(1, 0, e3), sharp = run(1, 1, e4);
      CHECK(shortT < full * 0.6, "short tail should carry less energy (%.1f vs %.1f)", shortT, full);
      CHECK(sharp > full * 1.1 && soft < full * 0.95, "transient: sharp %.1f / classic %.1f / soft %.1f", sharp, full, soft);
      CHECK(e3[17] < e1[17] * 0.5 && e4[17] > e1[17], "peak bin: soft %.3f classic %.3f sharp %.3f", e3[17], e1[17], e4[17]); }
    std::printf(fails ? "echo tests FAILED (%d)\n" : "echo tests passed\n", fails);
    return fails ? 1 : 0;
}
