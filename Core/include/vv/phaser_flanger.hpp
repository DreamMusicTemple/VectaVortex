// Phaser (2-12 stage allpass, stereo quarter-cycle LFO) and flanger. Port of PhaserFlangerProcessor.
#pragma once
#include "dsp_common.hpp"

namespace vv {

class PhaserFlanger {
public:
    struct Params { int mode = 0; /*0 phaser 1 flanger*/ double rate = 0.5, depth = 0.7, feedback = 0.3, mix = 0.5; int stages = 8; };

    void prepare(double sampleRate) {
        sr_ = sampleRate;
        delay_.init((int)std::ceil(0.05 * sr_) + 4);
        reset();
    }
    void reset() {
        lfoPhase_ = 0; fbState_ = 0; delay_.clear();
        for (int c = 0; c < 2; c++) { std::memset(apX_[c], 0, sizeof apX_[c]); std::memset(apY_[c], 0, sizeof apY_[c]); pfb_[c] = 0; }
    }
    // in: mono (may be nullptr). outL/outR: both written (flanger writes the same signal to both).
    void process(const float* in, float* outL, float* outR, int n, const Params& p) {
        const bool flanger = p.mode > 0;
        int stages = 2 * (int)std::lround(clampd(p.stages, 2, 12) / 2.0);
        if (stages < 2) stages = 2;
        if (stages > 12) stages = 12;
        const double piSr = 3.141592653589793 / sr_, fcMax = sr_ * 0.45;
        for (int i = 0; i < n; i++) {
            lfoPhase_ += p.rate / sr_; if (lfoPhase_ >= 1) lfoPhase_ -= 1;
            const double x = in ? in[i] : 0.0;
            if (flanger) {
                const double lfo = std::sin(kTwoPi * lfoPhase_);
                const double base = 0.001 * sr_, mod = p.depth * 0.005 * sr_;
                const double d = base + mod * (lfo * 0.5 + 0.5);
                delay_.write(x + fbState_ * p.feedback);
                const double wet = delay_.readInterpolated(d);
                fbState_ = wet;
                delay_.advance();
                const double o = x * (1 - p.mix) + wet * p.mix;
                outL[i] = (float)o; outR[i] = (float)o;
            } else {
                for (int ch = 0; ch < 2; ch++) {
                    const double lfo = std::sin(kTwoPi * (lfoPhase_ + ch * 0.25)) * 0.5 + 0.5;
                    double fc = 250.0 * std::pow(2.0, p.depth * 4.5 * lfo); if (fc > fcMax) fc = fcMax;
                    const double wc = std::tan(piSr * fc), g = (wc - 1) / (wc + 1);
                    float* ax = apX_[ch]; float* ay = apY_[ch];
                    double sig = x + (double)pfb_[ch] * p.feedback;
                    for (int s = 0; s < stages; s++) {
                        const double y = g * sig + (double)ax[s] - g * (double)ay[s];
                        ax[s] = (float)sig; ay[s] = (float)y; sig = y;
                    }
                    if (sig > 6) sig = 6; else if (sig < -6) sig = -6;
                    pfb_[ch] = (float)sig;
                    const double o = x * (1 - p.mix) + sig * p.mix;
                    if (ch == 0) outL[i] = (float)o; else outR[i] = (float)o;
                }
            }
        }
    }
private:
    double sr_ = 48000, lfoPhase_ = 0, fbState_ = 0;
    DelayLine delay_;
    float apX_[2][12] = {}, apY_[2][12] = {};
    float pfb_[2] = {};
};

} // namespace vv
