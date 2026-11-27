// Battery Saving Mode: policy table, bit-exact when off, voice reaping, FX / EQ idle sleep, wake-up, profile differences, and a render-cost benchmark.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include "vv/performance.hpp"
using namespace vv;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char* kPreset = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"env\":{\"a\":0.005,\"d\":0.2,\"s\":0.7,\"r\":0.6},\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8,\"unison\":4,\"detune\":12}]}},"
    "\"fxStages\":[{\"type\":\"reverb\",\"id\":1,\"enabled\":true,\"sends\":{\"WT\":true},\"params\":{\"mix\":0.5,\"size\":0.9}},{\"type\":\"delay\",\"id\":2,\"enabled\":true,\"sends\":{\"WT\":true},\"params\":{\"mix\":0.4,\"feedback\":0.5,\"time\":0.25}}],\"bpm\":120}";
struct Rig {
    Synth syn; std::shared_ptr<Preset> P;
    Rig() { auto wt = std::make_shared<WtTable>(); wt->tableSize = 256; std::vector<float> fr(256); for (int i = 0; i < 256; i++) fr[i] = (float)std::sin(2 * 3.14159265358979 * i / 256); wt->frames.push_back(fr);
        syn.prepare(48000); syn.setWavetable("analogMorph", wt); syn.seedRandom(5); P = parsePresetText(kPreset); syn.setPreset(P); }
    std::vector<float> run(double sec) { std::vector<float> out; std::vector<float> L(128), R(128); const int n = (int)(sec * 48000 / 128); for (int i = 0; i < n; i++) { syn.process(L.data(), R.data(), 128); out.insert(out.end(), L.begin(), L.end()); } return out; }
};
static double peakOf(const std::vector<float>& v, size_t a, size_t b) { double p = 0; for (size_t i = a; i < b && i < v.size(); i++) p = std::max(p, (double)std::fabs(v[i])); return p; }

static void testPolicy() {
    PowerCfg c; c.profile = 0; auto off = PowerPolicy::forConfig(c, 48000); CHECK(off.voiceBlocks == 0 && off.fxBlocks == 0 && off.uiDivider == 1 && off.modSettleTaus > 1e8f, "off = nothing");
    PowerPolicy p[4]; for (int i = 1; i <= 3; i++) { c.profile = i; p[i] = PowerPolicy::forConfig(c, 48000); }
    CHECK(p[1].voiceBlocks > p[2].voiceBlocks && p[2].voiceBlocks > p[3].voiceBlocks, "voice guard shrinks Light > Balanced > Max");
    CHECK(p[1].fxBlocks > p[2].fxBlocks && p[2].fxBlocks > p[3].fxBlocks, "fx guard shrinks");
    CHECK(p[1].fxThresh < p[2].fxThresh && p[2].fxThresh < p[3].fxThresh, "thresholds grow");
    CHECK(p[1].modSettleTaus > p[2].modSettleTaus && p[2].modSettleTaus > p[3].modSettleTaus && p[1].uiDivider < p[2].uiDivider && p[2].uiDivider < p[3].uiDivider, "mod + ui differ per profile");
    c.profile = 2; c.flags = PW_FX; auto only = PowerPolicy::forConfig(c, 48000); CHECK(only.fxBlocks > 0 && only.voiceBlocks == 0 && only.eqBlocks == 0 && only.uiDivider == 1, "independent switches");
}
// Off must be sample-identical to a rig that never heard of power settings.
static void testOffIdentical() {
    Rig a, b; b.syn.setPower(PowerCfg{ 0, 31 });
    a.syn.noteOn(1, 60, 0.8); b.syn.noteOn(1, 60, 0.8); auto x = a.run(0.5); a.syn.noteOff(1, 60); b.run(0.5); b.syn.noteOff(1, 60); auto y = a.run(1.0); auto y2 = b.run(1.0);
    bool same = y.size() == y2.size(); for (size_t i = 0; same && i < y.size(); i++) same = y[i] == y2[i]; CHECK(same, "profile 0 renders identically"); (void) x;
}
static void testSleepAndWake() {
    // a short note through reverb + delay: tail must be intact (matches the unoptimised render within the profile's threshold) and the stages must eventually sleep
    for (int prof = 1; prof <= 3; prof++) {
        Rig ref, opt; opt.syn.setPower(PowerCfg{ prof, 31 });
        ref.syn.noteOn(1, 60, 0.8); opt.syn.noteOn(1, 60, 0.8); ref.run(0.3); opt.run(0.3); ref.syn.noteOff(1, 60); opt.syn.noteOff(1, 60);
        auto a = ref.run(30.0), b = opt.run(30.0); double maxd = 0; for (size_t i = 0; i < a.size(); i++) maxd = std::max(maxd, (double)std::fabs(a[i] - b[i]));
        const double tol = prof == 1 ? 2e-6 : prof == 2 ? 5e-6 : 2e-5;
        CHECK(maxd < tol, "profile %d: render differs from unoptimised by %.3g (tolerance %.1g)", prof, maxd, tol);
        const auto& st = opt.syn.powerStats(); printf("profile %d: max diff %.3g  voices reaped %llu  fx blocks skipped %llu  eq blocks skipped %llu\n", prof, maxd, (unsigned long long)st.voicesReaped.load(), (unsigned long long)st.fxBlocksSkipped.load(), (unsigned long long)st.eqBlocksSkipped.load());
        CHECK(st.eqBlocksSkipped.load() > 0, "profile %d: layer EQ idles after the tail", prof);
        // wake: a second note after everything slept must sound exactly as strong as on the reference
        ref.syn.noteOn(1, 64, 0.8); opt.syn.noteOn(1, 64, 0.8); auto c = ref.run(0.5), d = opt.run(0.5);
        CHECK(peakOf(d, 0, d.size()) > 0.05 && std::fabs(peakOf(c, 0, c.size()) - peakOf(d, 0, d.size())) < 0.02, "profile %d: wakes correctly (%.3f vs %.3f)", prof, peakOf(c, 0, c.size()), peakOf(d, 0, d.size()));
    }
}
static double benchMs(int prof, int voices, double sec, uint64_t* skipped = nullptr) {
    Rig r; r.syn.setPower(PowerCfg{ prof, getenv("PW_FLAGS") ? (uint32_t)atoi(getenv("PW_FLAGS")) : 31u }); r.run(0.01);
    for (int k = 0; k < voices; k++) r.syn.noteOn(1, 36 + k * 3, 0.7);
    std::vector<float> L(128), R(128); const int n = (int)(sec * 48000 / 128);
    // scenario: chord for 1 s, release, then long tails and silence (the typical idle / sparse-playing pattern)
    const auto t0 = std::chrono::steady_clock::now(); double tTail = 0; std::chrono::steady_clock::time_point tm;
    for (int i = 0; i < n; i++) { if (i == (int)(3.0 * 48000 / 128)) tm = std::chrono::steady_clock::now(); if (i == (int)(1.0 * 48000 / 128)) for (int k = 0; k < voices; k++) r.syn.noteOff(1, 36 + k * 3); r.syn.process(L.data(), R.data(), 128); }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); tTail = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tm).count(); if (getenv("PW_TAIL")) printf("    [prof %d voices %d: tail(3s..end) %.1f ms of %.1f]\n", prof, voices, tTail, ms);
    if (skipped) *skipped = r.syn.powerStats().fxBlocksSkipped.load() + r.syn.powerStats().eqBlocksSkipped.load() + r.syn.powerStats().voicesReaped.load(); return ms;
}

