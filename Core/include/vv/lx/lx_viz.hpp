// lx_viz.hpp - the LUA Visual engine ("Visual API v1").
//
//   script (Lua, sandboxed LX VM) --per frame--> uniforms[32] + post-fx[12] + a 2D command list  -->  GPU renderer (WebGL / Metal)
//
// Lua only does LOGIC. It never touches pixels and never runs on the audio thread: the host (UI thread) feeds the analysed audio
// bands and the effective parameter values in, calls update()/render() once per frame under an instruction budget, and reads the
// results back as plain float arrays. A script that errors or overruns is halted and reported; the host keeps drawing the last good
// uniforms. Everything (tables, command buffer) is preallocated; no allocation happens per frame.
#pragma once
#include "lx_lib.hpp"

namespace lx {
namespace viz {

constexpr int kMaxParams = 32, kNU = 32, kNPost = 12, kMaxCmds = 14000, kAudioN = 24, kHostN = 8, kSpecN = 32;
enum Band : int { B_FULL, B_SUB, B_BASS, B_LOWMID, B_MID, B_HIGHMID, B_HIGH, B_TREBLE, B_USER, kNBands };
enum Cmd : int { C_NONE, C_CLEAR, C_COLOR, C_BLEND, C_LINE, C_CIRCLE, C_RING, C_RECT, C_DOT, C_TRI, C_WIPE };
// post-fx slots (the host applies them in the final pass; a slot a script never sets keeps its neutral value)
enum Post : int { P_BLOOM, P_CHROMA, P_FEEDBACK, P_FBZOOM, P_FBROT, P_WARP, P_VIGNETTE, P_GRAIN, P_HUE, P_SAT, P_BRIGHT, P_CONTRAST };

struct VParam { char id[24], label[32], group[28], unit[10], opts[8][16]; float mn, mx, def, step; int kind, nopts, band, pol; float depth; };   // kind 0 slider 2 switch 3 menu
struct VMeta { char name[40], category[24], author[24], desc[160], shader[24]; int version = 1; };
struct Cmd8 { float op, a, b, c, d, e, f, g; };

struct Engine {
    VM vm; void* pool = nullptr; size_t poolBytes = 0; bool inited = false, loaded = false, inSetup = false, faulted = false, overflow = false;
    VMeta meta; VParam params[kMaxParams]; int nparams = 0;
    float pv[kMaxParams]; float audio[kAudioN]; float spec[kSpecN]; float host[kHostN]; float u[kNU]; float post[kNPost];
    Cmd8 cmds[kMaxCmds]; int ncmd = 0; int frameNo = 0;
    int refSpec = -1; int refInit = -1, refUpdate = -1, refRender = -1, refA = -1, refP = -1, refH = -1, refC = -1;
    Str* kA[kAudioN]; Str* kP[kMaxParams]; Str* kH[kHostN]; Str* kBandArr = nullptr; int refBandArr = -1;
    char err[240]; int errLine = 0, errCol = 0; bool hasError = false; char logbuf[2048]; int loglen = 0; bool logDirty = false;
    long budgetSetup = 20000000, budgetUpdate = 250000, budgetRender = 450000; long lastUpdate = 0, lastRender = 0, peakUpdate = 0, peakRender = 0; int faultCount = 0;
    static constexpr const char* kAudioNames[kAudioN] = { "full", "sub", "bass", "lowmid", "mid", "highmid", "high", "treble", "user", "level", "peak", "beat", "flux", "left", "right", "width", "t", "centroid", "peakhz", "", "", "", "", "" };
    static constexpr const char* kHostNames[kHostN] = { "quality", "w", "h", "fps", "frame", "scale", "dt", "seed" };

