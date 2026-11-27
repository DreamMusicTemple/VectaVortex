// EQ + IR reverb through the C ABI: host parameters create / edit / read back the stages, factory + user IR library, audition, missing IR, state round trip.
#include <cstring>
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>
#include "vv_engine.h"
#include "vv/params_generated.hpp"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
static const char* kPreset = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"level\":0.8},{\"level\":0}]}},\"bpm\":120}";
static uint32_t pid(const char* a) { for (int i = 0; i < vv::kParamCount; i++) if (!strcmp(vv::kParams[i].address, a)) return vv::kParams[i].id; return 0xffffffffu; }
static void render(VVEngine* e, size_t n, std::vector<float>* L = nullptr) { vv_engine_pump_params(e); std::vector<float> a(128), b(128); for (size_t i = 0; i < n; i += 128) { vv_engine_render(e, a.data(), b.data(), 128); if (L) L->insert(L->end(), a.begin(), a.end()); } }
static std::string preset(VVEngine* e) { std::vector<char> b(1 << 20); size_t n = vv_engine_export_preset(e, b.data(), b.size()); return std::string(b.data(), std::min(n, b.size() - 1)); }
static std::vector<unsigned char> wav(int n, int sr) { std::vector<unsigned char> o(44 + n * 2); auto w32 = [&](int p, uint32_t v) { memcpy(&o[p], &v, 4); }; auto w16 = [&](int p, uint16_t v) { memcpy(&o[p], &v, 2); };
    memcpy(&o[0], "RIFF", 4); w32(4, 36 + n * 2); memcpy(&o[8], "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 1); w32(24, sr); w32(28, sr * 2); w16(32, 2); w16(34, 16); memcpy(&o[36], "data", 4); w32(40, n * 2);
    uint32_t s = 7; for (int i = 0; i < n; i++) { s = s * 1664525u + 1013904223u; int16_t v = (int16_t)(((double)s / 4294967296.0 * 2 - 1) * 20000 * std::exp(-4.0 * i / n)); memcpy(&o[44 + i * 2], &v, 2); } return o; }
int main() {
    CHECK(vv_ir_factory_count() == 35 && vv_ir_category_count() == 11, "factory %d cats %d", vv_ir_factory_count(), vv_ir_category_count());
    CHECK(std::string(vv_ir_factory_id(0)) == "f:hall-concert" && std::string(vv_ir_factory_category(0)) == "Halls", "first factory entry");
    VVEngine* e = vv_engine_create(48000); { std::vector<unsigned char> wt(8 + 256 * 4); uint32_t fc = 1, fs = 256; memcpy(wt.data(), &fc, 4); memcpy(wt.data() + 4, &fs, 4); for (int i = 0; i < 256; i++) { float f = (float)std::sin(2 * 3.14159265 * i / 256); memcpy(wt.data() + 8 + i * 4, &f, 4); } vv_engine_load_wavetable(e, "analogMorph", wt.data(), wt.size()); }
    CHECK(vv_engine_load_preset_json(e, kPreset, strlen(kPreset)) == 1, "preset");
    CHECK(preset(e).find("\"ir\"") == std::string::npos, "no ir stage initially");
    vv_engine_set_param(e, pid("fxir_mix"), 0.5f); render(e, 1024); CHECK(preset(e).find("\"ir\"") == std::string::npos, "a parameter edit alone never creates the stage");
    vv_engine_set_param(e, pid("fxir_on"), 1); vv_engine_set_param(e, pid("fxeq_on"), 1); render(e, 2048);
    std::string p = preset(e); CHECK(p.find("\"type\":\"ir\"") != std::string::npos && p.find("\"type\":\"eq\"") != std::string::npos, "on creates both stages: %.200s", p.c_str());
    vv_engine_set_param(e, pid("fxir_factory"), 14); vv_engine_set_param(e, pid("fxeq_band2_gain"), 6.f); vv_engine_set_param(e, pid("fxir_predelay"), 40.f); render(e, 2048);
    p = preset(e); CHECK(p.find(vv_ir_factory_id(14)) != std::string::npos, "factory index sets irId"); CHECK(p.find("\"g2\":6") != std::string::npos, "EQ band gain edits the slot"); CHECK(p.find("\"predelay\":40") != std::string::npos, "pre-delay edits the slot");
    CHECK(vv_ir_wait_ready(5000) == 1, "IR ready"); render(e, 4096);
    vv_engine_set_param(e, pid("fxir_on"), 0); render(e, 1024); CHECK(preset(e).find("\"enabled\":false") != std::string::npos, "off disables (keeps the stage)");
    // user IR: import bytes, list, reference by id, missing after remove, audition
    auto w = wav(24000, 44100); char id[64], msg[256]; CHECK(vv_ir_import_bytes(w.data(), w.size(), "My Room", "", id, 64, msg, 256) == 1, "import: %s", msg); CHECK(std::string(id).rfind("u:", 0) == 0, "user id %s", id);
    std::vector<char> js(1 << 16); vv_ir_user_json(e ? js.data() : nullptr, js.size()); CHECK(std::string(js.data()).find("My Room") != std::string::npos, "listed");
    char bad[] = "not a wav file at all"; CHECK(vv_ir_import_bytes((const uint8_t*)bad, sizeof bad, "bad", "", id, 64, msg, 256) == 0 && msg[0], "garbage rejected: %s", msg);
    const std::string uid = [&] { std::string t(js.data()); size_t a = t.find("\"id\":\"") + 6; return t.substr(a, t.find('"', a) - a); }();
    std::string params = "{\"irId\":\"" + uid + "\"}"; std::vector<float> L(96000), R(96000); const int n = vv_ir_audition(params.c_str(), L.data(), R.data(), 96000, 48000);
    double en = 0; for (int i = 0; i < n; i++) en += L[i] * L[i]; CHECK(n > 40000 && en > 0 && std::isfinite(en), "audition n=%d en=%g", n, en);
    std::vector<float> env(64); const double secs = vv_ir_envelope(uid.c_str(), env.data(), 64, 48000); CHECK(secs > 0.4 && secs < 0.6 && env[0] > 0.9, "envelope secs=%.3f e0=%.2f", secs, env[0]);
    vv_engine_set_param(e, pid("fxir_on"), 1); render(e, 512);
    // point the stage at the user IR via the preset API, then remove it: the stage keeps running (default hall) and reports it missing
    char setb[256]; snprintf(setb, sizeof setb, "\"%s\"", uid.c_str()); vv_engine_set_value(e, "fxStages.1.params.irId", setb); render(e, 4096);
    CHECK(vv_ir_remove(uid.c_str()) == 1, "remove"); vv_engine_set_value(e, "fxStages.1.params.irName", "\"x\""); render(e, 8192);
    std::vector<float> o; vv_engine_note_on(e, 60, 0.9f); render(e, 48000, &o); double mx = 0; for (float v : o) { if (!std::isfinite(v)) mx = 1e9; mx = std::max(mx, (double)std::fabs(v)); } CHECK(mx > 0.001 && mx < 4, "output finite after the IR vanished, peak %.3f", mx);
    vv_ir_missing_json(js.data(), js.size()); CHECK(std::string(js.data()).find(uid) != std::string::npos, "missing reported: %s", js.data());
    // state round trip through a fresh engine
    const std::string saved = preset(e); VVEngine* e2 = vv_engine_create(48000); CHECK(vv_engine_load_preset_json(e2, saved.c_str(), saved.size()) == 1, "restore"); render(e2, 4096);
    CHECK(preset(e2).find("\"type\":\"eq\"") != std::string::npos, "stages restored"); vv_engine_destroy(e2); vv_engine_destroy(e);
    // ---- versioned state: legacy / damaged / newer presets
    { VVEngine* m = vv_engine_create(48000); char wb[1024];
      const char* legacy = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"level\":0.8}]},\"FM\":{\"type\":\"fm\"},\"SMP\":{}},\"fxStages\":[{\"type\":\"reverb\",\"params\":{\"mix\":0.4}},{\"type\":\"wormhole\",\"params\":{}},7,{\"type\":\"eq\",\"params\":{\"f1\":5,\"g2\":99}},{\"type\":\"ir\",\"params\":{\"start\":0.9,\"end\":0.1}}]}";
      CHECK(vv_engine_load_preset_json(m, legacy, strlen(legacy)) == 1, "legacy preset loads");
      vv_engine_load_warnings(m, wb, sizeof wb); std::string w = wb; CHECK(w.find("wormhole") != std::string::npos && w.find("Effect 3") != std::string::npos, "unknown / junk effects reported: %s", wb);
      std::string pj = preset(m); CHECK(pj.find("wormhole") == std::string::npos && pj.find("\"schemaVersion\":3") != std::string::npos, "skipped + stamped");
      CHECK(pj.find("\"g2\":24") != std::string::npos && pj.find("\"f1\":20") != std::string::npos, "eq repaired / clamped");
      CHECK(pj.find("\"irId\":\"f:hall-concert\"") != std::string::npos, "ir backfilled with the default hall");
      render(m, 4096); vv_ir_wait_ready(3000); render(m, 4096);
      const std::string before = preset(m);
      for (const char* bad : {"", "[]", "5", "{", "{}", "{\"layers\":5}", "\xff\xfe"}) CHECK(vv_engine_load_preset_json(m, bad, strlen(bad)) == 0, "damaged preset refused: %s", bad);
      CHECK(preset(m) == before, "a refused load leaves the current sound untouched");
      std::string big = "{\"layers\":{\"WT\":{},\"FM\":{},\"SMP\":{}},\"schemaVersion\":99,\"fxStages\":["; for (int i = 0; i < 40; i++) big += std::string(i ? "," : "") + "{\"type\":\"delay\",\"params\":{}}"; big += "]}";
      CHECK(vv_engine_load_preset_json(m, big.c_str(), big.size()) == 1, "future schema loads"); vv_engine_load_warnings(m, wb, sizeof wb); CHECK(std::string(wb).find("newer version") != std::string::npos && std::string(wb).find("first 24") != std::string::npos, "future + cap notices: %s", wb);
      vv_engine_destroy(m); }
    printf(fails ? "FAILED %d\n" : "all passed\n", fails); return fails ? 1 : 0;
}
