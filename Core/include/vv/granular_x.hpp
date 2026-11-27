// Extended Granular FX engine (native port of the web app's granular-processor extension, "xProcess").
// It runs next to the original Granular class: the original path is unchanged and is used whenever the effect still has
// its original controls; this engine takes over (adds its wet signal) as soon as a mode or any extra control is used.
// All memory is sized in prepare(); process() never allocates or locks.
#pragma once
#include <array>
#include <atomic>
#include "dsp_common.hpp"

namespace vv {

class GranularX {
public:
    static constexpr int kMaxG = 96, kLut = 1024, kViz = 24;
    struct P {
        int mode = 0, window = -1, rndType = 0, sync = 0, delaySync = 0, seed = 0; bool freeze = false, autoGain = false;
        double delay = 0, posRand = 1, sizeRand = 0, jitter = 0, fine = 0, pitchRand = 0, reverse = 0, volRand = 0, width = 1, drift = 0, lrSpread = 0, outLevel = 0,
               curve = 0.5, symmetry = 0.5, atk = 0, dec = 0, sus = 1, rel = 0, fb = 0, fbDamp = 0, fbLow = 0, fbSat = 0, fbDiff = 0, pingPong = 0, rndDepth = 1, rndRate = 2;
        // true when every control sits at its neutral value (the effect then behaves exactly like the original granular FX)
        bool isDefault() const { const P d; return mode == d.mode && window == d.window && rndType == d.rndType && sync == d.sync && delaySync == d.delaySync && seed == d.seed && freeze == d.freeze && autoGain == d.autoGain
            && delay == d.delay && posRand == d.posRand && sizeRand == d.sizeRand && jitter == d.jitter && fine == d.fine && pitchRand == d.pitchRand && reverse == d.reverse && volRand == d.volRand && width == d.width && drift == d.drift
            && lrSpread == d.lrSpread && outLevel == d.outLevel && curve == d.curve && symmetry == d.symmetry && atk == d.atk && dec == d.dec && sus == d.sus && rel == d.rel && fb == d.fb && fbDamp == d.fbDamp && fbLow == d.fbLow
            && fbSat == d.fbSat && fbDiff == d.fbDiff && pingPong == d.pingPong && rndDepth == d.rndDepth && rndRate == d.rndRate; }
    };
    struct Base { double grainSize = 80, density = 20, pitch = 0, spread = 0.5, panCenter = 0, followerAmount = 0, mix = 1; int legacyShape = 0; double bpm = 120; };

