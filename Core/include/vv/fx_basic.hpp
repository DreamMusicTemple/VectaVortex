// Ports of the simpler effect worklets: EQ, Resonator, Delay, Chorus, Panning, BandPan, Saturation, Wavefolder, Tape.
// Each mirrors the JavaScript AudioWorklet in full-app.html line for line (double scalars, float buffers) so the golden tests match.
// All memory is allocated in prepare(); process() never allocates.
#pragma once
#include "rotary_amp.hpp"
#include "dsp_common.hpp"

namespace vv {

// JavaScript's Math.round (halves round up)
inline double jsRound(double v) { return std::floor(v + 0.5); }

// ---------------------------------------------------------------------------------------------------------- Biquad
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void reset() { x1 = x2 = y1 = y2 = 0; }
    void norm(double B0, double B1, double B2, double A0, double A1, double A2) { b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0; }
    void setLowshelf(double freq, double gainDb, double sr) {
        const double A = std::pow(10.0, gainDb / 40), w0 = 2 * 3.141592653589793 * freq / sr, cosw0 = std::cos(w0), sinw0 = std::sin(w0);
        const double alpha = sinw0 / 2 * std::sqrt((A + 1 / A) * (1 / 0.707 - 1) + 2), sqA = std::sqrt(A);
        norm(A * ((A + 1) - (A - 1) * cosw0 + 2 * sqA * alpha), 2 * A * ((A - 1) - (A + 1) * cosw0), A * ((A + 1) - (A - 1) * cosw0 - 2 * sqA * alpha),
             (A + 1) + (A - 1) * cosw0 + 2 * sqA * alpha, -2 * ((A - 1) + (A + 1) * cosw0), (A + 1) + (A - 1) * cosw0 - 2 * sqA * alpha);
    }
    void setHighshelf(double freq, double gainDb, double sr) {
        const double A = std::pow(10.0, gainDb / 40), w0 = 2 * 3.141592653589793 * freq / sr, cosw0 = std::cos(w0), sinw0 = std::sin(w0);
        const double alpha = sinw0 / 2 * std::sqrt((A + 1 / A) * (1 / 0.707 - 1) + 2), sqA = std::sqrt(A);
        norm(A * ((A + 1) + (A - 1) * cosw0 + 2 * sqA * alpha), -2 * A * ((A - 1) + (A + 1) * cosw0), A * ((A + 1) + (A - 1) * cosw0 - 2 * sqA * alpha),
             (A + 1) - (A - 1) * cosw0 + 2 * sqA * alpha, 2 * ((A - 1) - (A + 1) * cosw0), (A + 1) - (A - 1) * cosw0 - 2 * sqA * alpha);
    }
    void setPeaking(double freq, double gainDb, double Q, double sr) {
        const double A = std::pow(10.0, gainDb / 40), w0 = 2 * 3.141592653589793 * freq / sr, cosw0 = std::cos(w0), alpha = std::sin(w0) / (2 * Q);
        norm(1 + alpha * A, -2 * cosw0, 1 - alpha * A, 1 + alpha / A, -2 * cosw0, 1 - alpha / A);
    }
    void setBandpass(double freq, double Q, double sr) {
        const double w0 = 2 * 3.141592653589793 * freq / sr, cosw0 = std::cos(w0), alpha = std::sin(w0) / (2 * Q);
        norm(alpha, 0, -alpha, 1 + alpha, -2 * cosw0, 1 - alpha);
    }
    void setAllpass(double freq, double Q, double sr) {
        const double w0 = 2 * 3.141592653589793 * freq / sr, cosw0 = std::cos(w0), alpha = std::sin(w0) / (2 * Q);
        norm(1 - alpha, -2 * cosw0, 1 + alpha, 1 + alpha, -2 * cosw0, 1 - alpha);
    }
    // Web Audio lowpass / highpass: Q is in dB (alpha = sin(w0) / (2 * 10^(Q/20)))
    void setLowpassDb(double freq, double qDb, double sr) {
        const double w0 = 2 * 3.141592653589793 * freq / sr, cosw0 = std::cos(w0), alpha = std::sin(w0) / (2 * std::pow(10.0, qDb / 20));
        norm((1 - cosw0) / 2, 1 - cosw0, (1 - cosw0) / 2, 1 + alpha, -2 * cosw0, 1 - alpha);
    }
    void setHighpassDb(double freq, double qDb, double sr) {
        const double w0 = 2 * 3.141592653589793 * freq / sr, cosw0 = std::cos(w0), alpha = std::sin(w0) / (2 * std::pow(10.0, qDb / 20));
        norm((1 + cosw0) / 2, -(1 + cosw0), (1 + cosw0) / 2, 1 + alpha, -2 * cosw0, 1 - alpha);
    }
    inline double process(double x) {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// ---------------------------------------------------------------------------------------------------------- EQ (4 band)
class EQ {
public:
    struct Params { double freq[4] = {120, 600, 2500, 8000}, gain[4] = {0, 0, 0, 0}; };
    void prepare(double sr) { sr_ = sr; reset(); }
    void reset() { for (auto& b : bands_) { b = Biquad{}; } }
    void process(const float* in, float* out, int n, const Params& p) {
        bands_[0].setLowshelf(p.freq[0], p.gain[0], sr_);
        bands_[1].setPeaking(p.freq[1], p.gain[1], 1, sr_);
        bands_[2].setPeaking(p.freq[2], p.gain[2], 1, sr_);
        bands_[3].setHighshelf(p.freq[3], p.gain[3], sr_);
        for (int i = 0; i < n; i++) {
            double x = in ? in[i] : 0.0;
            x = bands_[0].process(x); x = bands_[1].process(x); x = bands_[2].process(x); x = bands_[3].process(x);
            out[i] = (float)x;
        }
    }
private:
    double sr_ = 48000; Biquad bands_[4];
};

// ---------------------------------------------------------------------------------------------------------- Resonator
class Resonator {
public:
    struct Params { double freq = 440, resonance = 15, mix = 0.4; };
    void prepare(double sr) { sr_ = sr; reset(); }
    void reset() { for (auto& b : bands_) b = Biquad{}; }
    void process(const float* in, float* out, int n, const Params& p) {
        for (int i = 0; i < 4; i++) bands_[i].setBandpass(p.freq * (i + 1), p.resonance, sr_);
        for (int i = 0; i < n; i++) {
            const double x = in ? in[i] : 0.0;
            double wet = 0; for (int b = 0; b < 4; b++) wet += bands_[b].process(x);
            out[i] = (float)(x * (1 - p.mix) + wet * p.mix);
        }
    }
private:
    double sr_ = 48000; Biquad bands_[4];
};

// ---------------------------------------------------------------------------------------------------------- Delay
class Delay {
public:
    struct Params { int mode = 1; /*0 mono 1 stereo 2 ping-pong 3 reverse*/ double time = 0.35, feedback = 0.35, mix = 0.25; };
    void prepare(double sr) {
        sr_ = sr;
        const int maxLen = (int)std::ceil(2.0 * sr) + 8;
        dL_.init(maxLen); dR_.init(maxLen);
        revMax_ = (int)std::ceil(1.5 * sr) + 8;
        bufA_.assign(revMax_, 0.f); bufB_.assign(revMax_, 0.f);
        reset();
    }
    void reset() { dL_.clear(); dR_.clear(); std::fill(bufA_.begin(), bufA_.end(), 0.f); std::fill(bufB_.begin(), bufB_.end(), 0.f); revPos_ = 0; revLen_ = revMax_; revA_ = true; }
    void process(const float* in, float* outL, float* outR, int n, const Params& p) {
        const int mode = (int)jsRound(p.mode);
        const double ts = p.time * sr_;
        if (mode == 3) revLen_ = (int)std::max(1.0, std::min((double)revMax_, jsRound(ts)));
        for (int i = 0; i < n; i++) {
            const double x = in ? in[i] : 0.0;
            double wetL = 0, wetR = 0;
            if (mode == 0) {
                const double r = dL_.readInterpolated(ts);
                dL_.write(x + r * p.feedback); dL_.advance();
                wetL = wetR = r;
            } else if (mode == 1) {
                const double r = dL_.readInterpolated(ts);
                dL_.write(x + r * p.feedback); dL_.advance();
                const double rr = dR_.readInterpolated(ts);
                dR_.write(x + rr * p.feedback); dR_.advance();
                wetL = r; wetR = rr;
            } else if (mode == 2) {
                const double dRprev = dR_.readInterpolated(ts);
                const double dLprev = dL_.readInterpolated(ts);
                dL_.write(x + dRprev * p.feedback); dL_.advance();
                dR_.write(dLprev * p.feedback); dR_.advance();
                wetL = dLprev; wetR = dRprev;
            } else {
                wetL = wetR = reverse(x);
            }
            outL[i] = (float)(x * (1 - p.mix) + wetL * p.mix);
            outR[i] = (float)(x * (1 - p.mix) + wetR * p.mix);
        }
    }
private:
    inline double reverse(double x) {
        std::vector<float>& wb = revA_ ? bufA_ : bufB_;
        std::vector<float>& rb = revA_ ? bufB_ : bufA_;
        wb[revPos_] = (float)x;
        const int rp = revLen_ - 1 - revPos_;
        const double out = (rp >= 0 && rp < revLen_) ? rb[rp] : 0.0;
        revPos_++;
        if (revPos_ >= revLen_) { revPos_ = 0; revA_ = !revA_; }
        return out;
    }
    double sr_ = 48000; DelayLine dL_, dR_;
    std::vector<float> bufA_, bufB_; int revMax_ = 1, revPos_ = 0, revLen_ = 1; bool revA_ = true;
};

// ---------------------------------------------------------------------------------------------------------- Chorus
class Chorus {
public:
    struct Params { bool c1 = true, c2 = false; double mix = 0.5; };
    void prepare(double sr) {
        sr_ = sr;
        v_[0].init(sr, 0.51, 3.5, 6.5); v_[1].init(sr, 0.83, 6.0, 9.0);
    }
    void reset() { for (auto& v : v_) { v.phase = 0; v.line.clear(); } }
    void process(const float* in, float* out, int n, const Params& p) {
        for (int i = 0; i < n; i++) {
            const double x = in ? in[i] : 0.0;
            const double o1 = v_[0].process(x, sr_), o2 = v_[1].process(x, sr_);
            double wet = 0; int cnt = 0;
            if (p.c1) { wet += o1; cnt++; }
            if (p.c2) { wet += o2; cnt++; }
            if (cnt > 0) wet /= cnt;
            out[i] = (float)(x * (1 - p.mix) + wet * p.mix);
        }
    }
private:
    struct Voice {
        double rate = 0, depthS = 0, baseS = 0, phase = 0; DelayLine line;
        void init(double sr, double rateHz, double depthMs, double baseMs) { rate = rateHz; depthS = depthMs / 1000 * sr; baseS = baseMs / 1000 * sr; phase = 0; line.init((int)std::ceil(0.03 * sr) + 8); }
        inline double process(double x, double sr) {
            phase += rate / sr; if (phase >= 1) phase -= 1;
            const double lfo = std::sin(2 * 3.141592653589793 * phase);
            const double d = baseS + depthS * (lfo * 0.5 + 0.5);
            line.write(x);
            const double y = line.readInterpolated(d);
            line.advance();
            return y;
        }
    };
    double sr_ = 48000; Voice v_[2];
};

// ---------------------------------------------------------------------------------------------------------- Panning (Power Panning main stage)
class Panning {
public:
    struct Params { double width = 0; bool autoPan = false; double rate = 1; int shape = 0; int mode = 0; double distance = 0; };
    void prepare(double sr) { sr_ = sr; reset(); }
    void reset() { phase_ = 0; std::fill(buf_, buf_ + kLen, 0.f); pos_ = 0; }
    void process(const float* in, float* outL, float* outR, int n, const Params& p) {
        const double phInc = p.rate / sr_;
        const int shape = (int)jsRound(p.shape), mode = (int)jsRound(p.mode);
        const int delaySamples = (int)jsRound(p.width * 0.5 * sr_ * 0.02);
        for (int i = 0; i < n; i++) {
            const double x = in ? in[i] : 0.0;
            if (mode == 1) { const double g = 1 / (1 + p.distance * p.distance * 8); outL[i] = (float)(x * g); outR[i] = (float)(x * g); continue; }
            buf_[pos_] = (float)x;
            const int rp = ((pos_ - delaySamples) % kLen + kLen) % kLen;
            const double delayed = buf_[rp];
            pos_ = (pos_ + 1) % kLen;
            double l = x, r = delayed;
            if (p.autoPan) {
                phase_ += phInc; if (phase_ >= 1) phase_ -= 1;
                double pan;
                if (shape == 0) pan = phase_ < 0.5 ? -1 : 1;
                else if (shape == 1) pan = phase_ < 0.5 ? (-1 + 4 * phase_) : (3 - 4 * phase_);
                else {
                    const double half = phase_ < 0.5 ? phase_ * 2 : (phase_ - 0.5) * 2;
                    const double eased = half < 0.5 ? 2 * half * half : 1 - std::pow(-2 * half + 2, 2) / 2;
                    pan = phase_ < 0.5 ? (-1 + 2 * eased) : (1 - 2 * eased);
                }
                const double angle = (pan + 1) * 0.25 * 3.141592653589793;
                l = l * std::cos(angle); r = r * std::sin(angle);
            }
            outL[i] = (float)l; outR[i] = (float)r;
        }
    }
private:
    static constexpr int kLen = 2048;
    double sr_ = 48000, phase_ = 0; float buf_[kLen] = {}; int pos_ = 0;
};

// ---------------------------------------------------------------------------------------------------------- BandPan (one band of the multi-band)
class BandPan {
public:
    struct Params { double on = 1, width = 0.5, depth = 0, speed = 0.5, phase0 = 0, decor = 9; };
    void prepare(double sr) { sr_ = sr; line_.init((int)std::ceil(0.05 * sr) + 8); reset(); }
    void reset() { phase_ = 0; onS_ = 1; line_.clear(); }
    void process(const float* inL, const float* inR, float* oL, float* oR, int n, const Params& p) {
        const double phInc = p.speed / sr_, dS = p.decor * 0.001 * sr_, comp = 1 / std::sqrt(1 + 0.81 * p.width * p.width);
        for (int i = 0; i < n; i++) {
            const double L = inL ? inL[i] : 0.0, R = inR ? inR[i] : L;
            onS_ += (p.on - onS_) * 0.002;
            const double M = (L + R) * 0.5, S = (L - R) * 0.5;
            line_.write(M); const double d = line_.readInterpolated(dS); line_.advance();
            const double S2 = (S + 0.9 * d) * p.width;
            double pL = (M + S2) * comp, pR = (M - S2) * comp;
            phase_ += phInc; if (phase_ >= 1) phase_ -= 1;
            const double pan = p.depth * std::sin(6.2831853 * (phase_ + p.phase0));
            const double a = (pan + 1) * 0.7853982; pL *= std::cos(a) * 1.4142136; pR *= std::sin(a) * 1.4142136;
            oL[i] = (float)(pL * onS_ + L * (1 - onS_)); oR[i] = (float)(pR * onS_ + R * (1 - onS_));
        }
    }
private:
    double sr_ = 48000, phase_ = 0, onS_ = 1; DelayLine line_;
};

// ---------------------------------------------------------------------------------------------------------- Oversampled nonlinearity (Saturation + Wavefolder)
// Linear-interpolated upsample, nonlinearity at every sub-sample, 4 cascaded one-pole anti-alias lowpass stages, keep the last sub-sample.
template <class Shape>
struct OversampledStage {
    double lastIn = 0, y[4] = {0, 0, 0, 0};
    void reset() { lastIn = 0; for (double& v : y) v = 0; }
    // returns the (float-rounded, as in the JS Float32Array scratch) shaped sample
    inline double run(double cur, int factor, double sr, Shape& shape) {
        if (factor <= 1) { const double r = (double)(float)shape(cur); lastIn = cur; return r; }
        const double cutoff = sr * 0.45, srUp = sr * factor;
        const double a = 1 - std::exp(-2 * 3.141592653589793 * cutoff / srUp);
        double out = 0;
        for (int s = 0; s < factor; s++) {
            const double t = (double)(s + 1) / factor;
            const double xUp = lastIn + (cur - lastIn) * t;
            double f = shape(xUp);
            for (int k = 0; k < 4; k++) { y[k] = y[k] + a * (f - y[k]); f = y[k]; }
            out = f;
        }
        lastIn = cur;
        return (double)(float)out;
    }
};

class Saturation {
public:
    struct Params { double drive = 0.4; int hard = 0; double tone = 6000, mix = 0.5; int oversample = 0; };
    void prepare(double sr) { sr_ = sr; reset(); }
    void reset() { os_.reset(); toneY_ = 0; dcx_ = dcy_ = 0; dcR_ = 1 - 2 * 3.141592653589793 * 10 / sr_; }
    void process(const float* in, float* out, int n, const Params& p) {
        drive_ = p.drive; hard_ = (int)jsRound(p.hard);
        const int idx = (int)jsRound(p.oversample); const int factor = idx == 1 ? 2 : idx == 2 ? 4 : 1;
        Shape sh{this};
        for (int i = 0; i < n; i++) {
            const double x = in ? in[i] : 0.0;
            double shaped = os_.run(x, factor, sr_, sh);
            if (hard_ == 3) { const double d = shaped - dcx_ + dcR_ * dcy_; dcx_ = shaped; dcy_ = d; shaped = d; }   // tube mode is asymmetric: strip the DC it creates
            double a = p.tone * 6.0 / sr_; a = a < 0 ? 0 : (a > 0.999 ? 0.999 : a);
            toneY_ = toneY_ + a * (shaped - toneY_);
            out[i] = (float)(x * (1 - p.mix) + toneY_ * p.mix);
        }
    }
private:
    struct Shape {
        Saturation* s;
        inline double operator()(double x) const {
            const double d = s->drive_;
            return s->hard_ == 3 ? rotamp::tube(x * (1 + d * 7), 0.12 + 0.2 * d) * 0.8 : s->hard_ == 1 ? std::max(-1.0, std::min(1.0, x * (1 + d * 8))) : s->hard_ == 2 ? std::tanh(x * (1 + d * 2.2)) : std::tanh(x * (1 + d * 6));   // 3 = tube overdrive
        }
    };
    double sr_ = 48000, drive_ = 0.4, toneY_ = 0, dcx_ = 0, dcy_ = 0, dcR_ = 0.999; int hard_ = 0; OversampledStage<Shape> os_;
};

class Wavefolder {
public:
    struct Params { double drive = 3, symmetry = 0, mix = 1; int oversample = 0; int mode = 0; double pwm = 0.5; };
    void prepare(double sr) { sr_ = sr; reset(); }
    void reset() { os_.reset(); dcX_ = dcY_ = 0; }
    void process(const float* in, float* out, int n, const Params& p) {
        drive_ = p.drive; symmetry_ = p.symmetry; mode_ = (int)jsRound(p.mode); pwm_ = p.pwm;
        const int idx = (int)jsRound(p.oversample); const int factor = idx == 1 ? 2 : idx == 2 ? 4 : 1;
        const bool dcBlock = mode_ == 1 || mode_ == 3;
        Shape sh{this};
        for (int i = 0; i < n; i++) {
            const double x = in ? in[i] : 0.0;
            double f = os_.run(x, factor, sr_, sh);
            if (dcBlock) { const double y = f - dcX_ + 0.9985 * dcY_; dcX_ = f; dcY_ = y; f = y; }
            out[i] = (float)(x * (1 - p.mix) + f * p.mix);
        }
    }
private:
    double fold(double x) const {
        if (mode_ == 2) return std::sin(x * 1.5707963);
        if (mode_ == 1) x = (x < 0 ? -x : x) * 2 - 1;
        double y = x; int it = 0;
        while ((y > 1 || y < -1) && it < 32) { if (y > 1) y = 2 - y; else if (y < -1) y = -2 - y; it++; }
        if (mode_ == 3) return y > (0.5 - pwm_) * 2 ? 0.9 : -0.9;
        return y;
    }
    struct Shape { Wavefolder* w; inline double operator()(double x) const { return w->fold(x * w->drive_ + w->symmetry_); } };
    double sr_ = 48000, drive_ = 3, symmetry_ = 0, pwm_ = 0.5, dcX_ = 0, dcY_ = 0; int mode_ = 0; OversampledStage<Shape> os_;
};

// ---------------------------------------------------------------------------------------------------------- Tape (wow / flutter / dust / crackle)
// The web version uses Math.random(); here the generator is a small xorshift-free LCG you can seed (tests seed it to match the JS harness).
struct LcgRandom {
    uint32_t s = 1;
    inline double next() { s = s * 1664525u + 1013904223u; return (double)s / 4294967296.0; }
};
class Tape {
public:
    struct Params { double wowRate = 0.7, wowDepth = 0.3, flutterRate = 8, flutterDepth = 0.2, dust = 0.2, crackle = 0.2, mix = 1; };
    void prepare(double sr, uint32_t seed = 12345) {
        sr_ = sr; size_ = (int)std::ceil(sr * 0.06); buf_.assign(size_, 0.f); wpos_ = 0;
        rng_.s = seed; wow_ = rng_.next(); flut_ = rng_.next();
    }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.f); wpos_ = 0; }
    LcgRandom& rng() { return rng_; }
    void process(const float* in, float* outL, float* outR, int n, const Params& p) {
        if (!in) return;
        for (int i = 0; i < n; i++) {
            buf_[wpos_] = in[i];
            wow_ += p.wowRate / sr_; flut_ += p.flutterRate / sr_;
            const double wowMod = std::sin(2 * 3.141592653589793 * wow_) * p.wowDepth * 8;
            const double flutMod = std::sin(2 * 3.141592653589793 * flut_) * p.flutterDepth * 1.5;
            const double delayMs = 15 + wowMod + flutMod;
            const double ds = delayMs / 1000 * sr_;
            double rp = wpos_ - ds;
            while (rp < 0) rp += size_;
            const double fl = std::floor(rp);
            const int i0 = (int)fl % size_, i1 = (i0 + 1) % size_;
            const double frac = rp - fl;
            const double sample = buf_[i0] * (1 - frac) + buf_[i1] * frac;
            double noiseAdd = 0;
            if (rng_.next() < p.dust * 0.002) noiseAdd += (rng_.next() * 2 - 1) * 0.5;
            if (rng_.next() < p.crackle * 0.02) noiseAdd += (rng_.next() * 2 - 1) * 0.15;
            const double wet = sample + noiseAdd;
            const double o = (double)in[i] * (1 - p.mix) + wet * p.mix;
            outL[i] = (float)o; outR[i] = (float)o;
            wpos_ = (wpos_ + 1) % size_;
        }
    }
private:
    double sr_ = 48000, wow_ = 0, flut_ = 0; int size_ = 1, wpos_ = 0; std::vector<float> buf_; LcgRandom rng_;
};

} // namespace vv
