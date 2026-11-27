// DAW-style master recorder and click track (web "RECORDER" block: record / stop / settings with bit depth 16|24, sample rate 44.1|48|96 kHz,
// click Off | Beep | Rimshot). Needed for standalone use, where there is no DAW to record the app.
//  - DawRecorder: the audio thread pushes the master output into a preallocated single-producer ring (no allocation, no locks); a NON-audio thread
//    calls drain() regularly to move it into the take, and finish() to resample and write a PCM WAV.
//  - Metronome: added to the output AFTER the recorder tap, so (as on hardware, and in the web app) the click is heard but never recorded.
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

namespace vv {

class DawRecorder {
public:
    static constexpr int kRing = 1 << 18;                 // frames of slack between the audio thread and the drainer (~5 s at 48 kHz)
    void prepare(double sr, double maxSeconds = 600.0) {   // not realtime
        stop(); sr_ = sr; ringL_.assign(kRing, 0.f); ringR_.assign(kRing, 0.f); wr_.store(0); rd_ = 0; dropped_.store(0);
        maxFrames_ = (int64_t)(maxSeconds * sr); takeL_.clear(); takeR_.clear(); state_.store(0);
    }
    void start() {                                         // not realtime: clears the previous take
        if (ringL_.empty()) return; state_.store(0); wr_.store(0); rd_ = 0; dropped_.store(0); takeL_.clear(); takeR_.clear(); state_.store(1, std::memory_order_release);
    }
    void stop() { if (state_.load() == 1) state_.store(2, std::memory_order_release); }
    int state() const { return state_.load(std::memory_order_acquire); }   // 0 idle, 1 recording, 2 stopped (take ready after a final drain)
    bool recording() const { return state() == 1; }
    // audio thread
    void push(const float* l, const float* r, int n) {
        if (state_.load(std::memory_order_acquire) != 1) return;
        uint64_t w = wr_.load(std::memory_order_relaxed); const uint64_t rd = rdPub_.load(std::memory_order_acquire);
        for (int i = 0; i < n; i++) {
            if (w - rd >= (uint64_t)kRing) { dropped_.fetch_add(1, std::memory_order_relaxed); continue; }
            ringL_[w & (kRing - 1)] = l[i]; ringR_[w & (kRing - 1)] = r[i]; w++;
        }
        wr_.store(w, std::memory_order_release);
    }
    // non-audio thread. Returns the take length in frames. Stops the recording itself when the length limit is reached.
    int64_t drain() {
        const uint64_t w = wr_.load(std::memory_order_acquire);
        while (rd_ < w) { takeL_.push_back(ringL_[rd_ & (kRing - 1)]); takeR_.push_back(ringR_[rd_ & (kRing - 1)]); rd_++; }
        rdPub_.store(rd_, std::memory_order_release);
        if ((int64_t)takeL_.size() >= maxFrames_) stop();
        return (int64_t)takeL_.size();
    }
    int64_t frames() const { return (int64_t)takeL_.size(); }
    int64_t dropped() const { return dropped_.load(); }
    double sampleRate() const { return sr_; }
    const std::vector<float>& left() const { return takeL_; }
    const std::vector<float>& right() const { return takeR_; }
    void clearTake() { takeL_.clear(); takeR_.clear(); takeL_.shrink_to_fit(); takeR_.shrink_to_fit(); state_.store(0); }

    // Resample the take to targetRate (windowed sinc, 8 zero crossings each side) and write a PCM WAV (16 or 24 bit, stereo).
    static std::vector<float> resample(const std::vector<float>& in, double srcRate, double dstRate) {
        if (in.empty() || std::fabs(srcRate - dstRate) < 0.5) return in;
        const double ratio = dstRate / srcRate; const int64_t outN = (int64_t)std::ceil((double)in.size() * ratio); std::vector<float> out((size_t)outN);
        const double cut = std::min(1.0, ratio); const int H = 8; const int64_t n = (int64_t)in.size();
        for (int64_t o = 0; o < outN; o++) {
            const double pos = (double)o / ratio; const int64_t c = (int64_t)std::floor(pos); double acc = 0, norm = 0;
            for (int k = -H + 1; k <= H; k++) {
                const int64_t idx = c + k; const double d = (pos - (double)idx) * cut; const double wdw = 0.5 + 0.5 * std::cos(M_PI * (pos - (double)idx) / H);
                const double s = std::fabs(d) < 1e-9 ? 1.0 : std::sin(M_PI * d) / (M_PI * d); const double wt = s * wdw * cut; norm += wt;
                if (idx >= 0 && idx < n) acc += wt * in[(size_t)idx];
            }
            out[(size_t)o] = (float)(norm != 0 ? acc / norm : 0);
        }
        return out;
    }
    static std::vector<uint8_t> toWav(const std::vector<float>& L, const std::vector<float>& R, int sampleRate, int bitDepth) {
        const int bps = bitDepth == 24 ? 3 : 2; const uint32_t len = (uint32_t)std::min(L.size(), R.size()), blockAlign = 2 * bps, dataSize = len * blockAlign;
        std::vector<uint8_t> w(44 + (size_t)dataSize);
        auto u32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; i++) w[o + i] = (uint8_t)(v >> (8 * i)); }; auto u16 = [&](size_t o, uint16_t v) { w[o] = (uint8_t)v; w[o + 1] = (uint8_t)(v >> 8); };
        std::memcpy(&w[0], "RIFF", 4); u32(4, 36 + dataSize); std::memcpy(&w[8], "WAVEfmt ", 8); u32(16, 16); u16(20, 1); u16(22, 2); u32(24, (uint32_t)sampleRate);
        u32(28, (uint32_t)sampleRate * blockAlign); u16(32, (uint16_t)blockAlign); u16(34, (uint16_t)(bps * 8)); std::memcpy(&w[36], "data", 4); u32(40, dataSize);
        size_t o = 44;
        for (uint32_t i = 0; i < len; i++) for (int c = 0; c < 2; c++) {
            const double s = std::max(-1.0, std::min(1.0, (double)(c ? R[i] : L[i])));
            if (bps == 3) { const int32_t v = (int32_t)std::lround(s < 0 ? s * 8388608.0 : s * 8388607.0); w[o++] = (uint8_t)(v & 0xFF); w[o++] = (uint8_t)((v >> 8) & 0xFF); w[o++] = (uint8_t)((v >> 16) & 0xFF); }
            else { const int16_t v = (int16_t)std::lround(s < 0 ? s * 32768.0 : s * 32767.0); w[o++] = (uint8_t)(v & 0xFF); w[o++] = (uint8_t)((v >> 8) & 0xFF); }
        }
        return w;
    }
