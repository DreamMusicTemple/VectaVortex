// Scope lanes: per-oscillator capture of the Synth (web: Zones tab SCOPE pill)
#include <cmath>
#include <cstdio>
#include <vector>
#include <vv/performance.hpp>
using namespace vv;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
int main() {
    const std::string preset = "{\"layers\":{\"PRO\":{\"type\":\"procedural\",\"env\":{\"a\":0.01,\"d\":0.2,\"s\":1,\"r\":0.25},\"oscs\":[{\"enabled\":true,\"model\":\"flute\",\"level\":0.9,\"flute\":{\"instrument\":\"shinobue\"}},{\"enabled\":false}]}}}";
    Synth syn; syn.prepare(48000); syn.seedRandom(5); auto P = parsePresetText(preset); syn.setPreset(P);
    std::vector<float> L(128), R(128), t(1024);
    CHECK(!syn.scopeTapRead(3, 0, t.data(), 1024), "an untapped lane must report not-captured");
    syn.setScopeTapMask(3, 1u);
    syn.noteOn(1 << 3, 74, 0.8); for (int i = 0; i < 300; i++) syn.process(L.data(), R.data(), 128);
    CHECK(syn.scopeTapRead(3, 0, t.data(), 1024), "tapped lane not readable");
    double e = 0; for (float v : t) { CHECK(std::isfinite(v), "non-finite tap"); e += (double)v * v; } e = std::sqrt(e / 1024);
    CHECK(e > 0.005, "tap of the sounding generator is silent (rms %.5f)", e);
    CHECK(!syn.scopeTapRead(3, 1, t.data(), 1024), "generator B was not tapped");
    syn.setScopeTapMask(3, 0u); CHECK(!syn.scopeTapRead(3, 0, t.data(), 1024), "cleared lane still readable");
    std::printf("scope tap rms %.4f, %d failure(s)\n", e, fails); return fails ? 1 : 0;
}
