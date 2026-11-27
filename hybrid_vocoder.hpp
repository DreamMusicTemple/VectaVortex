// Hybrid Vocoder & Vocal Resynthesis Engine - C++ port of the web app's hybrid vocoder (rebuildVocoder + 'hybrid-vocoder-processor').
// Signal flow: modulator (layer tap or external input) -> gain -> 2x high-pass (80-150 Hz) -> gate -> compressor/limiter
//              carrier (internal saw+pulse synth, sampler slots, a layer tap, or external input) -> drive (wavefolder/tube)
//              engine: 0 classic channel vocoder (8/12/16/24 log or linear band-pass pairs + rectifier/LP envelope followers)
//                      1 formant/talkbox (LPC vocal-tract filter with an independent formant scale)
//                      2 pitch corrector (YIN + scale/held-note quantiser + dual-tap pitch shifter)
//                      3 spectral resynthesis (STFT, modulator envelope x carrier spectrum, carrier phase kept)
//              -> optional BBD-style 3-tap ensemble chorus -> wet; plus dry voice and sibilance/unvoiced noise injection.
// No allocation on the audio thread: every buffer is sized in prepare().
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include "vocoder.hpp"
#include "compressor.hpp"

namespace vv {

inline const std::vector<std::pair<std::string, std::vector<int>>>& hvScaleTable() {
    static const std::vector<std::pair<std::string, std::vector<int>>> t = { {"major",{0,2,4,5,7,9,11}}, {"minor",{0,2,3,5,7,8,10}}, {"harmonicMinor",{0,2,3,5,7,8,11}}, {"dorian",{0,2,3,5,7,9,10}}, {"mixolydian",{0,2,4,5,7,9,10}}, {"majorPent",{0,2,4,7,9}}, {"minorPent",{0,3,5,7,10}}, {"blues",{0,3,5,6,7,10}}, {"japanese",{0,2,3,7,8}}, {"spanish",{0,1,4,5,7,8,10}}, {"egyptian",{0,2,5,7,10}}, {"indonesian",{0,1,3,7,8}}, {"filipino",{0,2,3,7,9}}, {"jazz",{0,2,4,5,7,9,10,11}}, {"fSharpMajor",{1,3,5,6,8,10,11}}, {"kumoi",{0,2,3,7,9}}, {"inSen",{0,1,5,7,10}}, {"chineseGong",{0,4,6,7,11}}, {"chineseShang",{0,2,5,7,10}}, {"majorBlues",{0,2,3,4,7,9}}, {"altered",{0,1,3,4,6,8,10}} };
    return t;
}
inline void hvScaleMask(const std::string& name, bool* mask12) {
    for (int i = 0; i < 12; i++) mask12[i] = false;
    if (name == "chromatic") { for (int i = 0; i < 12; i++) mask12[i] = true; return; }
    for (auto& e : hvScaleTable()) if (e.first == name) { for (int d : e.second) mask12[((d % 12) + 12) % 12] = true; return; }
    for (int d : { 0, 2, 4, 5, 7, 9, 11 }) mask12[d] = true;
}

struct HvParams {
    bool on = false; int mode = 0;            // 0 classic, 1 formant, 2 pitch, 3 spectral
    int bands = 8; bool logSpacing = true, poles4 = false; double mix = 0.8;
    int modSrc = 1;                           // 0 external/mic input, 1 WT, 2 FM, 3 SMP, 4 PRO
    int carSrc = 1;                           // 0 internal, 1 SMP slots, 2 WT, 3 FM, 4 PRO, 5 external
    double micGainDb = 0, hpf = 100, gateDb = -80; bool comp = true;
    double sibSens = 0.5, noiseDb = -18, formantSt = 0, releaseMs = 18, q = 4, drive = 0, chorusDepth = 0, chorusRate = 0.8;
    double pitchSpeedMs = 20; bool mask[12]; int root = 0; bool held = false, drone = false;
    bool slots[16];
    HvParams() { for (bool& b : mask) b = true; for (bool& b : slots) b = true; }
};

class HybridVocoder {
public:
    static constexpr int kMaxBands = 24;
    HvParams P;
    void prepare(double sr) {
        sr_ = sr; comp_.prepare(sr);
        lpcWin_.resize(kWN); for (int i = 0; i < kWN; i++) lpcWin_[i] = (float)(0.5 - 0.5 * std::cos(2 * M_PI * i / (kWN - 1)));
        mbuf_.assign(kWN, 0.f); ybuf_.assign(kYN, 0.f); dline_.assign(kDL, 0.f); yd_.assign(512, 0.f);
        cosT_.resize(kN / 2); sinT_.resize(kN / 2); for (int i = 0; i < kN / 2; i++) { cosT_[i] = std::cos(2 * M_PI * i / kN); sinT_[i] = std::sin(2 * M_PI * i / kN); }
        sw_.resize(kN); for (int i = 0; i < kN; i++) sw_[i] = 0.5 - 0.5 * std::cos(2 * M_PI * i / kN);
        mIn_.assign(kN, 0.f); cIn_.assign(kN, 0.f); oacc_.assign(kN * 2, 0.0);
        mr_.assign(kN, 0); mi_.assign(kN, 0); cr_.assign(kN, 0); ci_.assign(kN, 0);
        menv_.assign(kN / 2 + 1, 0); menvS_.assign(kN / 2 + 1, 0); ysm_.assign(kN / 2 + 1, 0); shifted_.assign(kN / 2 + 1, 0); prevEnv_.assign(kN / 2 + 1, 0); cenv_.assign(kN / 2 + 1, 0);
        for (int k = 0; k < 256; k++) curve_[k] = (float)std::fabs((k / 255.0) * 2 - 1);
        for (int i = 0; i < 3; i++) ens_[i].assign(2048, 0.f);
        apply();
    }
    // (re)configures from P. Resets filter state like a freshly built graph.
    void apply() {
        if (sr_ <= 0) return;
        nB_ = std::max(2, std::min(kMaxBands, P.bands)); const double ratio = std::pow(2.0, P.formantSt / 12.0);
        for (int i = 0; i < nB_; i++) {
            const double t = (double)i / (nB_ - 1), f = P.logSpacing ? 100.0 * std::pow(100.0, t) : 100.0 + t * 9900.0;
            for (int c = 0; c < 2; c++) {
                for (int s = 0; s < 2; s++) { b_[i].mod[c][s].bandpass(f, P.q, sr_); b_[i].car[c][s].bandpass(std::min(18000.0, f * ratio), P.q, sr_); b_[i].mod[c][s].reset(); b_[i].car[c][s].reset(); }
                b_[i].env[c].lowpass(envHz(), 1, sr_); b_[i].env[c].reset();
            }
            b_[i].freq = f;
        }
        hpf_[0].highpass(P.hpf, 0.707, sr_); hpf_[1] = hpf_[0];
        sibHp_.highpass(5000, 0.7, sr_); sibLp_.lowpass(60, 1, sr_); nHp_.highpass(4500, 1, sr_);
        for (int i = 0; i < 3; i++) { enLp_[i].lowpass(6500, 0.6, sr_); }
        modGain_ = std::pow(10.0, P.micGainDb / 20.0); noiseGain_ = std::pow(10.0, P.noiseDb / 20.0);
        sibThr_ = 0.35 - 0.33 * P.sibSens; relCoef_ = 1 - std::exp(-1.0 / (sr_ * std::max(0.002, P.releaseMs / 1000.0)));
        wetGain_ = P.mix * (P.mode == 0 ? 14.0 : 1.4);
        lastMode_ = P.mode;
    }
    void setParams(const HvParams& p) {
        const bool structural = p.mode != P.mode || p.bands != P.bands || p.logSpacing != P.logSpacing || p.poles4 != P.poles4;
        P = p; apply(); if (structural) hardReset();
    }
    void hardReset() {
        std::fill(mbuf_.begin(), mbuf_.end(), 0.f); std::fill(ybuf_.begin(), ybuf_.end(), 0.f); std::fill(dline_.begin(), dline_.end(), 0.f);
        std::fill(oacc_.begin(), oacc_.end(), 0.0); std::fill(mIn_.begin(), mIn_.end(), 0.f); std::fill(cIn_.begin(), cIn_.end(), 0.f); std::fill(prevEnv_.begin(), prevEnv_.end(), 0.0);
        for (int i = 0; i < 6; i++) { sb1_[i] = sb2_[i] = tb1_[i] = tb2_[i] = s1_[i] = s2_[i] = 0; } gain_ = tgain_ = 0; crms_ = 0.01; mpow_ = 0; opow_ = 1e-6; sc_ = 0;
        ratio_ = tratio_ = 1; mpos_ = 0; hopCnt_ = 0; ypos_ = 0; ycnt_ = 0; dw_ = 0; dph_ = 0; sp_ = 0; scnt_ = 0; opos_ = 0; oq_ = 0; hopStep_ = -1;
        for (int i = 0; i < 12; i++) { roots_[i][0] = 0.8 * std::cos(2 * M_PI * (i + 0.5) / 12 * 0.5); roots_[i][1] = 0.8 * std::sin(2 * M_PI * (i + 0.5) / 12 * 0.5); }
    }
    // Internal-carrier note events (the synth forwards its note on/off).
    void noteOn(int note) { for (int i = 0; i < 16; i++) if (iv_[i].on && iv_[i].note == note && !iv_[i].rel) { return; } heldAdd(note);
        int k = -1; for (int i = 0; i < 16; i++) if (!iv_[i].on) { k = i; break; } if (k < 0) k = 0;
        Int& v = iv_[k]; v = Int(); v.on = true; v.note = note; v.f = 440.0 * std::pow(2.0, (note - 69) / 12.0); v.rel = false; }
    void noteOff(int note) { heldRemove(note); for (int i = 0; i < 16; i++) if (iv_[i].on && iv_[i].note == note && !iv_[i].rel) iv_[i].rel = true; }
    void allNotesOff() { nHeld_ = 0; for (auto& v : iv_) if (v.on) v.rel = true; }

