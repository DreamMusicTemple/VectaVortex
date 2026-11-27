// Port of ProceduralProcessor: PLUCK (Karplus-Strong), MODAL, BREATH, CHAOS, BOW (+ Stiffness dispersion, Strike material, Bore, coupling input). No allocation in process().
// NOTE: the web version refreshes the modal coefficients every 4th 128-sample block; process() therefore walks its input in 128-sample
// chunks so behaviour is identical for any host buffer size.
#pragma once
#include "dsp_common.hpp"
#include "fx_basic.hpp"

namespace vv {

class Procedural {
public:
    struct Params { double frequency = 220; int model = 0; double bright = 0.5, damp = 0.5, body = 0.3, chaos = 0.4, stiffness = 0, excite = 0, bore = 0; };
    void prepare(double sr, uint32_t seed = 12345) {
        sr_ = sr; seed0_ = seed ? seed : 12345; N_ = (int)std::ceil(sr / 12) + 8; buf_.assign(N_, 0.f); reset();
    }
    // restart with a new seed without allocating (voice reuse)
    void start(uint32_t seed) { seed0_ = seed ? seed : 12345; reset(); }
    void reset() {
        seed_ = seed0_; std::fill(buf_.begin(), buf_.end(), 0.f); w_ = 0; lp_ = 0; fired_ = false;
        for (int i = 0; i < 6; i++) mx_[i] = my_[i] = mc_[i] = ms_[i] = mr_[i] = mamp_[i] = 0;
        mfired_ = false; l1_ = b1_ = l2_ = b2_ = l3_ = b3_ = 0;
        phase_ = 0; z_ = 0.37; zt_ = 0.37; zcount_ = 0; zsm_ = 0.37; lastModel_ = -1; blockCount_ = 0;
        clickN_ = 0; clickDecay_ = 1; clickG_ = 0; for (int k = 0; k < 4; k++) apX_[k] = apY_[k] = 0; bowVelSm_ = 0;
    }
    // freqArr: optional per-sample frequency (a-rate), may be nullptr
    // inArr: optional per-sample input (the web's coupling input; nullptr = nothing connected)
    void process(float* out, int n, const Params& p, const float* freqArr = nullptr, const float* inArr = nullptr) {
        for (int off = 0; off < n; off += 128) block(out + off, std::min(128, n - off), p, freqArr ? freqArr + off : nullptr, inArr ? inArr + off : nullptr);
    }
private:
    inline double rnd() { seed_ = seed_ * 1664525u + 1013904223u; return (double)seed_ / 2147483648.0 - 1.0; }
    // 4-stage cascaded first-order allpass (stiffness / inharmonic dispersion); state is float like the web's Float32Array
    inline double disperse(double x, double c) {
        for (int k = 0; k < 4; k++) { const double xp = apX_[k], yp = apY_[k]; const double y = c * (x - yp) + xp; apX_[k] = (float)x; apY_[k] = (float)y; x = (double)(float)y; }
        return x;
    }
    void block(float* out, int n, const Params& p, const float* fArr, const float* inp) {
        const double sr = sr_;
        const double f0 = (fArr ? fArr[0] : p.frequency) > 0.01 ? (fArr ? fArr[0] : p.frequency) : 0.01;
        const int model = (int)jsRound(p.model);
        const double bright = p.bright, damp = p.damp, body = p.body, chaos = p.chaos, stiffness = p.stiffness, bore = p.bore;
        const double apc = -0.7 * stiffness;
        if (model != lastModel_) { lastModel_ = model; fired_ = false; mfired_ = false; }
        if (model == 0) {
            const double maxD = N_ - 4;
            double L = sr / f0 - 0.5; if (L > maxD) L = maxD; if (L < 2) L = 2;
            if (!fired_) {
                fired_ = true;
                const int Li = (int)std::floor(L); const double a = 0.25 + 0.7 * bright; double lp = 0;
                for (int i = 0; i < N_; i++) buf_[i] = 0;
                for (int i = 0; i < Li; i++) { lp += a * (rnd() - lp); buf_[i] = (float)(lp * (1.4 - 0.4 * bright)); }
                const int pk = (int)std::max(1.0, std::floor(Li * (0.08 + 0.42 * body)));
                for (int i = Li - 1; i >= pk; i--) buf_[i] = (float)(buf_[i] - buf_[i - pk] * 0.9);
                w_ = Li % N_; lp_ = 0; for (int k = 0; k < 4; k++) apX_[k] = apY_[k] = 0;
            }
            const double g = 0.9 + 0.0999 * std::pow(1 - damp, 0.6);
            const double a = 0.30 + 0.69 * bright;
            for (int i = 0; i < n; i++) {
                const double f = fArr ? (fArr[i] > 0.01 ? fArr[i] : 0.01) : f0;
                double D = sr / f - 0.5; if (D > maxD) D = maxD; if (D < 2) D = 2;
                double rp = w_ - D; while (rp < 0) rp += N_;
                const double fl = std::floor(rp); const int i0 = (int)fl; const double fr = rp - fl; const int i1 = i0 + 1 >= N_ ? 0 : i0 + 1;
                double y = buf_[i0] * (1 - fr) + buf_[i1] * fr;
                if (apc != 0) y = disperse(y, apc);
                lp_ += a * (y - lp_);
                double v = lp_ * g + (inp ? inp[i] * 0.6 : 0); if (!(v < 4 && v > -4)) v = 0;
                buf_[w_] = (float)v; w_++; if (w_ >= N_) w_ = 0;
                out[i] = (float)y;
            }
        } else if (model == 1) {
            static const double T0[6] = {1, 2.756, 5.404, 8.933, 13.34, 18.64}, T1[6] = {1, 2.0, 2.4, 3.0, 4.5, 5.6}, T2[6] = {1, 2, 3, 4, 5, 6}, T3[6] = {1, 1.59, 2.14, 2.30, 2.65, 2.92};
            static const struct { double rolloffMul, clickAmt, clickDecay; } PRO_EXCITE[4] = { {1.6, 0.08, 500}, {1.0, 0.35, 150}, {0.55, 0.8, 60}, {1.3, 0.15, 220} };
            const auto& EXC = PRO_EXCITE[std::max(0, std::min(3, (int)jsRound(p.excite)))];
            const double seg = std::min(2.999, body * 3); const int si = (int)std::floor(seg); const double sf = seg - si;
            const double* A = si == 0 ? T0 : si == 1 ? T1 : T2; const double* B = si == 0 ? T1 : si == 1 ? T2 : T3;
            if (blockCount_ % 4 == 0 || !mfired_) {
                const double tau0 = 0.05 + 6 * (1 - damp) * (1 - damp), spread = 0.5 + damp * 2.5, rolloff = (0.4 + (1 - bright) * 1.8) * EXC.rolloffMul;
                for (int m = 0; m < 6; m++) {
                    const double ratio = A[m] * (1 - sf) + B[m] * sf; double fm = f0 * ratio; if (fm > sr * 0.45) fm = sr * 0.45;
                    const double w = 2 * 3.141592653589793 * fm / sr; mc_[m] = (float)std::cos(w); ms_[m] = (float)std::sin(w);
                    const double tau = tau0 / (1 + m * spread * 0.35);
                    mr_[m] = (float)std::exp(-1 / (sr * std::max(tau, 0.003)));
                    mamp_[m] = (float)(1 / std::pow(m + 1, rolloff));
                }
            }
            if (!mfired_) { mfired_ = true; for (int m = 0; m < 6; m++) { mx_[m] = (float)(mamp_[m] * 0.35); my_[m] = 0; } clickN_ = (int)EXC.clickDecay; clickDecay_ = EXC.clickDecay; clickG_ = EXC.clickAmt; }
            for (int i = 0; i < n; i++) {
                double s = 0;
                for (int m = 0; m < 6; m++) {
                    const double r = mr_[m], c = mc_[m], sn = ms_[m], x = mx_[m], y = my_[m];
                    const double nx = r * (c * x - sn * y), ny = r * (sn * x + c * y);
                    mx_[m] = (float)nx; my_[m] = (float)ny; s += ny;
                }
                if (clickN_ > 0) { s += rnd() * clickG_ * (clickN_ / clickDecay_); clickN_--; }
                s += inp ? inp[i] * 0.8 : 0;
                if (!(s < 8 && s > -8)) { s = 0; for (int m = 0; m < 6; m++) { mx_[m] = 0; my_[m] = 0; } }
                out[i] = (float)(s * 0.9);
            }
        } else if (model == 2) {
            const double fc = std::min(f0, sr * 0.2);
            const double q = 0.015 + 0.5 * damp;
            const double breath = 0.05 + 0.9 * bright, h2 = body, h3 = body * body;
            const double k1 = 2 * std::sin(3.141592653589793 * fc / sr);
            const double reed = chaos, ratio2 = 3 - bore, ratio3 = 5 - bore * 2;
            const double f2 = std::min(fc * ratio2, sr * 0.2), f3 = std::min(fc * ratio3, sr * 0.2);
            const double k2 = 2 * std::sin(3.141592653589793 * f2 / sr), k3 = 2 * std::sin(3.141592653589793 * f3 / sr);
            double l1 = l1_, b1 = b1_, l2 = l2_, b2 = b2_, l3 = l3_, b3 = b3_;
            for (int i = 0; i < n; i++) {
                const double reedMod = 1 + reed * 2.5 * std::tanh(b1 * 0.6);
                const double x = (rnd() + (inp ? inp[i] * 1.2 : 0)) * std::max(0.2, reedMod);
                l1 += k1 * b1; b1 += k1 * (x - l1 - q * b1);
                l2 += k2 * b2; b2 += k2 * (x - l2 - q * b2);
                l3 += k3 * b3; b3 += k3 * (x - l3 - q * b3);
                double v = b1 + h2 * b2 * 0.7 + h3 * b3 * 0.5 + breath * 0.12 * x;
                if (!(v < 40 && v > -40)) { l1 = b1 = l2 = b2 = l3 = b3 = 0; v = 0; }
                out[i] = (float)(v * (q < 0.05 ? 0.8 : 1.4));
            }
            l1_ = l1; b1_ = b1; l2_ = l2; b2_ = b2; l3_ = l3; b3_ = b3;
        } else if (model == 3) {
            const double rate = f0 * (1 + body * 7);
            const double r = 3.5 + 0.495 * chaos; const double depth = chaos * 3.141592653589793 * 1.4;
            const double slew = 1 - std::exp(-1 / (sr * (0.0002 + damp * 0.02)));
            const double inc = f0 / sr; const int step = (int)std::max(1.0, jsRound(sr / std::max(rate, 1.0)));
            for (int i = 0; i < n; i++) {
                if (zcount_ <= 0) { zt_ = r * zt_ * (1 - zt_); if (!(zt_ > 0.0001 && zt_ < 0.9999)) zt_ = 0.37; zcount_ = step; }
                zcount_--;
                zsm_ += slew * (zt_ - zsm_);
                phase_ += inc; if (phase_ >= 1) phase_ -= 1;
                const double ph = 2 * 3.141592653589793 * phase_ + depth * (zsm_ - 0.5) * 2 + (inp ? inp[i] * 1.5 : 0);
                double v = std::sin(ph); if (bright > 0.5) v += (bright - 0.5) * 0.9 * std::sin(ph * 2.01);
                out[i] = (float)(v * 0.4);
            }
        } else {   // BOW: continuously excited single delay line with a stick-slip friction curve
            const double maxD = N_ - 4;
            if (!fired_) { fired_ = true; for (int i = 0; i < N_; i++) buf_[i] = 0; w_ = 0; lp_ = 0; bowVelSm_ = 0; for (int k = 0; k < 4; k++) apX_[k] = apY_[k] = 0; }
            const double pressure = 0.15 + 0.85 * body, bowVelTarget = 0.08 + 0.5 * chaos;
            const double g = 0.985 + 0.0149 * std::pow(1 - damp, 0.6), lpA = 0.3 + 0.69 * bright, a = 100;
            for (int i = 0; i < n; i++) {
                const double f = fArr ? (fArr[i] > 0.01 ? fArr[i] : 0.01) : f0;
                double D = sr / f - 0.5; if (D > maxD) D = maxD; if (D < 2) D = 2;
                double rp = w_ - D; while (rp < 0) rp += N_;
                const double fl = std::floor(rp); const int i0 = (int)fl; const double fr = rp - fl; const int i1 = i0 + 1 >= N_ ? 0 : i0 + 1;
                double y = buf_[i0] * (1 - fr) + buf_[i1] * fr;
                if (apc != 0) y = disperse(y, apc);
                bowVelSm_ += 0.002 * (bowVelTarget - bowVelSm_);
                const double vrel = bowVelSm_ - y;
                double friction = std::sqrt(2 * a) * vrel * std::exp(-a * vrel * vrel + 0.5) * pressure * 0.9;
                if (!(friction < 6 && friction > -6)) friction = 0;
                lp_ += lpA * ((y * g + friction + (inp ? inp[i] * 0.5 : 0)) - lp_);
                double v = lp_; if (!(v < 4 && v > -4)) v = 0;
                buf_[w_] = (float)v; w_++; if (w_ >= N_) w_ = 0;
                out[i] = (float)(y * 0.8);
            }
        }
        blockCount_++;
    }
    double sr_ = 48000, lp_ = 0, l1_ = 0, b1_ = 0, l2_ = 0, b2_ = 0, l3_ = 0, b3_ = 0, phase_ = 0, z_ = 0.37, zt_ = 0.37, zsm_ = 0.37;
    uint32_t seed_ = 12345, seed0_ = 12345; int N_ = 1, w_ = 0, zcount_ = 0, lastModel_ = -1; int64_t blockCount_ = 0;
    bool fired_ = false, mfired_ = false; int clickN_ = 0; double clickDecay_ = 1, clickG_ = 0, bowVelSm_ = 0; float apX_[4] = {}, apY_[4] = {};
    std::vector<float> buf_; float mx_[6] = {}, my_[6] = {}, mc_[6] = {}, ms_[6] = {}, mr_[6] = {}, mamp_[6] = {};
};

} // namespace vv
