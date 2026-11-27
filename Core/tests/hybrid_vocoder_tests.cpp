// Hybrid vocoder tests: every mode produces finite audio with sensible spectral behaviour; formant scaling and pitch quantising work.
#include <cstdio>
#include <vector>
#include <cmath>
#include "vv/hybrid_vocoder.hpp"
using namespace vv;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
static const double SR = 48000;
static double centroid(const std::vector<float>& x, size_t a) { // crude: zero-crossing based dominant freq proxy via DFT of 4096
    const int N = 4096; double num = 0, den = 0; for (int k = 1; k < N / 2; k++) { double re = 0, im = 0; for (int i = 0; i < N; i += 1) { const double w = 0.5 - 0.5 * cos(2 * M_PI * i / N); const double v = x[a + i] * w; re += v * cos(2 * M_PI * k * i / N); im -= v * sin(2 * M_PI * k * i / N); }
        const double mg = sqrt(re * re + im * im); num += mg * k * SR / N; den += mg; } return num / (den + 1e-12); }
static double dominant(const std::vector<float>& x, size_t a, double lo, double hi) {
    const int N = 8192; double best = 0, bf = 0; for (double f = lo; f < hi; f += 1.0) { double re = 0, im = 0; for (int i = 0; i < N; i += 4) { const double w = 0.5 - 0.5 * cos(2 * M_PI * i / N); re += x[a + i] * w * cos(2 * M_PI * f * i / SR); im += x[a + i] * w * sin(2 * M_PI * f * i / SR); } const double m = re * re + im * im; if (m > best) { best = m; bf = f; } } return bf; }
// vowel-ish modulator: glottal-like pulse train through two resonators
static std::vector<float> voice(double f0, double f1, double f2, int n) {
    std::vector<float> o(n); double y1a = 0, y2a = 0, y1b = 0, y2b = 0, ph = 0; auto res = [&](double f, double bw, double& y1, double& y2, double x) { const double r = exp(-M_PI * bw / SR), a1 = -2 * r * cos(2 * M_PI * f / SR), a2 = r * r; const double y = x - a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; };
    for (int i = 0; i < n; i++) { ph += f0 / SR; double e = 0; if (ph >= 1) { ph -= 1; e = 1; } double a = res(f1, 90, y1a, y2a, e), b = res(f2, 120, y1b, y2b, e); o[i] = (float)((a + 0.6 * b) * 0.02); } return o; }
static std::vector<float> run(HybridVocoder& v, const std::vector<float>& mod, int carNote, bool useCar = true) {
    const int n = (int)mod.size(); std::vector<float> L(n, 0.f), R(n, 0.f), zero(128, 0.f); if (useCar) v.noteOn(carNote);
    for (int i = 0; i < n; i += 128) { v.process(&mod[i], zero.data(), zero.data(), nullptr, &L[i], &R[i], 128); } return L; }
int main() {
    const int n = (int)SR * 2; auto mod = voice(120, 700, 1200, n); (void)centroid; 
    for (int mode = 0; mode < 4; mode++) {
        HybridVocoder v; v.prepare(SR); HvParams p; p.on = true; p.mode = mode; p.carSrc = 0; p.mix = 1; p.sibSens = 0; p.comp = false; p.bands = 16; p.chorusDepth = 30; p.drive = 20; v.setParams(p);
        auto out = run(v, mod, 45); double pk = 0, rms = 0; bool finite = true; for (size_t i = n / 2; i < (size_t)n; i++) { if (!std::isfinite(out[i])) finite = false; pk = std::max(pk, (double)std::fabs(out[i])); rms += out[i] * out[i]; }
        rms = sqrt(rms / (n / 2)); printf("mode %d peak %.3f rms %.4f\n", mode, pk, rms);
        CHECK(finite, "mode %d non-finite", mode); if (mode == 1) CHECK(rms < 1e-6, "retired LPC/formant mode must be silent like the web worklet (rms %.3g)", rms); else CHECK(rms > 1e-3, "mode %d silent", mode); CHECK(pk < 8, "mode %d runaway %.2f", mode, pk);
    }
    // classic: formant shift moves the output energy upward
    { double c[2]; for (int s = 0; s < 2; s++) { HybridVocoder v; v.prepare(SR); HvParams p; p.on = true; p.mode = 0; p.carSrc = 0; p.mix = 1; p.sibSens = 0; p.comp = false; p.bands = 24; p.formantSt = s ? 12 : 0; v.setParams(p);
        auto o = run(v, mod, 45); c[s] = centroid(o, n - 8192); } printf("classic centroid 0st %.0f, +12st %.0f\n", c[0], c[1]); CHECK(c[1] > c[0] * 1.15, "classic formant shift should raise the centroid"); }
    // (the LPC formant mode was removed from the web app; mode 1 is the silent legacy slot, checked above)
    // pitch corrector: 130 Hz voice (~C3 + 0.4 st sharp) snaps to a note in C major
    { auto mv = voice(131.3, 700, 1200, n); HybridVocoder v; v.prepare(SR); HvParams p; p.on = true; p.mode = 2; p.carSrc = 0; p.mix = 1; p.sibSens = 0; p.comp = false; p.pitchSpeedMs = 0; hvScaleMask("major", p.mask); p.root = 0; v.setParams(p);
      auto o = run(v, mv, 45, false); const double f = dominant(o, n - 16384, 100, 400); const double midi = 69 + 12 * log2(f / 440); printf("pitch corrector: out %.1f Hz (midi %.2f)\n", f, midi); CHECK(std::fabs(midi - std::round(midi)) < 0.2, "pitch not snapped (%.2f)", midi); }
    // gate: silent input below threshold gives no output; sibilance injection adds HF energy
    { std::vector<float> quiet(n, 0.f); for (int i = 0; i < n; i++) quiet[i] = (float)(0.0005 * sin(i * 0.05)); HybridVocoder v; v.prepare(SR); HvParams p; p.on = true; p.mode = 0; p.carSrc = 0; p.mix = 0.0; p.gateDb = -40; p.comp = false; v.setParams(p); auto o = run(v, quiet, 45, false); double e = 0; for (size_t i = n / 2; i < (size_t)n; i++) e += o[i] * o[i]; CHECK(sqrt(e / (n / 2)) < 1e-4, "gate should close"); }
    { std::vector<float> hiss(n); uint32_t z = 1; for (auto& x : hiss) { z ^= z << 13; z ^= z >> 17; z ^= z << 5; x = ((float)z / 4294967295.f * 2 - 1) * 0.3f; }
      double e[2]; for (int s = 0; s < 2; s++) { HybridVocoder v; v.prepare(SR); HvParams p; p.on = true; p.mode = 0; p.carSrc = 0; p.mix = 1; p.sibSens = s ? 1.0 : 0.0; p.noiseDb = -6; p.comp = false; p.hpf = 100; v.setParams(p); auto o = run(v, hiss, 45, false); double a = 0; for (size_t i = n / 2; i < (size_t)n; i++) a += o[i] * o[i]; e[s] = a; }
      printf("sibilance injection energy off %.4f on %.4f\n", e[0], e[1]); CHECK(e[1] > e[0] * 2 + 1e-6, "sibilance injection should add energy"); }
    printf(fails ? "hybrid vocoder tests FAILED (%d)\n" : "hybrid vocoder tests passed\n", fails); return fails ? 1 : 0;
}