    // mod: modulator mono (n samples); carL/carR: carrier (stereo); ext: external input mono (may be null). Adds into outL/outR.
    void process(const float* mod, const float* carL, const float* carR, const float* ext, float* outL, float* outR, int n) {
        if (!P.on || n > kQ) return;
        float m[kQ], c[kQ], cL[kQ], cR[kQ];
        // ---- modulator conditioning
        for (int i = 0; i < n; i++) { double x = mod[i] * modGain_; x = hpf_[0].tick(x); x = hpf_[1].tick(x); m[i] = (float)x; }
        if (P.gateDb > -80) {
            double s = 0; for (int i = 0; i < n; i++) s += (double)m[i] * m[i]; const double db = 10 * std::log10(s / n + 1e-12);
            if (db > P.gateDb) { holdLeft_ = (int)(0.08 * sr_); gateOpen_ = true; } else { holdLeft_ -= n; if (holdLeft_ <= 0) gateOpen_ = false; }
            const double tc = gateOpen_ ? 0.004 : 0.03, k = 1 - std::exp(-1.0 / (sr_ * tc));
            for (int i = 0; i < n; i++) { gateG_ += k * ((gateOpen_ ? 1.0 : 0.0) - gateG_); m[i] = (float)(m[i] * gateG_); }
        } else { gateG_ = 1; gateOpen_ = true; }
        if (P.comp) { Compressor::Params cp; cp.thresholdDb = -22; cp.kneeDb = 12; cp.ratio = 6; cp.attack = 0.003f; cp.release = 0.15f; comp_.process(m, m, mO_, mO_, n, cp); std::memcpy(m, mO_, sizeof(float) * n); }
        // ---- carrier
        if (P.carSrc == 0) {
            for (int i = 0; i < n; i++) {
                double s = 0; bool any = false;
                for (auto& v : iv_) if (v.on) { any = true; s += v.tick(sr_); }
                if (P.drone && nHeld_ == 0) { s += drone_.tick(sr_); any = true; }
                (void)any; cL[i] = cR[i] = (float)s;
            }
        } else if (P.carSrc == 5) { for (int i = 0; i < n; i++) cL[i] = cR[i] = ext ? ext[i] : 0.f; }
        else { std::memcpy(cL, carL, sizeof(float) * n); std::memcpy(cR, carR, sizeof(float) * n); }
        if (P.drive > 0) { const double d = P.drive / 100.0, pre = 1 + d * 2, g = 1 + 5 * d; for (int i = 0; i < n; i++) { cL[i] = (float)drv(cL[i] * pre, g); cR[i] = (float)drv(cR[i] * pre, g); } }
        for (int i = 0; i < n; i++) c[i] = 0.5f * (cL[i] + cR[i]);
        // ---- engine
        float wl[kQ], wr[kQ];
        if (P.mode == 0) {
            for (int i = 0; i < n; i++) {
                double sl = 0, sr = 0;
                for (int b = 0; b < nB_; b++) {
                    Band& B = b_[b]; double mm = m[i]; mm = B.mod[0][0].tick(mm); if (P.poles4) mm = B.mod[0][1].tick(mm);
                    const double g = B.env[0].tick(shape(mm));
                    double a = cL[i], r = cR[i]; a = B.car[0][0].tick(a); r = B.car[1][0].tick(r); if (P.poles4) { a = B.car[0][1].tick(a); r = B.car[1][1].tick(r); }
                    sl += a * g; sr += r * g;
                }
                wl[i] = (float)sl; wr[i] = (float)sr;
            }
        } else {
            float y[kQ];
            if (P.mode == 1) { for (int i = 0; i < n; i++) y[i] = 0; }   // the web removed the LPC 'formant' mode: a preset still naming it reaches the worklet's mode 0, which outputs silence (the LPC code below is kept but no longer reachable)
            else if (P.mode == 2) runPitch(m, y, n); else runStft(m, c, y, n);
            for (int i = 0; i < n; i++) wl[i] = wr[i] = y[i];
        }
        if (P.chorusDepth > 0) ensemble(wl, wr, n);
        const double dryG = 1 - P.mix, unvG = noiseGain_ * std::max(0.2, P.mix);
        for (int i = 0; i < n; i++) {
            double l = wl[i] * wetGain_ + m[i] * dryG, r = wr[i] * wetGain_ + m[i] * dryG;
            // sibilance / unvoiced injection: >5 kHz envelope drives high-passed noise and the high-passed modulator
            const double hpv = sibHp_.tick(m[i]); const double env = sibLp_.tick(std::fabs(hpv)) * 14.0;
            double sh = P.sibSens <= 0 ? 0 : std::max(0.0, std::min(1.0, (env - sibThr_) / 0.25));
            sibEnvSm_ += 0.2 * (sh - sibEnvSm_);
            nz_ ^= nz_ << 13; nz_ ^= nz_ >> 17; nz_ ^= nz_ << 5; const double nd = ((double)nz_ / 4294967295.0) * 2 - 1;
            const double inj = (nHp_.tick(nd) + 0.6 * hpv) * sibEnvSm_ * unvG;
            outL[i] += (float)(l + inj); outR[i] += (float)(r + inj);
        }
    }

private:
    static constexpr int kQ = 128, kWN = 512, kHop = 128, kP = 12, kYN = 1024, kDL = 4096, kN = 1024, kSH = 256;
    struct Band { VocBiquad mod[2][2], car[2][2], env[2]; double freq = 0; };
    struct Int { bool on = false, rel = false; int note = 0; double f = 0, ph1 = 0, ph2 = 0, env = 0, t = 0;
        double tick(double sr) { // saw (-6 c) + square (+6 c), PolyBLEP; 6 ms attack, ~40 ms release
            const double d1 = f * std::pow(2.0, -6 / 1200.0) / sr, d2 = f * std::pow(2.0, 6 / 1200.0) / sr;
            auto blep = [](double t, double dt) { if (t < dt) { t /= dt; return t + t - t * t - 1; } if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; } return 0.0; };
            ph1 += d1; if (ph1 >= 1) ph1 -= 1; ph2 += d2; if (ph2 >= 1) ph2 -= 1;
            double saw = 2 * ph1 - 1 - blep(ph1, d1), sq = (ph2 < 0.5 ? 1.0 : -1.0) + blep(ph2, d2) - blep(std::fmod(ph2 + 0.5, 1.0), d2);
            if (rel) env += (1 - std::exp(-1.0 / (sr * 0.04))) * (0 - env); else env = std::min(0.5, env + 0.5 / (0.006 * sr));
            if (rel && env < 1e-4) on = false;
            return (saw * 0.6 + sq * 0.35) * env; } };
    struct Drone { double ph = 0; double tick(double sr) { ph += 65.406 / sr; if (ph >= 1) ph -= 1; return (2 * ph - 1) * 0.25; } };
    double envHz() const { return std::min(400.0, 1000.0 / (2 * M_PI * (P.releaseMs / 2.2))); }
    static double drv(double x, double g) { const double c = std::max(-1.0, std::min(1.0, x)); return std::sin(M_PI * 0.5 * c * g) * 0.9 + std::tanh(c * g * 0.8) * 0.1; }
    inline double shape(double x) const {
        if (x <= -1) return curve_[0]; if (x >= 1) return curve_[255];
        const double v = (x + 1) * 0.5 * 255.0; const int k = (int)std::floor(v); const double f = v - k;
        return k >= 255 ? curve_[255] : curve_[k] * (1 - f) + curve_[k + 1] * f;
    }
    void heldAdd(int n) { for (int i = 0; i < nHeld_; i++) if (held_[i] == n) return; if (nHeld_ < 16) held_[nHeld_++] = n; }
    void heldRemove(int n) { for (int i = 0; i < nHeld_; i++) if (held_[i] == n) { held_[i] = held_[--nHeld_]; return; } }

