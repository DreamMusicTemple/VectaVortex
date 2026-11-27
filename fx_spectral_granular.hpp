// Ports of the SpectralFX (STFT) and Granular worklets.
#pragma once
#include <array>
#include "dsp_common.hpp"
#include "fx_basic.hpp"

namespace vv {

// ---------------------------------------------------------------------------------------------------------- SpectralFX
// In-place radix-2 FFT on float arrays. Every store rounds to float exactly like the JavaScript Float32Array version.
inline void fftInPlace(float* re, float* im, int n, bool invert) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = (invert ? 2 : -2) * 3.141592653589793 / len;
        const double wRe = std::cos(ang), wIm = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            double curRe = 1, curIm = 0;
            const int half = len >> 1;
            for (int j = 0; j < half; j++) {
                const double uRe = re[i + j], uIm = im[i + j];
                const double vRe = re[i + j + half] * curRe - im[i + j + half] * curIm;
                const double vIm = re[i + j + half] * curIm + im[i + j + half] * curRe;
                re[i + j] = (float)(uRe + vRe); im[i + j] = (float)(uIm + vIm);
                re[i + j + half] = (float)(uRe - vRe); im[i + j + half] = (float)(uIm - vIm);
                const double nRe = curRe * wRe - curIm * wIm, nIm = curRe * wIm + curIm * wRe;
                curRe = nRe; curIm = nIm;
            }
        }
    }
    if (invert) for (int i = 0; i < n; i++) { re[i] = (float)(re[i] / n); im[i] = (float)(im[i] / n); }
}

class SpectralFX {
public:
    struct Params { bool freeze = false; double blur = 0, shift = 0, threshold = 0, spread = 0, tilt = 0, disperse = 0, mix = 1; };   // tilt: dB/oct about 1 kHz; disperse: 0..1 quadratic phase offset
    static constexpr int kFft = 1024, kHop = kFft / 4, kBins = kFft / 2;
    SpectralFX() { for (int i = 0; i < kFft; i++) window_[i] = (float)(0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / kFft)); reset(); }
    void prepare(double sr) { sr_ = sr; reset(); }
    void reset() {
        std::fill(inBuf_, inBuf_ + kFft, 0.f); inFill_ = 0; std::fill(accL_, accL_ + kFft, 0.f); std::fill(accR_, accR_ + kFft, 0.f);
        cursor_ = 0; writeCursor_ = 0; hasFrozen_ = false;
        std::fill(frozenMag_, frozenMag_ + kBins, 0.f); std::fill(frozenPhase_, frozenPhase_ + kBins, 0.f);
    }
    // 1024-sample analysis window, so the effect has 768 samples of latency (the web version behaves identically)
    static constexpr int latency() { return kFft - kHop; }
    void process(const float* in, float* outL, float* outR, int n, const Params& p) {
        if (!in) return;
        for (int s = 0; s < n; s++) {
            inBuf_[inFill_] = in[s]; inFill_++;
            if (inFill_ == kFft) {
                frame(p);
                std::memmove(inBuf_, inBuf_ + kHop, (kFft - kHop) * sizeof(float));
                inFill_ = kFft - kHop;
            }
            const double dry = in[s];
            const double wetL = accL_[cursor_] * (1.0 / 1.5), wetR = accR_[cursor_] * (1.0 / 1.5);
            accL_[cursor_] = 0; accR_[cursor_] = 0;
            cursor_ = (cursor_ + 1) % kFft;
            outL[s] = (float)(dry * (1 - p.mix) + wetL * p.mix);
            outR[s] = (float)(dry * (1 - p.mix) + wetR * p.mix);
        }
    }
