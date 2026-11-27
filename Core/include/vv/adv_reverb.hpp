// Advanced stereo reverb engine: HALL (early reflections + long FDN tail), PLATE AMBIENT, KRISTAL, FLOW (Dattorro-style allpass tank),
// MOTHERSHIP (Hilbert frequency shifter in the feedback loop) and FORMANT (vowel filter before the tank).
// Port of AdvReverbProcessor. Mono in, stereo out. algo 9 = idle (output silence).
#pragma once
#include "dsp_common.hpp"

namespace vv {

class AdvReverb {
public:
    enum Algo { Hall = 0, PlateAmbient = 1, Kristal = 2, Flow = 3, Mothership = 4, Formant = 5, Idle = 9 };
    struct Params { int algo = Idle; double decay = 0.5, size = 0.5, predelay = 0.02, mix = 0.3, mod = 0.4, colour = 0.5; };

    void prepare(double sampleRate) {
        sr_ = sampleRate;
        const double sr = sr_;
        pre_.init((int)std::ceil(0.35 * sr));
        er_.init((int)std::ceil(0.6 * sr));
        static const double dfT[8] = { 4.7, 6.3, 8.9, 11.3, 3.9, 5.3, 7.7, 9.7 };
        for (int i = 0; i < 8; i++) { dfT_[i] = dfT[i] * sr / 1000.0; dfB_[i].init((int)std::ceil(dfT_[i]) + 8); }
        for (int i = 0; i < 8; i++) lines_[i].init((int)std::ceil(0.55 * sr));
        k_ = sr / 29761.0;
        static const int fdT[4] = { 142, 107, 379, 277 };
        for (int i = 0; i < 4; i++) { fdInT_[i] = fdT[i] * k_; fdIn_[i].init((int)std::ceil(fdT[i] * k_) + 8); }
        apL1_.init((int)std::ceil(672 * k_) + 80); apR1_.init((int)std::ceil(908 * k_) + 80);
        apL2_.init((int)std::ceil(1800 * k_) + 8); apR2_.init((int)std::ceil(2656 * k_) + 8);
        d1L_.init((int)std::ceil(4453 * k_ * 2.6)); d2L_.init((int)std::ceil(3720 * k_ * 2.6));
        d1R_.init((int)std::ceil(4217 * k_ * 2.6)); d2R_.init((int)std::ceil(3163 * k_ * 2.6));
        reset();
    }
    void reset() {
        pre_.clear(); er_.clear();
        for (auto& b : dfB_) b.clear();
        for (auto& l : lines_) l.clear();
        for (auto& b : fdIn_) b.clear();
        apL1_.clear(); apR1_.clear(); apL2_.clear(); apR2_.clear(); d1L_.clear(); d2L_.clear(); d1R_.clear(); d2R_.clear();
        std::memset(dly_, 0, sizeof dly_); std::memset(g_, 0, sizeof g_); std::memset(ds_, 0, sizeof ds_);
        std::memset(yv_, 0, sizeof yv_); std::memset(tv_, 0, sizeof tv_); std::memset(mInc_, 0, sizeof mInc_);
        for (int i = 0; i < 8; i++) lph_[i] = (float)std::fmod(i * 0.37, 1.0);
        erLp_ = erLpR_ = 0;
        std::memset(hs_, 0, sizeof hs_); hq_ = 0; shPh_ = 0; fbV_ = 0; dcX_ = 0; dcY_ = 0;
        std::memset(fx_, 0, sizeof fx_); std::memset(fb0_, 0, sizeof fb0_); std::memset(fa1_, 0, sizeof fa1_); std::memset(fa2_, 0, sizeof fa2_);
        lfoPh_ = 0; lpL_ = lpR_ = tankL_ = tankR_ = flBw_ = 0; mph1_ = 0; mph2_ = 0.4;
    }

