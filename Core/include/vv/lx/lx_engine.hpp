// lx_engine.hpp - the LUA layer's synthesis engine ("API v1").
//
//   script (Lua, sandboxed VM)  --control-rate-->  oscillator bank / modulators (native DSP, preallocated)  -->  stereo out
//
// Lua never runs per sample. A script declares an instrument (name, parameters, wavetables) once at load time, and then
// steers a native oscillator bank once per audio block: init(v) when a note starts, control(v) every block, release(v) at
// note-off. Wavetables are generated at load time (never on the audio thread) and band-limited into mip levels. Every call
// into Lua runs under an instruction budget; a script that fails or overruns is muted and reported, nothing else is affected.
#pragma once
#include "lx_lib.hpp"

namespace lx {
namespace eng {

constexpr int kMaxVoices = 32, kMaxOsc = 16, kMaxMod = 16, kMaxParams = 16, kMaxTables = 48, kMaxUni = 7, kBlock = 128, kScope = 512;
constexpr int kSineN = 4096;

enum Wave : int { W_SINE, W_TRI, W_SAW, W_RSAW, W_SQUARE, W_PULSE, W_NOISE, W_TABLE0 = 16 };
enum ModKind : int { M_LFO, M_ENV, M_STEPS, M_CHAOS, M_FOLLOW, M_NOISE };

struct WTable {                 // a band-limited wavetable bank: frames x levels x size floats
    char name[24]; int frames = 0, size = 0, levels = 0; float* data = nullptr; bool used = false;
    const float* at(int frame, int level) const { return data + ((size_t)frame * levels + level) * (size_t)size; }
};
struct Param { char id[24], label[32], unit[10], group[16], opts[8][16]; float mn, mx, def, step; int kind, nopts; };   // kind 0 knob 1 slider 2 switch 3 menu 4 number
struct Meta { char name[40], category[24], author[24], desc[120], tags[64]; int version = 1, poly = 1, maxVoices = 8, api = 1; };

struct Osc {
    double ph[kMaxUni]; float prevLvl = 0, prevRatio = 1, prevPw = 0.5f, prevPos = 0, prevFm = 0, prevPm = 0, prevRing = 0, prevAm = 0, prevFold = 0, prevDrive = 0, prevPan = 0;
    float out = 0; bool wrapped = false; bool fresh = true; int wave = W_SAW; const Str* waveKey = nullptr;
};
struct Mod {
    int kind = M_LFO, shape = 0; double ph = 0; float rate = 1, out = 0, a = 0.01f, d = 0.1f, s = 0.7f, r = 0.2f, glide = 0, amount = 1, depth = 1; double state = 0, x = 0.1, y = 0, z = 0; bool bipolar = true, sync = false; float syncMul = 1; int nsteps = 0; float steps[32]; int step = 0;
    u64 rng = 1; float target = 0; int srcOsc = 0; float env = 0; int stage = 0; float attackPos = 0; int syncDiv = 0; float pinkB[3];
};
struct HostIn { float freq; float bpm; float playing; float gate; double beat; float params[kMaxParams]; float pitchCents; };

struct Voice {
    bool active = false, gate = false, faulted = false, started = false; int note = 60; float vel = 1; double t = 0; u32 seed = 1; u64 rng = 1; int vref = -1, pref = -1;
    int nosc = 0, oscRef[kMaxOsc]; Osc osc[kMaxOsc]; int nmod = 0, modRef[kMaxMod]; Mod mod[kMaxMod];
    float level = 1, pan = 0, prevLevel = 1, prevPan = 0, peak = 0; float modOut[4] = { 0, 0, 0, 0 }; float lastParams[kMaxParams]; bool paramsInit = false;
    float scope[kScope]; int scopePos = 0; long instrBlock = 0; char err[160]; int errLine = 0; int voiceId = 0;
};

struct Engine {
    VM vm; void* pool = nullptr; size_t poolBytes = 0; bool inited = false, loaded = false, inSetup = false, inject = false;
    Meta meta; Param params[kMaxParams]; int nparams = 0; WTable tables[kMaxTables]; int ntables = 0; size_t tableBytes = 0, tableCap = 24u << 20;
    int refInit = -1, refControl = -1, refRelease = -1; Voice voices[kMaxVoices]; HostIn host[kMaxVoices];
    float sr = 48000; char err[200]; int errLine = 0, errCol = 0; bool hasError = false; char logbuf[2048]; int loglen = 0; bool logDirty = false;
    long budgetControl = 15000, budgetSetup = 60000000; long lastBlockInstr = 0, peakBlockInstr = 0; int activeVoices = 0; u32 layerSeed = 0;
    float sine[kSineN + 1]; int injectIdx = 0; int faultCount = 0; long totalInstr = 0;
    // pre-interned key strings (pointer compare in the hot read path)
    struct Keys { Str *wave, *level, *pan, *ratio, *detune, *semi, *pw, *phase, *pos, *fm, *fmsrc, *pm, *pmsrc, *ring, *ringsrc, *am, *amsrc, *sync, *syncsrc, *fold, *drive, *unison, *spread, *width, *on, *out, *freq, *kind, *shape, *rate, *a, *d, *s, *r, *depth, *bipolar, *amount, *src, *glide, *color, *seed, *div;
        Str *note, *vel, *gate, *t, *dt, *bpm, *beat, *playing, *sr, *index, *p, *id, *mod1, *mod2, *mod3, *mod4, *done, *vpitch, *vlevel, *vpan, *steps, *step, *x, *y; } K;
    float E_sine(int k, float fr) const { k &= (kSineN - 1); return sine[k] + (sine[k + 1] - sine[k]) * fr; }
    float sinf_(float a) const { float c = a * 0.15915494309f; c -= (float)floor_(c); float x = c * kSineN; int k = (int)x; return E_sine(k, x - k); }
    float cosf_(float a) const { return sinf_(a + 1.57079632679f); }

    // ------------------------------------------------------------ lifecycle
    bool init(void* mem, size_t bytes, float sampleRate) {
        pool = mem; poolBytes = bytes; sr = sampleRate; inited = false; loaded = false; hasError = false; err[0] = 0; ntables = 0; nparams = 0; tableBytes = 0; loglen = 0;
        for (int i = 0; i < kSineN + 1; i++) sine[i] = (float)sin_(2.0 * kPi * i / kSineN);
        for (int i = 0; i < kMaxVoices; i++) voices[i].active = false;
        mzero(&meta, sizeof(meta)); copyStr(meta.name, "Untitled", 40); copyStr(meta.category, "User Scripts", 24); meta.poly = 1; meta.maxVoices = 8; meta.version = 1;
        refInit = refControl = refRelease = -1; inited = true; return true;
    }
    static void copyStr(char* dst, const char* src, int cap) { int i = 0; while (src && src[i] && i < cap - 1) { dst[i] = src[i]; i++; } dst[i] = 0; }
    static bool strEq(const Str* s, const char* z) { if (!s) return false; size_t n = slen(z); return s->len == n && mcmp(s->data, z, n) == 0; }
    void setErr(const char* m, int line, int col) { copyStr(err, m, 200); errLine = line; errCol = col; hasError = true; }
    void log(const char* s, size_t n) { for (size_t i = 0; i < n; i++) { if (loglen >= 2040) { for (int k = 0; k < 1024; k++) logbuf[k] = logbuf[k + 1024]; loglen -= 1024; } logbuf[loglen++] = s[i]; } logbuf[loglen] = 0; logDirty = true; }
    ~Engine() {}

    // ------------------------------------------------------------ Lua API plumbing
    static Engine* of(VM& vm) { return (Engine*)vm.user; }
    static bool tnum(VM& vm, Table* t, Str* k, float& out) { Value v = vm.tgetStr(t, k); if (v.t == T_NUM) { out = (float)v.n; return true; } if (v.t == T_BOOL) { out = v.b ? 1.f : 0.f; return true; } return false; }
    static float gnum(VM& vm, Table* t, Str* k, float def) { Value v = vm.tgetStr(t, k); if (v.t == T_NUM) return (float)v.n; if (v.t == T_BOOL) return v.b ? 1.f : 0.f; return def; }
    static int gint(VM& vm, Table* t, Str* k, int def) { Value v = vm.tgetStr(t, k); return v.t == T_NUM ? (int)v.n : def; }
    static void gstr(VM& vm, Table* t, Str* k, char* dst, int cap) { Value v = vm.tgetStr(t, k); if (v.t == T_STR) copyStr(dst, v.s->data, cap); }
    Str* istr(const char* s) { return vm.internPerm(s, slen(s)); }