    static void copyStr(char* dst, const char* src, int cap) { int i = 0; while (src && src[i] && i < cap - 1) { dst[i] = src[i]; i++; } dst[i] = 0; }
    static bool strEq(const Str* s, const char* z) { if (!s) return false; size_t n = slen(z); return s->len == n && mcmp(s->data, z, n) == 0; }
    static Engine* of(VM& vm) { return (Engine*)vm.user; }
    static float gnum(VM& vm, Table* t, const char* k, float def) { Value v = vm.tgetS(t, k); if (v.t == T_NUM) return (float)v.n; if (v.t == T_BOOL) return v.b ? 1.f : 0.f; return def; }
    static void gstr(VM& vm, Table* t, const char* k, char* dst, int cap) { Value v = vm.tgetS(t, k); if (v.t == T_STR) copyStr(dst, v.s->data, cap); }
    void setErr(const char* m, int line, int col) { copyStr(err, m, 240); errLine = line; errCol = col; hasError = true; }
    void log(const char* s, size_t n) { for (size_t i = 0; i < n; i++) { if (loglen >= 2040) { for (int k = 0; k < 1024; k++) logbuf[k] = logbuf[k + 1024]; loglen -= 1024; } logbuf[loglen++] = s[i]; } logbuf[loglen] = 0; logDirty = true; }
    static void printHook(void* u, const char* s, size_t n) { ((Engine*)u)->log(s, n); }
    void neutral() {
        mzero(u, sizeof(u)); mzero(post, sizeof(post)); post[P_SAT] = 1; post[P_BRIGHT] = 1; post[P_CONTRAST] = 1; post[P_FBZOOM] = 1; ncmd = 0; overflow = false;
    }
    bool init(void* mem, size_t bytes) {
        pool = mem; poolBytes = bytes; inited = true; loaded = false; hasError = false; err[0] = 0; nparams = 0; loglen = 0; logbuf[0] = 0;
        mzero(&meta, sizeof(meta)); copyStr(meta.name, "Untitled", 40); copyStr(meta.category, "User Scripts", 24); copyStr(meta.shader, "none", 24); meta.version = 1;
        mzero(pv, sizeof(pv)); mzero(audio, sizeof(audio)); mzero(spec, sizeof(spec)); mzero(host, sizeof(host)); host[0] = 2; host[1] = 640; host[2] = 360; host[3] = 60; host[5] = 1; neutral(); return true;
    }
    void clearScript() {
        nparams = 0; loaded = false; faulted = false; hasError = false; err[0] = 0; refInit = refUpdate = refRender = refA = refP = refH = refC = -1; faultCount = 0; loglen = 0; logbuf[0] = 0; frameNo = 0;
        mzero(&meta, sizeof(meta)); copyStr(meta.name, "Untitled", 40); copyStr(meta.category, "User Scripts", 24); copyStr(meta.shader, "none", 24); meta.version = 1; neutral();
    }

