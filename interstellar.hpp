// INTERSTELLAR - spectral particle ambient FX.
//
// Any incoming audio is written to a rolling buffer (Live) or a captured buffer (Capture). A clock cuts that buffer into segments
// (forward / reverse / random / walk / ping-pong) and a read head loops through the current segment at 1/stretch speed. Every hop the
// frame under the head is analysed (FFT), its tonal skeleton extracted, split into six bands, and re-synthesised as a stack of
// "particles": pitch-shifted copies of one band each, with their own life, pan and drift. Particle pitches are chosen up / down with
// separate ranges and probabilities and snapped to the key (detected from the material or set by hand). The result is rebuilt with
// random-phase overlap-add (decorrelated left / right), rung through tuned resonators that follow the segment's own harmony, and
// bathed in a built-in FDN reverb with shimmer and freeze.
//
// Deterministic (JsRandom LCG, double precision) so the web AudioWorklet port renders the same sound. No allocation after prepare():
// every buffer is sized for the Lush quality and a quality change only reconfigures the used part.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "dsp_common.hpp"
#include "synth_common.hpp"

namespace vv {
namespace is {

constexpr double kPi = 3.141592653589793;
constexpr int kBands = 6, kMaxParticles = 36, kMaxRes = 16;

struct Params {
    // source
    int source = 0;               // 0 live, 1 capture
    bool freeze = false;          // live: stop writing
    int capture = 0;              // counter: a change starts a new capture
    double captureLen = 4, span = 8;
    // analysis
    double tonal = 0.5;
    double band[kBands] = { 0, 0, 0, 0, 0, 0 };   // dB
    // segments
    int segments = 4;
    bool segSync = false; std::string segDiv = "1/1"; double segTime = 4;
    int segOrder = 0;             // 0 fwd, 1 rev, 2 random, 3 walk, 4 ping-pong
    double stretch = 8, bloom = 0.5;
    // particles
    double density = 0.5, life = 6, pitchUp = 12, upProb = 0.4, pitchDown = 12, downProb = 0.3, body = 0.7, particles = 0.8, spread = 0.8;
    int snap = 0;                 // 0 key, 1 octaves+fifths, 2 free
    int scale = 0;                // 0 auto, 1 major, 2 minor, 3 dorian, 4 lydian, 5 pent maj, 6 pent min, 7 whole tone
    int key = -1;                 // -1 auto, 0..11
    // resonators
    double resonance = 0.3, ring = 3, strum = 0.3;
    int resTune = 0;              // 0 segment harmony, 1 key chord, 2 open fifths
    // motion
    double evolve = 0.4, drift = 0.4;
    // space
    double space = 0.5, size = 0.6, decay = 0.6, predelay = 0.03, damp = 0.4, revMod = 0.4, width = 0.8, shimmer = 0.2; bool revFreeze = false;
    // global
    double character = 0, mix = 1, level = 0, bpm = 120;
    int quality = 1;              // 0 eco, 1 normal, 2 lush
};

inline double dbToGain(double db) { return std::pow(10.0, db / 20.0); }
inline double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline double smooth01(double t) { t = clamp01(t); return t * t * (3 - 2 * t); }

// ------------------------------------------------------------------------------------------------------------------ FFT (double, table driven)
class Fft {
public:
    void prepare(int maxN) { maxN_ = maxN; cs_.assign(maxN / 2, 0.0); sn_.assign(maxN / 2, 0.0); rev_.assign(maxN, 0);
        for (int i = 0; i < maxN / 2; i++) { cs_[i] = std::cos(2 * kPi * i / maxN); sn_[i] = -std::sin(2 * kPi * i / maxN); } setSize(maxN); }
    void setSize(int n) { n_ = n; int bits = 0; while ((1 << bits) < n) bits++;
        for (int i = 0; i < n; i++) { int r = 0; for (int b = 0; b < bits; b++) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev_[i] = r; } }
    // forward: e^{-i..}; inverse: e^{+i..}, unnormalised
    void run(double* re, double* im, bool inverse) const {
        const int n = n_;
        for (int i = 0; i < n; i++) { const int j = rev_[i]; if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
        for (int len = 2; len <= n; len <<= 1) {
            const int half = len >> 1, step = maxN_ / len;
            for (int i = 0; i < n; i += len) for (int j = 0; j < half; j++) {
                const double wr = cs_[j * step], wi = inverse ? -sn_[j * step] : sn_[j * step];
                const int a = i + j, b = a + half;
                const double vr = re[b] * wr - im[b] * wi, vi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - vr; im[b] = im[a] - vi; re[a] += vr; im[a] += vi;
            }
        }
    }
private:
    int maxN_ = 0, n_ = 0; std::vector<double> cs_, sn_; std::vector<int> rev_;
};

// octave-up pitch shifter: two crossfaded delay taps sweeping at one sample per sample
struct OctaveUp {
    void prepare(double sr) { len_ = (int)std::ceil(0.1 * sr); buf_.assign(len_ + 4, 0.f); pos_ = 0; ph_ = 0; lp_ = 0; }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.f); pos_ = 0; ph_ = 0; lp_ = 0; }
    inline double tick(double x) {
        buf_[pos_] = (float)x; const double W = (double)len_ - 2;
        ph_ -= 1.0 / W; if (ph_ < 0) ph_ += 1.0; double p2 = ph_ + 0.5; if (p2 >= 1) p2 -= 1;
        const double y = tap(ph_ * W) * std::sin(kPi * ph_) + tap(p2 * W) * std::sin(kPi * p2);
        if (++pos_ >= len_ + 4) pos_ = 0; lp_ += (y - lp_) * 0.45; return lp_;
    }
private:
    inline double tap(double d) const { double r = (double)pos_ - d; const int n = len_ + 4; while (r < 0) r += n; const int i0 = (int)r; const double f = r - i0; const int i1 = i0 + 1 >= n ? 0 : i0 + 1; return buf_[i0] * (1 - f) + buf_[i1] * f; }
    std::vector<float> buf_; int len_ = 1, pos_ = 0; double ph_ = 0, lp_ = 0;
};

// ------------------------------------------------------------------------------------------------------------------ reverb (8-line FDN + shimmer)
class Reverb {
public:
    static constexpr int L = 8;
    void prepare(double sr) {
        sr_ = sr; maxLen_ = (int)std::ceil(0.16 * sr) + 32;
        for (int i = 0; i < L; i++) { line_[i].assign(maxLen_, 0.f); wp_[i] = 0; lp_[i] = 0; ph_[i] = std::fmod(i * 0.173, 1.0); }
        static const double ms[L] = { 29.7, 37.1, 41.1, 43.7, 53.3, 61.3, 67.9, 73.1 };
        static const double rate[L] = { 0.11, 0.17, 0.23, 0.29, 0.13, 0.19, 0.31, 0.37 };
        for (int i = 0; i < L; i++) { base_[i] = ms[i] * sr / 1000.0; rate_[i] = rate[i] / sr; }
        static const double ap[4] = { 5.1, 7.7, 11.3, 16.9 };
        for (int c = 0; c < 2; c++) for (int j = 0; j < 4; j++) { apLen_[c][j] = std::max(2, (int)std::floor(ap[j] * sr / 1000.0 * (c ? 1.07 : 1.0))); apBuf_[c][j].assign(apLen_[c][j] + 1, 0.f); apPos_[c][j] = 0; }
        preMax_ = (int)std::ceil(0.4 * sr) + 8; for (int c = 0; c < 2; c++) { pre_[c].assign(preMax_, 0.f); } prePos_ = 0;
        shIn_.prepare(sr); shTail_.prepare(sr);
        sizeS_ = 1; reset();
    }
    void reset() { for (int i = 0; i < L; i++) { std::fill(line_[i].begin(), line_[i].end(), 0.f); lp_[i] = 0; wp_[i] = 0; } for (int c = 0; c < 2; c++) { std::fill(pre_[c].begin(), pre_[c].end(), 0.f); for (int j = 0; j < 4; j++) std::fill(apBuf_[c][j].begin(), apBuf_[c][j].end(), 0.f); }
        shIn_.reset(); shTail_.reset(); ctr_ = 0; fbS_ = 0; inS_ = 1; }
    void set(double size, double decay, double predelay, double damp, double mod, double width, double shimmer, bool freeze) {
        sizeT_ = 0.6 + 1.2 * clamp01(size); decay_ = clamp01(decay); preSamp_ = clampd(predelay, 0, 0.35) * sr_; damp_ = clamp01(damp);
        mod_ = clamp01(mod) * 8.0; width_ = clampd(width, 0, 1.5); shim_ = clamp01(shimmer); freeze_ = freeze;
    }
    inline void tick(float xl, float xr, float& ol, float& orr) {
        if ((ctr_++ & 31) == 0) updateCoefs();
        sizeS_ += (sizeT_ - sizeS_) * 0.0004; fbS_ += ((freeze_ ? 1.0 : 0.0) - fbS_) * 0.0004; inS_ += ((freeze_ ? 0.0 : 1.0) - inS_) * 0.0004;
        // predelay
        pre_[0][prePos_] = xl; pre_[1][prePos_] = xr;
        int rp = prePos_ - (int)preSamp_; if (rp < 0) rp += preMax_;
        float il = pre_[0][rp], ir = pre_[1][rp]; if (++prePos_ >= preMax_) prePos_ = 0;
        il = (float)(il * inS_); ir = (float)(ir * inS_);
        { const double sI = shIn_.tick(0.5 * (il + ir)) * shim_ * 0.8 * inS_; il += (float)sI; ir += (float)sI; }   // octave-up copy of the input: feed-forward, so it can never run away
        // diffusion
        il = allpass(0, il); ir = allpass(1, ir);
        // read lines
        double v[L];
        for (int i = 0; i < L; i++) {
            ph_[i] += rate_[i]; if (ph_[i] >= 1) ph_[i] -= 1;
            const double d = clampd(base_[i] * sizeS_ + mod_ * std::sin(2 * kPi * ph_[i]), 2.0, (double)(maxLen_ - 4));
            double rpos = wp_[i] - d; if (rpos < 0) rpos += maxLen_;
            const int i0 = (int)rpos; const double f = rpos - i0; const int i1 = i0 + 1 >= maxLen_ ? 0 : i0 + 1;
            double s = line_[i][i0] * (1 - f) + line_[i][i1] * f;
            lp_[i] += (s - lp_[i]) * (freeze_ ? 1.0 : dampA_);   // one-pole damping in the loop (none while frozen)
            v[i] = lp_[i] * g_[i];
        }
        // Hadamard mix (fast WHT, 8)
        for (int s = 1; s < L; s <<= 1) for (int i = 0; i < L; i += s << 1) for (int j = i; j < i + s; j++) { const double a = v[j], b = v[j + s]; v[j] = a + b; v[j + s] = a - b; }
        const double nrm = 0.35355339059327373;
        // output taps (before re-injection)
        double sl = 0, sr = 0; for (int i = 0; i < L; i++) { v[i] *= nrm; if (i & 1) sr += v[i]; else sl += v[i]; }
        const double shTail = shTail_.tick(0.5 * (sl + sr)) * shim_ * 0.35;   // parallel octave-up of the tail (not fed back)
        for (int i = 0; i < L; i++) {
            const double inj = (i & 1 ? ir : il) * 0.5;
            double w = v[i] + inj; if (!(w == w)) w = 0; if (w > 8) w = 8; if (w < -8) w = -8;
            line_[i][wp_[i]] = (float)w; if (++wp_[i] >= maxLen_) wp_[i] = 0;
        }
        const double m = 0.5 * (sl + sr) + shTail, s = 0.5 * (sl - sr);
        ol = (float)((m + s * width_) * 0.5); orr = (float)((m - s * width_) * 0.5);
    }
private:
    void updateCoefs() {
        const double rt60 = 0.5 + decay_ * decay_ * 30.0;
        for (int i = 0; i < L; i++) { const double len = base_[i] * sizeS_ / sr_; g_[i] = freeze_ ? 0.99995 : std::pow(10.0, -3.0 * len / rt60) ; }
        const double fc = 16000.0 * (1 - damp_) + 700.0 * damp_; dampA_ = 1.0 - std::exp(-2 * kPi * fc / sr_);
    }
    inline float allpass(int c, float x) {
        for (int j = 0; j < 4; j++) { auto& b = apBuf_[c][j]; const int len = apLen_[c][j]; int& p = apPos_[c][j];
            const float d = b[p]; const float y = -0.6f * x + d; b[p] = x + 0.6f * y; if (++p >= len) p = 0; x = y; }
        return x;
    }
    double sr_ = 48000; int maxLen_ = 0, preMax_ = 0, prePos_ = 0, ctr_ = 0, apLen_[2][4] = {}, apPos_[2][4] = {};
    std::vector<float> line_[L], pre_[2], apBuf_[2][4]; OctaveUp shIn_, shTail_; int wp_[L] = {}; double lp_[L] = {}, ph_[L] = {}, base_[L] = {}, rate_[L] = {}, g_[L] = {};
    double sizeT_ = 1.32, sizeS_ = 1, decay_ = 0.6, preSamp_ = 0, damp_ = 0.4, dampA_ = 0.5, mod_ = 3, width_ = 1, shim_ = 0, fbS_ = 0, inS_ = 1; bool freeze_ = false;
};

// ------------------------------------------------------------------------------------------------------------------ the engine
class Interstellar {
public:
    void prepare(double sr, uint32_t seed = 1) {
        sr_ = sr; rng_.s = seed ? seed : 1; maxN_ = sr >= 88000 ? 16384 : 8192;
        bufLen_ = (int)std::ceil(20.0 * sr) + 8; buf_.assign(bufLen_, 0.f);
        fft_.prepare(maxN_);
        auto D = [&](std::vector<double>& v, int n) { v.assign(n, 0.0); };
        D(re_, maxN_); D(im_, maxN_); D(mag_, maxN_ / 2 + 2); D(S_, maxN_ / 2 + 2); D(sm_, maxN_ / 2 + 2); D(S2_, maxN_ / 2 + 2); D(pref_, maxN_ / 2 + 3); D(curve_, maxN_ / 2 + 2);
        D(accL_, maxN_ / 2 + 2); D(accR_, maxN_ / 2 + 2); D(winA_, maxN_); D(winS_, maxN_); D(lf_, maxN_ / 2 + 2);
        for (int b = 0; b < kBands; b++) D(W_[b], maxN_ / 2 + 2);
        olaL_.assign(maxN_ + maxN_ / 8 + 8, 0.f); olaR_.assign(maxN_ + maxN_ / 8 + 8, 0.f);
        rev_.prepare(sr);
        for (int i = 0; i < kMaxParticles; i++) pt_[i] = Particle();
        for (int i = 0; i < 12; i++) walk_[i] = 0;
        chroma_.fill(0); hasKey_ = false; detRoot_ = 0; detMinor_ = false;
        for (int i = 0; i < kMaxRes; i++) { rc_[i] = rs_[i] = 0; rf_[i] = 220; rt_[i] = 220; rg_[i] = 0; }
        configureQuality(1, true); reset();
    }
    void reset() {
        std::fill(buf_.begin(), buf_.end(), 0.f); w_ = 0; written_ = 0; recLen_ = 0; capturing_ = false; lastCapture_ = 0; autoCaptured_ = false;
        std::fill(olaL_.begin(), olaL_.end(), 0.f); std::fill(olaR_.begin(), olaR_.end(), 0.f); rd_ = 0; hopCtr_ = 0; clockAcc_ = 0; segIdx_ = 0; segDir_ = 1; pos_ = 0;
        std::fill(sm_.begin(), sm_.end(), 0.0); for (int i = 0; i < kMaxParticles; i++) pt_[i].alive = false;
        for (int i = 0; i < kMaxRes; i++) { rc_[i] = rs_[i] = 0; }
        for (int b = 0; b < kBands; b++) bandE_[b] = 1e-6;
        rev_.reset(); mixS_ = P_.mix; levelS_ = 1; initParticles_ = true; scheduleHop();
    }
    void setParams(const Params& p) { P_ = p; if (p.quality != quality_) configureQuality(p.quality, false); }
    const Params& params() const { return P_; }
    int detectedRoot() const { return detRoot_; }
    bool detectedMinor() const { return detMinor_; }
    bool hasDetectedKey() const { return hasKey_; }
    int fftSize() const { return N_; }
    int activeParticles() const { int n = 0; for (int i = 0; i < kMaxParticles; i++) if (pt_[i].alive) n++; return n; }
    int currentSegment() const { return segIdx_; }
    // inL/inR: stereo input. outL/outR: stereo result (dry/wet mixed).
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) {
        const Params& p = P_;
        handleCapture();
        const double mixT = clamp01(p.mix), levT = dbToGain(clampd(p.level, -36, 12));
        for (int i = 0; i < n; i++) {
            const float xl = inL[i], xr = inR[i]; const float mono = 0.5f * (xl + xr);
            writeInput(mono);
            runDueStages();
            if (++hopCtr_ >= hop_) { hopCtr_ = 0; scheduleHop(); }
            // pad out
            float pl = olaL_[rd_], pr = olaR_[rd_]; olaL_[rd_] = 0; olaR_[rd_] = 0; if (++rd_ >= olaN_) rd_ = 0;
            // resonators
            double resL = 0, resR = 0; const double xin = 0.5 * (pl + pr);
            if (resOn_) for (int r = 0; r < nRes_; r++) {
                rg_[r] += (rgT_[r] - rg_[r]) * 0.002;
                const double c = rc_[r], s = rs_[r]; const double nc = rr_[r] * (c * rcos_[r] - s * rsin_[r]) + xin * rg_[r]; const double ns = rr_[r] * (c * rsin_[r] + s * rcos_[r]);
                rc_[r] = nc; rs_[r] = ns; const double y = ns * rnorm_[r]; if (r & 1) resR += y; else resL += y;
            }
            // the ringing is levelled against the pad it rings (follower AGC), so the Resonance dial is a relative amount for any material
            eP_ += (xin * xin - eP_) * 0.0005; eR_ += (0.5 * (resL * resL + resR * resR) - eR_) * 0.0005;
            const double want = std::sqrt(eP_ / (eR_ + 1e-14)); resAgc_ += (std::min(want, 400.0) - resAgc_) * 0.0008;
            const double resG = resGain_ * resAgc_;
            double padL = pl + resG * resL, padR = pr + resG * resR;
            // reverb
            mixS_ += (mixT - mixS_) * 0.002; levelS_ += (levT - levelS_) * 0.002; spaceS_ += (clamp01(p.space) - spaceS_) * 0.002;
            float rl, rr; rev_.tick((float)padL, (float)padR, rl, rr);
            const double sp = spaceS_;
            double wl = padL * (1 - 0.5 * sp) + rl * sp * 0.8, wr = padR * (1 - 0.5 * sp) + rr * sp * 0.8;
            wl *= levelS_; wr *= levelS_;
            double ol = xl * (1 - mixS_) + wl * mixS_, orr = xr * (1 - mixS_) + wr * mixS_;
            outL[i] = (float)limit(ol); outR[i] = (float)limit(orr);
        }
    }

private:
    struct Particle { bool alive = false; int band = 0; double ratio = 1, gain = 1, pan = 0, age = 0, life = 1, cents = 0, centsT = 0; };

