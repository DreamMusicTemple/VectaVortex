// Vocoder - port of rebuildVocoder() in the web app. Modulator = pre-bus Wavetable layer signal (xmodTap.WT), carrier = the checked
// Sampler slots (post slot envelope/mix, pre filter). Per band: bandpass(Q 4) on both, modulator -> rectifier (256-point |x| WaveShaper)
// -> 20 Hz lowpass (Q 1 dB, Chromium) -> gain of the carrier band. Output x (mix * 14) goes to the master bus.
// Built from Chromium's BiquadFilterNode formulas; processed per channel (the lowpass output is averaged to mono before it drives the gain,
// exactly like an AudioParam input).
#pragma once
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

namespace vv {

struct VocBiquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void reset() { x1 = x2 = y1 = y2 = 0; }
    void norm(double B0, double B1, double B2, double A0, double A1, double A2) { b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0; }
    // Chromium setBandpassParams (Q linear)
    void bandpass(double freq, double q, double sr) {
        const double f = std::max(0.0, std::min(1.0, freq / (sr * 0.5)));
        if (f > 0 && f < 1) {
            if (q > 0) { const double w0 = 3.141592653589793 * f, alpha = std::sin(w0) / (2 * q), k = std::cos(w0); norm(alpha, 0, -alpha, 1 + alpha, -2 * k, 1 - alpha); }
            else { b0 = 1; b1 = b2 = a1 = a2 = 0; }
        } else { b0 = b1 = b2 = a1 = a2 = 0; }
    }
    // Chromium setLowpassParams (resonance in dB)
    void lowpass(double freq, double qDb, double sr) {
        const double f = std::max(0.0, std::min(1.0, freq / (sr * 0.5)));
        if (f == 1) { b0 = 1; b1 = b2 = a1 = a2 = 0; return; }
        if (f <= 0) { b0 = b1 = b2 = a1 = a2 = 0; return; }
        const double q = std::pow(10.0, qDb / 20.0), th = 3.141592653589793 * f, alpha = std::sin(th) / (2 * q), cw = std::cos(th), beta = (1 - cw) / 2;
        norm(beta, 2 * beta, beta, 1 + alpha, -2 * cw, 1 - alpha);
    }
    // Chromium setHighpassParams (resonance in dB), same structure as lowpass()
    void highpass(double freq, double qDb, double sr) {
        const double f = std::max(0.0, std::min(1.0, freq / (sr * 0.5)));
        if (f == 1) { b0 = b1 = b2 = a1 = a2 = 0; return; }
        if (f <= 0) { b0 = 1; b1 = b2 = a1 = a2 = 0; return; }
        const double q = std::pow(10.0, qDb / 20.0), th = 3.141592653589793 * f, alpha = std::sin(th) / (2 * q), cw = std::cos(th), beta = (1 + cw) / 2;
        norm(beta, -2 * beta, beta, 1 + alpha, -2 * cw, 1 - alpha);
    }
    inline double tick(double x) { const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
};

class Vocoder {
public:
    static constexpr int kMaxBands = 64;
    bool on = false; int nBands = 8; bool logSpacing = true; double mix = 0.8; bool slots[16];
    Vocoder() { for (bool& b : slots) b = true; }

    // (re)builds the band bank: all filter state starts at zero, band gains at 0 (like fresh nodes)
    void rebuild(double sr) {
        sr_ = sr; if (!on) return;
        const int n = std::max(2, std::min(kMaxBands, nBands));
        nActive_ = n;
        for (int i = 0; i < n; i++) {
            const double t = (double)i / (n - 1), freq = logSpacing ? 200.0 * std::pow(20.0, t) : 200.0 + t * 6000.0;
            for (int c = 0; c < 2; c++) { b_[i].mod[c].bandpass(freq, 4, sr); b_[i].car[c].bandpass(freq, 4, sr); b_[i].env[c].lowpass(20, 1, sr); b_[i].mod[c].reset(); b_[i].car[c].reset(); b_[i].env[c].reset(); }
        }
        for (int k = 0; k < 256; k++) curve_[k] = (float)std::fabs((k / 255.0) * 2 - 1);
        wet_ = mix * 14;
    }
    void setMix(double m) { mix = m; wet_ = m * 14; }
    // modL/modR: modulator; carL/carR: carrier. Adds the result into outL/outR.
    void process(const float* modL, const float* modR, const float* carL, const float* carR, float* outL, float* outR, int n) {
        if (!on) return;
        for (int i = 0; i < n; i++) {
            double sl = 0, sr = 0;
            for (int b = 0; b < nActive_; b++) {
                Band& B = b_[b];
                const double ml = B.mod[0].tick(modL[i]), mr = B.mod[1].tick(modR[i]);
                const double el = B.env[0].tick(shape(ml)), er = B.env[1].tick(shape(mr));
                const double g = 0.5 * (el + er);   // AudioParam input: stereo -> mono downmix
                sl += B.car[0].tick(carL[i]) * g; sr += B.car[1].tick(carR[i]) * g;
            }
            outL[i] += (float)(sl * wet_); outR[i] += (float)(sr * wet_);
        }
    }

private:
    struct Band { VocBiquad mod[2], car[2], env[2]; };
    // WaveShaper: linear interpolation of the 256-point |x| curve, input clamped to [-1, 1]
    inline double shape(double x) const {
        if (x <= -1) return curve_[0]; if (x >= 1) return curve_[255];
        const double v = (x + 1) * 0.5 * 255.0; const int k = (int)std::floor(v); const double f = v - k;
        return k >= 255 ? curve_[255] : curve_[k] * (1 - f) + curve_[k + 1] * f;
    }
    double sr_ = 48000, wet_ = 0; int nActive_ = 0; Band b_[kMaxBands]; float curve_[256];
};

}  // namespace vv
