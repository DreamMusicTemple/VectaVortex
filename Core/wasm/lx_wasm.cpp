// lx_wasm.cpp - WebAssembly build of the LUA layer engine (freestanding: no libc, no libm, no exceptions).
// One instance hosts any number of script engines; the AudioWorklet and the main thread each instantiate their own copy.
#include "../include/vv/lx/lx_engine.hpp"
using namespace lx; using namespace lx::eng;
#define EXPORT(n) __attribute__((export_name(n)))

extern "C" unsigned char __heap_base;
static size_t g_top = 0, g_limit = 0; static void* g_free[40];
static void* wmalloc(size_t n) {
    n = (n + 15) & ~(size_t)15; int c = 4; size_t cs = 16; while (cs < n) { cs <<= 1; c++; }
    if (g_free[c]) { void* p = g_free[c]; g_free[c] = *(void**)p; return p; }
    if (!g_top) { g_top = ((size_t)&__heap_base + 15) & ~(size_t)15; g_limit = __builtin_wasm_memory_size(0) * 65536; }
    if (g_top + cs > g_limit) { size_t need = g_top + cs - g_limit; size_t pages = (need + 65535) / 65536; if (__builtin_wasm_memory_grow(0, pages) == (size_t)-1) return nullptr; g_limit = __builtin_wasm_memory_size(0) * 65536; }
    void* p = (void*)g_top; g_top += cs; return p;
}
static void wfree(void* p, size_t n) { if (!p) return; n = (n + 15) & ~(size_t)15; int c = 4; size_t cs = 16; while (cs < n) { cs <<= 1; c++; } *(void**)p = g_free[c]; g_free[c] = p; }
static float* allocF(size_t n) { float* p = (float*)wmalloc(n * 4); if (p) mzero(p, n * 4); return p; }
static void freeF(float* p, size_t n) { wfree(p, n * 4); }

struct Handle { Engine eng; float outL[kBlock], outR[kBlock]; char jsonbuf[6144]; };
static const size_t kPool = 4u << 20;

