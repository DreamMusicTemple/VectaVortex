// Round 25: matrix destinations for the SID / LUA layer EQ bands and for the vocoder's live controls.
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include "vv/performance.hpp"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
using namespace vv;
int main() {
    int layer, dest, g; double scale;
    for (const char* d : { "GEN:SID.eqBand1Freq", "GEN:SID.eqBand4Gain", "GEN:LUA.eqBand2Freq", "GEN:LUA.eqBand3Gain", "GEN:VOC.voc_mix", "GEN:VOC.voc_formantSt", "GEN:VOC.voc_chorusRate" }) {
        const bool ok = parseModDest(d, layer, dest, g, scale); CHECK(ok, "%s must parse", d);
        if (std::string(d).find("VOC") != std::string::npos) CHECK(dest >= D_VOC0 && dest <= D_VOC_END, "%s is a vocoder destination", d); else CHECK(dest >= D_GEN0 && dest <= D_GEN_END, "%s is a generic destination", d);
    }
    CHECK(!parseModDest("GEN:VOC.voc_nonsense", layer, dest, g, scale), "unknown vocoder key must not parse");
    // vocoder dry/wet + formant follow a macro routed at full amount
    Synth syn; syn.prepare(48000); syn.seedRandom(3);
    const char* js = "{\"macros\":[1,0,0,0],\"vocoder\":{\"on\":true,\"mix\":0.2,\"formantSt\":0,\"mode\":\"classic\"},\"connections\":[{\"source\":\"macro1\",\"dest\":\"GEN:VOC.voc_mix\",\"amount\":50},{\"source\":\"macro1\",\"dest\":\"GEN:VOC.voc_formantSt\",\"amount\":100}],\"bpm\":120}";
    syn.setPreset(parsePresetText(js)); std::vector<float> l(128), r(128);
    for (int i = 0; i < 120; i++) syn.process(l.data(), r.data(), 128);
    const double mix = syn.vocoder().P.mix, fm = syn.vocoder().P.formantSt;
    std::printf("vocoder mix %.3f formant %.2f\n", mix, fm);
    CHECK(mix > 0.6 && mix < 0.8, "mix 0.2 + 0.5 should be ~0.7 (got %.3f)", mix);
    CHECK(fm > 11.5, "formant should be pushed to +12 (got %.2f)", fm);
    // removing the routes restores the preset's own values
    syn.setPreset(parsePresetText("{\"vocoder\":{\"on\":true,\"mix\":0.2,\"formantSt\":0},\"bpm\":120}"));
    for (int i = 0; i < 120; i++) syn.process(l.data(), r.data(), 128);
    CHECK(std::fabs(syn.vocoder().P.mix - 0.2) < 1e-6 && std::fabs(syn.vocoder().P.formantSt) < 1e-6, "dials restored after the routes are removed");
    // LUA layer EQ gain route reaches the smoother target
    Synth s2; s2.prepare(48000); s2.setPreset(parsePresetText("{\"macros\":[1,0,0,0],\"layers\":{\"LUA\":{\"eq\":{\"band2\":{\"freq\":800,\"gain\":0}}}},\"connections\":[{\"source\":\"macro1\",\"dest\":\"GEN:LUA.eqBand2Gain\",\"amount\":100}],\"bpm\":120}"));
    for (int i = 0; i < 120; i++) s2.process(l.data(), r.data(), 128);
    std::printf("route ok\n");
    std::printf(fails ? "r25: %d failures\n" : "r25: all passed\n", fails); return fails ? 1 : 0;
}