    // ---------------------------------------------------------------- Lua API
    static int bandIndex(const Str* s) {
        static const char* nm[] = { "full", "sub", "bass", "lowmid", "mid", "highmid", "high", "treble", "user" };
        for (int i = 0; i < kNBands; i++) if (strEq(s, nm[i])) return i; return -1;
    }
    void push(Cmd8 c) { if (ncmd >= kMaxCmds) { overflow = true; return; } cmds[ncmd++] = c; }
    void registerApi();
    bool load(const char* src, size_t len);
    bool call(int fnRef, Value* args, int nargs, long budget, long& used);
    bool frame(float dt);        // update() then render(); false when the script is (now) halted
};

inline void Engine::registerApi() {
    using namespace lib;
    // ---- declarations (setup phase only)
    static NativeFn visual = [](VM& vm, Value* a, int n) -> int {
        Engine* E = of(vm); if (!E->inSetup) { vm.setError(0, 0, "visual{} can only be called at the top of the script (setup phase)"); return 0; } if (n < 1 || a[0].t != T_TAB) { argErr(vm, 0, "visual", "table", a, n); return 0; }
        Table* t = a[0].tb; gstr(vm, t, "name", E->meta.name, 40); gstr(vm, t, "category", E->meta.category, 24); gstr(vm, t, "author", E->meta.author, 24); gstr(vm, t, "desc", E->meta.desc, 160); gstr(vm, t, "shader", E->meta.shader, 24);
        E->meta.version = (int)gnum(vm, t, "version", 1); return 0; };
    static NativeFn param = [](VM& vm, Value* a, int n) -> int {
        Engine* E = of(vm); if (!E->inSetup) { vm.setError(0, 0, "param() can only be called at the top of the script (setup phase)"); return 0; } Str* id; if (!needStr(vm, a, n, 0, "param", id)) return 0; if (n < 2 || a[1].t != T_TAB) { argErr(vm, 1, "param", "table", a, n); return 0; }
        if (E->nparams >= kMaxParams) { vm.setError(0, 0, "too many parameters (max 32)"); return 0; }
        if (id->len < 1 || id->len > 22) { vm.setError(0, 0, "parameter id must be 1 to 22 characters"); return 0; }
        for (u32 i = 0; i < id->len; i++) { char c = id->data[i]; if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) { vm.setError(0, 0, "parameter id may only use letters, digits and _"); return 0; } }
        for (int i = 0; i < E->nparams; i++) if (strEq(id, E->params[i].id)) { vm.setError(0, 0, "this parameter id is already declared"); return 0; }
        VParam& p = E->params[E->nparams]; mzero(&p, sizeof(p)); copyStr(p.id, id->data, 24); Table* t = a[1].tb; copyStr(p.label, p.id, 32); gstr(vm, t, "label", p.label, 32); gstr(vm, t, "unit", p.unit, 10); copyStr(p.group, "Geometry", 28); gstr(vm, t, "group", p.group, 28);
        p.mn = gnum(vm, t, "min", 0); p.mx = gnum(vm, t, "max", 1); p.def = gnum(vm, t, "default", p.mn); p.step = gnum(vm, t, "step", 0); if (p.mx < p.mn) { float tm = p.mn; p.mn = p.mx; p.mx = tm; } if (p.def < p.mn) p.def = p.mn; if (p.def > p.mx) p.def = p.mx;
        Value kd = vm.tgetS(t, "kind"); p.kind = 0; if (kd.t == T_STR) { if (strEq(kd.s, "switch")) p.kind = 2; else if (strEq(kd.s, "menu")) p.kind = 3; }
        Value op = vm.tgetS(t, "options"); if (op.t == T_TAB) { for (u32 i = 1; i <= vm.tlen(op.tb) && p.nopts < 8; i++) { Value s = vm.tget(op.tb, Value::num(i)); if (s.t == T_STR) copyStr(p.opts[p.nopts++], s.s->data, 16); } if (p.nopts > 0) { p.kind = 3; p.mn = 0; p.mx = (float)(p.nopts - 1); p.step = 1; if (p.def > p.mx) p.def = p.mx; } }
        if (p.kind == 2) { p.mn = 0; p.mx = 1; p.step = 1; p.def = p.def > 0.5f ? 1.f : 0.f; }
        p.band = -1; p.pol = 1; p.depth = 0.5f; Value bd = vm.tgetS(t, "band"); if (bd.t == T_STR) p.band = bandIndex(bd.s); p.depth = gnum(vm, t, "depth", 0.5f);
        Value pl = vm.tgetS(t, "polarity"); if (pl.t == T_STR) { if (strEq(pl.s, "down")) p.pol = -1; else if (strEq(pl.s, "both")) p.pol = 0; }
        E->pv[E->nparams] = p.def; E->nparams++; return 0; };
    reg(vm, vm.globals, "visual", visual); reg(vm, vm.globals, "param", param);

    // ---- gfx: uniforms, post-fx
    Table* G = vm.newTable(0, 8); vm.tsetS(vm.globals, "gfx", Value::tab(G));
    static NativeFn uni = [](VM& vm, Value* a, int n) -> int { double i, v; if (!needNum(vm, a, n, 0, "uniform", i) || !needNum(vm, a, n, 1, "uniform", v)) return 0; int k = (int)i; if (k < 0 || k >= kNU) { vm.setError(0, 0, "gfx.uniform: index must be 0 to 31"); return 0; } of(vm)->u[k] = (float)v; return 0; };
    static NativeFn unis = [](VM& vm, Value* a, int n) -> int { double f; if (!needNum(vm, a, n, 0, "uniforms", f)) return 0; int k = (int)f; Engine* E = of(vm); for (int j = 1; j < n; j++) { int idx = k + j - 1; if (idx < 0 || idx >= kNU) { vm.setError(0, 0, "gfx.uniforms: index out of range (0 to 31)"); return 0; } double v; if (!needNum(vm, a, n, j, "uniforms", v)) return 0; E->u[idx] = (float)v; } return 0; };
    static NativeFn postf = [](VM& vm, Value* a, int n) -> int {
        Str* nm; double v; if (!needStr(vm, a, n, 0, "post", nm) || !needNum(vm, a, n, 1, "post", v)) return 0;
        static const char* names[] = { "bloom", "chroma", "feedback", "zoom", "rotate", "warp", "vignette", "grain", "hue", "saturation", "brightness", "contrast" };
        for (int i = 0; i < kNPost; i++) if (strEq(nm, names[i])) { of(vm)->post[i] = (float)v; return 0; }
        vm.setError(0, 0, "gfx.post: unknown effect (bloom, chroma, feedback, zoom, rotate, warp, vignette, grain, hue, saturation, brightness, contrast)"); return 0; };
    reg(vm, G, "uniform", uni); reg(vm, G, "uniforms", unis); reg(vm, G, "post", postf);

