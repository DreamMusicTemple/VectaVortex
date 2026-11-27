// Master output stage: look-ahead brick-wall limiter + ITU-R BS.1770-4 / EBU R128 loudness meter.
// Port of MasterLimiterProcessor (web app). Disabled = bit-exact pass-through with no added delay; switching it on cross-fades the
// delayed (limited) path in over 20 ms. Parameters are lock-free atomics (UI thread writes, audio thread reads). Loudness is measured
// on this stage's output: K-weighting, momentary (400 ms), short-term (3 s) and gated integrated (-70 LUFS absolute, -10 LU relative
// gate; the gate statistics use a 0.1 LU histogram so the audio thread never allocates or walks a long list), plus L/R sample peak
// and the limiter's maximum gain reduction since the last read.
#pragma once
#include <atomic>
#include <cmath>
#include <algorithm>
#include <vector>

namespace vv {

class MasterOut {
public:
    struct Params { bool on = false; double threshold = -1, ceiling = -1, release = 100, knee = 0, softClip = 0; };
    MasterOut() { for (auto& a : p_) a.store(0.f); p_[1].store(-1.f); p_[2].store(-1.f); p_[3].store(100.f); }
    void prepare(double sr) {
        sr_ = sr; la_ = std::max(8, (int)std::lround(0.0015 * sr)); dl_.assign(la_, 0.f); dr_.assign(la_, 0.f); det_.assign(la_, 0.f); wp_ = 0; g_ = 1; en_ = 0;
        const double fc1 = 1681.974450955533, G = 3.999843853973347, Q1 = 0.7071752369554196, K1 = std::tan(M_PI * fc1 / sr), Vh = std::pow(10.0, G / 20), Vb = std::pow(Vh, 0.4996667741545416), a01 = 1 + K1 / Q1 + K1 * K1;
        s1_[0] = (Vh + Vb * K1 / Q1 + K1 * K1) / a01; s1_[1] = 2 * (K1 * K1 - Vh) / a01; s1_[2] = (Vh - Vb * K1 / Q1 + K1 * K1) / a01; s1_[3] = 2 * (K1 * K1 - 1) / a01; s1_[4] = (1 - K1 / Q1 + K1 * K1) / a01;
        const double fc2 = 38.13547087602444, Q2 = 0.5003270373238773, K2 = std::tan(M_PI * fc2 / sr), a02 = 1 + K2 / Q2 + K2 * K2;
        s2_[0] = 1; s2_[1] = -2; s2_[2] = 1; s2_[3] = 2 * (K2 * K2 - 1) / a02; s2_[4] = (1 - K2 / Q2 + K2 * K2) / a02;
        for (auto& c : z_) for (double& v : c) v = 0;
        hop_ = (int)std::lround(0.1 * sr); hopN_ = 0; hopAcc_[0] = hopAcc_[1] = 0; ring_.assign(kRing, 0.0); ringN_ = 0; resetIntegrated(); peakL_ = peakR_ = 0; grMax_ = 0;
    }
    void setParams(const Params& q) { p_[0].store(q.on ? 1.f : 0.f); p_[1].store((float)q.threshold); p_[2].store((float)q.ceiling); p_[3].store((float)q.release); p_[4].store((float)q.knee); p_[5].store((float)q.softClip); }
    void resetIntegrated() { resetReq_.store(true); }
    // readouts (LUFS / dBFS); -infinity when there is nothing to report yet
    double momentary() const { return m_.load(); } double shortTerm() const { return s_.load(); } double integrated() const { return i_.load(); }
    double peakLdb() const { return pl_.load(); } double peakRdb() const { return pr_.load(); } double gainReductionDb() const { return gr_.load(); }
    void process(float* L, float* R, int n) {
        if (resetReq_.exchange(false)) { std::fill(hist_, hist_ + kBins, 0.0); histN_ = 0; }
        const bool on = p_[0].load() > 0.5f; const double thr = p_[1].load(), ceilDb = p_[2].load(), rel = p_[3].load(), knee = std::max(0.0, (double)p_[4].load()), sc = std::min(1.0, std::max(0.0, (double)p_[5].load()));
        const double ceil = std::pow(10.0, ceilDb / 20), pre = std::pow(10.0, (ceilDb - thr) / 20);
        const double aAtk = 1 - std::exp(-1 / (la_ * 0.35)), aRel = 1 - std::exp(-1 / (std::max(5.0, rel) / 1000 * sr_)), enStep = 1 / (0.02 * sr_);
        const double target = on ? 1.0 : 0.0;
        for (int i = 0; i < n; i++) {
            const double xl = L[i], xr = R[i]; double yl, yr;
            if (en_ == 0 && target == 0) {
                yl = xl; yr = xr;   // bit-exact bypass; keep the look-ahead line warm
                dl_[wp_] = (float)(xl * pre); dr_[wp_] = (float)(xr * pre); det_[wp_] = (float)(std::max(std::fabs(xl), std::fabs(xr)) * pre); wp_ = (wp_ + 1) % la_;
            } else {
                double pl = xl * pre, pr = xr * pre;
                if (sc > 0) { pl = ceil * ((1 - sc) * (pl / ceil) + sc * std::tanh(pl / ceil)); pr = ceil * ((1 - sc) * (pr / ceil) + sc * std::tanh(pr / ceil)); }
                const int wp = wp_; const double ol = dl_[wp], orr = dr_[wp];
                dl_[wp] = (float)pl; dr_[wp] = (float)pr; det_[wp] = (float)std::max(std::fabs(pl), std::fabs(pr)); wp_ = (wp + 1) % la_;
                double pk = 0; for (int k = 0; k < la_; k++) pk = std::max(pk, (double)det_[k]);
                double red = 0;
                if (pk > 0) { const double over = 20 * std::log10(pk / ceil); if (knee > 0 && over > -knee / 2) red = over >= knee / 2 ? over : (over + knee / 2) * (over + knee / 2) / (2 * knee); else if (knee == 0 && over > 0) red = over; }
                const double gt = std::pow(10.0, -std::max(0.0, red) / 20);
                g_ += (gt - g_) * (gt < g_ ? aAtk : aRel);
                double g = g_; const double peakNow = std::max(std::fabs(ol), std::fabs(orr)); if (peakNow * g > ceil) g = ceil / peakNow;
                en_ += (target > en_ ? enStep : -enStep); en_ = std::min(1.0, std::max(0.0, en_));
                yl = xl * (1 - en_) + ol * g * en_; yr = xr * (1 - en_) + orr * g * en_;
                const double grDb = -20 * std::log10(std::max(1e-6, g)); if (grDb > grMax_) grMax_ = grDb;
            }
            L[i] = (float)yl; R[i] = (float)yr;
            const double al = std::fabs(yl), ar = std::fabs(yr); if (al > peakL_) peakL_ = al; if (ar > peakR_) peakR_ = ar;
            const double kl = kw(0, yl), kr = kw(1, yr); hopAcc_[0] += kl * kl; hopAcc_[1] += kr * kr;
            if (++hopN_ >= hop_) { block((hopAcc_[0] + hopAcc_[1]) / hopN_); hopAcc_[0] = hopAcc_[1] = 0; hopN_ = 0; report(); }
        }
    }
private:
    static constexpr int kRing = 300;                       // 30 s of 100 ms blocks (short-term needs 30)
    static constexpr int kBins = 800;                       // -70 .. +10 LUFS in 0.1 LU steps
    static double lufs(double ms) { return ms > 0 ? -0.691 + 10 * std::log10(ms) : -INFINITY; }
    double kw(int ch, double x) {
        double* z = z_[ch]; double y = s1_[0] * x + z[0]; z[0] = s1_[1] * x - s1_[3] * y + z[1]; z[1] = s1_[2] * x - s1_[4] * y;
        const double w = y; y = s2_[0] * w + z[2]; z[2] = s2_[1] * w - s2_[3] * y + z[3]; z[3] = s2_[2] * w - s2_[4] * y; return y;
    }
    void block(double ms) {
        ring_[ringN_ % kRing] = ms; ringN_++;
        if (ringN_ >= 4) {   // 400 ms gating block every 100 ms
            double s = 0; for (int k = 1; k <= 4; k++) s += ring_[(ringN_ - k) % kRing]; s /= 4;
            const double l = lufs(s); if (l > -70) { int b = (int)((l + 70) * 10); b = std::min(kBins - 1, std::max(0, b)); hist_[b] += s; histN_++; histCnt_[b]++; }
        }
    }
    double mean(int k) const { const int c = std::min<long>(k, ringN_); if (!c) return 0; double s = 0; for (int i = 1; i <= c; i++) s += ring_[(ringN_ - i) % kRing]; return s / c; }
    void report() {
        m_.store(ringN_ >= 4 ? lufs(mean(4)) : -INFINITY); s_.store(ringN_ >= 30 ? lufs(mean(30)) : (ringN_ >= 4 ? lufs(mean((int)ringN_)) : -INFINITY));
        double sum = 0; long cnt = 0; for (int b = 0; b < kBins; b++) if (histCnt_[b]) { sum += hist_[b]; cnt += histCnt_[b]; }
        double integ = -INFINITY;
        if (cnt) { const double gate = lufs(sum / cnt) - 10; double s2 = 0; long c2 = 0; for (int b = 0; b < kBins; b++) if (histCnt_[b] && (b / 10.0 - 70) > std::max(-70.0, gate) - 0.05) { s2 += hist_[b]; c2 += histCnt_[b]; } if (c2) integ = lufs(s2 / c2); }
        i_.store(integ); pl_.store(peakL_ > 0 ? 20 * std::log10(peakL_) : -INFINITY); pr_.store(peakR_ > 0 ? 20 * std::log10(peakR_) : -INFINITY); gr_.store(grMax_);
        peakL_ = peakR_ = 0; grMax_ = 0;
    }
    std::atomic<float> p_[6]; std::atomic<bool> resetReq_{ false };
    std::atomic<double> m_{ -INFINITY }, s_{ -INFINITY }, i_{ -INFINITY }, pl_{ -INFINITY }, pr_{ -INFINITY }, gr_{ 0 };
    double sr_ = 44100; int la_ = 66, wp_ = 0; std::vector<float> dl_, dr_, det_; double g_ = 1, en_ = 0;
    double s1_[5] = {}, s2_[5] = {}, z_[2][4] = {}; int hop_ = 4410, hopN_ = 0; double hopAcc_[2] = {};
    std::vector<double> ring_; long ringN_ = 0; double hist_[kBins] = {}; long histCnt_[kBins] = {}; long histN_ = 0; double peakL_ = 0, peakR_ = 0, grMax_ = 0;
};

} // namespace vv
