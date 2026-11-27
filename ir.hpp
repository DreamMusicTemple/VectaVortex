// Impulse-response (convolution) reverb engine: factory IR generator, IR shaping, WAV/AIFF import, sample-rate conversion, the IR library, and a
// non-uniform partitioned FFT convolver. Mirrors the web app's IR module (fx_module.js: irGen / irProcess / irParseFile / irResample) so a factory IR
// renders the same on both platforms.
//
// Real-time rules: everything that allocates or touches files (generation, import, resampling, partition spectra) runs on a background thread
// (ir::Worker) or the UI thread. The audio thread only runs PartConv::process on buffers that were allocated and filled beforehand, and swaps a fully
// prepared engine in with one atomic exchange; the replaced engine is handed back through a lock-free retire slot and freed off the audio thread.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <dirent.h>

namespace vv { namespace ir {

constexpr double kMaxSec = 12.0, kImportMaxSec = 60.0;
inline double clampv(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
inline double jsRoundV(double v) { return std::floor(v + 0.5); }

// ------------------------------------------------------------------------------------------------ factory IR table (generated from the web table)
struct FactorySpec { const char* id; const char* cat; const char* name; double rt, pre, erN, erMs, fc, damp, dens0, build, shape, per, x, wid; };
static const FactorySpec kFactory[] = {
    {"hall-concert", "Halls", "Concert Hall", 2.2, 0.02, 12, 60, 14000, 1.2, 0.15, 0.08, 0, 0, 0, 1},
    {"hall-warm", "Halls", "Warm Hall", 2.8, 0.024, 10, 70, 9000, 1.6, 0.15, 0.1, 0, 0, 0, 1},
    {"hall-grand", "Halls", "Grand Hall", 3.6, 0.03, 14, 90, 11000, 1.3, 0.12, 0.12, 0, 0, 0, 1},
    {"stage-small", "Stages", "Small Stage", 0.9, 0.008, 10, 25, 12000, 1, 0.25, 0.04, 0, 0, 0, 0.9},
    {"stage-theatre", "Stages", "Theatre Stage", 1.4, 0.012, 12, 40, 11000, 1.1, 0.2, 0.06, 0, 0, 0, 0.95},
    {"stage-arena", "Stages", "Arena Stage", 2, 0.022, 12, 80, 10000, 1.2, 0.15, 0.1, 0, 0, 0, 1},
    {"studio-live", "Studios", "Live Room", 0.5, 0.004, 8, 18, 13000, 1, 0.35, 0.03, 0, 0, 0, 0.9},
    {"studio-booth", "Studios", "Vocal Booth", 0.25, 0.002, 6, 10, 10000, 1.2, 0.5, 0.02, 0, 0, 0, 0.8},
    {"studio-drum", "Studios", "Drum Studio", 0.8, 0.006, 9, 22, 12000, 1, 0.3, 0.04, 0, 0, 0, 0.9},
    {"room-small", "Rooms", "Small Room", 0.35, 0.003, 8, 14, 11000, 1, 0.4, 0.02, 0, 0, 0, 0.85},
    {"room-wood", "Rooms", "Wood Room", 0.6, 0.004, 9, 20, 7000, 1.3, 0.35, 0.03, 0, 0, 0, 0.9},
    {"room-tiled", "Rooms", "Tiled Room", 0.9, 0.004, 10, 24, 16000, 0.5, 0.4, 0.03, 0, 0, 0, 0.9},
    {"chamber-stone", "Chambers", "Stone Chamber", 1.6, 0.01, 10, 35, 9000, 1.3, 0.3, 0.05, 0, 0, 0, 1},
    {"chamber-echo", "Chambers", "Echo Chamber", 2.2, 0.014, 8, 50, 8000, 1.1, 0.25, 0.06, 0, 0, 0, 1},
    {"chamber-vault", "Chambers", "Vault Chamber", 3, 0.016, 8, 60, 7000, 1, 0.3, 0.08, 0, 0, 0, 1},
    {"church-small", "Churches", "Small Church", 3, 0.02, 12, 80, 8000, 1.4, 0.12, 0.12, 0, 0, 0, 1},
    {"church-village", "Churches", "Village Church", 3.8, 0.026, 12, 95, 7500, 1.4, 0.1, 0.14, 0, 0, 0, 1},
    {"church-stone", "Churches", "Stone Church", 4.6, 0.03, 14, 110, 7000, 1.5, 0.1, 0.16, 0, 0, 0, 1},
    {"cath-main", "Cathedrals", "Cathedral", 6.5, 0.04, 14, 140, 6500, 1.6, 0.08, 0.22, 0, 0, 0, 1},
    {"cath-great", "Cathedrals", "Great Cathedral", 8.5, 0.05, 14, 170, 6000, 1.7, 0.06, 0.28, 0, 0, 0, 1},
    {"cath-nave", "Cathedrals", "Gothic Nave", 7.5, 0.045, 12, 150, 7000, 1.8, 0.06, 0.25, 0, 0, 0, 1},
    {"plate-bright", "Plates", "Plate Bright", 1.8, 0, 0, 0, 16000, 0.3, 1, 0.001, 1, 0, 0, 1},
    {"plate-dark", "Plates", "Plate Dark", 2.2, 0, 0, 0, 6000, 0.6, 1, 0.001, 1, 0, 0, 1},
    {"plate-short", "Plates", "Plate Short", 0.9, 0, 0, 0, 12000, 0.5, 1, 0.001, 1, 0, 0, 1},
    {"spring-small", "Springs", "Spring Small", 1.4, 0, 0, 0, 5000, 1.4, 0.6, 0.02, 2, 0.038, 0, 0.7},
    {"spring-long", "Springs", "Spring Long", 2.6, 0, 0, 0, 4500, 1.2, 0.6, 0.02, 2, 0.052, 0, 0.7},
    {"spring-splash", "Springs", "Spring Splash", 2, 0, 0, 0, 6500, 1, 0.6, 0.02, 2, 0.029, 0, 0.7},
    {"amb-studio", "Ambiences", "Studio Ambience", 0.18, 0.004, 8, 12, 12000, 1, 0.5, 0.01, 0, 0, 0, 0.9},
    {"amb-bright", "Ambiences", "Bright Ambience", 0.3, 0.003, 8, 16, 16000, 0.6, 0.5, 0.01, 0, 0, 0, 0.9},
    {"amb-open", "Ambiences", "Open Air", 0.6, 0.01, 6, 60, 8000, 1.4, 0.05, 0.08, 0, 0, 0, 1},
    {"exp-reverse", "Experimental", "Reverse Swell", 2, 0, 0, 0, 12000, 0.6, 1, 0.001, 3, 0, 0, 1},
    {"exp-gated", "Experimental", "Gated Verb", 1.2, 0, 0, 0, 14000, 0.2, 1, 0.001, 4, 0, 0.32, 1},
    {"exp-metal", "Experimental", "Metal Ring", 2.4, 0.002, 0, 0, 15000, 0.4, 0.6, 0.02, 5, 0, 220, 1},
    {"exp-frozen", "Experimental", "Frozen Space", 11, 0.02, 0, 0, 6000, 0, 0.3, 0.4, 0, 0, 0, 1},
    {"exp-dust", "Experimental", "Granular Dust", 3, 0.01, 0, 0, 9000, 0.8, 0.02, 1, 0, 0, 0, 1},
};
constexpr int kFactoryCount = (int)(sizeof(kFactory) / sizeof(kFactory[0]));
static const char* const kCategories[] = {"Halls", "Stages", "Studios", "Rooms", "Chambers", "Churches", "Cathedrals", "Plates", "Springs", "Ambiences", "Experimental"};
constexpr int kCategoryCount = 11;
constexpr const char* kDefaultId = "f:hall-concert";
inline int factoryIndex(const std::string& id) { const std::string k = id.rfind("f:", 0) == 0 ? id.substr(2) : id; for (int i = 0; i < kFactoryCount; i++) if (k == kFactory[i].id) return i; return -1; }

struct Buf { std::vector<float> L, R; double sr = 48000; std::string name; };
using BufPtr = std::shared_ptr<const Buf>;

struct Rng { uint32_t x; double next() { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return (double)x / 4294967296.0 * 2 - 1; } };
inline uint32_t seedOf(const char* id) { uint32_t h = 2166136261u; for (const char* c = id; *c; c++) { h ^= (uint8_t)*c; h *= 16777619u; } return h ? h : 1u; }
inline void fadeOut(std::vector<float>* ch, int nch, int nf) { nf = std::max(1, nf); for (int c = 0; c < nch; c++) { auto& h = ch[c]; const int n = (int)h.size(); for (int i = 0; i < nf && i < n; i++) h[(size_t)(n - 1 - i)] = (float)(h[(size_t)(n - 1 - i)] * ((double)i / nf)); } }
inline void normalise(std::vector<float>* ch, int nch) { double e = 0; for (int c = 0; c < nch; c++) for (float v : ch[c]) e += (double)v * v; e /= nch; const double g = 1 / std::sqrt(std::max(1e-12, e)); for (int c = 0; c < nch; c++) for (auto& v : ch[c]) v = (float)(v * g); }

// Deterministic factory IR (same generator and xorshift32 as the web module).
inline Buf generate(const FactorySpec& s, double sr) {
    const int shape = (int)s.shape; const double pre = s.pre, rt = s.rt;
    const int n = (int)std::min(std::ceil((pre + rt * 1.1 + 0.05) * sr), std::floor(kMaxSec * sr));
    Buf b; b.sr = sr; b.name = s.name; b.L.assign((size_t)n, 0.f); b.R.assign((size_t)n, 0.f); std::vector<float>* out[2] = {&b.L, &b.R};
    const uint32_t seed = seedOf(s.id);
    for (int ch = 0; ch < 2; ch++) {
        uint32_t x0 = (uint32_t)(seed + (uint32_t)ch * 0x9E3779B9u); if (!x0) x0 = 1; Rng g{x0}; std::vector<float>& h = *out[ch];
        const int i0 = (int)std::floor(pre * sr); double y = 0, hp = 0;
        for (int i = i0; i < n; i++) {
            const double t = (i - i0) / sr; double env = std::exp(-6.907755 * t / rt);
            if (shape == 3) { const double u = std::min(1.0, t / rt); env = u * u * (1 - u * u * u); }
            else if (shape == 4) { const double gt = s.x; env = t < gt ? 1 : std::max(0.0, 1 - (t - gt) / 0.01); }
            const double u = g.next() * .5 + .5, v = g.next(), d = std::min(1.0, s.dens0 + t / std::max(1e-4, s.build));
            const double nz = u < d ? v : 0;
            const double fc = std::max(400.0, s.fc * std::exp(-s.damp * t / rt * 2.5)), a = 1 - std::exp(-6.2831853 * fc / sr);
            y += a * (nz * env - y); hp += 0.01 * (y - hp);
            double z = y - hp;
            if (shape == 5) z *= 1 + std::sin(6.2831853 * s.x * t);
            h[(size_t)i] = (float)z;
        }
        if (shape == 2) {
            const int per = (int)std::max(1.0, jsRoundV(s.per * sr)), cl = (int)std::floor(0.012 * sr);
            for (int k = 0; k < 24; k++) {
                const int st = i0 + k * per; const double amp = std::pow(0.8, k) * 0.9; double ph = 0;
                for (int j = 0; j < cl && st + j < n; j++) {
                    const double f = 3500 - (3500 - 180) * (double)j / cl; ph += 6.2831853 * f / sr; const double w = 0.5 - 0.5 * std::cos(6.2831853 * (j + 0.5) / cl);
                    h[(size_t)(st + j)] = (float)(h[(size_t)(st + j)] + std::sin(ph) * w * amp * (1 + 0.3 * g.next()) * 0.02);
                }
            }
        }
        for (int k = 0; k < (int)s.erN; k++) {
            const double a = g.next() * .5 + .5, bb = g.next(), tt = pre + (s.erMs / 1000) * std::pow(a, 1.3); const int idx = (int)std::min((double)(n - 1), jsRoundV(tt * sr));
            h[(size_t)idx] = (float)(h[(size_t)idx] + bb * (0.9 - 0.6 * k / s.erN) * 0.05);
        }
    }
    if (s.wid < 1) for (int i = 0; i < n; i++) b.R[(size_t)i] = (float)(s.wid * b.R[(size_t)i] + (1 - s.wid) * b.L[(size_t)i]);
    std::vector<float> ch2[2] = {std::move(b.L), std::move(b.R)}; fadeOut(ch2, 2, (int)std::floor(n * 0.05)); normalise(ch2, 2); b.L = std::move(ch2[0]); b.R = std::move(ch2[1]);
    return b;
}

// ------------------------------------------------------------------------------------------------ shaping (start / end / length / early-late / damping / reverse / normalise / gain)
struct Shape {
    double start = 0, end = 1, lenPct = 100, earlyLate = 0, damping = 0, irGain = 0; bool reverse = false, normalise = true;
    bool operator==(const Shape& o) const { return start == o.start && end == o.end && lenPct == o.lenPct && earlyLate == o.earlyLate && damping == o.damping && irGain == o.irGain && reverse == o.reverse && normalise == o.normalise; }
};
inline Buf process(const Buf& src, const Shape& P) {
    const double sr = src.sr; const int N = (int)src.L.size();
    const int st = (int)std::max(0.0, std::min((double)(N - 2), std::floor(clampv(P.start, 0, 1) * N)));
    int en = (int)std::max((double)(st + 2), std::min((double)N, std::ceil(clampv(P.end, 0, 1) * N)));
    en = std::min(en, st + (int)std::floor(kMaxSec * sr));
    int n = en - st; const double lenPct = clampv(P.lenPct, 1, 100); n = (int)std::max(32.0, std::floor(n * lenPct / 100));
    Buf o; o.sr = sr; o.name = src.name; o.L.assign((size_t)n, 0.f); o.R.assign((size_t)n, 0.f); std::vector<float>* outs[2] = {&o.L, &o.R}; const std::vector<float>* srcs[2] = {&src.L, src.R.empty() ? &src.L : &src.R};
    const double el = clampv(P.earlyLate, -1, 1), ge = std::pow(10.0, -el * 9 / 20), gl = std::pow(10.0, el * 9 / 20), damp = clampv(P.damping, 0, 1);
    for (int c = 0; c < 2; c++) {
        const std::vector<float>& a = *srcs[c]; std::vector<float>& oc = *outs[c]; double y = 0;
        for (int i = 0; i < n; i++) {
            const double t = i / sr; double v = (st + i) < N ? a[(size_t)(st + i)] : 0.0;
            if (el != 0) { const double u = clampv((t - 0.07) / 0.02, 0, 1), sm = u * u * (3 - 2 * u); v *= ge + (gl - ge) * sm; }
            if (damp > 0.001) { const double fc = std::max(500.0, 18000 * std::exp(-damp * t * 3)), k = 1 - std::exp(-6.2831853 * fc / sr); y += k * (v - y); v = y; }
            oc[(size_t)i] = (float)v;
        }
    }
    std::vector<float> ch2[2] = {std::move(o.L), std::move(o.R)}; fadeOut(ch2, 2, (int)std::max(8.0, std::floor(std::min(n * 0.1, 0.02 * sr))));
    if (P.reverse) { std::reverse(ch2[0].begin(), ch2[0].end()); std::reverse(ch2[1].begin(), ch2[1].end()); }
    if (P.normalise) normalise(ch2, 2);
    const double g = std::pow(10.0, clampv(P.irGain, -24, 24) / 20); if (g != 1) for (int c = 0; c < 2; c++) for (auto& v : ch2[c]) v = (float)(v * g);
    o.L = std::move(ch2[0]); o.R = std::move(ch2[1]); return o;
}
inline void autoTrim(const Buf& src, double& start, double& end) {
    const int N = (int)src.L.size(); const std::vector<float>& R = src.R.empty() ? src.L : src.R; double pk = 0; for (int i = 0; i < N; i++) pk = std::max(pk, (double)std::max(std::fabs(src.L[(size_t)i]), std::fabs(R[(size_t)i])));
    start = 0; end = 1; if (pk <= 0) return;
    const double t0 = pk * 0.001, t1 = pk * 0.00025; int a = 0, b = N - 1;
    while (a < N - 1 && std::fabs(src.L[(size_t)a]) < t0 && std::fabs(R[(size_t)a]) < t0) a++;
    while (b > a && std::fabs(src.L[(size_t)b]) < t1 && std::fabs(R[(size_t)b]) < t1) b--;
    a = std::max(0, a - 16); b = std::min(N, b + 1 + (int)std::floor(0.01 * src.sr)); start = (double)a / N; end = (double)b / N;
}
// Windowed-sinc sample-rate conversion (Hann window, 12 zero crossings per side, cutoff scaled when down-sampling).
inline std::vector<float> resample(const std::vector<float>& x, double srcSr, double dstSr) {
    if (std::fabs(srcSr - dstSr) < 0.5) return x;
    const double ratio = dstSr / srcSr; const size_t m = (size_t)std::max(1.0, jsRoundV((double)x.size() * ratio)); std::vector<float> out(m); const double cut = std::min(1.0, ratio), W = 12 / cut;
    for (size_t i = 0; i < m; i++) {
        const double pos = (double)i / ratio; double acc = 0;
        for (long k = (long)std::ceil(pos - W); k <= (long)std::floor(pos + W); k++) {
            if (k < 0 || k >= (long)x.size()) continue; const double d = pos - (double)k, u = d * cut, s = std::fabs(u) < 1e-9 ? 1.0 : std::sin(M_PI * u) / (M_PI * u), w = 0.5 + 0.5 * std::cos(M_PI * d / W);
            acc += x[(size_t)k] * s * w * cut;
        }
        out[i] = (float)acc;
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ WAV / AIFF import with validation
struct Parsed { bool ok = false; std::string error; std::vector<std::string> warn; std::vector<float> L, R; double sr = 0; int ch = 0; size_t frames = 0; };
inline Parsed parseFile(const uint8_t* d, size_t n) {
    Parsed r; auto fail = [&](const char* e) { r.ok = false; r.error = e; return r; };
    if (!d || n < 44) return fail("File is too small to be a WAV or AIFF file.");
    auto u16 = [&](size_t o, bool be) -> uint32_t { return be ? (uint32_t)(d[o] << 8 | d[o + 1]) : (uint32_t)(d[o] | d[o + 1] << 8); };
    auto u32 = [&](size_t o, bool be) -> uint32_t { return be ? ((uint32_t)d[o] << 24 | (uint32_t)d[o + 1] << 16 | (uint32_t)d[o + 2] << 8 | d[o + 3]) : ((uint32_t)d[o + 3] << 24 | (uint32_t)d[o + 2] << 16 | (uint32_t)d[o + 1] << 8 | d[o]); };
    auto tag = [&](size_t o, const char* t) { return o + 4 <= n && std::memcmp(d + o, t, 4) == 0; };
    int ch = 0, bits = 0, fmt = 0; double sr = 0; size_t dataOff = 0, dataLen = 0; bool be = false;
    if (tag(0, "RIFF") && tag(8, "WAVE")) {
        size_t o = 12; bool haveFmt = false, haveData = false;
        while (o + 8 <= n) {
            const size_t sz = u32(o + 4, false), b = o + 8;
            if (tag(o, "fmt ")) { if (b + 16 > n) return fail("The WAV file has no fmt or data chunk."); fmt = (int)u16(b, false); ch = (int)u16(b + 2, false); sr = u32(b + 4, false); bits = (int)u16(b + 14, false); if (fmt == 0xFFFE && sz >= 26 && b + 26 <= n) fmt = (int)u16(b + 24, false); haveFmt = true; }
            else if (tag(o, "data")) { dataOff = b; dataLen = std::min(sz, n - b); haveData = true; }
            o = b + sz + (sz & 1); if (haveFmt && haveData) break;
        }
        if (!haveFmt || !haveData) return fail("The WAV file has no fmt or data chunk.");
        if (fmt != 1 && fmt != 3) return fail("Unsupported WAV encoding (only PCM and float WAV can be imported).");
    } else if (tag(0, "FORM") && (tag(8, "AIFF") || tag(8, "AIFC"))) {
        const bool comp = tag(8, "AIFC"); size_t o = 12; bool haveC = false, haveS = false; be = true; fmt = 1;
        while (o + 8 <= n) {
            const size_t sz = u32(o + 4, true), b = o + 8;
            if (tag(o, "COMM")) {
                if (b + 18 > n) return fail("The AIFF file has no COMM or SSND chunk.");
                ch = (int)u16(b, true); bits = (int)u16(b + 6, true); const int ex = (int)(u16(b + 8, true) & 0x7fff) - 16383; const double hi = u32(b + 10, true), lo = u32(b + 14, true); sr = jsRoundV((hi * 4294967296.0 + lo) / std::pow(2.0, 63 - ex));
                if (comp && b + 22 <= n) { if (tag(b + 18, "sowt")) be = false; else if (tag(b + 18, "fl32") || tag(b + 18, "FL32")) fmt = 3; else if (!tag(b + 18, "NONE") && !tag(b + 18, "twos")) return fail("Compressed AIFC audio is not supported."); }
                haveC = true;
            } else if (tag(o, "SSND")) { if (b + 8 > n) return fail("The AIFF file has no COMM or SSND chunk."); const size_t off = u32(b, true); dataOff = b + 8 + off; if (dataOff > n || sz < 8 + off) return fail("The AIFF file has no COMM or SSND chunk."); dataLen = std::min(sz - 8 - off, n - dataOff); haveS = true; }
            o = b + sz + (sz & 1); if (haveC && haveS) break;
        }
        if (!haveC || !haveS) return fail("The AIFF file has no COMM or SSND chunk.");
    } else return fail("Not a WAV or AIFF file.");
    if (!(ch >= 1 && ch <= 8)) return fail("Invalid channel count.");
    if (!(sr >= 8000 && sr <= 384000)) return fail("Unsupported sample rate.");
    if (bits != 8 && bits != 16 && bits != 24 && bits != 32 && bits != 64) return fail("Unsupported bit depth.");
    if (fmt == 3 && bits != 32 && bits != 64) return fail("Unsupported float width.");
    const size_t bps = (size_t)bits >> 3, nF = dataLen / (bps * (size_t)ch); if (nF < 16) return fail("The file contains no audio.");
    if ((double)nF / sr > kImportMaxSec) return fail("The impulse response is longer than 60 s.");
    if (ch > 2) r.warn.push_back("Only the first two channels are used.");
    r.L.assign(nF, 0.f); r.R.assign(nF, 0.f); const int nc = std::min(2, ch);
    for (int c = 0; c < nc; c++) { std::vector<float>& o = c ? r.R : r.L;
        for (size_t i = 0; i < nF; i++) {
            const uint8_t* p = d + dataOff + (i * (size_t)ch + (size_t)c) * bps; double v;
            if (fmt == 3) { if (bits == 32) { uint32_t w = be ? ((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]) : ((uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]); float f; std::memcpy(&f, &w, 4); v = f; }
                else { uint64_t w = 0; for (int k = 0; k < 8; k++) w = (w << 8) | p[be ? k : 7 - k]; double f; std::memcpy(&f, &w, 8); v = f; } }
            else if (bits == 8) v = be ? (int8_t)p[0] / 128.0 : ((int)p[0] - 128) / 128.0;
            else if (bits == 16) v = (be ? (int16_t)(p[0] << 8 | p[1]) : (int16_t)(p[1] << 8 | p[0])) / 32768.0;
            else if (bits == 24) { const int32_t a = be ? (int32_t)(int8_t)p[0] * 65536 + p[1] * 256 + p[2] : (int32_t)(int8_t)p[2] * 65536 + p[1] * 256 + p[0]; v = a / 8388608.0; }
            else v = (be ? (int32_t)((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]) : (int32_t)((uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0])) / 2147483648.0;
            if (!std::isfinite(v)) return fail("The file contains invalid (NaN) samples.");
            o[i] = (float)v;
        } }
    if (nc == 1) r.R = r.L;
    double pk = 0; for (size_t i = 0; i < nF; i++) pk = std::max(pk, (double)std::max(std::fabs(r.L[i]), std::fabs(r.R[i])));
    if (pk < 1e-7) return fail("The impulse response is silent.");
    if ((double)nF / sr > kMaxSec) r.warn.push_back("Longer than 12 s: only the first 12 s are used.");
    r.ok = true; r.sr = sr; r.ch = nc; r.frames = nF; return r;
}

// ------------------------------------------------------------------------------------------------ library (UI / worker threads only)
struct UserIr { std::string id, name; double sr = 48000; std::vector<float> L, R; uint64_t added = 0; std::string file; };
class Library {
public:
    static Library& get() { static Library l; return l; }
    // Registers an IR from file bytes. Id = "u:" + FNV-1a of the bytes (stable across launches and processes, so a preset reference survives a reinstall of the same file).
    bool add(const uint8_t* d, size_t n, const std::string& name, std::string& idOut, std::string& err, std::vector<std::string>& warn, const std::string& replaceId = "") {
        Parsed p = parseFile(d, n); if (!p.ok) { err = p.error; return false; }
        uint32_t h = 2166136261u; for (size_t i = 0; i < n; i++) { h ^= d[i]; h *= 16777619u; } char hex[16]; std::snprintf(hex, sizeof hex, "%08x", h);
        auto u = std::make_shared<UserIr>(); u->id = replaceId.empty() ? std::string("u:") + hex : replaceId; u->name = name.empty() ? "Imported IR" : name.substr(0, 48); u->sr = p.sr;
        const size_t nmax = (size_t)(kMaxSec * p.sr); if (p.L.size() > nmax) { p.L.resize(nmax); p.R.resize(nmax); }
        u->L = std::move(p.L); u->R = std::move(p.R); warn = p.warn;
        std::lock_guard<std::mutex> lk(m_); u->added = ++counter_; auto it = users_.find(u->id); if (it != users_.end()) u->added = it->second->added; users_[u->id] = u; missing_.erase(u->id); dropCache(u->id); idOut = u->id; return true;
    }
    bool remove(const std::string& id) { std::lock_guard<std::mutex> lk(m_); const bool had = users_.erase(id) > 0; dropCache(id); return had; }
    bool has(const std::string& id) { std::lock_guard<std::mutex> lk(m_); return users_.count(id) > 0; }
    std::string name(const std::string& id) { const int f = factoryIndex(id); if (id.rfind("f:", 0) == 0 && f >= 0) return kFactory[f].name; std::lock_guard<std::mutex> lk(m_); auto it = users_.find(id); return it == users_.end() ? std::string() : it->second->name; }
    // Source IR at the engine's sample rate (cached). A missing user IR falls back to the default factory IR and is recorded in the missing list.
    BufPtr source(const std::string& id, double sr, bool* missing = nullptr, std::string* usedId = nullptr) {
        std::lock_guard<std::mutex> lk(m_); const std::string key = id + "|" + std::to_string((long)sr);
        auto c = cache_.find(key); if (c != cache_.end()) { if (missing) *missing = c->second.missing; if (usedId) *usedId = c->second.used; return c->second.buf; }
        BufPtr out; bool miss = false; std::string used = id;
        if (id.rfind("u:", 0) == 0) {
            auto it = users_.find(id);
            if (it != users_.end()) { auto b = std::make_shared<Buf>(); b->sr = sr; b->name = it->second->name; b->L = resample(it->second->L, it->second->sr, sr); b->R = resample(it->second->R, it->second->sr, sr); out = b; } else miss = true;
        } else { const int f = factoryIndex(id); if (f >= 0) out = std::make_shared<Buf>(generate(kFactory[f], sr)); else miss = true; }
        if (!out) { out = std::make_shared<Buf>(generate(kFactory[0], sr)); used = kDefaultId; missing_.insert(id); } else missing_.erase(id);
        if (cache_.size() > 24) cache_.erase(cache_.begin());
        cache_[key] = {out, miss, used}; if (missing) *missing = miss; if (usedId) *usedId = used; return out;
    }
    // [{"id":..,"name":..,"sr":..,"seconds":..}] of the imported IRs, in import order
    std::string userJson() { std::lock_guard<std::mutex> lk(m_); std::vector<std::shared_ptr<UserIr>> v; for (auto& kv : users_) v.push_back(kv.second); std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a->added < b->added; });
        std::string s = "["; for (size_t i = 0; i < v.size(); i++) { char b[128]; std::snprintf(b, sizeof b, ",\"sr\":%.0f,\"seconds\":%.3f}", v[i]->sr, (double)v[i]->L.size() / v[i]->sr); s += (i ? "," : "") + std::string("{\"id\":\"") + v[i]->id + "\",\"name\":\"" + esc(v[i]->name) + "\"" + b; } return s + "]"; }
    std::string missingJson() { std::lock_guard<std::mutex> lk(m_); std::string s = "["; bool f = true; for (auto& id : missing_) { s += (f ? "" : ",") + std::string("\"") + esc(id) + "\""; f = false; } return s + "]"; }
    // Registers every WAV / AIFF in a folder (the app-group "IRs" folder). File name: <id>__<name>.<ext> or any <name>.<ext>. Returns the number added.
    int scanDir(const std::string& dir) {
        int added = 0; DIR* dp = opendir(dir.c_str()); if (!dp) return 0;
        while (dirent* e = readdir(dp)) { const std::string f = e->d_name; const size_t dot = f.rfind('.'); if (dot == std::string::npos) continue; std::string ext = f.substr(dot + 1); for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext != "wav" && ext != "aif" && ext != "aiff" && ext != "aifc") continue; std::string id, err; std::vector<std::string> w; if (addFile(dir + "/" + f, f.substr(0, dot), id, err, w)) added++; }
        closedir(dp); return added;
    }
    bool addFile(const std::string& path, const std::string& name, std::string& id, std::string& err, std::vector<std::string>& warn, const std::string& replaceId = "") {
        std::FILE* fp = std::fopen(path.c_str(), "rb"); if (!fp) { err = "The file could not be opened."; return false; }
        std::fseek(fp, 0, SEEK_END); const long sz = std::ftell(fp); std::fseek(fp, 0, SEEK_SET); if (sz <= 0 || sz > 200L * 1024 * 1024) { std::fclose(fp); err = "The file is empty or too large."; return false; }
        std::vector<uint8_t> b((size_t)sz); const size_t got = std::fread(b.data(), 1, b.size(), fp); std::fclose(fp); if (got != b.size()) { err = "The file could not be read."; return false; }
        std::string nm = name; const size_t sep = nm.find("__"); if (sep != std::string::npos) nm = nm.substr(sep + 2);
        return add(b.data(), b.size(), nm, id, err, warn, replaceId);
    }
private:
    static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; if ((unsigned char)c >= 32) o += c; } return o; }
    struct CE { BufPtr buf; bool missing; std::string used; };
    void dropCache(const std::string& id) { for (auto it = cache_.begin(); it != cache_.end();) { if (it->first.rfind(id + "|", 0) == 0) it = cache_.erase(it); else ++it; } }
    std::mutex m_; std::map<std::string, std::shared_ptr<UserIr>> users_; std::map<std::string, CE> cache_; std::set<std::string> missing_; uint64_t counter_ = 0;
};