    // ---- canvas: records a command list (the GPU renderer draws it; coordinates are render-target pixels)
    Table* C = vm.newTable(0, 16); vm.tsetS(vm.globals, "canvas", Value::tab(C));
    #define CVN(i, nm) double nm = 0; if (i < n && !needNum(vm, a, n, i, "canvas", nm)) return 0;
    static NativeFn cclear = [](VM& vm, Value* a, int n) -> int { CVN(0, r) CVN(1, g) CVN(2, b) double al = 1; if (n > 3 && !needNum(vm, a, n, 3, "clear", al)) return 0; of(vm)->push({ (float)C_CLEAR, (float)r, (float)g, (float)b, (float)al, 0, 0, 0 }); return 0; };
    static NativeFn cwipe = [](VM& vm, Value*, int) -> int { of(vm)->push({ (float)C_WIPE, 0, 0, 0, 0, 0, 0, 0 }); return 0; };
    static NativeFn ccolor = [](VM& vm, Value* a, int n) -> int { CVN(0, r) CVN(1, g) CVN(2, b) double al = 1; if (n > 3 && !needNum(vm, a, n, 3, "color", al)) return 0; of(vm)->push({ (float)C_COLOR, (float)r, (float)g, (float)b, (float)al, 0, 0, 0 }); return 0; };
    static NativeFn cblend = [](VM& vm, Value* a, int n) -> int { double m = 0; if (n > 0) { if (a[0].t == T_STR) m = strEq(a[0].s, "add") ? 1 : 0; else if (!needNum(vm, a, n, 0, "blend", m)) return 0; } of(vm)->push({ (float)C_BLEND, (float)m, 0, 0, 0, 0, 0, 0 }); return 0; };
    static NativeFn cline = [](VM& vm, Value* a, int n) -> int { CVN(0, x1) CVN(1, y1) CVN(2, x2) CVN(3, y2) double w = 1; if (n > 4 && !needNum(vm, a, n, 4, "line", w)) return 0; of(vm)->push({ (float)C_LINE, (float)x1, (float)y1, (float)x2, (float)y2, (float)w, 0, 0 }); return 0; };
    static NativeFn ccirc = [](VM& vm, Value* a, int n) -> int { CVN(0, x) CVN(1, y) CVN(2, r) of(vm)->push({ (float)C_CIRCLE, (float)x, (float)y, (float)r, 0, 0, 0, 0 }); return 0; };
    static NativeFn cring = [](VM& vm, Value* a, int n) -> int { CVN(0, x) CVN(1, y) CVN(2, r) double w = 1; if (n > 3 && !needNum(vm, a, n, 3, "ring", w)) return 0; of(vm)->push({ (float)C_RING, (float)x, (float)y, (float)r, (float)w, 0, 0, 0 }); return 0; };
    static NativeFn crect = [](VM& vm, Value* a, int n) -> int { CVN(0, x) CVN(1, y) CVN(2, w) CVN(3, h) of(vm)->push({ (float)C_RECT, (float)x, (float)y, (float)w, (float)h, 0, 0, 0 }); return 0; };
    static NativeFn cdot = [](VM& vm, Value* a, int n) -> int { CVN(0, x) CVN(1, y) double s = 4; if (n > 2 && !needNum(vm, a, n, 2, "dot", s)) return 0; of(vm)->push({ (float)C_DOT, (float)x, (float)y, (float)s, 0, 0, 0, 0 }); return 0; };
    static NativeFn ctri = [](VM& vm, Value* a, int n) -> int { CVN(0, x1) CVN(1, y1) CVN(2, x2) CVN(3, y2) CVN(4, x3) CVN(5, y3) of(vm)->push({ (float)C_TRI, (float)x1, (float)y1, (float)x2, (float)y2, (float)x3, (float)y3, 0 }); return 0; };
    reg(vm, C, "clear", cclear); reg(vm, C, "wipe", cwipe); reg(vm, C, "color", ccolor); reg(vm, C, "blend", cblend); reg(vm, C, "line", cline); reg(vm, C, "circle", ccirc); reg(vm, C, "ring", cring); reg(vm, C, "rect", crect); reg(vm, C, "dot", cdot); reg(vm, C, "tri", ctri);

