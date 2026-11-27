// Interstellar engine: level, tonal fidelity, sustain, particle pitch, key detection, determinism, bounds at every quality.
#include <cstdio>
#include <cmath>
#include <vector>
#include "vv/interstellar.hpp"
using namespace vv; using namespace vv::is;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
static const double SR = 48000;

static Params dry() {   // a clean pad: no particles, resonators or reverb, so the spectral stage alone is measured
    Params p; p.density = 0; p.resonance = 0; p.space = 0; p.body = 1; p.particles = 0; p.evolve = 0; p.drift = 0; p.stretch = 1; p.bloom = 0; p.tonal = 0; p.mix = 1; p.segments = 1; p.span = 4; return p;
}
static double rms(const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; i++) s += (double)v[i] * v[i]; return std::sqrt(s / (b - a)); }
// magnitude of a frequency in a stretch of samples (single bin DFT, Hann windowed)
static double mag(const std::vector<float>& v, size_t a, size_t n, double f) { double re = 0, im = 0; for (size_t i = 0; i < n; i++) { const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / n); const double ph = 2 * M_PI * f * (a + i) / SR; re += v[a + i] * w * std::cos(ph); im += v[a + i] * w * std::sin(ph); } return std::sqrt(re * re + im * im) / n * 4; }
static void run(Interstellar& e, const std::vector<float>& in, std::vector<float>& L, std::vector<float>& R) {
    L.assign(in.size(), 0); R.assign(in.size(), 0); const int B = 128;
    for (size_t i = 0; i < in.size(); i += B) { const int n = (int)std::min<size_t>(B, in.size() - i); e.process(&in[i], &in[i], &L[i], &R[i], n); }
}
static std::vector<float> chord(const std::vector<double>& fs, double secs, double amp) { std::vector<float> v((size_t)(secs * SR)); for (size_t i = 0; i < v.size(); i++) { double s = 0; for (double f : fs) s += std::sin(2 * M_PI * f * i / SR); v[i] = (float)(amp * s / fs.size()); } return v; }