// ------------------------------------------------------------------------------------------------ FFT + partitioned convolver
struct Fft {
    int n = 0; std::vector<float> cs, sn; std::vector<int> rev;
    void init(int N) { n = N; cs.resize((size_t)N / 2); sn.resize((size_t)N / 2); for (int i = 0; i < N / 2; i++) { cs[(size_t)i] = (float)std::cos(-2 * M_PI * i / N); sn[(size_t)i] = (float)std::sin(-2 * M_PI * i / N); }
        rev.assign((size_t)N, 0); int lg = 0; while ((1 << lg) < N) lg++; for (int i = 0; i < N; i++) { int r = 0; for (int b = 0; b < lg; b++) if (i & (1 << b)) r |= 1 << (lg - 1 - b); rev[(size_t)i] = r; } }
    void fwd(float* re, float* im) const {
        for (int i = 0; i < n; i++) { const int j = rev[(size_t)i]; if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); } }
        for (int len = 2; len <= n; len <<= 1) { const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len) for (int k = 0; k < half; k++) { const float wr = cs[(size_t)(k * step)], wi = sn[(size_t)(k * step)]; float* a = re + i + k; float* b = re + i + k + half; float* ai = im + i + k; float* bi = im + i + k + half;
                const float tr = *b * wr - *bi * wi, ti = *b * wi + *bi * wr; *b = *a - tr; *bi = *ai - ti; *a += tr; *ai += ti; } }
    }
    void inv(float* re, float* im) const { fwd(im, re); const float s = 1.0f / (float)n; for (int i = 0; i < n; i++) { re[i] *= s; im[i] *= s; } }
};