    void makeKeys() {
        #define KS(f) K.f = istr(#f)
        KS(wave); KS(level); KS(pan); KS(ratio); KS(detune); KS(semi); KS(pw); KS(phase); KS(pos); KS(fm); KS(fmsrc); KS(pm); KS(pmsrc); KS(ring); KS(ringsrc); KS(am); KS(amsrc); KS(sync); KS(syncsrc); KS(fold); KS(drive); KS(unison); KS(spread); KS(width); KS(on); KS(out); KS(freq); KS(kind); KS(shape); KS(rate); KS(a); KS(d); KS(s); KS(r); KS(depth); KS(bipolar); KS(amount); KS(src); KS(glide); KS(color); KS(seed); KS(div);
        KS(note); KS(vel); KS(gate); KS(t); KS(dt); KS(bpm); KS(beat); KS(playing); KS(sr); KS(index); KS(p); KS(id); KS(mod1); KS(mod2); KS(mod3); KS(mod4); KS(done); KS(steps); KS(step); KS(x); KS(y);
        K.vpitch = istr("pitch"); K.vlevel = K.level; K.vpan = K.pan; K.step = istr("step");
        #undef KS
    }
    WTable* findTable(const char* n) { for (int i = 0; i < ntables; i++) if (slen(tables[i].name) == slen(n) && mcmp(tables[i].name, n, slen(n)) == 0) return &tables[i]; return nullptr; }
    WTable* findTableS(const Str* s) { for (int i = 0; i < ntables; i++) if (slen(tables[i].name) == s->len && mcmp(tables[i].name, s->data, s->len) == 0) return &tables[i]; return nullptr; }

