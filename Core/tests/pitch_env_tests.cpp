// Round 11: per-oscillator pitch envelope (WT OSC1/2, FM A/B, Procedural A/B) - drawn curve -> semitone offset, shared evalCurveY (incl. straight segments).
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include "vv/performance.hpp"
static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { fails++; printf("FAIL %s\n", m); } } while (0)
static double zcFreq(const std::vector<float>& x, size_t a, size_t b, double sr) {   // positive-going zero crossings per second
    int n = 0; for (size_t i = a + 1; i < b; i++) if (x[i - 1] <= 0 && x[i] > 0) n++; return n / ((b - a) / sr);
}
static std::vector<float> play(const std::string& pe, int blocks, int note = 57) {
    using namespace vv;
    Synth syn; auto wt = std::make_shared<WtTable>(); wt->tableSize = 256; std::vector<float> fr(256); for (int i = 0; i < 256; i++) fr[i] = (float)std::sin(2 * 3.14159265358979 * i / 256); wt->frames.push_back(fr);
    syn.prepare(48000); syn.setWavetable("analogMorph", wt); syn.seedRandom(5);
    const std::string js = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"env\":{\"a\":0.001,\"d\":0.1,\"s\":1,\"r\":0.05},\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.5,\"position\":0," + pe + "}]}},\"bpm\":120}";
    syn.setPreset(parsePresetText(js.c_str()));
    std::vector<float> l(128), r(128), all; auto pump = [&](int n) { for (int i = 0; i < n; i++) { syn.process(l.data(), r.data(), 128); all.insert(all.end(), l.begin(), l.end()); } };
    syn.noteOn(1, note, 0.9); pump(blocks); return all;
}
// Round 12: per-key LUA overrides (slot controls + script parameters) reach the key's resolved LUA layer; other keys are untouched.
static void luaXfxTest() {
    using namespace vv;
    const std::string js = "{\"layers\":{\"LUA\":{\"type\":\"lua\",\"oscs\":[{\"enabled\":true,\"level\":0.8,\"lua\":{\"src\":\"x\",\"params\":{\"detune\":10}}},{},{},{}]}},"
        "\"extra\":{\"xfx\":{\"enabled\":true,\"active\":0,\"banks\":[{\"name\":\"B\",\"keys\":{\"60\":{\"en\":{\"LUA\":true},\"o\":{\"LUA.oscs.0.lua.params.detune\":33,\"LUA.oscs.0.level\":0.25}}}}]}}}";
    auto P = parsePresetText(js.c_str());
    CHECK(P && P->xfx, "xfx table built with a LUA override");
    if (!(P && P->xfx)) return;
    auto it = P->xfx->cfg.find(60 * kNumLayers + 5);
    CHECK(it != P->xfx->cfg.end(), "key 60 has a resolved LUA layer");
    if (it == P->xfx->cfg.end()) return;
    const auto& o = it->second.oscs[0]; double dv = -1; for (auto& kv : o.luaParams) if (kv.first == "detune") dv = kv.second;
    CHECK(std::fabs(dv - 33) < 1e-9, "script parameter override applied for key 60");
    CHECK(std::fabs(o.level - 0.25) < 1e-9, "slot level override applied for key 60");
    CHECK(P->xfx->cfg.find(61 * kNumLayers + 5) == P->xfx->cfg.end(), "other keys use the shared layer");
}
int main() {
    luaXfxTest();
    const double sr = 48000;
    const std::string drop = "\"pitchEnv\":{\"on\":true,\"amount\":12,\"time\":0.3,\"bipolar\":true,\"retrig\":true,\"pts\":[{\"x\":0,\"y\":1,\"lin\":true},{\"x\":1,\"y\":0.5}]}";
    auto on = play(drop, 600);   // 600 blocks * 128 = 1.6 s
    auto off = play("\"pitchEnv\":{\"on\":false,\"amount\":12,\"time\":0.3,\"pts\":[{\"x\":0,\"y\":1},{\"x\":1,\"y\":0.5}]}", 600);
    const size_t s0 = 300, mid = 0 + 24000;
    const double fOffEarly = zcFreq(off, s0, s0 + 2400, sr), fOnEarly = zcFreq(on, s0, s0 + 2400, sr), fOnLate = zcFreq(on, 0 + 60000, 0 + 72000, sr), fOffLate = zcFreq(off, 0 + 60000, 0 + 72000, sr);
    printf("peak %.3f\n", [&]{ double p=0; for(float v: off) p=std::max(p,(double)std::fabs(v)); return p; }());
    printf("early off %.1f on %.1f | late off %.1f on %.1f\n", fOffEarly, fOnEarly, fOffLate, fOnLate);
    CHECK(fOnEarly > fOffEarly * 1.7, "start of a +12 st envelope is about an octave up");
    CHECK(std::fabs(fOnLate / fOffLate - 1) < 0.02, "after the curve ends the pitch settles to the resting pitch (bipolar 0.5 = 0 st)");
    (void)mid;
    // unipolar: y=0 at the end -> 0 st, no shift; start y=1 -> +12
    auto uni = play("\"pitchEnv\":{\"on\":true,\"amount\":12,\"time\":0.3,\"bipolar\":false,\"pts\":[{\"x\":0,\"y\":1,\"lin\":true},{\"x\":1,\"y\":0}]}", 600);
    CHECK(zcFreq(uni, s0, s0 + 2400, sr) > fOffEarly * 1.7, "unipolar start is +amount");
    CHECK(std::fabs(zcFreq(uni, 0 + 60000, 0 + 72000, sr) / fOffLate - 1) < 0.02, "unipolar end settles at 0 st");
    // negative amount flips direction
    auto neg = play("\"pitchEnv\":{\"on\":true,\"amount\":-12,\"time\":0.3,\"bipolar\":false,\"pts\":[{\"x\":0,\"y\":1,\"lin\":true},{\"x\":1,\"y\":0}]}", 600);
    CHECK(zcFreq(neg, s0, s0 + 2400, sr) < fOffEarly * 0.6, "negative amount starts an octave down");
    // click check: largest sample-to-sample jump while the envelope sweeps must stay small relative to the signal
    double mx = 0, pk = 0; for (size_t i = 3000; i < 20000; i++) { mx = std::max(mx, (double)std::fabs(on[i] - on[i - 1])); pk = std::max(pk, (double)std::fabs(on[i])); }
    CHECK(mx < pk * 0.5, "no discontinuities while sweeping");
    bool fin = true; for (float v : on) if (!std::isfinite(v)) fin = false; CHECK(fin, "finite output");
    printf(fails ? "FAILED\n" : "all passed\n"); return fails ? 1 : 0;
}
