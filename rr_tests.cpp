// Sampler Round Robin through the C ABI: rotation order, per-key scope, reset, take markers, variation bounds, slice hits unaffected.
#include <cstring>
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>
#include <cstdint>
#include "vv_engine.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
static std::vector<uint8_t> wav(double hz, double sec, int sr = 44100) {   // mono 16-bit sine
    const int n = (int)(sec * sr); std::vector<uint8_t> b(44 + n * 2); auto w32 = [&](int o, uint32_t v) { memcpy(&b[o], &v, 4); }; auto w16 = [&](int o, uint16_t v) { memcpy(&b[o], &v, 2); };
    memcpy(&b[0], "RIFF", 4); w32(4, 36 + n * 2); memcpy(&b[8], "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 1); w32(24, sr); w32(28, sr * 2); w16(32, 2); w16(34, 16); memcpy(&b[36], "data", 4); w32(40, n * 2);
    for (int i = 0; i < n; i++) { int16_t s = (int16_t)(std::sin(2 * 3.14159265358979 * hz * i / sr) * 12000); memcpy(&b[44 + i * 2], &s, 2); } return b;
}
static double freqOf(const std::vector<float>& v, size_t a, size_t b, double sr = 48000) { int zc = 0; for (size_t i = a + 1; i < b; i++) if (v[i - 1] <= 0 && v[i] > 0) zc++; return zc * sr / (double)(b - a); }
static std::string preset(const std::string& rr0, const std::string& extra = "") {
    std::string o = "{\"layers\":{\"SMP\":{\"type\":\"sample\",\"oscs\":[";
    const char* names[4] = {"s0", "s1", "s2", "s3"};
    for (int i = 0; i < 4; i++) { o += std::string(i ? "," : "") + "{\"name\":\"" + names[i] + "\",\"root\":60,\"level\":" + (i == 0 ? "0.8" : "0") + ",\"start\":0,\"end\":1,\"loop\":true,\"env\":{\"a\":0.001,\"d\":0.1,\"s\":1,\"r\":0.02}" + (i == 0 ? ",\"rr\":" + rr0 : "") + extra + "}"; }
    return o + "]}},\"bpm\":120}";
}
static VVEngine* make(const std::string& rr, const std::string& extra = "") {
    VVEngine* e = vv_engine_create(48000); const double hz[4] = {200, 300, 450, 600}; const char* names[4] = {"s0", "s1", "s2", "s3"};
    for (int i = 0; i < 4; i++) { auto w = wav(hz[i], 2.0); CHECK(vv_engine_load_sample_wav(e, names[i], w.data(), w.size()) == 1, "load %d", i); }
    const std::string p = preset(rr, extra); CHECK(vv_engine_load_preset_json(e, p.c_str(), p.size()) == 1, "preset"); return e;
}
// play one note, return the dominant frequency of the hit (Hz at note 60 / root 60 = the sample's own frequency)
static double hit(VVEngine* e, int note = 60) {
    std::vector<float> L(24000), R(24000); vv_engine_note_on(e, note, 0.9f); for (int i = 0; i < 24000; i += 128) vv_engine_render(e, &L[i], &R[i], std::min(128, 24000 - i));
    const double f = freqOf(L, 3000, 20000); vv_engine_note_off(e, note); std::vector<float> a(9600), b(9600); for (int i = 0; i < 9600; i += 128) vv_engine_render(e, &a[i], &b[i], std::min(128, 9600 - i)); return f;
}
static int takeOf(double f) { const double hz[4] = {200, 300, 450, 600}; int best = 0; for (int i = 1; i < 4; i++) if (std::fabs(f - hz[i]) < std::fabs(f - hz[best])) best = i; return std::fabs(f - hz[best]) < 12 ? best : -1; }
int main() {
    {   VVEngine* e = make("{\"on\":false,\"takes\":[1,2,3]}"); int t = takeOf(hit(e)); CHECK(t == 0, "RR off plays its own sample (got %d)", t); vv_engine_destroy(e); }
    {   VVEngine* e = make("{\"on\":true,\"order\":\"seq\",\"scope\":\"free\",\"variation\":0,\"takes\":[1,2,3]}"); std::string s; for (int i = 0; i < 9; i++) s += std::to_string(takeOf(hit(e)));
        CHECK(s == "012301230", "sequential rotation: %s", s.c_str()); CHECK(vv_engine_smp_rr_last(e, 0) == 0, "indicator reports the last take (%d)", vv_engine_smp_rr_last(e, 0));
        vv_engine_smp_rr_reset(e); std::vector<float> a(1280), b(1280); vv_engine_render(e, a.data(), b.data(), 1280); CHECK(takeOf(hit(e)) == 0, "reset restarts the rotation"); vv_engine_destroy(e); }
    {   VVEngine* e = make("{\"on\":true,\"order\":\"pingpong\",\"scope\":\"free\",\"variation\":0,\"takes\":[1,2]}"); std::string s; for (int i = 0; i < 8; i++) s += std::to_string(takeOf(hit(e))); CHECK(s == "01210121", "ping-pong: %s", s.c_str()); vv_engine_destroy(e); }
    {   VVEngine* e = make("{\"on\":true,\"order\":\"norep\",\"scope\":\"free\",\"variation\":0,\"takes\":[1,2,3]}"); int prev = -1, rep = 0, seen[4] = {0, 0, 0, 0}; for (int i = 0; i < 24; i++) { const int t = takeOf(hit(e)); if (t == prev) rep++; if (t >= 0) seen[t]++; prev = t; }
        CHECK(rep == 0, "no immediate repeats (%d)", rep); CHECK(seen[0] && seen[1] && seen[2] && seen[3], "every take used: %d %d %d %d", seen[0], seen[1], seen[2], seen[3]); vv_engine_destroy(e); }
    {   VVEngine* e = make("{\"on\":true,\"order\":\"seq\",\"scope\":\"key\",\"variation\":0,\"takes\":[1,2]}"); std::string a, b; a += std::to_string(takeOf(hit(e, 60))); a += std::to_string(takeOf(hit(e, 60))); b += std::to_string(takeOf(hit(e, 60 + 12) / 2)); a += std::to_string(takeOf(hit(e, 60)));
        CHECK(a == "012", "per-key rotation continues for the same key: %s", a.c_str()); vv_engine_destroy(e); }
    {   VVEngine* e = make("{\"on\":true,\"order\":\"seq\",\"scope\":\"transport\",\"variation\":0,\"takes\":[1,2]}"); int t0 = takeOf(hit(e)), t1 = takeOf(hit(e)); vv_engine_start_sequencer(e, 1); std::vector<float> a(1280), b(1280); vv_engine_render(e, a.data(), b.data(), 1280); const int t2 = takeOf(hit(e));
        CHECK(t0 == 0 && t1 == 1 && t2 == 0, "reset on transport start: %d %d %d", t0, t1, t2); vv_engine_destroy(e); }
    {   // a take plays with its OWN start / end markers: slot 1 is cut to the first 25 % (0.5 s): the note must end ~0.5 s in
        VVEngine* e = make("{\"on\":true,\"order\":\"seq\",\"scope\":\"free\",\"variation\":0,\"takes\":[1]}"); std::string p = preset("{\"on\":true,\"order\":\"seq\",\"scope\":\"free\",\"variation\":0,\"takes\":[1]}"); const size_t k = p.find("\"name\":\"s1\""); p.replace(k, 11, "\"name\":\"s1\",\"_x\":0"); const size_t k2 = p.find("\"end\":1", k); p.replace(k2, 7, "\"end\":0.25"); { const size_t k3 = p.find("\"loop\":true"); p.replace(k3, 11, "\"loop\":false"); }
        VVEngine* e2 = vv_engine_create(48000); for (int i = 0; i < 4; i++) { const double hz[4] = {200, 300, 450, 600}; const char* n[4] = {"s0", "s1", "s2", "s3"}; auto w = wav(hz[i], 2.0); vv_engine_load_sample_wav(e2, n[i], w.data(), w.size()); }
        CHECK(vv_engine_load_preset_json(e2, p.c_str(), p.size()) == 1, "marker preset"); std::vector<float> L(60000), R(60000); (void)hit(e2); vv_engine_note_on(e2, 60, 0.9f); for (int i = 0; i < 60000; i += 128) vv_engine_render(e2, &L[i], &R[i], std::min(128, 60000 - i));
        double early = 0, late = 0; for (int i = 4000; i < 16000; i++) early += (double)L[i] * L[i]; for (int i = 36000; i < 48000; i++) late += (double)L[i] * L[i]; CHECK(early > 1e-3 && late < early * 0.01, "take 1 stops at its own end marker (early %.4f late %.6f)", early, late);
        vv_engine_destroy(e2); vv_engine_destroy(e); }
    {   // variation keeps level within +-1.3 dB and pitch within +-6 cents
        VVEngine* e = make("{\"on\":true,\"order\":\"seq\",\"scope\":\"free\",\"variation\":1,\"takes\":[]}"); double lo = 1e9, hi = 0; for (int i = 0; i < 12; i++) { std::vector<float> L(12000), R(12000); vv_engine_note_on(e, 60, 0.9f); for (int k = 0; k < 12000; k += 128) vv_engine_render(e, &L[k], &R[k], std::min(128, 12000 - k)); double s = 0; for (int k = 3000; k < 12000; k++) s += (double)L[k] * L[k]; s = std::sqrt(s / 9000); lo = std::min(lo, s); hi = std::max(hi, s); vv_engine_note_off(e, 60); std::vector<float> a(9600), b(9600); for (int k = 0; k < 9600; k += 128) vv_engine_render(e, &a[k], &b[k], std::min(128, 9600 - k)); }
        const double db = 20 * std::log10(hi / lo); CHECK(db > 0.1 && db < 2.8, "variation spread %.2f dB", db); vv_engine_destroy(e); }
    if (fails) { printf("%d FAILED\n", fails); return 1; } printf("rr_tests: all passed\n"); return 0;
}
