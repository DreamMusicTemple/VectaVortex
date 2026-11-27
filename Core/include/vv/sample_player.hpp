// Sample playback equivalent to Web Audio's AudioBufferSourceNode (linear interpolation, loop points, start offset / duration),
// plus the two derived buffers the web app builds per note (ping-pong and reversed slice) done as index maths - no allocation.
#pragma once
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>

namespace vv {

struct SampleBuf {
    int sr = 48000, ch = 1, len = 0;
    std::vector<float> d[2];
    float at(int c, long i) const { if (i < 0 || i >= len) return 0.f; return d[c < ch ? c : 0][(size_t)i]; }
};

class SamplePlayer {
public:
    enum Mode { kPlain = 0, kPingPong = 1, kReverseSlice = 2 };
    // sIdx/segLen are only used by the derived modes. startSec/durSec follow AudioBufferSourceNode.start(when, offset, duration).
    void start(const SampleBuf* b, Mode mode, long sIdx, long segLen, double ctxSr, bool loop, double loopStartSec, double loopEndSec, bool loopPointsSet,
               double offsetSec, double durationSec /* <0 none */, double baseRate = 1.0) {
        buf_ = b; mode_ = mode; sIdx_ = sIdx; seg_ = std::max(1L, segLen); ctxSr_ = ctxSr; done_ = false; loop_ = loop;
        nb_ = mode == kPlain ? b->len : mode == kPingPong ? seg_ * 2 : seg_;
        bufSr_ = b->sr;
        loopPoints_ = loopPointsSet && loopStartSec >= 0 && loopEndSec > 0 && loopStartSec < loopEndSec;
        loopStart_ = loopStartSec * bufSr_; loopEnd_ = loopEndSec * bufSr_;
        // grain
        vri_ = std::max(0.0, std::min((double)nb_, offsetSec * bufSr_));
        grainEnd_ = durationSec >= 0 ? std::min((double)nb_, vri_ + durationSec * bufSr_) : (double)nb_;
        frames_ = 0; limit_ = (!loop_ && durationSec >= 0) ? (long)std::ceil(durationSec / baseRate * ctxSr - 1e-9) : -1;
        if (loop_) {   // looping sources ignore the duration; an offset past the loop end wraps to the loop start
            double vend = nb_, vstart = 0;
            if (loopPoints_) { vstart = loopStart_; vend = std::min(loopEnd_, (double)nb_); }
            if (vri_ >= vend) vri_ = vstart;
        }
    }
    bool done() const { return done_; }
    // Playback position as a fraction (0..1) of the whole source buffer, or -1 when idle: drives the editor's playhead. Audio thread, no allocation.
    double headFrac() const {
        if (done_ || !buf_ || buf_->len <= 0) return -1;
        const double n = (double)buf_->len; double p;
        if (mode_ == kPlain) p = vri_; else if (mode_ == kPingPong) p = (double)sIdx_ + (vri_ < (double)seg_ ? vri_ : (double)(2 * seg_ - 1) - vri_); else p = (double)(sIdx_ + seg_ - 1) - vri_;
        return p < 0 ? 0 : p > n ? 1 : p / n;
    }
    inline void tick(double rate, double& l, double& r) {
        l = r = 0;
        if (done_ || !buf_) return;
        if (limit_ >= 0 && frames_ >= limit_) { done_ = true; return; }
        frames_++;
        double vend = nb_, vstart = 0;
        if (loop_) { if (loopPoints_) { vstart = loopStart_; vend = std::min(loopEnd_, (double)nb_); } }
        else vend = grainEnd_;
        if (vri_ < 0) {   // a negative playbackRate (a glide whose start offset overshoots the base rate) runs the source backwards: a one-shot ends at the start, a loop wraps to its end
            if (!loop_) { done_ = true; return; }
            const double span = vend - vstart; vri_ = span > 0 ? vend - std::fmod(vstart - vri_, span) : vstart;
        }
        if (loop_) { if (vri_ >= vend) { const double span = vend - vstart; if (span > 0) vri_ = vstart + std::fmod(vri_ - vstart, span); else vri_ = vstart; } }
        else if (vri_ >= vend) { done_ = true; return; }
        const long i1 = (long)vri_; const double fr = vri_ - (double)i1;
        long i2 = i1 + 1;
        if (i2 >= nb_) { if (loop_) i2 = (long)(vri_ + 1 - vend + vstart); else i2 = i1; }
        for (int c = 0; c < buf_->ch && c < 2; c++) {
            const double s1 = get(c, i1), s2 = get(c, i2);
            const double v = (1.0 - fr) * s1 + fr * s2;
            if (c == 0) l = v; else r = v;
        }
        if (buf_->ch == 1) r = l;
        vri_ += rate * bufSr_ / ctxSr_;
    }
    long frames_ = 0, limit_ = -1;
    int channels() const { return buf_ ? buf_->ch : 1; }
private:
    inline double get(int c, long i) const {
        if (i < 0 || i >= nb_) return 0.0;
        switch (mode_) {
            case kPlain: return buf_->d[c][(size_t)i];
            case kPingPong: { const long k = i < seg_ ? i : (2 * seg_ - 1 - i); return buf_->at(c, sIdx_ + k); }
            default: return buf_->at(c, sIdx_ + (seg_ - 1 - i));
        }
    }
    const SampleBuf* buf_ = nullptr; Mode mode_ = kPlain; long sIdx_ = 0, seg_ = 1, nb_ = 0; double ctxSr_ = 48000, bufSr_ = 48000;
    bool loop_ = false, loopPoints_ = false, done_ = true; double loopStart_ = 0, loopEnd_ = 0, vri_ = 0, grainEnd_ = 0;
};
}  // namespace vv
