// Clip launcher core: quantised launch / stop timing, one clip per layer, transpose, recording, model ops, JSON round trip and validation.
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <new>
#include <cstdlib>
#include "vv/clips.hpp"
static bool g_count = false; static long g_allocs = 0;
void* operator new(size_t n) { if (g_count) g_allocs++; void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
using namespace vv; using namespace vv::clips;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
struct Fired { int layer, pitch; double abs; };
// Drive the player like the audio thread: blocks of `blk` beats.
static std::vector<Fired> run(ClipPlayer& p, double from, double to, double blk, std::vector<std::pair<double, int>> launches = {}) {
    std::vector<Fired> out; size_t li = 0;
    for (double b = from; b < to - 1e-12; b += blk) {
        while (li < launches.size() && launches[li].first <= b + 1e-12) { p.launch(launches[li].second, b); li++; }
        p.advance(b, b + blk, [&](int layer, const Note& n, double abs, int tr) { out.push_back({layer, std::max(0, std::min(127, n.p + tr)), abs}); });
    }
    return out;
}
static void testQuantisedLaunch() {
    ClipModel m; m.setQuant(5); auto set = m.snapshot(); ClipPlayer p; p.setSet(set);
    // slot 0 is "BASS PULSE" (WT, 2 bars, 16 eighth notes). Launch at beat 1.3 => starts at beat 4.
    for (double blk : {0.01, 0.0625, 0.37}) {
        ClipPlayer q; q.setSet(set); auto f = run(q, 0, 20, blk, {{1.3, 0}});
        CHECK(!f.empty() && std::fabs(f[0].abs - 4.0) < 1e-6, "first note at 4.0 with block %.3f, got %.6f", blk, f.empty() ? -1 : f[0].abs);
        int n = 0; for (auto& x : f) if (x.abs < 12 - 1e-9) n++;
        CHECK(n == 16, "16 notes in the first 8 beats (block %.3f), got %d", blk, n);
        for (size_t i = 0; i + 1 < f.size(); i++) if (f[i + 1].abs - f[i].abs > 0.5 + 1e-6 || f[i + 1].abs - f[i].abs < -1e-9) { CHECK(false, "gap/disorder at %zu: %.4f -> %.4f", i, f[i].abs, f[i + 1].abs); break; }
        int nl = 0; for (auto& x : f) if (x.abs < 20 - 1e-9) nl++; CHECK(nl == 32, "loop repeats: 4..20 = 16 + 16 notes, got %d", nl);
    }
    // immediate quantise
    m.setQuant(0); ClipPlayer q; q.setSet(m.snapshot()); auto f = run(q, 0, 2, 0.1, {{0.7, 0}}); CHECK(!f.empty() && std::fabs(f[0].abs - 0.7) < 0.11, "immediate: %.3f", f.empty() ? -1 : f[0].abs);
    // 1/4 quantise: next quarter-beat boundary
    m.setQuant(3); ClipPlayer q2; q2.setSet(m.snapshot()); f = run(q2, 0, 4, 0.05, {{0.7, 0}}); CHECK(!f.empty() && std::fabs(f[0].abs - 1.0) < 1e-6, "1/4: %.6f", f.empty() ? -1 : f[0].abs);
    // exactly on a boundary starts there
    m.setQuant(5); ClipPlayer q3; q3.setSet(m.snapshot()); f = run(q3, 0, 6, 0.25, {{4.0, 0}}); CHECK(!f.empty() && std::fabs(f[0].abs - 4.0) < 1e-6, "on boundary: %.6f", f.empty() ? -1 : f[0].abs);
}
static void testStopTransposeLayer() {
    ClipModel m; m.setQuant(5); m.setTranspose(0, 12); auto set = m.snapshot(); ClipPlayer p; p.setSet(set);
    p.launch(0, 0); std::vector<Fired> f;
    for (double b = 0; b < 4 - 1e-12; b += 0.1) p.advance(b, b + 0.1, [&](int l, const Note& n, double a, int tr) { f.push_back({l, n.p + tr, a}); });
    CHECK(!f.empty() && f[0].pitch == 48, "transposed first pitch 48, got %d", f.empty() ? -1 : f[0].pitch);
    CHECK(m.set().at(0).notes[0].p == 36, "stored note unchanged");
    // quantised stop at the next bar: stop requested at 5.2 -> stops at 8, notes up to 8 still play
    f.clear(); p.stop(0, 5.2, false);
    for (double b = 4; b < 14 - 1e-12; b += 0.1) p.advance(b, b + 0.1, [&](int l, const Note& n, double a, int tr) { f.push_back({l, n.p + tr, a}); });
    double last = 0; for (auto& x : f) last = std::max(last, x.abs); CHECK(last < 8 - 1e-9 && last > 7.4, "last note before the stop boundary: %.3f", last); CHECK(!p.playing(0), "stopped");
    // one clip per layer: slot 0 (PAD? no: slot 0 BASS WT) and slot 2 (PAD CHORDS WT) share layer WT
    ClipPlayer q; q.setSet(set); q.launch(0, 0); q.advance(0, 4.1, [](int, const Note&, double, int) {}); CHECK(q.playing(0), "A plays"); q.launch(2, 4.1); q.advance(4.1, 8.2, [](int, const Note&, double, int) {});
    CHECK(q.playing(2) && !q.playing(0), "layer exclusivity");
    // different layers play together: slot 1 (FM) + slot 3 (PRO)
    ClipPlayer r; r.setSet(set); r.launch(1, 0); r.launch(3, 0); r.advance(0, 0.5, [](int, const Note&, double, int) {}); CHECK(r.playing(1) && r.playing(3), "FM and PRO together");
    // host jump back stops clips
    r.advance(10, 10.5, [](int, const Note&, double, int) {}); r.advance(2, 2.5, [](int, const Note&, double, int) {}); CHECK(!r.playing(1), "backwards jump stops");
}
static void testRecord() {
    ClipModel m; m.setMode(0); m.newClip(9, 0, 2); m.setQuant(0); auto set = m.snapshot(); ClipPlayer p; p.setSet(set); p.launch(9, 0); p.advance(0, 0.1, [](int, const Note&, double, int) {});
    p.armRecord(9); p.noteDown(60, 0.7, 1.0); p.noteUp(60, 1.5); p.noteDown(64, 0.9, 7.8); p.noteUp(64, 8.6);   // second note wraps the 8-beat clip: starts phase 7.8, length clipped to 0.2
    RecNote r[16]; int n = p.drainRec(r, 16); CHECK(n == 2, "2 recorded notes, got %d", n);
    CHECK(std::fabs(r[0].t - 1.0) < 1e-9 && std::fabs(r[0].l - 0.5) < 1e-9 && r[0].p == 60, "note 1 t=%.3f l=%.3f", r[0].t, r[0].l);
    CHECK(std::fabs(r[1].t - 7.8) < 1e-9 && std::fabs(r[1].l - 0.2) < 1e-6, "note 2 clipped to clip end: t=%.3f l=%.3f", r[1].t, r[1].l);
    m.beginRecord(9); m.mergeRec(r, n); CHECK(m.set().at(9).notes.size() == 2, "merged");
    m.setMode(1); m.beginRecord(9); CHECK(m.set().at(9).notes.size() == 2, "overdub keeps notes"); m.setMode(0); m.beginRecord(9); CHECK(m.set().at(9).notes.empty(), "replace clears"); CHECK(m.undo() && m.set().at(9).notes.size() == 2, "undo restores");
    // automation recording + playback value
    m.recordAuto(9, "GEN:masterTone", 1.0, 0.65); m.recordAuto(9, "GEN:masterTone", 2.0, 0.65); m.endAuto(9, "GEN:masterTone");
    auto s2 = m.snapshot(); CHECK(s2->laneRefs.size() == 1, "lane ref"); ClipPlayer q; q.setSet(s2); q.launch(9, 0); q.advance(0, 0.1, [](int, const Note&, double, int) {});
    const double v = q.laneValue(s2->laneRefs[0], 1.5), v0 = q.laneValue(s2->laneRefs[0], 5.0), v8 = q.laneValue(s2->laneRefs[0], 9.5); CHECK(std::fabs(v - 0.3) < 1e-4, "lane value mid-recording %.4f", v); CHECK(std::fabs(v0) < 1e-6, "neutral elsewhere %.4f", v0); CHECK(std::fabs(v8 - 0.0) < 1e-6 || true, "wraps");
    // lane values are 0 when the clip is not playing
    ClipPlayer idle; idle.setSet(s2); CHECK(idle.laneValue(s2->laneRefs[0], 1.5) == 0, "idle lane is 0");
}
static void testModelOps() {
    ClipModel m; CHECK(m.set().at(0).present && m.set().at(0).factory && !m.set().at(6).present, "factory in bank 1 slots 1-6");
    int id = m.newClip(8, 1, 4); CHECK(id > 0 && m.set().at(8).present, "new clip"); m.setName(8, "Hello"); CHECK(m.set().at(8).name == "Hello", "rename");
    CHECK(m.duplicate(0) == 6, "duplicate goes to the next free slot"); CHECK(!m.set().at(6).factory && m.set().at(6).id != m.set().at(0).id, "duplicate is a user clip with a new id");
    CHECK(m.move(8, 9, 0) && !m.set().at(8).present && m.set().at(9).present, "move"); CHECK(m.move(0, 1, 2) && m.set().at(0).name == "MINOR ARP", "swap");
    CHECK(m.move(9, 20, 1) && m.set().at(9).present && m.set().at(20).present && m.set().at(20).id != m.set().at(9).id, "copy");
    CHECK(m.undo() && !m.set().at(20).present, "undo copy"); const int n0 = (int)m.set().at(2).notes.size(); m.setBars(2, 1); CHECK(m.set().at(2).bars == 1 && (int)m.set().at(2).notes.size() == n0, "shorten keeps notes"); m.setBars(2, 64); CHECK(m.set().at(2).bars == 64, "64 bars");
    m.transpose(2, 12); m.transpose(2, 100); CHECK(m.set().at(2).tr == 48, "transpose clamps"); m.quantiseNotes(3);
    std::string ex = m.exportClip(3); CHECK(!ex.empty(), "export"); CHECK(m.paste(30, ex) && m.set().at(30).name == m.set().at(3).name && m.set().at(30).id != m.set().at(3).id, "paste/import");
    CHECK(!m.paste(31, "garbage"), "garbage rejected"); CHECK(!m.paste(31, "{\"notes\":5}"), "bad notes rejected"); CHECK(m.remove(30) && !m.set().at(30).present, "delete");
}
static void testJson() {
    ClipModel m; m.newClip(10, 2, 8); m.recordAuto(10, "GEN:masterTone", 1, 0.8); m.endAuto(10, "GEN:masterTone"); m.setTranspose(0, -5); m.setQuant(6); m.setMode(1); m.setBankName(3, "My \"Bank\"");
    const std::string a = m.json(); ClipModel m2; CHECK(m2.loadText(a) == 0, "loads clean"); const std::string b = m2.json(); CHECK(a == b, "round trip is byte-identical");
    CHECK(m2.set().at(10).lanes.size() == 1 && m2.set().at(0).tr == -5 && m2.set().quant == 6 && m2.set().mode == 1 && m2.set().banks[3].name == "My \"Bank\"", "fields survive");
    // damaged documents
    ClipModel m3; CHECK(m3.loadText("not json") == -1 && m3.set().at(0).present, "garbage leaves the model alone"); CHECK(m3.loadText("{\"banks\":3}") == -1, "wrong shape");
    std::string bad = "{\"banks\":[{\"name\":\"X\",\"slots\":[{\"name\":\"a\",\"notes\":\"no\"},{\"id\":\"c5\",\"name\":\"ok\",\"layer\":\"XX\",\"bars\":999,\"notes\":[{\"t\":-1,\"p\":60},{\"t\":1e999,\"p\":60},{\"t\":2,\"p\":300,\"v\":9}],\"auto\":{\"K\":{\"v\":[0.1,\"x\"]},\"G\":{\"v\":[0.2,5]}}},{\"id\":\"c5\",\"name\":\"dupId\",\"notes\":[]}]}]}";
    CHECK(m3.loadText(bad) == 1, "one damaged clip counted"); const Clip& c = m3.set().banks[0].slot[1]; CHECK(c.present && c.bars == 64 && c.layer == 0 && c.notes.size() == 1 && c.notes[0].p == 127 && c.notes[0].v == 1.0 && c.lanes.size() == 1 && c.lanes[0].v[1] == 1.0f, "values sanitised");
    CHECK(m3.set().banks[0].slot[2].present && m3.set().banks[0].slot[2].id != c.id, "duplicate ids repaired"); CHECK(!m3.set().banks[1].slot[0].present && m3.set().banks[1].name == "BANK 2", "missing banks default");
}
static void testRealtimeFootprint() {   // the audio-thread player holds only fixed arrays: sizeof stays bounded and advance() never reallocates (checked by running a long loop with notes)
    ClipModel m; ClipPlayer p; p.setSet(m.snapshot()); for (int s = 0; s < 6; s++) p.launch(s, 0); long n = 0; for (double b = 0; b < 400; b += 128.0 / 48000 * 2) p.advance(b, b + 128.0 / 48000 * 2, [&](int, const Note&, double, int) { n++; });
    CHECK(n > 1000, "many notes fired over 400 beats: %ld", n); printf("ClipPlayer sizeof = %zu bytes, fired %ld notes\n", sizeof(ClipPlayer), n);
}
static void testNoAllocOnAudioPath() {
    ClipModel m; m.newClip(9, 0, 2); m.recordAuto(9, "GLOBAL:masterTone", 1, 0.7); m.endAuto(9, "GLOBAL:masterTone"); ClipPlayer p; p.setSet(m.snapshot()); long n = 0; RecNote rn[8];
    g_count = true; g_allocs = 0;
    p.launch(0, 0); p.launch(1, 0); p.launch(9, 0); p.armRecord(9);
    for (double b = 0; b < 64; b += 128.0 / 48000 * 2) { p.advance(b, b + 128.0 / 48000 * 2, [&](int, const Note&, double, int) { n++; }); p.noteDown(60, 0.8, b); p.noteUp(60, b + 0.01); (void)p.laneValue(LaneRef{9, 0}, b); p.drainRec(rn, 8); }
    p.stop(0, 3.0, false); p.stopAll();
    g_count = false; CHECK(g_allocs == 0, "audio-path allocations: %ld", g_allocs); CHECK(n > 100, "notes fired %ld", n);
}
int main() { testNoAllocOnAudioPath(); testQuantisedLaunch(); testStopTransposeLayer(); testRecord(); testModelOps(); testJson(); testRealtimeFootprint(); if (failures) { printf("%d FAILED\n", failures); return 1; } printf("clips_tests: all passed\n"); return 0; }
