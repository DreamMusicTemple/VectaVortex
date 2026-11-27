// Port of ReverbProcessor (the original reverb: Room, Spring, Plate, Hall, Shimmer, Reverse). Mono in, mono out.
#pragma once
#include "dsp_common.hpp"
#include "fx_basic.hpp"

namespace vv {

class LegacyReverb {
public:
    struct Params { int algo = 3; /*0 room 1 spring 2 plate 3 hall 4 shimmer 5 reverse, >=9 idle*/ double decay = 0.5, size = 0.5, predelay = 0.02, mix = 0.3, shimA = 12, shimB = -12;
                    double rvTail = 1, rvTrans = 0.5; /*reverse only: tail = how much of the swell build-up is heard (1 = all), trans = peak character (0 soft, .5 classic, 1 sharp)*/ };
    void prepare(double sr, uint32_t seed = 1) {
        sr_ = sr;
        predelay_.init((int)std::ceil(0.35 * sr) + 8);
        for (auto& c : combs_) c.init((int)std::ceil(0.2 * sr) + 8);
        shWin_ = (int)std::ceil(0.07 * sr);
        shLineA_.init(shWin_ * 2 + 16); shLineB_.init(shWin_ * 2 + 16);
        rvMax_ = (int)std::ceil(1.3 * sr);
        rv_[0].assign(rvMax_, 0.f); rv_[1].assign(rvMax_, 0.f);
        spLine_[0].init((int)std::ceil(0.25 * sr) + 8); spLine_[1].init((int)std::ceil(0.25 * sr) + 8);
        rng_.s = seed;
        reset();
    }
    void reset() {
        predelay_.clear(); for (auto& c : combs_) c.clear();
        ap1_ = Biquad{}; ap2_ = Biquad{};
        shLineA_.clear(); shLineB_.clear(); shPhA_ = shPhB_ = shFb_ = shDcX_ = shDcY_ = 0;
        std::fill(rv_[0].begin(), rv_[0].end(), 0.f); std::fill(rv_[1].begin(), rv_[1].end(), 0.f);
        rvRec_ = 0; rvPos_ = 0; rvN_ = (int)std::floor(0.5 * sr_); rvPlayN_ = 0;
        for (int k = 0; k < 2; k++) { spLine_[k].clear(); std::fill(spX_[k], spX_[k] + kSpN, 0.f); std::fill(spY_[k], spY_[k] + kSpN, 0.f); spLp_[k] = 0; }
        spOutLp_ = 0; exEnv_ = 0; exBright_ = 0.5; exLp_ = 0; thunkEnv_ = 0; thunkPh_ = 0; wob_ = 0; strokeTarget_ = 0; strokeCur_ = 0; strokeLp_ = 0;
    }
    LcgRandom& rng() { return rng_; }
    // hands-on spring excitation (from the UI): a struck coil / a scraped magnet strip
    void tap(double s) { s = clampd(s, 0, 1); exEnv_ = std::max(exEnv_, s); exBright_ = s; thunkEnv_ = std::max(thunkEnv_, s); wob_ = std::max(wob_, s * 600); }
    void stroke(double v) { strokeTarget_ = clampd(v, 0, 1); }

