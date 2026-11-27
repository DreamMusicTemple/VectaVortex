// XFX as the 7th layer: zone (keys programmed in the XFX bank), FX send, solo / mute (bit 6), hold, and the host-parameter-independent state round trip.
#include <cstring>
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>
#include "vv_engine.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
static std::string preset(bool xfxSend, bool hold, bool xfxOn = true) {
    return std::string("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"env\":{\"a\":0.005,\"d\":0.1,\"s\":0.8,\"r\":0.05},\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.3,\"env\":{\"a\":0.005,\"d\":0.1,\"s\":0.8,\"r\":0.05}}]},\"FM\":{\"type\":\"fm\"},\"SMP\":{}},\"bpm\":120,")
        + "\"fxStages\":[{\"type\":\"delay\",\"enabled\":true,\"params\":{\"time\":0.12,\"feedback\":0.4,\"mix\":1},\"sends\":{\"XFX\":" + (xfxSend ? "true" : "false") + "}}],"
        + "\"extra\":{\"xfx\":{\"v\":1,\"enabled\":" + (xfxOn ? "true" : "false") + ",\"active\":0,\"hold\":" + (hold ? "true" : "false") + ",\"banks\":[{\"name\":\"Bank 1\",\"keys\":{\"60\":{\"en\":{\"WT\":true},\"o\":{}}}}]}}}";
}
static VVEngine* make(const std::string& p) {
    VVEngine* e = vv_engine_create(48000);
    std::vector<unsigned char> wt(8 + 256 * 4); uint32_t fc = 1, fs = 256; memcpy(wt.data(), &fc, 4); memcpy(wt.data() + 4, &fs, 4); for (int i = 0; i < 256; i++) { float f = (float)std::sin(2 * 3.14159265 * i / 256); memcpy(wt.data() + 8 + i * 4, &f, 4); }
    vv_engine_load_wavetable(e, "analogMorph", wt.data(), wt.size());
    CHECK(vv_engine_load_preset_json(e, p.c_str(), p.size()) == 1, "preset loads"); return e;
}
static double energy(VVEngine* e, int note, double secs, bool release, double* tailEnergy = nullptr) {
    std::vector<float> l(128), r(128); vv_engine_note_on(e, note, 0.9f); double en = 0, tail = 0; const int blocks = (int)(secs * 48000 / 128), relAt = blocks / 4;
    for (int b = 0; b < blocks; b++) { if (release && b == relAt) vv_engine_note_off(e, note); vv_engine_pump_params(e); vv_engine_render(e, l.data(), r.data(), 128); double x = 0; for (int i = 0; i < 128; i++) x += (double)l[i] * l[i] + (double)r[i] * r[i]; en += x; if (b > blocks * 3 / 4) tail += x; }
    if (!release) vv_engine_note_off(e, note); for (int b = 0; b < 400; b++) vv_engine_render(e, l.data(), r.data(), 128); if (tailEnergy) *tailEnergy = tail; return en;
}
int main() {
    { // FX send: zone key 60 gains the delay wet; key 61 is untouched by it
        VVEngine* a = make(preset(false, false)); VVEngine* b = make(preset(true, false));
        double ta60 = 0, tb60 = 0, ta61 = 0, tb61 = 0; energy(a, 60, 1.0, true, &ta60); energy(b, 60, 1.0, true, &tb60); energy(a, 61, 1.0, true, &ta61); energy(b, 61, 1.0, true, &tb61);
        CHECK(tb60 > ta60 * 1.5 + 1e-6, "XFX send gives the zone key the delay tail: %g vs %g", tb60, ta60); CHECK(std::fabs(tb61 - ta61) <= 1e-4 * std::max(1e-3, ta61), "non-zone key unaffected: %g vs %g", tb61, ta61);
        vv_engine_destroy(a); vv_engine_destroy(b); }
    { // XFX off (bypass) = no tap
        VVEngine* a = make(preset(false, false, false)); VVEngine* b = make(preset(true, false, false)); const double a60 = energy(a, 60, 1.0, true), b60 = energy(b, 60, 1.0, true);
        CHECK(std::fabs(b60 - a60) < 1e-9 * std::max(1.0, a60), "bypassed XFX: send has no effect (%g vs %g)", b60, a60); vv_engine_destroy(a); vv_engine_destroy(b); }
    { // solo / mute on bit 6
        VVEngine* e = make(preset(false, false)); vv_engine_set_solo_mute(e, 64, 0); const double z = energy(e, 60, 0.6, true), n = energy(e, 61, 0.6, true);
        CHECK(z > 0.01 && n < 1e-9, "SOLO XFX: only the zone key sounds (%g / %g)", z, n);
        vv_engine_set_solo_mute(e, 0, 64); const double z2 = energy(e, 60, 0.6, true), n2 = energy(e, 61, 0.6, true); CHECK(z2 < 1e-9 && n2 > 0.01, "MUTE XFX silences the zone key only (%g / %g)", z2, n2);
        vv_engine_set_solo_mute(e, 64 | 1, 0); const double n3 = energy(e, 61, 0.6, true); CHECK(n3 > 0.01, "with a layer solo as well, non-zone keys follow the layer solo (%g)", n3);
        vv_engine_set_solo_mute(e, 0, 0); CHECK(energy(e, 61, 0.6, true) > 0.01, "solo off: everything plays"); vv_engine_destroy(e); }
    { // hold: zone key keeps sounding after key-up, others do not
        VVEngine* e = make(preset(false, true)); VVEngine* e2 = make(preset(false, true)); double tz = 0, tn = 0; energy(e, 60, 2.0, true, &tz); energy(e2, 61, 2.0, true, &tn); vv_engine_destroy(e2);
        CHECK(tz > 1e-3 && tn < tz * 0.01, "HOLD XFX latches the zone key (tail %g vs %g)", tz, tn); vv_engine_destroy(e); }
    { // state: sends and hold survive export / re-load
        VVEngine* e = make(preset(true, true)); std::vector<char> b(1 << 20); size_t n = vv_engine_export_preset(e, b.data(), b.size()); std::string out(b.data(), std::min(n, b.size() - 1));
        CHECK(out.find("\"XFX\":true") != std::string::npos, "XFX send exported"); CHECK(out.find("\"hold\":true") != std::string::npos, "XFX hold exported");
        VVEngine* f = make(out); double tz = 0; energy(f, 60, 2.0, true, &tz); CHECK(tz > 1e-3, "restored hold still latches"); vv_engine_destroy(f); vv_engine_destroy(e); }
    { // MPE touch-keyboard glide: +12 semitones on a held note doubles its frequency (zero-crossing count), without retrigger
        auto cross = [&](double semis) { VVEngine* e = make(preset(false, false)); vv_engine_set_mpe(e, 1); std::vector<float> l(128), r(128); vv_engine_note_on(e, 61, 0.9f);
            for (int b = 0; b < 40; b++) { vv_engine_pump_params(e); vv_engine_render(e, l.data(), r.data(), 128); }
            vv_engine_mpe_glide(e, 61, semis); for (int b = 0; b < 40; b++) { vv_engine_pump_params(e); vv_engine_render(e, l.data(), r.data(), 128); }   // settle
            int c = 0; float prev = 0; for (int b = 0; b < 100; b++) { vv_engine_pump_params(e); vv_engine_render(e, l.data(), r.data(), 128); for (int i = 0; i < 128; i++) { if (prev < 0 && l[i] >= 0) c++; prev = l[i]; } }
            vv_engine_destroy(e); return c; };
        const int c0 = cross(0), c12 = cross(12), cm12 = cross(-12);
        CHECK(c0 > 10 && std::fabs(c12 / (double)c0 - 2.0) < 0.12 && std::fabs(cm12 / (double)c0 - 0.5) < 0.12, "glide +-12 st doubles / halves the pitch (%d, %d, %d)", c0, c12, cm12); }
    printf(fails ? "FAILED %d\n" : "all passed\n", fails); return fails ? 1 : 0;
}