    // ---- table storage (float arena owned by the engine; host supplies the allocator through allocFloats)
    float* (*allocFloats)(size_t n) = nullptr; void (*freeFloats)(float*, size_t n) = nullptr;
    WTable* newTable(const char* name, int frames, int size, int levels) {
        WTable* ex = findTable(name); if (ex) { setErr("a wavetable with this name already exists", 0, 0); return nullptr; }
        if (ntables >= kMaxTables) { vm.setError(0, 0, "too many wavetables (max 48)"); return nullptr; }
        size_t n = (size_t)frames * levels * size; if (tableBytes + n * 4 > tableCap) { vm.setError(0, 0, "wavetable memory limit reached (reduce frames or size)"); return nullptr; }
        float* d = allocFloats ? allocFloats(n) : nullptr; if (!d) { vm.setError(0, 0, "out of wavetable memory"); return nullptr; }
        WTable* t = &tables[ntables++]; copyStr(t->name, name, 24); t->frames = frames; t->size = size; t->levels = levels; t->data = d; tableBytes += n * 4; return t;
    }
    // in-place radix-2 FFT (N power of two)
    static void fft(double* re, double* im, int N, bool inv) {
        for (int i = 1, j = 0; i < N; i++) { int bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
        for (int len = 2; len <= N; len <<= 1) { double ang = 2 * kPi / len * (inv ? 1 : -1); double wr = cos_(ang), wi = sin_(ang); for (int i = 0; i < N; i += len) { double cr = 1, ci = 0; for (int j = 0; j < len / 2; j++) { double ur = re[i + j], ui = im[i + j]; double vr = re[i + j + len / 2] * cr - im[i + j + len / 2] * ci, vi = re[i + j + len / 2] * ci + im[i + j + len / 2] * cr; re[i + j] = ur + vr; im[i + j] = ui + vi; re[i + j + len / 2] = ur - vr; im[i + j + len / 2] = ui - vi; double ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr; } } }
        if (inv) for (int i = 0; i < N; i++) { re[i] /= N; im[i] /= N; }
    }
    // builds the band-limited mip levels of one frame (raw: size samples in)
    void buildMips(WTable* t, int frame, const double* raw, double* re, double* im) {
        int N = t->size; for (int i = 0; i < N; i++) { re[i] = raw[i]; im[i] = 0; } fft(re, im, N, false);
        for (int L = 0; L < t->levels; L++) {
            int maxH = (N / 2) >> L; if (maxH < 1) maxH = 1; double* tr = (double*)heapScratch(N * 2 * 8); double* ti = tr + N;
            for (int i = 0; i < N; i++) { tr[i] = 0; ti[i] = 0; } tr[0] = re[0] * (L == 0 ? 1 : 1); ti[0] = 0;
            for (int h = 1; h <= maxH && h < N / 2; h++) { double fade = 1; if (L > 0 && h > maxH * 0.9) fade = (maxH - h) / (maxH * 0.1 + 1e-9); tr[h] = re[h] * fade; ti[h] = im[h] * fade; tr[N - h] = re[N - h] * fade; ti[N - h] = im[N - h] * fade; }
            if (L == 0) { for (int i = 0; i < N; i++) { tr[i] = re[i]; ti[i] = im[i]; } }
            fft(tr, ti, N, true); float* out = (float*)t->at(frame, L); for (int i = 0; i < N; i++) out[i] = (float)tr[i];
        }
    }
    double scratchBuf[4096 * 2 + 16]; void* heapScratch(size_t) { return scratchBuf; }
    double rawBuf[4096], reBuf[4096], imBuf[4096];

    // ------------------------------------------------------------ script loading
    void clearScript() {
        for (int i = 0; i < kMaxVoices; i++) voices[i].active = false;
        for (int i = 0; i < ntables; i++) if (tables[i].data && freeFloats) freeFloats(tables[i].data, (size_t)tables[i].frames * tables[i].levels * tables[i].size);
        ntables = 0; tableBytes = 0; nparams = 0; loaded = false; hasError = false; err[0] = 0; refInit = refControl = refRelease = -1; faultCount = 0; loglen = 0; logbuf[0] = 0;
        mzero(&meta, sizeof(meta)); copyStr(meta.name, "Untitled", 40); copyStr(meta.category, "User Scripts", 24); meta.poly = 1; meta.maxVoices = 8; meta.version = 1; meta.api = 1;
    }
    static void printHook(void* u, const char* s, size_t n) { ((Engine*)u)->log(s, n); }
    bool load(const char* src, size_t len, bool injectTables = false);
    void registerApi();

    // ------------------------------------------------------------ voices
    int voiceOn(int note, float vel, u32 seed, int slotHint = -1);
    void voiceOff(int slot) { if (slot < 0 || slot >= kMaxVoices) return; Voice& v = voices[slot]; if (!v.active) return; v.gate = false; if (!v.faulted && refRelease >= 0) callVoice(v, refRelease, false); }
    void voiceKill(int slot) { if (slot < 0 || slot >= kMaxVoices) return; Voice& v = voices[slot]; if (!v.active) return; freeVoice(v); }
    void freeVoice(Voice& v) { for (int i = 0; i < v.nosc; i++) vm.unref(v.oscRef[i]); for (int i = 0; i < v.nmod; i++) vm.unref(v.modRef[i]); vm.unref(v.pref); vm.unref(v.vref); v.nosc = v.nmod = 0; v.vref = v.pref = -1; v.active = false; }
    bool callVoice(Voice& v, int fnRef, bool withBudget);
    void render(int slot, float* L, float* R, int n);
    void updateMods(Voice& v, const HostIn& h, int n);
    void readOsc(Voice& v);
    int countActive() { int c = 0; for (int i = 0; i < kMaxVoices; i++) if (voices[i].active) c++; return c; }
};

// ======================================================================= implementation
inline float polyBlep(double t, double dt) {
    if (t < dt) { double x = t / dt; return (float)(x + x - x * x - 1.0); }
    if (t > 1.0 - dt) { double x = (t - 1.0) / dt; return (float)(x * x + x + x + 1.0); }
    return 0.f;
}
inline float softclip(float x) { if (x > 3.f) x = 3.f; if (x < -3.f) x = -3.f; return x * (27.f + x * x) / (27.f + 9.f * x * x); }   // tanh-like rational

inline void Engine::registerApi() {
    using namespace lib;
    Table* VV = vm.newTable(0, 32); vm.tsetS(vm.globals, "vv", Value::tab(VV));
    struct Fn { const char* n; NativeFn f; };
    // utility
    static NativeFn mtof = [](VM& vm, Value* a, int n) -> int { double x; if (!needNum(vm, a, n, 0, "mtof", x)) return 0; return rnum(vm, 440.0 * pow_(2.0, (x - 69.0) / 12.0)); };
    static NativeFn ftom = [](VM& vm, Value* a, int n) -> int { double x; if (!needNum(vm, a, n, 0, "ftom", x)) return 0; return rnum(vm, 69.0 + 12.0 * log_(x / 440.0) / kLn2); };
    static NativeFn dbl = [](VM& vm, Value* a, int n) -> int { double x; if (!needNum(vm, a, n, 0, "db", x)) return 0; return rnum(vm, pow_(10.0, x / 20.0)); };
    static NativeFn clampf = [](VM& vm, Value* a, int n) -> int { double x, lo, hi; if (!needNum(vm, a, n, 0, "clamp", x) || !needNum(vm, a, n, 1, "clamp", lo) || !needNum(vm, a, n, 2, "clamp", hi)) return 0; return rnum(vm, x < lo ? lo : (x > hi ? hi : x)); };
    static NativeFn lerpf = [](VM& vm, Value* a, int n) -> int { double x, y, t; if (!needNum(vm, a, n, 0, "lerp", x) || !needNum(vm, a, n, 1, "lerp", y) || !needNum(vm, a, n, 2, "lerp", t)) return 0; return rnum(vm, x + (y - x) * t); };
    static NativeFn randf = [](VM& vm, Value* a, int n) -> int {   // vv.rand() -> [0,1); vv.rand(hi) -> [0,hi); vv.rand(lo,hi) -> [lo,hi) (floating point)
        double u = lib::rngUnit(vm); if (n == 0) return rnum(vm, u); double lo = 0, hi = 0;
        if (n == 1) { if (!needNum(vm, a, n, 0, "rand", hi)) return 0; } else if (!needNum(vm, a, n, 0, "rand", lo) || !needNum(vm, a, n, 1, "rand", hi)) return 0;
        return rnum(vm, lo + (hi - lo) * u); };
    static NativeFn randi = [](VM& vm, Value* a, int n) -> int { return lib::m_random(vm, a, n); };   // vv.randi(lo,hi): integer in [lo,hi]
    reg(vm, VV, "mtof", mtof); reg(vm, VV, "ftom", ftom); reg(vm, VV, "db", dbl); reg(vm, VV, "clamp", clampf); reg(vm, VV, "lerp", lerpf); reg(vm, VV, "rand", randf); reg(vm, VV, "randi", randi); reg(vm, VV, "log", b_print);
    vm.tsetS(VV, "api", Value::num(1));
    // declarations (setup phase only)
    static NativeFn instrument = [](VM& vm, Value* a, int n) -> int {
        Engine* E = of(vm); if (!E->inSetup) { vm.setError(0, 0, "instrument{} can only be called at the top of the script (setup phase)"); return 0; } if (n < 1 || a[0].t != T_TAB) { argErr(vm, 0, "instrument", "table", a, n); return 0; }
        Table* t = a[0].tb; Meta& m = E->meta; gstr(vm, t, vm.internPerm("name", 4), m.name, 40); gstr(vm, t, vm.internPerm("category", 8), m.category, 24); gstr(vm, t, vm.internPerm("author", 6), m.author, 24); gstr(vm, t, vm.internPerm("desc", 4), m.desc, 120);
        Value tg = vm.tgetS(t, "tags"); if (tg.t == T_TAB) { int o = 0; for (u32 i = 1; i <= vm.tlen(tg.tb); i++) { Value s = vm.tget(tg.tb, Value::num(i)); if (s.t == T_STR) { for (u32 k = 0; k < s.s->len && o < 60; k++) m.tags[o++] = s.s->data[k]; if (o < 60) m.tags[o++] = ','; } } if (o > 0 && m.tags[o - 1] == ',') o--; m.tags[o] = 0; } else if (tg.t == T_STR) copyStr(m.tags, tg.s->data, 64);
        Value pl = vm.tgetS(t, "poly"); if (pl.t == T_BOOL) m.poly = pl.b ? 1 : 0; m.maxVoices = (int)gnum(vm, t, vm.internPerm("voices", 6), 8); if (m.maxVoices < 1) m.maxVoices = 1; if (m.maxVoices > kMaxVoices) m.maxVoices = kMaxVoices; m.version = (int)gnum(vm, t, vm.internPerm("version", 7), 1); return 0; };
    static NativeFn param = [](VM& vm, Value* a, int n) -> int {
        Engine* E = of(vm); if (!E->inSetup) { vm.setError(0, 0, "param() can only be called at the top of the script (setup phase)"); return 0; } Str* id; if (!needStr(vm, a, n, 0, "param", id)) return 0; if (n < 2 || a[1].t != T_TAB) { argErr(vm, 1, "param", "table", a, n); return 0; }
        if (E->nparams >= kMaxParams) { vm.setError(0, 0, "too many parameters (max 16)"); return 0; } for (int i = 0; i < E->nparams; i++) if (strEq(id, E->params[i].id)) { vm.setError(0, 0, "this parameter id is already declared"); return 0; }
        Param& p = E->params[E->nparams]; mzero(&p, sizeof(p)); copyStr(p.id, id->data, 24); Table* t = a[1].tb; copyStr(p.label, p.id, 32); gstr(vm, t, vm.internPerm("label", 5), p.label, 32); gstr(vm, t, vm.internPerm("unit", 4), p.unit, 10); gstr(vm, t, vm.internPerm("group", 5), p.group, 16);
        p.mn = gnum(vm, t, vm.internPerm("min", 3), 0); p.mx = gnum(vm, t, vm.internPerm("max", 3), 1); p.def = gnum(vm, t, vm.internPerm("default", 7), p.mn); p.step = gnum(vm, t, vm.internPerm("step", 4), 0);
        if (p.mx < p.mn) { float tmp = p.mn; p.mn = p.mx; p.mx = tmp; } if (p.def < p.mn) p.def = p.mn; if (p.def > p.mx) p.def = p.mx;
        Value kd = vm.tgetS(t, "kind"); p.kind = 0; if (kd.t == T_STR) { if (strEq(kd.s, "slider")) p.kind = 1; else if (strEq(kd.s, "switch")) p.kind = 2; else if (strEq(kd.s, "menu")) p.kind = 3; else if (strEq(kd.s, "number")) p.kind = 4; else if (!strEq(kd.s, "knob")) { vm.setError(0, 0, "param kind must be knob, slider, switch, menu or number"); return 0; } }
        Value op = vm.tgetS(t, "options"); if (op.t == T_TAB) { p.nopts = 0; for (u32 i = 1; i <= vm.tlen(op.tb) && p.nopts < 8; i++) { Value s = vm.tget(op.tb, Value::num(i)); if (s.t == T_STR) copyStr(p.opts[p.nopts++], s.s->data, 16); } if (p.kind != 3 && p.nopts) p.kind = 3; if (p.kind == 3) { p.mn = 0; p.mx = (float)(p.nopts > 0 ? p.nopts - 1 : 0); p.step = 1; if (p.def > p.mx) p.def = p.mx; } }
        if (p.kind == 2) { p.mn = 0; p.mx = 1; p.step = 1; if (p.def > 0.5f) p.def = 1; else p.def = 0; }
        E->nparams++; return 0; };
    reg(vm, VV, "instrument", instrument); reg(vm, VV, "param", param); reg(vm, vm.globals, "instrument", instrument); reg(vm, vm.globals, "param", param);
    // wavetables
    static NativeFn mkWave = [](VM& vm, Value* a, int n) -> int {       // vv.waveform(name, fn(phase)->sample [, {size=2048, normalize=true}])
        Engine* E = of(vm); if (!E->inSetup) { vm.setError(0, 0, "waveform() can only be called during setup"); return 0; } Str* nm; if (!needStr(vm, a, n, 0, "waveform", nm)) return 0; if (n < 2 || (a[1].t != T_FUN && a[1].t != T_NAT)) { argErr(vm, 1, "waveform", "function", a, n); return 0; }
        int size = 2048; bool norm = true; if (n > 2 && a[2].t == T_TAB) { size = (int)gnum(vm, a[2].tb, vm.internPerm("size", 4), 2048); Value nv = vm.tgetS(a[2].tb, "normalize"); if (nv.t == T_BOOL) norm = nv.b; }
        if (size < 64 || size > 4096 || (size & (size - 1))) { vm.setError(0, 0, "wavetable size must be a power of two between 64 and 4096"); return 0; }
        if (E->inject) { if (!E->findTable(nm->data)) { vm.setError(0, 0, "internal: injected wavetable missing"); return 0; } return 0; }
        int levels = 0; for (int h = size / 2; h >= 4 && levels < 8; h >>= 1) levels++; WTable* t = E->newTable(nm->data, 1, size, levels); if (!t) return 0;
        for (int i = 0; i < size; i++) { Value arg = Value::num((double)i / size); Value r = vm.call1(a[1], &arg, 1); if (vm.fail) return 0; E->rawBuf[i] = r.t == T_NUM ? r.n : 0; }
        double mean = 0, pk = 0; for (int i = 0; i < size; i++) mean += E->rawBuf[i]; mean /= size; for (int i = 0; i < size; i++) { E->rawBuf[i] -= mean; if (fabs_(E->rawBuf[i]) > pk) pk = fabs_(E->rawBuf[i]); } if (norm && pk > 1e-9) for (int i = 0; i < size; i++) E->rawBuf[i] /= pk;
        E->buildMips(t, 0, E->rawBuf, E->reBuf, E->imBuf); return 0; };
    static NativeFn mkBank = [](VM& vm, Value* a, int n) -> int {       // vv.wavetable(name, frames, fn(frame01, phase)->sample [, opts])
        Engine* E = of(vm); if (!E->inSetup) { vm.setError(0, 0, "wavetable() can only be called during setup"); return 0; } Str* nm; double fr; if (!needStr(vm, a, n, 0, "wavetable", nm) || !needNum(vm, a, n, 1, "wavetable", fr)) return 0; if (n < 3 || (a[2].t != T_FUN && a[2].t != T_NAT)) { argErr(vm, 2, "wavetable", "function", a, n); return 0; }
        int size = 1024; bool norm = true; if (n > 3 && a[3].t == T_TAB) { size = (int)gnum(vm, a[3].tb, vm.internPerm("size", 4), 1024); Value nv = vm.tgetS(a[3].tb, "normalize"); if (nv.t == T_BOOL) norm = nv.b; }
        int frames = (int)fr; if (frames < 1 || frames > 256) { vm.setError(0, 0, "wavetable frames must be 1 to 256"); return 0; } if (size < 64 || size > 4096 || (size & (size - 1))) { vm.setError(0, 0, "wavetable size must be a power of two between 64 and 4096"); return 0; }
        if (E->inject) { if (!E->findTable(nm->data)) { vm.setError(0, 0, "internal: injected wavetable missing"); return 0; } return 0; }
        int levels = 0; for (int h = size / 2; h >= 4 && levels < 8; h >>= 1) levels++; WTable* t = E->newTable(nm->data, frames, size, levels); if (!t) return 0; double gpk = 0;
        // first pass finds the global peak so all frames share one gain
        float* peaks = (float*)vm.heap.alloc(sizeof(float) * (size_t)frames); if (!peaks) { vm.setFail(F_MEMORY, "out of memory"); return 0; }
        for (int f = 0; f < frames; f++) {
            for (int i = 0; i < size; i++) { Value args[2] = { Value::num(frames > 1 ? (double)f / (frames - 1) : 0.0), Value::num((double)i / size) }; Value r = vm.call1(a[2], args, 2); if (vm.fail) { vm.heap.release(peaks, sizeof(float) * (size_t)frames); return 0; } E->rawBuf[i] = r.t == T_NUM ? r.n : 0; }
            double mean = 0, pk = 0; for (int i = 0; i < size; i++) mean += E->rawBuf[i]; mean /= size; for (int i = 0; i < size; i++) { E->rawBuf[i] -= mean; if (fabs_(E->rawBuf[i]) > pk) pk = fabs_(E->rawBuf[i]); } if (pk > gpk) gpk = pk; peaks[f] = (float)pk;
            E->buildMips(t, f, E->rawBuf, E->reBuf, E->imBuf);
        }
        if (norm && gpk > 1e-9) { size_t tot = (size_t)frames * levels * size; for (size_t i = 0; i < tot; i++) t->data[i] = (float)(t->data[i] / gpk); }
        vm.heap.release(peaks, sizeof(float) * (size_t)frames); return 0; };
    static NativeFn mkAdd = [](VM& vm, Value* a, int n) -> int {        // vv.additive(name, {amp1, amp2, ...} [, {size=2048}])
        Engine* E = of(vm); if (!E->inSetup) { vm.setError(0, 0, "additive() can only be called during setup"); return 0; } Str* nm; if (!needStr(vm, a, n, 0, "additive", nm) || !needTab(vm, a, n, 1, "additive")) return 0;
        int size = 2048; if (n > 2 && a[2].t == T_TAB) size = (int)gnum(vm, a[2].tb, vm.internPerm("size", 4), 2048); if (size < 64 || size > 4096 || (size & (size - 1))) { vm.setError(0, 0, "wavetable size must be a power of two between 64 and 4096"); return 0; }
        if (E->inject) return 0; int levels = 0; for (int h = size / 2; h >= 4 && levels < 8; h >>= 1) levels++; WTable* t = E->newTable(nm->data, 1, size, levels); if (!t) return 0;
        int nh = (int)vm.tlen(a[1].tb); if (nh > size / 2 - 1) nh = size / 2 - 1; for (int i = 0; i < size; i++) E->rawBuf[i] = 0;
        for (int h = 1; h <= nh; h++) { Value av = vm.tget(a[1].tb, Value::num(h)); double amp = av.t == T_NUM ? av.n : 0; if (amp == 0) continue; for (int i = 0; i < size; i++) E->rawBuf[i] += amp * E->sine[(int)(((long long)h * i) % size * (kSineN / size) % kSineN)]; if (size > kSineN) {} }
        double pk = 0; for (int i = 0; i < size; i++) if (fabs_(E->rawBuf[i]) > pk) pk = fabs_(E->rawBuf[i]); if (pk > 1e-9) for (int i = 0; i < size; i++) E->rawBuf[i] /= pk; E->buildMips(t, 0, E->rawBuf, E->reBuf, E->imBuf); return 0; };
    static NativeFn mkAddBank = [](VM& vm, Value* a, int n) -> int {    // vv.additiveBank(name, frames, nharm, fn(frame01, harmonic)->amplitude [, opts])
        Engine* E = of(vm); if (!E->inSetup) { vm.setError(0, 0, "additiveBank() can only be called during setup"); return 0; } Str* nm; double fr, nh; if (!needStr(vm, a, n, 0, "additiveBank", nm) || !needNum(vm, a, n, 1, "additiveBank", fr) || !needNum(vm, a, n, 2, "additiveBank", nh)) return 0; if (n < 4 || (a[3].t != T_FUN && a[3].t != T_NAT)) { argErr(vm, 3, "additiveBank", "function", a, n); return 0; }
        int size = 1024; if (n > 4 && a[4].t == T_TAB) size = (int)gnum(vm, a[4].tb, vm.internPerm("size", 4), 1024); int frames = (int)fr, H = (int)nh; if (frames < 1 || frames > 256) { vm.setError(0, 0, "frames must be 1 to 256"); return 0; } if (H < 1 || H > 256) { vm.setError(0, 0, "harmonics must be 1 to 256"); return 0; } if (size < 64 || size > 4096 || (size & (size - 1))) { vm.setError(0, 0, "wavetable size must be a power of two between 64 and 4096"); return 0; }
        if (E->inject) return 0; int levels = 0; for (int h = size / 2; h >= 4 && levels < 8; h >>= 1) levels++; WTable* t = E->newTable(nm->data, frames, size, levels); if (!t) return 0; double gpk = 0;
        for (int f = 0; f < frames; f++) {
            for (int i = 0; i < size; i++) E->rawBuf[i] = 0;
            for (int h = 1; h <= H && h < size / 2; h++) { Value args[2] = { Value::num(frames > 1 ? (double)f / (frames - 1) : 0.0), Value::num(h) }; Value r = vm.call1(a[3], args, 2); if (vm.fail) return 0; double amp = r.t == T_NUM ? r.n : 0; if (amp == 0) continue; for (int i = 0; i < size; i++) E->rawBuf[i] += amp * E->sine[(int)(((long long)h * i) % size * (kSineN / size) % kSineN)]; }
            double pk = 0; for (int i = 0; i < size; i++) if (fabs_(E->rawBuf[i]) > pk) pk = fabs_(E->rawBuf[i]); if (pk > gpk) gpk = pk; E->buildMips(t, f, E->rawBuf, E->reBuf, E->imBuf);
        }
        if (gpk > 1e-9) { size_t tot = (size_t)frames * levels * size; for (size_t i = 0; i < tot; i++) t->data[i] = (float)(t->data[i] / gpk); } return 0; };
    reg(vm, VV, "waveform", mkWave); reg(vm, VV, "wavetable", mkBank); reg(vm, VV, "additive", mkAdd); reg(vm, VV, "additiveBank", mkAddBank);
    // oscillators and modulators attach to the voice table they are given
    static NativeFn mkOsc = [](VM& vm, Value* a, int n) -> int {
        Engine* E = of(vm); if (n < 2 || a[0].t != T_TAB || a[1].t != T_TAB) { argErr(vm, a[0].t != T_TAB ? 0 : 1, "osc", "table", a, n); return 0; }
        Voice* vo = nullptr; for (int i = 0; i < kMaxVoices; i++) if (E->voices[i].active && E->voices[i].vref >= 0 && E->vm.getref(E->voices[i].vref).tb == a[0].tb) { vo = &E->voices[i]; break; } if (!vo) { vm.setError(0, 0, "vv.osc must be called with the voice table (the argument of init)"); return 0; }
        if (vo->nosc >= kMaxOsc) { vm.setError(0, 0, "too many oscillators in one voice (max 16)"); return 0; } vo->oscRef[vo->nosc] = vm.ref(a[1]); vo->osc[vo->nosc] = Osc(); vo->nosc++;
        if (vm.tgetStr(a[1].tb, E->K.wave).t == T_NIL) vm.tsetRaw(a[1].tb, Value::str(E->K.wave), Value::str(vm.internPerm("saw", 3))); return r1(vm, a[1]); };
    static NativeFn mkMod = [](VM& vm, Value* a, int n) -> int {         // vv.mod(v, {kind="lfo"|"env"|"steps"|"chaos"|"follow"|"noise", ...}) -> table with .out
        Engine* E = of(vm); if (n < 2 || a[0].t != T_TAB || a[1].t != T_TAB) { argErr(vm, 1, "mod", "table", a, n); return 0; }
        Voice* vo = nullptr; for (int i = 0; i < kMaxVoices; i++) if (E->voices[i].active && E->voices[i].vref >= 0 && E->vm.getref(E->voices[i].vref).tb == a[0].tb) { vo = &E->voices[i]; break; } if (!vo) { vm.setError(0, 0, "vv.mod must be called with the voice table (the argument of init)"); return 0; }
        if (vo->nmod >= kMaxMod) { vm.setError(0, 0, "too many modulators in one voice (max 16)"); return 0; }
        Mod& m = vo->mod[vo->nmod]; m = Mod(); Table* t = a[1].tb; Value kd = vm.tgetStr(t, E->K.kind); Str* ks = kd.t == T_STR ? kd.s : nullptr;
        if (!ks) { vm.setError(0, 0, "vv.mod needs kind = \"lfo\", \"env\", \"steps\", \"chaos\", \"follow\" or \"noise\""); return 0; }
        if (strEq(ks, "lfo")) m.kind = M_LFO; else if (strEq(ks, "env")) m.kind = M_ENV; else if (strEq(ks, "steps")) m.kind = M_STEPS; else if (strEq(ks, "chaos")) m.kind = M_CHAOS; else if (strEq(ks, "follow")) m.kind = M_FOLLOW; else if (strEq(ks, "noise")) m.kind = M_NOISE; else { vm.setError(0, 0, "unknown modulator kind (use lfo, env, steps, chaos, follow or noise)"); return 0; }
        m.rng = (u64)vo->seed * 2654435761ULL + (u64)vo->nmod * 40503ULL + 1; m.ph = gnum(vm, t, E->K.phase, 0); m.depth = 1; m.bipolar = true;
        Value sh = vm.tgetStr(t, E->K.shape); if (sh.t == T_STR && m.kind != M_CHAOS) { const char* names[] = { "sine", "tri", "saw", "rsaw", "square", "sh", "smooth", "noise" }; m.shape = 0; bool ok = false; for (int i = 0; i < 8; i++) if (strEq(sh.s, names[i])) { m.shape = i; ok = true; } if (!ok) { vm.setError(0, 0, "unknown LFO shape (sine, tri, saw, rsaw, square, sh, smooth, noise)"); return 0; } }
        Value dv = vm.tgetStr(t, E->K.div); static const char* divs[] = { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T", "2/1", "4/1" }; static const float beats[] = { 4, 2, 1, .5f, .25f, .125f, 2.f / 3, 1.f / 3, 1.f / 6, 8, 16 };
        m.syncDiv = -1; if (dv.t == T_STR) { for (int i = 0; i < 11; i++) if (strEq(dv.s, divs[i])) m.syncDiv = i; if (m.syncDiv < 0) { vm.setError(0, 0, "unknown tempo division (use 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/4T, 1/8T, 1/16T, 2/1, 4/1)"); return 0; } m.syncMul = 1.f / beats[m.syncDiv]; m.sync = true; }
        m.rate = gnum(vm, t, E->K.rate, 1.f); m.a = gnum(vm, t, E->K.a, 0.01f); m.d = gnum(vm, t, E->K.d, 0.1f); m.s = gnum(vm, t, E->K.s, 0.7f); m.r = gnum(vm, t, E->K.r, 0.2f); m.glide = gnum(vm, t, E->K.glide, 0); m.amount = gnum(vm, t, E->K.amount, 1); m.srcOsc = gint(vm, t, E->K.src, 0);
        Value bp = vm.tgetStr(t, E->K.bipolar); if (bp.t == T_BOOL) m.bipolar = bp.b; if (m.kind == M_FOLLOW) { m.a = gnum(vm, t, E->K.a, 0.005f); m.r = gnum(vm, t, E->K.r, 0.15f); }
        Value stp = vm.tgetStr(t, E->K.steps); if (stp.t == T_TAB) { m.nsteps = (int)vm.tlen(stp.tb); if (m.nsteps > 32) m.nsteps = 32; for (int i = 0; i < m.nsteps; i++) { Value s = vm.tget(stp.tb, Value::num(i + 1)); m.steps[i] = s.t == T_NUM ? (float)s.n : 0; } }
        if (m.kind == M_STEPS && m.nsteps == 0) { vm.setError(0, 0, "a steps modulator needs steps = {...}"); return 0; }
        if (m.kind == M_CHAOS) { m.x = 0.1 + 0.0001 * (vo->seed % 1000); m.y = 0; m.z = 0; Value cs = vm.tgetStr(t, E->K.shape); m.shape = (cs.t == T_STR && strEq(cs.s, "logistic")) ? 1 : ((cs.t == T_STR && strEq(cs.s, "henon")) ? 2 : 0); if (m.shape == 1) m.x = 0.3 + 0.0001 * (vo->seed % 1000); if (m.shape == 2) { m.x = 0.1; m.y = 0.1; } }
        vm.tsetRaw(t, Value::str(E->K.out), Value::num(0)); vo->modRef[vo->nmod] = vm.ref(a[1]); vo->nmod++; return r1(vm, a[1]); };
    reg(vm, VV, "osc", mkOsc); reg(vm, VV, "mod", mkMod);
}

inline bool Engine::load(const char* src, size_t len, bool injectTables) {
    if (!inited) return false;
    clearScript(); inject = injectTables; injectIdx = 0;
    if (!vm.init(pool, poolBytes, 1536 * 1024, 6000)) { setErr("script engine could not allocate memory", 0, 0); return false; }
    vm.user = this; vm.maxDepth = 60; vm.chunk = "script"; vm.printHook = printHook; lib::ls().print = printHook; lib::ls().user = this;
    lib::openLibs(vm); makeKeys(); registerApi();
    static const char prelude[] =
        "vv.scales={major={0,2,4,5,7,9,11},minor={0,2,3,5,7,8,10},dorian={0,2,3,5,7,9,10},phrygian={0,1,3,5,7,8,10},lydian={0,2,4,6,7,9,11},mixolydian={0,2,4,5,7,9,10},pentatonic={0,2,4,7,9},minorpent={0,3,5,7,10},blues={0,3,5,6,7,10},wholetone={0,2,4,6,8,10},harmonicminor={0,2,3,5,7,8,11},chromatic={0,1,2,3,4,5,6,7,8,9,10,11}}\n"
        "function vv.scale(name,degree,root) local s=vv.scales[name] if not s then error('unknown scale '..tostring(name),2) end local n=#s local oct=math.floor(degree/n) local i=degree-oct*n return (root or 60)+s[i+1]+12*oct end\n"
        "function vv.map(x,a,b,c,d) return c+(d-c)*(x-a)/(b-a) end\n"
        "function vv.saw(p) return 2*(p-math.floor(p))-1 end\n"
        "function vv.tri(p) p=p-math.floor(p) return 4*math.abs(p-0.5)-1 end\n"
        "function vv.pulse(p,w) p=p-math.floor(p) if p<(w or 0.5) then return 1 else return -1 end end\n"
        "function vv.sine(p) return math.sin(6.283185307179586*p) end\n";
    Value pf; vm.budget = vm.budgetStart = 2000000; if (!vm.compile(prelude, sizeof(prelude) - 1, &pf) || vm.callv(pf, nullptr, 0), vm.fail) { setErr("internal prelude failed", 0, 0); return false; }
    vm.chunk = "script"; Value fn; vm.budget = vm.budgetStart = budgetSetup; inSetup = true;
    if (!vm.compile(src, len, &fn)) { setErr(vm.errmsg, vm.errLine, vm.errCol); inSetup = false; return false; }
    vm.callv(fn, nullptr, 0); inSetup = false;
    if (vm.fail) { setErr(vm.errmsg, vm.errLine, vm.errCol); return false; }
    totalInstr = vm.instrUsed();
    Value fi = vm.tgetS(vm.globals, "init"), fc = vm.tgetS(vm.globals, "control"), fr = vm.tgetS(vm.globals, "release");
    if (fc.t != T_FUN && fi.t != T_FUN) { setErr("the script must define a control(v) function (and optionally init(v) and release(v))", 0, 0); return false; }
    if (fi.t == T_FUN) refInit = vm.ref(fi); if (fc.t == T_FUN) refControl = vm.ref(fc); if (fr.t == T_FUN) refRelease = vm.ref(fr);
    if (vm.heap.failed) { setErr("script ran out of memory during setup", 0, 0); return false; }
    vm.gcRun(); loaded = true; return true;
}

inline int Engine::voiceOn(int note, float vel, u32 seed, int slotHint) {
    if (!loaded) return -1; int slot = -1;
    if (slotHint >= 0 && slotHint < kMaxVoices && !voices[slotHint].active) slot = slotHint; else for (int i = 0; i < kMaxVoices; i++) if (!voices[i].active) { slot = i; break; }
    if (slot < 0) return -1;
    Voice& v = voices[slot]; v = Voice(); v.active = true; v.gate = true; v.note = note; v.vel = vel; v.seed = seed * 2654435761u + (u32)slot * 97u + layerSeed * 7919u + 1u; v.rng = (u64)v.seed * 6364136223846793005ULL + 1442695040888963407ULL; if (!v.rng) v.rng = 1; v.voiceId = slot;
    v.level = v.prevLevel = 1; v.pan = v.prevPan = 0; v.err[0] = 0; v.paramsInit = false;
    Table* vt = vm.newTable(0, 24); Table* pt = vm.newTable(0, 20); if (!vt || !pt) { vm.fail = F_NONE; v.active = false; return -1; }
    v.vref = vm.ref(Value::tab(vt)); v.pref = vm.ref(Value::tab(pt)); vm.tsetRaw(vt, Value::str(K.p), Value::tab(pt)); vm.tsetRaw(vt, Value::str(K.note), Value::num(note)); vm.tsetRaw(vt, Value::str(K.vel), Value::num(vel)); vm.tsetRaw(vt, Value::str(K.index), Value::num(slot + 1)); vm.tsetRaw(vt, Value::str(K.sr), Value::num(sr)); vm.tsetRaw(vt, Value::str(K.gate), Value::boolean(true));
    vm.tsetRaw(vt, Value::str(K.level), Value::num(1)); vm.tsetRaw(vt, Value::str(K.pan), Value::num(0)); vm.tsetRaw(vt, Value::str(K.vpitch), Value::num(0)); vm.tsetRaw(vt, Value::str(K.t), Value::num(0));
    for (int i = 0; i < nparams; i++) { Str* k = istr(params[i].id); vm.tsetRaw(pt, Value::str(k), Value::num(params[i].def)); v.lastParams[i] = params[i].def; }
    if (vm.fail) { vm.fail = F_NONE; freeVoice(v); return -1; }
    activeVoices = countActive(); if (refInit >= 0) callVoice(v, refInit, true); return slot;
}

inline bool Engine::callVoice(Voice& v, int fnRef, bool) {
    Value fn = vm.getref(fnRef); Value arg = vm.getref(v.vref); vm.fail = F_NONE; vm.sp = vm.stack; vm.depth = 0; vm.budget = vm.budgetStart = budgetControl; vm.rngState = v.rng ? v.rng : 1;
    vm.callv(fn, &arg, 1); v.rng = vm.rngState; long used = vm.instrUsed(); v.instrBlock += used; lastBlockInstr += used; totalInstr += used;
    if (vm.fail) {
        v.faulted = true; copyStr(v.err, vm.errmsg, 160); v.errLine = vm.errLine; if (!hasError || true) { setErr(vm.errmsg, vm.errLine, vm.errCol); } faultCount++;
        vm.fail = F_NONE; vm.sp = vm.stack; vm.depth = 0; vm.errval = Value(); return false;
    }
    vm.gcMaybe(); return true;
}

inline void Engine::readOsc(Voice& v) {
    for (int i = 0; i < v.nosc; i++) {
        Value tv = vm.getref(v.oscRef[i]); if (tv.t != T_TAB) continue; Osc& o = v.osc[i]; Table* t = tv.tb; (void)t;
    }
}

inline void Engine::updateMods(Voice& v, const HostIn& h, int n) {
    double dt = n / (double)sr; float bpm = h.bpm > 1 ? h.bpm : 120.f;
    for (int i = 0; i < v.nmod; i++) {
        Mod& m = v.mod[i]; double hz = m.rate;
        if (m.sync) { double bps = bpm / 60.0; hz = bps * m.syncMul; if (m.kind == M_LFO || m.kind == M_STEPS || m.kind == M_CHAOS || m.kind == M_NOISE) {} }
        float out = 0;
        switch (m.kind) {
            case M_LFO: {
                double prev = m.ph; m.ph += hz * dt; bool wrap = m.ph >= 1.0; if (wrap) m.ph -= floor_(m.ph); double p = m.ph; float u = 0;
                switch (m.shape) {
                    case 0: u = (float)sin_(2 * kPi * p); break; case 1: u = (float)(4 * fabs_(p - 0.5) - 1); break; case 2: u = (float)(2 * p - 1); break; case 3: u = (float)(1 - 2 * p); break; case 4: u = p < 0.5 ? 1.f : -1.f; break;
                    case 5: if (wrap || m.step == 0) { m.rng = m.rng * 6364136223846793005ULL + 1442695040888963407ULL; m.target = (float)((m.rng >> 40) / 8388608.0) * 2.f - 1.f; m.step = 1; } u = m.target; break;
                    case 6: if (wrap || m.step == 0) { m.rng = m.rng * 6364136223846793005ULL + 1442695040888963407ULL; m.state = m.target; m.target = (float)((m.rng >> 40) / 8388608.0) * 2.f - 1.f; m.step = 1; } { double c = 0.5 - 0.5 * cos_(kPi * p); u = (float)(m.state + (m.target - m.state) * c); } break;
                    default: m.rng = m.rng * 6364136223846793005ULL + 1442695040888963407ULL; u = (float)((m.rng >> 40) / 8388608.0) * 2.f - 1.f;
                }
                (void)prev; out = m.bipolar ? u : 0.5f * u + 0.5f; out *= m.depth; break; }
            case M_ENV: {
                float tau; float e = m.env;
                if (v.gate) { if (m.stage == 0) { tau = m.a; if (tau <= 1e-4f) e = 1; else e += (float)(dt / tau); if (e >= 1) { e = 1; m.stage = 1; } } else { tau = m.d; float tgt = m.s; if (tau <= 1e-4f) e = tgt; else e += (tgt - e) * (float)(1.0 - exp_(-dt * 4.0 / tau)); } }
                else { m.stage = 2; tau = m.r; if (tau <= 1e-4f) e = 0; else e += (0 - e) * (float)(1.0 - exp_(-dt * 4.0 / tau)); }
                if (e < 0) e = 0; m.env = e; out = e; break; }
            case M_STEPS: {
                double prev = m.ph; m.ph += hz * dt; while (m.ph >= 1.0) { m.ph -= 1.0; m.step = (m.step + 1) % (m.nsteps ? m.nsteps : 1); } (void)prev;
                float tgt = m.steps[m.step % (m.nsteps ? m.nsteps : 1)]; if (m.glide > 0) m.out += (tgt - m.out) * (float)(1.0 - exp_(-dt / (m.glide + 1e-4))); else m.out = tgt; out = m.out; break; }
            case M_CHAOS: {
                double stepSize = hz * dt; m.ph += stepSize; int it = 0;
                while (m.ph >= 1.0 && it < 8) { m.ph -= 1.0; it++;
                    if (m.shape == 0) { double dtl = 0.01; double sg = 10, rho = 28, bt = 8.0 / 3; for (int k = 0; k < 5; k++) { double dx = sg * (m.y - m.x), dy = m.x * (rho - m.z) - m.y, dz = m.x * m.y - bt * m.z; m.x += dx * dtl; m.y += dy * dtl; m.z += dz * dtl; } }
                    else if (m.shape == 1) { m.x = 3.9 * m.x * (1 - m.x); if (m.x <= 0 || m.x >= 1) m.x = 0.37; }
                    else { double nx = 1 - 1.4 * m.x * m.x + m.y; m.y = 0.3 * m.x; m.x = nx; if (m.x > 3 || m.x < -3) { m.x = 0.1; m.y = 0.1; } } }
                float raw = m.shape == 0 ? (float)(m.x / 20.0) : (m.shape == 1 ? (float)(m.x * 2 - 1) : (float)(m.x / 1.3)); if (raw > 1) raw = 1; if (raw < -1) raw = -1; m.out += (raw - m.out) * (float)(1.0 - exp_(-dt * 30)); out = (m.bipolar ? m.out : 0.5f * m.out + 0.5f) * m.amount; break; }
            case M_FOLLOW: {
                float inp = v.peak; float tau = inp > m.env ? m.a : m.r; float c = tau <= 1e-4f ? 1.f : (float)(1.0 - exp_(-dt / tau)); m.env += (inp - m.env) * c; out = m.env * m.amount; break; }
            case M_NOISE: {
                m.rng = m.rng * 6364136223846793005ULL + 1442695040888963407ULL; float w = (float)((m.rng >> 40) / 8388608.0) * 2.f - 1.f; Value dummy; (void)dummy;
                if (m.shape == 1) { m.pinkB[0] = 0.99765f * m.pinkB[0] + w * 0.0990460f; m.pinkB[1] = 0.96300f * m.pinkB[1] + w * 0.2965164f; m.pinkB[2] = 0.57000f * m.pinkB[2] + w * 1.0526913f; out = (m.pinkB[0] + m.pinkB[1] + m.pinkB[2] + w * 0.1848f) * 0.25f; }
                else if (m.shape == 2) { m.out += w * 0.1f; if (m.out > 1) m.out = 1; if (m.out < -1) m.out = -1; out = m.out; } else out = w;
                out *= m.amount; break; }
        }
        m.out = (m.kind == M_STEPS) ? m.out : out; if (m.kind != M_STEPS && m.kind != M_CHAOS) m.out = out; if (m.kind == M_CHAOS) m.out = m.out;
        Value mv = vm.getref(v.modRef[i]); if (mv.t == T_TAB) vm.tsetRaw(mv.tb, Value::str(K.out), Value::num(m.kind == M_CHAOS || m.kind == M_STEPS ? (m.kind == M_STEPS ? m.out : (m.bipolar ? m.out : 0.5f * m.out + 0.5f)) : out));
    }
}

inline void Engine::render(int slot, float* L, float* R, int n) {
    for (int i = 0; i < n; i++) { L[i] = 0; R[i] = 0; }
    if (slot < 0 || slot >= kMaxVoices) return; Voice& v = voices[slot]; if (!v.active || !loaded) return;
    HostIn& h = host[slot]; if (n > kBlock) n = kBlock; v.peak = 0;
    // 1. hand the host state to the script
    Value vt = vm.getref(v.vref); if (vt.t != T_TAB) return; Table* T = vt.tb; Value pv = vm.getref(v.pref); Table* P = pv.t == T_TAB ? pv.tb : nullptr;
    vm.tsetRaw(T, Value::str(K.freq), Value::num(h.freq)); vm.tsetRaw(T, Value::str(K.gate), Value::boolean(v.gate)); vm.tsetRaw(T, Value::str(K.t), Value::num(v.t)); vm.tsetRaw(T, Value::str(K.dt), Value::num(n / (double)sr));
    vm.tsetRaw(T, Value::str(K.bpm), Value::num(h.bpm > 1 ? h.bpm : 120)); vm.tsetRaw(T, Value::str(K.beat), Value::num(h.beat)); vm.tsetRaw(T, Value::str(K.playing), Value::boolean(h.playing > 0.5f));
    if (P) for (int i = 0; i < nparams; i++) { float pvv = h.params[i]; if (!v.paramsInit || pvv != v.lastParams[i]) { vm.tsetRaw(P, Value::str(istr(params[i].id)), Value::num(pvv)); v.lastParams[i] = pvv; } } v.paramsInit = true;
    if (vm.fail) { vm.fail = F_NONE; }
    // 2. modulators then the script's control function (budgeted; failure mutes only this voice)
    updateMods(v, h, n);
    if (!v.faulted && refControl >= 0) callVoice(v, refControl, true);
    if (v.faulted) { v.t += n / (double)sr; return; }
    // 3. read what the script asked for
    float vlev = 1, vpan = 0, vpitch = 0; { Value tv = vm.getref(v.vref); if (tv.t == T_TAB) { vlev = gnum(vm, tv.tb, K.level, 1); vpan = gnum(vm, tv.tb, K.pan, 0); vpitch = gnum(vm, tv.tb, K.vpitch, 0); for (int q = 0; q < 4; q++) { Str* mk = q == 0 ? K.mod1 : q == 1 ? K.mod2 : q == 2 ? K.mod3 : K.mod4; v.modOut[q] = gnum(vm, tv.tb, mk, v.modOut[q]); } } }
    if (vlev < 0) vlev = 0; if (vlev > 4) vlev = 4; if (vpan < -1) vpan = -1; if (vpan > 1) vpan = 1;
    struct D { float level, pan, ratio, pw, pos, fm, pm, ring, am, fold, drive, phase, width, spread, detCents; int wave, unison, fmsrc, pmsrc, ringsrc, amsrc, syncsrc, sync; bool on, out; float fixedHz; bool hasFixed; const WTable* tab; } d[kMaxOsc];
    double baseHz = h.freq * pow_(2.0, (h.pitchCents + vpitch * 100.0) / 1200.0);
    for (int i = 0; i < v.nosc; i++) {
        Value ov = vm.getref(v.oscRef[i]); D& q = d[i]; q.on = false; q.out = false; q.tab = nullptr; if (ov.t != T_TAB) continue; Table* t = ov.tb; Osc& o = v.osc[i];
        Value wv = vm.tgetStr(t, K.wave); q.wave = W_SAW; q.tab = nullptr;
        if (wv.t == T_STR) {
            if (wv.s != o.waveKey) { o.waveKey = wv.s; const char* nm[] = { "sine", "tri", "saw", "rsaw", "square", "pulse", "noise" }; o.wave = -1; for (int k = 0; k < 7; k++) if (strEq(wv.s, nm[k])) o.wave = k; if (o.wave < 0) { WTable* tt = findTableS(wv.s); if (tt) { o.wave = W_TABLE0 + (int)(tt - tables); } else { o.wave = W_SAW; if (!v.faulted) { v.faulted = true; copyStr(v.err, "unknown waveform name (use a built-in name or one created with vv.waveform / vv.wavetable)", 160); setErr(v.err, 0, 0); faultCount++; return; } } } }
            q.wave = o.wave; if (q.wave >= W_TABLE0) q.tab = &tables[q.wave - W_TABLE0];
        } else { o.waveKey = nullptr; }
        Value onv = vm.tgetStr(t, K.on); q.on = !(onv.t == T_BOOL && !onv.b); Value outv = vm.tgetStr(t, K.out); q.out = !(outv.t == T_BOOL && !outv.b);
        q.level = gnum(vm, t, K.level, 1); q.pan = gnum(vm, t, K.pan, 0); q.ratio = gnum(vm, t, K.ratio, 1); q.pw = gnum(vm, t, K.pw, 0.5f); q.pos = gnum(vm, t, K.pos, 0); q.fm = gnum(vm, t, K.fm, 0); q.pm = gnum(vm, t, K.pm, 0); q.ring = gnum(vm, t, K.ring, 0); q.am = gnum(vm, t, K.am, 0); q.fold = gnum(vm, t, K.fold, 0); q.drive = gnum(vm, t, K.drive, 0); q.phase = gnum(vm, t, K.phase, 0);
        q.fmsrc = gint(vm, t, K.fmsrc, 0); q.pmsrc = gint(vm, t, K.pmsrc, 0); q.ringsrc = gint(vm, t, K.ringsrc, 0); q.amsrc = gint(vm, t, K.amsrc, 0); q.syncsrc = gint(vm, t, K.syncsrc, 0); q.sync = 0; Value sv = vm.tgetStr(t, K.sync); if (sv.t == T_BOOL && sv.b) q.sync = 1; else if (sv.t == T_NUM) q.sync = (int)sv.n; else if (sv.t == T_STR) q.sync = strEq(sv.s, "soft") ? 2 : 1;
        q.unison = gint(vm, t, K.unison, 1); if (q.unison < 1) q.unison = 1; if (q.unison > kMaxUni) q.unison = kMaxUni; q.spread = gnum(vm, t, K.spread, 0); q.width = gnum(vm, t, K.width, 0.5f);
        q.detCents = gnum(vm, t, K.detune, 0) + gnum(vm, t, K.semi, 0) * 100.f; Value fx = vm.tgetStr(t, K.freq); q.hasFixed = fx.t == T_NUM; q.fixedHz = q.hasFixed ? (float)fx.n : 0;
        if (q.pw < 0.02f) q.pw = 0.02f; if (q.pw > 0.98f) q.pw = 0.98f; if (q.level < 0) q.level = 0; if (q.level > 8) q.level = 8; if (q.ratio < 0) q.ratio = 0; if (q.fm > 64) q.fm = 64; if (q.drive < 0) q.drive = 0; if (q.fold < 0) q.fold = 0; if (q.fold > 16) q.fold = 16;
        if (o.fresh) { o.prevLvl = q.level; o.prevRatio = q.ratio; o.prevPw = q.pw; o.prevPos = q.pos; o.prevFm = q.fm; o.prevPm = q.pm; o.prevRing = q.ring; o.prevAm = q.am; o.prevFold = q.fold; o.prevDrive = q.drive; o.prevPan = q.pan; o.fresh = false; u64 r = v.rng; for (int u = 0; u < kMaxUni; u++) { r = r * 6364136223846793005ULL + 1442695040888963407ULL; o.ph[u] = (u == 0 ? 0.0 : (double)(r >> 11) / 9007199254740992.0) + q.phase; } v.rng = r; }
    }
    // 4. render the bank (control-rate parameters are ramped across the block)
    float outs[kMaxOsc]; float prevOuts[kMaxOsc]; for (int i = 0; i < v.nosc; i++) { outs[i] = v.osc[i].out; prevOuts[i] = v.osc[i].out; }
    float invN = 1.f / n; double isr = 1.0 / sr; float detF[kMaxOsc][kMaxUni]; float uPan[kMaxOsc][kMaxUni];
    for (int i = 0; i < v.nosc; i++) { D& q = d[i]; for (int u = 0; u < q.unison; u++) { float pos = q.unison > 1 ? (float)u / (q.unison - 1) * 2.f - 1.f : 0.f; detF[i][u] = (float)pow_(2.0, (q.detCents + pos * q.spread) / 1200.0); uPan[i][u] = q.unison > 1 ? pos * q.width : 0.f; } }
    float pk = 0; float gvl = v.prevLevel, gdl = (vlev - v.prevLevel) * invN, gvp = v.prevPan, gdp = (vpan - v.prevPan) * invN;
    for (int s = 0; s < n; s++) {
        float mixL = 0, mixR = 0; float frac = (float)(s + 1) * invN;
        for (int i = 0; i < v.nosc; i++) {
            D& q = d[i]; Osc& o = v.osc[i]; if (!q.on) { outs[i] = 0; o.wrapped = false; continue; }
            float lvl = o.prevLvl + (q.level - o.prevLvl) * frac, rat = o.prevRatio + (q.ratio - o.prevRatio) * frac, pw = o.prevPw + (q.pw - o.prevPw) * frac, pos = o.prevPos + (q.pos - o.prevPos) * frac, fmv = o.prevFm + (q.fm - o.prevFm) * frac, pmv = o.prevPm + (q.pm - o.prevPm) * frac, rg = o.prevRing + (q.ring - o.prevRing) * frac, am = o.prevAm + (q.am - o.prevAm) * frac, fold = o.prevFold + (q.fold - o.prevFold) * frac, drv = o.prevDrive + (q.drive - o.prevDrive) * frac, pn = o.prevPan + (q.pan - o.prevPan) * frac;
            double f0 = q.hasFixed ? q.fixedHz : baseHz * rat; if (f0 > sr * 0.45) f0 = sr * 0.45; if (f0 < 0) f0 = 0;
            float modFm = (q.fmsrc >= 1 && q.fmsrc <= v.nosc && q.fmsrc != i + 1) ? (q.fmsrc - 1 < i ? outs[q.fmsrc - 1] : prevOuts[q.fmsrc - 1]) : 0.f;
            float modPm = (q.pmsrc >= 1 && q.pmsrc <= v.nosc && q.pmsrc != i + 1) ? (q.pmsrc - 1 < i ? outs[q.pmsrc - 1] : prevOuts[q.pmsrc - 1]) : 0.f;
            bool syncHit = false; if (q.sync && q.syncsrc >= 1 && q.syncsrc <= v.nosc && q.syncsrc - 1 < i) syncHit = v.osc[q.syncsrc - 1].wrapped;
            float sum = 0, sumL = 0, sumR = 0; bool wrapAny = false;
            for (int u = 0; u < q.unison; u++) {
                double inc = f0 * detF[i][u] * isr; double& ph = o.ph[u]; ph += inc;
                if (ph >= 1.0) { ph -= floor_(ph); if (u == 0) wrapAny = true; } else if (ph < 0) ph += 1.0;
                if (syncHit) { if (q.sync == 2) ph = 1.0 - ph; else ph = 0.0; }
                double p = ph + q.phase + (double)pmv * modPm + (double)fmv * modFm * (1.0 / 6.283185307179586); p -= floor_(p);
                float y;
                switch (q.wave) {
                    case W_SINE: { double x = p * kSineN; int k = (int)x; float fr = (float)(x - k); y = E_sine(k, fr); break; }
                    case W_TRI: y = (float)(4.0 * fabs_(p - 0.5) - 1.0); break;
                    case W_SAW: y = (float)(2.0 * p - 1.0) - polyBlep(p, inc); break;
                    case W_RSAW: y = -((float)(2.0 * p - 1.0) - polyBlep(p, inc)); break;
                    case W_SQUARE: case W_PULSE: { double w = q.wave == W_SQUARE ? 0.5 : pw; y = p < w ? 1.f : -1.f; double p2 = p + (1.0 - w); p2 -= floor_(p2); y += polyBlep(p, inc) - polyBlep(p2, inc); break; }
                    case W_NOISE: { o.ph[kMaxUni - 1] = o.ph[kMaxUni - 1]; v.rng = v.rng * 6364136223846793005ULL + 1442695040888963407ULL; y = (float)((v.rng >> 40) / 8388608.0) * 2.f - 1.f; break; }
                    default: { const WTable* tb = q.tab; if (!tb || !tb->data) { y = 0; break; }
                        int lvl2 = 0; { double hmax = (sr * 0.45) / (f0 * detF[i][u] + 1e-9); int half = tb->size / 2; double ratioH = half / (hmax < 1 ? 1 : hmax); while (lvl2 < tb->levels - 1 && (double)(half >> (lvl2)) > hmax) lvl2++; (void)ratioH; }
                        float fpos = tb->frames > 1 ? (pos < 0 ? 0 : (pos > 1 ? 1 : pos)) * (tb->frames - 1) : 0.f; int f0i = (int)fpos; float ff = fpos - f0i; int f1i = f0i + 1 < tb->frames ? f0i + 1 : f0i;
                        double x = p * tb->size; int k = (int)x; float fr = (float)(x - k); int mask = tb->size - 1; const float* a = tb->at(f0i, lvl2); float ya = a[k & mask] + (a[(k + 1) & mask] - a[k & mask]) * fr;
                        if (f1i != f0i) { const float* b = tb->at(f1i, lvl2); float yb = b[k & mask] + (b[(k + 1) & mask] - b[k & mask]) * fr; y = ya + (yb - ya) * ff; } else y = ya; }
                }
                sum += y; float a = y * lvl; float pp = pn + uPan[i][u]; if (pp < -1) pp = -1; if (pp > 1) pp = 1; float ang = (pp + 1.f) * 0.78539816339f; sumL += a * cosf_(ang); sumR += a * sinf_(ang);
            }
            float norm = q.unison > 1 ? 1.f / sqrt_((double)q.unison) : 1.f; sum *= norm; sumL *= norm; sumR *= norm; o.wrapped = wrapAny;
            float shape = 1.f;
            if (fold > 0.001f) { float x = sum * (1.f + fold); float t2 = x * 0.25f + 0.25f; t2 -= (float)floor_(t2); float fo = fabs_(t2 * 4.f - 2.f) - 1.f; shape = sum != 0.f ? fo / (sum == 0 ? 1.f : sum) : 1.f; sum = fo; sumL *= shape; sumR *= shape; }
            if (drv > 0.001f) { float g = 1.f + drv * 4.f; float x = sum * g; float yy = softclip(x) / sqrt_(g); float sh2 = sum != 0.f ? yy / sum : 1.f; sum = yy; sumL *= sh2; sumR *= sh2; }
            if (rg > 0.001f && q.ringsrc >= 1 && q.ringsrc <= v.nosc && q.ringsrc != i + 1) { float m = q.ringsrc - 1 < i ? outs[q.ringsrc - 1] : prevOuts[q.ringsrc - 1]; float k = 1.f - rg + rg * m; sum *= k; sumL *= k; sumR *= k; }
            if (am > 0.001f && q.amsrc >= 1 && q.amsrc <= v.nosc && q.amsrc != i + 1) { float m = q.amsrc - 1 < i ? outs[q.amsrc - 1] : prevOuts[q.amsrc - 1]; float k = 1.f - am + am * (0.5f + 0.5f * m); sum *= k; sumL *= k; sumR *= k; }
            outs[i] = sum; if (q.out) { mixL += sumL; mixR += sumR; }
        }
        for (int i = 0; i < v.nosc; i++) prevOuts[i] = outs[i];
        gvl += gdl; gvp += gdp; float ang = (gvp + 1.f) * 0.78539816339f; float gl = gvl * cosf_(ang) * 1.41421356f, gr = gvl * sinf_(ang) * 1.41421356f;
        float l = mixL * gl, r = mixR * gr; if (!(l == l) || l > 16 || l < -16) l = 0; if (!(r == r) || r > 16 || r < -16) r = 0; L[s] = l; R[s] = r; float aa = fabs_(l) > fabs_(r) ? fabs_(l) : fabs_(r); if (aa > pk) pk = aa;
        if ((s & 3) == 0) { v.scope[v.scopePos] = 0.5f * (l + r); v.scopePos = (v.scopePos + 1) % kScope; }
    }
    for (int i = 0; i < v.nosc; i++) { D& q = d[i]; Osc& o = v.osc[i]; o.prevLvl = q.level; o.prevRatio = q.ratio; o.prevPw = q.pw; o.prevPos = q.pos; o.prevFm = q.fm; o.prevPm = q.pm; o.prevRing = q.ring; o.prevAm = q.am; o.prevFold = q.fold; o.prevDrive = q.drive; o.prevPan = q.pan; o.out = outs[i]; }
    v.prevLevel = vlev; v.prevPan = vpan; v.peak = pk; v.t += n / (double)sr;
}
}  // namespace eng
}  // namespace lx