constexpr int kChunk = 128;
// Non-uniform partitioned convolver, true stereo-parallel (IR L on input L, IR R on input R). Levels (block size B, IR offset s = c*B, P partitions):
//   level 0: B=128, immediate (c = 0), covers IR[0, 2048)       level 1: B=512,  c=4, up to 8 partitions  [2048, 6144)
//   level 2: B=2048, c=3, up to 3 partitions [6144, 12288)       level 3: B=4096, c=3, the rest (up to 12 s)
// Levels above 0 compute the product for block m during block m-1 (time-sliced over its 128-sample chunks), so no processing step does more than a
// small slice of work and the output has no latency beyond the host buffer.
class PartConv {
public:
    void build(const float* irL, const float* irR, int N) {
        levels_.clear(); N = std::max(1, N);
        struct Spec { int B, c, P; }; std::vector<Spec> specs;
        int off = 0; { const int P = std::min(16, (N + 127) / 128); specs.push_back({128, 0, P}); off = P * 128; }
        if (off < N) { const int B = 512, c = off / B; const int P = std::min(8, (N - off + B - 1) / B); if (off % B == 0 && c >= 2) { specs.push_back({B, c, P}); off += P * B; } }
        if (off < N) { const int B = 2048, c = off / B; const int P = std::min(3, (N - off + B - 1) / B); if (off % B == 0 && c >= 2) { specs.push_back({B, c, P}); off += P * B; } }
        if (off < N) { const int B = 4096, c = off / B; const int P = (N - off + B - 1) / B; if (off % B == 0 && c >= 2) { specs.push_back({B, c, P}); off += P * B; } }
        levels_.resize(specs.size());
        for (size_t li = 0; li < specs.size(); li++) {
            Level& L = levels_[li]; L.B = specs[li].B; L.F = 2 * L.B; L.c = specs[li].c; L.P = specs[li].P; L.r = L.B / kChunk; L.R = L.c + L.P + 1; L.fft.init(L.F); const int nb = L.B + 1; const int s0 = L.c * L.B;
            for (int ch = 0; ch < 2; ch++) {
                const float* ir = ch ? irR : irL; L.Hre[ch].assign((size_t)L.P * nb, 0.f); L.Him[ch].assign((size_t)L.P * nb, 0.f); L.Xre[ch].assign((size_t)L.R * nb, 0.f); L.Xim[ch].assign((size_t)L.R * nb, 0.f);
                L.in[ch].assign((size_t)L.B, 0.f); L.prev[ch].assign((size_t)L.B, 0.f); L.outA[ch].assign((size_t)L.B, 0.f); L.outB[ch].assign((size_t)L.B, 0.f); L.tr[ch].assign((size_t)L.F, 0.f); L.ti[ch].assign((size_t)L.F, 0.f);
                L.accR[ch].assign((size_t)nb, 0.f); L.accI[ch].assign((size_t)nb, 0.f);
                for (int p = 0; p < L.P; p++) {
                    std::fill(L.tr[ch].begin(), L.tr[ch].end(), 0.f); std::fill(L.ti[ch].begin(), L.ti[ch].end(), 0.f);
                    for (int k = 0; k < L.B; k++) { const long idx = (long)s0 + (long)p * L.B + k; if (idx < N) L.tr[ch][(size_t)k] = ir[idx]; }
                    L.fft.fwd(L.tr[ch].data(), L.ti[ch].data());
                    std::memcpy(&L.Hre[ch][(size_t)p * nb], L.tr[ch].data(), sizeof(float) * (size_t)nb); std::memcpy(&L.Him[ch][(size_t)p * nb], L.ti[ch].data(), sizeof(float) * (size_t)nb);
                }
            }
            L.cur = 0; L.t = 0; L.j = 0;
        }
        std::fill(fifoIn_[0], fifoIn_[0] + kChunk, 0.f); std::fill(fifoIn_[1], fifoIn_[1] + kChunk, 0.f); fifoFill_ = 0; outPos_ = kChunk; std::fill(outBuf_[0], outBuf_[0] + kChunk, 0.f); std::fill(outBuf_[1], outBuf_[1] + kChunk, 0.f);
    }
    bool ready() const { return !levels_.empty(); }
    // n frames. Whole 128-frame chunks (the engine quantum) are convolved in place with no added latency; any other size goes through a small FIFO (+128 frames of latency).
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) {
        int i = 0;
        while (i < n) {
            if (fifoFill_ == 0 && n - i >= kChunk) { chunk(inL + i, inR + i, outL + i, outR + i); i += kChunk; continue; }
            outL[i] = outPos_ < kChunk ? outBuf_[0][outPos_] : 0.f; outR[i] = outPos_ < kChunk ? outBuf_[1][outPos_] : 0.f; if (outPos_ < kChunk) outPos_++;   // output of the previous chunk (exactly 128 frames late)
            fifoIn_[0][fifoFill_] = inL[i]; fifoIn_[1][fifoFill_] = inR[i]; fifoFill_++;
            if (fifoFill_ == kChunk) { chunk(fifoIn_[0], fifoIn_[1], outBuf_[0], outBuf_[1]); fifoFill_ = 0; outPos_ = 0; }
            i++;
        }
    }
    // single call with exactly 128 frames: returns the convolved chunk (used by the tests for direct verification)
    void processChunk(const float* inL, const float* inR, float* outL, float* outR) { chunk(inL, inR, outL, outR); }
    size_t bytes() const { size_t b = 0; for (auto& L : levels_) for (int c = 0; c < 2; c++) b += (L.Hre[c].size() + L.Him[c].size() + L.Xre[c].size() + L.Xim[c].size()) * 4; return b; }
    int levelCount() const { return (int)levels_.size(); }
