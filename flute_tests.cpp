// Wooden flutes: exact parity with the web FluteCore reference values, stability fuzz, and the voice engine (model "flute") end to end.
#include <cmath>
#include <cstdio>
#include <vector>
#include <vv/flute.hpp>
#include <vv/performance.hpp>
using namespace vv;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void renderN(flute::Core& c, std::vector<float>& o, double f) { for (size_t i = 0; i < o.size(); i += 128) c.render(&o[i], (int)std::min<size_t>(128, o.size() - i), nullptr, f); }
int main() {
    // 1) parity with the web core (energy and samples after one second): shakuhachi, recorder, pan flute
    struct Ref { int inst; double f, e, s0, s1, s2; };
    const Ref refs[3] = { { 0, 293.66, 2145.138769, 0.4449808, -0.2849732, -0.2707753 }, { 4, 523.25, 2852.685530, 0.2035037, 0.3071728, -0.2213890 }, { 6, 392, 2452.028321, 0.0250869, -0.3932163, -0.3266096 } };
    for (auto& r : refs) { flute::Core c(48000, 9); c.setInstrument(r.inst); c.snapParams(); c.noteOn(0.8); std::vector<float> o(48000); renderN(c, o, r.f);
        double e = 0; for (float v : o) e += (double)v * v;
        CHECK(std::fabs(e - r.e) < 1e-3 && std::fabs(o[20000] - r.s0) < 1e-6 && std::fabs(o[30001] - r.s1) < 1e-6 && std::fabs(o[47999] - r.s2) < 1e-6, "web parity instrument %d (energy %.6f)", r.inst, e); }
    // 2) every instrument oscillates, stays finite and bounded across the register, including extreme parameter values
    uint32_t s = 5; auto R = [&]() { s = s * 1664525u + 1013904223u; return s / 4294967296.0; };
    for (int run = 0; run < 150; run++) {
        flute::Core c(48000, 100 + run); c.setInstrument(run % 7); for (int k = 0; k < flute::kNumParams; k++) { const auto& d = flute::kParams[k]; c.setParam(k, d.lo + (d.hi - d.lo) * (R() < 0.3 ? (R() < 0.5 ? 0 : 1) : R())); }
        c.snapParams(); c.noteOn(0.2 + 0.8 * R()); std::vector<float> o(48000 / 2); const double f = 110 * std::pow(2.0, 4.5 * R()); bool bad = false; double pk = 0;
        for (int b = 0; b < 4; b++) { renderN(c, o, f); for (float v : o) { if (!std::isfinite(v)) bad = true; pk = std::max(pk, (double)std::fabs(v)); } if (b == 1) c.noteOff(); }
        CHECK(!bad && pk <= 1.0, "run %d instrument %d unstable (peak %.3f)", run, run % 7, pk);
    }
    // 3) pitch: shakuhachi holds the target within 30 cents on a spread of notes (autocorrelation)
    for (double f : { 220.0, 293.66, 440.0, 880.0, 1760.0 }) { flute::Core c(48000, 9); c.setInstrument(0); c.snapParams(); c.noteOn(0.8); std::vector<float> o(48000); renderN(c, o, f);
        const int a = 24000, n = 8192; int bestL = 0; double best = 0; for (int L = (int)(48000 / (f * 1.3)); L < (int)(48000 / (f * 0.75)); L++) { double sum = 0; for (int i = 0; i < n - L; i++) sum += (double)o[a + i] * o[a + i + L]; if (sum > best) { best = sum; bestL = L; } }
        const double cents = 1200 * std::log2((48000.0 / bestL) / f); CHECK(std::fabs(cents) < 30, "pitch %.1f Hz off by %.0f cents", f, cents); }

    // 4) engine: a Procedural layer whose generator A is the flute model plays through the real Synth, rings while held and falls silent after release
    {
        const std::string preset = "{\"layers\":{\"PRO\":{\"type\":\"procedural\",\"env\":{\"a\":0.01,\"d\":0.2,\"s\":1,\"r\":0.25},\"oscs\":[{\"enabled\":true,\"model\":\"flute\",\"level\":0.9,\"flute\":{\"instrument\":\"shinobue\",\"pressure\":0.55,\"vibrato\":0.2}},{\"enabled\":false}]}}}";
        Synth syn; syn.prepare(48000); syn.seedRandom(5); auto P = parsePresetText(preset); syn.setPreset(P);
        std::vector<float> L(128), R(128); auto run = [&](double sec) { std::vector<float> o; const int n = (int)(sec * 48000 / 128); for (int i = 0; i < n; i++) { syn.process(L.data(), R.data(), 128); o.insert(o.end(), L.begin(), L.end()); } return o; };
        auto rms = [&](const std::vector<float>& v, size_t a, size_t b) { double e = 0; for (size_t i = a; i < b && i < v.size(); i++) e += (double)v[i] * v[i]; return std::sqrt(e / std::max<size_t>(1, b - a)); };
        syn.noteOn(1 << 3, 74, 0.8); auto held = run(0.8); syn.noteOff(1 << 3, 74); auto after = run(1.5);
        const double rh = rms(held, 24000, 38400), rt = rms(after, 48000, 72000);
        CHECK(rh > 0.01, "flute voice silent through the engine (rms %.5f)", rh); CHECK(rt < rh * 0.02, "flute voice did not release (held %.4f, tail %.5f)", rh, rt);
        for (float v : held) CHECK(std::isfinite(v), "non-finite sample");
    }
    std::printf(fails ? "flute tests FAILED (%d)\n" : "flute tests passed\n", fails);
    return fails ? 1 : 0;
}
