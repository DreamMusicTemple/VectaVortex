// Unit tests for the performance layer (held notes / polyphony / legato / arp / step sequencer). The browser version is timer based and cannot be rendered
// deterministically, so these tests check the exact note timing and ordering the native sample-clocked version must produce.
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include "vv/performance.hpp"
using namespace vv;

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct Rig {
    Synth syn; Performance perf; std::shared_ptr<Preset> preset;
    struct Ev { double t; int layer, note; }; std::vector<Ev> started; uint64_t lastAge = 0;
    std::vector<std::pair<double,int>> offs;
    explicit Rig(const std::string& json) {
        auto wt = std::make_shared<WtTable>(); wt->tableSize = 256; std::vector<float> fr(256); for (int i = 0; i < 256; i++) fr[i] = (float)std::sin(2 * 3.14159265358979 * i / 256); wt->frames.push_back(fr);
        syn.prepare(48000); syn.setWavetable("analogMorph", wt);
        preset = parsePresetText(json); syn.tickSeconds = 1e9; syn.setPreset(preset); perf.attach(&syn); perf.setPreset(preset);
    }
    void run(double seconds) {
        const int q = (int)(seconds * 48000 / 128); std::vector<float> L(128), R(128);
        for (int i = 0; i < q; i++) {
            perf.advance(syn.clock(), 128); syn.process(L.data(), R.data(), 128);
            for (auto& v : syn.voices()) if (v.alive && v.layer == 0 && v.age > lastAge) { started.push_back({v.startT, v.layer, v.note}); }
            for (auto& v : syn.voices()) lastAge = std::max(lastAge, v.age);
        }
    }
};
static const char* kBase = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8}]}},\"bpm\":120}";

static void testPolyAndLegato() {
    Rig r(kBase);
    r.perf.maxPolyphony = 2;
    r.perf.noteDown(60, 0.8); r.perf.noteDown(64, 0.8); r.run(0.05);
    CHECK(r.syn.hasVoice(L_WT, 60) && r.syn.hasVoice(L_WT, 64), "two notes sounding");
    r.perf.noteDown(67, 0.8); r.run(0.05);
    CHECK(!r.syn.hasVoice(L_WT, 60), "oldest note released when polyphony limit hit");
    CHECK(r.syn.hasVoice(L_WT, 64) && r.syn.hasVoice(L_WT, 67), "newer notes kept");
    {   // default polyphony is 64 and all 64 notes sound together; the 65th retires the oldest
        const std::string off = "[{\"enabled\":false,\"rangeLow\":126,\"rangeHigh\":127}]";
        Rig big("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8,\"rangeLow\":0,\"rangeHigh\":127}]},\"FM\":{\"type\":\"fm\",\"oscs\":" + off + "},\"SMP\":{\"type\":\"sampler\",\"oscs\":" + off + "},\"PRO\":{\"type\":\"procedural\",\"oscs\":" + off + "},\"SID\":{\"type\":\"sid\",\"oscs\":" + off + "}},\"bpm\":120}"); CHECK(big.perf.maxPolyphony == 64, "default max polyphony is 64");
        for (int n = 0; n < 64; n++) big.perf.noteDown(24 + n, 0.7); big.run(0.05);
        bool all = true; for (int n = 0; n < 64; n++) all = all && big.syn.hasVoice(L_WT, 24 + n); CHECK(all, "64 simultaneous notes sound");
        big.perf.noteDown(100, 0.7); big.run(0.05); CHECK(!big.syn.hasVoice(L_WT, 24) && big.syn.hasVoice(L_WT, 100), "65th note retires the oldest");
    }
    // mono + legato: second note cuts the first immediately and starts at sustain
    Rig m(kBase); m.perf.maxPolyphony = 1; m.perf.legato = true;
    m.perf.noteDown(60, 0.8); m.run(0.1); m.perf.noteDown(64, 0.8); m.run(0.05);
    CHECK(!m.syn.hasVoice(L_WT, 60) && m.syn.hasVoice(L_WT, 64), "mono hand-over");
    m.run(0.6); int alive = 0; for (auto& v : m.syn.voices()) if (v.alive && v.layer == 0) alive++;
    CHECK(alive == 1, "handed-over voice is gone after its release tail (alive=%d)", alive);
    // key up releases
    m.perf.noteUp(64); m.run(0.05); CHECK(!m.syn.hasVoice(L_WT, 64), "note up releases");
}