    static inline double limit(double x) { if (!(x == x)) return 0; const double a = std::fabs(x); if (a <= 0.8) return x; const double y = 0.8 + 0.2 * std::tanh((a - 0.8) / 0.2); return x < 0 ? -y : y; }

    void configureQuality(int q, bool first) {
        quality_ = q < 0 ? 0 : q > 2 ? 2 : q;
        N_ = (quality_ == 0 ? 2048 : quality_ == 1 ? 4096 : 8192) * (maxN_ == 16384 ? 2 : 1); hop_ = N_ / 4; Dl_ = hop_ / 2; olaN_ = N_ + Dl_;
        nPart_ = quality_ == 0 ? 10 : quality_ == 1 ? 20 : 36; nRes_ = quality_ == 0 ? 6 : quality_ == 1 ? 12 : 16;
        fft_.setSize(N_);
        const int H = N_ / 2;
        for (int n = 0; n < N_; n++) { winA_[n] = 0.5 - 0.5 * std::cos(2 * kPi * n / N_); winS_[n] = winA_[n]; }
        for (int k = 0; k <= H; k++) lf_[k] = k > 0 ? std::log2((double)k * sr_ / N_ / 1000.0) : -10.0;
        // band weights: log-frequency crossovers with smooth overlaps
        static const double edges[kBands - 1] = { 150, 500, 1500, 3500, 8000 };
        for (int b = 0; b < kBands; b++) { kLo_[b] = H; kHi_[b] = 0; }
        for (int k = 0; k <= H; k++) {
            const double f = std::max(1.0, (double)k * sr_ / N_); double sum = 0; double w[kBands];
            for (int b = 0; b < kBands; b++) {
                const double lo = b == 0 ? 0.0 : 1.0 - smooth01((std::log2(edges[b - 1]) - std::log2(f)) / 0.7 + 0.5);   // rises past the lower edge
                const double hi = b == kBands - 1 ? 1.0 : smooth01((std::log2(edges[b]) - std::log2(f)) / 0.7 + 0.5);       // falls past the upper edge
                w[b] = (b == 0 ? 1.0 : lo) * hi; sum += w[b];
            }
            for (int b = 0; b < kBands; b++) { W_[b][k] = sum > 0 ? w[b] / sum : 0; if (W_[b][k] > 1e-4) { if (k < kLo_[b]) kLo_[b] = k; if (k > kHi_[b]) kHi_[b] = k; } }
        }
        std::fill(olaL_.begin(), olaL_.end(), 0.f); std::fill(olaR_.begin(), olaR_.end(), 0.f); rd_ = 0; hopCtr_ = 0; std::fill(sm_.begin(), sm_.end(), 0.0);
        initParticles_ = true; scheduleHop(); (void)first;
    }