private:
    double sr_ = 48000; int64_t maxFrames_ = 0; std::vector<float> ringL_, ringR_, takeL_, takeR_;
    std::atomic<uint64_t> wr_{0}, rdPub_{0}; uint64_t rd_ = 0; std::atomic<int64_t> dropped_{0}; std::atomic<int> state_{0};
};

// Click track: 'beep' = 1800 Hz sine, gain 0.25 falling exponentially to 0.001 over 60 ms (stopped at 80 ms); 'rimshot' = 45 ms squared-decay noise burst through a
// 2200 Hz / Q 4 band-pass, gain 0.5 falling exponentially to 0.001. Beats come every 60/bpm seconds counted from start().
class Metronome {
public:
    enum Mode { Off = 0, Beep = 1, Rimshot = 2 };
    void prepare(double sr) {   // not realtime
        sr_ = sr; const int n = (int)std::ceil(sr * 0.045); burst_.assign((size_t)n, 0.f); uint32_t s = 12345;
        for (int i = 0; i < n; i++) { s = s * 1664525u + 1013904223u; const double r = (double)s / 4294967296.0 * 2 - 1, e = 1.0 - (double)i / n; burst_[(size_t)i] = (float)(r * e * e); }
        const double w0 = 2 * M_PI * 2200.0 / sr, alpha = std::sin(w0) / (2 * 4.0), a0 = 1 + alpha;
        b0_ = alpha / a0; b2_ = -alpha / a0; a1_ = -2 * std::cos(w0) / a0; a2_ = (1 - alpha) / a0; beatPos_ = 1e18; voicePos_ = -1; restart_.store(false);
    }
    void setMode(int m) { mode_.store(m, std::memory_order_relaxed); }
    int mode() const { return mode_.load(std::memory_order_relaxed); }
    void start() { restart_.store(true, std::memory_order_release); }   // first beat fires immediately (applied on the audio thread)
    // audio thread: adds the click to l/r
    void process(float* l, float* r, int n, double bpm) {
        if (restart_.exchange(false, std::memory_order_acq_rel)) { beatPos_ = 1e18; voicePos_ = -1; }
        const int m = mode_.load(std::memory_order_relaxed); if (m == Off) { voicePos_ = -1; return; }
        const double beat = sr_ * 60.0 / std::max(20.0, bpm);
        for (int i = 0; i < n; i++) {
            if (beatPos_ >= beat) { beatPos_ -= beat; if (beatPos_ > beat) beatPos_ = 0; voicePos_ = 0; z1_ = z2_ = 0; voiceMode_ = m; }
            beatPos_ += 1.0;
            if (voicePos_ >= 0) {
                const double t = (double)voicePos_ / sr_; double v = 0;
                if (voiceMode_ == Beep) { if (t < 0.08) { const double g = t < 0.06 ? 0.25 * std::pow(0.001 / 0.25, t / 0.06) : 0.001; v = g * std::sin(2 * M_PI * 1800.0 * t); } else voicePos_ = -2; }
                else { const int bi = (int)voicePos_; if (bi < (int)burst_.size()) { const double x = burst_[(size_t)bi]; const double y = b0_ * x + z1_; z1_ = -a1_ * y + z2_; z2_ = b2_ * x - a2_ * y; v = y * 0.5 * std::pow(0.001 / 0.5, t / 0.045); } else voicePos_ = -2; }
                if (voicePos_ >= 0) { voicePos_++; l[i] += (float)v; r[i] += (float)v; }
            }
        }
    }
private:
    double sr_ = 48000, beatPos_ = 1e18; int64_t voicePos_ = -1; int voiceMode_ = 1; std::atomic<int> mode_{0}; std::atomic<bool> restart_{true};
    std::vector<float> burst_; double b0_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, z1_ = 0, z2_ = 0;
};

} // namespace vv