private:
    double sr_ = 44100;
    void frame(const Params& p) {
        const int n = kFft, nb = kBins;
        for (int i = 0; i < n; i++) { reA_[i] = (float)((double)inBuf_[i] * window_[i]); imA_[i] = 0; }
        fftInPlace(reA_, imA_, n, false);
        for (int i = 0; i < nb; i++) { mag_[i] = (float)std::hypot((double)reA_[i], (double)imA_[i]); phase_[i] = (float)std::atan2((double)imA_[i], (double)reA_[i]); }
        if (p.freeze) {
            if (!hasFrozen_) { std::memcpy(frozenMag_, mag_, sizeof mag_); std::memcpy(frozenPhase_, phase_, sizeof phase_); hasFrozen_ = true; }
        } else hasFrozen_ = false;
        const float* srcMag = p.freeze ? frozenMag_ : mag_;
        const float* srcPhase = p.freeze ? frozenPhase_ : phase_;
        const double blurAmt = p.blur;
        if (blurAmt > 0) {
            blurred_[0] = srcMag[0]; blurred_[nb - 1] = srcMag[nb - 1];
            for (int i = 1; i < nb - 1; i++) blurred_[i] = (float)((srcMag[i - 1] + (double)srcMag[i] * 2.0 + srcMag[i + 1]) * 0.25);
        }
        const int shiftOffset = (int)jsRound(p.shift);
        for (int i = 0; i < nb; i++) {
            const double baseMag = blurAmt > 0 ? ((double)srcMag[i] * (1 - blurAmt) + (double)blurred_[i] * blurAmt) : srcMag[i];
            const int si = i - shiftOffset;
            if (shiftOffset != 0) {
                if (si >= 0 && si < nb) {
                    const double sb = blurAmt > 0 ? ((double)srcMag[si] * (1 - blurAmt) + (double)blurred_[si] * blurAmt) : srcMag[si];
                    shMag_[i] = (float)sb; shPhase_[i] = srcPhase[si];
                } else { shMag_[i] = 0; shPhase_[i] = 0; }
            } else { shMag_[i] = (float)baseMag; shPhase_[i] = srcPhase[i]; }
        }
        if (p.tilt != 0) {   // spectral tilt: gain slope of tilt dB per octave, pivot 1 kHz (web: SpectralFXProcessor 3b)
            const double binHz = sr_ / kFft;
            for (int i = 1; i < nb; i++) shMag_[i] = (float)((double)shMag_[i] * std::pow(10.0, p.tilt * std::log2(i * binHz / 1000.0) / 20.0));
        }
        if (p.disperse > 0) {   // phase dispersion: phi(k) = amount * 40 * (k/N)^2 (web 3c)
            for (int i = 1; i < nb; i++) { const double q = (double)i / nb; shPhase_[i] = (float)((double)shPhase_[i] + p.disperse * 40.0 * q * q); }
        }
        const double thr = p.threshold * (kFft * 0.5);
        for (int i = 0; i < nb; i++) if (shMag_[i] < thr) { shMag_[i] = 0; shPhase_[i] = 0; }
        for (int i = 0; i < n; i++) { reL_[i] = 0; imL_[i] = 0; reR_[i] = 0; imR_[i] = 0; }
        for (int i = 0; i < nb; i++) {
            const double pan = (double)i / nb;
            const double gL = p.spread > 0 ? (1.0 - pan * p.spread) : 1.0;
            const double gR = p.spread > 0 ? (pan * p.spread) : 1.0;
            const double mL = shMag_[i] * gL, mR = shMag_[i] * gR;
            const double ph = shPhase_[i];
            const double rl = mL * std::cos(ph), il = mL * std::sin(ph), rr = mR * std::cos(ph), ir = mR * std::sin(ph);
            reL_[i] = (float)rl; imL_[i] = (float)il; reR_[i] = (float)rr; imR_[i] = (float)ir;
            if (i > 0 && i < nb) { reL_[n - i] = (float)rl; imL_[n - i] = (float)-il; reR_[n - i] = (float)rr; imR_[n - i] = (float)-ir; }
        }
        fftInPlace(reL_, imL_, n, true);
        fftInPlace(reR_, imR_, n, true);
        for (int i = 0; i < n; i++) {
            const int idx = (writeCursor_ + i) % kFft;
            accL_[idx] = (float)(accL_[idx] + (double)reL_[i] * window_[i]);
            accR_[idx] = (float)(accR_[idx] + (double)reR_[i] * window_[i]);
        }
        writeCursor_ = (writeCursor_ + kHop) % kFft;
    }
    float window_[kFft], inBuf_[kFft], accL_[kFft], accR_[kFft], reA_[kFft], imA_[kFft], reL_[kFft], imL_[kFft], reR_[kFft], imR_[kFft];
    float mag_[kBins], phase_[kBins], frozenMag_[kBins], frozenPhase_[kBins], blurred_[kBins], shMag_[kBins], shPhase_[kBins];
    int inFill_ = 0, cursor_ = 0, writeCursor_ = 0; bool hasFrozen_ = false;
};

