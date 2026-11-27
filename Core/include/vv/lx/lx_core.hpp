// lx_core.hpp - freestanding foundations for the Vektur Lua-subset VM ("LX"):
// fixed-pool allocator (no malloc, so it is safe on an audio thread), deterministic maths,
// number <-> text conversion. No libc, no STL: the same file builds for WebAssembly and native.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace lx {
struct PlaceTag {};
}
inline void* operator new(size_t, void* p, lx::PlaceTag) noexcept { return p; }
namespace lx {
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t i32; typedef uint64_t u64; typedef int64_t i64;

inline void mcopy(void* d, const void* s, size_t n) { u8* a = (u8*)d; const u8* b = (const u8*)s; for (size_t i = 0; i < n; i++) a[i] = b[i]; }
inline void mmove(void* d, const void* s, size_t n) { u8* a = (u8*)d; const u8* b = (const u8*)s; if (a < b) for (size_t i = 0; i < n; i++) a[i] = b[i]; else for (size_t i = n; i > 0; i--) a[i - 1] = b[i - 1]; }
inline void mzero(void* d, size_t n) { u8* a = (u8*)d; for (size_t i = 0; i < n; i++) a[i] = 0; }
inline int mcmp(const void* x, const void* y, size_t n) { const u8* a = (const u8*)x; const u8* b = (const u8*)y; for (size_t i = 0; i < n; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1; return 0; }
inline size_t slen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }

// ---------------------------------------------------------------- deterministic maths
constexpr double kPi = 3.14159265358979323846, kLn2 = 0.693147180559945309417;
inline double fabs_(double x) { return __builtin_fabs(x); }
inline double floor_(double x) { return __builtin_floor(x); }
inline double ceil_(double x) { return __builtin_ceil(x); }
inline double sqrt_(double x) { return __builtin_sqrt(x); }
inline double trunc_(double x) { return __builtin_trunc(x); }
inline bool isnan_(double x) { return x != x; }
inline bool isinf_(double x) { return x - x != 0 && x == x; }
inline double huge_() { return __builtin_huge_val(); }
inline double nan_() { return __builtin_nan(""); }

inline double exp_(double x) {
    if (x != x) return x; if (x > 709.78) return huge_(); if (x < -745.0) return 0.0;
    double kf = floor_(x / kLn2 + 0.5); double r = x - kf * 0.693147180369123816490 - kf * 1.90821492927058770002e-10;   // Cody-Waite split of ln2
    double t = 1.0, s = 1.0; for (int i = 1; i < 18; i++) { t *= r / i; s += t; }
    int k = (int)kf; u64 bits; double sc; if (k > 1023) { s *= 8.98846567431158e307; k -= 1023; } if (k < -1022) { s *= 2.2250738585072014e-308; k += 1022; }
    bits = (u64)(k + 1023) << 52; __builtin_memcpy(&sc, &bits, 8); return s * sc;
}
inline double log_(double x) {
    if (x != x || x < 0) return nan_(); if (x == 0) return -huge_(); if (isinf_(x)) return x;
    u64 b; __builtin_memcpy(&b, &x, 8); int e = (int)((b >> 52) & 0x7ff); if (e == 0) { x *= 18014398509481984.0; __builtin_memcpy(&b, &x, 8); e = (int)((b >> 52) & 0x7ff) - 54; }
    e -= 1023; b = (b & 0x000fffffffffffffULL) | 0x3ff0000000000000ULL; double m; __builtin_memcpy(&m, &b, 8);
    if (m > 1.4142135623730951) { m *= 0.5; e++; }
    double f = (m - 1.0) / (m + 1.0), f2 = f * f, t = f, s = 0; for (int i = 1; i < 40; i += 2) { s += t / i; t *= f2; }
    return 2.0 * s + e * kLn2;
}
inline double sincos_core(double r, bool cosine) {     // r in [-pi/4, pi/4]
    double r2 = r * r, t, s;
    if (!cosine) { t = r; s = r; for (int i = 1; i < 12; i++) { t *= -r2 / ((2 * i) * (2 * i + 1)); s += t; } return s; }
    t = 1; s = 1; for (int i = 1; i < 12; i++) { t *= -r2 / ((2 * i - 1) * (2 * i)); s += t; } return s;
}
inline void sincos_(double x, double* sn, double* cs) {
    if (x != x || isinf_(x)) { *sn = *cs = nan_(); return; }
    double q = floor_(x * (2.0 / kPi) + 0.5); double r = x - q * 1.57079632679489655800 - q * 6.12323399573676603587e-17;
    if (fabs_(q) > 1e8) { r = x - kPi * 2 * floor_(x / (kPi * 2)); q = 0; if (r > kPi) r -= 2 * kPi; }
    double s = sincos_core(r, false), c = sincos_core(r, true); int qi = (int)(q - 4.0 * floor_(q / 4.0));
    switch (qi) { case 0: *sn = s; *cs = c; break; case 1: *sn = c; *cs = -s; break; case 2: *sn = -s; *cs = -c; break; default: *sn = -c; *cs = s; }
}
inline double sin_(double x) { double s, c; sincos_(x, &s, &c); return s; }
inline double cos_(double x) { double s, c; sincos_(x, &s, &c); return c; }
inline double tan_(double x) { double s, c; sincos_(x, &s, &c); return s / c; }
inline double atan_(double x) {
    if (x != x) return x; double sg = x < 0 ? -1 : 1; x = fabs_(x); if (isinf_(x)) return sg * kPi / 2;
    bool inv = x > 1; if (inv) x = 1 / x; int red = 0; while (x > 0.2679491924311227) { x = (x - 0.5773502691896258) / (1 + x * 0.5773502691896258); red++; if (red > 1) break; }   // tan(pi/6) reductions
    double x2 = x * x, t = x, s = 0; for (int i = 1; i < 60; i += 2) { s += t / i * ((i / 2) % 2 ? -1 : 1); t *= x2; }
    s += red * (kPi / 6); if (inv) s = kPi / 2 - s; return sg * s;
}
inline double atan2_(double y, double x) {
    if (x != x || y != y) return nan_(); if (x > 0) return atan_(y / x); if (x < 0) return y >= 0 ? atan_(y / x) + kPi : atan_(y / x) - kPi; return y > 0 ? kPi / 2 : (y < 0 ? -kPi / 2 : 0);
}
inline double asin_(double x) { if (x > 1 || x < -1) return nan_(); if (x == 1) return kPi / 2; if (x == -1) return -kPi / 2; return atan_(x / sqrt_(1 - x * x)); }
inline double acos_(double x) { if (x > 1 || x < -1) return nan_(); return kPi / 2 - asin_(x); }
inline double fmod_(double a, double b) { if (b == 0 || isinf_(a) || a != a || b != b) return nan_(); if (isinf_(b)) return a; double r = a - trunc_(a / b) * b; if (fabs_(a) > 4e15) { r = a; while (fabs_(r) >= fabs_(b)) { double q = trunc_(r / b); r -= q * b; if (q == 0) break; } } return r; }
inline double pow_(double x, double y) {
    if (y == 0) return 1; if (x == 1) return 1; if (x != x || y != y) return nan_();
    if (y == floor_(y) && fabs_(y) < 1024) { double r = 1, b = x; long long n = (long long)fabs_(y); while (n) { if (n & 1) r *= b; b *= b; n >>= 1; } return y < 0 ? 1.0 / r : r; }
    if (x < 0) return nan_(); if (x == 0) return y > 0 ? 0 : huge_(); return exp_(y * log_(x));
}

// ---------------------------------------------------------------- numbers <-> text
// Writes up to cap-1 chars + NUL, returns length. Integral values print without ".0" (all LX numbers are doubles).
inline int fmt_fixed(char* out, int cap, double v, int prec, bool force_sign = false);
inline int fmt_num(char* out, int cap, double v) {      // like "%.14g"
    if (v != v) { const char* s = v < 0 ? "-nan" : "nan"; int n = 0; while (s[n] && n < cap - 1) { out[n] = s[n]; n++; } out[n] = 0; return n; }
    if (isinf_(v)) { const char* s = v < 0 ? "-inf" : "inf"; int n = 0; while (s[n] && n < cap - 1) { out[n] = s[n]; n++; } out[n] = 0; return n; }
    char tmp[48]; int n = 0; if (v == 0) { out[0] = '0'; out[1] = 0; return 1; }
    bool neg = v < 0; if (neg) v = -v;
    int e10 = (int)floor_(log_(v) / 2.302585092994046); double sc = pow_(10.0, 13 - e10); double m = v * sc; if (13 - e10 > 300) { m = (v * 1e300) * pow_(10.0, 13 - e10 - 300); }
    double r = floor_(m + 0.5); if (r >= 1e14) { r = floor_(r / 10 + 0.5); e10++; } else if (r < 1e13) { e10--; m = v * pow_(10.0, 13 - e10); r = floor_(m + 0.5); }
    u64 mant = (u64)r; char dg[16]; for (int i = 13; i >= 0; i--) { dg[i] = (char)('0' + mant % 10); mant /= 10; }
    int nd = 14; while (nd > 1 && dg[nd - 1] == '0') nd--;
    if (neg) tmp[n++] = '-';
    if (e10 < -5 || e10 >= 14) {       // exponent form
        tmp[n++] = dg[0]; if (nd > 1) { tmp[n++] = '.'; for (int i = 1; i < nd; i++) tmp[n++] = dg[i]; }
        tmp[n++] = 'e'; int ee = e10; if (ee < 0) { tmp[n++] = '-'; ee = -ee; } else tmp[n++] = '+'; if (ee < 10) tmp[n++] = '0'; char eb[8]; int en = 0; while (ee) { eb[en++] = (char)('0' + ee % 10); ee /= 10; } while (en) tmp[n++] = eb[--en];
    } else if (e10 >= 0) {
        for (int i = 0; i <= e10; i++) tmp[n++] = i < nd ? dg[i] : '0';
        if (nd > e10 + 1) { tmp[n++] = '.'; for (int i = e10 + 1; i < nd; i++) tmp[n++] = dg[i]; }
    } else { tmp[n++] = '0'; tmp[n++] = '.'; for (int i = 0; i < -e10 - 1; i++) tmp[n++] = '0'; for (int i = 0; i < nd; i++) tmp[n++] = dg[i]; }
    if (n > cap - 1) n = cap - 1; for (int i = 0; i < n; i++) out[i] = tmp[i]; out[n] = 0; return n;
}
inline int fmt_fixed(char* out, int cap, double v, int prec, bool force_sign) {   // like "%.<prec>f"
    char tmp[400]; int n = 0; if (v != v) { return fmt_num(out, cap, v); } if (isinf_(v)) return fmt_num(out, cap, v);
    bool neg = v < 0 || (v == 0 && 1 / v < 0); if (neg) v = -v; if (prec > 20) prec = 20;
    if (v >= 1e18) { return fmt_num(out, cap, neg ? -v : v); }
    double ip = floor_(v), fp = v - ip; double sc = pow_(10.0, prec); double fr = floor_(fp * sc + 0.5);
    if (fr >= sc) { ip += 1; fr -= sc; }
    if (neg) tmp[n++] = '-'; else if (force_sign) tmp[n++] = '+';
    char ib[32]; int in = 0; u64 iv = (u64)ip; if (iv == 0) ib[in++] = '0'; while (iv) { ib[in++] = (char)('0' + iv % 10); iv /= 10; } while (in) tmp[n++] = ib[--in];
    if (prec > 0) { tmp[n++] = '.'; char fb[24]; u64 fv = (u64)fr; for (int i = prec - 1; i >= 0; i--) { fb[i] = (char)('0' + fv % 10); fv /= 10; } for (int i = 0; i < prec; i++) tmp[n++] = fb[i]; }
    if (n > cap - 1) n = cap - 1; for (int i = 0; i < n; i++) out[i] = tmp[i]; out[n] = 0; return n;
}
// Parses a Lua numeric literal / tonumber() string. Returns true when the whole text is a number.
inline bool parse_num(const char* s, size_t n, double* out) {
    size_t i = 0; while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++; while (n > i && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n' || s[n - 1] == '\r')) n--;
    if (i >= n) return false; bool neg = false; if (s[i] == '-') { neg = true; i++; } else if (s[i] == '+') i++; if (i >= n) return false;
    double v = 0;
    if (i + 1 < n && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        i += 2; if (i >= n) return false; bool any = false;
        for (; i < n; i++) { char c = s[i]; int d; if (c >= '0' && c <= '9') d = c - '0'; else if (c >= 'a' && c <= 'f') d = c - 'a' + 10; else if (c >= 'A' && c <= 'F') d = c - 'A' + 10; else return false; v = v * 16 + d; any = true; }
        if (!any) return false; *out = neg ? -v : v; return true;
    }
    bool any = false; double digs = 0; int scale = 0; int nd = 0;
    for (; i < n && s[i] >= '0' && s[i] <= '9'; i++) { any = true; if (nd < 19) { digs = digs * 10 + (s[i] - '0'); if (digs > 0) nd++; } else scale++; }
    if (i < n && s[i] == '.') { i++; for (; i < n && s[i] >= '0' && s[i] <= '9'; i++) { any = true; if (nd < 19) { digs = digs * 10 + (s[i] - '0'); if (digs > 0) nd++; scale--; } } }
    if (!any) return false; int ex = 0;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) { i++; bool en = false; if (i < n && (s[i] == '+' || s[i] == '-')) { en = s[i] == '-'; i++; } if (i >= n || s[i] < '0' || s[i] > '9') return false; for (; i < n && s[i] >= '0' && s[i] <= '9'; i++) { if (ex < 100000) ex = ex * 10 + (s[i] - '0'); } if (en) ex = -ex; }
    if (i != n) return false; int tot = ex + scale;
    if (tot == 0) v = digs; else if (tot > 0) v = tot <= 22 ? digs * pow_(10.0, tot) : digs * pow_(10.0, tot); else v = tot >= -22 ? digs / pow_(10.0, -tot) : digs * pow_(10.0, tot);
    *out = neg ? -v : v; return true;
}

// ---------------------------------------------------------------- fixed-pool allocator
// Size classes are powers of two (16 B .. 1 MB). Blocks are carved from a bump region of the pool and recycled through
// per-class free lists, so alloc/free are O(1) and never call the system allocator. A separate permanent region holds
// the AST and constant strings (released only when the whole VM is reset).
struct Heap {
    u8* base = nullptr; size_t size = 0, permEnd = 0, permLimit = 0, bump = 0, used = 0, peak = 0, perm_used = 0;
    void* freeList[18]; bool failed = false;
    void init(void* mem, size_t bytes, size_t permBytes) { base = (u8*)mem; size = bytes; permLimit = permBytes; reset(); }
    void reset() { permEnd = 0; bump = permLimit; used = 0; peak = 0; perm_used = 0; failed = false; for (int i = 0; i < 18; i++) freeList[i] = nullptr; }
    static int cls(size_t n) { int c = 0; size_t s = 16; while (s < n) { s <<= 1; c++; } return c; }
    static size_t clsSize(int c) { return (size_t)16 << c; }
    void* alloc(size_t n) {
        if (n == 0) n = 1; int c = cls(n); if (c >= 18) { failed = true; return nullptr; } size_t sz = clsSize(c);
        if (freeList[c]) { void* p = freeList[c]; freeList[c] = *(void**)p; used += sz; if (used > peak) peak = used; return p; }
        if (bump + sz > size) { failed = true; return nullptr; } void* p = base + bump; bump += sz; used += sz; if (used > peak) peak = used; return p;
    }
    void release(void* p, size_t n) { if (!p) return; int c = cls(n ? n : 1); *(void**)p = freeList[c]; freeList[c] = p; used -= clsSize(c); }
    void* perm(size_t n) { n = (n + 15) & ~(size_t)15; if (permEnd + n > permLimit) { failed = true; return nullptr; } void* p = base + permEnd; permEnd += n; perm_used = permEnd; mzero(p, n); return p; }
};
}  // namespace lx
