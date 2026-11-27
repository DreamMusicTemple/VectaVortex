// lxv_wasm.cpp - WebAssembly build of the LUA Visual engine (main-thread instance; the audio engine has its own module) (freestanding: no libc, no libm, no exceptions).
// One instance hosts any number of script engines; the AudioWorklet and the main thread each instantiate their own copy.
#include "../include/vv/lx/lx_viz.hpp"
using namespace lx; using namespace lx::viz;
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


struct Handle { Engine eng; char json[16384]; };
static const size_t kPool = 6u << 20;
extern "C" {
EXPORT("lxv_version") int lxv_version() { return 1; }
EXPORT("lxv_alloc") void* lxv_alloc(int n) { return wmalloc((size_t)n); }
EXPORT("lxv_free") void lxv_free(void* p, int n) { wfree(p, (size_t)n); }
EXPORT("lxv_create") Handle* lxv_create() { Handle* h = (Handle*)wmalloc(sizeof(Handle)); void* pool = wmalloc(kPool); if (!h || !pool) return nullptr; mzero(h, sizeof(Handle)); new (&h->eng, PlaceTag{}) Engine(); h->eng.init(pool, kPool); return h; }
EXPORT("lxv_destroy") void lxv_destroy(Handle* h) { if (!h) return; wfree(h->eng.pool, kPool); wfree(h, sizeof(Handle)); }
EXPORT("lxv_load") int lxv_load(Handle* h, const char* src, int len) { return h->eng.load(src, (size_t)len) ? 1 : 0; }
EXPORT("lxv_loaded") int lxv_loaded(Handle* h) { return h->eng.loaded ? 1 : 0; }
EXPORT("lxv_faulted") int lxv_faulted(Handle* h) { return h->eng.faulted ? 1 : 0; }
EXPORT("lxv_error") const char* lxv_error(Handle* h) { return h->eng.err; }
EXPORT("lxv_has_error") int lxv_has_error(Handle* h) { return h->eng.hasError ? 1 : 0; }
EXPORT("lxv_error_line") int lxv_error_line(Handle* h) { return h->eng.errLine; }
EXPORT("lxv_error_col") int lxv_error_col(Handle* h) { return h->eng.errCol; }
EXPORT("lxv_clear_fault") void lxv_clear_fault(Handle* h) { h->eng.faulted = false; h->eng.hasError = false; h->eng.err[0] = 0; }
EXPORT("lxv_audio") float* lxv_audio(Handle* h) { return h->eng.audio; }
EXPORT("lxv_spec") float* lxv_spec(Handle* h) { return h->eng.spec; }
EXPORT("lxv_params") float* lxv_params(Handle* h) { return h->eng.pv; }
EXPORT("lxv_host") float* lxv_host(Handle* h) { return h->eng.host; }
EXPORT("lxv_uniforms") float* lxv_uniforms(Handle* h) { return h->eng.u; }
EXPORT("lxv_post") float* lxv_post(Handle* h) { return h->eng.post; }
EXPORT("lxv_cmds") float* lxv_cmds(Handle* h) { return (float*)h->eng.cmds; }
EXPORT("lxv_ncmd") int lxv_ncmd(Handle* h) { return h->eng.ncmd; }
EXPORT("lxv_overflow") int lxv_overflow(Handle* h) { return h->eng.overflow ? 1 : 0; }
EXPORT("lxv_frame") int lxv_frame(Handle* h, float dt) { return h->eng.frame(dt) ? 1 : 0; }
EXPORT("lxv_nparams") int lxv_nparams(Handle* h) { return h->eng.nparams; }
EXPORT("lxv_last_update") int lxv_last_update(Handle* h) { return (int)h->eng.lastUpdate; }
EXPORT("lxv_last_render") int lxv_last_render(Handle* h) { return (int)h->eng.lastRender; }
EXPORT("lxv_peak_update") int lxv_peak_update(Handle* h) { int v = (int)h->eng.peakUpdate; h->eng.peakUpdate = 0; return v; }
EXPORT("lxv_peak_render") int lxv_peak_render(Handle* h) { int v = (int)h->eng.peakRender; h->eng.peakRender = 0; return v; }
EXPORT("lxv_mem_used") int lxv_mem_used(Handle* h) { return (int)h->eng.vm.heap.used; }
EXPORT("lxv_log") const char* lxv_log(Handle* h) { h->eng.logDirty = false; return h->eng.logbuf; }
EXPORT("lxv_log_dirty") int lxv_log_dirty(Handle* h) { return h->eng.logDirty ? 1 : 0; }
static void jesc(char*& o, const char* end, const char* s) { while (*s && o < end - 8) { char c = *s++; if (c == '"' || c == '\\') { *o++ = '\\'; *o++ = c; } else if ((unsigned char)c < 32) { *o++ = ' '; } else *o++ = c; } }
static void jnum(char*& o, const char* end, double v) { char b[40]; int n = fmt_num(b, 40, v); for (int i = 0; i < n && o < end - 2; i++) *o++ = b[i]; }
static void jlit(char*& o, const char* end, const char* s) { while (*s && o < end - 2) *o++ = *s++; }
EXPORT("lxv_meta_json") const char* lxv_meta_json(Handle* h) {
    Engine& E = h->eng; char* o = h->json; const char* end = o + sizeof(h->json);
    jlit(o, end, "{\"name\":\""); jesc(o, end, E.meta.name); jlit(o, end, "\",\"category\":\""); jesc(o, end, E.meta.category); jlit(o, end, "\",\"author\":\""); jesc(o, end, E.meta.author);
    jlit(o, end, "\",\"desc\":\""); jesc(o, end, E.meta.desc); jlit(o, end, "\",\"shader\":\""); jesc(o, end, E.meta.shader); jlit(o, end, "\",\"version\":"); jnum(o, end, E.meta.version);
    jlit(o, end, ",\"hasUpdate\":"); jlit(o, end, E.refUpdate >= 0 ? "true" : "false"); jlit(o, end, ",\"hasRender\":"); jlit(o, end, E.refRender >= 0 ? "true" : "false"); jlit(o, end, ",\"params\":[");
    for (int i = 0; i < E.nparams; i++) { VParam& p = E.params[i]; if (i) jlit(o, end, ","); jlit(o, end, "{\"id\":\""); jesc(o, end, p.id); jlit(o, end, "\",\"label\":\""); jesc(o, end, p.label); jlit(o, end, "\",\"group\":\""); jesc(o, end, p.group); jlit(o, end, "\",\"unit\":\""); jesc(o, end, p.unit);
        jlit(o, end, "\",\"min\":"); jnum(o, end, p.mn); jlit(o, end, ",\"max\":"); jnum(o, end, p.mx); jlit(o, end, ",\"def\":"); jnum(o, end, p.def); jlit(o, end, ",\"step\":"); jnum(o, end, p.step); jlit(o, end, ",\"kind\":"); jnum(o, end, p.kind);
        jlit(o, end, ",\"band\":"); jnum(o, end, p.band); jlit(o, end, ",\"depth\":"); jnum(o, end, p.depth); jlit(o, end, ",\"pol\":"); jnum(o, end, p.pol); jlit(o, end, ",\"options\":[");
        for (int k = 0; k < p.nopts; k++) { if (k) jlit(o, end, ","); jlit(o, end, "\""); jesc(o, end, p.opts[k]); jlit(o, end, "\""); } jlit(o, end, "]}"); }
    jlit(o, end, "]}"); *o = 0; return h->json;
}
static Engine* g_checker = nullptr; static void* g_chkPool = nullptr;
EXPORT("lxv_check") int lxv_check(const char* src, int len) {
    if (!g_checker) { g_checker = (Engine*)wmalloc(sizeof(Engine)); g_chkPool = wmalloc(2u << 20); mzero(g_checker, sizeof(Engine)); new (g_checker, PlaceTag{}) Engine(); g_checker->init(g_chkPool, 2u << 20); }
    VM& vm = g_checker->vm; vm.init(g_chkPool, 2u << 20, 512 * 1024, 512); vm.chunk = "script"; Value fn; bool ok = vm.compile(src, (size_t)len, &fn);
    if (!ok) { Engine::copyStr(g_checker->err, vm.errmsg, 240); g_checker->errLine = vm.errLine; g_checker->errCol = vm.errCol; } else g_checker->err[0] = 0; return ok ? 1 : 0;
}
EXPORT("lxv_check_error") const char* lxv_check_error() { return g_checker ? g_checker->err : ""; }
EXPORT("lxv_check_line") int lxv_check_line() { return g_checker ? g_checker->errLine : 0; }
EXPORT("lxv_check_col") int lxv_check_col() { return g_checker ? g_checker->errCol : 0; }
}