    void process(const float* in, float* out, int n, const Params& p) {
        const int algo = (int)jsRound(p.algo);
        if (algo >= 9) { std::fill(out, out + n, 0.f); return; }
        static const double combT[6][4] = {{0.021, 0.026, 0.031, 0.037}, {0.009, 0.011, 0.014, 0.018}, {0.013, 0.017, 0.021, 0.025}, {0.045, 0.056, 0.061, 0.068}, {0.045, 0.056, 0.061, 0.068}, {0.045, 0.056, 0.061, 0.068}};
        const double* times = (algo >= 0 && algo < 6) ? combT[algo] : combT[3];
        const double lpCutoff = algo == 4 ? 9000 : (algo == 5 ? 6000 : 4000);
        ap1_.setAllpass(500, 1, sr_); ap2_.setAllpass(1500, 1, sr_);
        const double predelaySamples = p.predelay * sr_;
        const bool shim = algo == 4, spring = algo == 1, reverse = algo == 5;
        const double rvFade = std::max(8.0, std::floor(0.008 * sr_));
        const double rvS0 = 0.7 * (1 - p.rvTail), rvEmph = p.rvTrans > 0.5 ? (p.rvTrans - 0.5) * 2.4 : 0.0;
        const double spFb = std::min(0.96, p.decay * 0.985), spSize = 0.5 + p.size;
        const double thunkInc = 2 * 3.141592653589793 * 95 / sr_;
        const double shimFbScale = 0.9 * (1 - p.decay) * 0.25;
        const double ratioA = std::pow(2.0, p.shimA / 12), ratioB = std::pow(2.0, p.shimB / 12);
        const double incA = (1 - ratioA) / shWin_, incB = (1 - ratioB) / shWin_;
        for (int i = 0; i < n; i++) {
            const double x = in ? in[i] : 0.0;
            double fed = x;
            if (spring) {
                const double nz = rng_.next() * 2 - 1;
                exEnv_ *= 0.9992; thunkEnv_ *= 0.9985; wob_ *= 0.99995;
                exLp_ += (nz - exLp_) * (0.06 + 0.7 * exBright_);
                strokeCur_ += (strokeTarget_ - strokeCur_) * 0.002;
                strokeLp_ += (nz - strokeLp_) * 0.3;
                thunkPh_ += thunkInc; if (thunkPh_ > 6.2831853) thunkPh_ -= 6.2831853;
                fed = x + exLp_ * exEnv_ * 1.6 + std::sin(thunkPh_) * thunkEnv_ * 0.9 + (nz - strokeLp_) * strokeCur_ * 0.5;
            }
            if (shim) {
                const double fb = shFb_;
                const double yA = shiftSample(shLineA_, shPhA_, fb), yB = shiftSample(shLineB_, shPhB_, fb);
                shPhA_ += incA; shPhA_ -= std::floor(shPhA_);
                shPhB_ += incB; shPhB_ -= std::floor(shPhB_);
                const double v = (yA + yB) * 0.5;
                fed = x + (v > 1 ? 1 : v < -1 ? -1 : v);
            }
            predelay_.write(fed);
            const double pd = predelay_.readInterpolated(predelaySamples);
            predelay_.advance();
            if (spring) {
                double sOut = 0;
                for (int k = 0; k < 2; k++) {
                    DelayLine& line = spLine_[k];
                    double y = line.readInterpolated(std::max(8.0, spBase_[k] * spSize * sr_ - wob_));
                    float* xs = spX_[k]; float* ys = spY_[k]; const double a = kSpA;
                    for (int s = 0; s < kSpN; s++) { const double o = -a * y + xs[s] + a * ys[s]; xs[s] = (float)y; ys[s] = (float)o; y = o; }
                    spLp_[k] += (y - spLp_[k]) * 0.45;
                    line.write(pd * 0.5 + spLp_[k] * spFb);
                    line.advance();
                    sOut += spLp_[k];
                }
                spOutLp_ += (sOut * 0.5 - spOutLp_) * 0.6;
                out[i] = (float)(x * (1 - p.mix) + spOutLp_ * 1.4 * p.mix);
                continue;
            }
            double combSum = 0;
            for (int c = 0; c < 4; c++) {
                const double ds = times[c] * (0.5 + p.size) * sr_;
                combSum += combs_[c].process(pd, ds, p.decay, lpCutoff, sr_);
            }
            double wet = ap1_.process(combSum);
            wet = ap2_.process(wet);
            if (reverse) {
                std::vector<float>& rec = rv_[rvRec_]; std::vector<float>& play = rv_[1 - rvRec_];
                rec[rvPos_] = (float)wet;
                const int pi = rvPlayN_ - 1 - rvPos_;
                double rv = 0;
                if (pi >= 0) {
                    const double pn = rvPlayN_, u = rvPos_ / pn, fo = rvFo(rvPlayN_, p.rvTrans), rem = pn - rvPos_;
                    double g = std::min(1.0, rvPos_ / rvFade);                                      // click-free start
                    if (rvS0 > 0) { const double q = (u - rvS0) / (1 - rvS0); g *= q <= 0 ? 0.0 : q * q * (3 - 2 * q); }   // Tail: where the swell starts to be heard
                    if (rem < fo) g *= 0.5 - 0.5 * std::cos(3.141592653589793 * rem / fo);           // Transient: how the swell lands
                    if (rvEmph > 0) { const double u2 = u * u, u4 = u2 * u2; g *= 1 + rvEmph * u4 * u4; }   // Transient > 0.5: emphasise the peak
                    rv = play[pi] * g;
                }
                rvPos_++;
                if (rvPos_ >= rvN_) {
                    rvPlayN_ = rvN_; rvRec_ = 1 - rvRec_; rvPos_ = 0;
                    rvN_ = (int)std::min((double)rvMax_, std::floor((0.25 + p.size * 0.45) * sr_));
                }
                wet = rv * 1.2;
            }
            if (shim) {
                const double dc = wet - shDcX_ + 0.995 * shDcY_; shDcX_ = wet; shDcY_ = dc;
                shFb_ = dc * shimFbScale;
            }
            out[i] = (float)(x * (1 - p.mix) + wet * p.mix);
        }
    }
private:
    struct Comb {
        DelayLine line; double lpY = 0;
        void init(int n) { line.init(n); lpY = 0; }
        void clear() { line.clear(); lpY = 0; }
        inline double process(double x, double ds, double fb, double lpCut, double sr) {
            const double delayed = line.readInterpolated(ds);
            double a = lpCut * 6.0 / sr; a = a < 0 ? 0 : (a > 0.999 ? 0.999 : a);
            lpY = lpY + a * (delayed - lpY);
            line.write(x + lpY * fb);
            line.advance();
            return delayed;
        }
    };
    inline double shiftSample(DelayLine& line, double ph, double x) {
        line.write(x);
        const double W = shWin_;
        const double p2 = ph + 0.5 >= 1 ? ph - 0.5 : ph + 0.5;
        const double s1 = std::sin(3.141592653589793 * ph), s2 = std::sin(3.141592653589793 * p2);
        const double y = line.readInterpolated(ph * W + 2) * s1 * s1 + line.readInterpolated(p2 * W + 2) * s2 * s2;
        line.advance();
        return y;
    }
    static constexpr int kSpN = 28; static constexpr double kSpA = 0.62;
    double sr_ = 48000; DelayLine predelay_; Comb combs_[4]; Biquad ap1_, ap2_;
    int shWin_ = 1; DelayLine shLineA_, shLineB_; double shPhA_ = 0, shPhB_ = 0, shFb_ = 0, shDcX_ = 0, shDcY_ = 0;
    int rvMax_ = 1, rvRec_ = 0, rvPos_ = 0, rvN_ = 1, rvPlayN_ = 0; std::vector<float> rv_[2];
    // fade-out length (samples) of each reversed swell, cached so pow() is not run per sample
    double rvFo(int pn, double t) { if (pn == foPn_ && t == foT_) return foV_; const double ms = t >= 0.5 ? 8 - (t - 0.5) * 2 * 7.4 : 8 * std::pow(45.0, (0.5 - t) * 2); foV_ = std::max(8.0, std::min(0.45 * pn, ms * 0.001 * sr_)); foPn_ = pn; foT_ = t; return foV_; }
    int foPn_ = -1; double foT_ = -1, foV_ = 8;
    DelayLine spLine_[2]; float spX_[2][kSpN] = {}, spY_[2][kSpN] = {}; double spLp_[2] = {0, 0}, spBase_[2] = {0.0337, 0.0419}, spOutLp_ = 0;
    double exEnv_ = 0, exBright_ = 0.5, exLp_ = 0, thunkEnv_ = 0, thunkPh_ = 0, wob_ = 0, strokeTarget_ = 0, strokeCur_ = 0, strokeLp_ = 0;
    LcgRandom rng_;
};

} // namespace vv
