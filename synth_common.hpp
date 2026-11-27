// Shared helpers for the native synth engine (voices, layers, master).
#pragma once
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <map>
#include <vector>
#include "dsp_common.hpp"
#include "param.hpp"
#include "voice_dsp.hpp"
#include "model.hpp"
#include "power.hpp"

namespace vv {

constexpr int kQuantum = 128;   // internal processing block = the Web Audio render quantum
static_assert(kQuantum % 64 == 0, "the DX7 core renders in 64-sample blocks");

// StereoPanner, mono input: gains for a pan value in [-1, 1].
inline void monoPanGains(double pan, double& gl, double& gr) {
    pan = std::max(-1.0, std::min(1.0, pan));
    const double x = (pan * 0.5 + 0.5) * 1.5707963267948966;
    gl = std::cos(x); gr = std::sin(x);
}

// setTargetAtTime-style one-pole smoother (exact exponential approach, restartable).
struct Smoother {
    double v = 1, target = 1, a = 0;   // a = exp(-1/(sr*tc))
    void set(double value) { v = target = value; }
    void setTarget(double t, double sr, double tc) { target = t; a = std::exp(-1.0 / (sr * tc)); }
    inline double tick() { v = target + (v - target) * a; return v; }
    // advance n samples at once (closed form); returns the value BEFORE advancing (what a k-rate param reads at the quantum start)
    inline double advance(int n) { const double before = v; v = target + (v - target) * std::pow(a, (double)n); return before; }
};

// A ConstantSource.offset driven by setTargetAtTime(target, now, tau): exact exponential approach, evaluated at any time.
struct ModSlot {
    double v0 = 0, tgt = 0, t0 = 0, tau = 0.02;
    inline double at(double t) const { if (v0 == tgt) return tgt; const double el = t - t0; if (el > gModSettleTaus.load(std::memory_order_relaxed) * tau) return tgt; return tgt + (v0 - tgt) * std::exp(-el / tau); }   // Battery Saving: a glide older than N time constants has arrived
    inline void set(double target, double now, double tc) { v0 = at(now); tgt = (double)(float)target; t0 = now; tau = tc; }
    inline void snap(double v) { v0 = tgt = (double)(float)v; }
};

// Instantaneous pitch offset of a ConstantSource that feeds a frequency/detune param: glide ramp + live (bend/drift/mod) offset.
// Mono history of one layer's pre-bus signal (the web app's xmodTap -> 3 ms DelayNode). Read with a fractional absolute-sample position
// (linear interpolation, like Chromium's DelayNode). The tap is delayed by 3 ms, which is longer than one 128-frame quantum at >= 43 kHz,
// so a quantum only ever reads samples written by earlier quanta.
struct TapRing {
    static constexpr int N = 16384;
    float d[N]; uint64_t n = 0; double sr = 48000;
    TapRing() { reset(); }
    void reset() { for (int i = 0; i < N; i++) d[i] = 0; n = 0; }
    inline void push(float v) { d[n & (N - 1)] = v; n++; }
    inline double sampleAt(double p) const {   // p = absolute sample position
        if (p < 0) return 0;
        const double rp = std::floor(p + 0.5); const double q = std::fabs(p - rp) < 1e-6 ? rp : p;
        const uint64_t i0 = (uint64_t)std::floor(q); const double f = q - (double)i0;
        if (i0 + 1 >= n + 1 || n - std::min<uint64_t>(n, i0) > (uint64_t)(N - 2)) return 0;
        const double a = (i0 < n) ? d[i0 & (N - 1)] : 0.0;
        if (f == 0) return a;
        const double b = (i0 + 1 < n) ? d[(i0 + 1) & (N - 1)] : 0.0;
        return a * (1 - f) + b * f;
    }
    inline double delayed(double t) const { return sampleAt(t * sr - 0.003 * sr); }
};

inline double evalCurveY(const std::vector<CurvePt>& pts, double p);   // modulation.hpp - the ONE curve evaluator shared by LFO shapes, filter envelope shape and pitch envelopes
// Round 11 pitch envelope runtime. Built once at note-on (no allocation: fixed table) from the same evalCurveY the editor draws, so the displayed curve IS the modulation.
// After the curve's `dur` the last value holds. Read per sample (control-rate table interpolation, no clicks: the table is continuous and the curve is time-based).
struct PegRt {
    static constexpr int N = 256;
    bool on = false; double t0 = 0, dur = 1, amount = 0; bool bip = true; float tab[N + 1];
    void build(const PegCfg& c, double t, bool legatoJoin) {
        on = false; if (!c.on || c.pts.size() < 2 || std::fabs(c.amount) < 1e-9) return;
        amount = clampd(c.amount, -48, 48); bip = c.bipolar; dur = std::max(0.01, c.time);
        for (int i = 0; i <= N; i++) tab[i] = (float)clampd(evalCurveY(c.pts, (double)i / N), 0.0, 1.0);
        t0 = (legatoJoin && !c.retrig) ? t - dur * 2 : t;   // legato without retrigger joins at the resting pitch (curve end)
        on = true;
    }
    inline double semis(double t) const {
        double u = (t - t0) / dur; u = u < 0 ? 0 : (u > 1 ? 1 : u);
        const double x = u * N; const int i = (int)x < N ? (int)x : N - 1; const double f = x - i, y = tab[i] + (tab[i + 1] - tab[i]) * f;
        return bip ? (y - 0.5) * 2 * amount : y * amount;
    }
    inline double mul(double t) const { return on ? std::exp2(semis(t) / 12.0) : 1.0; }
};
struct PitchOffset {
    const TapRing* xr[4] = {nullptr, nullptr, nullptr, nullptr}; double xd[4] = {0, 0, 0, 0}; int nx = 0;   // cross-mod FM inputs (audio-rate)
    double gStart = 0, gT0 = 0, gDur = 0, live = 0;
    ModSlot lm;                      // modulation / bend / drift / retune offset (tau 20 ms)
    double drift = 0, driftVal = 0, built = 0; int kind = 0, osc = 0;
    inline double at(double t) const {
        double g = 0;
        if (gDur > 0 && t < gT0 + gDur) { g = t <= gT0 ? gStart : gStart * (1.0 - (t - gT0) / gDur); }
        double r = g + live + lm.at(t);
        for (int k = 0; k < nx; k++) r += xd[k] * xr[k]->delayed(t);
        return r;
    }
};

// Plain sine oscillator (the Web Audio OscillatorNode 'sine' used for sub oscillators).
// Round 22: shaped sub oscillator. shape 0 = sine, 1 = triangle, 2 = square (polyBLEP). Triangle and square are level-matched to the sine
// (x1.2 / x0.55) exactly as the web makeSub(), so the choice changes the tone, not the loudness. The phase lives in SineOsc::phase.
inline double subPolyBlep(double t, double dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}
struct SineOsc {
    double phase = 0;
    inline double tick(double freq, double sr) {
        const double o = std::sin(kTwoPi * phase);
        phase += freq / sr; phase -= std::floor(phase);
        return o;
    }
};
inline double subShapedTick(SineOsc& o, int shape, double freq, double sr) {
    if (shape <= 0) return o.tick(freq, sr);
    const double ph = o.phase, dt = std::fabs(freq) / sr; double out;
    if (shape == 1) { out = (ph < 0.25) ? 4.0 * ph : (ph < 0.75) ? 2.0 - 4.0 * ph : 4.0 * ph - 4.0; out *= 1.2; }
    else { out = (ph < 0.5) ? 1.0 : -1.0; double p2 = ph + 0.5; p2 -= std::floor(p2); out += subPolyBlep(ph, dt) - subPolyBlep(p2, dt); out *= 0.55; }
    o.phase += freq / sr; o.phase -= std::floor(o.phase);
    return out;
}

inline double midiToFreq(double note, double a4) { return a4 * std::pow(2.0, (note - 69.0) / 12.0); }

// Deterministic stand-in for Math.random (used where the web app draws randoms) - seedable for parity tests.
struct JsRandom {
    uint32_t s = 777;
    inline double next() { s = s * 1664525u + 1013904223u; return (double)s / 4294967296.0; }
};

struct PLocks {
    bool hasPosition = false, hasCutoff = false, hasLevel = false, hasSamplePosition = false, hasSampleEnd = false, hasSamplePan = false, hasSampleReverse = false;
    double position = 0, cutoff = 0, level = 1, samplePosition = 0, sampleEndPosition = 1, samplePan = 0; bool sampleReverse = false;
    // arpeggiator voice split: arpMode 0 = normal, 1 = arp voice (builds only the oscillators in arpMask), 2 = direct voice while the arp runs (builds the others)
    int arpMode = 0; unsigned arpMask = 0;
    // MPC pad voices: pitchNote >= 0 plays at that MIDI pitch, ignoring keyboard zones (the voice itself is keyed by the unique `note` passed to noteOn).
    int pitchNote = -1;
    // New Note Next / Random Sample: when >= 0 a sampler voice builds only this slot (chosen per new note by Synth::pickAdvanceSlot, or fixed by an MPC-recorded roll note)
    int slot = -1;
};

}  // namespace vv
