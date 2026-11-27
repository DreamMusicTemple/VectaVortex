// Round 6: XFX MODE as a sequencer target. The pseudo layer XFX expands every sequenced note to the layers the XFX key programs: piano roll (extra.xfxSeq),
// clips (layer "XFX"), the drum sequencer (target "xfx", lane i = key base + i) and the chord maker (target "XFX"). A key whose layers are all switched off stays silent.
#include <cstring>
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>
#include "vv_engine.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL {\"layers\":{\"WT\":{\"type\":\"wavetable\",\"env\":{\"a\":0.005,\"d\":0.1,\"s\":0.8,\"r\":0.05},\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.3,\"env\":{\"a\":0.005,\"d\":0.1,\"s\":0.8,\"r\":0.05}}]},\"FM\":{\"type\":\"fm\"},\"SMP\":{}},\"bpm\":120,:%d  {\"layers\":{\"WT\":{\"type\":\"wavetable\",\"env\":{\"a\":0.005,\"d\":0.1,\"s\":0.8,\"r\":0.05},\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.3,\"env\":{\"a\":0.005,\"d\":0.1,\"s\":0.8,\"r\":0.05}}]},\"FM\":{\"type\":\"fm\"},\"SMP\":{}},\"bpm\":120,  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
static const char* kBase = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"env\":{\"a\":0.005,\"d\":0.1,\"s\":0.8,\"r\":0.05},\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.3,\"env\":{\"a\":0.005,\"d\":0.1,\"s\":0.8,\"r\":0.05}}]},\"FM\":{\"type\":\"fm\"},\"SMP\":{}},\"bpm\":120,";
static std::string xfx() { return "\"xfx\":{\"v\":1,\"enabled\":true,\"active\":0,\"banks\":[{\"name\":\"Bank 1\",\"keys\":{\"60\":{\"en\":{\"WT\":true},\"o\":{}},\"62\":{\"en\":{\"WT\":false},\"o\":{}}}}]}"; }
static std::string withExtra(const std::string& extra, const std::string& top = "") { return std::string(kBase) + top + "\"extra\":{" + xfx() + extra + "}}"; }
static VVEngine* make(const std::string& p) {
    VVEngine* e = vv_engine_create(48000);
    std::vector<unsigned char> wt(8 + 256 * 4); uint32_t fc = 1, fs = 256; memcpy(wt.data(), &fc, 4); memcpy(wt.data() + 4, &fs, 4); for (int i = 0; i < 256; i++) { float f = (float)std::sin(2 * 3.14159265 * i / 256); memcpy(wt.data() + 8 + i * 4, &f, 4); }
    vv_engine_load_wavetable(e, "analogMorph", wt.data(), wt.size());
    CHECK(vv_engine_load_preset_json(e, p.c_str(), p.size()) == 1, "preset loads"); return e;
}
static double run(VVEngine* e, double secs, void (*start)(VVEngine*)) {
    std::vector<float> l(128), r(128); for (int b = 0; b < 20; b++) { vv_engine_pump_params(e); vv_engine_render(e, l.data(), r.data(), 128); }   /* let the preset swap in */
    if (start) start(e); double en = 0; const int blocks = (int)(secs * 48000 / 128);
    for (int b = 0; b < blocks; b++) { vv_engine_pump_params(e); vv_engine_render(e, l.data(), r.data(), 128); for (int i = 0; i < 128; i++) en += (double)l[i] * l[i] + (double)r[i] * r[i]; }
    return en;
}
static void play(VVEngine* e) { vv_engine_set_host_transport(e, 1, 1, 0.0, 120.0); }
static void clip(VVEngine* e) { char out[64]; vv_clip_op(e, "new", "{\"slot\":0,\"layer\":6,\"bars\":1}", out, sizeof out); vv_clip_launch(e, 0); }
static std::string roll(int p) { return "\"xfxSeq\":{\"currentSlot\":0,\"patterns\":[{\"length\":16,\"steps\":[],\"roll\":{\"bars\":1,\"loopOn\":true,\"loopStart\":0,\"loopEnd\":1,\"notes\":[{\"t\":0,\"l\":1,\"p\":" + std::to_string(p) + ",\"v\":0.9,\"prob\":100,\"rat\":1,\"tr\":0,\"pl\":{}}]}}]}"; }
static std::string drum(int base) { std::string st; for (int k = 0; k < 32; k++) st += std::string(k ? "," : "") + (k % 4 == 0 ? "true" : "false"); return "\"drumSeq\":{\"on\":true,\"target\":\"xfx\",\"xfxBase\":" + std::to_string(base) + ",\"lanes\":[{\"length\":16,\"steps\":[" + st + "]}]}"; }
int main() {
    { /* control: the same roll on the real WT layer sounds, so a silent XFX result is a routing problem, not the harness */
      std::string b(kBase); const size_t k = b.find("\"oscs\""); std::string pj = b.substr(0, k) + "\"currentSlot\":0,\"patterns\":[{\"length\":16,\"steps\":[],\"roll\":{\"bars\":1,\"loopOn\":true,\"loopStart\":0,\"loopEnd\":1,\"notes\":[{\"t\":0,\"l\":1,\"p\":60,\"v\":0.9,\"prob\":100,\"rat\":1,\"tr\":0,\"pl\":{}}]}}]," + b.substr(k) + "\"extra\":{}}";
      VVEngine* a = make(pj); const double ea = run(a, 1.2, play); CHECK(ea > 0.01, "control roll on WT is audible (%g)", ea); vv_engine_destroy(a); }
    { VVEngine* a = make(withExtra("," + roll(60))); VVEngine* b = make(withExtra("," + roll(62))); const double ea = run(a, 1.2, play), eb = run(b, 1.2, play);
      CHECK(ea > 0.01 && eb < 1e-9, "XFX piano roll: key 60 sounds its WT layer, key 62 (all layers off) is silent (%g / %g)", ea, eb); vv_engine_destroy(a); vv_engine_destroy(b); }
    { VVEngine* a = make(withExtra("," + drum(60))); VVEngine* b = make(withExtra("," + drum(62))); const double ea = run(a, 1.2, play), eb = run(b, 1.2, play);
      CHECK(ea > 0.01 && eb < 1e-9, "XFX drum sequencer: lane 1 plays key 60 / key 62 silent (%g / %g)", ea, eb); vv_engine_destroy(a); vv_engine_destroy(b); }
    { VVEngine* a = make(withExtra("")); char out[64]; vv_clip_op(a, "new", "{\"slot\":0,\"layer\":6,\"bars\":1}", out, sizeof out);
      std::vector<char> b(1 << 16); const size_t n = vv_clip_state_json(a, b.data(), b.size()); const std::string st(b.data(), std::min(n, b.size() - 1));
      CHECK(st.find("\"layer\":\"XFX\"") != std::string::npos, "clip layer XFX is kept in the clip state"); vv_engine_destroy(a); }
    printf(fails ? "FAILED %d\n" : "all passed\n", fails); return fails ? 1 : 0;
}
