// Clip launcher end to end through the C ABI: audible quantised playback, stop, recording, automation lane, persistence via exportPreset, damaged state.
#include <cstring>
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>
#include "vv_engine.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
static double rms(const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b && i < v.size(); i++) s += (double)v[i] * v[i]; return b > a ? std::sqrt(s / (double)(b - a)) : 0; }
static const char* kPreset = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"level\":0.8},{\"level\":0}]},\"FM\":{\"type\":\"fm\"}},\"bpm\":120}";
static VVEngine* make() {
    VVEngine* e = vv_engine_create(48000);
    std::vector<unsigned char> wt(8 + 256 * 4); uint32_t fc = 1, fs = 256; memcpy(wt.data(), &fc, 4); memcpy(wt.data() + 4, &fs, 4); for (int i = 0; i < 256; i++) { float f = (float)std::sin(2 * 3.14159265358979 * i / 256); memcpy(wt.data() + 8 + i * 4, &f, 4); }
    vv_engine_load_wavetable(e, "analogMorph", wt.data(), wt.size()); vv_engine_set_value(e, "bpm", "120");
    CHECK(vv_engine_load_preset_json(e, kPreset, strlen(kPreset)) == 1, "preset loads"); return e;
}
static void render(VVEngine* e, std::vector<float>& L, std::vector<float>& R, size_t from, size_t n) { for (size_t i = 0; i < n; i += 128) { const size_t c = std::min<size_t>(128, n - i); vv_engine_render(e, &L[from + i], &R[from + i], (int)c); } }
static std::string stateOf(VVEngine* e) { std::vector<char> b(1 << 20); const size_t n = vv_clip_state_json(e, b.data(), b.size()); return std::string(b.data(), std::min(n, b.size() - 1)); }
static void testPlayback() {
    VVEngine* e = make(); char out[64];
    const std::string st = stateOf(e); CHECK(st.find("BASS PULSE") != std::string::npos && st.find("\"bank\":0") != std::string::npos, "factory clips present");
    CHECK(vv_clip_op(e, "quant", "{\"q\":5}", out, sizeof out) == 1, "quant op");
    const size_t N = 48000 * 8; std::vector<float> L(N), R(N);
    render(e, L, R, 0, 4800); CHECK(rms(L, 0, 4800) == 0, "silent before launch");
    vv_clip_launch(e, 0);   // transport not running: the launch starts it, clip begins at beat 0
    render(e, L, R, 4800, 48000 - 4800);
    CHECK(rms(L, 4800, 24000) > 0.003, "clip audible after launch, rms=%.5f", rms(L, 4800, 24000)); CHECK((vv_clip_playing_mask(e) & 1) != 0, "slot 0 reported playing"); const float ph = vv_clip_phase(e, 0); CHECK(ph > 0.5f && ph < 3.0f, "phase advances (%.3f)", ph);
    vv_clip_stop(e, 0, 0); render(e, L, R, 48000, 4800); CHECK((vv_clip_queued_stop_mask(e) & 1) != 0, "stop is queued, not immediate");
    render(e, L, R, 52800, N - 52800);   // the bar boundary is at beat 8 = 4 s; the clip is 2 bars (8 beats) so it already wrapped at 4 s: the stop lands at the next bar line
    CHECK((vv_clip_playing_mask(e) & 1) == 0, "clip stopped on the boundary"); CHECK(rms(L, 48000 * 6, 48000 * 8) < 1e-4, "silence after the stop, rms=%.6f", rms(L, 48000 * 6, 48000 * 8));
    vv_engine_destroy(e);
}
static void testRecordPersistAutomation() {
    VVEngine* e = make(); char out[8192]; std::vector<float> L(48000 * 12), R(48000 * 12);
    CHECK(vv_clip_op(e, "new", "{\"slot\":9,\"layer\":0,\"bars\":2}", out, sizeof out) == 1, "new clip"); CHECK(vv_clip_op(e, "quant", "{\"q\":0}", out, sizeof out) == 1, "immediate");
    CHECK(vv_clip_op(e, "mode", "{\"m\":0}", out, sizeof out) == 1, "replace");
    CHECK(vv_clip_op(e, "recordBegin", "{\"slot\":9}", out, sizeof out) == 1, "record armed"); vv_clip_launch(e, 9); render(e, L, R, 0, 12000);
    CHECK(vv_clip_recording_slot(e) == 9, "recording slot reported");
    vv_engine_note_on(e, 62, 0.7f); render(e, L, R, 12000, 24000); vv_engine_note_off(e, 62); render(e, L, R, 36000, 4800);
    vv_clip_op(e, "recordEnd", "{}", out, sizeof out); render(e, L, R, 40800, 1280); const int merged = vv_clip_drain_recording(e); CHECK(merged == 1, "one note merged, got %d", merged);
    std::string st = stateOf(e); CHECK(st.find("\"p\":62") != std::string::npos, "recorded note in state: %s", st.substr(st.find("CLIP 7") == std::string::npos ? 0 : st.find("CLIP 7"), 200).c_str());
    // automation: write a lane (phase 1..2 beats at 1.0)
    for (int i = 0; i < 12; i++) { char a[200]; snprintf(a, sizeof a, "{\"slot\":9,\"key\":\"GLOBAL:masterTone\",\"phase\":%.3f,\"value\":1.0}", 1.0 + i * 0.1); vv_clip_op(e, "autoWrite", a, out, sizeof out); }
    vv_clip_op(e, "autoEnd", "{\"slot\":9,\"key\":\"GLOBAL:masterTone\"}", out, sizeof out); st = stateOf(e); CHECK(st.find("GLOBAL:masterTone") != std::string::npos, "lane stored");
    // persistence: exportPreset carries the clips; a fresh engine restores them identically
    std::vector<char> pb(4 << 20); const size_t pn = vv_engine_export_preset(e, pb.data(), pb.size()); CHECK(pn > 0 && pn < pb.size(), "preset exported (%zu)", pn);
    VVEngine* e2 = vv_engine_create(48000); CHECK(vv_engine_load_preset_json(e2, pb.data(), pn) == 1, "reload"); CHECK(stateOf(e2) == st, "clip state survives export / import byte for byte");
    // damaged clip data inside a preset: skipped, engine fine
    std::string bad(pb.data(), pn); const size_t k = bad.find("\"banks\":["); CHECK(k != std::string::npos, "has banks"); const size_t k2 = bad.find("\"slots\":[", k); CHECK(k2 != std::string::npos, "has slots"); bad.replace(k2, 9, "\"slots\":[{\"notes\":7},"); VVEngine* e3 = vv_engine_create(48000);
    CHECK(vv_engine_load_preset_json(e3, bad.data(), bad.size()) == 1, "damaged clip data does not fail the load"); vv_clip_launch(e3, 0); std::vector<float> a(4800), b(4800); for (int i = 0; i < 4800; i += 128) vv_engine_render(e3, &a[i], &b[i], std::min(128, 4800 - i));
    vv_engine_destroy(e3); vv_engine_destroy(e2); vv_engine_destroy(e);
}
static void testAutomationAudible() {
    auto run = [&](bool withLane) { VVEngine* e = make(); char out[256]; vv_clip_op(e, "quant", "{\"q\":0}", out, sizeof out); vv_clip_op(e, "new", "{\"slot\":9,\"layer\":0,\"bars\":1}", out, sizeof out);
        vv_clip_op(e, "mode", "{\"m\":1}", out, sizeof out);
        // a held note clip: note C3 for the whole bar
        std::string clip = "{\"vvClip\":1,\"clip\":{\"name\":\"HOLD\",\"layer\":\"WT\",\"bars\":1,\"notes\":[{\"t\":0,\"l\":3.9,\"p\":48,\"v\":0.9}]}}"; std::string a = "{\"slot\":9,\"json\":\""; for (char ch : clip) { if (ch == '"') a += "\\\""; else a += ch; } a += "\"}";
        CHECK(vv_clip_op(e, "paste", a.c_str(), out, sizeof out) == 1, "paste hold clip");
        if (withLane) { for (int i = 0; i < 40; i++) { char w[200]; snprintf(w, sizeof w, "{\"slot\":9,\"key\":\"GLOBAL:masterTone\",\"phase\":%.3f,\"value\":1.0}", i * 0.1); vv_clip_op(e, "autoWrite", w, out, sizeof out); } vv_clip_op(e, "autoEnd", "{\"slot\":9,\"key\":\"GLOBAL:masterTone\"}", out, sizeof out); }
        std::vector<float> L(48000), R(48000); render(e, L, R, 0, 2400); vv_clip_launch(e, 9); render(e, L, R, 2400, 40000); double zc = 0; for (int i = 9000; i < 38000; i++) zc += std::fabs((double)L[i] - L[i - 1]); const double r = rms(L, 9000, 38000); vv_engine_destroy(e); return std::make_pair(r, zc); };
    auto a = run(false), b = run(true); printf("tone lane: rms %.5f -> %.5f, roughness %.3f -> %.3f\n", a.first, b.first, a.second, b.second);
    CHECK(a.first > 0.001 && b.first > 0.001, "both audible"); CHECK(std::fabs(a.second - b.second) / std::max(1e-9, a.second) > 0.02 || std::fabs(a.first - b.first) / a.first > 0.02, "automation lane changes the sound");
}
int main() { testPlayback(); testRecordPersistAutomation(); testAutomationAudible(); if (fails) { printf("%d FAILED\n", fails); return 1; } printf("clips_engine_tests: all passed\n"); return 0; }