    void writeInput(float x) {
        const Params& p = P_;
        bool write = true;
        if (p.source == 0) { if (p.freeze) write = false; }
        else { write = capturing_; }
        if (!write) return;
        buf_[w_] = x; if (++w_ >= bufLen_) w_ = 0; if (written_ < bufLen_) written_++;
        if (p.source == 1 && capturing_) { recLen_++; if (recLen_ >= capLenSamp_) capturing_ = false; }
    }
    void handleCapture() {
        const Params& p = P_;
        if (p.source != 1) { autoCaptured_ = false; return; }
        if (p.capture != lastCapture_ || (!autoCaptured_ && recLen_ == 0 && !capturing_)) { lastCapture_ = p.capture; autoCaptured_ = true; startCapture(); }
    }
    void startCapture() { capLenSamp_ = (int)clampd(P_.captureLen * sr_, 0.25 * sr_, 16.0 * sr_); capturing_ = true; recLen_ = 0; w_ = 0; written_ = 0; std::fill(buf_.begin(), buf_.end(), 0.f); pos_ = 0; }

    // region of the source buffer currently in play: absolute start index (ring) and length in samples
    void region(int& start, int& len) const {
        const Params& p = P_;
        if (p.source == 1) { start = 0; len = std::min(recLen_, bufLen_); return; }
        len = (int)std::min<double>(clampd(p.span, 1, 16) * sr_, (double)written_); start = w_ - len; if (start < 0) start += bufLen_;
    }
    inline double srcAt(int absIdx) const { int i = absIdx % bufLen_; if (i < 0) i += bufLen_; return buf_[i]; }

