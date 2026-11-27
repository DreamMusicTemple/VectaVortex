// Preset generator (GENERATE per category) and MODIFY: every category produces valid, audible, seed-deterministic patches; MODIFY stays within +-10 %.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cmath>
#include <vector>
#include "vv/model.hpp"
#include "vv/presetgen.hpp"
#include <dirent.h>
#include <algorithm>
#include "vv_engine.h"
#include "vv/assets.hpp"
#ifndef VV_ASSETS
#define VV_ASSETS "../../Assets"
#endif
using namespace vv;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static std::string readFile(const char* a, const char* b) { std::ifstream f(a); if (!f) f.open(b); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static double energyAfter(VVEngine* e, double seconds, bool play) {
    { std::vector<float> L(480), R(480); vv_engine_render(e, L.data(), R.data(), 480); }   // applies the staged preset update
    if (play) { vv_engine_all_notes_off(e); unsigned char on[3] = { 0x90, 60, 110 }; vv_engine_midi(e, on, 3); }
    std::vector<float> L(480), R(480); double en = 0; for (int i = 0; i < (int)(seconds * 100); i++) { vv_engine_render(e, L.data(), R.data(), 480); for (float x : L) en += (double)x * x; } return en;
}
static VVEngine* makeEngine(const std::string& preset) {
    static std::vector<std::pair<std::string, std::vector<unsigned char>>> wts, smps; static bool loaded = false;
    if (!loaded) { loaded = true; const std::string A = VV_ASSETS;
        if (DIR* d = opendir((A + "/Wavetables").c_str())) { while (dirent* de = readdir(d)) { std::string f = de->d_name; if (f.size() > 5 && f.substr(f.size() - 5) == ".vvwt") { std::vector<unsigned char> b; readFile(A + "/Wavetables/" + f, b); wts.push_back({ f.substr(0, f.size() - 5), b }); } } closedir(d); }
        Json bm = JsonParser::parse(readTextFile(A + "/builtin_samples.json")); for (auto& m : bm.a) { std::vector<unsigned char> b; if (readFile(A + "/Samples/" + m.str("file"), b)) smps.push_back({ m.str("name"), b }); } }
    VVEngine* e = vv_engine_create(48000);
    for (auto& w : wts) vv_engine_load_wavetable(e, w.first.c_str(), w.second.data(), w.second.size());
    for (auto& m : smps) vv_engine_load_sample_wav(e, m.first.c_str(), m.second.data(), m.second.size());
    vv_engine_load_preset_json(e, preset.c_str(), preset.size()); return e;
}
int main() {
    const std::string def = readFile("../Assets/default_preset.json", "../../Assets/default_preset.json");
    CHECK(def.size() > 1000, "default preset readable");
    const char* cats[] = { "Arp", "Bass", "Key", "Lead", "Pad", "Drum", "FX", "XFX" };
    const int playNote[] = { 60, 40, 60, 72, 60, 36, 60, 60 };
    for (int ci = 0; ci < 8; ci++) {
        const char* c = cats[ci];
        for (uint32_t seed : { 1234u, 77u, 31337u }) {
            VVEngine* e = makeEngine(def);
            CHECK(vv_engine_preset_action(e, "generate", c, seed) == 1, "%s generate", c);
            char buf[400000]; vv_engine_get_value(e, "", buf, sizeof buf); Json j = JsonParser::parse(buf);
            const Json* lv = jsonPathPtr(const_cast<Json&>(j), "layers.SID.oscs.0.env.rep", false); CHECK(lv != nullptr, "%s: all five layers present in the generated preset", c);
            { std::vector<float> L(480), R(480); vv_engine_render(e, L.data(), R.data(), 480); }
            vv_engine_all_notes_off(e); unsigned char on[3] = { 0x90, (unsigned char)playNote[ci], 110 }; vv_engine_midi(e, on, 3);
            std::vector<float> L(480), R(480); double en = 0; for (int i = 0; i < 400; i++) { vv_engine_render(e, L.data(), R.data(), 480); for (float x : L) en += (double)x * x; }
            CHECK(en > 0.05, "%s seed %u is audible (energy %.3f)", c, seed, en);
            vv_engine_destroy(e);
        }
        VVEngine* e = makeEngine(def); vv_engine_preset_action(e, "generate", c, 1234); char b1[400000], b2[400000]; vv_engine_get_value(e, "", b1, sizeof b1);
        VVEngine* e2 = makeEngine(def); vv_engine_preset_action(e2, "generate", c, 1234); vv_engine_get_value(e2, "", b2, sizeof b2);
        CHECK(std::string(b1) == std::string(b2), "%s: same seed gives the same patch", c);
        vv_engine_preset_action(e2, "generate", c, 999); vv_engine_get_value(e2, "", b2, sizeof b2); CHECK(std::string(b1) != std::string(b2), "%s: another seed gives another patch", c);
        vv_engine_destroy(e2); vv_engine_destroy(e);
    }
    {   // scale lock: step-sequencer pitches and XFX programming follow the chosen scale
        char nm[80], vw[8]; const uint32_t mask = (1u << 0) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 10);   // minor pentatonic
        VVEngine* e = makeEngine(def);
        CHECK(vv_engine_preset_generate(e, "XFX", 4242, 1, 9, mask, "minorPent", nm, sizeof nm, vw, sizeof vw) == 1, "XFX generate with scale");
        char buf[600000]; vv_engine_get_value(e, "", buf, sizeof buf); Json j = JsonParser::parse(buf);
        Json* keys = jsonPathPtr(j, "extra.xfx.banks.0.keys", false); CHECK(keys && keys->type == Json::Obj && keys->o.size() > 5, "XFX keys programmed");
        int outside = 0; if (keys) for (auto& kv : keys->o) { const int n = atoi(kv.first.c_str()); if (!(mask & (1u << ((((n - 9) % 12) + 12) % 12)))) outside++; }
        CHECK(outside == 0, "XFX with Scale lock programs only in-scale keys (%d outside)", outside);
        CHECK(std::string(nm).size() > 0 && std::string(vw).size() > 0, "variant name '%s' and view '%s' returned", nm, vw);
        vv_engine_destroy(e);
        for (uint32_t seed = 1; seed < 40; seed++) {   // Acid Squelch / Pattern Pluck patterns: every active step is in the scale
            VVEngine* e2 = makeEngine(def); vv_engine_preset_generate(e2, seed % 2 ? "Bass" : "Arp", seed, 1, 9, mask, "minorPent", nm, sizeof nm, vw, sizeof vw);
            char b[600000]; vv_engine_get_value(e2, "", b, sizeof b); Json jj = JsonParser::parse(b);
            for (const char* L : { "WT", "PRO" }) { Json* st = jsonPathPtr(jj, (std::string("layers.") + L + ".patterns.0.steps").c_str(), false);
                if (st) for (auto& s : st->a) if (s.boolean("on", false)) { const int p = (int)s.num("pitch", 0); if (s.num("pitch", 0) != 0 || std::string(nm) == "Acid Squelch") { const int rel = (((60 + p - 9) % 12) + 12) % 12; CHECK(mask & (1u << rel), "%s: step pitch %d is in the scale", nm, p); } } }
            vv_engine_destroy(e2);
        }
    }
    { VVEngine* e = makeEngine(def); CHECK(vv_engine_preset_action(e, "generate", "Nonsense", 1) == 0, "unknown category refused"); vv_engine_destroy(e); }
    {   // MODIFY: numbers move by at most 10 %, structure (pattern length, algorithms, zones) is untouched
        VVEngine* e = makeEngine(def); vv_engine_preset_action(e, "generate", "Key", 5);   // Tine Electric Piano / Bell Keys etc.
        char a[200000], b[200000]; vv_engine_get_value(e, "", a, sizeof a);
        CHECK(vv_engine_preset_action(e, "modify", "", 77) == 1, "modify"); vv_engine_get_value(e, "", b, sizeof b);
        Json ja = JsonParser::parse(a), jb = JsonParser::parse(b); CHECK(std::string(a) != std::string(b), "modify changes the sound");
        auto num = [](Json& j, const char* p) { Json* t = jsonPathPtr(j, p, false); return t ? t->asNum(-999) : -999.0; };
        for (const char* p : { "layers.FM.oscs.0.ratio", "layers.FM.oscs.1.level", "layers.FM.env.d", "layers.FM.oscs.0.filter.cutoff", "layers.WT.oscs.0.level" }) { const double x = num(ja, p), y = num(jb, p); CHECK(std::fabs(y - x) <= std::fabs(x) * 0.1001 + 1e-9, "%s moved by <= 10 %% (%g -> %g)", p, x, y); }
        CHECK(num(ja, "layers.FM.patterns.0.length") == num(jb, "layers.FM.patterns.0.length"), "pattern length untouched");
        CHECK(num(ja, "layers.FM.fmAlgorithms.0") == num(jb, "layers.FM.fmAlgorithms.0"), "algorithm untouched");
        CHECK(num(ja, "layers.WT.oscs.0.rangeHigh") == num(jb, "layers.WT.oscs.0.rangeHigh"), "zone untouched");
        vv_engine_destroy(e);
    }
    {   // MIDI CC map (web CC_MAP) and MIDI LEARN
        VVEngine* e = makeEngine(def); { std::vector<float> L(480), R(480); vv_engine_render(e, L.data(), R.data(), 480); }   // adopt the preset
        auto val = [&](const char* path) { char b[64]; vv_engine_get_value(e, path, b, sizeof b); return atof(b); };
        auto cc = [&](int n, int v, int ch = 0) { unsigned char m[3] = { (unsigned char)(0xB0 | ch), (unsigned char)n, (unsigned char)v }; vv_engine_midi(e, m, 3); };
        cc(74, 127); cc(71, 64); cc(73, 127); cc(106, 127); cc(102, 127); cc(7, 64);
        CHECK(vv_engine_poll_cc(e) >= 4, "poll handles the recorded CCs");
        CHECK(std::abs(val("layers.WT.filter.cutoff") - 18000) < 1, "CC74 = Wavetable cutoff (%g)", val("layers.WT.filter.cutoff"));
        CHECK(std::abs(val("layers.WT.filter.reso") - 64.0 / 127 * 130) < 0.1, "CC71 = Wavetable resonance (%g)", val("layers.WT.filter.reso"));
        CHECK(std::abs(val("layers.WT.env.a") - 3.001) < 0.01, "CC73 = Wavetable attack (%g)", val("layers.WT.env.a"));
        CHECK(std::abs(val("lfo.lfo1.hz") - 20.0) < 0.01, "CC106 = LFO 1 rate (%g)", val("lfo.lfo1.hz"));
        CHECK(vv_engine_poll_cc(e) == 0, "nothing new to poll");
        cc(20, 0); vv_engine_poll_cc(e); CHECK(std::abs(val("layers.FM.filter.cutoff") - 20) < 1, "CC20 = FM cutoff, no longer a macro (%g)", val("layers.FM.filter.cutoff"));
        // learn: arm a path, the next CC maps to it and later CCs drive it
        vv_engine_cc_learn_arm(e, "layers.WT.oscs.0.level", 0, 1); cc(50, 100); vv_engine_poll_cc(e);
        CHECK(vv_engine_cc_learned(e) == 50, "CC50 learned"); cc(50, 127); vv_engine_poll_cc(e); CHECK(std::abs(val("layers.WT.oscs.0.level") - 1.0) < 1e-3, "learned CC drives its parameter (%g)", val("layers.WT.oscs.0.level"));
        char js[2000]; vv_engine_cc_learn_json(e, js, sizeof js); CHECK(strstr(js, "\"50\"") && strstr(js, "oscs.0.level"), "learned map exports as JSON: %s", js);
        vv_engine_cc_learn_clear(e); cc(50, 0); vv_engine_poll_cc(e); CHECK(std::abs(val("layers.WT.oscs.0.level") - 1.0) < 1e-3, "cleared map: CC50 does nothing");
        CHECK(vv_engine_cc_learn_set_json(e, js) == 1, "map re-imports"); cc(50, 0); vv_engine_poll_cc(e); CHECK(val("layers.WT.oscs.0.level") < 0.01, "re-imported map drives the parameter again");
        // a learned mapping beats the built-in one
        vv_engine_cc_learn_arm(e, "layers.FM.filter.cutoff", 100, 200); cc(74, 0); vv_engine_poll_cc(e); cc(74, 127); vv_engine_poll_cc(e); CHECK(std::abs(val("layers.FM.filter.cutoff") - 200) < 0.01, "learned mapping overrides CC74 default (%g)", val("layers.FM.filter.cutoff"));
        vv_engine_destroy(e);
    }
    printf(failures ? "preset generator tests: %d FAILED\n" : "preset generator tests: all passed\n", failures); return failures ? 1 : 0;
}