private:
    struct Level {
        int B = 128, F = 256, c = 0, P = 1, r = 1, R = 2, t = 0, cur = 0; long j = 0; Fft fft;
        std::vector<float> Hre[2], Him[2], Xre[2], Xim[2], in[2], prev[2], outA[2], outB[2], tr[2], ti[2], accR[2], accI[2];
    };
    void forwardBlock(Level& L, int ch) {   // FFT of [prev, in] into slot j % R
        float* tr = L.tr[ch].data(); float* ti = L.ti[ch].data(); std::memcpy(tr, L.prev[ch].data(), sizeof(float) * (size_t)L.B); std::memcpy(tr + L.B, L.in[ch].data(), sizeof(float) * (size_t)L.B); std::fill(ti, ti + L.F, 0.f);
        L.fft.fwd(tr, ti); const int nb = L.B + 1; const size_t slot = (size_t)(L.j % L.R) * nb; std::memcpy(&L.Xre[ch][slot], tr, sizeof(float) * (size_t)nb); std::memcpy(&L.Xim[ch][slot], ti, sizeof(float) * (size_t)nb);
        std::memcpy(L.prev[ch].data(), L.in[ch].data(), sizeof(float) * (size_t)L.B);
    }
    void mac(Level& L, int ch, long idx, int p) {
        if (idx < 0) return; const int nb = L.B + 1; const float* xr = &L.Xre[ch][(size_t)(idx % L.R) * nb]; const float* xi = &L.Xim[ch][(size_t)(idx % L.R) * nb]; const float* hr = &L.Hre[ch][(size_t)p * nb]; const float* hi = &L.Him[ch][(size_t)p * nb];
        float* ar = L.accR[ch].data(); float* ai = L.accI[ch].data();
        for (int k = 0; k < nb; k++) { ar[k] += xr[k] * hr[k] - xi[k] * hi[k]; ai[k] += xr[k] * hi[k] + xi[k] * hr[k]; }
    }
    void inverseTo(Level& L, int ch, float* dst) {   // Hermitian-complete accR/accI, inverse FFT, last B samples
        float* tr = L.tr[ch].data(); float* ti = L.ti[ch].data(); const float* ar = L.accR[ch].data(); const float* ai = L.accI[ch].data();
        for (int k = 0; k <= L.B; k++) { tr[k] = ar[k]; ti[k] = ai[k]; } for (int k = 1; k < L.B; k++) { tr[L.F - k] = ar[k]; ti[L.F - k] = -ai[k]; }
        ti[0] = 0; ti[L.B] = 0; L.fft.inv(tr, ti); std::memcpy(dst, tr + L.B, sizeof(float) * (size_t)L.B);
    }
    void chunk(const float* inL, const float* inR, float* outL, float* outR) {
        std::fill(outL, outL + kChunk, 0.f); std::fill(outR, outR + kChunk, 0.f); const float* xin[2] = {inL, inR}; float* xout[2] = {outL, outR};
        for (Level& L : levels_) {
            for (int ch = 0; ch < 2; ch++) {
                std::memcpy(&L.in[ch][(size_t)L.t * kChunk], xin[ch], sizeof(float) * kChunk);
                if (L.c == 0) {   // immediate level
                    forwardBlock(L, ch); std::fill(L.accR[ch].begin(), L.accR[ch].end(), 0.f); std::fill(L.accI[ch].begin(), L.accI[ch].end(), 0.f);
                    for (int p = 0; p < L.P; p++) mac(L, ch, L.j - p, p);
                    inverseTo(L, ch, L.outA[ch].data()); for (int k = 0; k < kChunk; k++) xout[ch][k] += L.outA[ch][(size_t)k];
                    continue;
                }
                std::vector<float>& cur = L.cur ? L.outB[ch] : L.outA[ch]; std::vector<float>& nxt = L.cur ? L.outA[ch] : L.outB[ch];
                for (int k = 0; k < kChunk; k++) xout[ch][k] += cur[(size_t)L.t * kChunk + (size_t)k];
                if (L.t == 0) { std::fill(L.accR[ch].begin(), L.accR[ch].end(), 0.f); std::fill(L.accI[ch].begin(), L.accI[ch].end(), 0.f); }
                const int pa = (L.t * L.P) / L.r, pb = ((L.t + 1) * L.P) / L.r;
                for (int p = pa; p < pb; p++) mac(L, ch, L.j + 1 - L.c - p, p);
                if (L.t == L.r - 1) { inverseTo(L, ch, nxt.data()); forwardBlock(L, ch); }
            }
            if (L.c == 0) { L.j++; continue; }
            if (L.t == L.r - 1) { L.t = 0; L.j++; L.cur ^= 1; } else L.t++;
        }
    }
    std::vector<Level> levels_; float fifoIn_[2][kChunk] = {}; float outBuf_[2][kChunk] = {}; int fifoFill_ = 0, outPos_ = kChunk;
};