    void prepare(double sr, uint32_t baseSeed) {
        sr_ = sr; size_ = 1; while (size_ < sr * 3) size_ *= 2; mask_ = size_ - 1;
        L_.assign(size_, 0.f); R_.assign(size_, 0.f);
        baseSeed_ = baseSeed;
        const int lens[4] = { (int)std::max(8.0, jsr(sr * 0.0030)), (int)std::max(8.0, jsr(sr * 0.0022)), (int)std::max(8.0, jsr(sr * 0.0033)), (int)std::max(8.0, jsr(sr * 0.0024)) };
        for (int k = 0; k < 4; k++) { apLen_[k] = lens[k]; apBuf_[k].assign(lens[k], 0.f); }
        reset(); lastSeed_ = -1; lutKey_ = -1; set(P());
    }
    void reset() {
        std::fill(L_.begin(), L_.end(), 0.f); std::fill(R_.begin(), R_.end(), 0.f); w_ = 0; frozenAt_ = 0; frozen_ = false; count_ = 0; clock_ = 0; jit_ = 1; interval_ = -1; smoothCount_ = 0; t_ = 0;
        env_ = 0; fbL_ = fbR_ = 0; lpL_ = lpR_ = hpL_ = hpR_ = 0; fbHist_ = 0; stutStart_ = 0; stutN_ = 0; scrIdx_ = 8; trigN_ = 0; vizCount_ = 0;
        for (int k = 0; k < 8; k++) { rv_[k] = rprev_[k] = rnext_[k] = rtime_[k] = pk0_[k] = pk1_[k] = pk2_[k] = 0; }
        for (int k = 0; k < 4; k++) { std::fill(apBuf_[k].begin(), apBuf_[k].end(), 0.f); apPos_[k] = 0; }
        rs_ = (uint32_t)((uint64_t)baseSeed_ * 2654435761ull + 12345ull);
    }
    void set(const P& p) {
        p_ = p; ext_ = !p.isDefault();
        if (p.seed != lastSeed_) { lastSeed_ = p.seed; rs_ = (uint32_t)(((uint64_t)baseSeed_ + (uint64_t)p.seed * 7919ull) * 2654435761ull + 12345ull); }
        const double key = p.window * 1e9 + p.curve * 1e6 + p.symmetry * 1e4 + p.atk * 1e3 + p.dec * 1e2 + p.sus * 10 + p.rel + legacyShape_ * 0.123;
        if (key != lutKey_) { lutKey_ = key; buildLut(); }
    }
    void setLegacyShape(int s) { if (s != legacyShape_) { legacyShape_ = s; lutKey_ = -1; if (p_.window < 0) { lutKey_ = 0.5; buildLut(); } } }
    bool extOn() const { return ext_; }
    bool busy() const { return ext_ || count_ > 0 || fbL_ != 0 || fbR_ != 0; }
    double outGain() const { return p_.outLevel == 0 ? 1.0 : std::pow(10.0, p_.outLevel / 20); }

