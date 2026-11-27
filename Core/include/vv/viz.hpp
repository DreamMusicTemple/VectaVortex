// Plot maths for the "pro" filter-response and envelope displays (mirrors the web app's computeFilterMagnitudeDb / vvProEnv).
// Shared by the JUCE UI (Juce/Source/ProPlot.h) and documented for the SwiftUI port (Apple/Shared/ProPlots.swift re-implements the same formulas).
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace vv { namespace viz {

constexpr double kDbLo = -36.0, kDbSpan = 48.0;       // filter plot: -36..+12 dB
constexpr double kHzLo = 20.0, kHzHi = 20000.0;       // 3 decades, log axis

inline double hzAt(double x01) { return kHzLo * std::pow(kHzHi / kHzLo, x01); }
inline double xOfHz(double hz) { return std::log(hz / kHzLo) / std::log(kHzHi / kHzLo); }

inline double bump(double ratio, double reso, double k, double w = 4.0) { const double l = std::log2(ratio); return std::exp(-l * l * w) * (reso / 130.0) * k; }

/// Magnitude (dB) of the voice filter at `freq` for the web's drawn response model. `fmt` = formant ratios (formant type only).
inline double filterMagnitudeDb(const std::string& type, double cutoff, double reso, double freq, const std::vector<double>& fmt = {}) {
    const double ratio = freq / cutoff, safe = std::max(0.001, ratio); double m;
    if (type == "onepole6db") m = 1.0 / std::sqrt(1 + ratio * ratio);
    else if (type == "twopole12db" || type == "statevariable") { m = 1.0 / std::sqrt(1 + std::pow(ratio, 4)); m *= 1 + bump(ratio, reso, 3.5); }
    else if (type == "comb") m = 0.6 + 0.4 * std::sin(ratio * 3.14159265358979 * 6) * std::min(1.0, reso / 40);
    else if (type == "highpass") { m = 1.0 / std::sqrt(1 + std::pow(1 / safe, 4)); m *= 1 + bump(ratio, reso, 3.5); }
    else if (type == "notch") { const double q = 2 + (reso / 130) * 10, bp = 1.0 / std::sqrt(1 + std::pow(q * (ratio - 1 / safe), 2)); m = std::sqrt(std::max(0.0001, 1 - bp * bp)); }
    else if (type == "isem") { m = 1.0 / std::sqrt(1 + std::pow(ratio, 4)); m *= 1 + bump(ratio, reso, 2.5); }
    else if (type == "bandpass") { const double q = 2 + (reso / 130) * 10; m = 1.0 / std::sqrt(1 + std::pow(q * (ratio - 1 / safe), 2)); }
    else if (type == "formant") {
        const std::vector<double> r = fmt.empty() ? std::vector<double>{ 1, 1.74, 3.71 } : fmt; const double q = 4 + (reso / 130) * 14; double sum = 0;
        for (size_t i = 0; i < r.size(); ++i) { const double rr = freq / (cutoff * r[i]), pk = 1.0 / std::sqrt(1 + std::pow(q * (rr - 1 / std::max(0.001, rr)), 2)); sum += pk * (i == 0 ? 1 : 0.7); }
        m = std::min(1.0, sum * 0.6);
    }
    else if (type == "sid8580") { m = 1.0 / std::sqrt(1 + std::pow(ratio, 4)); m *= 1 + bump(ratio, reso, 4.0); }
    else if (type == "sid6581") { m = 1.0 / std::sqrt(1 + std::pow(ratio, 4)); m *= 1 + bump(ratio, reso, 5.5); if (ratio < 0.4) m *= 1 - (reso / 130) * 0.3; }
    else { m = 1.0 / std::sqrt(1 + std::pow(ratio, 8)); m *= 1 + bump(ratio, reso, 5.0, 3.0); }   // moog4pole / diode4pole / lpg / default
    return 20 * std::log10(std::max(0.001, m));
}

/// ADSR level (0..1) at time t for the plot: attack, decay to sustain, a short sustain hold, release (same layout as the web's vvProEnv).
struct EnvShape { double a, d, s, r, hold, total; };
inline EnvShape envShape(double a, double d, double s, double r) {
    EnvShape e; e.a = std::max(0.001, a); e.d = std::max(0.001, d); e.s = std::min(1.0, std::max(0.0, s)); e.r = std::max(0.001, r);
    e.hold = std::max(0.1, 0.35 * (e.a + e.d)); e.total = e.a + e.d + e.hold + e.r; return e;
}
inline double envLevel(const EnvShape& e, double t) {
    if (t < e.a) return t / e.a;
    if (t < e.a + e.d) return 1 - (1 - e.s) * ((t - e.a) / e.d);
    if (t < e.a + e.d + e.hold) return e.s;
    return e.s * std::max(0.0, 1 - (t - e.a - e.d - e.hold) / e.r);
}
/// A "nice" time-grid step so the plot shows <= 8 labelled divisions.
inline double envGridStep(double total) { static const double st[] = { 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5 }; for (double s : st) if (total / s <= 8) return s; return 5; }

} }