// ------------------------------------------------------------------------------------------------ prepared engine + background worker
struct Params { char id[96] = {0}; Shape shape; double sr = 48000; };
struct Engine {
    PartConv conv; int len = 0; double seconds = 0; bool missing = false; std::string usedId;
};
// One per IrStage; shared with the worker through a shared_ptr. The audio thread writes `req` (seqlock) and reads `pending` / writes `retired` with atomics only.
struct Shared {
    std::atomic<uint32_t> seq{0}; Params req; std::atomic<uint32_t> done{0}; std::atomic<Engine*> pending{nullptr}; std::atomic<Engine*> retired[4] = {{nullptr}, {nullptr}, {nullptr}, {nullptr}};
    std::atomic<int> lenSamples{0}; std::atomic<int> missingFlag{0}; std::atomic<uint32_t> built{0};
    void post(const Params& p) { uint32_t s = seq.load(std::memory_order_relaxed); seq.store(s + 1, std::memory_order_release); req = p; seq.store(s + 2, std::memory_order_release); }
    bool read(Params& out, uint32_t& s) const { for (int tries = 0; tries < 8; tries++) { const uint32_t a = seq.load(std::memory_order_acquire); if (a & 1) continue; out = req; std::atomic_thread_fence(std::memory_order_acquire); if (seq.load(std::memory_order_relaxed) == a) { s = a; return true; } } return false; }
    bool retire(Engine* e) { for (auto& r : retired) { Engine* z = nullptr; if (r.compare_exchange_strong(z, e)) return true; } return false; }
    void drain() { for (auto& r : retired) delete r.exchange(nullptr); }
    ~Shared() { delete pending.exchange(nullptr); drain(); }
};
inline Engine* buildEngine(const Params& p) {
    bool miss = false; std::string used; BufPtr src = Library::get().source(p.id, p.sr, &miss, &used); Buf pr = process(*src, p.shape);
    Engine* e = new Engine(); e->conv.build(pr.L.data(), pr.R.data(), (int)pr.L.size()); e->len = (int)pr.L.size(); e->seconds = e->len / p.sr; e->missing = miss; e->usedId = used; return e;
}
class Worker {
public:
    static Worker& get() { static Worker w; return w; }
    void add(const std::shared_ptr<Shared>& s) { std::lock_guard<std::mutex> lk(m_); list_.push_back(s); if (!th_.joinable()) th_ = std::thread([this] { run(); }); }
    // blocks until every registered stage has built its latest request (tests / offline bounce); returns false on timeout
    bool waitIdle(int timeoutMs) { const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < end) { bool idle = true; { std::lock_guard<std::mutex> lk(m_); for (auto& w : list_) if (auto s = w.lock()) { Params p; uint32_t q = 0; if (s->read(p, q) && s->built.load() != q && q != 0) idle = false; } }
            if (idle) return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); } return false; }
    ~Worker() { stop_ = true; if (th_.joinable()) th_.join(); }