    // Writes only the extended wet signal (not mixed with the dry); the caller adds it to the original granular output.
    void process(const float* in, float* wetL, float* wetR, int n, const Base& b) {
        const double sr = sr_; const int md = p_.mode;
        const bool freezeOn = p_.freeze || md == 2;
        if (freezeOn && !frozen_) { frozen_ = true; frozenAt_ = w_; } else if (!freezeOn && frozen_) frozen_ = false;
        const int grainSamples = (int)std::max(1.0, jsr(b.grainSize / 1000 * sr));
        double baseDens = b.density; double perBeat = kPerBeat[std::max(0, std::min(9, p_.sync))];
        if (md == 11 && perBeat == 0) perBeat = 4;
        if (md == 4) baseDens *= 3; else if (md == 6) baseDens *= 2; else if (md == 12) baseDens *= 0.7;
        double fb = p_.fb, fbDiff = p_.fbDiff, damp = p_.fbDamp;
        if (md == 12) { fb = std::max(fb, 0.3); if (damp < 0.1) damp = 0.4; }
        const double fbLp = 20000 * std::pow(0.015, damp), fbHz = 20 * std::pow(100.0, p_.fbLow);
        if (md == 4) fbDiff = std::max(fbDiff, 0.6);
        const double lpA = 1 - std::exp(-kTwoPi * std::min(fbLp, sr * 0.45) / sr), hpA = 1 - std::exp(-kTwoPi * fbHz / sr);
        const double sdrive = 1 + p_.fbSat * 6, smk = 1 / std::sqrt(sdrive), apg = 0.6 * fbDiff, pp = std::min(1.0, p_.pingPong);
        const double wd = p_.width; const bool ag = p_.autoGain;
        for (int i = 0; i < n; i++) {
            const double x = in[i];
            double fL = fbL_, fR = fbR_;
            if (!frozen_) { L_[w_] = (float)(x + fL); R_[w_] = (float)(x + fR); }
            env_ += (std::fabs(x) - env_) * 0.002;
            if (fb > 0 || fL != 0 || fR != 0) fbHist_ = size_; else if (fbHist_ > 0) fbHist_--;
            const bool same = fbHist_ <= 0;
            double spg;
            if (perBeat > 0) spg = sr / std::max(0.05, b.bpm / 60 * perBeat); else spg = sr / std::max(0.1, baseDens * (1 + b.followerAmount * env_ * 4));
            if (interval_ > 0) spg = interval_;
            clock_ += 1;
            if (clock_ >= spg * jit_ && count_ < kMaxG) {
                clock_ -= spg * jit_; if (clock_ > spg) clock_ = 0;
                int gs = grainSamples; if (md == 11) gs = (int)std::max(64.0, jsr(spg * 0.6));
                trigger(gs, b.spread, b.panCenter, b.pitch, b);
            }
            double sL = 0, sR = 0;
            for (int g = count_ - 1; g >= 0; g--) {
                const int age = gAge_[g];
                if (age >= gDur_[g]) { const int l = count_ - 1; if (g != l) copyGrain(g, l); count_--; continue; }
                const double pos = gPos_[g]; double aL, aR; const int aa = gAA_[g];
                if (aa <= 1) { aL = herm(L_, pos); aR = (same && gOff_[g] == 0) ? aL : herm(R_, pos + gOff_[g]); }
                else { aL = 0; aR = 0; const double sp = std::fabs(gStep_[g]) / aa; const bool sm = same && gOff_[g] == 0;
                    for (int q = 0; q < aa; q++) { const double o = (q - (aa - 1) * 0.5) * sp; aL += herm(L_, pos + o); if (!sm) aR += herm(R_, pos + gOff_[g] + o); }
                    aL /= aa; aR = sm ? aL : aR / aa; }
                const double ep = (double)age / gDur_[g] * kLut; const int ei = (int)ep; const double ef = ep - ei; const double env = lut_[ei] + (lut_[ei + 1] - lut_[ei]) * ef;
                if (gSpec_[g]) { const int bb = g * 4; const double f = gF_[g], q = 0.45;
                    double lo = gS_[bb], bp = gS_[bb + 1]; lo += f * bp; double hi = aL - lo - q * bp; bp += f * hi; gS_[bb] = lo; gS_[bb + 1] = bp; aL = bp * 1.6;
                    double lo2 = gS_[bb + 2], bp2 = gS_[bb + 3]; lo2 += f * bp2; double hi2 = aR - lo2 - q * bp2; bp2 += f * hi2; gS_[bb + 2] = lo2; gS_[bb + 3] = bp2; aR = bp2 * 1.6; }
                sL += aL * env * gGL_[g]; sR += aR * env * gGR_[g];
                gPos_[g] = pos + gStep_[g]; gAge_[g] = age + 1;
            }
            const int nAct = count_; smoothCount_ += (nAct - smoothCount_) * 0.0005;
            if (ag) { const double gg = 1 / std::sqrt(std::max(1.0, smoothCount_ * 0.5)); sL *= gg; sR *= gg; }
            if (wd != 1) { const double m = 0.5 * (sL + sR), s2 = 0.5 * (sL - sR) * wd; sL = m + s2; sR = m - s2; }
            if (sL > 1 || sL < -1) sL = sL > 0 ? 1 + std::tanh(sL - 1) : -1 - std::tanh(-sL - 1);
            if (sR > 1 || sR < -1) sR = sR > 0 ? 1 + std::tanh(sR - 1) : -1 - std::tanh(-sR - 1);
            if (fb > 0 || fbL_ != 0 || fbR_ != 0) {
                const double nrm = 1 / std::sqrt(std::max(1.0, nAct * 0.5));
                double a = (sL * (1 - pp) + sR * pp) * nrm, bq = (sR * (1 - pp) + sL * pp) * nrm;
                lpL_ += lpA * (a - lpL_); lpR_ += lpA * (bq - lpR_); a = lpL_; bq = lpR_;
                hpL_ += hpA * (a - hpL_); hpR_ += hpA * (bq - hpR_); a -= hpL_; bq -= hpR_;
                if (apg > 0) { a = allpass(1, allpass(0, a, apg), apg); bq = allpass(3, allpass(2, bq, apg), apg); }
                a = std::tanh(a * sdrive) * smk; bq = std::tanh(bq * sdrive) * smk;
                fbL_ = a * fb; fbR_ = bq * fb;
                if (std::fabs(fbL_) < 1e-18) fbL_ = 0; if (std::fabs(fbR_) < 1e-18) fbR_ = 0;
            }
            wetL[i] = (float)sL; wetR[i] = (float)sR;
            if (!frozen_) w_ = (w_ + 1) & mask_;
            t_++;
        }
        vizCount_ += n; if (vizCount_ >= 4096 && ext_) { vizCount_ = 0; publish(); }
    }
    // Keeps the history primed while the extended engine is idle (so switching it on never starts from silence).
    void prime(const float* in, int n) { for (int i = 0; i < n; i++) { L_[w_] = in[i]; R_[w_] = in[i]; w_ = (w_ + 1) & mask_; t_++; } }
    // Display snapshot for the UI: out[0]=active grains shown, out[1]=feedback, out[2]=frozen, out[3]=total, then 4 floats per grain (how far back 0..1, size, pan, position in window). Returns floats written.
    int viz(float* out, int cap) const { const int k = pub_.load(std::memory_order_acquire) & 1; const float* v = vizBuf_[k]; const int n = 4 + (int)v[0] * 4; const int m = std::min(n, cap); for (int i = 0; i < m; i++) out[i] = v[i]; return m; }

private:
    static constexpr double kPerBeat[10] = { 0, 0.25, 0.5, 1, 2, 4, 8, 1.5, 3, 6 };
    static constexpr double kBeats[10] = { 0, 4, 2, 1, 0.5, 0.25, 0.125, 2.0 / 3, 1.0 / 3, 1.0 / 6 };
    static constexpr int kScat[8] = { -12, -7, -5, 0, 0, 5, 7, 12 };
    static double jsr(double v) { return std::floor(v + 0.5); }
    double sr_ = 48000; int size_ = 1; uint32_t mask_ = 0, baseSeed_ = 1, rs_ = 1; P p_; bool ext_ = false; int lastSeed_ = -1; double lutKey_ = -1; int legacyShape_ = 0;
    std::vector<float> L_, R_; uint32_t w_ = 0, frozenAt_ = 0; bool frozen_ = false;
    int count_ = 0; double clock_ = 0, jit_ = 1, interval_ = -1, smoothCount_ = 0, env_ = 0; uint64_t t_ = 0;
    double gPos_[kMaxG], gStep_[kMaxG], gGL_[kMaxG], gGR_[kMaxG], gOff_[kMaxG], gF_[kMaxG], gS_[kMaxG * 4], gBack_[kMaxG]; int gAge_[kMaxG], gDur_[kMaxG], gAA_[kMaxG]; uint8_t gSpec_[kMaxG];
    float lut_[kLut + 2];
    double rv_[8], rprev_[8], rnext_[8], rtime_[8], pk0_[8], pk1_[8], pk2_[8];
    double fbL_ = 0, fbR_ = 0, lpL_ = 0, lpR_ = 0, hpL_ = 0, hpR_ = 0; int fbHist_ = 0;
    std::vector<float> apBuf_[4]; int apLen_[4] = { 8, 8, 8, 8 }, apPos_[4] = { 0, 0, 0, 0 };
    double stutStart_ = 0; int stutN_ = 0, scrIdx_ = 8, scrOrder_[8] = { 0, 1, 2, 3, 4, 5, 6, 7 }; uint64_t trigN_ = 0; int vizCount_ = 0;
    float vizBuf_[2][4 + kViz * 4] = {}; std::atomic<int> pub_{ 0 };

