#include "vv_viz.h"
#include "vv/lx/viz_host.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
using namespace lx; using namespace lx::viz;
static const size_t kPool = 6u << 20;
struct VVViz { Engine* eng = nullptr; void* pool = nullptr; vv::viz::Analyzer an; vv::viz::Params pr; char json[16384]; bool active = false; };
static void esc(char*& o, const char* end, const char* s) { while (*s && o < end - 8) { char c = *s++; if (c == '"' || c == '\\') { *o++ = '\\'; *o++ = c; } else if ((unsigned char)c < 32) *o++ = ' '; else *o++ = c; } }
static void lit(char*& o, const char* end, const char* s) { while (*s && o < end - 2) *o++ = *s++; }
static void num(char*& o, const char* end, double v) { char b[40]; int n = snprintf(b, sizeof b, "%.9g", v); for (int i = 0; i < n && o < end - 2; i++) *o++ = b[i]; }
extern "C" {
VVViz* vv_viz_create(void) { VVViz* v = new (std::nothrow) VVViz(); if (!v) return nullptr; v->pool = malloc(kPool); v->eng = (Engine*)aligned_alloc(64, (sizeof(Engine) + 63) & ~(size_t)63); if (!v->pool || !v->eng) { free(v->pool); free(v->eng); delete v; return nullptr; }
    memset((void*)v->eng, 0, sizeof(Engine)); new (v->eng, PlaceTag{}) Engine(); v->eng->init(v->pool, kPool); return v; }
void vv_viz_destroy(VVViz* v) { if (!v) return; free(v->pool); free(v->eng); delete v; }
int vv_viz_load(VVViz* v, const char* src) {
    /* compile in a scratch engine first so a bad edit never disturbs the running script */
    VVViz* t = vv_viz_create(); if (!t) return 0; bool ok = t->eng->load(src, strlen(src)); if (!ok) { Engine::copyStr(v->eng->err, t->eng->err, 240); v->eng->errLine = t->eng->errLine; v->eng->errCol = t->eng->errCol; v->eng->hasError = true; vv_viz_destroy(t); return 0; } vv_viz_destroy(t);
    ok = v->eng->load(src, strlen(src)); if (ok) { v->pr.setup(*v->eng); v->eng->hasError = false; v->eng->err[0] = 0; } return ok ? 1 : 0; }
int vv_viz_check(const char* src, char* err, int errCap, int* line, int* col) { VVViz* t = vv_viz_create(); if (!t) return 0; VM& vm = t->eng->vm; vm.init(t->pool, kPool, 512 * 1024, 512); vm.chunk = "script"; Value fn; bool ok = vm.compile(src, strlen(src), &fn);
    if (!ok) { if (err && errCap > 0) { Engine::copyStr(err, vm.errmsg, errCap); } if (line) *line = vm.errLine; if (col) *col = vm.errCol; } vv_viz_destroy(t); return ok ? 1 : 0; }
const char* vv_viz_error(VVViz* v) { return v->eng->err; }
const char* vv_viz_meta_json(VVViz* v) { Engine& E = *v->eng; char* o = v->json; const char* end = o + sizeof(v->json);
    lit(o, end, "{\"name\":\""); esc(o, end, E.meta.name); lit(o, end, "\",\"category\":\""); esc(o, end, E.meta.category); lit(o, end, "\",\"author\":\""); esc(o, end, E.meta.author); lit(o, end, "\",\"desc\":\""); esc(o, end, E.meta.desc); lit(o, end, "\",\"shader\":\""); esc(o, end, E.meta.shader); lit(o, end, "\",\"params\":[");
    for (int i = 0; i < E.nparams; i++) { VParam& p = E.params[i]; if (i) lit(o, end, ","); lit(o, end, "{\"id\":\""); esc(o, end, p.id); lit(o, end, "\",\"label\":\""); esc(o, end, p.label); lit(o, end, "\",\"group\":\""); esc(o, end, p.group); lit(o, end, "\",\"unit\":\""); esc(o, end, p.unit);
        lit(o, end, "\",\"min\":"); num(o, end, p.mn); lit(o, end, ",\"max\":"); num(o, end, p.mx); lit(o, end, ",\"def\":"); num(o, end, p.def); lit(o, end, ",\"step\":"); num(o, end, p.step); lit(o, end, ",\"kind\":"); num(o, end, p.kind); lit(o, end, ",\"band\":"); num(o, end, p.band); lit(o, end, ",\"depth\":"); num(o, end, p.depth); lit(o, end, ",\"pol\":"); num(o, end, p.pol); lit(o, end, ",\"options\":[");
        for (int k = 0; k < p.nopts; k++) { if (k) lit(o, end, ","); lit(o, end, "\""); esc(o, end, p.opts[k]); lit(o, end, "\""); } lit(o, end, "]}"); }
    lit(o, end, "]}"); *o = 0; return v->json; }
int vv_viz_nparams(VVViz* v) { return v->eng->nparams; }
void vv_viz_set_param(VVViz* v, int i, float x) { if (i < 0 || i >= v->eng->nparams) return; auto& d = v->eng->params[i]; v->pr.p[i].base = x < d.mn ? d.mn : (x > d.mx ? d.mx : x); }
float vv_viz_get_param(VVViz* v, int i) { return (i < 0 || i >= v->eng->nparams) ? 0.f : v->pr.p[i].base; }
void vv_viz_set_assign(VVViz* v, int i, int en, int band, float depth, int pol, float smooth, float lo, float hi) { if (i < 0 || i >= v->eng->nparams) return; auto& a = v->pr.p[i].a; a.enabled = en != 0 && band >= 0; a.band = band; a.depth = depth; a.pol = pol; a.smooth = smooth; a.lo = lo; a.hi = hi; }
void vv_viz_get_assign(VVViz* v, int i, int* en, int* band, float* depth, int* pol, float* smooth, float* lo, float* hi) { if (i < 0 || i >= v->eng->nparams) return; auto& a = v->pr.p[i].a; if (en) *en = a.enabled; if (band) *band = a.band; if (depth) *depth = a.depth; if (pol) *pol = a.pol; if (smooth) *smooth = a.smooth; if (lo) *lo = a.lo; if (hi) *hi = a.hi; }
void vv_viz_reset_params(VVViz* v) { v->pr.reset(*v->eng); }
void vv_viz_randomise_values(VVViz* v) { v->pr.randomiseValues(*v->eng, true); }
void vv_viz_randomise_assign(VVViz* v) { v->pr.randomiseAssign(*v->eng); }
void vv_viz_clear_assign(VVViz* v) { for (int i = 0; i < v->eng->nparams; i++) v->pr.p[i].a.enabled = false; }
void vv_viz_get_cfg(VVViz* v, VVVizCfg* o) { auto& c = v->an.cfg; o->enabled = c.enabled; o->logResponse = c.logResponse; o->stereo = c.stereo; o->sensitivity = c.sensitivity; o->gain = c.gain; o->smoothing = c.smoothing; o->attack = c.attack; o->release = c.release; o->threshold = c.threshold; for (int i = 0; i < 6; i++) o->cross[i] = c.cross[i]; o->user[0] = c.user[0]; o->user[1] = c.user[1]; }
void vv_viz_set_cfg(VVViz* v, const VVVizCfg* o) { auto& c = v->an.cfg; c.enabled = o->enabled != 0; c.logResponse = o->logResponse != 0; c.stereo = o->stereo != 0; c.sensitivity = o->sensitivity; c.gain = o->gain; c.smoothing = o->smoothing; c.attack = o->attack; c.release = o->release; c.threshold = o->threshold; for (int i = 0; i < 6; i++) c.cross[i] = o->cross[i]; c.user[0] = o->user[0]; c.user[1] = o->user[1]; }
int vv_viz_frame(VVViz* v, float dt, const float* L, const float* R, int n, float sr, int w, int h, int quality, int fps, float scale, int demo) {
    Engine& E = *v->eng; if (dt < .001f) dt = .001f; if (dt > .1f) dt = .1f;
    if (demo) v->an.demo(dt); else if (L && R && n >= vv::viz::kN) v->an.update(L + (n - vv::viz::kN), R + (n - vv::viz::kN), dt, sr); else v->an.decay(dt);
    v->an.fill(E); v->pr.step(E, dt, v->an.out, true); E.host[0] = (float)quality; E.host[1] = (float)w; E.host[2] = (float)h; E.host[3] = (float)fps; E.host[4] = (float)E.frameNo; E.host[5] = scale; E.host[6] = dt;
    return E.frame(dt) ? 1 : 0; }
const float* vv_viz_uniforms(VVViz* v) { return v->eng->u; }
const float* vv_viz_post(VVViz* v) { return v->eng->post; }
const float* vv_viz_cmds(VVViz* v) { return (const float*)v->eng->cmds; }
int vv_viz_ncmd(VVViz* v) { return v->eng->ncmd; }
const float* vv_viz_bands(VVViz* v) { return v->an.out; }
float vv_viz_beat(VVViz* v) { return v->an.beat; }
long vv_viz_last_update(VVViz* v) { return v->eng->lastUpdate; }
long vv_viz_last_render(VVViz* v) { return v->eng->lastRender; }
}
