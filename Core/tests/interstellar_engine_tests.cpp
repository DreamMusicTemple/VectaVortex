// Interstellar inside the full engine: it is a normal FX stage (audible, deterministic) and every FXP:interstellar:* destination is routable from any source.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <cmath>
#include <vector>
#include <string>
#include "vv_engine.h"
#include "vv/mod_defs.hpp"
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
static std::string readFile(const char* a, const char* b) { std::ifstream f(a); if (!f) f.open(b); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static std::string replaceOnce(std::string s, const std::string& a, const std::string& b) { auto i = s.find(a); if (i != std::string::npos) s.replace(i, a.size(), b); return s; }
static const char* kStage = "[{\"type\":\"interstellar\",\"enabled\":true,\"params\":{\"source\":\"live\",\"freeze\":0,\"capture\":0,\"captureLen\":4,\"span\":8,\"tonal\":0.5,\"band1\":0,\"band2\":0,\"band3\":0,\"band4\":0,\"band5\":0,\"band6\":0,"
    "\"segments\":4,\"segSync\":0,\"segDiv\":\"1/1\",\"segTime\":4,\"segOrder\":\"forward\",\"stretch\":8,\"bloom\":0.5,\"density\":0.5,\"life\":6,\"pitchUp\":12,\"upProb\":0.4,\"pitchDown\":12,\"downProb\":0.3,\"body\":0.7,\"particles\":0.8,\"spread\":0.8,"
    "\"snap\":\"key\",\"scale\":\"auto\",\"key\":-1,\"resonance\":0.3,\"ring\":3,\"strum\":0.3,\"resTune\":\"segment\",\"evolve\":0.4,\"drift\":0.4,\"space\":0.5,\"size\":0.6,\"decay\":0.6,\"predelay\":0.03,\"damp\":0.4,\"revMod\":0.4,\"width\":0.8,\"shimmer\":0.2,\"revFreeze\":0,"
    "\"character\":0,\"mix\":1,\"level\":0,\"quality\":\"eco\"},\"sends\":{\"WT\":true,\"FM\":false,\"SMP\":false,\"PRO\":false}}]";
static VVEngine* make(const std::string& base, const std::string& conns) {
    std::string p = replaceOnce(base, "\"fxStages\": []", std::string("\"fxStages\": ") + kStage);
    if (!conns.empty()) p = replaceOnce(p, "\"connections\": []", std::string("\"connections\": ") + conns);
    VVEngine* e = vv_engine_create(48000);
    std::vector<unsigned char> wt(8 + 256 * 4); uint32_t fc = 1, fs = 256; memcpy(wt.data(), &fc, 4); memcpy(wt.data() + 4, &fs, 4); for (int i = 0; i < 256; i++) { float x = (float)std::sin(6.2831853 * i / 256); memcpy(wt.data() + 8 + 4 * i, &x, 4); }
    vv_engine_load_wavetable(e, "analogMorph", wt.data(), wt.size()); vv_engine_load_preset_json(e, p.c_str(), p.size()); return e;
}
static std::vector<float> render(VVEngine* e, double secs) {
    std::vector<float> out; std::vector<float> L(480), R(480); vv_engine_render(e, L.data(), R.data(), 480);
    unsigned char on[3] = { 0x90, 60, 110 }; vv_engine_midi(e, on, 3);
    for (int i = 0; i < (int)(secs * 100); i++) { vv_engine_render(e, L.data(), R.data(), 480); out.insert(out.end(), L.begin(), L.end()); }
    return out;
}
static double rms(const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; i++) s += (double)v[i] * v[i]; return std::sqrt(s / (b - a)); }
int main() {
    const std::string base = readFile("../Assets/default_preset.json", "../../Assets/default_preset.json");
    CHECK(base.find("\"fxStages\": []") != std::string::npos, "default preset has an empty FX list");
    // audible + deterministic
    VVEngine* a = make(base, ""); VVEngine* b = make(base, ""); auto ra = render(a, 6), rb = render(b, 6);
    CHECK(rms(ra, 48000 * 3, 48000 * 6) > 0.005, "interstellar stage is audible (%.4f)", rms(ra, 48000 * 3, 48000 * 6));
    bool same = ra.size() == rb.size(); for (size_t i = 0; same && i < ra.size(); i++) if (ra[i] != rb[i]) same = false; CHECK(same, "deterministic in the engine");
    // every FXP:interstellar:* destination parses, and an LFO on it changes the sound
    int found = 0; for (int i = 0; i < vv::kNumFxp; i++) if (std::string(vv::kFxpDefs[i].type) == "interstellar") found++;
    CHECK(found >= 36, "interstellar modulation destinations present (%d)", found);
    for (const char* key : { "mix", "density", "space", "stretch", "resonance", "shimmer", "character", "pitchUp" }) {
        std::string conns = std::string("[{\"source\":\"lfo1\",\"dest\":\"FXP:interstellar:") + key + "\",\"amount\":100}]";
        VVEngine* m = make(base, conns); VVEngine* r0 = make(base, "");
        // lfo1 needs a clearly moving shape: 2 Hz sine
        vv_engine_set_value(m, "lfo.lfo1.hz", "2"); vv_engine_set_value(r0, "lfo.lfo1.hz", "2");
        auto x = render(m, 5), y = render(r0, 5); double d = 0; for (size_t i = 48000 * 2; i < x.size() && i < y.size(); i++) d += std::fabs((double)x[i] - y[i]);
        CHECK(d > 0.5, "lfo -> FXP:interstellar:%s changes the sound (diff %.3f)", key, d);
        vv_engine_destroy(m); vv_engine_destroy(r0);
    }
    vv_engine_destroy(a); vv_engine_destroy(b);
    printf(failures ? "INTERSTELLAR ENGINE TESTS FAILED (%d)\n" : "all interstellar engine tests passed\n", failures); return failures ? 1 : 0;
}