    void copyGrain(int g, int l) { gPos_[g] = gPos_[l]; gStep_[g] = gStep_[l]; gAge_[g] = gAge_[l]; gDur_[g] = gDur_[l]; gGL_[g] = gGL_[l]; gGR_[g] = gGR_[l]; gOff_[g] = gOff_[l]; gAA_[g] = gAA_[l]; gF_[g] = gF_[l]; gSpec_[g] = gSpec_[l]; gBack_[g] = gBack_[l]; for (int q = 0; q < 4; q++) gS_[g * 4 + q] = gS_[l * 4 + q]; }
    void publish() {
        const int k = (pub_.load(std::memory_order_relaxed) + 1) & 1; float* v = vizBuf_[k]; const int m = std::min(kViz, count_);
        v[0] = (float)m; v[1] = (float)std::min(1.0, std::fabs(fbL_) + std::fabs(fbR_)); v[2] = frozen_ ? 1.f : 0.f; v[3] = (float)count_;
        for (int g = 0; g < m; g++) { v[4 + g * 4] = (float)(gBack_[g] / (sr_ * 3)); v[5 + g * 4] = (float)(gDur_[g] / (sr_ * 0.5)); v[6 + g * 4] = (float)(gGR_[g] - gGL_[g]); v[7 + g * 4] = (float)((double)gAge_[g] / gDur_[g]); }
        pub_.store(k, std::memory_order_release);
    }
    void buildLut() {
        const P& p = p_; int w = p.window; if (w < 0) { static const int map[4] = { 8, 4, 3, 5 }; w = map[std::max(0, std::min(3, legacyShape_))]; }
        const double gam = std::pow(2.0, (0.5 - p.symmetry) * 2), kap = std::pow(2.0, (p.curve - 0.5) * 3); const double a = p.atk, d = p.dec, s = p.sus, r = p.rel, TP = kTwoPi, PI = 3.141592653589793;
        for (int i = 0; i <= kLut + 1; i++) {
            const double x0 = std::min(1.0, (double)i / kLut), x = std::pow(x0, gam); double e;
            switch (w) {
                case 0: e = 0.5 - 0.5 * std::cos(TP * x); break;
                case 1: e = 0.54 - 0.46 * std::cos(TP * x); break;
                case 2: e = 0.42 - 0.5 * std::cos(TP * x) + 0.08 * std::cos(2 * TP * x); break;
                case 3: e = std::exp(-0.5 * std::pow((x - 0.5) / 0.17, 2)); break;
                case 4: e = x < 0.5 ? x * 2 : (1 - x) * 2; break;
                case 5: e = 1; break;
                case 6: e = x < 0.25 ? 0.5 - 0.5 * std::cos(PI * x / 0.25) : (x > 0.75 ? 0.5 - 0.5 * std::cos(PI * (1 - x) / 0.25) : 1); break;
                case 8: e = std::sin(PI * x); break;
                default: e = x < 0.08 ? std::sin(0.5 * PI * x / 0.08) : std::exp(-4.6 * (x - 0.08) / 0.92); break;
            }
            e = std::pow(std::max(0.0, e), kap); double g = 1;
            if (a > 0 && x0 < a) g *= 0.5 - 0.5 * std::cos(PI * x0 / a);
            if (d > 0 && x0 >= a && x0 < a + d) g *= 1 - (1 - s) * (x0 - a) / d; else if (x0 >= a + d) g *= s;
            if (r > 0 && x0 > 1 - r) g *= 0.5 - 0.5 * std::cos(PI * (1 - x0) / r);
            double fade = 1; if (x0 < 0.01) fade = x0 / 0.01; else if (x0 > 0.99) fade = (1 - x0) / 0.01;
            lut_[i] = (float)(e * g * std::max(0.0, fade));
        }
    }
    double rand() { rs_ = rs_ * 1664525u + 1013904223u; return (double)rs_ / 4294967296.0; }
    double noise(int ch) {
        const int T = p_.rndType; double v;
        if (T == 0) v = rand() * 2 - 1;
        else if (T == 1) { const double w = rand() * 2 - 1; pk0_[ch] = 0.99765 * pk0_[ch] + w * 0.0990460; pk1_[ch] = 0.963 * pk1_[ch] + w * 0.2965164; pk2_[ch] = 0.57 * pk2_[ch] + w * 1.0526913; v = (pk0_[ch] + pk1_[ch] + pk2_[ch] + w * 0.1848) * 0.30; }
        else if (T == 2) { double s = rv_[ch] + (rand() * 2 - 1) * 0.28; if (s > 1) s = 2 - s; if (s < -1) s = -2 - s; rv_[ch] = s; v = s; }
        else {
            const double per = 1 / std::max(0.05, p_.rndRate), now = (double)t_ / sr_;
            if (T == 3) { if (now >= rtime_[ch]) { rv_[ch] = rand() * 2 - 1; rtime_[ch] = now + per; } v = rv_[ch]; }
            else if (T == 4) { if (now >= rtime_[ch]) { rprev_[ch] = rnext_[ch]; rnext_[ch] = rand() * 2 - 1; rtime_[ch] = now + per; }
                const double f = std::max(0.0, std::min(1.0, 1 - (rtime_[ch] - now) / per)); v = rprev_[ch] + (rnext_[ch] - rprev_[ch]) * (0.5 - 0.5 * std::cos(3.141592653589793 * f)); }
            else { const double a2 = std::min(0.5, 0.005 + 0.01 * p_.rndRate); rv_[ch] += ((rand() * 2 - 1) - rv_[ch]) * a2; v = rv_[ch] / std::sqrt(a2 / (2 - a2)); if (v > 1) v = 1; else if (v < -1) v = -1; }
        }
        return std::max(-1.0, std::min(1.0, v * p_.rndDepth));
    }
    double herm(const std::vector<float>& buf, double pos) const {
        const double fl = std::floor(pos), f = pos - fl; const int64_t ii = (int64_t)fl; const uint32_t i = (uint32_t)ii & mask_;
        const double ym = buf[(i - 1) & mask_], y0 = buf[i], y1 = buf[(i + 1) & mask_], y2 = buf[(i + 2) & mask_];
        const double c0 = y0, c1 = 0.5 * (y1 - ym), c2 = ym - 2.5 * y0 + 2 * y1 - 0.5 * y2, c3 = 0.5 * (y2 - ym) + 1.5 * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }
    double allpass(int k, double x, double g) { std::vector<float>& b = apBuf_[k]; const int n = apLen_[k], i = apPos_[k]; const double d = b[i]; const double v = x + g * d; b[i] = (float)v; apPos_[k] = i + 1 >= n ? 0 : i + 1; return d - g * v; }
    void trigger(int grainSamples, double spread, double panCenter, double rate0, const Base& base) {
        const P& p = p_; const double sr = sr_; const int k = count_, md = p.mode;
        double size = grainSamples, pitch = rate0 + p.fine / 100, pitchR = p.pitchRand, posR = p.posRand, delay = p.delay / 1000 * sr, rev = p.reverse, vRand = p.volRand, spr = spread, gain = 1; int spec = 0;
        if (p.delaySync > 0) delay = kBeats[p.delaySync] * 60 / base.bpm * sr;
        const double rP = noise(0), rS = noise(1), rT = noise(2), rPan = noise(3), rV = noise(4), rD = noise(5), rJ = noise(6), rLR = noise(7);
        double interval = -1; int fixedStart = -1;
        switch (md) {
            case 1: if (delay < 1) delay = 0.25 * sr; posR = std::min(posR, 0.25); break;
            case 2: posR = std::max(posR, 2.0); break;
            case 3: rev = 1; break;
            case 4: size = std::max(size * 0.5, 0.02 * sr); posR = std::max(posR, 3.0); spr = std::max(spr, 0.9); break;
            case 5: { if (pitchR < 0.5) pitchR = 12; const double lim = pitchR; const int idx = (int)std::floor((rP * 0.5 + 0.5) * 7.999); double o = kScat[idx]; if (std::fabs(o) > lim) o = 0; pitch += o; pitchR = 0; break; }
            case 6: size *= 1.4; posR = std::max(posR, 2.0); spr = std::max(spr, 0.85); pitchR = std::max(pitchR, 0.12); break;
            case 7: { static const double m[4] = { 0.25, 0.5, 1, 2 }; size *= m[(int)std::floor((rS * 0.5 + 0.5) * 3.999)]; rev = std::max(rev, 0.3); vRand = std::max(vRand, 0.4); posR = std::max(posR, 1.0); pitchR = std::max(pitchR, 7.0); break; }
            case 8: { interval = size; if (stutN_ <= 0 || stutN_ > 8) { stutN_ = 0; fixedStart = -2; } break; }
            case 9: { const double sl = jsr(0.1 * sr); size = sl; interval = sl; break; }
            case 10: posR = std::max(posR, 12.0); pitchR = 0; break;
            case 12: size = std::max(size * 3, 0.2 * sr); spr = std::max(spr, 0.9); break;
            case 13: spec = 1; break;
            default: break;
        }
        size = std::max(64.0, std::min(sr * 1.2, size * (1 + p.sizeRand * rS * 0.9)));
        const double semis = pitch + pitchR * rP; const double rate = std::pow(2.0, std::max(-48.0, std::min(48.0, semis)) / 12);
        const bool isRev = (md == 3) || (rD * 0.5 + 0.5 < rev);
        const double span = size * std::max(1.0, rate);
        const double anchor = frozen_ ? (double)frozenAt_ : (double)w_;
        const double offS = p.lrSpread / 1000 * sr * rLR;
        double look = span + delay + std::max(0.0, posR) * size * (rT * 0.5 + 0.5);
        if (md == 9) { if (scrIdx_ >= 8) { for (int q = 0; q < 8; q++) scrOrder_[q] = q; for (int q = 7; q > 0; q--) { const int j = (int)std::floor(rand() * (q + 1)); const int t = scrOrder_[q]; scrOrder_[q] = scrOrder_[j]; scrOrder_[j] = t; } scrIdx_ = 0; } look = (scrOrder_[scrIdx_++] + 1) * size + delay; }
        if (md == 8) { if (fixedStart == -2) stutStart_ = anchor - look; look = anchor - stutStart_; stutN_++; }
        look = std::max(span + 3 + std::fabs(offS), std::min((double)size_ - 8 - std::fabs(offS), look));
        const double start = anchor - look; double pos, step;
        if (isRev) { pos = start + span; step = -rate; } else { pos = start; step = rate; }
        if (md == 12 && (trigN_ % 3) == 2) { step *= 2; size /= 2; gain = 0.5; }
        const double panV = std::max(-1.0, std::min(1.0, panCenter + p.drift * std::sin(kTwoPi * 0.13 * (double)t_ / sr) + rPan * spr));
        const double ang = (panV + 1) * 3.141592653589793 / 4; const double vol = 1 - vRand * (rV * 0.5 + 0.5);
        gPos_[k] = pos; gStep_[k] = step; gAge_[k] = 0; gDur_[k] = (int)std::max(8.0, jsr(size)); gGL_[k] = std::cos(ang) * vol * gain; gGR_[k] = std::sin(ang) * vol * gain;
        gOff_[k] = offS; gAA_[k] = (int)std::min(4.0, std::max(1.0, std::ceil(rate))); gSpec_[k] = (uint8_t)spec;
        if (spec) { const double fc = std::min(sr * 0.45, 200 * std::pow(2.0, (rJ * 0.5 + 0.5) * 6)); gF_[k] = 2 * std::sin(3.141592653589793 * fc / sr); const int b = k * 4; gS_[b] = gS_[b + 1] = gS_[b + 2] = gS_[b + 3] = 0; }
        gBack_[k] = look; count_ = k + 1; trigN_++;
        double jf = 1 + p.jitter * rJ * 0.9; if (jf < 0.05) jf = 0.05; jit_ = jf; interval_ = interval;
    }
};

} // namespace vv