    double segPeriodSamples() const { const Params& p = P_; const double s = p.segSync ? syncSeconds(p.segDiv, p.bpm) : clampd(p.segTime, 0.25, 16.0); return s * sr_; }
    static double syncSeconds(const std::string& div, double bpm) {
        double q = 4; static const struct { const char* n; double q; } t[] = { {"2/1", 8}, {"1/1", 4}, {"1/2", 2}, {"1/4", 1}, {"1/8", 0.5}, {"1/4.", 1.5}, {"1/2.", 3}, {"1/1.", 6}, {"1/4t", 2.0 / 3} };
        for (auto& e : t) if (div == e.n) q = e.q;
        return (60.0 / clampd(bpm, 20, 400)) * q;
    }

    // ---- scale / interval helpers
    static const int* scaleSet(int type, int& count) {
        static const int major[] = { 0, 2, 4, 5, 7, 9, 11 }, minor[] = { 0, 2, 3, 5, 7, 8, 10 }, dorian[] = { 0, 2, 3, 5, 7, 9, 10 }, lydian[] = { 0, 2, 4, 6, 7, 9, 11 }, pmaj[] = { 0, 2, 4, 7, 9 }, pmin[] = { 0, 3, 5, 7, 10 }, whole[] = { 0, 2, 4, 6, 8, 10 };
        switch (type) { case 2: count = 7; return minor; case 3: count = 7; return dorian; case 4: count = 7; return lydian; case 5: count = 5; return pmaj; case 6: count = 5; return pmin; case 7: count = 6; return whole; default: count = 7; return major; }
    }
    int resolvedScale() const { return P_.scale > 0 ? P_.scale : (detMinor_ ? 2 : 1); }
    int resolvedRoot() const { return P_.key >= 0 ? P_.key % 12 : detRoot_; }
    // how many notes of the scale stay in the scale after a shift of n semitones (0..count)
    int overlap(int type, int n) const {
        int cnt; const int* sc = scaleSet(type, cnt); bool in[12] = {}; for (int i = 0; i < cnt; i++) in[sc[i]] = true;
        int o = 0; for (int i = 0; i < cnt; i++) { int m = ((sc[i] + n) % 12 + 12) % 12; if (in[m]) o++; } return o;
    }
    // draw an interval (semitones, > 0) for one direction
    double pickInterval(double range) {
        if (range <= 0.001) return 0;
        const Params& p = P_; const int maxN = (int)std::floor(range + 1e-9);
        if (p.snap == 2) return rng_.next() * range;
        if (maxN < 1) return 0;
        double w[48]; int n[48]; int c = 0; double tot = 0;
        const int type = resolvedScale(); int cnt; scaleSet(type, cnt);
        for (int s = 1; s <= maxN && c < 48; s++) {
            double wt;
            if (p.snap == 1) { const int m = s % 12; wt = m == 0 ? 1.5 : m == 7 ? 1.0 : m == 5 ? 0.6 : 0.0; }
            else { const double ov = (double)overlap(type, s) / cnt; wt = ov * ov * (s % 12 == 0 ? 1.8 : 1.0); if (s % 12 == 7) wt *= 1.3; }
            if (wt <= 0) continue; n[c] = s; w[c] = wt; tot += wt; c++;
        }
        if (c == 0) return 0;
        double u = rng_.next() * tot; for (int i = 0; i < c; i++) { u -= w[i]; if (u <= 0) return n[i]; }
        return n[c - 1];
    }