    void process(const float* in, float* outL, float* outR, int N, const Params& p) {
        const int algo = p.algo;
        if (algo > 5) { for (int n = 0; n < N; n++) { outL[n] = 0; outR[n] = 0; } return; }
        const double sr = sr_, decay = p.decay, size = p.size, mix = p.mix, modA = p.mod, colour = p.colour;
        const double pdSamp = p.predelay * sr;
        const bool flow = algo == Flow, hall = algo == Hall, moth = algo == Mothership, form = algo == Formant;
        double rtRef = 4, rt60 = 3, fcBase = 8000, szScale = 1, depthMs = 0.3, rateScale = 1, wetGain = 1, difG = 0.6;
        int nDif = 8;
        if (hall)            { rt60 = 0.5 * std::exp(decay * 4.2); fcBase = 5200;  nDif = 4; szScale = 0.6 + size * 0.8;  depthMs = 0.6; rateScale = 0.6; wetGain = 1.0;  rtRef = 4.08; difG = 0.55; }
        else if (algo == 1)  { rt60 = 0.8 * std::exp(decay * 4.0); fcBase = 9500;  nDif = 6; szScale = 0.6 + size * 0.8;  depthMs = 4.0; rateScale = 1.6; wetGain = 0.94; rtRef = 5.9;  difG = 0.65; }
        else if (algo == 2)  { rt60 = 0.4 * std::exp(decay * 4.6); fcBase = 12500; nDif = 8; szScale = 0.35 + size * 1.2; depthMs = 0.4; rateScale = 0.8; wetGain = 1.12; rtRef = 4.0;  difG = 0.65; }
        else if (moth)       { rt60 = 0.6 * std::exp(decay * 4.2); fcBase = 8000;  nDif = 6; szScale = 0.45 + size;       depthMs = 0.5; rateScale = 0.7; wetGain = 1.03; rtRef = 4.9;  difG = 0.6; }
        else if (form)       { rt60 = 0.5 * std::exp(decay * 4.2); fcBase = 6500;  nDif = 8; szScale = 0.5 + size * 0.9;  depthMs = 0.5; rateScale = 0.7; wetGain = 2.3;  rtRef = 4.08; difG = 0.6; }
        if (flow) { wetGain = 0.9; const double tg0 = std::min(0.985, 0.25 + decay * 0.73); rt60 = 1.0 / (-std::log(tg0)); rtRef = 1.0 / (-std::log(0.615)); }
        { double nrm = std::sqrt(rtRef / rt60); nrm = nrm < 0.35 ? 0.35 : (nrm > 2.5 ? 2.5 : nrm); wetGain *= nrm; }
        const double fc = clampd(fcBase * (0.3 + colour * 1.4), 900, 18000);
        const double dcoef = 1 - std::exp(-kTwoPi * fc / sr);
        if (!flow) {
            const double* b = baseFor(algo); const double gdB = -3 / (sr * rt60);
            for (int i = 0; i < 8; i++) {
                const double d = b[i] * szScale * sr / 1000.0; dly_[i] = (float)d;
                double gg = std::pow(10.0, gdB * d); if (gg > 0.9997) gg = 0.9997; g_[i] = (float)gg;
                mInc_[i] = (float)((0.23 + 0.061 * i) * rateScale / sr);
            }
        }
        const double mDepth = flow ? 0 : modA * depthMs * sr / 1000.0;
        const double erScale = 0.5 + size;
        const double k = k_, sf = 0.5 + size;
        const double tg = std::min(0.985, 0.25 + decay * 0.73), dd2 = clampd(tg + 0.15, 0.25, 0.5);
        const double flMod = modA * 10 * k;
        const double shiftHz = -200 + colour * 500, shInc = kTwoPi * shiftHz / sr, fbAmt = std::min(0.42, 0.9 * (1 - decay));
        if (form) {
            lfoPh_ += 0.3 * N / sr; if (lfoPh_ >= 1) lfoPh_ -= 1;
            double u = colour + std::sin(kTwoPi * lfoPh_) * modA * 0.35; u = u < 0 ? 0 : (u > 1 ? 1 : u);
            const double pos = u * 4; const int i0 = std::min(3, (int)pos); const double fr = pos - i0;
            for (int j = 0; j < 3; j++) {
                const double f = vow_[i0][j] * (1 - fr) + vow_[i0 + 1][j] * fr;
                const double w0 = kTwoPi * f / sr, alpha = std::sin(w0) / (2 * (9 + j * 2)), a0 = 1 + alpha;
                fb0_[j] = (float)(alpha / a0); fa1_[j] = (float)(-2 * std::cos(w0) / a0); fa2_[j] = (float)((1 - alpha) / a0);
            }
        }
        for (int n = 0; n < N; n++) {
            const double x = in ? in[n] : 0.0;
            pre_.push(x);
            const double xp = pre_.tap(pdSamp < 1 ? 1 : pdSamp);
            double erL = 0, erR = 0, tailIn = xp;
            if (hall) {
                er_.push(xp);
                for (int t = 0; t < 12; t++) {
                    const double gm = 0.8 * std::exp(-erTL_[t] * 0.018 / (0.5 + size * 0.5)), sg = (t & 1) ? -1 : 1;
                    erL += sg * gm * er_.tap(erTL_[t] * erScale * sr / 1000.0);
                    erR += -sg * gm * er_.tap(erTR_[t] * erScale * sr / 1000.0);
                }
                erLp_ += 0.5 * (erL - erLp_); erLpR_ += 0.5 * (erR - erLpR_);
                erL = erLp_ * 0.55; erR = erLpR_ * 0.55;
                tailIn = er_.tap(18 * erScale * sr / 1000.0);
            }
            double wl, wr;
            if (flow) {
                flBw_ += 0.55 * (tailIn - flBw_);
                double v = flBw_;
                v = ap(fdIn_[0], v, fdInT_[0], 0.75); v = ap(fdIn_[1], v, fdInT_[1], 0.75);
                v = ap(fdIn_[2], v, fdInT_[2], 0.625); v = ap(fdIn_[3], v, fdInT_[3], 0.625);
                mph1_ += 1.1 / sr; if (mph1_ >= 1) mph1_ -= 1; mph2_ += 0.87 / sr; if (mph2_ >= 1) mph2_ -= 1;
                const double mL = std::sin(kTwoPi * mph1_) * flMod, mR = std::sin(kTwoPi * mph2_) * flMod;
                const double inL = v + tankR_ * tg, inR = v + tankL_ * tg;
                double a = ap(apL1_, inL, 672 * k + mL + flMod + 2, -0.7);
                d1L_.push(a); double y = d1L_.tap(4453 * k * sf); lpL_ += dcoef * (y - lpL_); y = lpL_;
                y = ap(apL2_, y * tg, 1800 * k, dd2); d2L_.push(y); tankL_ = d2L_.tap(3720 * k * sf);
                a = ap(apR1_, inR, 908 * k + mR + flMod + 2, -0.7);
                d1R_.push(a); y = d1R_.tap(4217 * k * sf); lpR_ += dcoef * (y - lpR_); y = lpR_;
                y = ap(apR2_, y * tg, 2656 * k, dd2); d2R_.push(y); tankR_ = d2R_.tap(3163 * k * sf);
                wl = 0.6 * (d1R_.tap(266 * k * sf) + d1R_.tap(2974 * k * sf) - apR2_.tap(1913 * k) + d2R_.tap(1996 * k * sf) - d1L_.tap(1990 * k * sf) - apL2_.tap(187 * k) - d2L_.tap(1066 * k * sf));
                wr = 0.6 * (d1L_.tap(353 * k * sf) + d1L_.tap(3627 * k * sf) - apL2_.tap(1228 * k) + d2L_.tap(2673 * k * sf) - d1R_.tap(2111 * k * sf) - apR2_.tap(335 * k) - d2R_.tap(121 * k * sf));
                wl *= 0.8; wr *= 0.8;
            } else {
                double v = tailIn;
                if (form) {
                    double s = 0;
                    for (int j = 0; j < 3; j++) {
                        const int o = j * 4;
                        const double y = (double)fb0_[j] * (v - (double)fx_[o + 1]) - (double)fa1_[j] * (double)fx_[o + 2] - (double)fa2_[j] * (double)fx_[o + 3];
                        fx_[o + 1] = fx_[o]; fx_[o] = (float)v; fx_[o + 3] = fx_[o + 2]; fx_[o + 2] = (float)y; s += y * fg_[j];
                    }
                    v = s * 2.2 + v * 0.15;
                }
                for (int d = 0; d < nDif; d++) v = ap(dfB_[d], v, dfT_[d] * (0.5 + size), difG);
                double inj = 0;
                if (moth) {
                    const double dcv = fbV_ - dcX_ + 0.995 * dcY_; dcX_ = fbV_; dcY_ = dcv;
                    double hi = dcv, hq = dcv;
                    for (int s = 0; s < 4; s++) { hi = hsec(s * 4, hA1_[s], hi); hq = hsec((4 + s) * 4, hA2_[s], hq); }
                    const double qd = hq, idl = hq_; hq_ = hi; hi = idl;
                    shPh_ += shInc; if (shPh_ > kTwoPi) shPh_ -= kTwoPi; else if (shPh_ < -kTwoPi) shPh_ += kTwoPi;
                    double sh = hi * std::cos(shPh_) + qd * std::sin(shPh_);
                    sh *= fbAmt; inj = sh > 1 ? 1 : (sh < -1 ? -1 : sh);
                }
                double* y = yd_; float* t = tv_;
                for (int i = 0; i < 8; i++) {
                    double ph = (double)lph_[i] + (double)mInc_[i]; if (ph >= 1) ph -= 1; lph_[i] = (float)ph;
                    const double vv_ = lines_[i].tap((double)dly_[i] + mDepth * (std::sin(kTwoPi * ph) + 1));
                    ds_[i] = (float)((double)ds_[i] + dcoef * (vv_ - (double)ds_[i]));
                    yv_[i] = (float)((double)ds_[i] * (double)g_[i]);
                    y[i] = yv_[i];
                }
                t[0] = (float)(y[0] + y[1]); t[1] = (float)(y[0] - y[1]); t[2] = (float)(y[2] + y[3]); t[3] = (float)(y[2] - y[3]); t[4] = (float)(y[4] + y[5]); t[5] = (float)(y[4] - y[5]); t[6] = (float)(y[6] + y[7]); t[7] = (float)(y[6] - y[7]);
                const double u0 = t[0] + t[2], u1 = t[1] + t[3], u2 = t[0] - t[2], u3 = t[1] - t[3], u4 = t[4] + t[6], u5 = t[5] + t[7], u6 = t[4] - t[6], u7 = t[5] - t[7];
                const double s = 0.35355339, inG = 0.3, ii = v * inG + inj * 0.3;
                lines_[0].push(ii + (u0 + u4) * s); lines_[1].push(-ii + (u1 + u5) * s); lines_[2].push(ii + (u2 + u6) * s); lines_[3].push(-ii + (u3 + u7) * s);
                lines_[4].push(ii + (u0 - u4) * s); lines_[5].push(-ii + (u1 - u5) * s); lines_[6].push(ii + (u2 - u6) * s); lines_[7].push(-ii + (u3 - u7) * s);
                wl = 0.42 * (y[0] - y[1] + y[2] - y[3] + y[4] - y[5] + y[6] - y[7]);
                wr = 0.42 * (y[0] + y[1] - y[2] - y[3] + y[4] + y[5] - y[6] - y[7]);
                if (moth) fbV_ = (wl + wr) * 0.5;
            }
            wl = wl * wetGain + erL; wr = wr * wetGain + erR;
            outL[n] = (float)(x * (1 - mix) + wl * mix);
            outR[n] = (float)(x * (1 - mix) + wr * mix);
        }
    }

private:
    static double ap(RvBuf& buf, double x, double d, double gA) { const double z = buf.tap(d), v = x - gA * z; buf.push(v); return z + gA * v; }
    // one Hilbert allpass section: y = a^2 (x + y[n-2]) - x[n-2]
    double hsec(int o, double a, double x) {
        const double x2 = hs_[o + 1], y2 = hs_[o + 3];
        const double y = a * a * (x + y2) - x2;
        hs_[o + 1] = hs_[o]; hs_[o] = (float)x; hs_[o + 3] = hs_[o + 2]; hs_[o + 2] = (float)y;
        return y;
    }
    static const double* baseFor(int algo) {
        static const double b0[8] = { 37.3, 44.7, 53.1, 61.9, 71.3, 83.7, 97.9, 113.3 };
        static const double b1[8] = { 11.3, 14.9, 17.7, 21.1, 25.3, 29.9, 34.7, 40.3 };
        static const double b2[8] = { 19.9, 23.9, 29.9, 34.1, 41.9, 47.9, 55.1, 63.7 };
        static const double b4[8] = { 17.3, 21.7, 26.3, 31.9, 37.1, 43.3, 51.7, 59.9 };
        static const double b5[8] = { 24.1, 29.3, 35.9, 41.3, 47.9, 55.7, 63.1, 71.9 };
        switch (algo) { case 0: return b0; case 1: return b1; case 2: return b2; case 4: return b4; default: return b5; }
    }

