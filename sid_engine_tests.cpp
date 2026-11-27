// The SID layer end to end through the C ABI.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "vv_engine.h"
static double rms(const std::vector<float>& v, int a, int b) { double s = 0; for (int i = a; i < b; i++) s += v[i] * v[i]; return std::sqrt(s / (b - a)); }
int main() {
    int fails = 0; auto check = [&](const char* n, bool ok) { std::printf("%-52s %s\n", n, ok ? "OK" : "FAIL"); if (!ok) fails++; };
    auto run = [&](const char* json, int note, double* sus, double* tail) {
        VVEngine* e = vv_engine_create(48000); std::vector<float> a(48000), b(48000);
        if (!vv_engine_load_preset_json(e, json, strlen(json))) { vv_engine_destroy(e); return false; }
        vv_engine_note_on(e, note, 0.8f); for (int i = 0; i < 48000; i += 480) vv_engine_render(e, &a[i], &b[i], 480);
        *sus = rms(a, 24000, 48000);
        vv_engine_note_off(e, note); std::vector<float> c(48000 * 6), d(48000 * 6); for (int i = 0; i < 48000 * 6; i += 480) vv_engine_render(e, &c[i], &d[i], 480);
        *tail = rms(c, 48000 * 5, 48000 * 6); vv_engine_destroy(e); return true; };
    double s, t;
    check("SID layer plays and releases", run("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"level\":0},{\"level\":0}]},\"SID\":{\"type\":\"sid\",\"oscs\":[{\"enabled\":true,\"wave\":\"sawtooth\",\"level\":0.8},{\"enabled\":true,\"wave\":\"pulse\",\"pulseWidth\":0.3,\"level\":0.5,\"pan\":0.3},{\"enabled\":false}]}},\"bpm\":120}", 57, &s, &t) && s > 0.01 && t < 1e-4);
    check("all SID voices disabled -> silent", run("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"level\":0},{\"level\":0}]},\"SID\":{\"type\":\"sid\",\"oscs\":[{\"enabled\":false},{\"enabled\":false},{\"enabled\":false}]}},\"bpm\":120}", 57, &s, &t) && s < 1e-6);
    std::printf(fails ? "SID ENGINE TESTS FAILED\n" : "SID engine tests passed\n"); return fails ? 1 : 0;
}
