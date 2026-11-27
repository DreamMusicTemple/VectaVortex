// Round 22: WT sub oscillator shape (sine / triangle / square), coloured noise (brown, blue, violet), Semitone + Fine tune in hard sync.
#include <vv/viz.hpp>
#include <cstdio>
#include <cmath>
#include <complex>
#include <vector>
#include <string>
#include "vv/performance.hpp"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
using namespace vv;
static std::shared_ptr<WtTable> makeTable(bool saw) {
    auto wt = std::make_shared<WtTable>(); wt->tableSize = 1024; std::vector<float> fr(1024);
    for (int i = 0; i < 1024; i++) { double s = 0; if (!saw) s = std::sin(2 * M_PI * i / 1024.0); else for (int h = 1; h <= 24; h++) s += std::sin(2 * M_PI * h * i / 1024.0) / h; fr[i] = (float)(saw ? s * 0.55 : s); }
    wt->frames.push_back(fr);
    if (saw) { std::vector<float> f2(1024); for (int i = 0; i < 1024; i++) f2[i] = (float)std::sin(2 * M_PI * i / 1024.0) * 0.55f; wt->frames.insert(wt->frames.begin(), f2); }   // saw tables are 2-frame: sine (position 0) -> saw (position 1) so Position is audible
    return wt;
}
static std::vector<float> render(const std::string& layerJson, int note, int blocks, bool saw = false) {
    Synth syn; syn.prepare(48000); syn.setWavetable("analogMorph", makeTable(saw)); syn.seedRandom(5);
    const std::string js = "{\"layers\":{\"WT\":" + layerJson + "},\"bpm\":120}"; syn.setPreset(parsePresetText(js.c_str()));
    std::vector<float> l(128), r(128), all; for (int i = 0; i < 40; i++) syn.process(l.data(), r.data(), 128);   // warm-up: let the parameter smoothers settle
    syn.noteOn(1, note, 0.9); for (int i = 0; i < blocks; i++) { syn.process(l.data(), r.data(), 128); all.insert(all.end(), l.begin(), l.end()); } return all;
}
static double magAt(const std::vector<float>& x, size_t a, size_t b, double f, double sr) {   // Hann-windowed single-bin DFT magnitude
    std::complex<double> acc(0, 0); const size_t n = b - a; for (size_t i = 0; i < n; i++) { const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (n - 1)); acc += (double)x[a + i] * w * std::polar(1.0, -2 * M_PI * f * (double)i / sr); }
    return std::abs(acc) / (double)n * 2.0;
}
static double acorr(const std::vector<float>& x, size_t a, size_t b, size_t lag) { double s = 0, e = 0; for (size_t i = a; i + lag < b; i++) { s += (double)x[i] * x[i + lag]; e += (double)x[i] * x[i]; } return s / (e > 0 ? e : 1); }
static void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size(); for (size_t i = 1, j = 0; i < n; i++) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) { const auto wl = std::polar(1.0, -2 * M_PI / (double)len); for (size_t i = 0; i < n; i += len) { std::complex<double> w(1, 0); for (size_t k = 0; k < len / 2; k++) { auto u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; } } }
}
static double densitySlope(const std::vector<float>& x, double sr) {   // dB per octave of the power spectral DENSITY (white = 0, pink = -3, brown = -6, blue = +3, violet = +6)
    const size_t N = 8192, blocks = x.size() / N; std::vector<double> P(N / 2, 0.0);
    for (size_t b = 0; b < blocks; b++) { std::vector<std::complex<double>> a(N); for (size_t i = 0; i < N; i++) a[i] = x[b * N + i] * (0.5 - 0.5 * std::cos(2 * M_PI * i / (N - 1))); fft(a); for (size_t k = 1; k < N / 2; k++) P[k] += std::norm(a[k]); }
    std::vector<double> xs, ys; for (double f = 200; f * 2 <= 12800; f *= 2) { double s = 0; size_t c = 0; for (size_t k = (size_t)(f * N / sr); k < (size_t)(2 * f * N / sr); k++) { s += P[k]; c++; } xs.push_back(std::log2(f)); ys.push_back(10 * std::log10(s / (double)c)); }
    double mx = 0, my = 0; for (size_t i = 0; i < xs.size(); i++) { mx += xs[i]; my += ys[i]; } mx /= xs.size(); my /= ys.size(); double sxy = 0, sxx = 0; for (size_t i = 0; i < xs.size(); i++) { sxy += (xs[i] - mx) * (ys[i] - my); sxx += (xs[i] - mx) * (xs[i] - mx); } return sxy / sxx;
}
int main() {
    const double sr = 48000;
    // ---- sub oscillator shapes: note 57 (220 Hz), sub 1 octave down = 110 Hz. A square sub adds a 3rd harmonic at 330 Hz, a triangle a weaker one, a sine none.
    auto subLayer = [](const char* shape) { return std::string("{\"type\":\"wavetable\",\"env\":{\"a\":0.001,\"d\":0.1,\"s\":1,\"r\":0.05},\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.0,\"position\":0,\"sub\":1,\"subWave\":\"") + shape + "\",\"enabled\":true},{\"table\":\"analogMorph\",\"level\":0,\"enabled\":false}]}"; };
    // the oscillator level is 0 so only the sub could be heard, but the sub is skipped at level 0 (same as web) -> use a small level and measure ratios against the 110 Hz line
    auto subLayer2 = [](const char* shape) { return std::string("{\"type\":\"wavetable\",\"env\":{\"a\":0.001,\"d\":0.1,\"s\":1,\"r\":0.05},\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.5,\"position\":0,\"sub\":1,\"subWave\":\"") + shape + "\",\"enabled\":true},{\"table\":\"analogMorph\",\"level\":0,\"enabled\":false}]}"; };
    (void)subLayer; double h3[3], h1[3]; const char* shapes[3] = { "sine", "triangle", "square" };
    for (int s = 0; s < 3; s++) { auto x = render(subLayer2(shapes[s]), 57, 600); const size_t a = x.size() / 2, b = x.size(); h1[s] = magAt(x, a, b, 110.0, sr); h3[s] = magAt(x, a, b, 330.0, sr); for (float v : x) CHECK(std::isfinite(v), "non-finite sample"); }
    std::printf("sub 110Hz: sine %.4f tri %.4f sq %.4f | 330Hz: sine %.5f tri %.5f sq %.5f\n", h1[0], h1[1], h1[2], h3[0], h3[1], h3[2]);
    CHECK(h1[0] > 0.02 && h1[1] > 0.02 && h1[2] > 0.02, "every sub shape must be audible at 110 Hz");
    CHECK(h3[2] > h3[0] * 8 && h3[1] > h3[0] * 4, "square / triangle subs must carry a 3rd harmonic that the sine does not (sq %.5f tri %.5f sine %.5f)", h3[2], h3[1], h3[0]);
    CHECK(h3[2] > h3[1] * 2, "square 3rd harmonic should be stronger than triangle's");
    CHECK(std::fabs(h1[0] - h1[2]) < h1[0] * 0.6 && std::fabs(h1[0] - h1[1]) < h1[0] * 0.6, "level-matched shapes: fundamental within 60%% of the sine's (sine %.4f tri %.4f sq %.4f)", h1[0], h1[1], h1[2]);
    // ---- coloured noise spectra (helper) and engine path
    const char* names[3] = { "brown", "blue", "violet" }; const int idx[3] = { 2, 3, 4 }; const double want[3] = { -6, 3, 6 };
    for (int k = 0; k < 3; k++) { CHECK(noiseTypeIndex(names[k]) == idx[k], "noise type index for %s", names[k]); std::vector<float> v((size_t)(sr * 2)); fillColouredNoise(v, idx[k], 4242u + k);
        double ss = 0, pk = 0; for (float f : v) { ss += (double)f * f; pk = std::max(pk, (double)std::fabs(f)); } const double rms = std::sqrt(ss / v.size()); const double sl = densitySlope(v, sr);
        std::printf("%s: rms %.3f peak %.3f slope %.2f dB/oct (want %.0f)\n", names[k], rms, pk, sl, want[k]);
        CHECK(std::fabs(sl - want[k]) < 1.3, "%s slope %.2f, want %.0f", names[k], sl, want[k]); CHECK(rms > 0.2 && rms < 0.4 && pk <= 1.0, "%s level", names[k]); }
    CHECK(noiseTypeIndex("red") == 2 && noiseTypeIndex("white") == 0 && noiseTypeIndex("pink") == 1 && noiseTypeIndex("nonsense") == 0, "noise type aliases");
    auto noiseLayer = [](const char* t) { return std::string("{\"type\":\"wavetable\",\"env\":{\"a\":0.001,\"d\":0.1,\"s\":1,\"r\":0.05},\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8,\"noiseOn\":true,\"noiseType\":\"") + t + "\",\"enabled\":true},{\"level\":0,\"enabled\":false}]}"; };
    double lo[3], hi[3];
    for (int k = 0; k < 3; k++) { auto x = render(noiseLayer(names[k]), 60, 400); double l = 0, h = 0; for (int f : { 100, 150, 200, 300 }) l += magAt(x, 4096, x.size(), f * 1.37, sr); for (int f : { 1500, 2000, 2500, 3000 }) h += magAt(x, 4096, x.size(), f * 1.01, sr); lo[k] = l; hi[k] = h; for (float v : x) CHECK(std::isfinite(v), "non-finite noise sample"); }
    std::printf("engine noise lo/hi: brown %.4f/%.4f blue %.4f/%.4f violet %.4f/%.4f\n", lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
    { const double q0 = lo[0] / (hi[0] + 1e-9), q1 = lo[1] / (hi[1] + 1e-9), q2 = lo[2] / (hi[2] + 1e-9);   // low/high balance must fall brown > blue > violet (all at the same RMS)
      CHECK(q0 > q1 && q1 > q2, "colour ordering through the engine: brown %.1f blue %.1f violet %.1f", q0, q1, q2); CHECK(lo[0] > 0 && lo[1] > 0 && lo[2] > 0, "all coloured noises audible"); }
    // ---- hard sync: master (OSC1, silent) resets the slave (OSC2); Semitone + Fine tune set the slave's pitch (ratio 2.5 vs the master)
    const std::string syncJs = "{\"type\":\"wavetable\",\"syncEnabled\":SYNC,\"env\":{\"a\":0.001,\"d\":0.1,\"s\":1,\"r\":0.05},\"oscs\":[{\"table\":\"analogMorph\",\"level\":0,\"enabled\":true},{\"table\":\"analogMorph\",\"level\":0.8,\"oct\":1,\"semitone\":4,\"fineTune\":86.31,\"enabled\":true}]}";
    auto mk = [&](const char* on) { std::string s = syncJs; s.replace(s.find("SYNC"), 4, on); return s; };
    auto xs = render(mk("true"), 45, 700, true), xn = render(mk("false"), 45, 700, true); const size_t lag = (size_t)std::lround(sr / 110.0), a = xs.size() / 2, b = xs.size();
    double rs = 0, rn = 0; for (int d = -2; d <= 2; d++) { rs = std::max(rs, acorr(xs, a, b, lag + d)); rn = std::max(rn, acorr(xn, a, b, lag + d)); }
    std::printf("autocorr at the master period: sync %.3f, no sync %.3f\n", rs, rn);
    CHECK(rs > 0.85, "synced output must repeat at the master period (%.3f)", rs); CHECK(rn < 0.5, "unsynced slave at ratio 2.5 must not repeat at the master period (%.3f)", rn);
    // pro plot maths (viz.hpp)
    { using namespace vv::viz;
      CHECK(std::fabs(hzAt(0) - 20) < 1e-9 && std::fabs(hzAt(1) - 20000) < 1e-6, "plot Hz axis spans 20..20000");
      CHECK(std::fabs(xOfHz(hzAt(0.37)) - 0.37) < 1e-9, "Hz axis round-trips");
      const double lo = filterMagnitudeDb("moog4pole", 1000, 20, 100), hi = filterMagnitudeDb("moog4pole", 1000, 20, 10000);
      CHECK(lo > -1 && hi < -30, "lowpass passes lows (%.1f) and cuts highs (%.1f)", lo, hi);
      CHECK(filterMagnitudeDb("highpass", 1000, 20, 100) < -30 && filterMagnitudeDb("highpass", 1000, 20, 10000) > -1, "highpass mirrors lowpass");
      CHECK(filterMagnitudeDb("bandpass", 1000, 60, 1000) > -0.1 && filterMagnitudeDb("bandpass", 1000, 60, 100) < -15, "bandpass peaks at cutoff");
      CHECK(filterMagnitudeDb("notch", 1000, 60, 1000) < -30, "notch dips at cutoff");
      CHECK(filterMagnitudeDb("moog4pole", 1000, 120, 1000) > filterMagnitudeDb("moog4pole", 1000, 0, 1000) + 6, "resonance lifts the cutoff peak");
      auto e = envShape(0.1, 0.2, 0.5, 0.4);
      CHECK(std::fabs(envLevel(e, 0)) < 1e-12 && std::fabs(envLevel(e, 0.1) - 1) < 1e-12 && std::fabs(envLevel(e, 0.3) - 0.5) < 1e-12 && envLevel(e, e.total) < 1e-9, "ADSR plot passes 0 -> 1 -> sustain -> 0");
      CHECK(envGridStep(1.0) == 0.2 && envGridStep(0.1) == 0.02, "grid step (%.3f)", envGridStep(1.0)); }
    // ---- Position auto-scan sources (Round 23): matrix source, amp envelope, pitch envelope all drive Position like the manual dial
    { auto wt = [&](const std::string& extra, double pos) { char b[160]; std::snprintf(b, sizeof b, "%.3f", pos);
          return "{\"type\":\"wavetable\",\"env\":{\"a\":0.001,\"d\":0.05,\"s\":0.5,\"r\":0.1},\"oscs\":[{\"table\":\"analogMorph\",\"enabled\":true,\"level\":0.8,\"position\":" + std::string(b) + extra + "},{\"table\":\"analogMorph\",\"enabled\":false}]}"; };
      auto diff = [](const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to = (size_t)-1) { double d = 0, e = 0; for (size_t i = from; i < a.size() && i < b.size() && i < to; i++) { d += std::fabs((double)a[i] - b[i]); e += std::fabs((double)a[i]); } return d / (e > 0 ? e : 1); };
      const auto m0 = render(wt("", 0.0), 45, 200, true), m1 = render(wt("", 1.0), 45, 200, true), m09 = render(wt("", 0.8), 45, 200, true);
      CHECK(diff(m0, m1, 4000) > 0.2, "test table: position 0 and 1 must sound different (%.3f)", diff(m0, m1, 4000));
      const auto vel = render(wt(",\"positionLfoSource\":\"velocity\"", 0.0), 45, 200, true);   // a source with no voice reads the default velocity 0.8 -> +0.6 -> position 0.8
      CHECK(diff(vel, m09, 4000) < 0.02, "velocity source drives Position 0.8 (%.4f)", diff(vel, m09, 4000));
      const auto amp = render(wt(",\"positionLfoSource\":\"ampEnv\"", 0.0), 45, 200, true), m5 = render(wt("", 0.5), 45, 200, true);
      CHECK(diff(amp, m0, 4000) > 0.1, "amp envelope auto-scan is not the manual Position 0 (%.3f)", diff(amp, m0, 4000));
      std::printf("amp-env scan vs manual 0.5 (sustain): %.3f\n", diff(amp, m5, 12000));
      // NB: like the web, the Matrix evaluates on a 100 ms tick, so Position follows the envelope in 100 ms steps.
      const auto m0l = render(wt("", 0.0), 45, 320, true), m1l = render(wt("", 1.0), 45, 320, true);
      const auto peg = render(wt(",\"positionLfoSource\":\"pitchEnv\",\"pitchEnv\":{\"on\":false,\"amount\":12,\"time\":0.5,\"bipolar\":false,\"pts\":[{\"x\":0,\"y\":1},{\"x\":1,\"y\":0}]}", 0.0), 45, 320, true);
      CHECK(diff(peg, m1l, 5200, 8800) < diff(peg, m0l, 5200, 8800), "pitch-envelope scan starts high, near Position 1 (%.3f vs pos0 %.3f)", diff(peg, m1l, 5200, 8800), diff(peg, m0l, 5200, 8800));
      CHECK(diff(peg, m0l, 31000, 38000) < 0.03, "pitch-envelope scan has fallen to Position 0 after its time (%.4f)", diff(peg, m0l, 31000, 38000)); }
    std::printf("%d failure(s)\n", fails); return fails ? 1 : 0;
}
