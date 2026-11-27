// viz_host.hpp - UI-thread host for the LUA Visual engine: audio analysis of the scope snapshot (never touches the audio thread),
// per-parameter band reactivity, and the glue that drives lx::viz::Engine.  Mirrors the web VizAudio/VizParams.  All buffers are
// members (no allocation after construction).
#pragma once
#include "lx_viz.hpp"
#include <cmath>
#include <cstring>
namespace vv { namespace viz {
using lx::viz::Engine;
constexpr int kN = 2048, kBins = kN / 2, kBands = 9;
struct AudioCfg { bool enabled = true, logResponse = true, stereo = false; float sensitivity = 1, gain = 1, smoothing = .35f, attack = .02f, release = .22f, threshold = .02f; float cross[6] = { 60, 250, 500, 2000, 4000, 8000 }; float user[2] = { 150, 1800 }; };
struct Assign { bool enabled = false; int band = -1; float depth = .5f, smooth = .25f, lo = 0, hi = 1; int pol = 1; };
struct Param { float base = 0, cur = 0, modSm = 0; Assign a; };

struct Analyzer {
    AudioCfg cfg; float sampleRate = 48000; float env[kBands] = {}, sm[kBands] = {}, out[kBands] = {}, spec32[32] = {}; float level = 0, peak = 0, flux = 0, left = 0, right = 0, width = 0, centroid = 0, peakHz = 0, beat = 0, t = 0;
    float avgBass = 0, prevBass = 0, lastBeat = -1; float win[kN], cs[kN / 2], sn[kN / 2]; float re[kN], im[kN], db[kBins + 1], dbL[kBins + 1], dbR[kBins + 1], prev[kBins + 1]; bool hasPrev = false;
    Analyzer() { for (int i = 0; i < kN; i++) win[i] = .5f - .5f * std::cos(6.2831853f * i / kN); for (int i = 0; i < kN / 2; i++) { cs[i] = std::cos(6.2831853f * i / kN); sn[i] = -std::sin(6.2831853f * i / kN); } for (int i = 0; i <= kBins; i++) db[i] = dbL[i] = dbR[i] = prev[i] = -120; }
    // radix-2 FFT of x (kN real samples, windowed) -> dB per bin, same convention as WebAudio getFloatFrequencyData (|X|/N, 20 log10)
    void fftDb(const float* x, float* outDb) {
        for (int i = 0; i < kN; i++) { re[i] = x[i] * win[i]; im[i] = 0; }
        for (int i = 1, j = 0; i < kN; i++) { int bit = kN >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { float a = re[i]; re[i] = re[j]; re[j] = a; } }
        for (int len = 2; len <= kN; len <<= 1) { int step = kN / len; for (int i = 0; i < kN; i += len) for (int k = 0; k < len / 2; k++) { float wr = cs[k * step], wi = sn[k * step]; int a = i + k, b = i + k + len / 2; float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr; re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti; } }
        for (int i = 0; i <= kBins; i++) { float m = std::sqrt(re[i] * re[i] + im[i] * im[i]) / (float)kN * 2.f; float d = 20.f * std::log10(m + 1e-9f); outDb[i] = d < -100.f ? -100.f : (d > -5.f ? -5.f : d); }
    }
    void edges(float* e) const { e[0] = 20; for (int i = 0; i < 6; i++) e[i + 1] = cfg.cross[i]; e[7] = 20000; for (int i = 1; i < 8; i++) if (e[i] <= e[i - 1]) e[i] = e[i - 1] + 1; }
    void ranges(float r[kBands][2]) const { float e[8]; edges(e); r[0][0] = 20; r[0][1] = 20000; for (int i = 0; i < 7; i++) { r[i + 1][0] = e[i]; r[i + 1][1] = e[i + 1]; } r[8][0] = std::fmin(cfg.user[0], cfg.user[1]); r[8][1] = std::fmax(cfg.user[0], cfg.user[1]); }
    float respond(float d, float hz) const { float x; if (cfg.logResponse) { float tilt = 3.f * std::log2(std::fmax(hz, 40.f) / 1000.f); x = (d + tilt + 72.f) / 58.f; } else { float amp = std::pow(10.f, d / 20.f); x = std::fmin(1.f, amp * 14.f); }
        x = x * cfg.gain * cfg.sensitivity; float th = cfg.threshold; x = (x - th) / (1.f - th); return x < 0 ? 0 : (x > 1 ? 1 : x); }
    float bandEnergy(const float* f, float binHz, float lo, float hi) const { int a = (int)std::fmax(1.f, std::floor(lo / binHz)); int b = (int)std::fmin((float)(kBins - 1), std::fmax((float)a, std::ceil(hi / binHz))); float p = 0, mx = -200; int n = 0;
        for (int i = a; i <= b; i++) { float d = f[i]; if (d > mx) mx = d; p += std::pow(10.f, d / 10.f); n++; } float mean = 10.f * std::log10(p / (n < 1 ? 1 : n) + 1e-12f); return mean * .5f + mx * .5f; }
    void decay(float dt) { for (int i = 0; i < kBands; i++) { env[i] *= std::exp(-dt * 6.f); out[i] = env[i]; } level *= .9f; beat *= .9f; }
    // L/R are the newest kN samples of the master output
    void update(const float* L, const float* R, float dt, float sr) {
        t += dt; sampleRate = sr; if (!cfg.enabled) { decay(dt); return; }
        float mono[kN]; double ss = 0; float pk = 0; for (int i = 0; i < kN; i++) { float v = .5f * (L[i] + R[i]); mono[i] = v; ss += v * v; float a = std::fabs(v); if (a > pk) pk = a; }
        level = std::fmin(1.f, std::sqrt((float)(ss / kN)) * 3.2f * cfg.gain); peak = std::fmin(1.f, pk * cfg.gain); fftDb(mono, db); float binHz = sr / kN; float R9[kBands][2]; ranges(R9); float raw[kBands];
        for (int i = 0; i < kBands; i++) raw[i] = respond(bandEnergy(db, binHz, R9[i][0], R9[i][1]), std::sqrt(R9[i][0] * R9[i][1]));
        if (cfg.stereo) { double sl = 0, sr2 = 0, sm2 = 0, ssd = 0; for (int i = 0; i < kN; i++) { float l = L[i], r = R[i], m = (l + r) * .5f, s = (l - r) * .5f; sl += l * l; sr2 += r * r; sm2 += m * m; ssd += s * s; }
            left = std::fmin(1.f, std::sqrt((float)(sl / kN)) * 3.2f * cfg.gain); right = std::fmin(1.f, std::sqrt((float)(sr2 / kN)) * 3.2f * cfg.gain); width = (sm2 + ssd) > 1e-9 ? std::sqrt((float)(ssd / (sm2 + ssd))) : 0;
            fftDb(L, dbL); fftDb(R, dbR); for (int i = 0; i < kBands; i++) { float hz = std::sqrt(R9[i][0] * R9[i][1]); float l = respond(bandEnergy(dbL, binHz, R9[i][0], R9[i][1]), hz), r = respond(bandEnergy(dbR, binHz, R9[i][0], R9[i][1]), hz); raw[i] = std::fmax(raw[i] * .5f + (l + r) * .25f, std::fmax(l, r) * .8f); } }
        else { left = right = level; width = 0; }
        float ka = 1.f - std::exp(-dt / std::fmax(.001f, cfg.attack)), kr = 1.f - std::exp(-dt / std::fmax(.005f, cfg.release)), ks = cfg.smoothing <= .001f ? 1.f : 1.f - std::exp(-dt / (cfg.smoothing * .25f));
        for (int i = 0; i < kBands; i++) { float e = env[i]; env[i] = e + (raw[i] - e) * (raw[i] > e ? ka : kr); sm[i] += (env[i] - sm[i]) * ks; out[i] = sm[i]; }
        float fl = 0; int n = kBins < 400 ? kBins : 400; for (int i = 2; i < n; i++) { float d = db[i] - prev[i]; if (d > 0) fl += d; prev[i] = db[i]; } flux = std::fmin(1.f, fl / (n * 4.5f));
        float bs = std::fmax(env[1], env[2]); avgBass += (bs - avgBass) * (1.f - std::exp(-dt / .8f)); beat *= std::exp(-dt * 7.f);
        if (bs > avgBass * 1.3f + .08f && bs > prevBass + .015f && t - lastBeat > .12f) { beat = 1; lastBeat = t; } prevBass = bs;
        double wsum = 0, msum = 0; float pkv = -200; int pki = 0, top = (int)std::fmin((float)kBins, std::floor(10000.f / binHz)); for (int i = 2; i < top; i++) { double m = std::pow(10.0, db[i] / 20.0); wsum += m * i * binHz; msum += m; if (db[i] > pkv) { pkv = db[i]; pki = i; } }
        float cen = msum > 1e-9 ? (float)(wsum / msum) : 0; centroid = cen > 0 ? std::fmax(0.f, std::fmin(1.f, std::log(cen / 40.f) / std::log(250.f))) : 0;
        if (pkv > -78 && pki > 1) { float a = db[pki - 1], b = db[pki], c2 = pki + 1 <= kBins ? db[pki + 1] : b; float den = a - 2 * b + c2; float off = std::fabs(den) > 1e-6f ? .5f * (a - c2) / den : 0; off = off < -.5f ? -.5f : (off > .5f ? .5f : off); float hz = (pki + off) * binHz; peakHz += (hz - peakHz) * std::fmin(1.f, dt * 14.f); } else peakHz *= std::exp(-dt * 2.f);
        for (int k = 0; k < 32; k++) { float lo = 30.f * std::pow(16000.f / 30.f, k / 32.f), hi = 30.f * std::pow(16000.f / 30.f, (k + 1) / 32.f); float v = respond(bandEnergy(db, binHz, lo, hi), std::sqrt(lo * hi)); spec32[k] += (v - spec32[k]) * (v > spec32[k] ? ka : kr); }
    }
    // synthetic "preview without sound" signal (same as the web demoStep)
    void demo(float dt) { t += dt; float bpm = 108, ph = std::fmod(t * bpm / 60.f, 1.f), kick = std::exp(-ph * 7.f); out[0] = .5f; out[1] = .25f + .55f * kick; out[2] = .3f + .45f * kick + .15f * std::sin(t * .9f); out[3] = .35f + .2f * std::sin(t * 1.3f); out[4] = .4f + .3f * std::sin(t * 2.1f + 1); out[5] = .3f + .25f * std::sin(t * 3.3f); out[6] = .25f + .3f * std::fmax(0.f, std::sin(t * 7.f)); out[7] = .2f + .25f * std::fmax(0.f, std::sin(t * 9.f + 2)); out[8] = .4f;
        level = .35f + .3f * kick; peak = std::fmin(1.f, level + .2f); beat = ph < .03f ? 1.f : beat * std::exp(-dt * 7.f); flux = .1f + .3f * kick; left = right = level; width = .25f + .1f * std::sin(t * .3f); peakHz = 110.f * std::pow(2.f, 1.5f + 1.5f * std::sin(t * .15f)); centroid = .4f + .25f * std::sin(t * .31f);
        for (int k = 0; k < 32; k++) spec32[k] = std::fmax(0.f, .75f - k / 48.f + .22f * std::sin(t * 2.2f + k * .5f) + .25f * kick * (k < 8 ? 1.f : 0.f)); }
    void fill(Engine& E) const { for (int i = 0; i < kBands; i++) E.audio[i] = out[i]; E.audio[9] = level; E.audio[10] = peak; E.audio[11] = beat; E.audio[12] = flux; E.audio[13] = left; E.audio[14] = right; E.audio[15] = width; E.audio[16] = t; E.audio[17] = centroid; E.audio[18] = peakHz; for (int k = 0; k < 32; k++) E.spec[k] = spec32[k]; }
};

// per-parameter base value + band assignment; step() writes effective values (slider slew ~60 ms + audio modulation)
struct Params {
    Param p[lx::viz::kMaxParams]; bool masterEnabled = true; float depthScale = 1; uint32_t rng = 0x9e3779b9u;
    float rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.f; }
    void setup(Engine& E) { for (int i = 0; i < E.nparams; i++) { auto& d = E.params[i]; Param& o = p[i]; o = Param(); o.base = o.cur = d.def; o.a.enabled = d.band >= 0 && d.kind == 0; o.a.band = d.band; o.a.depth = d.depth; o.a.pol = d.pol ? (int)d.pol : 1; o.a.lo = d.mn; o.a.hi = d.mx; } }
    void step(Engine& E, float dt, const float* bands, bool active) {
        float slew = 1.f - std::exp(-dt / .06f);
        for (int i = 0; i < E.nparams; i++) { auto& d = E.params[i]; Param& o = p[i]; if (d.kind == 2 || d.kind == 3) { o.cur = o.base; E.pv[i] = o.base; continue; }
            o.cur += (o.base - o.cur) * slew; float v = o.cur;
            if (active && masterEnabled && o.a.enabled && o.a.band >= 0) { float b = bands[o.a.band], span = d.mx - d.mn; float target = (o.a.pol == 0 ? (b * 2 - 1) : o.a.pol * b) * o.a.depth * depthScale * span; float k = o.a.smooth <= .001f ? 1.f : 1.f - std::exp(-dt / (o.a.smooth * .4f)); o.modSm += (target - o.modSm) * k; v += o.modSm; v = std::fmax(o.a.lo, std::fmin(o.a.hi, v)); }
            else o.modSm *= std::exp(-dt * 8.f);
            E.pv[i] = std::fmax(d.mn, std::fmin(d.mx, v)); } }
    void randomiseAssign(Engine& E) { for (int i = 0; i < E.nparams; i++) { auto& d = E.params[i]; if (d.kind == 2 || d.kind == 3) continue; Assign& a = p[i].a; a.enabled = rnd() < .6f; a.band = (int)(rnd() * kBands) % kBands; a.depth = .15f + rnd() * .6f; a.pol = rnd() < .75f ? 1 : (rnd() < .5f ? -1 : 0); } }
    void randomiseValues(Engine& E, bool skipFreeze) { for (int i = 0; i < E.nparams; i++) { auto& d = E.params[i]; if (d.kind == 2) continue; if (skipFreeze && (!std::strcmp(d.id, "freeze") || !std::strcmp(d.id, "freezeColor"))) continue; float v = d.mn + rnd() * (d.mx - d.mn); if (d.step >= 1) v = std::round(v); if (d.kind == 3) v = std::floor(rnd() * d.nopts); p[i].base = v; } }
    void reset(Engine& E) { for (int i = 0; i < E.nparams; i++) p[i].base = E.params[i].def; }
};
}}
