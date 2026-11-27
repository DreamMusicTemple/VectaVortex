// ECHO - natural spatial echo (port of EchoCore in the web app, same algorithm, same constants).
//   * two feedback lines (L / R, slightly different lengths) carrying a field of reflections: early cluster + late echoes + satellite micro-taps
//   * every tap: its own modulated delay, age-dependent high-frequency absorption, level from the Decay law, a stereo position that can drift
//   * feedback path: low/high damping, two allpass diffusers, cross-feed and a soft limiter (bounded for any setting)
//   * deterministic reflection patterns (seeded per environment / count); no allocation in process()
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vv {
namespace echo {

struct Env { const char* name; double size, early, pw, irr, decay, hfAbs, lfAbs, tone, loop, diff, width, modB; double sat[3]; double satG; };
inline const Env kEnvs[9] = {
    { "Small Wooden Room",        0.30, 0.65, 1.00, 0.35, 0.45, 0.55, 0.20, 0.92, 0.90, 0.80, 0.75, 0.0, { 1.7, 3.1, 4.9 },   0.55 },
    { "Warm Recording Studio",    0.42, 0.60, 1.10, 0.30, 0.55, 0.45, 0.15, 0.85, 0.85, 0.60, 0.85, 0.1, { 2.1, 3.7, 5.9 },   0.50 },
    { "Large Wooden Hall",        1.00, 0.40, 1.15, 0.40, 1.20, 0.60, 0.25, 0.85, 0.85, 0.90, 1.00, 0.1, { 3.3, 6.1, 9.7 },   0.58 },
    { "Stone Chamber",            0.70, 0.45, 1.00, 0.45, 1.50, 0.25, 0.10, 1.00, 1.00, 0.70, 0.90, 0.0, { 2.6, 5.3, 8.1 },   0.60 },
    { "Cathedral",                1.90, 0.35, 1.25, 0.50, 2.80, 0.50, 0.10, 0.95, 0.90, 1.00, 1.00, 0.2, { 5.1, 9.3, 14.9 },  0.62 },
    { "Mountain Canyon",          2.20, 0.20, 1.90, 0.55, 2.20, 0.85, 0.30, 0.90, 0.80, 0.25, 1.00, 0.0, { 4.7, 8.9, 13.3 },  0.40 },
    { "Deep Cave",                1.40, 0.30, 1.30, 0.50, 3.50, 1.20, 0.05, 0.70, 0.65, 0.95, 0.85, 0.1, { 3.9, 7.7, 12.1 },  0.62 },
    { "Open Landscape",           3.00, 0.12, 1.60, 0.60, 1.10, 1.00, 0.50, 0.95, 0.80, 0.10, 1.00, 0.0, { 6.3, 11.1, 17.3 }, 0.30 },
    { "Dreamlike Infinite Space", 2.00, 0.25, 1.20, 0.70, 7.00, 0.40, 0.20, 0.90, 0.90, 1.00, 1.00, 1.0, { 4.1, 8.3, 13.7 },  0.65 }
};

// factory presets (same values as the web app); order of v[] = kPresetKeys
inline const char* const kPresetKeys[18] = { "env", "time", "count", "density", "feedback", "decay", "diffusion", "width", "spread", "early", "late", "hfDamp", "lfDamp", "modDepth", "modRate", "movement", "mix", "level" };
struct Preset { const char* name; double v[18]; };
inline const Preset kPresets[10] = {
    { "Bamboo Temple", { 2, 0.38, 9, 0.5, 0.38, 0.5, 0.6, 1, 0.8, 0.55, 0.8, 0.5, 0.2, 0.25, 0.4, 0.4, 0.32, 0 } },
    { "Shakuhachi Canyon", { 5, 0.9, 6, 0.25, 0.5, 0.55, 0.3, 1.1, 0.9, 0.3, 1, 0.6, 0.15, 0.15, 0.3, 0.3, 0.4, 0 } },
    { "Ancient Stone Chamber", { 3, 0.5, 10, 0.6, 0.5, 0.6, 0.7, 0.95, 0.75, 0.6, 0.75, 0.3, 0.1, 0.1, 0.3, 0.2, 0.35, 0 } },
    { "Infinite Reflections", { 8, 1.6, 14, 0.8, 0.82, 0.85, 1, 1.2, 0.9, 0.4, 0.9, 0.35, 0.2, 0.5, 0.3, 0.6, 0.45, -2 } },
    { "Warm Wooden Hall", { 2, 0.45, 8, 0.5, 0.4, 0.5, 0.6, 1, 0.7, 0.6, 0.8, 0.55, 0.2, 0.2, 0.4, 0.3, 0.3, 0 } },
    { "Dream Echo", { 8, 0.75, 10, 0.6, 0.6, 0.7, 0.9, 1.25, 0.85, 0.35, 0.9, 0.45, 0.2, 0.55, 0.3, 0.7, 0.4, 0 } },
    { "Floating Atmosphere", { 7, 1.2, 5, 0.4, 0.55, 0.6, 0.4, 1.35, 1, 0.2, 1, 0.5, 0.25, 0.35, 0.25, 0.8, 0.4, 0 } },
    { "Distant Horizons", { 7, 2.4, 4, 0.2, 0.6, 0.7, 0.2, 1.2, 0.9, 0.1, 1, 0.75, 0.3, 0.15, 0.2, 0.4, 0.35, 0 } },
    { "Deep Space Reflections", { 6, 1.9, 12, 0.7, 0.78, 0.9, 0.95, 1.2, 0.85, 0.3, 0.9, 0.7, 0.35, 0.4, 0.25, 0.55, 0.45, -2 } },
    { "Organic Tape Echo", { 1, 0.32, 3, 0.15, 0.5, 0.45, 0.25, 0.6, 0.35, 0.2, 1, 0.55, 0.3, 0.35, 0.6, 0.2, 0.35, 0 } }
};

struct Params {
    int env = 2, count = 8;
    double time = 0.42, density = 0.4, feedback = 0.40, decay = 0.50, diffusion = 0.55, width = 1.0, spread = 0.7, early = 0.6, late = 0.8,
           hfDamp = 0.45, lfDamp = 0.15, modDepth = 0.25, modRate = 0.45, movement = 0.35, mix = 0.35, level = 0;
};

class Core {
public:
    static constexpr int MAXK = 16, SAT = 3, BLK = 32;
    static constexpr double WETCAL = 1.25;
    explicit Core(double sr) : sr_(sr) {
        int n = 1; const int need = (int)std::ceil(5.0 * sr) + 8; while (n < need) n <<= 1;
        N_ = n; mask_ = n - 1; for (auto& b : buf_) b.assign((size_t)n, 0.f);
        for (int c = 0; c < 2; c++) {
            apLen_[c][0] = (int)std::lround((c ? 0.0089 : 0.0073) * sr); apLen_[c][1] = (int)std::lround((c ? 0.0137 : 0.0119) * sr);
            for (int a = 0; a < 2; a++) apBuf_[c][a].assign((size_t)apLen_[c][a] + 1, 0.f);
        }
        reset(); T = Params(); S = Params(); Ts_ = T.time;
    }
    void reset() {
        for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.f);
        for (int c = 0; c < 2; c++) { lpz_[c] = hpz_[c] = outHp_[c] = 0; for (int a = 0; a < 2; a++) { std::fill(apBuf_[c][a].begin(), apBuf_[c][a].end(), 0.f); apIdx_[c][a] = 0; } tz_[c].fill(0); }
        first_ = true;
    }
    void setParams(const Params& p) { T = p; }
    void snap() { S = T; Ts_ = T.time; first_ = true; }
    // Process n samples. inR may be null (mono). Never allocates.
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) {
        const double sr = sr_; int pos = 0;
        while (pos < n) {
            const int nb = std::min(BLK, n - pos); const double dt = nb / sr;
            const double kS = 1 - std::exp(-dt / 0.03), kT = 1 - std::exp(-dt / 0.09);
            auto sm = [&](double& s, double t) { s += (t - s) * kS; };
            sm(S.time, T.time); sm(S.density, T.density); sm(S.feedback, T.feedback); sm(S.decay, T.decay); sm(S.diffusion, T.diffusion); sm(S.width, T.width); sm(S.spread, T.spread);
            sm(S.early, T.early); sm(S.late, T.late); sm(S.hfDamp, T.hfDamp); sm(S.lfDamp, T.lfDamp); sm(S.modDepth, T.modDepth); sm(S.modRate, T.modRate); sm(S.movement, T.movement);
            sm(S.mix, T.mix); sm(S.level, T.level);
            Ts_ += (T.time - Ts_) * kT;
            const int envIdx = std::max(0, std::min(8, T.env)); const Env& env = kEnvs[envIdx]; const int Kc = std::max(1, std::min(MAXK, T.count));
            if (patKey_ != envIdx * 100 + Kc) buildPattern(envIdx, Kc);
            const double Ts = std::max(0.02, Ts_), RT = rt(S.decay, env);
            const double md = std::min(1.0, S.modDepth + env.modB * 0.3), mv = S.movement, spread = S.spread;
            const double dens = S.density * SAT, keff = Kc * (1 + 0.35 * S.density), gbase = 0.85 / std::sqrt(keff);
            const double fcStart = 16000 * (1 - 0.35 * S.hfDamp) * env.tone, kabs = (0.5 + 3.5 * S.hfDamp) * env.hfAbs;
            const bool first = first_;
            for (int c = 0; c < 2; c++) {
                const double Tc = Ts * (c ? 1 + 0.037 * (0.5 + spread) : 1);
                for (int j = 0; j < MAXK; j++) {
                    const bool live = j < Kc;
                    tPh_[c][j] += 6.2832 * tPhInc_[c][j] * dt; tPhM_[c][j] += 6.2832 * S.modRate * tPhMK_[c][j] * dt; tPhD_[c][j] += 6.2832 * 0.07 * dt;
                    if (tPh_[c][j] > 6.2832) tPh_[c][j] -= 6.2832; if (tPhM_[c][j] > 6.2832) tPhM_[c][j] -= 6.2832; if (tPhD_[c][j] > 6.2832) tPhD_[c][j] -= 6.2832;
                    const bool early = tMode_[c][j] == 1;
                    const double baseSec = early ? tBase_[c][j] * (0.85 + 0.3 * std::min(1.0, Tc)) : tBase_[c][j] * Tc;
                    const double dsec = std::max(0.0015, baseSec + mv * 0.0012 * std::sin(tPhD_[c][j]) + md * 0.0030 * std::sin(tPhM_[c][j]));
                    const double dsamp = dsec * sr;
                    const double lvl = (early ? S.early : S.late) * gbase * std::pow(10.0, -3 * dsec / RT) * (live ? 1 : 0);
                    const double posv = std::max(-1.0, std::min(1.0, tPan_[c][j] * spread * 1.5 + 0.30 * mv * std::sin(tPh_[c][j])));
                    const double ang = (posv + 1) * 0.7853982;
                    const double fc = std::max(350.0, std::min(19000.0, fcStart * std::exp(-kabs * dsec) * (1 + 0.12 * mv * std::sin(tPh_[c][j] * 1.7))));
                    const double a = 1 - std::exp(-6.2832 * fc / sr);
                    if (first) { tdCur_[c][j] = dsamp; tgCur_[c][j] = lvl; tlCur_[c][j] = std::cos(ang); trCur_[c][j] = std::sin(ang); }
                    tdPrev_[c][j] = tdCur_[c][j]; tgPrev_[c][j] = tgCur_[c][j]; tlPrev_[c][j] = tlCur_[c][j]; trPrev_[c][j] = trCur_[c][j];
                    tdCur_[c][j] += (dsamp - tdCur_[c][j]) * 0.5; tgCur_[c][j] += (lvl - tgCur_[c][j]) * 0.35;
                    tlCur_[c][j] += (std::cos(ang) - tlCur_[c][j]) * 0.35; trCur_[c][j] += (std::sin(ang) - trCur_[c][j]) * 0.35; ta_[c][j] = a;
                }
                const double ld = std::max(8.0, (Tc + md * 0.0007 * std::sin(lPh_[c])) * sr); lPh_[c] += 6.2832 * S.modRate * 0.37 * dt;
                if (lPh_[c] > 6.2832) lPh_[c] -= 6.2832;
                if (first) ldCur_[c] = ld; ldPrev_[c] = ldCur_[c]; ldCur_[c] += (ld - ldCur_[c]) * 0.5;
            }
            first_ = false;
            const double loopG = std::min(0.95, S.feedback) * std::pow(10.0, -0.8 * Ts / RT); lGp_ = first ? loopG : lG_; lG_ = loopG;
            const double loopFc = std::max(400.0, 9000 * (1 - 0.75 * S.hfDamp) * env.loop), laLP = 1 - std::exp(-6.2832 * loopFc / sr);
            const double hpFc = 25 + 450 * S.lfDamp * (0.4 + env.lfAbs * 2), laHP = 1 - std::exp(-6.2832 * hpFc / sr);
            const double apg = 0.15 + 0.5 * S.diffusion * env.diff;
            const double wetHp = 20 + 700 * std::pow(S.lfDamp, 1.5), laW = 1 - std::exp(-6.2832 * wetHp / sr);
            const double mixA = S.mix * 1.5707963, dryG = std::cos(mixA), wetG = std::sin(mixA) * WETCAL, lvlG = std::pow(10.0, S.level / 20), wid = S.width;
            const double sw0 = std::pow(env.satG, 1), sw1 = std::pow(env.satG, 2), sw2 = std::pow(env.satG, 3); const double NN = (double)N_;
            const double swA0 = std::min(1.0, std::max(0.0, dens)) * sw0, swA1 = std::min(1.0, std::max(0.0, dens - 1)) * sw1, swA2 = std::min(1.0, std::max(0.0, dens - 2)) * sw2;
            for (int i = 0; i < nb; i++) {
                const double f = (i + 1.0) / nb; const int idx = pos + i;
                const double l = inL[idx], r = inR ? inR[idx] : l, x = 0.5 * (l + r);
                const int w = w_; double fbv[2] = { 0, 0 };
                for (int c = 0; c < 2; c++) {
                    const double d = ldPrev_[c] + (ldCur_[c] - ldPrev_[c]) * f; const float* bp = buf_[c].data(); const double p = w - d + NN; const int ip = (int)p; const double fr = p - ip;
                    double v = bp[ip & mask_] + (bp[(ip + 1) & mask_] - bp[ip & mask_]) * fr;
                    lpz_[c] += laLP * (v - lpz_[c]); v = lpz_[c];
                    hpz_[c] += laHP * (v - hpz_[c]); v -= hpz_[c];
                    for (int a = 0; a < 2; a++) { float* ab = apBuf_[c][a].data(); const int ai = apIdx_[c][a]; const double b = ab[ai]; const double vv2 = v - apg * b; ab[ai] = (float)vv2; v = b + apg * vv2; apIdx_[c][a] = ai + 1 >= apLen_[c][a] ? 0 : ai + 1; }
                    const double lg = lGp_ + (lG_ - lGp_) * f; v *= lg; fbv[c] = v;
                }
                const double cross = 0.22 * S.width;
                const double in0 = fbv[0] * (1 - cross) + fbv[1] * cross, in1 = fbv[1] * (1 - cross) + fbv[0] * cross;
                buf_[0][w] = (float)(x + lim(in0)); buf_[1][w] = (float)(x + lim(in1));
                double oL = 0, oR = 0;
                for (int c = 0; c < 2; c++) {
                    const float* bp = buf_[c].data();
                    for (int j = 0; j < kAct_; j++) {
                        const double g0 = tgPrev_[c][j], g1 = tgCur_[c][j]; if (g0 < 1e-6 && g1 < 1e-6) continue;
                        const double d = tdPrev_[c][j] + (tdCur_[c][j] - tdPrev_[c][j]) * f; double p = w - d + NN; int ip = (int)p; double fr = p - ip;
                        double v = bp[ip & mask_] + (bp[(ip + 1) & mask_] - bp[ip & mask_]) * fr;
                        if (swA0 > 0) {
                            const double dq = d + tSat_[c][j * SAT]; p = w - dq + NN; ip = (int)p; fr = p - ip; v += swA0 * (bp[ip & mask_] + (bp[(ip + 1) & mask_] - bp[ip & mask_]) * fr);
                            if (swA1 > 0) { p = w - d - tSat_[c][j * SAT + 1] + NN; ip = (int)p; fr = p - ip; v += swA1 * (bp[ip & mask_] + (bp[(ip + 1) & mask_] - bp[ip & mask_]) * fr);
                                if (swA2 > 0) { p = w - d - tSat_[c][j * SAT + 2] + NN; ip = (int)p; fr = p - ip; v += swA2 * (bp[ip & mask_] + (bp[(ip + 1) & mask_] - bp[ip & mask_]) * fr); } }
                        }
                        tz_[c][j] += ta_[c][j] * (v - tz_[c][j]);
                        const double y = tz_[c][j] * (g0 + (g1 - g0) * f);
                        oL += y * (tlPrev_[c][j] + (tlCur_[c][j] - tlPrev_[c][j]) * f); oR += y * (trPrev_[c][j] + (trCur_[c][j] - trPrev_[c][j]) * f);
                    }
                }
                w_ = (w + 1) & mask_;
                outHp_[0] += laW * (oL - outHp_[0]); outHp_[1] += laW * (oR - outHp_[1]); oL -= outHp_[0]; oR -= outHp_[1];
                const double m = 0.5 * (oL + oR), sd = 0.5 * (oL - oR) * wid; oL = lim(m + sd); oR = lim(m - sd);
                double yl = (l * dryG + oL * wetG) * lvlG, yr = (r * dryG + oR * wetG) * lvlG;
                if (!(yl == yl) || !(yr == yr) || yl > 1e6 || yl < -1e6 || yr > 1e6 || yr < -1e6) { reset(); yl = yr = 0; }
                outL[idx] = (float)yl; outR[idx] = (float)yr;
            }
            pos += nb;
        }
    }
    static double rt(double decay, const Env& env) { return 0.3 * std::pow(40.0, std::max(0.0, std::min(1.0, decay))) * env.decay; }
    static double lim(double x) { const double s = 0.5 * x; if (s > 3) return 2; if (s < -3) return -2; return 2 * s * (27 + s * s) / (27 + 9 * s * s); }
    Params T, S;
private:
    static uint32_t nextRnd(uint32_t s) { return s * 1664525u + 1013904223u; }
    void buildPattern(int envIdx, int K) {
        const Env& env = kEnvs[envIdx]; const double sr = sr_;
        for (int c = 0; c < 2; c++) {
            uint32_t s = (uint32_t)(envIdx * 7919u + (uint32_t)K * 104729u + (uint32_t)c * 15485863u + 1u);
            auto R = [&]() { s = nextRnd(s); return s / 4294967296.0; };
            const int nE = K == 1 ? 0 : std::max(1, std::min(K - 1, (int)std::lround(K * env.early))), nL = K - nE;
            for (int j = 0; j < K; j++) {
                int mode; double base;
                if (j < nE) { mode = 1; const double u = (j + 0.5) / nE; base = env.size * (0.006 + 0.070 * std::pow(u, 1.25)) * (1 + env.irr * 0.5 * (R() * 2 - 1)) * (c ? 1.07 : 1); }
                else { mode = 0; const double u = (double)(j - nE + 1) / nL; base = (0.10 + 0.90 * std::pow(u, env.pw)) * (1 + env.irr * 0.12 * (R() * 2 - 1) * (u < 1 ? 1 : 0)); if (j == K - 1) base = 1; }
                tMode_[c][j] = mode; tBase_[c][j] = base;
                const double side = ((j + c) & 1) ? 1 : -1; tPan_[c][j] = side * (0.45 + 0.55 * R());
                tPhInc_[c][j] = 0.03 + 0.22 * R(); tPhMK_[c][j] = 0.7 + 0.6 * R(); tPh_[c][j] = R() * 6.2832; tPhM_[c][j] = R() * 6.2832; tPhD_[c][j] = R() * 6.2832;
                for (int q = 0; q < SAT; q++) tSat_[c][j * SAT + q] = env.sat[q] * 0.001 * (0.6 + 0.8 * R()) * sr;
            }
        }
        kAct_ = K; patKey_ = envIdx * 100 + K;
    }
    double sr_; int N_ = 0, mask_ = 0, w_ = 0, kAct_ = 0, patKey_ = -1; bool first_ = true;
    std::vector<float> buf_[2];
    std::array<double, MAXK> tBase_[2], tPan_[2], tPh_[2], tPhInc_[2], tPhM_[2], tPhMK_[2], tPhD_[2], tdPrev_[2], tdCur_[2], tgPrev_[2], tgCur_[2], tlPrev_[2], tlCur_[2], trPrev_[2], trCur_[2], tz_[2], ta_[2];
    std::array<int, MAXK> tMode_[2]; std::array<double, MAXK * SAT> tSat_[2];
    double lpz_[2] = {0, 0}, hpz_[2] = {0, 0}, outHp_[2] = {0, 0}, ldPrev_[2] = {0, 0}, ldCur_[2] = {0, 0}, lPh_[2] = {0, 0}, lG_ = 0, lGp_ = 0, Ts_ = 0.42;
    int apLen_[2][2] = {{0, 0}, {0, 0}}, apIdx_[2][2] = {{0, 0}, {0, 0}}; std::vector<float> apBuf_[2][2];
};

} // namespace echo
} // namespace vv
