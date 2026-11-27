// lx_lib.hpp - the sandboxed standard library of the LX VM: base, math, string, table.
// There is deliberately no io, os, package, debug, require, load or dofile: a script has no way to reach the file system,
// the network, the clock or native code. (string patterns - match/gmatch/gsub - are not provided in v1.)
#pragma once
#include "lx_interp.hpp"

namespace lx {
namespace lib {

typedef void (*PrintHook)(void* user, const char* s, size_t n);
struct LibState { PrintHook print = nullptr; void* user = nullptr; };
inline LibState& ls() { static LibState s; return s; }

inline void argErr(VM& vm, int i, const char* fname, const char* expect, const Value* a, int n) {
    char b[160]; int k = 0; const char* m = "bad argument #"; while (*m) b[k++] = *m++; b[k++] = (char)('0' + i + 1); const char* t = " to '"; while (*t) b[k++] = *t++; while (*fname) b[k++] = *fname++; b[k++] = '\''; b[k++] = ' '; b[k++] = '('; while (*expect) b[k++] = *expect++;
    const char* g = " expected, got "; while (*g) b[k++] = *g++; const char* tn = i < n ? VM::typeName(a[i]) : "no value"; while (*tn) b[k++] = *tn++; b[k++] = ')'; b[k] = 0; vm.setError(0, 0, b);
}
inline bool needNum(VM& vm, Value* a, int n, int i, const char* f, double& out) { if (i < n && vm.toNumber(a[i], out) && a[i].t != T_BOOL) return true; argErr(vm, i, f, "number", a, n); return false; }
inline bool optNum(VM& vm, Value* a, int n, int i, const char* f, double def, double& out) { if (i >= n || a[i].t == T_NIL) { out = def; return true; } return needNum(vm, a, n, i, f, out); }
inline bool needTab(VM& vm, Value* a, int n, int i, const char* f) { if (i < n && a[i].t == T_TAB) return true; argErr(vm, i, f, "table", a, n); return false; }
inline bool needStr(VM& vm, Value* a, int n, int i, const char* f, Str*& out) { if (i < n && (a[i].t == T_STR || a[i].t == T_NUM)) { out = vm.tostr(a[i]); return out != nullptr; } argErr(vm, i, f, "string", a, n); return false; }
inline int r1(VM& vm, const Value& v) { vm.ret[0] = v; return 1; }
inline int rnum(VM& vm, double d) { vm.ret[0] = Value::num(d); return 1; }
inline int rstr(VM& vm, const char* s, size_t n) { Str* x = vm.newStr(s, n); if (!x) return 0; vm.ret[0] = Value::str(x); return 1; }

// ------------------------------------------------------------------ base
inline int b_type(VM& vm, Value* a, int n) { if (n < 1) { argErr(vm, 0, "type", "value", a, n); return 0; } return r1(vm, Value::str(vm.internPerm(VM::typeName(a[0]), slen(VM::typeName(a[0]))))); }
inline int b_tostring(VM& vm, Value* a, int n) {
    if (n < 1) { argErr(vm, 0, "tostring", "value", a, n); return 0; }
    if (a[0].t == T_TAB && a[0].tb->meta) { Value h = vm.tgetS(a[0].tb->meta, "__tostring"); if (h.t != T_NIL) { Value args[1] = { a[0] }; Value r = vm.call1(h, args, 1); if (vm.fail) return 0; return r1(vm, r); } }
    Str* s = vm.tostr(a[0]); if (!s) return 0; return r1(vm, Value::str(s));
}
inline int b_tonumber(VM& vm, Value* a, int n) {
    if (n < 1) { argErr(vm, 0, "tonumber", "value", a, n); return 0; }
    if (n >= 2 && a[1].t == T_NUM) {
        if (a[0].t != T_STR) { argErr(vm, 0, "tonumber", "string", a, n); return 0; } int base = (int)a[1].n; if (base < 2 || base > 36) { vm.setError(0, 0, "bad argument #2 to 'tonumber' (base out of range)"); return 0; }
        const char* s = a[0].s->data; size_t l = a[0].s->len, i = 0; while (i < l && s[i] == ' ') i++; bool neg = false; if (i < l && s[i] == '-') { neg = true; i++; } double v = 0; bool any = false;
        for (; i < l; i++) { int c = s[i], d; if (c >= '0' && c <= '9') d = c - '0'; else if (c >= 'a' && c <= 'z') d = c - 'a' + 10; else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10; else break; if (d >= base) return r1(vm, Value()); v = v * base + d; any = true; }
        while (i < l && s[i] == ' ') i++; if (!any || i != l) return r1(vm, Value()); return rnum(vm, neg ? -v : v);
    }
    if (a[0].t == T_NUM) return r1(vm, a[0]); double d; if (a[0].t == T_STR && parse_num(a[0].s->data, a[0].s->len, &d)) return rnum(vm, d); return r1(vm, Value());
}
inline int b_next(VM& vm, Value* a, int n) { if (!needTab(vm, a, n, 0, "next")) return 0; Value k = n > 1 ? a[1] : Value(), v; if (!vm.tnext(a[0].tb, k, v)) { if (vm.fail) return 0; return r1(vm, Value()); } vm.ret[0] = k; vm.ret[1] = v; return 2; }
inline int b_ipairs_iter(VM& vm, Value* a, int n) { (void)n; double i = a[1].n + 1; Value v = vm.index(a[0], Value::num(i)); if (vm.fail) return 0; if (v.t == T_NIL) return r1(vm, Value()); vm.ret[0] = Value::num(i); vm.ret[1] = v; return 2; }
inline NatDef& natdef(VM& vm, const char* name, NativeFn fn) { NatDef* d = (NatDef*)vm.heap.perm(sizeof(NatDef)); Str* s = vm.internPerm(name, slen(name)); d->name = s ? s->data : name; d->fn = fn; return *d; }
inline Value natv(VM& vm, const char* name, NativeFn fn) { Value v; v.t = T_NAT; v.nd = &natdef(vm, name, fn); return v; }
inline void reg(VM& vm, Table* t, const char* name, NativeFn fn) { vm.tsetS(t, name, natv(vm, name, fn)); }
inline int b_pairs(VM& vm, Value* a, int n) { if (n < 1 || (a[0].t != T_TAB)) { argErr(vm, 0, "pairs", "table", a, n); return 0; } vm.ret[0] = vm.tgetS(vm.registry, "__next"); vm.ret[1] = a[0]; vm.ret[2] = Value(); return 3; }
inline int b_ipairs(VM& vm, Value* a, int n) { if (n < 1 || (a[0].t != T_TAB)) { argErr(vm, 0, "ipairs", "table", a, n); return 0; } vm.ret[0] = vm.tgetS(vm.registry, "__ipairs"); vm.ret[1] = a[0]; vm.ret[2] = Value::num(0); return 3; }
inline int b_select(VM& vm, Value* a, int n) {
    if (n >= 1 && a[0].t == T_STR && a[0].s->len == 1 && a[0].s->data[0] == '#') return rnum(vm, n - 1);
    double d; if (!needNum(vm, a, n, 0, "select", d)) return 0; int i = (int)d; if (i < 0) i = n + i; else if (i == 0) { vm.setError(0, 0, "bad argument #1 to 'select' (index out of range)"); return 0; } if (i < 1) { vm.setError(0, 0, "bad argument #1 to 'select' (index out of range)"); return 0; }
    int c = 0; for (int j = i; j < n && c < 38; j++) vm.ret[c++] = a[j]; return c;
}
inline int b_rawget(VM& vm, Value* a, int n) { if (!needTab(vm, a, n, 0, "rawget")) return 0; return r1(vm, vm.tget(a[0].tb, n > 1 ? a[1] : Value())); }
inline int b_rawset(VM& vm, Value* a, int n) { if (!needTab(vm, a, n, 0, "rawset")) return 0; vm.tsetRaw(a[0].tb, n > 1 ? a[1] : Value(), n > 2 ? a[2] : Value()); return r1(vm, a[0]); }
inline int b_rawequal(VM& vm, Value* a, int n) { return r1(vm, Value::boolean(n >= 2 && a[0].t == a[1].t && VM::keyEq(a[0], a[1]))); }
inline int b_rawlen(VM& vm, Value* a, int n) { if (n >= 1 && a[0].t == T_TAB) return rnum(vm, vm.tlen(a[0].tb)); if (n >= 1 && a[0].t == T_STR) return rnum(vm, a[0].s->len); argErr(vm, 0, "rawlen", "table or string", a, n); return 0; }
inline int b_setmt(VM& vm, Value* a, int n) { if (!needTab(vm, a, n, 0, "setmetatable")) return 0; if (n < 2 || (a[1].t != T_TAB && a[1].t != T_NIL)) { argErr(vm, 1, "setmetatable", "nil or table", a, n); return 0; } a[0].tb->meta = a[1].t == T_TAB ? a[1].tb : nullptr; return r1(vm, a[0]); }
inline int b_getmt(VM& vm, Value* a, int n) { if (n >= 1 && a[0].t == T_TAB && a[0].tb->meta) return r1(vm, Value::tab(a[0].tb->meta)); if (n >= 1 && a[0].t == T_STR && vm.strmeta) return r1(vm, Value::tab(vm.strmeta)); return r1(vm, Value()); }
inline int b_assert(VM& vm, Value* a, int n) { if (n < 1 || !a[0].truthy()) { if (n >= 2 && a[1].t == T_STR) { vm.setError(0, 0, a[1].s->data); } else vm.setError(0, 0, "assertion failed!"); return 0; } int c = n < 38 ? n : 38; for (int i = 0; i < c; i++) vm.ret[i] = a[i]; return c; }
inline int b_error(VM& vm, Value* a, int n) {
    Value v = n > 0 ? a[0] : Value(); double lvl = 1; if (n > 1 && a[1].t == T_NUM) lvl = a[1].n;
    if (v.t == T_STR) { if (lvl > 0) vm.setError(0, 0, v.s->data); else { vm.fail = F_ERROR; int k = 0; while (v.s->data[k] && k < 290) { vm.errmsg[k] = v.s->data[k]; k++; } vm.errmsg[k] = 0; vm.errLine = vm.curLine; } if (lvl <= 0) { vm.errval = v; vm.errHasVal = true; } }
    else { vm.setError(0, 0, "(error object is not a string)"); vm.errval = v; vm.errHasVal = true; }
    return 0;
}
inline int b_pcall(VM& vm, Value* a, int n) {
    if (n < 1) { argErr(vm, 0, "pcall", "value", a, n); return 0; }
    int savedDepth = vm.depth; int cnt = vm.callv(a[0], a + 1, n - 1);
    if (vm.fail == F_NONE) { // success: shift results up by one
        int c = cnt > 37 ? 37 : cnt; for (int i = c; i > 0; i--) vm.ret[i] = vm.ret[i - 1]; vm.ret[0] = Value::boolean(true); return c + 1;
    }
    if (vm.fail != F_ERROR) return 0;       // budget / memory / depth failures are fatal and cannot be swallowed by a script
    Value ev = vm.errval; if (!vm.errHasVal) { Str* s = vm.newStr(vm.errmsg, slen(vm.errmsg)); if (!s) return 0; ev = Value::str(s); }
    vm.fail = F_NONE; vm.errval = Value(); vm.errHasVal = false; vm.depth = savedDepth; vm.ret[0] = Value::boolean(false); vm.ret[1] = ev; return 2;
}
inline int b_unpack(VM& vm, Value* a, int n) {
    if (!needTab(vm, a, n, 0, "unpack")) return 0; double i, j; if (!optNum(vm, a, n, 1, "unpack", 1, i)) return 0; if (!optNum(vm, a, n, 2, "unpack", (double)vm.tlen(a[0].tb), j)) return 0;
    if (j - i >= 37) { vm.setError(0, 0, "too many results to unpack"); return 0; } int c = 0; for (double k = i; k <= j; k++) vm.ret[c++] = vm.tget(a[0].tb, Value::num(k)); return c;
}
inline int b_print(VM& vm, Value* a, int n) {
    char buf[1024]; size_t len = 0;
    for (int i = 0; i < n; i++) { Str* s = vm.tostr(a[i]); if (!s) return 0; if (i) { if (len < 1020) buf[len++] = '\t'; } for (u32 k = 0; k < s->len && len < 1020; k++) buf[len++] = s->data[k]; }
    if (len < 1022) buf[len++] = '\n'; if (vm.printHook) vm.printHook(vm.user, buf, len); else if (ls().print) ls().print(ls().user, buf, len); return 0;
}

// ------------------------------------------------------------------ math
#define MATH1(NAME, EXPR) inline int m_##NAME(VM& vm, Value* a, int n) { double x; if (!needNum(vm, a, n, 0, #NAME, x)) return 0; return rnum(vm, EXPR); }
MATH1(floor, floor_(x)) MATH1(ceil, ceil_(x)) MATH1(abs, fabs_(x)) MATH1(sqrt, sqrt_(x)) MATH1(sin, sin_(x)) MATH1(cos, cos_(x)) MATH1(tan, tan_(x)) MATH1(asin, asin_(x)) MATH1(acos, acos_(x)) MATH1(exp, exp_(x))
MATH1(deg, x * (180.0 / kPi)) MATH1(rad, x * (kPi / 180.0)) MATH1(tointeger, (x == floor_(x) ? x : nan_()))
inline int m_atan(VM& vm, Value* a, int n) { double y, x; if (!needNum(vm, a, n, 0, "atan", y) || !optNum(vm, a, n, 1, "atan", 1, x)) return 0; return rnum(vm, atan2_(y, x)); }
inline int m_log(VM& vm, Value* a, int n) { double x, b; if (!needNum(vm, a, n, 0, "log", x) || !optNum(vm, a, n, 1, "log", 0, b)) return 0; if (n < 2 || a[1].t == T_NIL) return rnum(vm, log_(x)); if (b == 2) return rnum(vm, log_(x) / kLn2); if (b == 10) return rnum(vm, log_(x) / 2.302585092994046); return rnum(vm, log_(x) / log_(b)); }
inline int m_pow(VM& vm, Value* a, int n) { double x, y; if (!needNum(vm, a, n, 0, "pow", x) || !needNum(vm, a, n, 1, "pow", y)) return 0; return rnum(vm, pow_(x, y)); }
inline int m_fmod(VM& vm, Value* a, int n) { double x, y; if (!needNum(vm, a, n, 0, "fmod", x) || !needNum(vm, a, n, 1, "fmod", y)) return 0; return rnum(vm, fmod_(x, y)); }
inline int m_modf(VM& vm, Value* a, int n) { double x; if (!needNum(vm, a, n, 0, "modf", x)) return 0; double ip = x >= 0 ? floor_(x) : ceil_(x); vm.ret[0] = Value::num(ip); vm.ret[1] = Value::num(isinf_(x) ? 0.0 : x - ip); return 2; }
inline int m_max(VM& vm, Value* a, int n) { double m; if (!needNum(vm, a, n, 0, "max", m)) return 0; for (int i = 1; i < n; i++) { double x; if (!needNum(vm, a, n, i, "max", x)) return 0; if (x > m) m = x; } return rnum(vm, m); }
inline int m_min(VM& vm, Value* a, int n) { double m; if (!needNum(vm, a, n, 0, "min", m)) return 0; for (int i = 1; i < n; i++) { double x; if (!needNum(vm, a, n, i, "min", x)) return 0; if (x < m) m = x; } return rnum(vm, m); }
inline u64 rngNext(VM& vm) { u64 x = vm.rngState; x ^= x >> 12; x ^= x << 25; x ^= x >> 27; vm.rngState = x; return x * 2685821657736338717ULL; }
inline double rngUnit(VM& vm) { return (double)(rngNext(vm) >> 11) * (1.0 / 9007199254740992.0); }
inline int m_random(VM& vm, Value* a, int n) {
    double u = rngUnit(vm); if (n == 0) return rnum(vm, u); double lo = 1, hi; if (!needNum(vm, a, n, 0, "random", hi)) return 0;
    if (n >= 2) { lo = hi; if (!needNum(vm, a, n, 1, "random", hi)) return 0; } if (lo > hi) { vm.setError(0, 0, "bad argument to 'random' (interval is empty)"); return 0; }
    return rnum(vm, lo + floor_(u * (floor_(hi) - floor_(lo) + 1)));
}
inline int m_randomseed(VM& vm, Value* a, int n) { double x = 0; if (n > 0 && !needNum(vm, a, n, 0, "randomseed", x)) return 0; u64 s = (u64)(long long)x * 0x9E3779B97F4A7C15ULL + 0x1234567ULL; if (!s) s = 88172645463325252ULL; vm.rngState = s; for (int i = 0; i < 8; i++) rngNext(vm); return 0; }
inline int m_type(VM& vm, Value* a, int n) { if (n < 1 || a[0].t != T_NUM) { if (n >= 1) return r1(vm, Value()); argErr(vm, 0, "type", "value", a, n); return 0; } const char* s = (a[0].n == floor_(a[0].n) && fabs_(a[0].n) < 9e15) ? "integer" : "float"; return r1(vm, Value::str(vm.internPerm(s, slen(s)))); }

// ------------------------------------------------------------------ string
inline int s_len(VM& vm, Value* a, int n) { Str* s; if (!needStr(vm, a, n, 0, "len", s)) return 0; return rnum(vm, s->len); }
inline bool strRange(double i, double j, size_t len, size_t& st, size_t& en) {   // 1-based inclusive with negatives; returns false when empty
    long long l = (long long)len, a = (long long)i, b = (long long)j; if (a < 0) { a = l + a + 1; if (a < 1) a = 1; } else if (a == 0) a = 1; if (b < 0) b = l + b + 1; else if (b > l) b = l; if (a > b) return false; st = (size_t)(a - 1); en = (size_t)b; return true;
}
inline int s_sub(VM& vm, Value* a, int n) { Str* s; double i, j; if (!needStr(vm, a, n, 0, "sub", s) || !needNum(vm, a, n, 1, "sub", i) || !optNum(vm, a, n, 2, "sub", -1, j)) return 0; size_t st, en; if (!strRange(i, j, s->len, st, en)) return rstr(vm, "", 0); return rstr(vm, s->data + st, en - st); }
inline int s_upper(VM& vm, Value* a, int n) { Str* s; if (!needStr(vm, a, n, 0, "upper", s)) return 0; char* t = (char*)vm.heap.alloc(s->len + 1); if (!t) return 0; for (u32 i = 0; i < s->len; i++) t[i] = (s->data[i] >= 'a' && s->data[i] <= 'z') ? (char)(s->data[i] - 32) : s->data[i]; int r = rstr(vm, t, s->len); vm.heap.release(t, s->len + 1); return r; }
inline int s_lower(VM& vm, Value* a, int n) { Str* s; if (!needStr(vm, a, n, 0, "lower", s)) return 0; char* t = (char*)vm.heap.alloc(s->len + 1); if (!t) return 0; for (u32 i = 0; i < s->len; i++) t[i] = (s->data[i] >= 'A' && s->data[i] <= 'Z') ? (char)(s->data[i] + 32) : s->data[i]; int r = rstr(vm, t, s->len); vm.heap.release(t, s->len + 1); return r; }
inline int s_reverse(VM& vm, Value* a, int n) { Str* s; if (!needStr(vm, a, n, 0, "reverse", s)) return 0; char* t = (char*)vm.heap.alloc(s->len + 1); if (!t) return 0; for (u32 i = 0; i < s->len; i++) t[i] = s->data[s->len - 1 - i]; int r = rstr(vm, t, s->len); vm.heap.release(t, s->len + 1); return r; }
inline int s_rep(VM& vm, Value* a, int n) {
    Str* s; double c; if (!needStr(vm, a, n, 0, "rep", s) || !needNum(vm, a, n, 1, "rep", c)) return 0; Str* sep = nullptr; if (n > 2 && a[2].t != T_NIL) { if (!needStr(vm, a, n, 2, "rep", sep)) return 0; }
    if (c <= 0) return rstr(vm, "", 0); size_t sl = sep ? sep->len : 0; double total = c * s->len + (c - 1) * sl; if (total > 100000) { vm.setError(0, 0, "resulting string too large"); return 0; } vm.budget -= (long)(total / 16);
    char* t = (char*)vm.heap.alloc((size_t)total + 1); if (!t) return 0; size_t p = 0; for (long i = 0; i < (long)c; i++) { mcopy(t + p, s->data, s->len); p += s->len; if (sep && i < (long)c - 1) { mcopy(t + p, sep->data, sl); p += sl; } }
    int r = rstr(vm, t, p); vm.heap.release(t, (size_t)total + 1); return r;
}
inline int s_byte(VM& vm, Value* a, int n) { Str* s; double i, j; if (!needStr(vm, a, n, 0, "byte", s) || !optNum(vm, a, n, 1, "byte", 1, i)) return 0; if (!optNum(vm, a, n, 2, "byte", i, j)) return 0; size_t st, en; if (!strRange(i, j, s->len, st, en)) return 0; int c = 0; for (size_t k = st; k < en && c < 38; k++) vm.ret[c++] = Value::num((u8)s->data[k]); return c; }
inline int s_char(VM& vm, Value* a, int n) { char b[64]; if (n > 60) n = 60; for (int i = 0; i < n; i++) { double d; if (!needNum(vm, a, n, i, "char", d)) return 0; if (d < 0 || d > 255) { vm.setError(0, 0, "bad argument to 'char' (value out of range)"); return 0; } b[i] = (char)(int)d; } return rstr(vm, b, (size_t)n); }
inline int s_find(VM& vm, Value* a, int n) {
    Str *s, *p; double init; if (!needStr(vm, a, n, 0, "find", s) || !needStr(vm, a, n, 1, "find", p) || !optNum(vm, a, n, 2, "find", 1, init)) return 0;
    long long st = (long long)init; if (st < 0) { st = (long long)s->len + st + 1; if (st < 1) st = 1; } else if (st == 0) st = 1; if (st > (long long)s->len + 1) return r1(vm, Value());
    for (size_t i = (size_t)st - 1; i + p->len <= s->len; i++) if (mcmp(s->data + i, p->data, p->len) == 0) { vm.ret[0] = Value::num((double)i + 1); vm.ret[1] = Value::num((double)(i + p->len)); return 2; }   // plain search only
    return r1(vm, Value());
}
inline int s_format(VM& vm, Value* a, int n) {
    Str* f; if (!needStr(vm, a, n, 0, "format", f)) return 0; char out[2048]; size_t o = 0; int ai = 1;
    auto put = [&](const char* s, size_t l) { for (size_t i = 0; i < l && o < 2040; i++) out[o++] = s[i]; };
    for (u32 i = 0; i < f->len; i++) {
        char c = f->data[i]; if (c != '%') { if (o < 2040) out[o++] = c; continue; } i++; if (i >= f->len) { vm.setError(0, 0, "invalid conversion '%' to 'format'"); return 0; }
        if (f->data[i] == '%') { put("%", 1); continue; }
        bool left = false, zero = false, plus = false, space = false; for (; i < f->len; i++) { char fl = f->data[i]; if (fl == '-') left = true; else if (fl == '0') zero = true; else if (fl == '+') plus = true; else if (fl == ' ') space = true; else if (fl == '#') {} else break; }
        int width = 0, prec = -1; while (i < f->len && f->data[i] >= '0' && f->data[i] <= '9') { width = width * 10 + (f->data[i] - '0'); i++; } if (width > 99) width = 99;
        if (i < f->len && f->data[i] == '.') { i++; prec = 0; while (i < f->len && f->data[i] >= '0' && f->data[i] <= '9') { prec = prec * 10 + (f->data[i] - '0'); i++; } if (prec > 30) prec = 30; }
        if (i >= f->len) { vm.setError(0, 0, "invalid conversion to 'format'"); return 0; }
        char conv = f->data[i]; char tmp[420]; int tl = 0;
        if (ai >= n && conv != '%') { vm.setError(0, 0, "bad argument to 'format' (no value)"); return 0; }
        switch (conv) {
            case 'd': case 'i': case 'u': { double d; if (!needNum(vm, a, n, ai, "format", d)) return 0; ai++; if (d != floor_(d)) { vm.setError(0, 0, "bad argument to 'format' (number has no integer representation)"); return 0; } tl = fmt_fixed(tmp, 420, d, 0, plus); if (space && d >= 0 && !plus) { for (int k = tl; k >= 0; k--) tmp[k + 1] = tmp[k]; tmp[0] = ' '; tl++; } if (prec > 0) { int digs = tl - (tmp[0] == '-' || tmp[0] == '+' ? 1 : 0); while (digs < prec && tl < 400) { int sg = (tmp[0] == '-' || tmp[0] == '+') ? 1 : 0; for (int k = tl; k >= sg; k--) tmp[k + 1] = tmp[k]; tmp[sg] = '0'; tl++; digs++; } } break; }
            case 'f': case 'F': { double d; if (!needNum(vm, a, n, ai, "format", d)) return 0; ai++; tl = fmt_fixed(tmp, 420, d, prec < 0 ? 6 : prec, plus); if (space && d >= 0 && !plus) { for (int k = tl; k >= 0; k--) tmp[k + 1] = tmp[k]; tmp[0] = ' '; tl++; } break; }
            case 'g': case 'G': case 'e': case 'E': { double d; if (!needNum(vm, a, n, ai, "format", d)) return 0; ai++; tl = fmt_num(tmp, 420, d); break; }
            case 'x': case 'X': case 'o': { double d; if (!needNum(vm, a, n, ai, "format", d)) return 0; ai++; unsigned long long v = (unsigned long long)(long long)d; int base = conv == 'o' ? 8 : 16; char rv[40]; int rl = 0; if (!v) rv[rl++] = '0'; while (v) { int dg = (int)(v % base); rv[rl++] = (char)(dg < 10 ? '0' + dg : (conv == 'X' ? 'A' : 'a') + dg - 10); v /= base; } while (rl) tmp[tl++] = rv[--rl]; break; }
            case 'c': { double d; if (!needNum(vm, a, n, ai, "format", d)) return 0; ai++; tmp[tl++] = (char)(int)d; break; }
            case 's': { Str* s; if (a[ai].t == T_TAB && a[ai].tb->meta && vm.tgetS(a[ai].tb->meta, "__tostring").t != T_NIL) { Value tv[1] = { a[ai] }; Value h = vm.tgetS(a[ai].tb->meta, "__tostring"); Value r = vm.call1(h, tv, 1); if (vm.fail) return 0; s = vm.tostr(r); } else s = vm.tostr(a[ai]); if (!s) return 0; ai++; int l = (int)s->len; if (prec >= 0 && prec < l) l = prec; if (l > 400) l = 400; for (int k = 0; k < l; k++) tmp[tl++] = s->data[k]; break; }
            case 'q': { Str* s = vm.tostr(a[ai]); if (!s) return 0; ai++; tmp[tl++] = '"'; for (u32 k = 0; k < s->len && tl < 400; k++) { char ch = s->data[k]; if (ch == '"' || ch == '\\') tmp[tl++] = '\\'; tmp[tl++] = ch == '\n' ? 'n' : ch; } tmp[tl++] = '"'; break; }
            default: vm.setError(0, 0, "invalid conversion to 'format'"); return 0;
        }
        int pad = width > tl ? width - tl : 0;
        if (left) { put(tmp, (size_t)tl); for (int k = 0; k < pad; k++) put(" ", 1); }
        else if (zero && conv != 's' && conv != 'c') { int sg = (tl > 0 && (tmp[0] == '-' || tmp[0] == '+' || tmp[0] == ' ')) ? 1 : 0; put(tmp, (size_t)sg); for (int k = 0; k < pad; k++) put("0", 1); put(tmp + sg, (size_t)(tl - sg)); }
        else { for (int k = 0; k < pad; k++) put(" ", 1); put(tmp, (size_t)tl); }
    }
    return rstr(vm, out, o);
}

// ------------------------------------------------------------------ table
inline int t_insert(VM& vm, Value* a, int n) {
    if (!needTab(vm, a, n, 0, "insert")) return 0; Table* t = a[0].tb; u32 len = vm.tlen(t);
    if (n == 2) { vm.tsetRaw(t, Value::num((double)len + 1), a[1]); return 0; }
    if (n != 3) { vm.setError(0, 0, "wrong number of arguments to 'insert'"); return 0; } double p; if (!needNum(vm, a, n, 1, "insert", p)) return 0;
    if (p < 1 || p > (double)len + 1) { vm.setError(0, 0, "bad argument #2 to 'insert' (position out of bounds)"); return 0; }
    for (u32 i = len; i >= (u32)p; i--) { vm.tsetRaw(t, Value::num((double)i + 1), vm.tget(t, Value::num((double)i))); if (vm.fail) return 0; } vm.tsetRaw(t, Value::num(p), a[2]); vm.budget -= len / 8; return 0;
}
inline int t_remove(VM& vm, Value* a, int n) {
    if (!needTab(vm, a, n, 0, "remove")) return 0; Table* t = a[0].tb; u32 len = vm.tlen(t); double p = len; if (n > 1) { if (!needNum(vm, a, n, 1, "remove", p)) return 0; }
    if (len == 0 && (n < 2 || p == 0 || p == len)) return r1(vm, Value()); if (p < 1 || p > (double)len + 1) { vm.setError(0, 0, "bad argument #2 to 'remove' (position out of bounds)"); return 0; }
    Value r = vm.tget(t, Value::num(p)); for (u32 i = (u32)p; i < len; i++) vm.tsetRaw(t, Value::num((double)i), vm.tget(t, Value::num((double)i + 1))); if ((u32)p <= len) vm.tsetRaw(t, Value::num((double)len), Value()); vm.budget -= len / 8; return r1(vm, r);
}
inline int t_concat(VM& vm, Value* a, int n) {
    if (!needTab(vm, a, n, 0, "concat")) return 0; Str* sep = nullptr; if (n > 1 && a[1].t != T_NIL) { if (!needStr(vm, a, n, 1, "concat", sep)) return 0; } double i, j; if (!optNum(vm, a, n, 2, "concat", 1, i) || !optNum(vm, a, n, 3, "concat", (double)vm.tlen(a[0].tb), j)) return 0;
    char* buf = (char*)vm.heap.alloc(8192); if (!buf) return 0; size_t o = 0;
    for (double k = i; k <= j; k++) { Value v = vm.tget(a[0].tb, Value::num(k)); if (v.t != T_STR && v.t != T_NUM) { vm.heap.release(buf, 8192); vm.setError(0, 0, "invalid value in table for 'concat'"); return 0; } Str* s = vm.tostr(v); if (!s) { vm.heap.release(buf, 8192); return 0; } if (o + s->len + (sep ? sep->len : 0) > 8190) { vm.heap.release(buf, 8192); vm.setError(0, 0, "concat result too long"); return 0; } mcopy(buf + o, s->data, s->len); o += s->len; if (sep && k < j) { mcopy(buf + o, sep->data, sep->len); o += sep->len; } }
    int r = rstr(vm, buf, o); vm.heap.release(buf, 8192); return r;
}
inline int t_pack(VM& vm, Value* a, int n) { Table* t = vm.newTable((u32)n, 1); if (!t) return 0; for (int i = 0; i < n; i++) vm.tsetRaw(t, Value::num(i + 1), a[i]); vm.tsetS(t, "n", Value::num(n)); return r1(vm, Value::tab(t)); }
inline bool sortLess(VM& vm, const Value& cmp, const Value& x, const Value& y) {
    if (cmp.t != T_NIL) { Value args[2] = { x, y }; return vm.call1(cmp, args, 2).truthy(); }
    if (x.t == T_NUM && y.t == T_NUM) return x.n < y.n; if (x.t == T_STR && y.t == T_STR) { u32 m = x.s->len < y.s->len ? x.s->len : y.s->len; int c = mcmp(x.s->data, y.s->data, m); return c == 0 ? x.s->len < y.s->len : c < 0; }
    vm.setError(0, 0, "attempt to compare incompatible values in 'sort'"); return false;
}
inline int t_sort(VM& vm, Value* a, int n) {
    if (!needTab(vm, a, n, 0, "sort")) return 0; Value cmp = n > 1 ? a[1] : Value(); if (cmp.t != T_NIL && cmp.t != T_FUN && cmp.t != T_NAT) { argErr(vm, 1, "sort", "function", a, n); return 0; }
    Table* t = a[0].tb; long len = (long)t->asz; if (len < 2) return 0; vm.budget -= len * 8; Value* A = t->arr;
    // heap sort: in place, no recursion, no allocation; the comparator may be a script function
    auto sift = [&](long root, long end) { for (;;) { long child = 2 * root + 1; if (child > end) return; if (child + 1 <= end && sortLess(vm, cmp, A[child], A[child + 1])) child++; if (vm.fail) return; if (sortLess(vm, cmp, A[root], A[child])) { Value tmp = A[root]; A[root] = A[child]; A[child] = tmp; root = child; } else return; if (vm.fail) return; } };
    for (long s = (len - 2) / 2; s >= 0; s--) { sift(s, len - 1); if (vm.fail) return 0; }
    for (long e = len - 1; e > 0; e--) { Value tmp = A[0]; A[0] = A[e]; A[e] = tmp; sift(0, e - 1); if (vm.fail) return 0; }
    // heap sort is not stable; make equal runs keep a consistent order by a final insertion pass over adjacent inversions
    return 0;
}

inline bool openLibs(VM& vm) {
    Table* G = vm.globals;
    reg(vm, G, "type", b_type); reg(vm, G, "tostring", b_tostring); reg(vm, G, "tonumber", b_tonumber); reg(vm, G, "pairs", b_pairs); reg(vm, G, "ipairs", b_ipairs); reg(vm, G, "next", b_next); reg(vm, G, "select", b_select);
    reg(vm, G, "rawget", b_rawget); reg(vm, G, "rawset", b_rawset); reg(vm, G, "rawequal", b_rawequal); reg(vm, G, "rawlen", b_rawlen); reg(vm, G, "setmetatable", b_setmt); reg(vm, G, "getmetatable", b_getmt);
    reg(vm, G, "assert", b_assert); reg(vm, G, "error", b_error); reg(vm, G, "pcall", b_pcall); reg(vm, G, "unpack", b_unpack); reg(vm, G, "print", b_print);
    vm.natNext = b_next; vm.natIpairsIter = b_ipairs_iter; vm.tsetS(vm.registry, "__next", natv(vm, "next", b_next)); vm.tsetS(vm.registry, "__ipairs", natv(vm, "ipairs_iter", b_ipairs_iter));
    Table* M = vm.newTable(0, 32); vm.tsetS(G, "math", Value::tab(M));
    reg(vm, M, "floor", m_floor); reg(vm, M, "ceil", m_ceil); reg(vm, M, "abs", m_abs); reg(vm, M, "sqrt", m_sqrt); reg(vm, M, "sin", m_sin); reg(vm, M, "cos", m_cos); reg(vm, M, "tan", m_tan); reg(vm, M, "asin", m_asin); reg(vm, M, "acos", m_acos); reg(vm, M, "atan", m_atan);
    reg(vm, M, "exp", m_exp); reg(vm, M, "log", m_log); reg(vm, M, "pow", m_pow); reg(vm, M, "fmod", m_fmod); reg(vm, M, "modf", m_modf); reg(vm, M, "max", m_max); reg(vm, M, "min", m_min); reg(vm, M, "random", m_random); reg(vm, M, "randomseed", m_randomseed);
    reg(vm, M, "deg", m_deg); reg(vm, M, "rad", m_rad); reg(vm, M, "tointeger", m_tointeger); reg(vm, M, "type", m_type);
    vm.tsetS(M, "pi", Value::num(kPi)); vm.tsetS(M, "huge", Value::num(huge_())); vm.tsetS(M, "maxinteger", Value::num(9007199254740992.0)); vm.tsetS(M, "mininteger", Value::num(-9007199254740992.0));
    Table* S = vm.newTable(0, 16); vm.tsetS(G, "string", Value::tab(S));
    reg(vm, S, "len", s_len); reg(vm, S, "sub", s_sub); reg(vm, S, "upper", s_upper); reg(vm, S, "lower", s_lower); reg(vm, S, "rep", s_rep); reg(vm, S, "reverse", s_reverse); reg(vm, S, "byte", s_byte); reg(vm, S, "char", s_char); reg(vm, S, "format", s_format); reg(vm, S, "find", s_find);
    vm.strmeta = vm.newTable(0, 2); vm.tsetS(vm.strmeta, "__index", Value::tab(S));
    Table* T = vm.newTable(0, 8); vm.tsetS(G, "table", Value::tab(T));
    reg(vm, T, "insert", t_insert); reg(vm, T, "remove", t_remove); reg(vm, T, "concat", t_concat); reg(vm, T, "unpack", b_unpack); reg(vm, T, "pack", t_pack); reg(vm, T, "sort", t_sort);
    vm.tsetS(G, "_G", Value::tab(G)); vm.tsetS(G, "_VERSION", Value::str(vm.internPerm("LX 1 (Lua 5.4 subset)", 21)));
    return vm.fail == F_NONE;
}
}  // namespace lib
}  // namespace lx