private:
    void run() {
        std::map<Shared*, std::pair<uint32_t, int>> seen;   // last seq, number of polls it has been stable
        while (!stop_) {
            std::vector<std::shared_ptr<Shared>> live; { std::lock_guard<std::mutex> lk(m_); for (auto it = list_.begin(); it != list_.end();) { if (auto s = it->lock()) { live.push_back(s); ++it; } else it = list_.erase(it); } }
            for (auto& s : live) {
                s->drain(); Params p; uint32_t q = 0; if (!s->read(p, q) || q == 0 || s->built.load() == q) continue;
                auto& st = seen[s.get()]; if (st.first != q) { st.first = q; st.second = 0; } if (++st.second < 2 && s->built.load() != 0) continue;   // debounce: a changing request waits one extra poll
                Engine* e = buildEngine(p); Engine* old = s->pending.exchange(e); delete old; s->lenSamples.store(e->len); s->missingFlag.store(e->missing ? 1 : 0); s->built.store(q);
            }
            for (auto it = seen.begin(); it != seen.end();) { bool alive = false; for (auto& s : live) if (s.get() == it->first) alive = true; if (!alive) it = seen.erase(it); else ++it; }
            std::this_thread::sleep_for(std::chrono::milliseconds(12));
        }
    }
    std::mutex m_; std::vector<std::weak_ptr<Shared>> list_; std::thread th_; std::atomic<bool> stop_{false};
};