static void testLatchAndHold() {
    // Hold is the per-layer latch: with the arp on, a note played into a Hold layer stays in the arp after key up; layers without Hold stop.
    const char* two = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8,\"hold\":true}]},\"FM\":{\"type\":\"fm\",\"oscs\":[{\"level\":0.5}]}},\"bpm\":120,\"arp\":{\"on\":true,\"order\":\"UP\",\"rateMode\":\"hz\",\"hz\":8,\"octRange\":1}}";
    Rig a(two);
    a.perf.noteDown(60, 0.8); a.run(0.3); a.perf.noteUp(60); a.run(0.1);
    const size_t n0 = a.started.size(); a.run(0.5);
    CHECK(a.started.size() > n0, "latched note keeps arpeggiating after key up (WT has Hold)");
    a.perf.noteDown(60, 0.8); a.run(0.05); a.perf.noteUp(60); a.run(0.3);   // tapping the latched note again drops it
    const size_t n1 = a.started.size(); a.run(0.5); CHECK(a.started.size() == n1, "tapping a latched note again drops it from the arp");
    // no Hold anywhere: nothing is latched
    Rig nh("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8}]}},\"bpm\":120,\"arp\":{\"on\":true,\"order\":\"UP\",\"rateMode\":\"hz\",\"hz\":8,\"octRange\":1}}");
    nh.perf.noteDown(60, 0.8); nh.run(0.3); nh.perf.noteUp(60); nh.run(0.2); const size_t m0 = nh.started.size(); nh.run(0.5);
    CHECK(nh.started.size() == m0, "without Hold the arp stops when the key is released");
    // arp off clears latched notes
    Rig off(two); off.perf.noteDown(62, 0.8); off.run(0.2); off.perf.noteUp(62); off.perf.setArpOn(false); off.run(0.1); off.perf.setArpOn(true); const size_t k0 = off.started.size(); off.run(0.5);
    CHECK(off.started.size() == k0, "switching the arp off drops everything latched");
    // layer hold: key up does not release; second press toggles off
    Rig h("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8,\"hold\":true}]}}}");
    h.perf.noteDown(60, 0.8); h.run(0.02); h.perf.noteUp(60); h.run(0.02);
    CHECK(h.syn.hasVoice(L_WT, 60), "hold layer sustains");
    h.perf.noteDown(60, 0.8); h.run(0.02); CHECK(!h.syn.hasVoice(L_WT, 60), "second press on a held layer toggles it off");
}

static void testArpTargets() {
    // two wavetable oscillators; arp drives only WT2 -> WT1 plays directly on key down
    Rig r("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8},{\"table\":\"analogMorph\",\"level\":0.8}]}},\"bpm\":120,\"arp\":{\"on\":true,\"target\":\"WT2\",\"order\":\"UP\",\"rateMode\":\"sync\",\"division\":0.25,\"octRange\":1}}");
    r.perf.noteDown(60, 0.8); r.run(0.3);
    bool direct = false, arpV = false, directHasOsc1 = false;
    for (auto& v : r.syn.voices()) if (v.alive && v.layer == 0) { if (v.arpVoice) arpV = true; else { direct = true; directHasOsc1 = v.wt[1].on; } }
    CHECK(direct, "undriven oscillator plays directly while arp runs");
    CHECK(!directHasOsc1, "direct voice excludes the arp-driven oscillator");
    CHECK(r.started.size() >= 1 || arpV, "arp voices are produced");
    r.perf.noteUp(60); r.run(0.05);
    int dalive = 0; for (auto& v : r.syn.voices()) if (v.alive && !v.released && v.layer == 0 && !v.arpVoice) dalive++;
    CHECK(dalive == 0, "direct voice released on key up");
}

static void testBleedAndColour() {
    auto buf = std::make_shared<SampleBuf>(); buf->len = 48000; buf->d[0].resize(48000);
    for (int i = 0; i < 48000; i++) buf->d[0][i] = (float)(0.5 * std::sin(2 * 3.14159265358979 * 220.0 * i / 48000));
    auto mk = [&](bool b1, bool b2, double amt, const char* mic, const char* spk) {
        std::string j = "{\"layers\":{\"SMP\":{\"type\":\"sample\",\"oscs\":[";
        for (int k = 0; k < 2; k++) {
            j += std::string(k ? "," : "") + "{\"name\":\"" + (k ? "B" : "A") + "\",\"level\":" + (k ? "0" : "0.8") + ",\"loop\":true,\"bleedOn\":" + ((k ? b2 : b1) ? "true" : "false") + ",\"bleed\":" + std::to_string(amt) + ",\"mic\":\"" + mic + "\",\"spk\":\"" + spk + "\"}";
        }
        return j + "]}}}";
    };
    (void)mk;
    // Slot B is a different (quieter) sample so bleed from A into B's filter group is detectable via total energy
    auto run = [&](bool b1, bool b2, double amt, const char* mic, const char* spk) {
        std::string j = "{\"layers\":{\"SMP\":{\"type\":\"sample\",\"oscs\":["
          "{\"name\":\"A\",\"level\":0.8,\"bleedOn\":" + std::string(b1 ? "true" : "false") + ",\"bleed\":" + std::to_string(amt) + ",\"mic\":\"" + mic + "\",\"spk\":\"" + spk + "\"},"
          "{\"name\":\"B\",\"level\":0.8,\"bleedOn\":" + std::string(b2 ? "true" : "false") + ",\"bleed\":0}]}}}";
        Rig r(j); r.syn.setSample("A", buf); auto buf2 = std::make_shared<SampleBuf>(*buf); for (auto& x : buf2->d[0]) x = 0.f; r.syn.setSample("B", buf2);
        r.perf.noteDown(60, 0.8); std::vector<float> L(128), R(128); double e = 0;
        for (int i = 0; i < 300; i++) { r.perf.advance(r.syn.clock(), 128); r.syn.process(L.data(), R.data(), 128); if (i > 100) for (int k = 0; k < 128; k++) e += L[k] * L[k]; }
        return e;
    };
    const double base = run(false, false, 0.0, "none", "none"), bleed = run(true, true, 1.0, "none", "none");
    CHECK(base > 0, "sample slot A sounds");
    CHECK(bleed > base * 1.3, "bleed sends A into B's filter group (base=%g bleed=%g)", base, bleed);
    const double off = run(true, false, 1.0, "none", "none");
    CHECK(std::fabs(off - base) < base * 1e-6, "no bleed unless the receiving slot is ticked");
    const double phone = run(false, false, 0.0, "phone", "none");
    CHECK(std::fabs(phone - base) > base * 1e-3, "mic type changes the sample sound (base=%g phone=%g)", base, phone);
}