    // ---------------- mode 1: LPC ----------------
    void lpcFrame() {
        double x[kWN], r[kP + 1], a[kP + 1], tmp[kP + 1]; const int N = kWN;
        for (int i = 0; i < N; i++) x[i] = mbuf_[(mpos_ + i) % N] * lpcWin_[i];
        for (int k = 0; k <= kP; k++) { double s = 0; for (int i = k; i < N; i++) s += x[i] * x[i - k]; r[k] = s; }
        if (r[0] < 1e-9) { tgain_ = 0; return; }
        r[0] *= 1.0005; for (int k = 1; k <= kP; k++) { const double w = 2 * M_PI * 60 / sr_ * k; r[k] *= std::exp(-0.5 * w * w); }
        std::memset(a, 0, sizeof(a)); std::memset(tmp, 0, sizeof(tmp)); double e = r[0]; a[0] = 1;
        for (int i = 1; i <= kP; i++) { double acc = r[i]; for (int k = 1; k < i; k++) acc += a[k] * r[i - k]; const double kk = -acc / e; for (int k = 1; k < i; k++) tmp[k] = a[k] + kk * a[i - k]; for (int k = 1; k < i; k++) a[k] = tmp[k]; a[i] = kk; e *= (1 - kk * kk); if (e < 1e-12) { e = 1e-12; break; } }
        tgain_ = std::sqrt(e / N) * 3.2;
        for (int it = 0; it < 14; it++) { double maxd = 0;
            for (int i = 0; i < kP; i++) { const double zr = roots_[i][0], zi = roots_[i][1]; double pr = 1, pi = 0;
                for (int k = 1; k <= kP; k++) { const double nr = pr * zr - pi * zi + a[k], ni = pr * zi + pi * zr; pr = nr; pi = ni; }
                double dr = 1, di = 0; for (int j = 0; j < kP; j++) { if (j == i) continue; const double ar = zr - roots_[j][0], ai = zi - roots_[j][1]; const double nr2 = dr * ar - di * ai, ni2 = dr * ai + di * ar; dr = nr2; di = ni2; }
                const double den = dr * dr + di * di + 1e-30, qr = (pr * dr + pi * di) / den, qi = (pi * dr - pr * di) / den; roots_[i][0] -= qr; roots_[i][1] -= qi; maxd = std::max(maxd, std::fabs(qr) + std::fabs(qi)); }
            if (maxd < 1e-6) break; }
        struct Sec { double th, r; }; Sec secs[kP]; int ns = 0; const double f = std::pow(2.0, P.formantSt / 12.0);
        for (int i = 0; i < kP; i++) { const double zr = roots_[i][0], zi = roots_[i][1]; if (zi > 1e-4) { secs[ns].th = std::atan2(zi, zr); secs[ns].r = std::min(0.992, std::sqrt(zr * zr + zi * zi)); ns++; } }
        std::sort(secs, secs + ns, [](const Sec& p, const Sec& q) { return p.th < q.th; });
        for (int i = 0; i < 6; i++) { if (i < ns) { const double th = secs[i].th * f; double rr = secs[i].r; if (th > 2.7) rr = 0; tb1_[i] = -2 * rr * std::cos(th); tb2_[i] = rr * rr; } else { tb1_[i] = 0; tb2_[i] = 0; } }
    }
    void runLpc(const float* m, const float* c, float* y, int n) {
        for (int i = 0; i < n; i++) {
            mbuf_[mpos_] = m[i]; mpos_ = (mpos_ + 1) % kWN;
            if (++hopCnt_ >= kHop) { hopCnt_ = 0; lpcFrame(); for (int q = 0; q < 6; q++) { r1_[q] = (tb1_[q] - sb1_[q]) / kHop; r2_[q] = (tb2_[q] - sb2_[q]) / kHop; } rg_ = (tgain_ - gain_) / kHop; hopStep_ = 0; }
            if (hopStep_ >= 0 && hopStep_ < kHop) { for (int q = 0; q < 6; q++) { sb1_[q] += r1_[q]; sb2_[q] += r2_[q]; } gain_ += rg_; hopStep_++; }
            crms_ += relCoef_ * 0.2 * (std::fabs(c[i]) - crms_); double e = c[i] / std::max(0.02, crms_ * 1.6);
            for (int s = 0; s < 6; s++) { double o = e - sb1_[s] * s1_[s] - sb2_[s] * s2_[s]; if (!std::isfinite(o)) o = 0; s2_[s] = s1_[s]; s1_[s] = o; e = o; }
            mpow_ += 0.0015 * (m[i] * (double)m[i] - mpow_); opow_ += 0.0015 * (e * e - opow_);
            double sc = std::sqrt(mpow_ / (opow_ + 1e-9)); if (sc > 40) sc = 40; sc_ += 0.01 * (sc - sc_);
            y[i] = (float)std::tanh(e * sc_ * 1.2);
        }
    }
    // ---------------- mode 2: pitch corrector ----------------
    double yin() {
        const int N = kYN, half = N >> 1; double* d = yd_.data() ? (double*)nullptr : nullptr; (void)d;
        const int minT = (int)std::floor(sr_ / 700), maxT = std::min(half - 1, (int)std::floor(sr_ / 70));
        float* dd = yd_.data(); double run = 0; dd[0] = 1; int best = -1;
        for (int tau = 1; tau <= maxT; tau++) { double s = 0; for (int i = 0; i < half; i += 2) { const double df = ybuf_[(ypos_ + i) % N] - ybuf_[(ypos_ + i + tau) % N]; s += df * df; } run += s; dd[tau] = run > 0 ? (float)(s * tau / run) : 1.f; }
        for (int tau = minT; tau <= maxT; tau++) if (dd[tau] < 0.15f) { while (tau + 1 <= maxT && dd[tau + 1] < dd[tau]) tau++; best = tau; break; }
        if (best < 0) return 0;
        const double x0 = dd[best - 1], x1 = dd[best], x2 = dd[best + 1]; const double den = x0 + x2 - 2 * x1; const double shift = den != 0 ? (x0 - x2) / (2 * den) : 0; return sr_ / (best + shift);
    }
    double quantize(double f0) {
        const double mm = 69 + 12 * std::log2(f0 / 440); double target = mm, best = 1e9;
        if (P.held && nHeld_ > 0) { for (int k = 0; k < nHeld_; k++) { const double dd = std::fabs(held_[k] - mm); if (dd < best) { best = dd; target = held_[k]; } } }
        else { const int base = (int)std::lround(mm); for (int k = -7; k <= 7; k++) { const int cc = base + k, pc = (((cc - P.root) % 12) + 12) % 12; if (P.mask[pc]) { const double d2 = std::fabs(cc - mm); if (d2 < best) { best = d2; target = cc; } } } }
        return std::pow(2.0, (target - mm) / 12.0);
    }
    void runPitch(const float* m, float* y, int n) {
        const double spd = std::max(0.0005, P.pitchSpeedMs / 1000.0), k = 1 - std::exp(-1.0 / (sr_ * spd)); const double W = 1536;
        for (int i = 0; i < n; i++) {
            ybuf_[ypos_] = m[i]; ypos_ = (ypos_ + 1) % kYN;
            if (++ycnt_ >= 256) { ycnt_ = 0; const double f0 = yin(); if (f0 > 0) tratio_ = std::max(0.5, std::min(2.0, quantize(f0))); else tratio_ = 1; }
            ratio_ += (tratio_ - ratio_) * k;
            dline_[dw_] = m[i]; dph_ += (1 - ratio_); if (dph_ >= W) dph_ -= W; if (dph_ < 0) dph_ += W;
            const double d1 = dph_, d2 = std::fmod(dph_ + W / 2, W), w1 = 0.5 - 0.5 * std::cos(2 * M_PI * d1 / W), w2 = 1 - w1;
            const double rp1 = std::fmod(dw_ - d1 + kDL * 2, (double)kDL), rp2 = std::fmod(dw_ - d2 + kDL * 2, (double)kDL);
            const int i1 = (int)rp1, i2 = (int)rp2; const double f1 = rp1 - i1, f2 = rp2 - i2;
            const double s1 = dline_[i1 % kDL] * (1 - f1) + dline_[(i1 + 1) % kDL] * f1, s2 = dline_[i2 % kDL] * (1 - f2) + dline_[(i2 + 1) % kDL] * f2;
            y[i] = (float)(s1 * w1 + s2 * w2); dw_ = (dw_ + 1) % kDL;
        }
    }
    // ---------------- mode 3: STFT ----------------
    void fft(double* re, double* im, bool inv) {
        const int n = kN; int j = 0;
        for (int i = 1; i < n; i++) { int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); } }
        for (int len = 2; len <= n; len <<= 1) { const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len) for (int k = 0; k < half; k++) { const double wr = cosT_[k * step], wi = (inv ? 1 : -1) * sinT_[k * step]; const int a = i + k, b = a + half;
                const double xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr; re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi; } }
        if (inv) for (int i = 0; i < n; i++) { re[i] /= n; im[i] /= n; }
    }
    void stftFrame() {
        const int N = kN, h = N / 2; double* mr = mr_.data(); double* mi = mi_.data(); double* cr = cr_.data(); double* ci = ci_.data();
        for (int i = 0; i < N; i++) { const int idx = (sp_ + i) % N; mr[i] = mIn_[idx] * sw_[i]; mi[i] = 0; cr[i] = cIn_[idx] * sw_[i]; ci[i] = 0; }
        fft(mr, mi, false); fft(cr, ci, false);
        const int w = std::max(1, (int)std::lround(std::max(1.0, P.q * 1.5))); const double rel = std::exp(-((double)kSH / sr_) / std::max(0.005, P.releaseMs / 1000.0));
        double* mm = menv_.data(); double* cm = cenv_.data();
        for (int k = 0; k <= h; k++) { mm[k] = std::sqrt(mr[k] * mr[k] + mi[k] * mi[k]); cm[k] = std::sqrt(cr[k] * cr[k] + ci[k] * ci[k]); }
        const double f = std::max(0.25, std::pow(2.0, P.formantSt / 12.0));
        for (int k = 0; k <= h; k++) { const int a = std::max(0, k - w), b = std::min(h, k + w); double acc = 0; for (int i = a; i <= b; i++) acc += mm[i]; menvS_[k] = acc / (b - a + 1); }
        for (int k = 0; k <= h; k++) { const int a = std::max(0, k - w), b = std::min(h, k + w); double acc = 0; for (int i = a; i <= b; i++) acc += cm[i]; ysm_[k] = acc / (b - a + 1) + 1e-9; }
        for (int k = 0; k <= h; k++) { const double src = k / f; const int i0 = (int)std::floor(src); const double fr = src - i0; const double v = i0 >= h ? 0 : menvS_[i0] * (1 - fr) + menvS_[std::min(h, i0 + 1)] * fr; const double pe = prevEnv_[k] * rel; prevEnv_[k] = v > pe ? v : pe; shifted_[k] = prevEnv_[k]; }
        for (int k = 0; k <= h; k++) { const double g = shifted_[k] / ysm_[k]; cr[k] *= g; ci[k] *= g; if (k > 0 && k < h) { cr[N - k] = cr[k]; ci[N - k] = -ci[k]; } }
        ci[0] = 0; ci[h] = 0; fft(cr, ci, true);
        for (int i = 0; i < N; i++) oacc_[(opos_ + i) % (N * 2)] += cr[i] * sw_[i] / 1.5;
    }
    void runStft(const float* m, const float* c, float* y, int n) {
        for (int i = 0; i < n; i++) {
            mIn_[sp_] = m[i]; cIn_[sp_] = c[i]; sp_ = (sp_ + 1) % kN;
            y[i] = (float)oacc_[opos_]; oacc_[opos_] = 0; opos_ = (opos_ + 1) % (kN * 2);
            if (++scnt_ >= kSH) { scnt_ = 0; stftFrame(); }
        }
    }
    // ---------------- BBD-style ensemble ----------------
    void ensemble(float* wl, float* wr, int n) {
        const double depth = P.chorusDepth / 100.0, amp = (0.0004 + 0.0035 * depth) * sr_; const double mult[3] = { 1.0, 1.13, 0.91 }, base[3] = { 0.007, 0.011, 0.015 };
        for (int i = 0; i < n; i++) {
            const double in = 0.5 * (wl[i] + wr[i]); for (int t = 0; t < 3; t++) ens_[t][enW_ & 2047] = (float)in;
            double out = in * 0.6;
            for (int t = 0; t < 3; t++) {
                enPh_[t] += P.chorusRate * mult[t] / sr_; if (enPh_[t] >= 1) enPh_[t] -= 1;
                const double d = base[t] * sr_ + amp * std::sin(2 * M_PI * enPh_[t]); const double rp = (double)enW_ - d; const int i0 = (int)std::floor(rp); const double fr = rp - i0;
                const double s = ens_[t][(i0 + 4096) & 2047] * (1 - fr) + ens_[t][(i0 + 1 + 4096) & 2047] * fr;
                out += enLp_[t].tick(s) * 0.42;
            }
            enW_++; wl[i] = wr[i] = (float)out;
        }
    }

    double sr_ = 48000, modGain_ = 1, noiseGain_ = 0.1, sibThr_ = 0.2, relCoef_ = 0.001, wetGain_ = 1, sibEnvSm_ = 0, gateG_ = 1; int lastMode_ = 0, nB_ = 8, holdLeft_ = 0; bool gateOpen_ = true;
    VocBiquad hpf_[2], sibHp_, sibLp_, nHp_, enLp_[3]; Band b_[kMaxBands]; float curve_[256]; Compressor comp_; float mO_[kQ];
    uint32_t nz_ = 2463534242u; Int iv_[16]; Drone drone_; int held_[16]; int nHeld_ = 0;
    // LPC
    std::vector<float> lpcWin_, mbuf_; int mpos_ = 0, hopCnt_ = 0, hopStep_ = -1; double roots_[kP][2]; double sb1_[6], sb2_[6], tb1_[6], tb2_[6], s1_[6], s2_[6], r1_[6], r2_[6], gain_ = 0, tgain_ = 0, rg_ = 0, crms_ = 0.01, mpow_ = 0, opow_ = 1e-6, sc_ = 0;
    // pitch
    std::vector<float> ybuf_, dline_, yd_; int ypos_ = 0, ycnt_ = 0, dw_ = 0; double ratio_ = 1, tratio_ = 1, dph_ = 0;
    // STFT
    std::vector<double> cosT_, sinT_, sw_, oacc_, mr_, mi_, cr_, ci_, menv_, menvS_, ysm_, shifted_, prevEnv_, cenv_; std::vector<float> mIn_, cIn_; int sp_ = 0, scnt_ = 0, opos_ = 0, oq_ = 0;
    // ensemble
    std::vector<float> ens_[3]; int enW_ = 0; double enPh_[3] = { 0, 0.33, 0.66 };
public:
    HybridVocoder() { hardReset(); }
};
}  // namespace vv