int main() {
    // silence stays silent, no NaN
    { Interstellar e; e.prepare(SR, 1); Params p; e.setParams(p); std::vector<float> in((size_t)(2 * SR), 0.f), L, R; run(e, in, L, R); double m = 0; bool ok = true; for (float x : L) { if (!(x == x)) ok = false; m = std::max(m, (double)std::fabs(x)); } CHECK(ok && m < 1e-6, "silence in -> silence out (%g)", m); }
    // dry spectral stage keeps level and pitch: 220 Hz tone in, 220 Hz out at similar RMS
    for (int q = 0; q < 3; q++) {
        Interstellar e; e.prepare(SR, 1); Params p = dry(); p.quality = q; e.setParams(p);
        auto in = chord({ 220.0 }, 6, 0.4); std::vector<float> L, R; run(e, in, L, R);
        const double ri = rms(in, (size_t)(3 * SR), (size_t)(5 * SR)), ro = rms(L, (size_t)(3 * SR), (size_t)(5 * SR));
        CHECK(ro > ri * 0.5 && ro < ri * 1.6, "q%d level: in %.4f out %.4f", q, ri, ro);
        const double m0 = mag(L, (size_t)(3 * SR), 16384, 220), m1 = mag(L, (size_t)(3 * SR), 16384, 331);
        CHECK(m0 > 4 * m1, "q%d keeps the 220 Hz tone (%.4f vs %.4f off-pitch)", q, m0, m1);
    }
    // freeze sustains after the input stops (pad behaviour); Live without freeze decays away
    { Interstellar e; e.prepare(SR, 1); Params p = dry(); p.stretch = 1; p.freeze = false; e.setParams(p);
      auto in = chord({ 220.0, 330.0 }, 3, 0.4); in.resize((size_t)(10 * SR), 0.f);
      std::vector<float> L, R; const int B = 128;
      L.assign(in.size(), 0); R.assign(in.size(), 0);
      for (size_t i = 0; i < in.size(); i += B) { if (i == (size_t)(3 * SR) / B * B) { p.freeze = true; e.setParams(p); } e.process(&in[i], &in[i], &L[i], &R[i], B); }
      CHECK(rms(L, (size_t)(7 * SR), (size_t)(9 * SR)) > 0.05, "freeze holds the pad (%.4f)", rms(L, (size_t)(7 * SR), (size_t)(9 * SR))); }
    // pitch-up particles: octave-up energy appears
    { Interstellar e; e.prepare(SR, 3); Params p = dry(); p.body = 0; p.particles = 1; p.density = 1; p.upProb = 1; p.pitchUp = 12; p.snap = 1; p.pitchDown = 0; p.life = 3; p.tonal = 1; e.setParams(p);
      auto in = chord({ 220.0 }, 10, 0.5); std::vector<float> L, R; run(e, in, L, R);
      const double a = mag(L, (size_t)(6 * SR), 32768, 440), b = mag(L, (size_t)(6 * SR), 32768, 220);
      CHECK(a > 3 * b && a > 0.002, "octave-up particles: 440 Hz %.4f vs 220 Hz %.4f", a, b); }
    // pitch-down particles: octave-down energy appears
    { Interstellar e; e.prepare(SR, 3); Params p = dry(); p.body = 0; p.particles = 1; p.density = 1; p.upProb = 0; p.downProb = 1; p.pitchDown = 12; p.snap = 1; p.life = 3; p.tonal = 1; e.setParams(p);
      auto in = chord({ 440.0 }, 10, 0.5); std::vector<float> L, R; run(e, in, L, R);
      const double a = mag(L, (size_t)(6 * SR), 32768, 220), b = mag(L, (size_t)(6 * SR), 32768, 440);
      CHECK(a > 3 * b && a > 0.002, "octave-down particles: 220 Hz %.4f vs 440 Hz %.4f", a, b); }
    // key detection: C major triad material, then A minor
    { Interstellar e; e.prepare(SR, 1); Params p = dry(); e.setParams(p);
      auto in = chord({ 261.63, 329.63, 392.0, 523.25 }, 8, 0.5); std::vector<float> L, R; run(e, in, L, R);
      CHECK(e.hasDetectedKey() && e.detectedRoot() == 0 && !e.detectedMinor(), "C major detected (root %d minor %d)", e.detectedRoot(), (int)e.detectedMinor()); }
    { Interstellar e; e.prepare(SR, 1); Params p = dry(); e.setParams(p);
      auto in = chord({ 220.0, 261.63, 329.63, 440.0 }, 8, 0.5); std::vector<float> L, R; run(e, in, L, R);
      CHECK(e.detectedRoot() == 9 && e.detectedMinor(), "A minor detected (root %d minor %d)", e.detectedRoot(), (int)e.detectedMinor()); }
    // determinism + bounded output for every quality, all features on
    for (int q = 0; q < 3; q++) {
        Params p; p.quality = q; p.density = 1; p.resonance = 1; p.space = 1; p.shimmer = 1; p.decay = 1; p.evolve = 1; p.drift = 1; p.stretch = 20; p.character = 0.8; p.segOrder = 2; p.segments = 8; p.segTime = 0.5;
        std::vector<float> in((size_t)(8 * SR)); unsigned s = 5; for (auto& x : in) { s = s * 1664525u + 1013904223u; x = ((s >> 8) / 8388608.0f - 1.f) * 0.6f; }
        Interstellar a, b; a.prepare(SR, 42); b.prepare(SR, 42); a.setParams(p); b.setParams(p); std::vector<float> La, Ra, Lb, Rb; run(a, in, La, Ra); run(b, in, Lb, Rb);
        bool same = true, fin = true; double pk = 0; for (size_t i = 0; i < La.size(); i++) { if (La[i] != Lb[i] || Ra[i] != Rb[i]) same = false; if (!(La[i] == La[i]) || !(Ra[i] == Ra[i])) fin = false; pk = std::max(pk, (double)std::max(std::fabs(La[i]), std::fabs(Ra[i]))); }
        CHECK(same, "q%d deterministic", q); CHECK(fin, "q%d finite", q); CHECK(pk <= 1.0 && pk > 0.01, "q%d bounded and audible (peak %.3f)", q, pk);
        CHECK(rms(La, (size_t)(4 * SR), (size_t)(8 * SR)) > 0.01, "q%d produces a pad", q);
    }
    // wide stereo: pad L/R decorrelated; width 0 collapses to mono
    { Interstellar e; e.prepare(SR, 1); Params p = dry(); p.body = 1; p.width = 1; e.setParams(p); auto in = chord({ 220.0, 277.0, 330.0 }, 6, 0.4); std::vector<float> L, R; run(e, in, L, R);
      double lr = 0, ll = 0, rr = 0; for (size_t i = (size_t)(3 * SR); i < L.size(); i++) { lr += (double)L[i] * R[i]; ll += (double)L[i] * L[i]; rr += (double)R[i] * R[i]; } const double corr = lr / std::sqrt(ll * rr + 1e-12);
      CHECK(corr < 0.8, "wide pad is decorrelated (corr %.3f)", corr);
      Interstellar m; m.prepare(SR, 1); Params q = dry(); q.width = 0; m.setParams(q); std::vector<float> L2, R2; run(m, in, L2, R2); lr = ll = rr = 0; for (size_t i = (size_t)(3 * SR); i < L2.size(); i++) { lr += (double)L2[i] * R2[i]; ll += (double)L2[i] * L2[i]; rr += (double)R2[i] * R2[i]; }
      CHECK(lr / std::sqrt(ll * rr + 1e-12) > 0.95, "width 0 is mono (corr %.3f)", lr / std::sqrt(ll * rr + 1e-12)); }
    // capture mode records and loops what it heard, then ignores later input
    { Interstellar e; e.prepare(SR, 1); Params p = dry(); p.source = 1; p.captureLen = 2; e.setParams(p);
      std::vector<float> in = chord({ 220.0 }, 3, 0.4); auto b = chord({ 660.0 }, 5, 0.4); in.insert(in.end(), b.begin(), b.end());
      std::vector<float> L, R; run(e, in, L, R);
      const double a = mag(L, (size_t)(5 * SR), 16384, 220), c = mag(L, (size_t)(5 * SR), 16384, 660);
      CHECK(a > 5 * c, "capture keeps the captured 220 Hz, not the later 660 Hz (%.4f vs %.4f)", a, c); }
    // mix: 0 = dry passthrough, default 100 % wet
    { Params p; CHECK(p.mix == 1.0, "mix defaults to 100%% wet"); Interstellar e; e.prepare(SR, 1); p.mix = 0; e.setParams(p); auto in = chord({ 300.0 }, 1, 0.3); std::vector<float> L, R; run(e, in, L, R); double d = 0; for (size_t i = 4000; i < in.size(); i++) d = std::max(d, (double)std::fabs(L[i] - in[i])); CHECK(d < 1e-3, "mix 0 is dry (%g)", d); }
    // quality switch mid-stream keeps running
    { Interstellar e; e.prepare(SR, 1); Params p; e.setParams(p); auto in = chord({ 220.0, 330.0 }, 6, 0.4); std::vector<float> L(in.size()), R(in.size()); const int B = 128; bool fin = true;
      for (size_t i = 0; i + B <= in.size(); i += B) { if (i % (size_t)(SR) < B) { p.quality = (int)((i / (size_t)SR) % 3); e.setParams(p); } e.process(&in[i], &in[i], &L[i], &R[i], B); }
      for (float x : L) if (!(x == x)) fin = false; CHECK(fin, "quality switching is safe"); }
    printf(failures ? "INTERSTELLAR TESTS FAILED (%d)\n" : "all interstellar tests passed\n", failures);
    return failures ? 1 : 0;
}