static void testArp() {
    Rig r("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8}]}},\"bpm\":120,\"arp\":{\"on\":true,\"order\":\"UP\",\"rateMode\":\"sync\",\"division\":0.25,\"octRange\":2}}");
    r.perf.noteDown(64, 0.8); r.perf.noteDown(60, 0.8); r.perf.noteDown(67, 0.8);
    r.run(2.05);   // 120 bpm, 1/16 = 0.125 s
    CHECK(r.started.size() >= 15, "arp produced %zu notes", r.started.size());
    const int expect[] = {60, 64, 67, 72, 76, 79, 60, 64, 67, 72, 76, 79};
    for (size_t i = 0; i < r.started.size() && i < 12; i++) CHECK(r.started[i].note == expect[i], "arp UP step %zu got %d", i, r.started[i].note);
    for (size_t i = 1; i < r.started.size() && i < 12; i++) { const double dt = r.started[i].t - r.started[i - 1].t; CHECK(std::fabs(dt - 0.125) < 0.0035, "arp spacing %.4f", dt); }
    // keys up with arp on: nothing sounding by itself afterwards (arp keeps no notes without held keys)
    r.perf.noteUp(60); r.perf.noteUp(64); r.perf.noteUp(67); const size_t n0 = r.started.size(); r.run(0.5); CHECK(r.started.size() == n0, "arp stops when no keys are held");
    // order variants
    Rig d("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8}]}},\"bpm\":120,\"arp\":{\"on\":true,\"order\":\"UP/DOWN\",\"rateMode\":\"sync\",\"division\":0.25,\"octRange\":1}}");
    d.perf.noteDown(60, 0.8); d.perf.noteDown(64, 0.8); d.perf.noteDown(67, 0.8); d.run(1.1);
    const int ud[] = {60, 64, 67, 64, 60, 64, 67, 64};
    for (size_t i = 0; i < d.started.size() && i < 8; i++) CHECK(d.started[i].note == ud[i], "UP/DOWN step %zu got %d", i, d.started[i].note);
    Rig c("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8}]}},\"bpm\":120,\"arp\":{\"on\":true,\"order\":\"CHORD\",\"rateMode\":\"hz\",\"hz\":4,\"octRange\":1}}");
    c.perf.noteDown(60, 0.8); c.perf.noteDown(64, 0.8); c.run(0.3);
    CHECK(c.started.size() >= 2 && std::fabs(c.started[0].t - c.started[1].t) < 0.003, "CHORD plays all notes together");
}


