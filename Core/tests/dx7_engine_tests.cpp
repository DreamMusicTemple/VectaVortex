// The DX7 6-operator mode of the FM layer, end to end through the C ABI.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "vv_engine.h"
static double rms(const std::vector<float>& v, int a, int b) { double s = 0; for (int i = a; i < b; i++) s += v[i] * v[i]; return std::sqrt(s / (b - a)); }
int main() {
    int fails = 0; auto check = [&](const char* n, bool ok) { std::printf("%-52s %s\n", n, ok ? "OK" : "FAIL"); if (!ok) fails++; };
    auto run = [&](const char* json, int note, double* sustainRms, double* tailRms) {
        VVEngine* e = vv_engine_create(48000); std::vector<float> a(48000), b(48000);
        const int ok = vv_engine_load_preset_json(e, json, strlen(json)); if (!ok) { vv_engine_destroy(e); return false; }
        vv_engine_note_on(e, note, 0.8f); for (int i = 0; i < 48000; i += 480) vv_engine_render(e, &a[i], &b[i], 480);
        *sustainRms = rms(a, 24000, 48000);
        vv_engine_note_off(e, note); std::vector<float> c(48000 * 10), d(48000 * 10); for (int i = 0; i < 48000 * 10; i += 480) vv_engine_render(e, &c[i], &d[i], 480);
        *tailRms = rms(c, 48000 * 9, 48000 * 10); vv_engine_destroy(e); return true; };
    double s, t;
    check("6-op layer loads and plays (ROM1A #1)", run("{\"layers\":{\"FM\":{\"type\":\"fm\",\"sixOp\":true,\"sixOpEnabled\":true,\"dx7Bank\":\"ROM1A\",\"dx7PatchIdx\":0,\"oscs\":[{},{},{},{},{},{},{},{}]}},\"bpm\":120}", 60, &s, &t) && s > 0.005);
    check("6-op layer releases to silence", t < 1e-4);
    check("sixOpEnabled=false stays silent", run("{\"layers\":{\"FM\":{\"type\":\"fm\",\"sixOp\":true,\"sixOpEnabled\":false,\"oscs\":[{},{},{},{},{},{},{},{}]}},\"bpm\":120}", 60, &s, &t) && s < 1e-6);
    check("other ROM bank (ROM3A #20) plays", run("{\"layers\":{\"FM\":{\"type\":\"fm\",\"sixOp\":true,\"sixOpEnabled\":true,\"dx7Bank\":\"ROM3A\",\"dx7PatchIdx\":19,\"oscs\":[{},{},{},{},{},{},{},{}]}},\"bpm\":120}", 48, &s, &t) && s > 0.001);
    {   // matrix -> FM6OP:level lowers every operator's output level live (macro1 held at 1.0 with amount -100 = -50 on all six operators)
        auto lvl = [&](const char* conns, double macro) { VVEngine* e = vv_engine_create(48000); std::vector<float> a(48000), b(48000);
            std::string pj = std::string("{\"layers\":{\"FM\":{\"type\":\"fm\",\"sixOp\":true,\"sixOpEnabled\":true,\"dx7Bank\":\"ROM1A\",\"dx7PatchIdx\":0,\"oscs\":[{},{},{},{},{},{},{},{}]}},\"bpm\":120") + conns + "}";
            vv_engine_load_preset_json(e, pj.c_str(), pj.size()); vv_engine_set_macro(e, 0, (float)macro);
            for (int i = 0; i < 24000; i += 480) vv_engine_render(e, &a[i], &b[i], 480);   // let the matrix act on the patch first; new notes use the modulated patch
            vv_engine_note_on(e, 60, 0.8f); for (int i = 0; i < 48000; i += 480) vv_engine_render(e, &a[i], &b[i], 480);
            const double r = rms(a, 24000, 48000); vv_engine_destroy(e); return r; };
        const double plain = lvl("", 1), modded = lvl(",\"connections\":[{\"source\":\"macro1\",\"dest\":\"FM6OP:level\",\"amount\":-100}]", 1), zero = lvl(",\"connections\":[{\"source\":\"macro1\",\"dest\":\"FM6OP:level\",\"amount\":-100}]", 0);
        check("FM6OP:level mod lowers the output of new notes", modded < plain * 0.5);
        check("FM6OP:level with a zero source leaves the patch unchanged", std::fabs(zero - plain) < plain * 0.15);
    }
    std::printf(fails ? "DX7 ENGINE TESTS FAILED\n" : "DX7 engine tests passed\n"); return fails ? 1 : 0;
}