    void spawn(Particle& q, bool stagger) {
        const Params& p = P_;
        // band: weighted by user level and what the material actually contains
        double w[kBands], tot = 0; for (int b = 0; b < kBands; b++) { w[b] = dbToGain(clampd(p.band[b], -48, 12)) * (std::sqrt(bandE_[b]) + 1e-7); tot += w[b]; }
        int band = 0; double u = rng_.next() * tot; for (int b = 0; b < kBands; b++) { u -= w[b]; if (u <= 0) { band = b; break; } band = b; }
        q.band = band;
        double semis = 0; const double r1 = rng_.next();
        if (r1 < clamp01(p.upProb)) semis = pickInterval(clampd(p.pitchUp, 0, 24));
        else if (rng_.next() < clamp01(p.downProb)) semis = -pickInterval(clampd(p.pitchDown, 0, 24));
        else rng_.next();
        q.ratio = std::pow(2.0, semis / 12.0);
        q.gain = 0.5 + 0.5 * rng_.next();
        q.pan = (2 * rng_.next() - 1) * clamp01(p.spread);
        const double lifeS = clampd(p.life, 0.5, 20.0) * (0.5 + rng_.next());
        q.life = std::max(2.0, lifeS * sr_ / hop_); q.age = stagger ? rng_.next() * q.life : 0; if (!stagger) rng_.next();
        q.cents = q.centsT = 0; q.alive = true;
    }