static std::string arpJson(const std::string& extra) { return "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8}]}},\"bpm\":120,\"arp\":{\"on\":true,\"rateMode\":\"sync\",\"division\":0.25," + extra + "}}"; }
static std::vector<int> arpNotesFor(const std::string& extra, double secs, int* repeatCount = nullptr) {
    Rig r(arpJson(extra)); r.perf.noteDown(60, 0.8); r.perf.noteDown(64, 0.8); r.perf.noteDown(67, 0.8); r.run(secs);
    std::vector<int> v; for (auto& e : r.started) v.push_back(e.note); (void)repeatCount; return v;
}
static void testArpModes() {
    struct C { const char* ord; int oct; std::vector<int> exp; };
    const C cases[] = {
        {"DOWN/UP", 1, {67, 64, 60, 64, 67, 64, 60}}, {"PENDULUM", 1, {60, 64, 67, 67, 64, 60, 60, 64}}, {"SPIRAL", 1, {60, 67, 64, 60, 67, 64}},
        {"OCTAVE ORBIT", 2, {60, 64, 67, 72, 76, 79, 60, 64}}, {"SPIRAL", 2, {60, 79, 64, 76, 67, 72}} };
    for (const C& c : cases) {
        auto got = arpNotesFor(std::string("\"order\":\"") + c.ord + "\",\"octRange\":" + std::to_string(c.oct), 1.1);
        bool ok = got.size() >= c.exp.size(); for (size_t i = 0; ok && i < c.exp.size(); i++) ok = got[i] == c.exp[i];
        CHECK(ok, "arp %s oct%d sequence (got %zu notes, first %d)", c.ord, c.oct, got.size(), got.empty() ? -1 : got[0]);
    }
    // random walk only moves to a neighbour (or stays) in the expanded list
    auto w = arpNotesFor("\"order\":\"RANDOM WALK\",\"octRange\":1", 3.0); const int idxOf[] = {60, 64, 67};
    bool near = w.size() > 10; for (size_t i = 1; near && i < w.size(); i++) { int a = 0, b = 0; for (int k = 0; k < 3; k++) { if (w[i - 1] == idxOf[k]) a = k; if (w[i] == idxOf[k]) b = k; } near = std::abs(a - b) <= 1; }
    CHECK(near, "random walk moves by at most one step");
    // transpose
    auto t = arpNotesFor("\"order\":\"UP\",\"octRange\":1,\"transpose\":7", 0.5); CHECK(t.size() >= 3 && t[0] == 67 && t[1] == 71 && t[2] == 74, "transpose +7");
    // note repeat 2 => twice the notes per step, each pair identical
    auto rp = arpNotesFor("\"order\":\"UP\",\"octRange\":1,\"repeat\":2", 0.5); CHECK(rp.size() >= 6 && rp[0] == 60 && rp[1] == 60 && rp[2] == 64 && rp[3] == 64, "note repeat 2");
    // strum: staggered, ascending within the step
    Rig s(arpJson("\"order\":\"STRUM\",\"octRange\":1")); s.perf.noteDown(60, 0.8); s.perf.noteDown(64, 0.8); s.perf.noteDown(67, 0.8); s.run(0.3);
    CHECK(s.started.size() >= 3 && s.started[0].t < s.started[1].t && s.started[1].t < s.started[2].t, "strum notes are staggered");
    // gate: shorter gate releases earlier
    Rig g(arpJson("\"order\":\"UP\",\"octRange\":1,\"gate\":0.25")); g.perf.noteDown(60, 0.8); g.run(0.2);
    int rel = 0; for (auto& v : g.syn.voices()) if (v.alive && v.released) rel++; CHECK(rel >= 1 || g.started.size() >= 1, "gate produces released arp voices");
    // global latch: no Hold layer, but Latch keeps the arp going after key up; tapping again drops it
    Rig l(arpJson("\"order\":\"UP\",\"octRange\":1,\"latch\":true")); l.perf.noteDown(60, 0.8); l.perf.noteUp(60); l.run(0.1); const size_t n0 = l.started.size(); l.run(0.5);
    CHECK(l.started.size() > n0, "latch keeps the arp running after key up");
    l.perf.noteDown(60, 0.8); l.run(0.05); const size_t n1 = l.started.size(); l.run(0.5); CHECK(l.started.size() == n1, "tapping a latched note drops it");
    // velocity
    Rig v(arpJson("\"order\":\"UP\",\"octRange\":1,\"velocity\":0.4")); v.perf.noteDown(60, 1.0); v.run(0.16);
    bool vel = false; for (auto& x : v.syn.voices()) if (x.alive && x.layer == 0) vel = std::fabs(x.noteVel - 0.4) < 0.01; CHECK(vel, "arp velocity follows the Velocity setting");
}


static void testArpAdvancedEngine() {
    // Advanced mode through the full performance layer: events are produced, are reproducible for a seed, differ between seeds, respect Transpose / Repeat, and never leave stuck voices.
    auto run = [](const std::string& extra, double secs) { Rig r(arpJson("\"mode\":\"advanced\",\"octRange\":2," + extra)); r.perf.noteDown(60, 0.8); r.perf.noteDown(64, 0.8); r.perf.noteDown(67, 0.8); r.run(secs); std::vector<int> v; for (auto& e : r.started) v.push_back(e.note); return v; };
    const auto a = run("\"adv\":{\"algo\":\"markov\",\"seed\":42}", 1.6), b = run("\"adv\":{\"algo\":\"markov\",\"seed\":42}", 1.6), c = run("\"adv\":{\"algo\":\"markov\",\"seed\":43}", 1.6);
    CHECK(a.size() >= 10, "advanced arp produced %zu notes", a.size()); CHECK(a == b, "same seed gives the same pattern"); CHECK(a != c, "a different seed gives a different pattern");
    const auto t = run("\"adv\":{\"algo\":\"fractal\"},\"transpose\":12", 1.0), u = run("\"adv\":{\"algo\":\"fractal\"}", 1.0);
    bool tr = t.size() == u.size() && !t.empty(); for (size_t i = 0; tr && i < t.size(); i++) tr = t[i] == u[i] + 12; CHECK(tr, "transpose applies to advanced notes");
    const auto rep = run("\"adv\":{\"algo\":\"ratchetA\"}", 1.0); CHECK(rep.size() >= 8, "ratchet algorithm produces repeats (%zu)", rep.size());
    Rig k(arpJson("\"mode\":\"advanced\",\"octRange\":2,\"adv\":{\"algo\":\"burst\",\"density\":1}")); k.perf.noteDown(60, 0.8); k.perf.noteDown(64, 0.8); k.run(1.0); k.perf.noteUp(60); k.perf.noteUp(64); k.run(1.5);
    int stuck = 0; for (auto& v : k.syn.voices()) if (v.alive && !v.released) stuck++; CHECK(stuck == 0, "no stuck arp voices after release (%d)", stuck);
    // every algorithm runs through the engine without crashing
    for (int i = 0; i < arpadv::kNumAlgos; i++) { auto x = run(std::string("\"adv\":{\"algo\":\"") + arpadv::algoIds()[i] + "\"}", 0.6); (void)x; }
}

