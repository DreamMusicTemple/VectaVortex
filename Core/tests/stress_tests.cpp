// Robustness checks: block-size independence, stability at extreme settings, silence and denormal behaviour.
#include <cstdio>
#include <vector>
#include "vv/adv_reverb.hpp"
#include "vv/phaser_flanger.hpp"

static uint32_t g_s = 7;
static float rnd() { g_s = g_s * 1664525u + 1013904223u; return (float)((double)g_s / 4294967296.0 * 2 - 1); }

int main() {
    int fails = 0;
    // 1. block-size independence (host buffer sizes vary: 64, 128, 480, 512, 1024)
    const double sr = 48000; const int N = 48000;
    std::vector<float> in(N); for (int i = 0; i < N; i++) in[i] = i < 12000 ? rnd() * 0.3f : 0.0f;
    for (int algo : { 0, 1, 2, 3, 4 }) {
        std::vector<float> ref; bool haveRef = false;
        for (int B : { 128, 64, 480, 512, 1000 }) {
            vv::AdvReverb rv; rv.prepare(sr); vv::AdvReverb::Params p; p.algo = algo; p.mix = 1;
            std::vector<float> L(N), R(N); int done = 0;
            while (done < N) { int n = std::min(B, N - done); rv.process(&in[done], &L[done], &R[done], n, p); done += n; }
            if (!haveRef) { ref = L; haveRef = true; continue; }
            double e = 0; for (int i = 0; i < N; i++) e = std::max(e, (double)std::fabs(ref[i] - L[i]));
            if (e > 1e-5) { std::printf("algo %d block %d differs from block 128 by %.2e FAIL\n", algo, B, e); fails++; }
        }
    }
    std::printf("block-size independence: %s\n", fails ? "FAIL" : "OK");
    // 2. stability at extreme settings with loud input
    double worst = 0; int bad = 0;
    for (int algo = 0; algo <= 5; algo++) for (double decay : { 0.0, 0.5, 0.97 }) for (double size : { 0.1, 0.5, 2.0 }) for (double col : { 0.0, 1.0 }) for (double mod : { 0.0, 1.0 }) {
        vv::AdvReverb rv; rv.prepare(sr); vv::AdvReverb::Params p; p.algo = algo; p.decay = decay; p.size = size; p.colour = col; p.mod = mod; p.mix = 1;
        std::vector<float> L(128), R(128), x(128);
        for (int blk = 0; blk < 700; blk++) {
            for (int i = 0; i < 128; i++) x[i] = blk < 300 ? rnd() : 0.0f;
            rv.process(x.data(), L.data(), R.data(), 128, p);
            for (int i = 0; i < 128; i++) { double v = std::max(std::fabs(L[i]), std::fabs(R[i])); if (!std::isfinite(v)) bad++; worst = std::max(worst, v); }
        }
    }
    std::printf("reverb stability: worst peak %.2f, non-finite samples %d -> %s\n", worst, bad, (bad == 0 && worst < 20) ? "OK" : "FAIL");
    if (bad || worst >= 20) fails++;
    // 3. phaser extremes
    bad = 0; worst = 0;
    for (int mode : { 0, 1 }) for (int stages : { 2, 8, 12 }) for (double fb : { 0.0, 0.95 }) for (double depth : { 0.0, 1.0 }) {
        vv::PhaserFlanger pf; pf.prepare(sr); vv::PhaserFlanger::Params p; p.mode = mode; p.stages = stages; p.feedback = fb; p.depth = depth; p.rate = 5; p.mix = 0.5;
        std::vector<float> L(128), R(128), x(128);
        for (int blk = 0; blk < 400; blk++) { for (auto& v : x) v = rnd(); pf.process(x.data(), L.data(), R.data(), 128, p);
            for (int i = 0; i < 128; i++) { double v = std::max(std::fabs(L[i]), std::fabs(R[i])); if (!std::isfinite(v)) bad++; worst = std::max(worst, v); } }
    }
    std::printf("phaser/flanger stability: worst peak %.2f, non-finite %d -> %s\n", worst, bad, (bad == 0 && worst < 40) ? "OK" : "FAIL");
    if (bad || worst >= 40) fails++;
    // 4. null input pointer and idle algorithm
    { vv::AdvReverb rv; rv.prepare(sr); vv::AdvReverb::Params p; p.algo = 0; std::vector<float> L(128), R(128); rv.process(nullptr, L.data(), R.data(), 128, p);
      p.algo = vv::AdvReverb::Idle; rv.process(nullptr, L.data(), R.data(), 128, p); double s = 0; for (float v : L) s += std::fabs(v); std::printf("idle/null input: %s\n", s == 0 ? "OK" : "FAIL"); if (s != 0) fails++; }
    std::printf("%s\n", fails ? "STRESS TESTS FAILED" : "all stress tests passed");
    return fails ? 1 : 0;
}