    double sr_ = 48000, k_ = 1;
    RvBuf pre_, er_, dfB_[8], lines_[8], fdIn_[4], apL1_, apR1_, apL2_, apR2_, d1L_, d2L_, d1R_, d2R_;
    double dfT_[8] = {}, fdInT_[4] = {};
    float dly_[8] = {}, g_[8] = {}, ds_[8] = {}, yv_[8] = {}, tv_[8] = {}, lph_[8] = {}, mInc_[8] = {};
    double yd_[8] = {};
    double erLp_ = 0, erLpR_ = 0;
    const double erTL_[12] = { 4.3, 9.7, 14.9, 21.1, 27.7, 35.3, 43.1, 52.7, 63.9, 76.1, 89.3, 104.7 };
    const double erTR_[12] = { 3.1, 8.3, 13.7, 19.9, 29.3, 37.9, 46.1, 57.3, 69.5, 81.7, 96.1, 110.3 };
    const double hA1_[4] = { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
    const double hA2_[4] = { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };
    float hs_[32] = {};
    double hq_ = 0, shPh_ = 0, fbV_ = 0, dcX_ = 0, dcY_ = 0;
    float fx_[12] = {}, fb0_[3] = {}, fa1_[3] = {}, fa2_[3] = {};
    const double fg_[3] = { 1, 0.75, 0.55 };
    double lfoPh_ = 0;
    const double vow_[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 1700, 2700 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
    double lpL_ = 0, lpR_ = 0, tankL_ = 0, tankR_ = 0, flBw_ = 0, mph1_ = 0, mph2_ = 0.4;
};

} // namespace vv