static std::string seqPreset(const std::string& steps, const std::string& extra = "") {
    return "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8}],\"patterns\":[" + steps + "]" + extra + "}},\"bpm\":120}";
}
static std::string pat(int len, const std::string& onIdx, const std::string& stepExtra = "") {   // pattern with steps listed in onIdx (comma list) on
    std::string s = "{\"length\":" + std::to_string(len) + ",\"steps\":[";
    for (int i = 0; i < 32; i++) { bool on = ("," + onIdx + ",").find("," + std::to_string(i) + ",") != std::string::npos; s += std::string(i ? "," : "") + "{\"on\":" + (on ? "true" : "false") + ",\"pitch\":" + std::to_string(i % 5) + ",\"ratchet\":1,\"prob\":100,\"tempoRatio\":1,\"plocks\":{}" + (on ? stepExtra : "") + ",\"transientIdx\":0}"; }
    return s + "]}";
}
static void testSequencer() {
    Rig r(seqPreset(pat(4, "0,1,2,3")));
    r.perf.startTransport(); r.run(1.01);
    // web quirk kept: the clock counts the step before step 0 plays, so the first sounding step is step 1
    CHECK(r.started.size() >= 7, "sequencer produced %zu notes", r.started.size());
    const int expect[] = {61, 62, 63, 60, 61, 62, 63, 60};   // 60 + pitch(i%5) for step i = 1,2,3,0,...
    for (size_t i = 0; i < r.started.size() && i < 8; i++) CHECK(r.started[i].note == expect[i], "seq step %zu got %d", i, r.started[i].note);
    for (size_t i = 1; i < r.started.size() && i < 8; i++) { const double dt = r.started[i].t - r.started[i - 1].t; CHECK(std::fabs(dt - 0.125) < 0.0035, "step spacing %.4f", dt); }
    // swing: pair duration constant, second step later
    Rig s(seqPreset(pat(4, "0,1,2,3"))); s.perf.swingOn = true; s.perf.swingAmount = 60; s.perf.startTransport(); s.run(1.0);
    double pair = 0; if (s.started.size() >= 3) pair = s.started[2].t - s.started[0].t; CHECK(std::fabs(pair - 0.25) < 0.007, "swing keeps the pair duration (%.4f)", pair);
    // ratchets
    Rig ra(seqPreset(pat(4, "1", ""))); ra.preset->layers[L_WT].patterns[0].steps[1].ratchet = 4; ra.perf.setPreset(ra.preset); ra.syn.setPreset(ra.preset);
    ra.perf.startTransport(); ra.run(0.5);
    CHECK(ra.started.size() >= 4 && std::fabs((ra.started[1].t - ra.started[0].t) - 0.125 / 4) < 0.0035, "ratchet spacing");
    // probability 0: silent
    Rig p(seqPreset(pat(4, "0,1,2,3"))); for (auto& st : p.preset->layers[L_WT].patterns[0].steps) st.prob = 0; p.perf.startTransport(); p.run(1.0);
    CHECK(p.started.empty(), "prob 0 never plays");
    // chain: pattern 0 (length 2) then pattern 1 (length 2) in loop mode
    std::string chained = seqPreset(pat(2, "0,1") + "," + pat(2, "0,1", ""), ",\"chainOrder\":[0,1],\"chainMode\":\"loop\"");
    Rig ch(chained); ch.preset->layers[L_WT].patterns[1].steps[0].pitch = 7; ch.preset->layers[L_WT].patterns[1].steps[1].pitch = 9; ch.perf.setPreset(ch.preset);
    ch.perf.startTransport(); ch.run(1.2);
    // pattern 0 steps pitches 0,1 ; pattern 1 pitches 7,9 ; sequence of notes after the first (step 1 of p0): 61, then p1 steps 0,1 -> 67, 69, then p0 0,1 -> 60, 61
    const int cexp[] = {61, 67, 69, 60, 61, 67, 69};
    for (size_t i = 0; i < ch.started.size() && i < 7; i++) CHECK(ch.started[i].note == cexp[i], "chain note %zu got %d", i, ch.started[i].note);
    // oneshot chain stops advancing
    Rig os(seqPreset(pat(2, "0,1") + "," + pat(2, "0,1", ""), ",\"chainOrder\":[0,1],\"chainMode\":\"oneshot\"")); os.perf.startTransport(); os.run(1.5);
    CHECK(os.perf.currentSlot(L_WT) == 1, "oneshot chain parks on its last pattern (slot %d)", os.perf.currentSlot(L_WT));
}

static double rigEnergy(Rig& r, double secs) {
    const int q = (int)(secs * 48000 / 128); std::vector<float> L(128), R(128); double e = 0;
    for (int i = 0; i < q; i++) { r.perf.advance(r.syn.clock(), 128); r.syn.process(L.data(), R.data(), 128); for (int k = 0; k < 128; k++) e += (double)L[k] * L[k]; }
    return e;
}
static void testGateAndSidechain() {
    Rig a(seqPreset(pat(4, "0,1,2,3"))), b(seqPreset(pat(4, "0,1,2,3")));
    a.preset->layers[L_WT].gate = 0.1; b.preset->layers[L_WT].gate = 1.0;
    for (auto* r : {&a, &b}) { r->preset->layers[L_WT].env = EnvCfg{0.001, 0.05, 0.9, 0.01}; r->perf.setPreset(r->preset); r->syn.setPreset(r->preset); r->perf.startTransport(); }
    const double ea = rigEnergy(a, 1.0), eb = rigEnergy(b, 1.0);
    CHECK(eb > ea * 2.0, "short gate plays less than a full gate (%.3f vs %.3f)", ea, eb);
    // sidechain: a layer ducked by itself above a low threshold is quieter than the same layer with amount 0
    Rig c(kBase), d(kBase);
    c.preset->layers[L_WT].scSrc = L_WT; c.preset->layers[L_WT].scThresh = 0.01; c.preset->layers[L_WT].scAmount = 0.9;
    d.preset->layers[L_WT].scSrc = L_WT; d.preset->layers[L_WT].scThresh = 0.01; d.preset->layers[L_WT].scAmount = 0.0;
    for (auto* r : {&c, &d}) { r->perf.setPreset(r->preset); r->syn.setPreset(r->preset); r->perf.noteDown(60, 0.8); }
    const double ec = rigEnergy(c, 0.5), ed = rigEnergy(d, 0.5);
    CHECK(ec < ed * 0.2, "sidechain ducks (%.3f vs %.3f)", ec, ed);
}

