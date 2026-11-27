// Multi-band compressor with OTT profile — port of MbCompProcessor (web app).
//   3 bands split by Linkwitz-Riley 4th-order crossovers (low band also through an all-pass at the upper crossover so the bands
//   re-sum phase-flat); per band a stereo-linked peak detector (0.1 ms attack / 10 ms release) feeds a gain computer that works in
//   BOTH directions: downward above tHi by (1 - 1/rDn), upward below tLo by (1 - 1/rUp) (capped +24 dB, faded out below -80 dB);
//   the dB gain is smoothed with Attack (gain falling) / Release (gain rising); Depth scales the dB change, Mix blends against the
//   un-compressed all-pass-equivalent signal, per band makeup, input / output gain. Real-time safe: no allocation after construction.
#pragma once
#include <cmath>
#include <algorithm>
#include "json.hpp"
#include "dsp_common.hpp"

namespace vv {

struct MbCompParams {
    struct Band { bool on = true; double tHi = -18, rDn = 3, tLo = -60, rUp = 1, atk = 25, rel = 220, gain = 0; };
    double xLow = 150, xHigh = 2500, inGain = 0, outGain = 0, depth = 1, mix = 1;
    Band band[3] = { { true, -18, 3, -60, 1, 25, 220, 0 }, { true, -18, 3, -60, 1, 12, 160, 0 }, { true, -18, 3, -60, 1, 5, 110, 0 } };
    static MbCompParams fromJson(const Json& p) {
        MbCompParams q; q.xLow = p.num("xLow", q.xLow); q.xHigh = p.num("xHigh", q.xHigh); q.inGain = p.num("inGain", 0); q.outGain = p.num("outGain", 0);
        q.depth = p.num("depth", 1); q.mix = p.num("mix", 1);
        static const char* names[3] = { "lo", "mi", "hi" };
        for (int b = 0; b < 3; b++) if (auto o = p.get(names[b])) {
            Band& B = q.band[b]; auto flag = [&](const char* k, bool d) { auto v = o->get(k); if (!v) return d; return v->type == Json::Bool ? v->b : v->n != 0; };
            B.on = flag("on", true); B.tHi = o->num("tHi", B.tHi); B.rDn = o->num("rDn", B.rDn); B.tLo = o->num("tLo", B.tLo); B.rUp = o->num("rUp", B.rUp);
            B.atk = o->num("atk", B.atk); B.rel = o->num("rel", B.rel); B.gain = o->num("gain", B.gain);
        }
        return q;
    }
};

class MbComp {
public:
    void prepare(double sr) { sr_ = sr; reset(); xl_ = xh_ = -1; }
    void reset() { for (auto& c : f_) for (auto& s : c) s = Bq{}; for (int b = 0; b < 3; b++) { env_[b] = 0; g_[b] = 0; grAcc_[b] = lvAcc_[b] = 0; } cnt_ = 0; }
    void setParams(const MbCompParams& p) {
        P_ = p; const double xl = p.xLow, xh = std::max(p.xHigh, p.xLow * 1.2);
        if (xl != xl_ || xh != xh_) {
            xl_ = xl; xh_ = xh;
            for (int c = 0; c < 2; c++) for (int s = 0; s < 2; s++) {
                setBq(f_[c][LP1 + s], true, xl); setBq(f_[c][HP1 + s], false, xl); setBq(f_[c][LP2 + s], true, xh); setBq(f_[c][HP2 + s], false, xh);
                setBq(f_[c][APL + s], true, xh); setBq(f_[c][APH + s], false, xh);
            }
        }
    }
    // live meters (average over the last block of processing), same quantities the web display shows
    const double* gainReductionDb() const { return grOut_; }
    const double* levelDb() const { return lvOut_; }
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) {
        const double inG = std::pow(10.0, P_.inGain / 20), outG = std::pow(10.0, P_.outGain / 20);
        const double aDet = 1 - std::exp(-1 / (0.0001 * sr_)), rDet = 1 - std::exp(-1 / (0.010 * sr_));
        double co[3], ro[3], mk[3];
        for (int b = 0; b < 3; b++) { const auto& B = P_.band[b]; co[b] = 1 - std::exp(-1 / (std::max(0.0002, B.atk / 1000) * sr_)); ro[b] = 1 - std::exp(-1 / (std::max(0.002, B.rel / 1000) * sr_)); mk[b] = std::pow(10.0, B.gain / 20); }
        double bl[3], br[3];
        for (int i = 0; i < n; i++) {
            for (int ch = 0; ch < 2; ch++) {
                const double x = (double)(ch ? inR[i] : inL[i]) * inG;
                const double low0 = run(ch, LP1, x), rest = run(ch, HP1, x);
                const double mid = run(ch, LP2, rest), high = run(ch, HP2, rest);
                const double low = run(ch, APL, low0) + run(ch, APH, low0);
                double* d = ch ? br : bl; d[0] = low; d[1] = mid; d[2] = high;
            }
            double wetL = 0, wetR = 0; const double dryL = bl[0] + bl[1] + bl[2], dryR = br[0] + br[1] + br[2];
            for (int b = 0; b < 3; b++) {
                const auto& B = P_.band[b]; const double pk = std::max(std::fabs(bl[b]), std::fabs(br[b]));
                env_[b] += (pk - env_[b]) * (pk > env_[b] ? aDet : rDet);
                const double lvl = 20 * std::log10(env_[b] + 1e-9);
                double tg = 0;
                if (B.on) {
                    if (lvl > B.tHi) tg = -(lvl - B.tHi) * (1 - 1 / std::max(1.0, B.rDn));
                    else if (lvl < B.tLo && B.rUp > 1) { const double fade = std::min(1.0, std::max(0.0, (lvl + 80) / 10)); tg = std::min(24.0, (B.tLo - lvl) * (1 - 1 / B.rUp)) * fade; }
                    tg *= P_.depth;
                }
                g_[b] += (tg - g_[b]) * (tg < g_[b] ? co[b] : ro[b]);
                const double gain = std::pow(10.0, g_[b] / 20) * (B.on ? mk[b] : 1.0);
                wetL += bl[b] * gain; wetR += br[b] * gain; grAcc_[b] += g_[b]; lvAcc_[b] += lvl;
            }
            outL[i] = (float)((dryL * (1 - P_.mix) + wetL * P_.mix) * outG); outR[i] = (float)((dryR * (1 - P_.mix) + wetR * P_.mix) * outG);
        }
        cnt_ += n;
        if (cnt_ >= sr_ * 0.05) { for (int b = 0; b < 3; b++) { grOut_[b] = grAcc_[b] / cnt_; lvOut_[b] = lvAcc_[b] / cnt_; grAcc_[b] = lvAcc_[b] = 0; } cnt_ = 0; }
    }
private:
    struct Bq { double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] = { 0, 0 }, z2[2] = { 0, 0 }; };
    enum { LP1 = 0, HP1 = 2, LP2 = 4, HP2 = 6, APL = 8, APH = 10 };
    void setBq(Bq& f, bool lp, double fc) {
        const double w0 = 2 * 3.141592653589793 * std::min(fc, sr_ * 0.45) / sr_, c = std::cos(w0), al = std::sin(w0) / (2 * 0.70710678), a0 = 1 + al;
        const double b0 = lp ? (1 - c) / 2 : (1 + c) / 2, b1 = lp ? 1 - c : -(1 + c);
        f.b0 = b0 / a0; f.b1 = b1 / a0; f.b2 = b0 / a0; f.a1 = -2 * c / a0; f.a2 = (1 - al) / a0;
    }
    double run(int ch, int first, double x) { for (int s = 0; s < 2; s++) { Bq& f = f_[ch][first + s]; const double y = f.b0 * x + f.z1[ch]; f.z1[ch] = f.b1 * x - f.a1 * y + f.z2[ch]; f.z2[ch] = f.b2 * x - f.a2 * y; x = y; } return x; }
    double sr_ = 44100, xl_ = -1, xh_ = -1, env_[3] = {}, g_[3] = {}, grAcc_[3] = {}, lvAcc_[3] = {}, grOut_[3] = {}, lvOut_[3] = {}; int cnt_ = 0;
    MbCompParams P_; Bq f_[2][12];
};

} // namespace vv