static void testVoiceReap() {
    const std::string longRel = std::string(kPreset).replace(std::string(kPreset).find("\"r\":0.6"), 7, "\"r\":8");
    for (int prof = 1; prof <= 3; prof++) {
        Rig ref, opt; ref.P = parsePresetText(longRel); ref.syn.setPreset(ref.P); opt.P = parsePresetText(longRel); opt.syn.setPreset(opt.P); opt.syn.setPower(PowerCfg{ prof, PW_VOICES });
        ref.syn.noteOn(1, 60, 0.8); opt.syn.noteOn(1, 60, 0.8); ref.run(0.5); opt.run(0.5); ref.syn.noteOff(1, 60); opt.syn.noteOff(1, 60);
        auto a = ref.run(40.0), b = opt.run(40.0); double maxd = 0; for (size_t i = 0; i < a.size(); i++) maxd = std::max(maxd, (double)std::fabs(a[i] - b[i]));
        CHECK(maxd < 5e-5, "profile %d: voice reaping changed the tail by %.3g", prof, maxd);
        printf("voice reap profile %d: reaped %llu, max diff %.3g, voices alive at end ref %d opt %d\n", prof, (unsigned long long)opt.syn.powerStats().voicesReaped.load(), maxd, ref.syn.activeVoiceCount(), opt.syn.activeVoiceCount());
    }
}

// A muted layer's released voice is silent for its whole (long) release: that is the case the reaper exists for.
static void testReapMuted() {
    const std::string longRel = std::string(kPreset).replace(std::string(kPreset).find("\"r\":0.6"), 7, "\"r\":8");
    Rig ref, opt; ref.P = parsePresetText(longRel); ref.P->mixMute[mixIndex("WT1")] = true; ref.syn.setPreset(ref.P); opt.P = parsePresetText(longRel); opt.P->mixMute[mixIndex("WT1")] = true; opt.syn.setPreset(opt.P);
    opt.syn.setPower(PowerCfg{ 3, PW_VOICES }); ref.syn.noteOn(1, 60, 0.8); opt.syn.noteOn(1, 60, 0.8); ref.run(0.5); opt.run(0.5); ref.syn.noteOff(1, 60); opt.syn.noteOff(1, 60); ref.run(2.0); opt.run(2.0);
    CHECK(opt.syn.powerStats().voicesReaped.load() == 1 && opt.syn.activeVoiceCount() == 0 && ref.syn.activeVoiceCount() == 1, "muted released voice freed early: reaped %llu alive opt %d ref %d", (unsigned long long)opt.syn.powerStats().voicesReaped.load(), opt.syn.activeVoiceCount(), ref.syn.activeVoiceCount());
}
int main(int argc, char** argv) {
    testPolicy(); testOffIdentical(); testSleepAndWake(); testVoiceReap(); testReapMuted();
    if (argc > 1 && std::string(argv[1]) == "--bench") {
        const char* names[4] = { "off", "Light", "Balanced", "Maximum" };
        for (int scen = 0; scen < 2; scen++) { const int voices = scen == 0 ? 1 : 8; printf("\n%d-voice chord, 1 s held then 29 s release / silence (unison 4 + reverb + delay), render time for 30 s of audio:\n", voices);
            for (int p = 0; p < 4; p++) { double best = 1e9; for (int rep = 0; rep < 3; rep++) best = std::min(best, benchMs(p, voices, 30.0)); printf("  %-9s %8.1f ms   (%.2f%% of real time)\n", names[p], best, best / 300.0); } }
    }
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("power tests passed\n"); return 0;
}