    // ---- colour
    Table* K = vm.newTable(0, 8); vm.tsetS(vm.globals, "color", Value::tab(K));
    static NativeFn hsv = [](VM& vm, Value* a, int n) -> int {
        double h, s, v; if (!needNum(vm, a, n, 0, "hsv", h) || !needNum(vm, a, n, 1, "hsv", s) || !needNum(vm, a, n, 2, "hsv", v)) return 0; h -= floor_(h); double h6 = h * 6, f = h6 - floor_(h6); int i = (int)floor_(h6) % 6; double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f)), r, g, b;
        switch (i) { case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break; case 2: r = p; g = v; b = t; break; case 3: r = p; g = q; b = v; break; case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = q; }
        vm.ret[0] = Value::num(r); vm.ret[1] = Value::num(g); vm.ret[2] = Value::num(b); return 3; };
    static NativeFn pal = [](VM& vm, Value* a, int n) -> int {   // cosine palette: color.pal(t [, shift [, contrast]])
        double t, sh = 0, ct = 0.5; if (!needNum(vm, a, n, 0, "pal", t) || !optNum(vm, a, n, 1, "pal", 0, sh) || !optNum(vm, a, n, 2, "pal", 0.5, ct)) return 0;
        vm.ret[0] = Value::num(0.5 + ct * cos_(6.283185307 * (t + sh))); vm.ret[1] = Value::num(0.5 + ct * cos_(6.283185307 * (t + sh + 0.33))); vm.ret[2] = Value::num(0.5 + ct * cos_(6.283185307 * (t + sh + 0.67))); return 3; };
    static NativeFn cmix = [](VM& vm, Value* a, int n) -> int {
        double v[7]; for (int i = 0; i < 7; i++) if (!needNum(vm, a, n, i, "mix", v[i])) return 0; for (int i = 0; i < 3; i++) vm.ret[i] = Value::num(v[i] + (v[3 + i] - v[i]) * v[6]); return 3; };
    reg(vm, K, "hsv", hsv); reg(vm, K, "pal", pal); reg(vm, K, "mix", cmix);

    // ---- viz: helpers
    Table* V = vm.newTable(0, 12); vm.tsetS(vm.globals, "viz", Value::tab(V));
    static NativeFn clampf = [](VM& vm, Value* a, int n) -> int { double x, lo, hi; if (!needNum(vm, a, n, 0, "clamp", x) || !needNum(vm, a, n, 1, "clamp", lo) || !needNum(vm, a, n, 2, "clamp", hi)) return 0; return rnum(vm, x < lo ? lo : (x > hi ? hi : x)); };
    static NativeFn lerpf = [](VM& vm, Value* a, int n) -> int { double x, y, t; if (!needNum(vm, a, n, 0, "lerp", x) || !needNum(vm, a, n, 1, "lerp", y) || !needNum(vm, a, n, 2, "lerp", t)) return 0; return rnum(vm, x + (y - x) * t); };
    static NativeFn smooth = [](VM& vm, Value* a, int n) -> int { double e0, e1, x; if (!needNum(vm, a, n, 0, "smoothstep", e0) || !needNum(vm, a, n, 1, "smoothstep", e1) || !needNum(vm, a, n, 2, "smoothstep", x)) return 0; double t = (x - e0) / (e1 - e0 == 0 ? 1e-9 : e1 - e0); t = t < 0 ? 0 : (t > 1 ? 1 : t); return rnum(vm, t * t * (3 - 2 * t)); };
    static NativeFn randf = [](VM& vm, Value* a, int n) -> int {
        double u = lib::rngUnit(vm); if (n == 0) return rnum(vm, u); double lo = 0, hi = 0; if (n == 1) { if (!needNum(vm, a, n, 0, "rand", hi)) return 0; } else if (!needNum(vm, a, n, 0, "rand", lo) || !needNum(vm, a, n, 1, "rand", hi)) return 0; return rnum(vm, lo + (hi - lo) * u); };
    static NativeFn seedf = [](VM& vm, Value* a, int n) -> int { double s; if (!needNum(vm, a, n, 0, "seed", s)) return 0; u64 x = (u64)(s < 0 ? -s : s) * 2654435761ull + 88172645463325252ull; vm.rngState = x ? x : 1; return 0; };
    static NativeFn noise = [](VM& vm, Value* a, int n) -> int {   // smooth value noise in [-1,1]: viz.noise(x [, y [, z]])
        double x = 0, y = 0, z = 0; if (!optNum(vm, a, n, 0, "noise", 0, x) || !optNum(vm, a, n, 1, "noise", 0, y) || !optNum(vm, a, n, 2, "noise", 0, z)) return 0;
        double fx = floor_(x), fy = floor_(y), fz = floor_(z); double tx = x - fx, ty = y - fy, tz = z - fz; tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty); tz = tz * tz * (3 - 2 * tz);
        auto h = [](int i, int j, int k) -> double { unsigned q = (unsigned)i * 374761393u + (unsigned)j * 668265263u + (unsigned)k * 2147483647u; q = (q ^ (q >> 13)) * 1274126177u; q ^= q >> 16; return (double)(q & 0xFFFFFF) / 8388608.0 - 1.0; };
        int ix = (int)fx, iy = (int)fy, iz = (int)fz; double r = 0;
        for (int k = 0; k < 2; k++) for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) r += h(ix + i, iy + j, iz + k) * (i ? tx : 1 - tx) * (j ? ty : 1 - ty) * (k ? tz : 1 - tz);
        return rnum(vm, r); };
    reg(vm, V, "clamp", clampf); reg(vm, V, "lerp", lerpf); reg(vm, V, "smoothstep", smooth); reg(vm, V, "rand", randf); reg(vm, V, "seed", seedf); reg(vm, V, "randi", [](VM& vm, Value* a, int n) -> int { return lib::m_random(vm, a, n); }); reg(vm, V, "noise", noise); reg(vm, V, "log", b_print);
    vm.tsetS(V, "api", Value::num(1));
}

