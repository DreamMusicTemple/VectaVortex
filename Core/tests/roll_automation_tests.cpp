// Piano-roll playback, automation lanes, mixer mute / solo / A gating and the host-transport hook.
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include "vv/performance.hpp"
using namespace vv;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct Rig {
    Synth syn; Performance perf; std::shared_ptr<Preset> preset; std::vector<double> starts; std::vector<int> notes; uint64_t lastAge = 0; double rms = 0; long rmsN = 0;
    explicit Rig(const std::string& json) {
        auto wt = std::make_shared<WtTable>(); wt->tableSize = 256; std::vector<float> fr(256); for (int i = 0; i < 256; i++) fr[i] = (float)std::sin(2 * 3.14159265358979 * i / 256); wt->frames.push_back(fr);
        syn.prepare(48000); syn.setWavetable("analogMorph", wt);
        preset = parsePresetText(json); syn.tickSeconds = 0; syn.setPreset(preset); perf.attach(&syn); perf.setPreset(preset);
    }
    void run(double seconds) {
        const int q = (int)(seconds * 48000 / 128); std::vector<float> L(128), R(128);
        for (int i = 0; i < q; i++) {
            perf.advance(syn.clock(), 128); syn.process(L.data(), R.data(), 128);
            for (auto& v : syn.voices()) if (v.alive && v.layer == 0 && v.age > lastAge) { starts.push_back(v.startT); notes.push_back(v.note); }
            for (auto& v : syn.voices()) lastAge = std::max(lastAge, v.age);
            for (int k = 0; k < 128; k++) { rms += (double)L[k] * L[k]; rmsN++; }
        }
    }
    double level() const { return rmsN ? std::sqrt(rms / rmsN) : 0; }
};
static std::string rollPreset(const std::string& notes, const std::string& extra = "", const std::string& steps = "[]", const std::string& conns = "[]", const std::string& pre = "\"loopEnd\":1") {
    return "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8}],\"currentSlot\":0,\"patterns\":[{\"length\":16,\"steps\":" + steps +
           ",\"roll\":{\"bars\":1,\"loopOn\":true,\"loopStart\":0," + pre + ",\"notes\":" + notes + "}}]}},\"bpm\":120,\"connections\":" + conns + extra + "}";
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

static void testRollTiming() {
    Rig r(rollPreset("[{\"t\":0,\"l\":0.5,\"p\":60,\"v\":0.8},{\"t\":1,\"l\":0.5,\"p\":64,\"v\":0.8},{\"t\":2.5,\"l\":0.5,\"p\":67,\"v\":0.8}]"));
    r.perf.startTransport(); r.run(4.6);   // 120 bpm: 1 beat = 0.5 s; the 1-bar loop is 2 s
    CHECK(r.notes.size() == 8, "8 note starts expected in 4.6 s, got %zu", r.notes.size());
    const double exp[] = {0, 0.5, 1.25, 2, 2.5, 3.25, 4, 4.5};
    for (size_t i = 0; i < r.starts.size() && i < 8; i++) CHECK(near(r.starts[i], exp[i], 0.003), "note %zu starts at %.4f expected %.4f", i, r.starts[i], exp[i]);
    CHECK(r.notes.size() >= 3 && r.notes[0] == 60 && r.notes[1] == 64 && r.notes[2] == 67, "note order");
    CHECK(near(r.perf.beat(), 9.2, 0.05), "beat clock %.3f", r.perf.beat());
}
static void testRollReplacesSteps() {
    std::string steps = "["; for (int i = 0; i < 16; i++) steps += std::string(i ? "," : "") + "{\"on\":true,\"pitch\":0}"; steps += "]";
    Rig r(rollPreset("[{\"t\":0,\"l\":0.5,\"p\":72,\"v\":0.8}]", "", steps));
    r.perf.startTransport(); r.run(1.9);
    CHECK(r.notes.size() == 1 && r.notes[0] == 72, "a pattern with a roll must not also play its step grid (got %zu notes)", r.notes.size());
    Rig s("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8}],\"currentSlot\":0,\"patterns\":[{\"length\":16,\"steps\":" + steps + "}]}},\"bpm\":120}");
    s.perf.startTransport(); s.run(1.9);
    CHECK(s.notes.size() >= 14, "old step patterns without a roll keep playing (got %zu)", s.notes.size());
}
static void testProbabilityRatchet() {
    Rig r(rollPreset("[{\"t\":0,\"l\":1,\"p\":60,\"v\":0.8,\"prob\":0},{\"t\":2,\"l\":1,\"p\":62,\"v\":0.8,\"rat\":2}]"));
    r.perf.startTransport(); r.run(1.9);
    CHECK(r.notes.size() == 2 && r.notes[0] == 62 && r.notes[1] == 62, "prob 0 skipped, ratchet 2 = two hits (got %zu)", r.notes.size());
    if (r.starts.size() == 2) CHECK(near(r.starts[1] - r.starts[0], 0.25, 0.003), "ratchet spacing %.4f", r.starts[1] - r.starts[0]);
}
static void testLoopRegion() {   // loop bars 1..2 of a 2-bar roll: playback starts at the loop start
    std::string j = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8}],\"currentSlot\":0,\"patterns\":[{\"length\":16,\"steps\":[],\"roll\":{\"bars\":2,\"loopOn\":true,\"loopStart\":1,\"loopEnd\":2,\"notes\":[{\"t\":0,\"l\":1,\"p\":50,\"v\":0.8},{\"t\":4,\"l\":1,\"p\":55,\"v\":0.8}]}}]}},\"bpm\":120}";
    Rig r(j); r.perf.startTransport(); r.run(4.1);
    CHECK(r.notes.size() >= 2 && r.notes[0] == 55, "only the looped bar plays (first note %d)", r.notes.empty() ? -1 : r.notes[0]);
    bool any50 = false; for (int n : r.notes) any50 = any50 || n == 50; CHECK(!any50, "outside the loop must stay silent");
}
static void testHostTransport() {
    Rig r(rollPreset("[{\"t\":0,\"l\":0.5,\"p\":60,\"v\":0.8},{\"t\":1,\"l\":0.5,\"p\":64,\"v\":0.8}]"));
    r.perf.setHostTransport(true, 1.0, 90); r.perf.startTransport(); r.run(0.9);   // host playhead starts at beat 1: the note at beat 1 is first, beat 1 = 0 s
    CHECK(r.perf.bpm == 90, "host tempo adopted");
    CHECK(!r.notes.empty() && r.notes[0] == 64, "host playhead decides where the roll starts (first note %d)", r.notes.empty() ? -1 : r.notes[0]);
}
static double levelFor(const std::string& extra, const std::string& conns = "[]") {
    if (getenv("RA_DEBUG")) printf("%s\n", rollPreset("[]", extra, "[]", conns).c_str());
    Rig r(rollPreset("[{\"t\":0,\"l\":4,\"p\":60,\"v\":1}]", extra, "[]", conns, "\"loopEnd\":1")); r.perf.startTransport(); r.run(1.5); if (getenv("RA_DEBUG")) printf("flags mute0=%d notes=%zu level=%g\n", (int)r.preset->mixMute[0], r.notes.size(), r.level()); return r.level();
}
static void testMixerGates() {
    const double base = levelFor("");
    CHECK(base > 0.01, "baseline audible (%.5f)", base);
    CHECK(levelFor(",\"extra\":{\"mixerMute\":{\"WT1\":true}}") < base * 0.05, "mute silences the track");
    CHECK(levelFor(",\"extra\":{\"mixerSolo\":{\"SMP1\":true}}") < base * 0.05, "solo elsewhere silences this track");
    CHECK(levelFor(",\"extra\":{\"mixerSolo\":{\"WT1\":true}}") > base * 0.9, "soloed track plays");
    CHECK(levelFor(",\"extra\":{\"mixerMute\":{\"MASTER\":true}}") < base * 0.05, "master mute");
}
static void testAutomationLane() {
    std::string lane0 = "\"v\":[", lane1 = "\"v\":["; for (int i = 0; i < 32; i++) { lane0 += std::string(i ? "," : "") + "0"; lane1 += std::string(i ? "," : "") + "0.5"; } lane0 += "]"; lane1 += "]";
    auto ex = [&](const std::string& lane, bool a) { return std::string(",\"extra\":{\"mixerAuto\":{\"WT1\":") + (a ? "true" : "false") + "},\"automation\":{\"bars\":1,\"lanes\":{\"MIX:WT1\":{\"d\":1," + lane + "}}}}"; };
    const double base = levelFor(ex(lane1, true));   // neutral lane = no change
    const double low = levelFor(ex(lane0, true));    // lane at 0 = -24 dB around the fader
    const double off = levelFor(ex(lane0, false));   // A off: the lane does not play
    std::printf("neutral %.5f  lane@0 %.5f  A-off %.5f  ratio %.3f (expect %.3f)\\n", base, low, off, low / base, std::pow(10.0, -24 / 20.0));
    CHECK(near(low / base, std::pow(10.0, -24 / 20.0), 0.02), "lane at 0 lowers the track 24 dB");
    CHECK(near(off / base, 1.0, 0.02), "lane with A off does nothing");
}
static void testLaneEnableMapping() {
    Preset P; P.mixAuto[mixIndex("WT2")] = true; P.mixAuto[kNumMix] = false;
    CHECK(modelio::autoLaneEnabled(P, "GEN:WT.cutoff"), "WT lane follows either WT channel");
    CHECK(modelio::autoLaneEnabled(P, "WT:osc2Scan") && !modelio::autoLaneEnabled(P, "WT:osc1Scan"), "per-oscillator lane");
    CHECK(!modelio::autoLaneEnabled(P, "FXP:reverb:size") && !modelio::autoLaneEnabled(P, "S:macro1"), "FX / source lanes belong to Master");
    P.mixAuto[kNumMix] = true; CHECK(modelio::autoLaneEnabled(P, "FXP:reverb:size") && modelio::autoLaneEnabled(P, "S:macro1"), "Master A enables them");
    Preset Q; Q.mixAuto[mixIndex("SMP3")] = true; CHECK(modelio::autoLaneEnabled(Q, "SMP:s3Cutoff") && !modelio::autoLaneEnabled(Q, "SMP:s4Cutoff") && modelio::autoLaneEnabled(Q, "GEN:SMP.pan"), "sampler slots");
}

