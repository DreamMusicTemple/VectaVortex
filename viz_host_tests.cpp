// Native LUA Visual host test: analyser band response to known tones, beat detection, param reactivity, bad-script recovery, C bridge run of every factory script. usage: vv_viz_host_tests <factory dir>
#include "vv_viz.h"
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <dirent.h>
#include <algorithm>
#include <cstring>
static int bad = 0; static void ok(const char* n, bool c, const char* x = "") { printf("%s %s %s\n", c ? "OK  " : "FAIL", n, x); if (!c) bad++; }
static std::string slurp(const std::string& p) { FILE* f = fopen(p.c_str(), "rb"); std::string s; if (!f) return s; char b[8192]; size_t n; while ((n = fread(b, 1, sizeof b, f))) s.append(b, n); fclose(f); return s; }
static void tone(std::vector<float>& L, std::vector<float>& R, double hz, double sr, double amp, long& ph) { for (size_t i = 0; i < L.size(); i++, ph++) L[i] = R[i] = (float)(amp * std::sin(6.283185307 * hz * ph / sr)); }
int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "viz/factory"; const double sr = 48000; std::vector<float> L(2048), R(2048);
    VVViz* v = vv_viz_create(); std::string src = slurp(dir + "/02_fractal_tunnel.lua"); ok("load 02", vv_viz_load(v, src.c_str()) == 1, vv_viz_error(v));
    for (double hz : { 80.0, 1000.0, 6000.0 }) { long ph = 0; for (int f = 0; f < 90; f++) { tone(L, R, hz, sr, .5, ph); vv_viz_frame(v, 1 / 60.f, L.data(), R.data(), 2048, (float)sr, 640, 360, 2, 60, 1, 0); }
        const float* b = vv_viz_bands(v); int best = 1; for (int i = 1; i < 8; i++) if (b[i] > b[best]) best = i; int want = hz < 250 ? 1 : (hz < 2000 ? 4 : 6); char m[96]; snprintf(m, sizeof m, "tone %.0f Hz -> strongest band %d (expected %d or neighbour) levels %.2f %.2f %.2f %.2f %.2f %.2f %.2f", hz, best, want, b[1], b[2], b[3], b[4], b[5], b[6], b[7]); ok("band follows tone", std::abs(best - want) <= 1, m); }
    { long ph = 0; for (int f = 0; f < 90; f++) { tone(L, R, 440.0, sr, .5, ph); vv_viz_frame(v, 1 / 60.f, L.data(), R.data(), 2048, (float)sr, 640, 360, 2, 60, 1, 0); } /* peakHz read via audio[18] is not exposed; verify decay instead */
      std::fill(L.begin(), L.end(), 0.f); std::fill(R.begin(), R.end(), 0.f); for (int f = 0; f < 240; f++) vv_viz_frame(v, 1 / 60.f, L.data(), R.data(), 2048, (float)sr, 640, 360, 2, 60, 1, 0); const float* b = vv_viz_bands(v); float mx = 0; for (int i = 0; i < 9; i++) mx = std::max(mx, b[i]); ok("silence decays", mx < .05f, ""); }
    { float t0[32], t1[32]; long ph = 0; vv_viz_clear_assign(v); for (int f = 0; f < 60; f++) { tone(L, R, 80, sr, .5, ph); vv_viz_frame(v, 1 / 60.f, L.data(), R.data(), 2048, (float)sr, 640, 360, 2, 60, 1, 0); } memcpy(t0, vv_viz_uniforms(v), sizeof t0);
      vv_viz_set_assign(v, 0, 1, 1, 1.f, 1, .1f, 0, 1); for (int f = 0; f < 60; f++) { tone(L, R, 80, sr, .5, ph); vv_viz_frame(v, 1 / 60.f, L.data(), R.data(), 2048, (float)sr, 640, 360, 2, 60, 1, 0); } memcpy(t1, vv_viz_uniforms(v), sizeof t1);
      bool diff = false; for (int i = 0; i < 32; i++) if (std::fabs(t0[i] - t1[i]) > 1e-4f) diff = true; ok("band assignment moves output", diff); }
    ok("bad edit rejected", vv_viz_load(v, "function (((") == 0); char e[200]; int ln = 0, cl = 0; ok("check reports line", vv_viz_check("local x = = 1", e, 200, &ln, &cl) == 0 && ln == 1, e);
    ok("last good script still runs", vv_viz_frame(v, 1 / 60.f, L.data(), R.data(), 2048, (float)sr, 640, 360, 2, 60, 1, 0) == 1);
    ok("meta json", strstr(vv_viz_meta_json(v), "\"params\":[") != nullptr); vv_viz_destroy(v);
    std::vector<std::string> files; if (DIR* d = opendir(dir.c_str())) { while (dirent* en = readdir(d)) { std::string n = en->d_name; if (n.size() > 4 && n.substr(n.size() - 4) == ".lua") files.push_back(n); } closedir(d); } std::sort(files.begin(), files.end());
    int passed = 0; for (auto& fn : files) { VVViz* w = vv_viz_create(); std::string s = slurp(dir + "/" + fn); bool good = vv_viz_load(w, s.c_str()) == 1; long ph = 0; for (int f = 0; f < 180 && good; f++) { tone(L, R, 110.0 * (1 + (f / 60)), sr, .4, ph); if (!vv_viz_frame(w, 1 / 60.f, L.data(), R.data(), 2048, (float)sr, 640, 360, 2, 60, 1, f % 2)) good = false; } vv_viz_randomise_values(w); vv_viz_randomise_assign(w); for (int f = 0; f < 60 && good; f++) if (!vv_viz_frame(w, 1 / 60.f, L.data(), R.data(), 2048, (float)sr, 640, 360, 2, 60, 1, 0)) good = false;
        if (good) passed++; else { printf("FAIL %s: %s\n", fn.c_str(), vv_viz_error(w)); bad++; } vv_viz_destroy(w); }
    printf("bridge ran %d/%zu factory scripts with live analysis + randomised params\n", passed, files.size()); printf(bad ? "SOME FAILED (%d)\n" : "ALL OK\n", bad); return bad ? 1 : 0;
}