inline bool Engine::load(const char* src, size_t len) {
    if (!inited) return false; clearScript();
    if (!vm.init(pool, poolBytes, 512 * 1024, 4000)) { setErr("script engine could not allocate memory", 0, 0); return false; }
    vm.user = this; vm.maxDepth = 50; vm.chunk = "script"; vm.printHook = printHook; lib::ls().print = printHook; lib::ls().user = this;
    lib::openLibs(vm); registerApi();
    for (int i = 0; i < kAudioN; i++) kA[i] = kAudioNames[i][0] ? vm.internPerm(kAudioNames[i], slen(kAudioNames[i])) : nullptr;
    for (int i = 0; i < kHostN; i++) kH[i] = vm.internPerm(kHostNames[i], slen(kHostNames[i]));
    Table* A = vm.newTable(8, 28); Table* SP = vm.newTable(kSpecN, 0); if (SP) { for (int i = 1; i <= kSpecN; i++) vm.tsetRaw(SP, Value::num((double)i), Value::num(0)); refSpec = vm.ref(Value::tab(SP)); vm.tsetS(A, "spec", Value::tab(SP)); } Table* H = vm.newTable(0, 12); Table* Cv = vm.newTable(0, 16);
    if (!A || !H || !Cv) { setErr("out of memory", 0, 0); return false; }
    refA = vm.ref(Value::tab(A)); refH = vm.ref(Value::tab(H)); refC = vm.ref(vm.tgetS(vm.globals, "canvas")); vm.tsetS(vm.globals, "audio", Value::tab(A)); vm.tsetS(vm.globals, "host", Value::tab(H));
    vm.chunk = "script"; Value fn; vm.budget = vm.budgetStart = budgetSetup; inSetup = true;
    if (!vm.compile(src, len, &fn)) { setErr(vm.errmsg, vm.errLine, vm.errCol); inSetup = false; return false; }
    vm.callv(fn, nullptr, 0); inSetup = false;
    if (vm.fail) { setErr(vm.errmsg, vm.errLine, vm.errCol); return false; }
    Value fi = vm.tgetS(vm.globals, "init"), fu = vm.tgetS(vm.globals, "update"), fr = vm.tgetS(vm.globals, "render");
    if (fu.t != T_FUN && fr.t != T_FUN) { setErr("the script must define update(dt, audio, p) and/or render(canvas, w, h, p)", 0, 0); return false; }
    if (fi.t == T_FUN) refInit = vm.ref(fi); if (fu.t == T_FUN) refUpdate = vm.ref(fu); if (fr.t == T_FUN) refRender = vm.ref(fr);
    // the parameter table handed to update()/render(): one pre-interned key per declared parameter
    Table* P = vm.newTable(0, (u32)(nparams + 4)); if (!P) { setErr("out of memory", 0, 0); return false; } refP = vm.ref(Value::tab(P)); vm.tsetS(vm.globals, "params", Value::tab(P));
    for (int i = 0; i < nparams; i++) { kP[i] = vm.internPerm(params[i].id, slen(params[i].id)); vm.tsetRaw(P, Value::str(kP[i]), Value::num(pv[i])); }
    if (vm.heap.failed) { setErr("script ran out of memory during setup", 0, 0); return false; }
    vm.gcRun(); loaded = true; faulted = false;
    if (refInit >= 0) { long used; Value none; if (!call(refInit, &none, 0, budgetUpdate * 8, used)) return false; }
    return true;
}