    // ---- one analysis / synthesis frame, spread over the first half of every hop in small stages so no audio block carries the whole cost.
    // Stages: 0 begin (clock, read head, copy frame), 1 FFT, 2 tonal extraction, 3 bloom, 4 colour, 5 bands + key, 6 body,
    //         7.. one particle each, then build spectrum, inverse FFT, overlap-add, finalise (resonators, reverb settings).
    static constexpr int kPre = 7;
    int stageCount() const { return kPre + nPart_ + 4; }
    void scheduleHop() { stage_ = 0; nStages_ = stageCount(); sp_ = std::max(1, (hop_ / 2 - 4) / nStages_); stageOff_ = 0; }
    void runDueStages() {
        while (stage_ < nStages_ && hopCtr_ >= stageOff_) { runStage(stage_); stage_++; stageOff_ = stage_ * sp_; }
    }
    void runStage(int s) {
        const int H = N_ / 2; const Params& p = P_;
        if (s == 0) { stageBegin(); return; }
        const int last = nStages_ - 1;
        if (s == last) { stageFinalise(); return; }
        if (!frameValid_) return;
        if (s == 1) { fft_.run(re_.data(), im_.data(), false); return; }
        if (s == 2) {   // magnitude + harmonic extraction
            for (int k = 0; k <= H; k++) mag_[k] = std::sqrt(re_[k] * re_[k] + im_[k] * im_[k]);
            pref_[0] = 0; for (int k = 0; k <= H; k++) pref_[k + 1] = pref_[k] + mag_[k];
            const int wb = std::max(3, N_ / 1024 + 2); const double tonal = clamp01(p.tonal + tonAdd_);
            for (int k = 0; k <= H; k++) {
                const int a = std::max(0, k - wb), b = std::min(H, k + wb); const double E = (pref_[b + 1] - pref_[a]) / (b - a + 1);
                const double t = clamp01((mag_[k] / (E * 1.15 + 1e-9) - 1.0) * 1.5); const double ts = t * t * (3 - 2 * t);
                S_[k] = mag_[k] * ((1 - tonal) + tonal * ts);
            }
            return;
        }
        if (s == 3) {   // bloom: temporal persistence + spectral blur
            const double bl = clamp01(p.bloom), aT = bl * 0.92, bb = bl * 0.5;
            for (int k = 0; k <= H; k++) sm_[k] = sm_[k] * aT + S_[k] * (1 - aT);
            for (int k = 0; k <= H; k++) { const double l = sm_[k > 0 ? k - 1 : 0], r = sm_[k < H ? k + 1 : H]; S2_[k] = (1 - bb) * sm_[k] + bb * 0.25 * (l + 2 * sm_[k] + r); }
            return;
        }
        if (s == 4) {   // spectral colour: tilt (dB/oct around 1 kHz) and drifting vowel formants
            const double vm = 0.5 + 0.5 * walk_[10];
            const double f1 = 800 * (1 - vm) + 450 * vm, f2 = 1150 * (1 - vm) + 800 * vm, f3 = 2900 * (1 - vm) + 2830 * vm;
            const double l1 = std::log2(f1 / 1000.0), l2 = std::log2(f2 / 1000.0), l3 = std::log2(f3 / 1000.0);
            for (int k = 0; k <= H; k++) {
                double g = std::pow(2.0, lf_[k] * tilt_ / 6.0206);
                if (form_ > 0.001) { const double x = lf_[k]; const double a = (x - l1) / 0.22, b = (x - l2) / 0.22, c = (x - l3) / 0.25; g *= 1.0 + form_ * (2.2 * std::exp(-a * a) + 2.0 * std::exp(-b * b) + 1.2 * std::exp(-c * c)); }
                S2_[k] *= g;
            }
            return;
        }
        if (s == 5) {   // band gains (user + evolve), band energy, key
            for (int b = 0; b < kBands; b++) bg_[b] = dbToGain(clampd(p.band[b] + 8.0 * evo_ * walk_[b], -60, 14));
            for (int b = 0; b < kBands; b++) { double e = 0; for (int k = kLo_[b]; k <= kHi_[b]; k++) { const double v = S2_[k] * W_[b][k]; e += v * v; } bandE_[b] = bandE_[b] * 0.8 + 0.2 * (e / (double)(H + 1)); }
            updateKey(H);
            return;
        }
        if (s == 6) {   // clear the accumulators, lay down the body
            for (int k = 0; k <= H; k++) { accL_[k] = 0; accR_[k] = 0; }
            const double bodyL = clamp01(p.body) * 0.7071;
            if (bodyL > 0.0005) for (int k = 0; k <= H; k++) { double sum = 0; for (int b = 0; b < kBands; b++) sum += bg_[b] * W_[b][k]; const double v = S2_[k] * sum * bodyL; accL_[k] = v * v; accR_[k] = v * v; }
            active_ = (int)std::lround(clamp01(p.density) * nPart_ * (1 + 0.4 * evo_ * walk_[8])); if (active_ > nPart_) active_ = nPart_; if (active_ < 0) active_ = 0;
            if (initParticles_) { for (int i = 0; i < kMaxParticles; i++) pt_[i].alive = false; for (int i = 0; i < active_; i++) spawn(pt_[i], true); initParticles_ = false; }
            pl_ = clamp01(p.particles) / std::sqrt(std::max(1.0, active_ * 0.25));
            return;
        }
        if (s < kPre + nPart_) { particleStage(s - kPre); return; }
        const int j = s - (kPre + nPart_);
        if (j == 0) { buildSpectrum(); return; }
        if (j == 1) { fft_.run(re_.data(), im_.data(), true); return; }
        if (j == 2) {
            const double inv = 1.0 / N_;
            for (int n = 0; n < N_; n++) { int ix = wrBase_ + n; if (ix >= olaN_) ix -= olaN_; const double w = winS_[n] * inv; olaL_[ix] += (float)(re_[n] * w); olaR_[ix] += (float)(im_[n] * w); }
        }
    }
    void stageBegin() {
        const Params& p = P_;
        evo_ = clamp01(p.evolve); drf_ = clamp01(p.drift);
        characterOffsets(p.character, tilt_, form_, tonAdd_, shAdd_, dampAdd_, resAdd_);
        wrBase_ = rd_ + Dl_; if (wrBase_ >= olaN_) wrBase_ -= olaN_;
        for (int i = 0; i < 12; i++) walk_[i] = clampd(walk_[i] * 0.995 + (rng_.next() - 0.5) * (0.06 + 0.25 * drf_), -1, 1);
        int rs, rl; region(rs, rl);
        frameValid_ = rl >= N_;
        if (!frameValid_) return;
        const double period = std::max(segPeriodSamples(), (double)hop_);
        clockAcc_ += hop_;
        const int K = std::max(1, std::min(16, p.segments));
        if (clockAcc_ >= period) {
            clockAcc_ -= period; if (clockAcc_ > period) clockAcc_ = 0;
            int order = p.segOrder; if (evo_ > 0 && rng_.next() < evo_ * 0.35) order = 2; else rng_.next();
            if (order == 0) segIdx_ = (segIdx_ + 1) % K;
            else if (order == 1) segIdx_ = (segIdx_ - 1 + K) % K;
            else if (order == 2) segIdx_ = (int)std::floor(rng_.next() * K) % K;
            else if (order == 3) segIdx_ = (segIdx_ + (rng_.next() < 0.5 ? -1 : 1) + K) % K;
            else { if (K > 1) { int nx = segIdx_ + segDir_; if (nx >= K) { segDir_ = -1; nx = K - 2; } else if (nx < 0) { segDir_ = 1; nx = 1; } segIdx_ = nx; } }
            if (segIdx_ >= K) segIdx_ = 0;
            pos_ = 0;
        }
        if (segIdx_ >= K) segIdx_ = 0;
        const double segLen = (double)rl / K, segStart = (double)rs + segIdx_ * segLen;
        const double st = clampd(p.stretch, 1, 50) * std::pow(2.0, 0.6 * evo_ * walk_[6]);
        pos_ += hop_ / st; if (pos_ >= segLen) pos_ -= std::floor(pos_ / segLen) * segLen;
        const long long base = (long long)std::floor(segStart + pos_) - N_ / 2;
        for (int n = 0; n < N_; n++) { long long ix = (base + n) % bufLen_; if (ix < 0) ix += bufLen_; re_[n] = buf_[ix] * winA_[n]; im_[n] = 0; }
    }
    void particleStage(int i) {
        const Params& p = P_; const int H = N_ / 2;
        Particle& q = pt_[i];
        if (i >= active_) { q.alive = false; return; }
        if (!q.alive) spawn(q, false);
        q.age += 1; if (q.age >= q.life) spawn(q, false);
        if (rng_.next() < 0.05) q.centsT = (rng_.next() * 2 - 1) * 30.0 * drf_; else rng_.next();
        q.cents += (q.centsT - q.cents) * 0.1;
        const double ph = q.age / q.life; const double env = std::sin(kPi * ph); const double e2 = env * env * q.gain * pl_;
        if (e2 < 1e-5) return;
        const double r = q.ratio * std::pow(2.0, q.cents / 1200.0);
        const int b = q.band; const double th = (q.pan + 1) * kPi * 0.25, gl = std::cos(th) * e2 * bg_[b], gr = std::sin(th) * e2 * bg_[b];
        int k0 = (int)std::ceil(kLo_[b] * r), k1 = (int)std::floor(kHi_[b] * r); if (k0 < 1) k0 = 1; if (k1 > H - 1) k1 = H - 1;
        for (int k = k0; k <= k1; k++) {
            const double x = k / r; const int i0 = (int)x; const double f = x - i0; if (i0 + 1 > H) break;
            const double a0 = S2_[i0] * W_[b][i0], a1 = S2_[i0 + 1] * W_[b][i0 + 1]; const double v = a0 * (1 - f) + a1 * f;
            const double vl = v * gl, vr = v * gr; accL_[k] += vl * vl; accR_[k] += vr * vr;
        }
        (void)p;
    }
    void buildSpectrum() {
        const int H = N_ / 2; const double wid = clampd(P_.width, 0, 1); const double norm = 1.3333333333333333;
        // Z = XL + i XR, both Hermitian: one inverse FFT gives both channels
        for (int k = 0; k <= H; k++) {
            const double aL = std::sqrt(accL_[k]) * norm, aR = std::sqrt(accR_[k]) * norm;
            const double u1 = rng_.next(), u2 = rng_.next();
            if (k == 0 || k == H) { re_[k] = aL; im_[k] = aR; continue; }
            const double pL = 2 * kPi * u1, pR = pL + wid * 2 * kPi * (u2 - u1);
            const double a = aL * std::cos(pL), b = aL * std::sin(pL), c = aR * std::cos(pR), d = aR * std::sin(pR);
            re_[k] = a - d; im_[k] = b + c; re_[N_ - k] = a + d; im_[N_ - k] = -b + c;
        }
    }
    void stageFinalise() {
        const Params& p = P_;
        if (frameValid_) updateResonators();
        rev_.set(p.size, p.decay, p.predelay, clamp01(p.damp + dampAdd_), p.revMod, p.width, clamp01(p.shimmer + shAdd_), p.revFreeze);
        resGain_ = clamp01(p.resonance + resAdd_) * 1.2; resOn_ = resGain_ > 0.001;
    }