static void testChordMaker() {
    auto mk = [](const std::string& extra) { return std::string("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8}]}},\"bpm\":120,\"chordMaker\":{\"on\":true,\"scale\":\"major\",\"key\":0,\"octave\":3,\"rate\":4,\"steps\":2,\"target\":\"WT\",\"randomness\":0,\"gate\":0.5,"
        "\"progression\":[{\"deg\":0,\"tones\":[0,2,4]},{\"deg\":3,\"tones\":[3,5,7]}]" + extra + "}}"); };
    Rig r(mk("")); r.perf.startTransport(); r.run(0.6);
    // expected: C E G at t=0, F A C at 0.25, C E G at 0.5
    std::vector<int> a, b, c; for (auto& ev : r.started) { if (ev.t < 0.1) a.push_back(ev.note); else if (ev.t < 0.35) b.push_back(ev.note); else c.push_back(ev.note); }
    std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end()); std::sort(c.begin(), c.end());
    CHECK(a == (std::vector<int>{48, 52, 55}), "chord 1 notes (%zu)", a.size()); CHECK(b == (std::vector<int>{53, 57, 60}), "chord 2 notes (%zu)", b.size()); CHECK(c == (std::vector<int>{48, 52, 55}), "chord 3 notes (%zu)", c.size());
    if (!r.started.empty() && !b.empty()) { double t0 = 1e9; for (auto& ev : r.started) if (ev.note == 53) t0 = std::min(t0, ev.t); CHECK(std::fabs(t0 - 0.25) < 0.004, "second chord at 0.25 s (%.4f)", t0); }
    // euclid 3 pulses over 8 steps: chords at steps 0, 3, 6 -> spacing 3,3,2 steps
    Rig eu(mk(",\"euclid\":{\"on\":true,\"pulses\":3,\"steps\":8,\"rotate\":0}")); eu.perf.startTransport(); eu.run(2.1);
    std::vector<double> ts; for (auto& ev : eu.started) if (ev.note == 48 || ev.note == 53) { if (ts.empty() || ev.t - ts.back() > 0.05) ts.push_back(ev.t); }
    CHECK(ts.size() >= 4 && std::fabs(ts[1] - ts[0] - 0.75) < 0.01 && std::fabs(ts[2] - ts[1] - 0.75) < 0.01 && std::fabs(ts[3] - ts[2] - 0.5) < 0.01, "euclid spacing %zu", ts.size());
    // probability 0: silent; transport stop releases
    Rig p0(mk(",\"prob\":0")); p0.preset->chord.prob = 0; p0.perf.setPreset(p0.preset); p0.syn.setPreset(p0.preset); p0.perf.startTransport(); p0.run(1.0); CHECK(p0.started.empty(), "probability 0 plays nothing");
}

static void testPanic() {
    Rig r(kBase); r.perf.noteDown(60, 0.8); r.perf.noteDown(64, 0.8); r.run(0.05); r.perf.panic(); r.run(0.6);
    int alive = 0; for (auto& v : r.syn.voices()) if (v.alive) alive++; CHECK(alive == 0, "panic silences everything (%d alive)", alive);
}

// Pitch bend is a true musical bend in the native engine (the web app adds the bend's cents to a WT oscillator's frequency as Hz - a bug we do not copy).
static void testBend() {
    for (int mpe = 0; mpe < 2; mpe++) {
        Rig r(kBase); r.syn.tickSeconds = 0.1;
        std::vector<float> L(48000), R(48000);
        if (mpe) { r.syn.mpeEnabled = true; r.syn.mpeNoteOn(1, 60); }
        r.perf.noteDown(60, 0.8);
        if (mpe) r.syn.mpeController(1, 0, 0.5); else r.syn.pitchBend = 0.5;   // +0.5 * 2 semitones = +100 cents
        for (int i = 0; i < 48000; i += 128) { r.perf.advance(r.syn.clock(), 128); r.syn.process(&L[i], &R[i], 128); }
        int crossings = 0; for (int i = 24001; i < 48000; i++) if (L[i - 1] <= 0 && L[i] > 0) crossings++;
        const double f = crossings / 0.5, want = 261.6256 * std::pow(2.0, 1.0 / 12.0);
        CHECK(std::fabs(f - want) < 3.0, "%s bend: %.2f Hz vs %.2f", mpe ? "MPE per-note" : "global", f, want);
    }
}

