// Microphone / speaker colouring for sampler slots: the web app's MIC_TYPES / SPK_TYPES tables (a short biquad chain per choice, plus an
// optional soft-clip drive). ColBiquad maths follow the Web Audio BiquadFilterNode spec (RBJ cookbook; lowpass/highpass Q in dB).
// Differences from the browser: the soft-clip runs at the base rate (the browser's WaveShaper uses 2x oversampling).
#pragma once
#include <cmath>
#include <cstring>
#include <string>
#include <algorithm>
#include "synth_common.hpp"

namespace vv {

namespace colour {
enum BqType { HP = 0, LP, PEAK, LSHELF, HSHELF };
enum FxKind { FX_SAT = 0, FX_CRUSH, FX_RING, FX_ECHO, FX_WOBBLE, FX_COMB };
struct Stage { int type; double f, q, gain; };
// id, biquad chain, soft-clip drive (0 = none), character FX (kind < 0: none) with its two parameters
struct Entry { const char* id; int n; Stage st[4]; double drive; int fx; double fa, fb; };
// ids and values are identical to MIC_TYPES / SPK_TYPES in full-app.html
static const Entry kMic[] = {
    {"none", 0, {}, 0, -1, 0, 0},
    {"sm57", 3, {{HP, 90, 0.7, 0}, {PEAK, 5500, 1.2, 5}, {LP, 14000, 0.7, 0}}, 0, FX_SAT, 0.3, 0},
    {"u87", 3, {{HP, 40, 0.7, 0}, {PEAK, 9000, 0.8, 3}, {HSHELF, 12000, 0.7, 2}}, 0, FX_ECHO, 0.012, 0.25},
    {"r121", 3, {{HP, 50, 0.7, 0}, {LSHELF, 200, 0.7, 3}, {LP, 9000, 0.7, 0}}, 0, FX_WOBBLE, 0.6, 1.2},
    {"u47", 3, {{LSHELF, 120, 0.7, 2.5}, {PEAK, 3500, 1, 2}, {LP, 16000, 0.7, 0}}, 0.15, FX_SAT, 0.6, 0},
    {"c414", 3, {{HP, 40, 0.7, 0}, {PEAK, 3000, 0.8, 2}, {HSHELF, 11000, 0.7, 4}}, 0, FX_ECHO, 0.018, 0.3},
    {"sm7", 4, {{HP, 60, 0.7, 0}, {LSHELF, 150, 0.7, 3}, {PEAK, 6000, 1, -3}, {LP, 12000, 0.7, 0}}, 0, FX_SAT, 0.2, 0},
    {"coles", 4, {{HP, 40, 0.7, 0}, {LSHELF, 180, 0.7, 2}, {HSHELF, 8000, 0.7, -4}, {LP, 11000, 0.7, 0}}, 0, FX_WOBBLE, 0.4, 1.8},
    {"phone", 3, {{HP, 400, 1.2, 0}, {LP, 3200, 1.2, 0}, {PEAK, 1500, 2, 6}}, 0.3, FX_CRUSH, 6, 0},
    {"walkie", 3, {{HP, 500, 1, 0}, {LP, 2800, 1.5, 0}, {PEAK, 2000, 3, 8}}, 0.6, FX_RING, 55, 0},
    {"piezo", 3, {{HP, 150, 0.7, 0}, {PEAK, 4200, 4, 9}, {LP, 8000, 0.7, 0}}, 0.35, FX_COMB, 2200, 0.6},
    {"tincan", 3, {{HP, 250, 1, 0}, {PEAK, 1200, 6, 10}, {LP, 3500, 1, 0}}, 0.25, FX_COMB, 900, 0.75},
    {"lav", 3, {{HP, 180, 0.7, 0}, {PEAK, 6500, 1.5, 6}, {LP, 10000, 0.7, 0}}, 0, FX_COMB, 5200, 0.35},
    {"boundary", 3, {{HP, 120, 0.7, 0}, {PEAK, 800, 0.8, -3}, {HSHELF, 7000, 0.7, 3}}, 0, FX_ECHO, 0.045, 0.5},
    {"hydro", 3, {{HP, 60, 0.7, 0}, {LP, 1800, 1.5, 0}, {PEAK, 400, 2, 5}}, 0, FX_WOBBLE, 2.5, 3.5},
    {"babymon", 3, {{HP, 300, 1, 0}, {LP, 4200, 1.2, 0}, {PEAK, 2500, 3, 7}}, 0.2, FX_CRUSH, 7, 0},
    {"carbon", 3, {{HP, 350, 1, 0}, {LP, 3000, 1.4, 0}, {PEAK, 1400, 2.5, 8}}, 0.5, FX_SAT, 1.4, 0}};
static const Entry kSpk[] = {
    {"none", 0, {}, 0, -1, 0, 0},
    {"studio", 2, {{HP, 45, 0.7, 0}, {LP, 20000, 0.7, 0}}, 0, FX_ECHO, 0.008, 0.15},
    {"cab412", 3, {{HP, 90, 0.8, 0}, {PEAK, 2500, 1.2, 5}, {LP, 5500, 1.2, 0}}, 0.25, FX_SAT, 0.8, 0},
    {"hifi", 2, {{LSHELF, 80, 0.7, 3}, {HSHELF, 9000, 0.7, 2}}, 0, FX_WOBBLE, 0.3, 0.6},
    {"tuberadio", 3, {{HP, 150, 0.8, 0}, {LP, 4500, 1, 0}, {PEAK, 900, 1.5, 5}}, 0.3, FX_SAT, 0.9, 0},
    {"nearfield", 3, {{HP, 55, 0.7, 0}, {PEAK, 2000, 1, 1.5}, {HSHELF, 10000, 0.7, 1.5}}, 0, FX_ECHO, 0.006, 0.2},
    {"cab112", 4, {{HP, 80, 0.8, 0}, {PEAK, 1800, 1.4, 5}, {PEAK, 4000, 2, 3}, {LP, 6000, 1.2, 0}}, 0.2, FX_SAT, 0.7, 0},
    {"sub18", 2, {{LP, 120, 0.9, 0}, {LSHELF, 60, 0.7, 5}}, 0, FX_SAT, 0.4, 0},
    {"jukebox", 3, {{HP, 100, 0.8, 0}, {PEAK, 700, 1.2, 4}, {LP, 7000, 0.8, 0}}, 0.1, FX_ECHO, 0.03, 0.35},
    {"oldtv", 4, {{HP, 120, 1, 0}, {LP, 6000, 1, 0}, {PEAK, 15700, 8, 9}, {PEAK, 1000, 1.2, 4}}, 0.35, FX_RING, 50, 0},
    {"pocket", 3, {{HP, 350, 1, 0}, {LP, 3800, 1.4, 0}, {PEAK, 1800, 2, 6}}, 0.5, FX_CRUSH, 6, 0},
    {"laptop", 3, {{HP, 280, 1.2, 0}, {LP, 9000, 1, 0}, {PEAK, 3000, 2, 4}}, 0.15, FX_COMB, 3400, 0.5},
    {"mega", 3, {{HP, 500, 1.2, 0}, {LP, 3500, 1.5, 0}, {PEAK, 1800, 2.5, 8}}, 0.7, FX_SAT, 2.2, 0},
    {"intercom", 3, {{HP, 450, 1, 0}, {LP, 3000, 1.6, 0}, {PEAK, 1700, 3, 8}}, 0.3, FX_RING, 120, 0},
    {"handheld", 3, {{HP, 500, 1, 0}, {LP, 6000, 1, 0}, {PEAK, 3200, 3, 6}}, 0.2, FX_CRUSH, 5, 0},
    {"btmicro", 3, {{HP, 200, 1, 0}, {LP, 8500, 0.8, 0}, {PEAK, 2200, 1.5, 5}}, 0.1, FX_COMB, 2800, 0.55},
    {"bucket", 3, {{HP, 150, 1, 0}, {PEAK, 500, 5, 9}, {LP, 4500, 1.2, 0}}, 0.15, FX_COMB, 480, 0.8}};
template <size_t N> inline const Entry* find(const Entry (&tbl)[N], const std::string& id) { for (size_t i = 1; i < N; i++) if (id == tbl[i].id) return &tbl[i]; return nullptr; }
}  // namespace colour

struct ColBiquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set(int type, double f, double q, double gainDb, double sr) {
        const double w0 = 2 * 3.141592653589793 * std::min(f, sr * 0.499) / sr, c = std::cos(w0), s = std::sin(w0);
        const double A = std::pow(10.0, gainDb / 40.0); double n0, n1, n2, d0, d1, d2;
        switch (type) {
            case colour::HP: { const double al = s / (2 * std::pow(10.0, q / 20)); n0 = (1 + c) / 2; n1 = -(1 + c); n2 = (1 + c) / 2; d0 = 1 + al; d1 = -2 * c; d2 = 1 - al; break; }
            case colour::LP: { const double al = s / (2 * std::pow(10.0, q / 20)); n0 = (1 - c) / 2; n1 = 1 - c; n2 = (1 - c) / 2; d0 = 1 + al; d1 = -2 * c; d2 = 1 - al; break; }
            case colour::PEAK: { const double al = s / (2 * q); n0 = 1 + al * A; n1 = -2 * c; n2 = 1 - al * A; d0 = 1 + al / A; d1 = -2 * c; d2 = 1 - al / A; break; }
            case colour::LSHELF: { const double al = s / 2 * std::sqrt(2.0), t = 2 * std::sqrt(A) * al;
                n0 = A * ((A + 1) - (A - 1) * c + t); n1 = 2 * A * ((A - 1) - (A + 1) * c); n2 = A * ((A + 1) - (A - 1) * c - t);
                d0 = (A + 1) + (A - 1) * c + t; d1 = -2 * ((A - 1) + (A + 1) * c); d2 = (A + 1) + (A - 1) * c - t; break; }
            default: { const double al = s / 2 * std::sqrt(2.0), t = 2 * std::sqrt(A) * al;
                n0 = A * ((A + 1) + (A - 1) * c + t); n1 = -2 * A * ((A - 1) + (A + 1) * c); n2 = A * ((A + 1) + (A - 1) * c - t);
                d0 = (A + 1) - (A - 1) * c + t; d1 = 2 * ((A - 1) - (A + 1) * c); d2 = (A + 1) - (A - 1) * c - t; break; }
        }
        b0 = n0 / d0; b1 = n1 / d0; b2 = n2 / d0; a1 = d1 / d0; a2 = d2 / d0;
    }
    inline double tick(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void clear() { z1 = z2 = 0; }
};

// Character-FX state for one mic/speaker (the web's buildColFx): sat / crush / ring / echo / wobble / comb. Pool-allocated (no heap use on the audio thread).
struct ColFx {
    static constexpr int kLine = 8192;   // longest delay used by any table entry is 45 ms (4320 frames at 96 kHz)
    float line[kLine]; float fbh[128]; int w = 0, fi = 0; double ph = 0, y = 0; bool used = false;   // fbh: feedback history (a Web Audio DelayNode inside a loop adds one 128-frame render quantum to the loop)
    void clear() { std::memset(line, 0, sizeof(line)); std::memset(fbh, 0, sizeof(fbh)); w = 0; fi = 0; ph = 0; y = 0; }
};
// One sampler slot's microphone + speaker chain (up to 8 biquads, two soft-clip stages and two character-FX blocks). Fixed size: no allocation.
struct MicSpkChain {
    static constexpr int kMax = 8;
    struct Seg { int b0 = 0, b1 = 0; double drive = 0; int fx = -1; double fa = 0, fb = 0; ColFx* st = nullptr; double pre = 1, post = 1, delay = 0; Smoother mix; };
    ColBiquad bq[kMax]; int n = 0; Seg seg[2]; int nSeg = 0; bool active = false; double sr_ = 48000;
    // wantFx[k]: build mic (0) / speaker (1) character FX (web: only when its mix > 0 or the mix dial is modulated). pool[k]: state to use (or null).
    void setup(const std::string& mic, const std::string& spk, double sr, const double* mixInit, const bool* wantFx, ColFx** pool) {
        n = 0; nSeg = 0; active = false; sr_ = sr;
        const colour::Entry* e[2] = {colour::find(colour::kMic, mic), colour::find(colour::kSpk, spk)};
        for (int k = 0; k < 2; k++) {
            Seg& g = seg[k]; g = Seg{}; g.b0 = g.b1 = n;
            if (!e[k]) continue;
            for (int i = 0; i < e[k]->n && n < kMax; i++) { bq[n].set(e[k]->st[i].type, e[k]->st[i].f, e[k]->st[i].q, e[k]->st[i].gain, sr); bq[n].clear(); n++; }
            g.b1 = n; g.drive = e[k]->drive; active = true;
            if (e[k]->fx >= 0 && wantFx[k] && pool[k]) {
                g.fx = e[k]->fx; g.fa = e[k]->fa; g.fb = e[k]->fb; g.st = pool[k]; g.st->clear();
                g.mix.set(mixInit[k] < 0 ? 0 : (mixInit[k] > 1 ? 1 : mixInit[k])); g.mix.setTarget(g.mix.v, sr, 0.02);
                if (g.fx == colour::FX_SAT) { g.pre = 1 + g.fa; g.post = 1 / (1 + g.fa * 0.5); }
                if (g.fx == colour::FX_ECHO) g.delay = g.fa;
                if (g.fx == colour::FX_COMB) g.delay = 1.0 / std::max(40.0, g.fa);
            }
        }
        nSeg = 2;
    }
    // live Mic / Speaker FX mix (k = 0 mic, 1 speaker), smoothed like setTargetAtTime(m, now, 0.02)
    void setMix(int k, double m) { if (seg[k].fx >= 0) seg[k].mix.setTarget(m < 0 ? 0 : (m > 1 ? 1 : m), sr_, 0.02); }
    bool hasFx(int k) const { return seg[k].fx >= 0; }
    static inline double clampc(double x) { return x < -1 ? -1 : (x > 1 ? 1 : x); }
    inline double readLine(const ColFx& s, double d) const {   // linear-interpolated read d frames behind the write head
        double rp = (double)s.w - d; while (rp < 0) rp += ColFx::kLine; const int i0 = (int)rp; const double f = rp - i0;
        return s.line[i0 % ColFx::kLine] * (1 - f) + s.line[(i0 + 1) % ColFx::kLine] * f;
    }
    inline double fxTick(Seg& g, double x) {
        ColFx& s = *g.st; double y = 0;
        switch (g.fx) {
            case colour::FX_SAT: { const double amt = std::min(1.0, g.fa / 2.2), k = 1 + amt * 8; y = std::tanh(k * clampc(x * g.pre)) / std::tanh(k) * g.post; break; }
            case colour::FX_CRUSH: {   // WaveShaper with a 4096-point staircase curve (linear interpolation between curve points)
                const double lv = std::pow(2.0, g.fa - 1), p = (clampc(x) + 1) * 0.5 * 4095.0; const int i0 = (int)std::floor(p); const double f = p - i0;
                auto cv = [&](int i) { const double xx = (double)i / 4095.0 * 2 - 1; return std::round(xx * lv) / lv; };
                y = i0 >= 4095 ? cv(4095) : cv(i0) * (1 - f) + cv(i0 + 1) * f; break; }
            case colour::FX_RING: { y = x * std::sin(2 * 3.141592653589793 * s.ph); s.ph += g.fa / sr_; if (s.ph >= 1) s.ph -= 1; break; }
            case colour::FX_ECHO: { const double old = s.fbh[s.fi]; const double d = readLine(s, g.delay * sr_); s.line[s.w] = (float)(x + g.fb * old); s.fbh[s.fi] = (float)d; s.fi = (s.fi + 1) & 127; s.w = (s.w + 1) % ColFx::kLine; y = d; break; }
            case colour::FX_WOBBLE: {
                const double dt = 0.006 + std::sin(2 * 3.141592653589793 * s.ph) * (g.fb / 1000.0); s.ph += g.fa / sr_; if (s.ph >= 1) s.ph -= 1;
                s.line[s.w] = (float)x; y = readLine(s, std::max(0.0, dt) * sr_ + 0.0); s.w = (s.w + 1) % ColFx::kLine; break; }
            default: {   // comb: s2 = x + fb * delayed(s2); out = s2 / (1 + fb)
                const double old = s.fbh[s.fi]; const double s2 = x + g.fb * old; s.line[s.w] = (float)s2; const double d = readLine(s, g.delay * sr_); s.fbh[s.fi] = (float)d; s.fi = (s.fi + 1) & 127; s.w = (s.w + 1) % ColFx::kLine; y = s2 * (1.0 / (1.0 + g.fb)); break; }
        }
        return y;
    }
    inline double tick(double x) {
        if (!active) return x;
        for (int k = 0; k < nSeg; k++) {
            Seg& g = seg[k];
            for (int i = g.b0; i < g.b1; i++) x = bq[i].tick(x);
            if (g.drive > 0) { const double kk = 1 + g.drive * 8; x = std::max(-1.0, std::min(1.0, x)); x = std::tanh(kk * x) / std::tanh(kk); }
            if (g.fx >= 0) { const double m = g.mix.tick(); const double fxo = fxTick(g, x); x = x * (1 - m) + fxo * m; }
        }
        return x;
    }
};

}  // namespace vv
