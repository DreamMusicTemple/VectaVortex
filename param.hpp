// Sample-accurate parameter automation timeline mirroring Web Audio's AudioParam semantics
// (setValueAtTime, linearRampToValueAtTime, setTargetAtTime, setValueCurveAtTime, cancelScheduledValues).
// Fixed capacity, no allocation. Times are in seconds on the engine clock.
#pragma once
#include <cmath>
#include <algorithm>

namespace vv {
class Param {
public:
    enum Kind { kSet, kLinear, kTarget, kCurve };
    struct Ev { Kind kind; double time, v, tc, dur; const float* curve; int curveN; double v0; };
    static constexpr int kCap = 12;
    explicit Param(double v = 0) : intrinsic_(v) {}
    void setIntrinsic(double v) { intrinsic_ = (double)(float)v; }   // the .value setter with no events pending
    double intrinsic() const { return intrinsic_; }
    void clear() { n_ = 0; }
    void setValueAtTime(double v, double t) { insert({kSet, t, v, 0, 0, nullptr, 0, 0}); }
    void linearRampToValueAtTime(double v, double t, double now) {
        if (n_ == 0) insert({kSet, now, intrinsic_, 0, 0, nullptr, 0, 0});
        insert({kLinear, t, v, 0, 0, nullptr, 0, 0});
    }
    void setTargetAtTime(double target, double t, double tc, double now = 0) {
        (void)now;
        Ev e{kTarget, t, target, tc, 0, nullptr, 0, 0}; e.v0 = valueAt(t); insert(e);
    }
    void setValueCurveAtTime(const float* c, int cn, double t, double dur) { insert({kCurve, t, c[cn - 1], 0, dur, c, cn, 0}); }
    // Removes every event at or after t.
    void cancelScheduledValues(double t) { int w = 0; for (int i = 0; i < n_; i++) if (ev_[i].time < t) ev_[w++] = ev_[i]; n_ = w; }
    // Value at time t. Queries are expected to be (mostly) increasing in t.
    double valueAt(double t) const {
        if (n_ == 0) return intrinsic_;
        int i = n_ - 1; while (i >= 0 && ev_[i].time > t) i--;
        // i = last event with time <= t, or -1
        if (i < 0) {
            // before first event; a first linear ramp interpolates from the intrinsic value
            return intrinsic_;
        }
        const Ev& e = ev_[i];
        // a following linear ramp governs the segment starting at e
        if (i + 1 < n_ && ev_[i + 1].kind == kLinear) {
            const Ev& nx = ev_[i + 1]; double v1 = endValue(i), t1 = endTime(i);
            double span = nx.time - t1;
            if (span <= 0) return nx.v;
            return v1 + (nx.v - v1) * ((t - t1) / span);
        }
        switch (e.kind) {
            case kSet: case kLinear: return e.v;
            case kTarget: return e.v + (e.v0 - e.v) * std::exp(-(t - e.time) / e.tc);
            case kCurve: {
                if (t >= e.time + e.dur) return e.curve[e.curveN - 1];
                double pos = (e.curveN - 1) / e.dur * (t - e.time); int k = (int)pos; double f = pos - k;
                if (k + 1 >= e.curveN) return e.curve[e.curveN - 1];
                return e.curve[k] + (e.curve[k + 1] - e.curve[k]) * f;
            }
        }
        return e.v;
    }
    int eventCount() const { return n_; }
private:
    double endValue(int i) const {
        const Ev& e = ev_[i];
        if (e.kind == kTarget) return e.v + (e.v0 - e.v) * std::exp(-(ev_[i + 1].time - e.time) / e.tc);
        return e.v;
    }
    double endTime(int i) const { const Ev& e = ev_[i]; return e.kind == kCurve ? e.time + e.dur : e.time; }
    void insert(Ev e) {
        e.v = (double)(float)e.v;   // AudioParam event values are float32
        if (n_ == kCap) { for (int i = 1; i < n_; i++) ev_[i - 1] = ev_[i]; n_--; }
        int pos = n_; while (pos > 0 && ev_[pos - 1].time > e.time) pos--;
        for (int i = n_; i > pos; i--) ev_[i] = ev_[i - 1];
        ev_[pos] = e; n_++;
    }
    Ev ev_[kCap]; int n_ = 0; double intrinsic_ = 0;
};
}
