// Vektur Vortex DSP core - shared primitives.
// Rules for everything under Core/: C++17, no JUCE/Apple dependencies, no allocation or locks in process(),
// all memory sized in prepare(). Scalars are double and stored buffers are float so results track the
// JavaScript AudioWorklet reference (Float32Array storage, double arithmetic).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace vv {

constexpr double kTwoPi = 6.283185307179586;

inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Power-of-two circular buffer. read-then-push gives a delay of exactly d samples (matches RvBuf in the web app).
struct RvBuf {
    std::vector<float> b;
    uint32_t m = 0, w = 0;
    void init(int n) {
        uint32_t s = 16;
        while (s < (uint32_t)n + 8u) s <<= 1;
        b.assign(s, 0.0f);
        m = s - 1;
        w = 0;
    }
    void clear() { std::fill(b.begin(), b.end(), 0.0f); w = 0; }
    inline void push(double x) { b[w] = (float)x; w = (w + 1) & m; }
    inline double tap(double d) const {
        if (d < 1) d = 1;
        const int fl = (int)d;
        const double fr = d - fl;
        const uint32_t i = (w - (uint32_t)fl) & m;
        const double a = b[i], c = b[(i - 1) & m];
        return a + (c - a) * fr;
    }
};

// Linear-interpolated delay line with an arbitrary (non power-of-two) length (matches DelayLine in the web app).
struct DelayLine {
    std::vector<float> buf;
    int pos = 0, size = 1;
    void init(int maxSamples) { size = maxSamples < 1 ? 1 : maxSamples; buf.assign(size, 0.0f); pos = 0; }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); pos = 0; }
    inline void write(double x) { buf[pos] = (float)x; }
    inline double readInterpolated(double delaySamples) const {
        double rp = pos - delaySamples;
        while (rp < 0) rp += size;
        const double fl = std::floor(rp);
        const int i0 = (int)fl % size, i1 = (i0 + 1) % size;
        const double frac = rp - fl;
        return buf[i0] + ((double)buf[i1] - (double)buf[i0]) * frac;
    }
    inline void advance() { pos = (pos + 1) % size; }
};

} // namespace vv