extern "C" {
EXPORT("lx_version") int lx_version() { return 1; }
EXPORT("lx_alloc") void* lx_alloc(int n) { return wmalloc((size_t)n); }
EXPORT("lx_free") void lx_free(void* p, int n) { wfree(p, (size_t)n); }
EXPORT("lx_create") Handle* lx_create(float sr) {
    Handle* h = (Handle*)wmalloc(sizeof(Handle)); void* pool = wmalloc(kPool); if (!h || !pool) return nullptr; mzero(h, sizeof(Handle));
    new (&h->eng, PlaceTag{}) Engine(); h->eng.init(pool, kPool, sr); h->eng.allocFloats = allocF; h->eng.freeFloats = freeF; return h;
}
EXPORT("lx_destroy") void lx_destroy(Handle* h) { if (!h) return; h->eng.clearScript(); wfree(h->eng.pool, kPool); wfree(h, sizeof(Handle)); }
EXPORT("lx_load") int lx_load(Handle* h, const char* src, int len, int inject) { return h->eng.load(src, (size_t)len, inject != 0) ? 1 : 0; }
EXPORT("lx_loaded") int lx_loaded(Handle* h) { return h->eng.loaded ? 1 : 0; }
EXPORT("lx_error") const char* lx_error(Handle* h) { return h->eng.err; }
EXPORT("lx_has_error") int lx_has_error(Handle* h) { return h->eng.hasError ? 1 : 0; }
EXPORT("lx_error_line") int lx_error_line(Handle* h) { return h->eng.errLine; }
EXPORT("lx_error_col") int lx_error_col(Handle* h) { return h->eng.errCol; }
EXPORT("lx_clear_error") void lx_clear_error(Handle* h) { h->eng.hasError = false; h->eng.err[0] = 0; }
static void jesc(char*& o, const char* end, const char* s) { while (*s && o < end - 8) { char c = *s++; if (c == '"' || c == '\\') { *o++ = '\\'; *o++ = c; } else if ((unsigned char)c < 32) { *o++ = ' '; } else *o++ = c; } }
static void jnum(char*& o, const char* end, double v) { char b[40]; int n = fmt_num(b, 40, v); for (int i = 0; i < n && o < end - 2; i++) *o++ = b[i]; }
static void jlit(char*& o, const char* end, const char* s) { while (*s && o < end - 2) *o++ = *s++; }
EXPORT("lx_meta_json") const char* lx_meta_json(Handle* h) {
    Engine& E = h->eng; char* o = h->jsonbuf; const char* end = o + sizeof(h->jsonbuf);
    jlit(o, end, "{\"name\":\""); jesc(o, end, E.meta.name); jlit(o, end, "\",\"category\":\""); jesc(o, end, E.meta.category); jlit(o, end, "\",\"author\":\""); jesc(o, end, E.meta.author); jlit(o, end, "\",\"desc\":\""); jesc(o, end, E.meta.desc); jlit(o, end, "\",\"tags\":\""); jesc(o, end, E.meta.tags);
    jlit(o, end, "\",\"version\":"); jnum(o, end, E.meta.version); jlit(o, end, ",\"poly\":"); jlit(o, end, E.meta.poly ? "true" : "false"); jlit(o, end, ",\"voices\":"); jnum(o, end, E.meta.maxVoices); jlit(o, end, ",\"params\":[");
    for (int i = 0; i < E.nparams; i++) { Param& p = E.params[i]; if (i) jlit(o, end, ","); jlit(o, end, "{\"id\":\""); jesc(o, end, p.id); jlit(o, end, "\",\"label\":\""); jesc(o, end, p.label); jlit(o, end, "\",\"min\":"); jnum(o, end, p.mn); jlit(o, end, ",\"max\":"); jnum(o, end, p.mx); jlit(o, end, ",\"def\":"); jnum(o, end, p.def); jlit(o, end, ",\"step\":"); jnum(o, end, p.step); jlit(o, end, ",\"kind\":"); jnum(o, end, p.kind); jlit(o, end, ",\"unit\":\""); jesc(o, end, p.unit); jlit(o, end, "\",\"group\":\""); jesc(o, end, p.group); jlit(o, end, "\",\"options\":["); for (int k = 0; k < p.nopts; k++) { if (k) jlit(o, end, ","); jlit(o, end, "\""); jesc(o, end, p.opts[k]); jlit(o, end, "\""); } jlit(o, end, "]}"); }
    jlit(o, end, "],\"tables\":["); for (int i = 0; i < E.ntables; i++) { if (i) jlit(o, end, ","); jlit(o, end, "{\"name\":\""); jesc(o, end, E.tables[i].name); jlit(o, end, "\",\"frames\":"); jnum(o, end, E.tables[i].frames); jlit(o, end, ",\"size\":"); jnum(o, end, E.tables[i].size); jlit(o, end, ",\"levels\":"); jnum(o, end, E.tables[i].levels); jlit(o, end, "}"); }
    jlit(o, end, "],\"setupInstr\":"); jnum(o, end, (double)E.totalInstr); jlit(o, end, ",\"tableKB\":"); jnum(o, end, (double)(E.tableBytes / 1024)); jlit(o, end, "}"); *o = 0; return h->jsonbuf;
}
EXPORT("lx_table_data") float* lx_table_data(Handle* h, int i) { return i >= 0 && i < h->eng.ntables ? h->eng.tables[i].data : nullptr; }
EXPORT("lx_table_floats") int lx_table_floats(Handle* h, int i) { return i >= 0 && i < h->eng.ntables ? h->eng.tables[i].frames * h->eng.tables[i].levels * h->eng.tables[i].size : 0; }
// injection of tables generated elsewhere (main thread -> worklet): create the table, then JS fills the returned buffer
EXPORT("lx_table_prepare") float* lx_table_prepare(Handle* h, const char* name, int frames, int size, int levels) { h->eng.inSetup = true; h->eng.vm.fail = F_NONE; WTable* t = h->eng.newTable(name, frames, size, levels); h->eng.inSetup = false; return t ? t->data : nullptr; }
EXPORT("lx_voice_on") int lx_voice_on(Handle* h, int note, float vel, int seed) { return h->eng.voiceOn(note, vel, (u32)seed); }
EXPORT("lx_voice_off") void lx_voice_off(Handle* h, int slot) { h->eng.voiceOff(slot); }
EXPORT("lx_voice_kill") void lx_voice_kill(Handle* h, int slot) { h->eng.voiceKill(slot); }
EXPORT("lx_set_host") void lx_set_host(Handle* h, int slot, float freq, float bpm, double beat, float playing, float pitchCents) { if (slot < 0 || slot >= kMaxVoices) return; HostIn& H = h->eng.host[slot]; H.freq = freq; H.bpm = bpm; H.beat = beat; H.playing = playing; H.pitchCents = pitchCents; }
EXPORT("lx_set_param") void lx_set_param(Handle* h, int slot, int i, float v) { if (slot < 0 || slot >= kMaxVoices || i < 0 || i >= kMaxParams) return; h->eng.host[slot].params[i] = v; }
EXPORT("lx_set_seed") void lx_set_seed(Handle* h, int seed) { h->eng.layerSeed = (u32)seed; }
EXPORT("lx_set_budget") void lx_set_budget(Handle* h, int control) { h->eng.budgetControl = control; }
EXPORT("lx_out_l") float* lx_out_l(Handle* h) { return h->outL; }
EXPORT("lx_out_r") float* lx_out_r(Handle* h) { return h->outR; }
EXPORT("lx_render") void lx_render(Handle* h, int slot, int n) { h->eng.lastBlockInstr = 0; h->eng.render(slot, h->outL, h->outR, n); if (h->eng.lastBlockInstr > h->eng.peakBlockInstr) h->eng.peakBlockInstr = h->eng.lastBlockInstr; }
EXPORT("lx_voice_faulted") int lx_voice_faulted(Handle* h, int slot) { return slot >= 0 && slot < kMaxVoices && h->eng.voices[slot].active && h->eng.voices[slot].faulted ? 1 : 0; }
EXPORT("lx_voice_active") int lx_voice_active(Handle* h, int slot) { return slot >= 0 && slot < kMaxVoices && h->eng.voices[slot].active ? 1 : 0; }
EXPORT("lx_active_voices") int lx_active_voices(Handle* h) { return h->eng.countActive(); }
EXPORT("lx_last_instr") int lx_last_instr(Handle* h) { return (int)h->eng.lastBlockInstr; }
EXPORT("lx_peak_instr") int lx_peak_instr(Handle* h) { int v = (int)h->eng.peakBlockInstr; h->eng.peakBlockInstr = 0; return v; }
EXPORT("lx_fault_count") int lx_fault_count(Handle* h) { return h->eng.faultCount; }
EXPORT("lx_mem_used") int lx_mem_used(Handle* h) { return (int)h->eng.vm.heap.used; }
EXPORT("lx_log") const char* lx_log(Handle* h) { h->eng.logDirty = false; return h->eng.logbuf; }
EXPORT("lx_log_dirty") int lx_log_dirty(Handle* h) { return h->eng.logDirty ? 1 : 0; }
EXPORT("lx_log_clear") void lx_log_clear(Handle* h) { h->eng.loglen = 0; h->eng.logbuf[0] = 0; h->eng.logDirty = false; }
EXPORT("lx_scope") float* lx_scope(Handle* h, int slot) { return slot >= 0 && slot < kMaxVoices ? h->eng.voices[slot].scope : nullptr; }
EXPORT("lx_scope_pos") int lx_scope_pos(Handle* h, int slot) { return slot >= 0 && slot < kMaxVoices ? h->eng.voices[slot].scopePos : 0; }
EXPORT("lx_voice_mod") float lx_voice_mod(Handle* h, int slot, int i) { if (slot < 0 || slot >= kMaxVoices || i < 0) return 0; Voice& v = h->eng.voices[slot]; if (i < 4) return v.modOut[i]; return 0; }
EXPORT("lx_voice_nmods") int lx_voice_nmods(Handle* h, int slot) { return slot >= 0 && slot < kMaxVoices ? h->eng.voices[slot].nmod : 0; }
EXPORT("lx_voice_modval") float lx_voice_modval(Handle* h, int slot, int i) { if (slot < 0 || slot >= kMaxVoices) return 0; Voice& v = h->eng.voices[slot]; return i >= 0 && i < v.nmod ? v.mod[i].out : 0; }
EXPORT("lx_voice_oscout") float lx_voice_oscout(Handle* h, int slot, int i) { if (slot < 0 || slot >= kMaxVoices) return 0; Voice& v = h->eng.voices[slot]; return i >= 0 && i < v.nosc ? v.osc[i].out : 0; }
EXPORT("lx_voice_nosc") int lx_voice_nosc(Handle* h, int slot) { return slot >= 0 && slot < kMaxVoices ? h->eng.voices[slot].nosc : 0; }
EXPORT("lx_voice_err") const char* lx_voice_err(Handle* h, int slot) { return slot >= 0 && slot < kMaxVoices ? h->eng.voices[slot].err : ""; }

// stand-alone syntax check (no setup run): returns 1 when the script parses; error text/line/col readable via lx_check_*
static Engine* g_checker = nullptr; static void* g_chkPool = nullptr;
EXPORT("lx_check") int lx_check(const char* src, int len) {
    if (!g_checker) { g_checker = (Engine*)wmalloc(sizeof(Engine)); g_chkPool = wmalloc(2u << 20); mzero(g_checker, sizeof(Engine)); new (g_checker, PlaceTag{}) Engine(); g_checker->init(g_chkPool, 2u << 20, 48000); }
    VM& vm = g_checker->vm; vm.init(g_chkPool, 2u << 20, 1536 * 1024, 512); vm.chunk = "script"; Value fn; bool ok = vm.compile(src, (size_t)len, &fn);
    if (!ok) { Engine::copyStr(g_checker->err, vm.errmsg, 200); g_checker->errLine = vm.errLine; g_checker->errCol = vm.errCol; } else g_checker->err[0] = 0; return ok ? 1 : 0;
}
EXPORT("lx_check_error") const char* lx_check_error() { return g_checker ? g_checker->err : ""; }
EXPORT("lx_check_line") int lx_check_line() { return g_checker ? g_checker->errLine : 0; }
EXPORT("lx_check_col") int lx_check_col() { return g_checker ? g_checker->errCol : 0; }
}
