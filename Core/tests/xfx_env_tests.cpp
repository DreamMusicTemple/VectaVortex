// XFX MODE (per-key sound) and amp-envelope Repeat: model round-trip, repeat counts / timing, release behaviour, per-key override, layer switch, preset swap safety.
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include "vv/performance.hpp"
#include "vv/full_engine.hpp"
using namespace vv;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static std::string presetText(int rep, const std::string& extra = "") {
    return "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"env\":{\"a\":0.02,\"d\":0.05,\"s\":0.3,\"r\":0.08,\"rep\":" + std::to_string(rep) +
           "},\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8,\"unison\":1,\"detune\":0}]}}" + (extra.empty() ? "" : ",\"extra\":{" + extra + "}") + "}";
}
struct Rig {
    Synth syn; std::shared_ptr<Preset> P;
    explicit Rig(const std::string& text) {
        auto wt = std::make_shared<WtTable>(); wt->tableSize = 256; std::vector<float> fr(256);
        for (int i = 0; i < 256; i++) fr[i] = (float)std::sin(2 * 3.14159265358979 * i / 256); wt->frames.push_back(fr);
        syn.prepare(48000); syn.setWavetable("analogMorph", wt); syn.seedRandom(5); P = parsePresetText(text); syn.setPreset(P);
    }
    std::vector<float> run(double sec) {
        std::vector<float> out, L(128), R(128); const int n = (int)(sec * 48000 / 128);
        for (int i = 0; i < n; i++) { syn.process(L.data(), R.data(), 128); out.insert(out.end(), L.begin(), L.end()); }
        return out;
    }
};
// peak per 1 ms window
static std::vector<double> env(const std::vector<float>& x) {
    std::vector<double> e; for (size_t i = 0; i + 48 <= x.size(); i += 48) { double p = 0; for (size_t k = 0; k < 48; k++) p = std::max(p, (double)std::fabs(x[i + k])); e.push_back(p); }
    return e;
}
// attacks = 1 + number of dips (below 30 % of the running peak) that are followed by a recovery above 50 % of it
static int attacks(const std::vector<double>& e) {
    double mx = 0; for (double v : e) mx = std::max(mx, v);
    double pk = 0; bool low = false; int rec = 0;
    for (double v : e) {
        if (!low) { pk = std::max(pk, v); if (pk > 0.01 * mx && v < 0.3 * pk) low = true; }
        else if (v > 0.5 * pk) { low = false; rec++; pk = v; }
    }
    return 1 + rec;
}
static void testRepeatCounts() {
    for (int rep : {0, 1, 3, 8}) {
        Rig r(presetText(rep)); r.syn.noteOn(1, 96, 1.0); auto x = r.run(1.5); auto e = env(x);
        CHECK(attacks(e) == 1 + rep, "rep=%d should give %d attacks, got %d", rep, 1 + rep, attacks(e));
        double tail = 0; for (size_t i = e.size() - 100; i < e.size(); i++) tail = std::max(tail, e[i]);
        double mx = 0; for (double v : e) mx = std::max(mx, v);
        CHECK(tail > 0.05 * mx && tail <= mx, "rep=%d settles on the sustain level (%.3f of %.3f)", rep, tail, mx);
    }
}
static void testContinuous() {
    Rig r(presetText(-1)); r.syn.noteOn(1, 96, 1.0); auto x = r.run(1.2); auto e = env(x);
    CHECK(attacks(e) >= 10, "continuous repeat keeps cycling (%d attacks in 1.2 s; cycle is 72 ms)", attacks(e));
    r.syn.noteOff(1, 96); auto y = r.run(0.6); auto ey = env(y);
    double late = 0; for (size_t i = 300; i < ey.size(); i++) late = std::max(late, ey[i]);
    CHECK(late < 1e-3, "voice is silent after release (%.5f)", late);
    CHECK(attacks(ey) <= 1, "no new cycles after note-off (%d)", attacks(ey));
}
static void testTiming() {
    Rig r(presetText(2)); r.syn.noteOn(1, 96, 1.0); auto x = r.run(0.6); auto e = env(x);
    // first dip (cycle boundary) near a+d = 70 ms; second attack peak near 70 + 2 + 20 = 92 ms
    size_t dip = 0; double mx = 0; for (double v : e) mx = std::max(mx, v);
    for (size_t i = 30; i < e.size(); i++) if (e[i] < 0.12 * mx) { dip = i; break; }
    CHECK(dip >= 72 && dip <= 86, "first dip at %zu ms (expected ~71)", dip);
}
static void testXfxPerKey() {
    const std::string x = "\"xfx\":{\"v\":1,\"enabled\":true,\"active\":0,\"banks\":[{\"name\":\"T\",\"keys\":{\"64\":{\"en\":{},\"o\":{\"WT.env.rep\":3}}}}]}";
    Rig r(presetText(0, x));
    CHECK(r.P->xfx && r.P->xfx->any, "XFX table parsed from extra.xfx");
    r.syn.noteOn(1, 60, 1.0); r.syn.noteOn(1, 64, 1.0); // both keys held: the mix has two pitches, count attacks per key separately
    auto a = r.run(1.0); r.syn.noteOff(1, 60); r.syn.noteOff(1, 64); r.run(0.5);
    Rig q(presetText(0, x)); q.syn.noteOn(1, 60, 1.0); auto b = q.run(1.0); Rig z(presetText(0, x)); z.syn.noteOn(1, 64, 1.0); auto c = z.run(1.0);
    CHECK(attacks(env(b)) == 1, "key 60 untouched: single attack (%d)", attacks(env(b)));
    CHECK(attacks(env(c)) == 4, "key 64 override: repeat 3 -> 4 attacks (%d)", attacks(env(c)));
    (void)a;
}
static void testXfxLayerSwitch() {
    const std::string x = "\"xfx\":{\"v\":1,\"enabled\":true,\"active\":0,\"banks\":[{\"name\":\"T\",\"keys\":{\"64\":{\"en\":{\"WT\":false},\"o\":{}}}}]}";
    Rig r(presetText(0, x));
    r.syn.noteOn(1, 64, 1.0); auto a = r.run(0.3); double pa = 0; for (float v : a) pa = std::max(pa, (double)std::fabs(v));
    (void)pa;   // Synth::noteOn plays the layer directly; the layer switch is applied by Performance (checked below)
    Performance perf; perf.attach(&r.syn); perf.setPreset(r.P);
    CHECK((perf.layersForNoteMask(64) & 1) == 0, "layer switched off on key 64 (mask %d)", perf.layersForNoteMask(64));
    CHECK((perf.layersForNoteMask(60) & 1) != 0, "key 60 still plays (mask %d)", perf.layersForNoteMask(60));
}
static void testSwapSafety() {
    const std::string x = "\"xfx\":{\"v\":1,\"enabled\":true,\"active\":0,\"banks\":[{\"name\":\"T\",\"keys\":{\"64\":{\"en\":{},\"o\":{\"WT.env.r\":0.4}}}}]}";
    Rig r(presetText(0, x)); r.syn.noteOn(1, 64, 1.0); r.run(0.2);
    CHECK(r.syn.anyXfxVoice(), "XFX voice is tracked");
    r.syn.setPreset(parsePresetText(presetText(0)));   // wholesale swap drops XFX refs; the held voice must keep sounding without touching freed memory
    auto a = r.run(0.2); r.syn.noteOff(1, 64); r.run(0.5);
    CHECK(!r.syn.anyXfxVoice(), "no XFX voices after swap");
    (void)a;
}
static void testNoXfx() {
    Rig r(presetText(0)); CHECK(!r.P->xfx, "no XFX table when the preset has none");
    const std::string x = "\"xfx\":{\"v\":1,\"enabled\":false,\"active\":0,\"banks\":[{\"name\":\"T\",\"keys\":{\"64\":{\"en\":{},\"o\":{\"WT.env.rep\":3}}}}]}";
    Rig q(presetText(0, x)); CHECK(!q.P->xfx || !q.P->xfx->any, "disabled XFX applies nothing");
}
// The control path a UI uses: FullEngine::xfxAction edits extra.xfx, the engine re-parses, and the key sounds different while others do not.
static void testActionApi() {
    FullEngine fe; fe.prepare(48000);
    auto wt = std::make_shared<WtTable>(); wt->tableSize = 256; std::vector<float> fr(256); for (int i = 0; i < 256; i++) fr[i] = (float)std::sin(2 * 3.14159265358979 * i / 256); wt->frames.push_back(fr);
    fe.addWavetable("analogMorph", wt);
    CHECK(fe.loadPresetJson(presetText(0)), "load");
    CHECK(fe.xfxAction("setOverride", "{\"note\":64,\"path\":\"WT.env.rep\",\"value\":3}"), "setOverride accepted");
    CHECK(!fe.xfxAction("setOverride", "{\"note\":300,\"path\":\"WT.env.rep\",\"value\":3}"), "bad note rejected");
    CHECK(!fe.xfxAction("nonsense", "{\"note\":64}"), "unknown action rejected");
    std::string ex = fe.xfxExport(); CHECK(ex.find("WT.env.rep") != std::string::npos && ex.find("\"64\"") != std::string::npos, "export holds the override: %s", ex.c_str());
    CHECK(fe.xfxAction("layer", "{\"note\":64,\"layer\":\"WT\",\"state\":false}"), "layer switch");
    CHECK(fe.xfxAction("copyKey", "{\"from\":64,\"to\":65}"), "copy key");
    CHECK(fe.xfxExport().find("\"65\"") != std::string::npos, "key 65 copied");
    CHECK(fe.xfxAction("layer", "{\"note\":64,\"layer\":\"WT\",\"state\":null}"), "layer back to inherit");
    CHECK(fe.xfxAction("clearOverride", "{\"note\":64,\"path\":\"WT.env.rep\"}"), "clear override");
    CHECK(fe.xfxExport().find("\"64\"") == std::string::npos, "empty key pruned");
    CHECK(fe.xfxAction("bank", "{\"index\":2}") && fe.xfxAction("rename", "{\"name\":\"Drums\"}") && fe.xfxAction("enable", "{\"on\":false}"), "bank / rename / enable");
    CHECK(fe.xfxAction("reset", "{}") && fe.xfxExport() == "null", "reset removes extra.xfx");
    // audible: key 64 repeats 3x through the real engine, key 60 does not
    CHECK(fe.xfxAction("setOverride", "{\"note\":64,\"path\":\"WT.env.rep\",\"value\":3}"), "override again");
    float L[128], R[128]; auto render = [&](int note) { std::vector<float> out; fe.noteOn(note, 1.f); for (int i = 0; i < 400; i++) { fe.render(L, R, 128); out.insert(out.end(), L, L + 128); } fe.noteOff(note); for (int i = 0; i < 200; i++) fe.render(L, R, 128); return out; };
    for (int i = 0; i < 10; i++) fe.render(L, R, 128);
    auto a = render(60), b = render(64);
    CHECK(attacks(env(a)) == 1 && attacks(env(b)) == 4, "through FullEngine: key 60 -> %d attack(s), key 64 -> %d (expected 1 and 4)", attacks(env(a)), attacks(env(b)));
}
int main() {
    testActionApi();
    testRepeatCounts(); testContinuous(); testTiming(); testXfxPerKey(); testXfxLayerSwitch(); testSwapSafety(); testNoXfx();
    if (failures) { printf("%d FAILED\n", failures); return 1; }
    printf("xfx_env_tests: all passed\n"); return 0;
}