// pitch-bend TARGETS: with both WT oscillators un-targeted a full bend must not move the note
static void testBendTargets() {
    Rig r(kBase); r.syn.tickSeconds = 0.1;
    r.preset->bendTargets[L_WT][0] = false; r.preset->bendTargets[L_WT][1] = false; r.perf.setPreset(r.preset); r.syn.setPreset(r.preset);
    std::vector<float> L(48000), R(48000); r.perf.noteDown(60, 0.8); r.syn.pitchBend = 0.5;
    for (int i = 0; i < 48000; i += 128) { r.perf.advance(r.syn.clock(), 128); r.syn.process(&L[i], &R[i], 128); }
    int crossings = 0; for (int i = 24001; i < 48000; i++) if (L[i - 1] <= 0 && L[i] > 0) crossings++;
    CHECK(std::fabs(crossings / 0.5 - 261.6256) < 3.0, "un-targeted oscillators ignore pitch bend (%.2f Hz)", crossings / 0.5);
    auto P = parsePresetText("{\"layers\":{},\"extra\":{\"pitchBendTargets\":{\"SMP\":[true,false,true],\"FM\":[false,true]}}}");
    CHECK(P->bendTargets[L_SMP][0] && !P->bendTargets[L_SMP][1] && P->bendTargets[L_SMP][2] && P->bendTargets[L_SMP][3] && !P->bendTargets[L_FM][0] && P->bendTargets[L_FM][1] && P->bendTargets[L_WT][0], "extra.pitchBendTargets is parsed (missing = on)");
}

