// Audit regression: jsonEqual must agree with jsonDump equality; live FX edits must still reach the stage; adopting a live edit must not allocate in bulk.
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <new>
#include <atomic>
#include <string>
#include <vector>
#include <dirent.h>
#include "vv_engine.h"
#include "vv/assets.hpp"
static std::atomic<long> g_allocs{0}; static thread_local bool g_count = false;
void* operator new(size_t n) { if (g_count) g_allocs++; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); } void operator delete(void* p, size_t) noexcept { std::free(p); }
using namespace vv;
static int fails = 0;
static void check(const char* n, bool ok) { std::printf("%-62s %s\n", n, ok ? "OK" : "FAIL"); if (!ok) fails++; }
static double energy(VVEngine* e, int blocks) { std::vector<float> L(512), R(512); double s = 0; for (int k = 0; k < blocks; k++) { vv_engine_render(e, L.data(), R.data(), 512); for (int i = 0; i < 512; i++) s += (double)L[i] * L[i] + (double)R[i] * R[i]; } return s; }
int main() {
    const std::string A = std::string(VV_ASSETS);
    // 1) jsonEqual == (jsonDump equal) over every shipped preset's fx params / vocoder, against each neighbour
    std::vector<Json> js; DIR* d = opendir((A + "/Presets").c_str()); std::vector<std::string> fs;
    while (dirent* de = readdir(d)) { std::string f = de->d_name; if (f.size() > 5 && f.substr(f.size() - 5) == ".json") fs.push_back(f); } closedir(d);
    for (auto& f : fs) { Json j = JsonParser::parse(readTextFile(A + "/Presets/" + f)); if (const Json* st = j.get("fxStages")) for (auto& s : st->a) if (const Json* pp = s.get("params")) js.push_back(*pp); if (const Json* v = j.get("vocoder")) js.push_back(*v); }
    bool agree = true; size_t pairs = 0; for (size_t i = 0; i < js.size(); i++) for (size_t k : {i, (i + 1) % js.size(), (i * 7 + 3) % js.size()}) { pairs++; if (jsonEqual(js[i], js[k]) != (jsonDump(js[i]) == jsonDump(js[k]))) agree = false; }
    char nm[100]; std::snprintf(nm, sizeof nm, "jsonEqual agrees with dump equality (%zu pairs)", pairs); check(nm, agree && pairs > 100);
    { Json a = JsonParser::parse("{\"a\":[1,2,{\"b\":\"x\"}],\"c\":true}"), b = JsonParser::parse("{\"a\":[1,2,{\"b\":\"x\"}],\"c\":true}"), c = JsonParser::parse("{\"a\":[1,2,{\"b\":\"y\"}],\"c\":true}"), e2 = JsonParser::parse("{\"c\":true,\"a\":[1,2,{\"b\":\"x\"}]}");
      check("jsonEqual: equal / value-diff / order-diff", jsonEqual(a, b) && !jsonEqual(a, c) && !jsonEqual(a, e2)); }
    // 2) a live FX param edit still changes the sound, and the adopting block does not allocate in bulk
    VVEngine* e = vv_engine_create(48000);
    { DIR* dd = opendir((A + "/Wavetables").c_str()); while (dirent* de = readdir(dd)) { std::string f = de->d_name; if (f.size() > 5 && f.substr(f.size() - 5) == ".vvwt") { std::vector<unsigned char> b; readFile(A + "/Wavetables/" + f, b); vv_engine_load_wavetable(e, f.substr(0, f.size() - 5).c_str(), b.data(), b.size()); } } closedir(dd); }
    const std::string txt = readTextFile(A + "/Presets/Hall Verb Keys.json"); vv_engine_load_preset_json(e, txt.c_str(), txt.size());
    std::vector<float> L(512), R(512); for (int k = 0; k < 4; k++) vv_engine_render(e, L.data(), R.data(), 512);
    uint8_t on[3] = {0x90, 60, 100}, off[3] = {0x80, 60, 0}; vv_engine_midi(e, on, 3); energy(e, 20);
    vv_engine_set_value(e, "fxStages.0.params.mix", "0"); energy(e, 3); const double dry = energy(e, 12);
    vv_engine_set_value(e, "fxStages.0.params.mix", "1"); energy(e, 3); const double wet = energy(e, 12);
    check("fx mix edit 0 -> 1 reaches the reverb (output changes)", std::fabs(dry - wet) > 1e-6 * (dry + wet) && std::isfinite(dry) && std::isfinite(wet));
    vv_engine_set_value(e, "fxStages.0.params.mix", "0"); energy(e, 3); const double dry2 = energy(e, 12);
    check("fx mix edit 1 -> 0 restores the dry output", std::fabs(dry2 - dry) < 0.2 * dry + 1e-9);
    long worst = 0; for (int i = 0; i < 30; i++) { char v[16]; std::snprintf(v, 16, "%g", 0.1 + 0.02 * i); vv_engine_set_value(e, "fxStages.0.params.mix", v); g_allocs = 0; g_count = true; vv_engine_render(e, L.data(), R.data(), 512); g_count = false; worst = std::max(worst, (long)g_allocs); vv_engine_render(e, L.data(), R.data(), 512); }
    std::snprintf(nm, sizeof nm, "allocations in a live-edit adopting block (worst %ld, was 17)", worst); check(nm, worst <= 4);
    vv_engine_midi(e, off, 3); vv_engine_destroy(e);
    std::printf("%s (%d failures)\n", fails ? "FAILED" : "PASSED", fails); return fails ? 1 : 0;
}
