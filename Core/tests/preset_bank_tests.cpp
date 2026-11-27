// Every shipped preset (Assets/default_preset.json + Assets/Presets/*.json) must load through the C ABI and make finite, audible, non-clipping sound
// on a held note, and survive a burst of live edits.
#include <cstdio>
#include <cmath>
#include <dirent.h>
#include <string>
#include <vector>
#include <algorithm>
#include "vv_engine.h"
#include "vv/assets.hpp"
using namespace vv;
#ifndef VV_ASSETS
#define VV_ASSETS "../../Assets"
#endif
int main() {
    const std::string A = VV_ASSETS; int fails = 0, n = 0;
    VVEngine* e = vv_engine_create(48000);
    DIR* d = opendir((A + "/Wavetables").c_str()); int wts = 0;
    if (d) { while (dirent* de = readdir(d)) { std::string f = de->d_name; if (f.size() > 5 && f.substr(f.size() - 5) == ".vvwt") { std::vector<unsigned char> b; readFile(A + "/Wavetables/" + f, b); wts += vv_engine_load_wavetable(e, f.substr(0, f.size() - 5).c_str(), b.data(), b.size()); } } closedir(d); }
    Json bm = JsonParser::parse(readTextFile(A + "/builtin_samples.json")); int sm = 0;
    for (auto& m : bm.a) { std::vector<unsigned char> b; if (readFile(A + "/Samples/" + m.str("file"), b)) sm += vv_engine_load_sample_wav(e, m.str("name").c_str(), b.data(), b.size()); }
    printf("wavetables %d, samples %d\n", wts, sm);
    std::vector<std::string> files = { A + "/default_preset.json" };
    d = opendir((A + "/Presets").c_str());
    if (d) { while (dirent* de = readdir(d)) { std::string f = de->d_name; if (f.size() > 5 && f.substr(f.size() - 5) == ".json") files.push_back(A + "/Presets/" + f); } closedir(d); }
    std::sort(files.begin(), files.end());
    std::vector<float> L(24000), R(24000);
    for (auto& f : files) {
        const std::string txt = readTextFile(f); n++;
        bool ok = vv_engine_load_preset_json(e, txt.c_str(), txt.size()) == 1;
        double peak = 0, rms = 0; bool finite = true;
        if (ok) {
            vv_engine_all_notes_off(e); for (int k = 0; k < 10; k++) vv_engine_render(e, L.data(), R.data(), 4800);
            const uint8_t on[3][3] = {{0x90, 57, 100}, {0x90, 60, 100}, {0x90, 64, 100}}; for (auto& m : on) vv_engine_midi(e, m, 3);
            for (int k = 0; k < 8; k++) {
                vv_engine_render(e, L.data(), R.data(), 24000);
                if (k >= 1) for (int i = 0; i < 24000; i++) { if (!std::isfinite(L[i]) || !std::isfinite(R[i])) finite = false; peak = std::max(peak, (double)std::fabs(L[i])); rms += L[i] * L[i]; }
                if (k == 3) { vv_engine_set_value(e, "layers.WT.oscs.0.level", "0.5"); vv_engine_set_value(e, "fxStages.0.params.mix", "0.3"); vv_engine_set_value(e, "bpm", "140"); }
            }
            rms = std::sqrt(rms / (7 * 24000.0));
            const uint8_t off[3][3] = {{0x80, 57, 0}, {0x80, 60, 0}, {0x80, 64, 0}}; for (auto& m : off) vv_engine_midi(e, m, 3);
        }
        const bool good = ok && finite && rms > 1e-4 && peak <= 1.0001;
        printf("%-34s %s  rms=%.4f peak=%.3f\n", f.substr(f.find_last_of('/') + 1).c_str(), good ? "OK  " : "FAIL", rms, peak); if (!good) fails++;
    }
    vv_engine_destroy(e);
    printf(fails ? "PRESET BANK: %d of %d FAILED\n" : "all %d presets ok\n", fails ? fails : n, n); return fails ? 1 : 0;
}