// ------------------------------------------------------------------------------------------------ UI helpers (non-realtime)
// Audition: a short dry percussive burst + plucked tone convolved with the (shaped) IR, rendered offline. Returns a stereo buffer at the IR's rate.
inline Buf audition(const Buf& irb) {
    const double sr = irb.sr; const int burstN = (int)(sr * 0.8), len = (int)std::min<size_t>(irb.L.size(), (size_t)(6 * sr)), total = burstN + len;
    std::vector<float> x((size_t)total, 0.f); uint32_t st = 12345;
    for (int i = 0; i < burstN; i++) { const double t = i / sr; st = st * 1664525u + 1013904223u; const double nz = ((double)st / 4294967296.0 * 2 - 1) * std::exp(-t * 60), tone = std::sin(2 * M_PI * 330 * t) * std::exp(-t * 7) * 0.5; x[(size_t)i] = (float)((nz * 0.7 + (t < 0.4 ? tone : 0)) * 0.6); }
    PartConv pc; pc.build(irb.L.data(), irb.R.empty() ? irb.L.data() : irb.R.data(), len); Buf o; o.sr = sr; o.name = irb.name; const int padded = (total + kChunk - 1) / kChunk * kChunk; x.resize((size_t)padded, 0.f); o.L.assign((size_t)padded, 0.f); o.R.assign((size_t)padded, 0.f);
    for (int i = 0; i < padded; i += kChunk) pc.process(&x[(size_t)i], &x[(size_t)i], &o.L[(size_t)i], &o.R[(size_t)i], kChunk);
    o.L.resize((size_t)total); o.R.resize((size_t)total); return o;
}
// Envelope for the IR display: `bins` values 0..1 (level of the loudest sample in each bin, -70 dB .. 0 dB of the IR's peak).
inline void envelope(const Buf& b, float* out, int bins) {
    const size_t N = b.L.size(); const std::vector<float>& R = b.R.empty() ? b.L : b.R; double pk = 1e-9; for (size_t i = 0; i < N; i++) pk = std::max(pk, (double)std::max(std::fabs(b.L[i]), std::fabs(R[i])));
    for (int k = 0; k < bins; k++) { const size_t a = N * (size_t)k / (size_t)bins, e = std::max(a + 1, N * (size_t)(k + 1) / (size_t)bins); double m = 0; for (size_t i = a; i < e && i < N; i += std::max<size_t>(1, (e - a) >> 4)) m = std::max(m, (double)std::max(std::fabs(b.L[i]), std::fabs(R[i])));
        out[k] = (float)clampv((20 * std::log10(std::max(1e-6, m)) - 20 * std::log10(pk) + 70) / 70, 0, 1); }
}
}}  // namespace vv::ir