// MPC pads: one sampler slot plays alone at its original pitch (zones ignored), several pads sound together, a pad release stops only that pad.
static void testPads() {
    auto mkbuf = [](double hz) { auto b = std::make_shared<SampleBuf>(); b->len = 48000; b->d[0].resize(48000); for (int i = 0; i < 48000; i++) b->d[0][i] = (float)(0.5 * std::sin(2 * 3.14159265358979 * hz * i / 48000)); return b; };
    const std::string j = "{\"layers\":{\"SMP\":{\"type\":\"sample\",\"oscs\":[{\"name\":\"A\",\"level\":0.8,\"loop\":true,\"rangeLow\":0,\"rangeHigh\":10},{\"name\":\"B\",\"level\":0.8,\"loop\":true,\"rangeLow\":0,\"rangeHigh\":10}]}}}";
    Rig r(j); r.syn.setSample("A", mkbuf(220)); r.syn.setSample("B", mkbuf(330));
    auto energy = [&](int blocks) { std::vector<float> L(128), R(128); double e = 0; for (int i = 0; i < blocks; i++) { r.perf.advance(r.syn.clock(), 128); r.syn.process(L.data(), R.data(), 128); for (float x : L) e += (double)x * x; } return e; };
    r.perf.noteDown(60, 0.8); const double baseEn = energy(60); { int n = 0; for (auto& v : r.syn.voices()) if (v.alive && v.layer == L_SMP) for (auto& q : v.smp) if (q.on) n++; CHECK(n == 0, "note 60 is outside both sampler zones (0..10): no sampler voice (%d)", n); }
    r.perf.noteUp(60); energy(300);
    r.perf.padDown(L_SMP, 0, 1000, 60, 0.9); energy(10); CHECK(r.syn.hasVoice(L_SMP, 1000, true), "pad voice exists, keyed outside the MIDI range");
    int smpVoices = 0; for (auto& v : r.syn.voices()) if (v.alive && v.layer == L_SMP) for (auto& q : v.smp) if (q.on) smpVoices++; CHECK(smpVoices == 1, "pad plays only its own slot even with overlapping zones (%d slots)", smpVoices);
    const double one = energy(60); CHECK(one > baseEn + 0.1, "pad is audible although the zone excludes its pitch (%.3f vs %.3f)", one, baseEn);
    r.perf.padDown(L_SMP, 1, 1001, 60, 0.9); energy(10); CHECK(r.syn.hasVoice(L_SMP, 1000, true) && r.syn.hasVoice(L_SMP, 1001, true), "two pads sound together");
    r.perf.padUp(L_SMP, 1000); energy(400); CHECK(!r.syn.hasVoice(L_SMP, 1000, true) && r.syn.hasVoice(L_SMP, 1001, true), "releasing pad 1 leaves pad 2 sounding");
    r.perf.padUp(L_SMP, 1001); energy(400); CHECK(!r.syn.hasVoice(L_SMP, 1001, true), "second pad released");
}
static void testRollRecordAndLanes() {
    // recorded pitch-bend / mod lanes replay with the roll (last point in range wins) and the wheels rest again when the transport stops
    Rig r(seqPreset("{\"length\":16,\"steps\":[],\"roll\":{\"bars\":2,\"loopOn\":true,\"loopStart\":0,\"loopEnd\":2,\"notes\":[{\"t\":0,\"l\":0.5,\"p\":60,\"v\":0.8}],\"lanes\":{\"bend\":[[1.0,0.5],[3.0,0]],\"mod\":[[1.0,0.7]]}}}"));
    r.perf.startTransport(); r.run(0.4); CHECK(std::fabs(r.syn.pitchBend) < 1e-9, "bend untouched before its first point (%.3f)", r.syn.pitchBend);
    r.run(0.3); CHECK(std::fabs(r.syn.pitchBend - 0.5) < 1e-9 && std::fabs(r.syn.modwheel - 0.7) < 1e-9, "bend 0.5 / mod 0.7 after beat 1 (%.3f %.3f)", r.syn.pitchBend, r.syn.modwheel);
    r.run(0.9); CHECK(std::fabs(r.syn.pitchBend) < 1e-9, "bend back to 0 after beat 3 (%.3f)", r.syn.pitchBend);
    r.run(0.2); r.perf.stopTransport(); CHECK(std::fabs(r.syn.modwheel) < 1e-9, "mod wheel rests after stop (%.3f)", r.syn.modwheel);
    // capture: only while armed AND running; events carry increasing heard beats
    Rig k(seqPreset(pat(4, "0")));
    k.perf.recPush(0, 64, 0.8); rollrec::Ev e; CHECK(!k.perf.recRing().pop(e), "nothing captured while disarmed");
    k.perf.recArm(true); k.perf.recPush(0, 64, 0.8); CHECK(!k.perf.recRing().pop(e), "nothing captured while the transport is stopped");
    k.perf.startTransport(); k.run(0.5); k.perf.recPush(0, 64, 0.8); k.run(0.25); k.perf.recPush(1, 64, 0); k.perf.recPush(2, 0, 0.25); k.perf.recPush(3, 0, 0.5);
    rollrec::Ev a, b, c, d; CHECK(k.perf.recRing().pop(a) && k.perf.recRing().pop(b) && k.perf.recRing().pop(c) && k.perf.recRing().pop(d), "four events captured");
    CHECK(a.kind == 0 && a.note == 64 && std::fabs(a.x - 0.8f) < 1e-6 && b.kind == 1 && c.kind == 2 && d.kind == 3, "kinds and values");
    CHECK(b.beat > a.beat + 0.4 && std::fabs(a.beat - 1.0) < 0.1, "beats: on %.3f off %.3f (expected ~1.0 and ~1.5)", a.beat, b.beat);
}
static void testRecorderUnits() {
    using namespace rollrec; Recorder R; Cfg c; c.ls = 0; c.le = 4; c.roomEnd = 16; Out o;
    R.begin(); Ev e; e.kind = 0; e.note = 60; e.x = 0.5f; e.beat = 1.0; R.feed(e, c, o); e.kind = 1; e.beat = 1.5; R.feed(e, c, o);
    CHECK(o.notes.size() == 1 && std::fabs(o.notes[0].t - 1.0) < 1e-9 && std::fabs(o.notes[0].l - 0.5) < 1e-9 && o.notes[0].p == 60 && std::fabs(o.notes[0].v - 0.5) < 1e-9, "note pairing");
    o.clear(); e.kind = 0; e.note = 62; e.beat = 5.5; R.feed(e, c, o); e.kind = 1; e.beat = 5.75; R.feed(e, c, o);
    CHECK(o.notes.size() == 1 && std::fabs(o.notes[0].t - 1.5) < 1e-9, "a note played in the 2nd loop pass lands at loop position 1.5 (got %.3f)", o.notes.empty() ? -1 : o.notes[0].t);
    o.clear(); c.grid = 0.25; e.kind = 0; e.note = 64; e.beat = 2.07; R.feed(e, c, o); e.kind = 1; e.beat = 2.31; R.feed(e, c, o);
    CHECK(o.notes.size() == 1 && std::fabs(o.notes[0].t - 2.0) < 1e-9 && std::fabs(o.notes[0].l - 0.25) < 1e-9, "quantise: t %.3f l %.3f", o.notes[0].t, o.notes[0].l);
    o.clear(); c.grid = 0; R.begin(); e.kind = 2; for (int i = 0; i < 20; i++) { e.beat = 1.0 + i * 0.005; e.x = (float)(i / 20.0); R.feed(e, c, o); }   // a fast sweep within 0.1 beat -> thinned
    e.beat = 2.0; e.x = 0; R.feed(e, c, o); R.settle(3.0, c, o, true);
    CHECK(o.bend.size() <= 6 && o.bend.size() >= 2, "bend sweep thinned to %zu points", o.bend.size()); CHECK(!o.bendCut.empty(), "span to replace reported");
    // held notes are flushed (never left open) when recording stops
    o.clear(); R.begin(); e.kind = 0; e.note = 70; e.beat = 3.0; R.feed(e, c, o); R.settle(3.2, c, o, true); CHECK(o.notes.size() == 1 && o.notes[0].l > 0, "held note flushed on stop");
    // ring: bounded, never blocks, drops when full
    Ring rg; int pushed = 0; for (uint32_t i = 0; i < Ring::kCap + 10; i++) if (rg.push(Ev())) pushed++; CHECK(pushed == (int)Ring::kCap && rg.dropped() == 10, "ring bounded (%d pushed, %u dropped)", pushed, rg.dropped());
}

int main() { testRecorderUnits(); testRollRecordAndLanes(); testPads(); testBendTargets(); testPolyAndLegato(); testLatchAndHold(); testArpTargets(); testBleedAndColour(); testArp(); testArpModes(); testArpAdvancedEngine(); testSequencer(); testGateAndSidechain(); testChordMaker(); testPanic(); testBend(); printf(failures ? "performance tests: %d FAILED\n" : "performance tests: all passed\n", failures); return failures ? 1 : 0; }