inline bool Engine::call(int fnRef, Value* args, int nargs, long budget, long& used) {
    Value fn = vm.getref(fnRef); vm.fail = F_NONE; vm.sp = vm.stack; vm.depth = 0; vm.budget = vm.budgetStart = budget;
    vm.callv(fn, args, nargs); used = vm.instrUsed();
    if (vm.fail) { faulted = true; setErr(vm.errmsg, vm.errLine, vm.errCol); faultCount++; vm.fail = F_NONE; vm.sp = vm.stack; vm.depth = 0; vm.errval = Value(); return false; }
    vm.gcMaybe(); return true;
}

inline bool Engine::frame(float dt) {
    if (!loaded || faulted) return false;
    Table* A = vm.getref(refA).tb; Table* P = vm.getref(refP).tb; Table* H = vm.getref(refH).tb;
    for (int i = 0; i < kAudioN; i++) if (kA[i]) vm.tsetRaw(A, Value::str(kA[i]), Value::num(audio[i]));
    for (int i = 0; i < nparams; i++) vm.tsetRaw(P, Value::str(kP[i]), Value::num(pv[i]));
    if (refSpec >= 0) { Table* SP = vm.getref(refSpec).tb; for (int i = 0; i < kSpecN; i++) vm.tsetRaw(SP, Value::num((double)(i + 1)), Value::num(spec[i])); }
    host[6] = dt; host[4] = (float)frameNo; for (int i = 0; i < kHostN; i++) vm.tsetRaw(H, Value::str(kH[i]), Value::num(host[i]));
    ncmd = 0; overflow = false; long used = 0;
    if (refUpdate >= 0) { Value args[3] = { Value::num(dt), Value::tab(A), Value::tab(P) }; if (!call(refUpdate, args, 3, budgetUpdate, used)) return false; lastUpdate = used; if (used > peakUpdate) peakUpdate = used; }
    if (refRender >= 0) { Value args[4] = { vm.getref(refC), Value::num(host[1]), Value::num(host[2]), Value::tab(P) }; if (!call(refRender, args, 4, budgetRender, used)) return false; lastRender = used; if (used > peakRender) peakRender = used; }
    frameNo++; return true;
}

}  // namespace viz
}  // namespace lx
