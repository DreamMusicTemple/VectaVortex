// Master limiter + BS.1770 loudness meter tests.
#include "vv/master_out.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace vv;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL %s: ", #c); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
int main() {
    const double sr = 48000; const int N = (int)(sr * 8);
    std::vector<float> L(N), R(N), L0, R0;
    for (int i = 0; i < N; i++) { L[i] = R[i] = (float)(0.5 * std::sin(2 * M_PI * 997.0 * i / sr)); }
    L0 = L; R0 = R;
    { // bypass is bit-exact
        MasterOut m; m.prepare(sr); for (int i = 0; i < N; i += 128) m.process(&L[i], &R[i], std::min(128, N - i));
        CHECK(L == L0 && R == R0, "bypass not bit-exact");
        // EBU Tech 3341: 997 Hz sine at -20 dBFS (stereo, both channels) reads -20 LUFS... 0.5 amplitude = -6.02 dBFS peak -> -9.03 dBFS RMS/ch; two channels add 3 dB, K-weight ~ +0.7 dB at 1 kHz
        const double expect = -0.691 + 10 * std::log10(2 * 0.125 * std::pow(10.0, 0.0)) + 0.0;   // pre K gain
        std::printf("momentary %.2f short %.2f integrated %.2f (unweighted estimate %.2f)\n", m.momentary(), m.shortTerm(), m.integrated(), expect);
        CHECK(std::fabs(m.integrated() - m.momentary()) < 0.2, "steady tone: integrated ~ momentary");
        CHECK(std::fabs(m.integrated() - (expect + 0.7)) < 0.4, "K-weighted level at 1 kHz");
    }
    { // limiter holds the ceiling and is transparent below threshold
        MasterOut m; m.prepare(sr); MasterOut::Params p; p.on = true; p.threshold = -6; p.ceiling = -3; p.release = 100; m.setParams(p);
        std::vector<float> a(N), b(N); for (int i = 0; i < N; i++) a[i] = b[i] = (float)(1.5 * std::sin(2 * M_PI * 220.0 * i / sr));
        for (int i = 0; i < N; i += 128) m.process(&a[i], &b[i], std::min(128, N - i));
        double pk = 0; for (int i = (int)(sr * 2); i < N; i++) pk = std::max(pk, (double)std::fabs(a[i]));
        std::printf("limited peak %.4f (ceiling %.4f)\n", pk, std::pow(10.0, -3 / 20.0));
        CHECK(pk <= std::pow(10.0, -3 / 20.0) + 1e-4, "ceiling exceeded");
        CHECK(pk > 0.5, "over-limited");
    }
    { // hard-clamp: a single huge transient
        MasterOut m; m.prepare(sr); MasterOut::Params p; p.on = true; p.threshold = -1; p.ceiling = -1; m.setParams(p);
        std::vector<float> a(4800, 0.f), b(4800, 0.f); a[2400] = b[2400] = 4.f;
        for (int i = 0; i < 4800; i += 64) m.process(&a[i], &b[i], 64);
        double pk = 0; for (float v : a) pk = std::max(pk, (double)std::fabs(v));
        CHECK(pk <= std::pow(10.0, -1 / 20.0) + 1e-4, "transient exceeds ceiling: %f", pk);
    }
    std::printf(fails ? "FAILED\n" : "OK\n"); return fails ? 1 : 0;
}
