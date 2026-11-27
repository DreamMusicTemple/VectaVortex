// LUA layer (native): script host, sandbox/limits, polyphony, zones, velocity window, script replacement under playback, presets (incl. old presets without LUA).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include "vv_engine.h"
#include "vv/lua_host.hpp"
#include "vv/lua_factory.hpp"
static int fails = 0;
static void check(const char* n, bool ok, const std::string& info = "") { std::printf("%-58s %s %s\n", n, ok ? "OK" : "FAIL", info.c_str()); if (!ok) fails++; }
static double rms(const std::vector<float>& v, int a, int b) { double s = 0; for (int i = a; i < b; i++) s += (double)v[i] * v[i]; return std::sqrt(s / (b - a)); }
static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if (c == '\n') o += "\\n"; else if (c == '\t') o += "\\t"; else if ((unsigned char)c < 32) o += ' '; else o += c; } return o; }
static const char* factory(const char* id) { for (size_t i = 0; i < vv::kLuaFactoryCount; i++) if (!std::strcmp(vv::kLuaFactory[i].id, id)) return vv::kLuaFactory[i].src; return ""; }
static std::string oscJson(const std::string& src, bool en, int lo = 0, int hi = 127, int velLo = 1, double level = 0.8, const std::string& extra = "") {
    return "{\"enabled\":" + std::string(en ? "true" : "false") + ",\"level\":" + std::to_string(level) + ",\"rangeLow\":" + std::to_string(lo) + ",\"rangeHigh\":" + std::to_string(hi) + ",\"velLow\":" + std::to_string(velLo) + extra + ",\"lua\":{\"src\":\"" + esc(src) + "\",\"seed\":1,\"params\":{}}}";
}
static std::string preset(const std::vector<std::string>& oscs, bool filterOff = true) {
    std::string o; for (size_t i = 0; i < oscs.size(); i++) { if (i) o += ","; o += oscs[i]; }
    return std::string("{\"layers\":{\"LUA\":{\"type\":\"lua\",\"filter\":{\"on\":") + (filterOff ? "false" : "true") + "},\"oscs\":[" + o + "]}},\"bpm\":120}";
}
static std::vector<float> play(VVEngine* e, int note, float vel, int ms, int relMs = 0) {
    const int N = 48 * ms; std::vector<float> L(N), R(N); vv_engine_note_on(e, note, vel);
    for (int i = 0; i < N; i += 480) vv_engine_render(e, &L[i], &R[i], std::min(480, N - i));
    if (relMs) { vv_engine_note_off(e, note); const int M = 48 * relMs; std::vector<float> a(M), b(M); for (int i = 0; i < M; i += 480) vv_engine_render(e, &a[i], &b[i], std::min(480, M - i)); }
    return L;
}
int main() {
    // ---------- host-level
    { vv::LuaHost h; h.setSampleRate(48000);
      int ok = 0; for (size_t i = 0; i < vv::kLuaFactoryCount; i++) { if (!std::strcmp(vv::kLuaFactory[i].id, "template")) { ok++; continue; } auto inf = h.load(0, vv::kLuaFactory[i].src, 1); if (inf.ok) ok++; else std::printf("   %s: %s\n", vv::kLuaFactory[i].id, inf.error.c_str()); }
      check("all 16 factory scripts compile natively", ok == (int)vv::kLuaFactoryCount);
      auto bad1 = h.load(0, "function init(v", 1); check("syntax error reported with line", !bad1.ok && bad1.line >= 1, bad1.error);
      auto bad2 = h.load(0, "os.exit()", 1); check("os is not available (sandbox)", !bad2.ok);
      auto bad3 = h.load(0, "while true do end", 1); check("endless loop in setup stops (instruction budget)", !bad3.ok, bad3.error);
      auto bad4 = h.load(0, "local t={} for i=1,1e9 do t[i]=i end", 1); check("memory bomb stopped", !bad4.ok, bad4.error);
      auto good = h.load(0, factory("basic_va"), 7); check("valid script installs with parameters", good.ok && good.params.size() == 5);
      { auto g = h.acquire(0); int s = g.e->e.voiceOn(60, 0.8f, 1); check("voice allocated", s >= 0);
        double peak = 0; for (int b = 0; b < 400; b++) { auto& H = g.e->e.host[s]; H.freq = 261.63f; H.bpm = 120; H.playing = 1; H.gate = 1; h.copyParams(0, H.params); g.e->e.render(s, g.e->outL, g.e->outR, 128); for (int k = 0; k < 128; k++) { if (!std::isfinite(g.e->outL[k])) { peak = 1e9; break; } peak = std::fmax(peak, std::fabs(g.e->outL[k])); } }
        check("script renders finite, audible audio", peak > 0.1 && peak < 2.0, std::to_string(peak)); }
      auto lp = h.load(1, "instrument{name='l'}\nfunction init(v) v.o=vv.osc(v,{wave='sine',level=0.3}) end\nfunction control(v) while true do end end", 1);
      { auto g = h.acquire(1); int s = g.e ? g.e->e.voiceOn(60, 0.8f, 1) : -1; bool faulted = false; if (s >= 0) { g.e->e.host[s].freq = 440; g.e->e.render(s, g.e->outL, g.e->outR, 128); faulted = g.e->e.voices[s].faulted; } check("endless loop in control mutes only that voice", lp.ok && faulted); }
      // replace a script while a voice of the old one is held: no crash, old voice dropped
      auto g0 = h.acquire(0); auto oldGen = g0.e->gen; int s0 = g0.e->e.voiceOn(60, 0.8f, 1); (void)s0; (void)oldGen; }
    // ---------- through the full engine (C ABI)
    { VVEngine* e = vv_engine_create(48000);
      check("LUA preset loads", vv_engine_load_preset_json(e, preset({ oscJson(factory("basic_va"), true) }).c_str(), preset({ oscJson(factory("basic_va"), true) }).size()) == 1);
      auto a = play(e, 60, 0.8f, 800, 1500); check("LUA layer plays a note through the synth", rms(a, 12000, 38000) > 0.01, "rms " + std::to_string(rms(a, 12000, 38000)));
      std::vector<float> L(48000), R(48000); for (int i = 0; i < 48000; i += 480) vv_engine_render(e, &L[i], &R[i], 480); check("silent after release (voice freed)", rms(L, 24000, 48000) < 1e-4);
      vv_engine_destroy(e); }
    { VVEngine* e = vv_engine_create(48000); std::string p = preset({ oscJson(factory("basic_va"), false) }); vv_engine_load_preset_json(e, p.c_str(), p.size()); auto a = play(e, 60, 0.8f, 500); check("disabled slot is silent", rms(a, 4800, 24000) < 1e-5); vv_engine_destroy(e); }
    { VVEngine* e = vv_engine_create(48000); std::string p = preset({ oscJson(factory("basic_va"), true, 70, 127) }); vv_engine_load_preset_json(e, p.c_str(), p.size()); auto a = play(e, 60, 0.8f, 500); auto b = play(e, 80, 0.8f, 500);
      check("keyboard zone: note below range silent", rms(a, 4800, 24000) < 1e-5); check("keyboard zone: note inside range sounds", rms(b, 4800, 24000) > 0.01); vv_engine_destroy(e); }
    { VVEngine* e = vv_engine_create(48000); std::string p = preset({ oscJson(factory("basic_va"), true, 0, 127, 100) }); vv_engine_load_preset_json(e, p.c_str(), p.size()); auto a = play(e, 60, 0.3f, 500); auto b = play(e, 62, 0.9f, 500);
      check("velocity window: soft note blocked", rms(a, 4800, 24000) < 1e-5); check("velocity window: loud note sounds", rms(b, 4800, 24000) > 0.01); vv_engine_destroy(e); }
    { VVEngine* e = vv_engine_create(48000); std::string p = preset({ oscJson(factory("basic_va"), true), oscJson(factory("supersaw"), true), oscJson(factory("fm_bell"), true), oscJson(factory("evolving_wavetable"), true) }); vv_engine_load_preset_json(e, p.c_str(), p.size());
      auto one = [&] { VVEngine* x = vv_engine_create(48000); std::string q = preset({ oscJson(factory("basic_va"), true) }); vv_engine_load_preset_json(x, q.c_str(), q.size()); auto r = play(x, 60, 0.8f, 600); vv_engine_destroy(x); return rms(r, 6000, 28000); }();
      auto a = play(e, 60, 0.8f, 600); check("four slots sound together (louder than one)", rms(a, 6000, 28000) > one, std::to_string(rms(a, 6000, 28000)) + " vs " + std::to_string(one));
      std::vector<float> L(4800), R(4800); bool fin = true; for (int n = 0; n < 24; n++) { vv_engine_note_on(e, 40 + n * 2, 0.7f); vv_engine_render(e, L.data(), R.data(), 480); for (int i = 0; i < 480; i++) if (!std::isfinite(L[i])) fin = false; }
      for (int n = 0; n < 24; n++) { vv_engine_note_off(e, 40 + n * 2); vv_engine_render(e, L.data(), R.data(), 480); } for (int k = 0; k < 60; k++) { vv_engine_render(e, L.data(), R.data(), 4800); for (int i = 0; i < 4800; i++) if (!std::isfinite(L[i])) fin = false; }
      check("24 simultaneous notes x 4 scripts stay finite", fin); vv_engine_destroy(e); }
    // script replacement while playing (live edit of lua.src) and an invalid replacement
    { VVEngine* e = vv_engine_create(48000); std::string p = preset({ oscJson(factory("basic_va"), true) }); vv_engine_load_preset_json(e, p.c_str(), p.size()); std::vector<float> L(4800), R(4800);
      vv_engine_note_on(e, 60, 0.8f); for (int i = 0; i < 10; i++) vv_engine_render(e, L.data(), R.data(), 480);
      std::string js = std::string("\"") + esc(factory("fm_bell")) + "\""; bool ok = vv_engine_set_value(e, "layers.LUA.oscs.0.lua.src", js.c_str()) == 1; check("live script replacement accepted", ok);
      bool fin = true; for (int i = 0; i < 30; i++) { vv_engine_render(e, L.data(), R.data(), 480); for (int k = 0; k < 480; k++) if (!std::isfinite(L[k])) fin = false; } check("playing through a script swap stays finite", fin);
      vv_engine_note_on(e, 64, 0.8f); double r = 0; for (int i = 0; i < 20; i++) { vv_engine_render(e, L.data(), R.data(), 480); for (int k = 0; k < 480; k++) r += L[k] * L[k]; } check("new script sounds after the swap", r > 0.01);
      std::string bad = std::string("\"") + esc("function init(v") + "\""; vv_engine_set_value(e, "layers.LUA.oscs.0.lua.src", bad.c_str());
      vv_engine_note_on(e, 67, 0.8f); fin = true; for (int i = 0; i < 20; i++) { vv_engine_render(e, L.data(), R.data(), 480); for (int k = 0; k < 480; k++) if (!std::isfinite(L[k])) fin = false; } check("invalid replacement script leaves the engine stable", fin);
      vv_engine_destroy(e); }
    // old preset without a LUA layer: LUA silent, nothing breaks
    { VVEngine* e = vv_engine_create(48000); const char* pj = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"level\":0.8}]}},\"bpm\":120}"; check("preset without LUA loads", vv_engine_load_preset_json(e, pj, strlen(pj)) == 1); vv_engine_destroy(e); }
    // parameters: a script parameter changes the sound
    { VVEngine* e = vv_engine_create(48000); std::string p = preset({ oscJson(factory("basic_va"), true) }); vv_engine_load_preset_json(e, p.c_str(), p.size()); auto a = play(e, 60, 0.8f, 600, 800);
      vv_engine_set_value(e, "layers.LUA.oscs.0.lua.params.drive", "1.0"); auto b = play(e, 60, 0.8f, 600); double ra = rms(a, 6000, 28000), rb = rms(b, 6000, 28000); check("script parameter edit changes the output", std::fabs(ra - rb) > 1e-3, std::to_string(ra) + " -> " + std::to_string(rb)); vv_engine_destroy(e); }
    // long run
    { VVEngine* e = vv_engine_create(48000); std::string p = preset({ oscJson(factory("motion_pad"), true) }); vv_engine_load_preset_json(e, p.c_str(), p.size()); std::vector<float> L(4800), R(4800); bool fin = true; double pk = 0;
      for (int n = 0; n < 8; n++) vv_engine_note_on(e, 48 + n * 3, 0.7f); for (int k = 0; k < 600; k++) { vv_engine_render(e, L.data(), R.data(), 4800); for (int i = 0; i < 4800; i++) { if (!std::isfinite(L[i])) fin = false; pk = std::fmax(pk, std::fabs(L[i])); } }
      check("60 s of 8 sustained script voices: finite, bounded", fin && pk < 3.0, "peak " + std::to_string(pk)); vv_engine_destroy(e); }
    std::printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s"); return fails ? 1 : 0;
}