    static void characterOffsets(double c, double& tilt, double& form, double& tonAdd, double& shAdd, double& dampAdd, double& resAdd) {
        static const double T[5][6] = { { 0, 0, 0, 0, 0, 0 }, { 2.0, 0, 0.1, 0.5, -0.2, 0.0 }, { -3.0, 0, 0, 0, 0.35, 0.0 }, { -0.5, 1.0, 0.15, 0.1, 0, -0.1 }, { 1.0, 0, 0.5, 0.2, -0.3, 0.3 } };
        c = clamp01(c) * 4.0; int i = (int)std::floor(c); if (i > 3) i = 3; const double f = smooth01(c - i);
        double o[6]; for (int j = 0; j < 6; j++) o[j] = T[i][j] * (1 - f) + T[i + 1][j] * f;
        tilt = o[0]; form = o[1]; tonAdd = o[2]; shAdd = o[3]; dampAdd = o[4]; resAdd = o[5];
    }

    void updateKey(int H) {
        double c[12] = {}; double tot = 0;
        const int k0 = std::max(1, (int)std::ceil(55.0 * N_ / sr_)), k1 = std::min(H, (int)std::floor(1760.0 * N_ / sr_));
        for (int k = k0; k <= k1; k++) { const double f = (double)k * sr_ / N_; const double m = 69 + 12 * std::log2(f / 440.0); int pc = (int)std::lround(m) % 12; if (pc < 0) pc += 12; const double v = S2_[k] * S2_[k]; c[pc] += v; tot += v; }
        if (tot > 1e-12) for (int i = 0; i < 12; i++) chroma_[i] = chroma_[i] * 0.9 + 0.1 * (c[i] / tot);
        if (++keyCtr_ % 8 != 1) return;
        static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 }, mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        double mc = 0; for (int i = 0; i < 12; i++) mc += chroma_[i]; mc /= 12; double vc = 0; for (int i = 0; i < 12; i++) vc += (chroma_[i] - mc) * (chroma_[i] - mc);
        if (vc < 1e-10) return;
        double best = -2; int br = 0; bool bm = false;
        for (int mode = 0; mode < 2; mode++) {
            const double* pr = mode ? mnr : maj; double pm = 0; for (int i = 0; i < 12; i++) pm += pr[i]; pm /= 12; double pv = 0; for (int i = 0; i < 12; i++) pv += (pr[i] - pm) * (pr[i] - pm);
            for (int r = 0; r < 12; r++) { double cov = 0; for (int i = 0; i < 12; i++) cov += (chroma_[(i + r) % 12] - mc) * (pr[i] - pm); const double corr = cov / std::sqrt(vc * pv); if (corr > best) { best = corr; br = r; bm = mode == 1; } }
        }
        if (!hasKey_) { detRoot_ = br; detMinor_ = bm; hasKey_ = true; keyScore_ = best; }
        else if (best > keyScore_ + 0.04 || (br == detRoot_ && bm == detMinor_)) { detRoot_ = br; detMinor_ = bm; keyScore_ = best; }
        else keyScore_ *= 0.98;   // let an old key lose its grip slowly so a real modulation can win
    }

    void updateResonators() {
        const Params& p = P_; const double evo = clamp01(p.evolve);
        if (p.resonance + 0.3 * 0 <= 0.0005 && !(resGain_ > 0.001)) { /* still update so the bank is ready when the dial opens */ }
        double notes[kMaxRes]; int cnt = 0;
        const int root = resolvedRoot(); const int type = resolvedScale(); int sc; const int* set = scaleSet(type, sc);
        if (p.resTune == 0) {
            // top chroma classes of the segment under the head
            int idx[12]; for (int i = 0; i < 12; i++) idx[i] = i;
            for (int i = 0; i < 12; i++) for (int j = i + 1; j < 12; j++) if (chroma_[idx[j]] > chroma_[idx[i]]) { int t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
            int pcs[5], np = 0; const double thr = chroma_[idx[0]] * 0.55; for (int i = 0; i < 5 && chroma_[idx[i]] >= thr && chroma_[idx[i]] > 0; i++) pcs[np++] = idx[i];
            if (np == 0) { pcs[0] = root; pcs[1] = (root + 7) % 12; np = 2; }
            for (int oct = 3; oct <= 6 && cnt < nRes_; oct++) for (int i = 0; i < np && cnt < nRes_; i++) notes[cnt++] = 12 * (oct + 1) + pcs[i];
        } else if (p.resTune == 1) {
            const int deg[4] = { 0, 2, 4, 6 };
            for (int oct = 3; oct <= 6 && cnt < nRes_; oct++) for (int i = 0; i < 4 && cnt < nRes_; i++) { const int d = deg[i]; const int o = d / sc, r = d % sc; notes[cnt++] = 12 * (oct + 1) + root + set[r] + 12 * o; }
        } else {
            for (int i = 0; cnt < nRes_; i++) { const int m = 36 + root + (i / 2) * 12 + (i % 2 ? 7 : 0); if (m > 100) break; notes[cnt++] = m; }
        }
        const double ring = clampd(p.ring, 0.2, 12.0); const double phase = clockPhase();
        for (int r = 0; r < nRes_; r++) {
            const bool have = r < cnt; const double f = have ? 440.0 * std::pow(2.0, (notes[r] - 69) / 12.0) : rf_[r];
            if (have) rt_[r] = f; rf_[r] += (rt_[r] - rf_[r]) * 0.35;
            const double w = 2 * kPi * clampd(rf_[r], 20, sr_ * 0.45) / sr_; rcos_[r] = std::cos(w); rsin_[r] = std::sin(w);
            rr_[r] = std::pow(10.0, -3.0 / (ring * sr_)); rnorm_[r] = (1.0 - rr_[r]) * 2.0 / std::sqrt((double)std::max(1, std::min(cnt, nRes_)));
            const double d = (double)r / nRes_ * clamp01(p.strum); double g = 0.5 + 0.5 * std::cos(2 * kPi * (phase - d)); g = g * g;
            rgT_[r] = have ? (clamp01(p.strum) < 0.001 ? 1.0 : (1 - clamp01(p.strum)) + clamp01(p.strum) * g) : 0.0;
            rgT_[r] *= 1.0 + 0.3 * evo * walk_[(r % 4)];
        }
    }
    double clockPhase() const { const double period = std::max(segPeriodSamples(), (double)hop_); return clampd(clockAcc_ / period, 0, 1); }

    // ---- state
    double sr_ = 48000; Params P_; JsRandom rng_; int maxN_ = 8192, N_ = 4096, hop_ = 1024, quality_ = 1, nPart_ = 20, nRes_ = 12;
    std::vector<float> buf_, olaL_, olaR_; int bufLen_ = 0, w_ = 0, written_ = 0, recLen_ = 0, capLenSamp_ = 0, lastCapture_ = 0; bool capturing_ = false, autoCaptured_ = false;
    Fft fft_; std::vector<double> re_, im_, mag_, S_, sm_, S2_, pref_, curve_, accL_, accR_, winA_, winS_, lf_, W_[kBands];
    int kLo_[kBands] = {}, kHi_[kBands] = {}; double bandE_[kBands] = {};
    int stage_ = 0, nStages_ = 1, sp_ = 1, stageOff_ = 0, Dl_ = 512, olaN_ = 4096, wrBase_ = 0, active_ = 0; bool frameValid_ = false; double evo_ = 0, drf_ = 0, bg_[kBands] = {}, pl_ = 0, tilt_ = 0, form_ = 0, tonAdd_ = 0, shAdd_ = 0, dampAdd_ = 0, resAdd_ = 0;
    int rd_ = 0, hopCtr_ = 0, segIdx_ = 0, segDir_ = 1; double clockAcc_ = 0, pos_ = 0, walk_[12] = {}; bool initParticles_ = true;
    Particle pt_[kMaxParticles];
    std::array<double, 12> chroma_{}; int detRoot_ = 0, keyCtr_ = 0; bool detMinor_ = false, hasKey_ = false; double keyScore_ = 0;
    double rc_[kMaxRes] = {}, rs_[kMaxRes] = {}, rf_[kMaxRes] = {}, rt_[kMaxRes] = {}, rg_[kMaxRes] = {}, rgT_[kMaxRes] = {}, rr_[kMaxRes] = {}, rcos_[kMaxRes] = {}, rsin_[kMaxRes] = {}, rnorm_[kMaxRes] = {};
    double resGain_ = 0, resAgc_ = 0, eP_ = 0, eR_ = 0; bool resOn_ = false; Reverb rev_; double mixS_ = 1, levelS_ = 1, spaceS_ = 0.5;
};

}  // namespace is
}  // namespace vv