// New Note Next / Random Sample + pad slices
static std::string smpPreset(const char* mode) {
    std::string o = ""; for (int i = 0; i < 4; i++) { if (i) o += ","; o += "{\"name\":\"s" + std::to_string(i) + "\",\"level\":0.8,\"rangeLow\":0,\"rangeHigh\":" + std::string(i == 3 ? "40" : "127") + "}"; }
    return "{\"layers\":{\"SMP\":{\"type\":\"sample\",\"slotAdvance\":\"" + std::string(mode) + "\",\"oscs\":[" + o + "]}},\"bpm\":120}";
}
static std::vector<int> playSlots(const char* mode, int n, std::vector<int>* usedOut = nullptr) {
    Synth syn; syn.prepare(48000); syn.seedRandom(7); auto P = parsePresetText(smpPreset(mode)); syn.tickSeconds = 0; syn.setPreset(P);
    for (int i = 0; i < 4; i++) { auto b = std::make_shared<SampleBuf>(); b->len = 4800; b->d[0].assign(4800, 0.3f); if (i == 1) b->d[0].clear(), b->len = 0; syn.setSample("s" + std::to_string(i), i == 1 ? nullptr : b); }
    std::vector<int> got; std::vector<float> L(128), R(128);
    for (int k = 0; k < n; k++) {
        syn.noteOn(1 << L_SMP, 60 + k, 0.8, nullptr, false); syn.process(L.data(), R.data(), 128);
        for (auto& v : syn.voices()) if (v.alive && v.layer == L_SMP && v.note == 60 + k) { int cnt = 0, which = -1; for (int i = 0; i < 4; i++) if (v.smp[i].on) { cnt++; which = i; } got.push_back(cnt == 1 ? which : -cnt - 10); }
    }
    return got;
}
static void testSlotAdvance() {
    auto off = playSlots("off", 1); CHECK(off.size() == 1 && off[0] == -12, "off: every in-range slot plays together (got %d)", off.empty() ? 0 : off[0]);
    auto nx = playSlots("next", 7); const int want[7] = {0, 2, 0, 2, 0, 2, 0};   // slot 1 is empty, slot 3 is out of zone for notes > 40
    bool ok = nx.size() == 7; for (size_t i = 0; ok && i < 7; i++) ok = nx[i] == want[i];
    CHECK(ok, "next walks playable slots 0,2,0,2.. (got %d %d %d %d)", nx.size() > 3 ? nx[0] : -9, nx.size() > 3 ? nx[1] : -9, nx.size() > 3 ? nx[2] : -9, nx.size() > 3 ? nx[3] : -9);
    auto rn = playSlots("random", 40); int seen[4] = {0, 0, 0, 0}; bool good = rn.size() == 40; for (int v : rn) { if (v < 0 || v > 3) good = false; else seen[v]++; }
    CHECK(good && seen[0] > 0 && seen[2] > 0 && seen[1] == 0 && seen[3] == 0, "random only picks playable slots, both used (%d %d)", seen[0], seen[2]);
}
int main() {
    testRollTiming(); testRollReplacesSteps(); testProbabilityRatchet(); testLoopRegion(); testHostTransport(); testMixerGates(); testAutomationLane(); testLaneEnableMapping(); testSlotAdvance();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("roll / automation tests passed\n"); return 0;
}
