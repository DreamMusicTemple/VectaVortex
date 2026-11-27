// Ports of the oscillator / filter / glitch worklets: Wavetable, WavetableSync, MultiFilter, FM operator engine, Glitch.
// These are the building blocks the native voice engine will use. Mirrors full-app.html line for line.
#pragma once
#include <memory>
#include "dsp_common.hpp"
#include "fx_basic.hpp"

namespace vv {

inline double clampNum(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }

// ---------------------------------------------------------------------------------------------------------- Wavetable
// An immutable multi-frame table. Build it on a non-audio thread, then hand the audio thread a std::shared_ptr / raw pointer to it.
struct WtTable {
    int tableSize = 0;
    std::vector<std::vector<float>> frames;
    int frameCount() const { return (int)frames.size(); }
};

namespace wt {
inline double warpPhase(double phase, double pw) {
    if (std::fabs(pw - 0.5) < 0.001) return phase;
    return phase < pw ? (phase / pw) * 0.5 : 0.5 + ((phase - pw) / (1 - pw)) * 0.5;
}
inline double pingPongFold(double x) { double m = std::fmod(std::fmod(x, 2.0) + 2.0, 2.0); return m <= 1 ? m : 2 - m; }
// One table read, identical for the single and the dual (sync) processors.
inline double read(const WtTable* t, double phase, double pos, double pw, double phOff, double se, double seg, bool pingPong) {
    if (!t) return 0.0;
    const int nF = t->frameCount(); if (!nF || !t->tableSize) return 0.0;
    pos = pingPong ? pingPongFold(pos) : std::max(0.0, std::min(1.0, pos));
    const int effNF = (int)std::max(1.0, jsRound(nF * std::max(0.05, std::min(1.0, seg))));
    const double ps = pos * (effNF - 1); const double fl = std::floor(ps), ff = ps - fl; const int fi = (int)fl;
    const std::vector<float>& A = t->frames[fi]; const std::vector<float>& B = t->frames[std::min(fi + 1, effNF - 1)];
    const double shifted = std::fmod(phase + phOff, 1.0);
    const double warped = warpPhase(shifted, pw);
    const double rp = warped * se * t->tableSize; const double rf = std::floor(rp);
    const int ts = t->tableSize; const int i0 = (int)rf % ts, i1 = (i0 + 1) % ts; const double fr = rp - rf;
    const double sA = A[i0] + ((double)A[i1] - A[i0]) * fr, sB = B[i0] + ((double)B[i1] - B[i0]) * fr;
    return sA + (sB - sA) * ff;
}
} // namespace wt

class Wavetable {
public:
    void prepare(double sr) { sr_ = sr; phase_ = 0; }
    void reset() { phase_ = 0; }
    void setTable(const WtTable* t) { table_ = t; }
    void setPhase(double p) { phase_ = p; }
    double phase() const { return phase_; }
    // one sample. pos is the unclamped scan position (an LFO may push it outside 0..1: clamped, or folded when pingPong).
    inline double tick(double freq, double pos, double pw, double phOff, double se, double seg, bool pingPong) {
        if (!table_ || !table_->frameCount()) return 0.0;
        const double o = wt::read(table_, phase_, pos, pw, phOff, se, seg, pingPong);
        phase_ += freq / sr_; if (phase_ >= 1) phase_ -= 1;
        return o;
    }
private:
    double sr_ = 48000, phase_ = 0; const WtTable* table_ = nullptr;
};

class WavetableSync {
public:
    struct Osc { double freq = 440, pos = 0, pw = 0.5, phOff = 0, se = 1, seg = 1; bool pingPong = false; };
    void prepare(double sr) { sr_ = sr; reset(); }
    void reset() { ph0_ = ph1_ = 0; }
    void setTables(const WtTable* t0, const WtTable* t1) { t_[0] = t0; t_[1] = t1; }
    // writes the master (osc 0) and the hard-synced slave (osc 1)
    inline void tick(const Osc& a, const Osc& b, double& out0, double& out1) {
        out0 = wt::read(t_[0], ph0_, a.pos, a.pw, a.phOff, a.se, a.seg, a.pingPong);
        out1 = wt::read(t_[1], ph1_, b.pos, b.pw, b.phOff, b.se, b.seg, b.pingPong);
        ph0_ += a.freq / sr_;
        bool wrapped = false; if (ph0_ >= 1) { ph0_ -= 1; wrapped = true; }
        ph1_ += b.freq / sr_;
        if (wrapped) ph1_ = 0; else if (ph1_ >= 1) ph1_ -= 1;
    }
private:
    double sr_ = 48000, ph0_ = 0, ph1_ = 0; const WtTable* t_[2] = {nullptr, nullptr};
};

// ---------------------------------------------------------------------------------------------------------- MultiFilter
// types: 0 Moog ladder, 1 Diode ladder, 2 one-pole LP, 3 two-pole LP, 4 SVF LP, 5 comb, 6 SVF HP, 7 SVF BP, 8 formant, 9 notch, 10 iSEM
class MultiFilter {
public:
    // prepare(): self-contained (allocates its own 8192-sample comb line). prepareShared()+attachComb(): no allocation;
    // the engine hands out comb lines from a fixed pool only to voices that actually use the comb filter.
    void prepare(double sr) { sr_ = sr; own_.assign(8192, 0.f); comb_ = own_.data(); combLen_ = 8192; reset(); }
    void prepareShared(double sr) { sr_ = sr; comb_ = nullptr; combLen_ = 0; reset(); }
    void attachComb(float* buf, int len) { comb_ = buf; combLen_ = len; if (comb_) std::fill(comb_, comb_ + combLen_, 0.f); combPos_ = 0; }
    float* detachComb() { float* b = comb_; comb_ = nullptr; combLen_ = 0; return b; }
    bool hasComb() const { return comb_ != nullptr; }
    void reset() {
        y1_ = y2_ = y3_ = y4_ = 0; svfLow_ = svfBand_ = 0; if (comb_) std::fill(comb_, comb_ + combLen_, 0.f); combPos_ = 0;
        for (int k = 0; k < 3; k++) fLow_[k] = fBand_[k] = 0;
    }
    inline double tick(double x, double cutoff, double reso, int type) {
        const double fc = std::max(20.0, std::min(18000.0, cutoff)) / sr_;
        double y = 0;
        if (type == 0 || type == 1) {
            const double p = clampNum(fc * (1.8 - 0.8 * fc), 0, 1);
            const double k = clampNum(reso / 100, 0, 1.3) * 4.0;
            const double fbSrc = type == 0 ? y4_ : (y2_ * 0.5 + y4_ * 0.5);
            const double xin = x - k * fbSrc;
            y1_ = y1_ + p * (std::tanh(xin) - std::tanh(y1_));
            y2_ = y2_ + p * (std::tanh(y1_) - std::tanh(y2_));
            y3_ = y3_ + p * (std::tanh(y2_) - std::tanh(y3_));
            y4_ = y4_ + p * (std::tanh(y3_) - std::tanh(y4_));
            y = y4_;
        } else if (type == 2) {
            const double p = clampNum(fc * 6.0, 0, 0.999);
            y1_ = y1_ + p * (x - y1_);
            y = y1_;
        } else if (type == 3) {
            const double p = clampNum(fc * 6.0, 0, 0.999);
            const double k = clampNum(reso / 130, 0, 0.999);
            const double xin = x - k * y2_;
            y1_ = y1_ + p * (xin - y1_);
            y2_ = y2_ + p * (y1_ - y2_);
            y = y2_;
        } else if (type == 4 || type == 6 || type == 7) {
            const double f = clampNum(2 * std::sin(3.141592653589793 * fc), 0, 1.2);
            const double q = clampNum(1 - (reso / 130) * 0.99, 0.01, 1);
            const double high = x - svfLow_ - q * svfBand_;
            svfBand_ += f * high;
            svfLow_ += f * svfBand_;
            y = type == 4 ? svfLow_ : (type == 6 ? high : svfBand_);
        } else if (type == 5) {
            const int len = combLen_;
            if (!comb_ || len < 4) return 0.0;
            const double ds = std::max(2.0, std::min((double)(len - 1), jsRound(sr_ / std::max(20.0, cutoff))));
            const int readPos = ((combPos_ - (int)ds) % len + len) % len;
            const double delayed = comb_[readPos];
            const double fb = clampNum(reso / 135, 0, 0.98);
            const double val = x + delayed * fb;
            comb_[combPos_] = (float)val;
            combPos_ = (combPos_ + 1) % len;
            y = delayed;
        } else if (type == 9) {   // notch: SVF low + high taps
            const double f = clampNum(2 * std::sin(3.141592653589793 * fc), 0, 1.2);
            const double q = clampNum(1 - (reso / 130) * 0.99, 0.01, 1);
            const double high = x - svfLow_ - q * svfBand_;
            svfBand_ += f * high;
            svfLow_ += f * svfBand_;
            y = svfLow_ + high;
        } else if (type == 10) {  // iSEM: Oberheim-style 12 dB SVF, soft-saturated integrators
            const double f = clampNum(2 * std::sin(3.141592653589793 * fc), 0, 1.2);
            const double q = clampNum(1.05 - (reso / 130) * 1.0, 0.03, 1);
            const double high = x - svfLow_ - q * std::tanh(svfBand_);
            svfBand_ = svfBand_ + f * std::tanh(high);
            svfLow_ = svfLow_ + f * svfBand_;
            y = svfLow_ * 0.85 + (svfLow_ + high) * 0.15;
        } else if (type == 11) {  // SID 6581-style lowpass: OTA-flavoured soft asymmetric saturation in the resonant feedback path + bass loss as resonance rises
            const double f = clampNum(2 * std::sin(3.141592653589793 * fc), 0, 1.2);
            const double q = clampNum(1.05 - (reso / 130) * 0.97, 0.03, 1.1);
            const double drive = 1.0 + (reso / 130) * 2.2;
            const double high = x - svfLow_ - q * svfBand_;
            const double sat = std::tanh(high * drive) / drive + 0.02 * std::tanh(high * drive * 2.3);
            svfBand_ += f * sat;
            svfLow_ += f * svfBand_;
            const double bassLoss = clampNum((reso / 130) * 0.35, 0, 0.35);
            y = svfLow_ * (1 - bassLoss) + high * bassLoss * 0.3;
        } else if (type == 12) {  // SID 8580-style lowpass: the same resonant 2-pole core without the saturation or bass loss
            const double f = clampNum(2 * std::sin(3.141592653589793 * fc), 0, 1.2);
            const double q = clampNum(1.02 - (reso / 130) * 1.0, 0.02, 1);
            const double high = x - svfLow_ - q * svfBand_;
            svfBand_ += f * high;
            svfLow_ += f * svfBand_;
            y = svfLow_;
        } else if (type == 8) {
            const double ratios[3] = {1, fr2, fr3};
            const double q = clampNum(1 - (reso / 130) * 0.97, 0.02, 1);
            double sum = 0;
            for (int k = 0; k < 3; k++) {
                const double peak = std::max(80.0, std::min(sr_ * 0.45, cutoff * ratios[k]));
                const double fk = clampNum(2 * std::sin(3.141592653589793 * peak / sr_), 0, 1.2);
                const double high = x - fLow_[k] - q * fBand_[k];
                fBand_[k] += fk * high;
                fLow_[k] += fk * fBand_[k];
                sum += fBand_[k] * (k == 0 ? 1.0 : 0.7);
            }
            y = sum * 0.6;
        }
        return y;
    }
    double fr2 = 1.74, fr3 = 3.71;   // formant F2/F1 and F3/F1 ratios (formantR2 / formantR3)
    // block helper (k-rate settings, like the worklet with constant AudioParams)
    void process(const float* in, float* out, int n, double cutoff, double reso, int type) {
        for (int i = 0; i < n; i++) out[i] = (float)tick(in ? in[i] : 0.0, cutoff, reso, type);
    }
private:
    double sr_ = 48000, y1_ = 0, y2_ = 0, y3_ = 0, y4_ = 0, svfLow_ = 0, svfBand_ = 0, fLow_[3] = {0, 0, 0}, fBand_[3] = {0, 0, 0};
    std::vector<float> own_; float* comb_ = nullptr; int combLen_ = 0; int combPos_ = 0;
};

// ---------------------------------------------------------------------------------------------------------- FM (4 operators, 6 algorithms)
class FMVoice {
public:
    void prepare(double sr) { sr_ = sr; reset(); }
    void reset() { for (int i = 0; i < 4; i++) { phase_[i] = 0; last_[i] = 0; } }
    // wave: 0 sine 1 triangle 2 square. Same maths as FMVoiceEngine.processSample.
    double tick(double baseFreq, const double ratios[4], const double levels[4], const int waves[4], double feedback0, const double pitchCents[4], int algoIdx) {
        static const struct Algo { int nMods; int mods[3][2]; int nCar; int car[4]; } algos[6] = {
            {3, {{0, 1}, {1, 2}, {2, 3}}, 1, {0, 0, 0, 0}},
            {2, {{0, 1}, {2, 3}, {0, 0}}, 2, {0, 2, 0, 0}},
            {3, {{0, 1}, {0, 2}, {0, 3}}, 1, {0, 0, 0, 0}},
            {3, {{0, 1}, {1, 2}, {1, 3}}, 1, {0, 0, 0, 0}},
            {2, {{0, 2}, {1, 3}, {0, 0}}, 2, {0, 1, 0, 0}},
            {0, {{0, 0}, {0, 0}, {0, 0}}, 4, {0, 1, 2, 3}}};
        const Algo& a = (algoIdx >= 0 && algoIdx < 6) ? algos[algoIdx] : algos[2];
        double modHz[4] = {0, 0, 0, 0}, outs[4] = {0, 0, 0, 0};
        for (int i = 3; i >= 0; i--) {
            const double freqI = baseFreq * ratios[i] * std::pow(2.0, pitchCents[i] / 1200);
            double selfFb = 0;
            if (i == 0 && feedback0 > 0) selfFb = feedback0 * last_[0] * freqI * 2;
            const double o = op(i, freqI + modHz[i] + selfFb, waves[i]);
            outs[i] = o;
            for (int m = 0; m < a.nMods; m++) if (a.mods[m][1] == i) modHz[a.mods[m][0]] += o * levels[i] * freqI * 2;
        }
        double mixOut = 0;
        for (int c = 0; c < a.nCar; c++) mixOut += outs[a.car[c]] * levels[a.car[c]];
        return mixOut / std::max(1, a.nCar);
    }
private:
    double op(int i, double freqHz, int wave) {
        const double p = std::fmod(std::fmod(phase_[i], 1.0) + 1.0, 1.0);
        double out;
        if (wave == 1) out = p < 0.5 ? (4 * p - 1) : (3 - 4 * p);
        else if (wave == 2) out = p < 0.5 ? 1 : -1;
        else out = std::sin(2 * 3.141592653589793 * p);
        last_[i] = out;
        phase_[i] += freqHz / sr_;
        if (phase_[i] >= 1) phase_[i] -= 1; else if (phase_[i] < 0) phase_[i] += 1;
        return out;
    }
    double sr_ = 48000, phase_[4] = {0, 0, 0, 0}, last_[4] = {0, 0, 0, 0};
};

// ---------------------------------------------------------------------------------------------------------- Glitch (bitcrush, sample-rate reduction, stutter, reverse / gate / pitch jump / tape stop)
class Glitch {
public:
    struct Params { int bitDepth = 16; double srReduction = 1; double companding = 0; bool stutterOn = false; int stutterInterval = 11025; double stutterAmount = 0.5; int modeA = 0, modeB = 0, modeC = 0; };
    static constexpr int kMaxStutter = 200000;
    void prepare(double sr, uint32_t seed = 1) {
        sr_ = sr; ringLen_ = (int)jsRound(sr * 1.0); ring_.assign(ringLen_, 0.f); stutter_.assign(kMaxStutter, 0.f);
        rng_.s = seed; reset();
    }
    void reset() {
        holdValue_ = 0; holdCounter_ = 0; stutterLen_ = 0; stutterPos_ = 0; stutterCapturing_ = true;
        std::fill(ring_.begin(), ring_.end(), 0.f); ringWrite_ = 0; readPos_ = 0; prev_[0] = prev_[1] = prev_[2] = 0; age_ = 0;
    }
    LcgRandom& rng() { return rng_; }
    void process(const float* in, float* out, int n, const Params& p) {
        const int bitDepth = p.bitDepth;
        const int srRed = (int)std::max(1.0, jsRound(p.srReduction));
        const int interval = std::max(1, std::min(kMaxStutter, p.stutterInterval));
        const int modeA = p.modeA, modeB = p.modeB, modeC = p.modeC;
        if (modeA != prev_[0] || modeB != prev_[1] || modeC != prev_[2]) {
            age_ = 0;
            const int lookback = (int)jsRound(sr_ * 0.15);
            readPos_ = (double)((ringWrite_ - lookback + ringLen_ * 10) % ringLen_);
            prev_[0] = modeA; prev_[1] = modeB; prev_[2] = modeC;
        }
        const bool forceStutter = modeA == 1 || modeB == 1 || modeC == 1, forceBit = modeA == 2 || modeB == 2 || modeC == 2;
        const int effBits = forceBit ? std::min(bitDepth, 4) : bitDepth;
        const double effLevels = std::pow(2.0, effBits);
        const bool hasRev = modeA == 3 || modeB == 3 || modeC == 3, hasGate = modeA == 4 || modeB == 4 || modeC == 4,
                   hasPitch = modeA == 5 || modeB == 5 || modeC == 5, hasStop = modeA == 6 || modeB == 6 || modeC == 6;
        for (int i = 0; i < n; i++) {
            double x = in ? in[i] : 0.0;
            if (p.stutterOn || forceStutter) {
                if (stutterLen_ != interval) { stutterLen_ = interval; std::fill(stutter_.begin(), stutter_.begin() + interval, 0.f); stutterPos_ = 0; stutterCapturing_ = true; }
                if (stutterCapturing_) {
                    stutter_[stutterPos_] = (float)x; stutterPos_++;
                    if (stutterPos_ >= interval) { stutterPos_ = 0; stutterCapturing_ = rng_.next() > p.stutterAmount; }
                } else {
                    x = stutter_[stutterPos_]; stutterPos_++;
                    if (stutterPos_ >= interval) { stutterPos_ = 0; stutterCapturing_ = rng_.next() > p.stutterAmount; }
                }
            }
            if (srRed > 1) { if (holdCounter_ % srRed == 0) holdValue_ = x; holdCounter_++; x = holdValue_; }
            if (effBits < 16) {
                const double linear = jsRound(x * effLevels) / effLevels;
                if (p.companding > 0) {   // mu-law blend, identical to the web worklet
                    const double c = p.companding, mu = 1 + c * 254;
                    const double sg = x < 0 ? -1 : 1, ax = std::min(1.0, std::fabs(x));
                    const double comp = sg * std::log(1 + mu * ax) / std::log(1 + mu);
                    const double qc = jsRound(comp * effLevels / 2) / (effLevels / 2);
                    const double sg2 = qc < 0 ? -1 : 1, aq = std::fabs(qc);
                    const double expd = sg2 * (std::pow(1 + mu, aq) - 1) / mu;
                    x = linear * (1 - c) + expd * c;
                } else x = linear;
            }
            ring_[ringWrite_] = (float)x; ringWrite_ = (ringWrite_ + 1) % ringLen_;
            if (hasRev || hasPitch || hasStop) {
                double rate = 1;
                if (hasRev) rate = -1;
                if (hasPitch) rate = hasRev ? -2 : 2;
                if (hasStop) { const double slow = std::min(1.0, (double)age_ / (sr_ * 0.35)); rate = (hasRev ? -1 : 1) * (1 - slow); }
                readPos_ += rate;
                const double rl = ringLen_;
                readPos_ = std::fmod(std::fmod(readPos_, rl) + rl, rl);
                const int i0 = (int)std::floor(readPos_), i1 = (i0 + 1) % ringLen_; const double frac = readPos_ - i0;
                x = ring_[i0] * (1 - frac) + ring_[i1] * frac;
            }
            if (hasGate) x = 0;
            out[i] = (float)x;
            age_++;
        }
    }
private:
    double sr_ = 48000, holdValue_ = 0, readPos_ = 0; int64_t holdCounter_ = 0, age_ = 0;
    int ringLen_ = 1, ringWrite_ = 0, stutterLen_ = 0, stutterPos_ = 0, prev_[3] = {0, 0, 0}; bool stutterCapturing_ = true;
    std::vector<float> ring_, stutter_; LcgRandom rng_;
};

} // namespace vv