// ---------------------------------------------------------------------------------------------------------- Granular
class Granular {
public:
    struct Params { double grainSize = 80, density = 20, pitch = 0, spread = 0.5, panCenter = 0, envShape = 0, followerAmount = 0, mix = 1; bool noSpawn = false; };   // noSpawn: the extended engine (granular_x.hpp) is in charge, no new original-style grains
    static constexpr int kMaxGrains = 60;
    void prepare(double sr, uint32_t seed = 1) { sr_ = sr; size_ = (int)std::ceil(sr * 2); buf_.assign(size_, 0.f); rng_.s = seed; reset(); }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.f); wpos_ = 0; count_ = 0; clock_ = 0; env_ = 0; }
    LcgRandom& rng() { return rng_; }
    void process(const float* in, float* outL, float* outR, int n, const Params& p) {
        if (!in) return;
        const int grainSamples = (int)std::max(1.0, jsRound(p.grainSize / 1000 * sr_));
        const double rate = std::pow(2.0, p.pitch / 12);
        for (int i = 0; i < n; i++) {
            buf_[wpos_] = in[i];
            const double absIn = std::fabs((double)in[i]);
            env_ += (absIn - env_) * 0.002;
            const double effD = p.density * (1 + p.followerAmount * env_ * 4);
            const double spg = sr_ / std::max(0.1, effD);
            clock_++;
            if (!p.noSpawn && clock_ >= spg && count_ < kMaxGrains) {
                clock_ = 0;
                const int lookback = grainSamples + (int)std::floor(rng_.next() * grainSamples);
                const int rs = ((wpos_ - lookback) % size_ + size_) % size_;
                const double pan = clampd(p.panCenter + (rng_.next() * 2 - 1) * p.spread, -1, 1);
                Grain& g = grains_[count_++]; g.readPos = rs; g.age = 0; g.duration = grainSamples; g.pan = pan; g.rate = rate;
            }
            double sumL = 0, sumR = 0;
            for (int gi = count_ - 1; gi >= 0; gi--) {
                Grain& g = grains_[gi];
                if (g.age >= g.duration) { for (int k = gi; k < count_ - 1; k++) grains_[k] = grains_[k + 1]; count_--; continue; }
                const double idxF = g.readPos + g.age * g.rate;
                const double fl = std::floor(idxF);
                const int i0 = (int)std::fmod(fl, (double)size_), i1 = (i0 + 1) % size_;
                const double frac = idxF - fl;
                const double sample = buf_[(i0 + size_) % size_] * (1 - frac) + buf_[(i1 + size_) % size_] * frac;
                const double env = envelope((double)g.age / g.duration, p.envShape);
                const double angle = (g.pan + 1) * 3.141592653589793 / 4;
                sumL += sample * env * std::cos(angle); sumR += sample * env * std::sin(angle);
                g.age++;
            }
            outL[i] = (float)(in[i] * (1 - p.mix) + sumL * p.mix);
            outR[i] = (float)(in[i] * (1 - p.mix) + sumR * p.mix);
            wpos_ = (wpos_ + 1) % size_;
        }
    }
private:
    static double envelope(double pos, double shape) {
        if (shape < 1) return std::sin(3.141592653589793 * pos);
        if (shape < 2) return pos < 0.5 ? pos * 2 : (1 - pos) * 2;
        if (shape < 3) return std::exp(-std::pow((pos - 0.5) * 4.5, 2));
        const double fade = 0.02; if (pos < fade) return pos / fade; if (pos > 1 - fade) return (1 - pos) / fade; return 1;
    }
    struct Grain { double readPos = 0; int age = 0, duration = 1; double pan = 0, rate = 1; };
    double sr_ = 48000, clock_ = 0, env_ = 0; int size_ = 1, wpos_ = 0, count_ = 0; std::vector<float> buf_; Grain grains_[kMaxGrains]; LcgRandom rng_;
};

} // namespace vv
