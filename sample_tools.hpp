// Offline sample tools of the web app's slot tool row (non-realtime; run on the UI / a worker thread, never in the audio callback):
//   TIME STRETCH (WSOLA: keeps pitch; modes ratio / pitch-shift-keep-length / fit to beats), AUDIO WARP (per-segment WSOLA between warp markers),
//   SPECTRAL / RESYNTHESIS (STFT band shaping; "granular" engine adds a +-5 % per-band jitter).
// Ports of wsolaStretch, resampleKeepRate, applyStretchToOsc, applyWarpToOsc, spectralResynthesizeBuffer. Spectral "granular" jitter uses a fixed-seed
// generator (the web uses Math.random), so results are repeatable.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace vv { namespace stools {

struct Audio { int sr = 48000; std::vector<std::vector<float>> ch; int length() const { return ch.empty() ? 0 : (int)ch[0].size(); } };
inline double clampd_(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

inline Audio wsolaStretch(const Audio& in, double ratio, int win) {
    const int nCh = (int)in.ch.size(), len = in.length(), sr = in.sr; Audio out; out.sr = sr;
    const int outLen = std::max(1, (int)std::floor(len * ratio + 0.5)); const int hopS = win >> 1; const double hopA = hopS / ratio;
    const int tol = std::max(64, std::min(win >> 2, (int)std::floor(sr * 0.012 + 0.5)));
    const int dec = 4, winD = win / dec, tolD = (tol + dec - 1) / dec;
    std::vector<float> md((size_t)((len + dec - 1) / dec + winD + tolD + hopS + 16), 0.f);
    for (size_t i = 0; i < md.size(); i++) { double s = 0; int n = 0; for (int c = 0; c < nCh; c++) for (int k = 0; k < dec; k++) { const size_t idx = i * dec + k; if (idx < (size_t)len) { s += in.ch[c][idx]; n++; } } md[i] = n ? (float)(s / n) : 0.f; }
    auto M = [&](long i) { return (i >= 0 && i < (long)md.size()) ? md[(size_t)i] : 0.f; };
    std::vector<float> w(win); for (int i = 0; i < win; i++) w[i] = (float)(0.5 - 0.5 * std::cos(2 * 3.141592653589793 * i / win));
    std::vector<std::vector<float>> outs(nCh, std::vector<float>((size_t)outLen + win + 2, 0.f));
    long prevPos = 0; const int frames = (int)std::ceil((double)outLen / hopS) + 1;
    for (int f = 0; f < frames; f++) {
        const long nominal = (long)std::floor(f * hopA + 0.5); long pos = nominal;
        if (f > 0) {
            const long refD = (long)std::floor((prevPos + hopS) / (double)dec + 0.5); double best = -1e30; long bestP = nominal;
            const long loD = std::max(0L, (long)std::floor((nominal - tol) / (double)dec + 0.5)), hiD = (long)std::floor((nominal + tol) / (double)dec + 0.5);
            for (long cand = loD; cand <= hiD; cand++) { double sc = 0; for (int k = 0; k < winD; k += 2) sc += (double)M(cand + k) * M(refD + k); if (sc > best) { best = sc; bestP = cand * dec; } }
            pos = bestP;
        }
        const long op = (long)f * hopS;
        for (int c = 0; c < nCh; c++) { const auto& src = in.ch[c]; auto& dst = outs[c]; for (int i = 0; i < win; i++) { const long si = pos + i; if (si >= 0 && si < len) dst[(size_t)(op + i)] += src[(size_t)si] * w[i]; } }
        prevPos = pos;
    }
    out.ch.resize(nCh); for (int c = 0; c < nCh; c++) out.ch[c].assign(outs[c].begin(), outs[c].begin() + outLen);
    return out;
}
// factor > 1 => shorter + higher pitch (linear interpolation)
inline Audio resampleKeepRate(const Audio& in, double factor) {
    Audio out; out.sr = in.sr; const int len = in.length(); const int n = std::max(1, (int)std::floor(len / factor + 0.5)); out.ch.assign(in.ch.size(), std::vector<float>(n, 0.f));
    for (size_t c = 0; c < in.ch.size(); c++) for (int i = 0; i < n; i++) { const double p = i * factor; const long i0 = (long)std::floor(p); const double fr = p - i0; const float a = i0 < len ? in.ch[c][i0] : 0.f, b = i0 + 1 < len ? in.ch[c][i0 + 1] : a; out.ch[c][i] = (float)(a + (b - a) * fr); }
    return out;
}
struct StretchCfg { enum Mode { Ratio, Pitch, Fit } mode = Ratio; double ratio = 1, semi = 0, beats = 4, bpm = 120; int quality = 1; /*0 perc 1024, 1 normal 2048, 2 smooth 4096*/ };
inline Audio applyStretch(const Audio& orig, const StretchCfg& c) {
    const int win = c.quality == 0 ? 1024 : c.quality == 2 ? 4096 : 2048;
    if (c.mode == StretchCfg::Pitch) { const double p = std::pow(2.0, c.semi / 12); return std::fabs(p - 1) < 1e-4 ? orig : resampleKeepRate(wsolaStretch(orig, p, win), p); }
    const double dur = (double)orig.length() / orig.sr;
    const double ratio = c.mode == StretchCfg::Fit ? clampd_(((c.beats > 0 ? c.beats : 4) * 60.0 / std::max(20.0, c.bpm)) / std::max(1e-9, dur), 0.1, 8) : clampd_(c.ratio, 0.1, 8);
    return std::fabs(ratio - 1) < 1e-4 ? orig : wsolaStretch(orig, ratio, win);
}
struct WarpMarker { double orig = 0, now = 0; };
inline Audio applyWarp(const Audio& orig, std::vector<WarpMarker> markers) {
    markers.push_back({0, 0}); markers.push_back({1, 1}); std::sort(markers.begin(), markers.end(), [](const WarpMarker& a, const WarpMarker& b) { return a.orig < b.orig; });
    const int nCh = (int)orig.ch.size(), total = orig.length();
    const double span = markers.back().orig - markers.front().orig; const int outLen = std::max(1, (int)std::floor(total * (markers.back().now - markers.front().now) / (span != 0 ? span : 1) + 0.5));
    Audio out; out.sr = orig.sr; out.ch.assign(nCh, std::vector<float>((size_t)outLen, 0.f)); long writePos = 0;
    for (size_t i = 0; i + 1 < markers.size(); i++) {
        const auto& a = markers[i]; const auto& b = markers[i + 1];
        const long srcS = (long)std::floor(a.orig * total), srcE = std::max(srcS + 1, (long)std::floor(b.orig * total)); const long dstLen = std::max(1L, (long)std::floor((b.now - a.now) * total + 0.5));
        Audio seg; seg.sr = orig.sr; seg.ch.resize(nCh); for (int c = 0; c < nCh; c++) seg.ch[c].assign(orig.ch[c].begin() + std::min<long>(srcS, total), orig.ch[c].begin() + std::min<long>(srcE, total));
        const double ratio = (double)dstLen / (double)std::max<long>(1, srcE - srcS);
        const Audio st = std::fabs(ratio - 1) < 1e-3 ? seg : wsolaStretch(seg, ratio, 2048);
        for (int c = 0; c < nCh; c++) for (long k = 0; k < dstLen && writePos + k < outLen; k++) out.ch[c][(size_t)(writePos + k)] = k < (long)st.ch[c].size() ? st.ch[c][(size_t)k] : 0.f;
        writePos += dstLen;
    }
    return out;
}

// ---------------------------------------------------------------- spectral
inline void fft(std::vector<double>& re, std::vector<double>& im, bool invert) {
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; i++) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); } }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = (invert ? 1 : -1) * 2 * 3.141592653589793 / (double)len, wr = std::cos(ang), wi = std::sin(ang);
        for (size_t i = 0; i < n; i += len) { double cwr = 1, cwi = 0;
            for (size_t j = 0; j < len / 2; j++) { const double ur = re[i + j], ui = im[i + j], vr = re[i + j + len / 2] * cwr - im[i + j + len / 2] * cwi, vi = re[i + j + len / 2] * cwi + im[i + j + len / 2] * cwr;
                re[i + j] = ur + vr; im[i + j] = ui + vi; re[i + j + len / 2] = ur - vr; im[i + j + len / 2] = ui - vi; const double nwr = cwr * wr - cwi * wi, nwi = cwr * wi + cwi * wr; cwr = nwr; cwi = nwi; } }
    }
    if (invert) for (size_t i = 0; i < n; i++) { re[i] /= (double)n; im[i] /= (double)n; }
}
struct SpectralCfg { bool granular = false, logScale = false; int window = 0; /*0 hann 1 hamming 2 blackman 3 rect*/ std::vector<double> bands = std::vector<double>(8, 1.0); };
inline double spectralWin(int type, int n, int i) {
    const double pi = 3.141592653589793;
    switch (type) { case 1: return 0.54 - 0.46 * std::cos(2 * pi * i / (n - 1)); case 2: return 0.42 - 0.5 * std::cos(2 * pi * i / (n - 1)) + 0.08 * std::cos(4 * pi * i / (n - 1)); case 3: return 1; default: return 0.5 - 0.5 * std::cos(2 * pi * i / (n - 1)); }
}
inline Audio spectralResynth(const Audio& src, const SpectralCfg& cfg) {
    constexpr int N = 2048, HOP = N / 4; const int nCh = (int)src.ch.size(), outLen = src.length(); Audio out; out.sr = src.sr;
    std::vector<double> win(N); for (int i = 0; i < N; i++) win[i] = spectralWin(cfg.window, N, i);
    const int half = N / 2, nBands = (int)cfg.bands.size(); const double nyq = src.sr / 2.0; std::vector<double> gains(half);
    for (int k = 0; k < half; k++) { const double freq = k * nyq / half; double t; if (cfg.logScale) { const double f0 = 20, lf = std::log(std::max(freq, f0) / f0), lmax = std::log(nyq / f0); t = std::min(1.0, std::max(0.0, lf / lmax)); } else t = freq / nyq; gains[k] = cfg.bands[(size_t)std::min(nBands - 1, (int)std::floor(t * nBands))]; }
    uint32_t rs = 987654321u; auto rnd = [&] { rs = rs * 1664525u + 1013904223u; return (rs >> 8) / 16777216.0; };
    out.ch.resize(nCh);
    for (int c = 0; c < nCh; c++) {
        const auto& input = src.ch[c]; std::vector<double> output((size_t)outLen + N, 0.0), re(N), im(N); const int nFrames = std::max(1, (int)std::ceil((double)outLen / HOP));
        for (int f = 0; f < nFrames; f++) {
            const long start = (long)f * HOP;
            for (int i = 0; i < N; i++) { const long si = start + i; re[i] = (si < outLen ? input[(size_t)si] : 0.0) * win[i]; im[i] = 0; }
            fft(re, im, false);
            for (int k = 0; k <= half; k++) { const double jitter = cfg.granular ? (1 + (rnd() * 2 - 1) * 0.05) : 1.0; const double g = gains[(size_t)std::min(k, half - 1)] * jitter; re[k] *= g; im[k] *= g; if (k > 0 && k < half) { re[N - k] = re[k]; im[N - k] = -im[k]; } }
            fft(re, im, true);
            for (int i = 0; i < N; i++) { const size_t o = (size_t)start + i; if (o < output.size()) output[o] += re[i] * win[i]; }
        }
        double peak = 0; for (int i = 0; i < outLen; i++) peak = std::max(peak, std::fabs(output[i])); const double norm = peak > 1e-6 ? std::min(1 / peak, 2.2) : 1;
        out.ch[c].resize(outLen); for (int i = 0; i < outLen; i++) out.ch[c][i] = (float)(output[i] * norm);
    }
    return out;
}

// 16-bit PCM WAV (what the web app embeds in presets: audioBufferToBase64), 1 or 2 channels
inline std::vector<unsigned char> toWav16(const Audio& a) {
    const int nCh = std::max(1, std::min(2, (int)a.ch.size())), len = a.length(); const uint32_t dataBytes = (uint32_t)len * nCh * 2;
    std::vector<unsigned char> w(44 + dataBytes); auto u32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; i++) w[o + i] = (unsigned char)(v >> (8 * i)); }; auto u16 = [&](size_t o, uint16_t v) { w[o] = (unsigned char)v; w[o + 1] = (unsigned char)(v >> 8); };
    for (int i = 0; i < 4; i++) { w[i] = (unsigned char)"RIFF"[i]; w[8 + i] = (unsigned char)"WAVE"[i]; w[12 + i] = (unsigned char)"fmt "[i]; w[36 + i] = (unsigned char)"data"[i]; }
    u32(4, 36 + dataBytes); u32(16, 16); u16(20, 1); u16(22, (uint16_t)nCh); u32(24, (uint32_t)a.sr); u32(28, (uint32_t)a.sr * nCh * 2); u16(32, (uint16_t)(nCh * 2)); u16(34, 16); u32(40, dataBytes);
    size_t o = 44; for (int i = 0; i < len; i++) for (int c = 0; c < nCh; c++) { const double v = std::max(-1.0, std::min(1.0, (double)a.ch[(size_t)c][(size_t)i])); const int16_t q = (int16_t)(v < 0 ? v * 32768.0 : v * 32767.0); w[o] = (unsigned char)(q & 0xff); w[o + 1] = (unsigned char)((q >> 8) & 0xff); o += 2; }
    return w;
}

// IMAGE -> AUDIO (port of renderImageToBuffer): each image column is a moment in time, each row a harmonic of the base note (top row = highest),
// pixel brightness sets that harmonic's level. Input is RGBA8 (w x h). The web draws the image onto a cols x harmonics canvas with browser smoothing;
// here that downscale is an area average (box filter), so results match the web closely but not bit-for-bit.
struct ImageAudioCfg { double duration = 6; int note = 36, harmonics = 96, cols = 512; double tilt = 0.4, gamma = 1.2; bool invert = false, loop = true, rgbStereo = false; int sr = 48000; };
inline Audio imageToAudio(const unsigned char* rgba, int w, int h, const ImageAudioCfg& p) {
    Audio out; out.sr = p.sr; if (!rgba || w < 1 || h < 1) return out;
    const int N = std::max(1, p.harmonics), cols = std::max(1, std::min(p.cols, w)); const double sr = p.sr;
    std::vector<float> px((size_t)cols * N * 3);   // area-averaged rgb in 0..1
    for (int y = 0; y < N; y++) { const int y0 = (int)((long long)y * h / N), y1 = std::max(y0 + 1, (int)((long long)(y + 1) * h / N));
        for (int x = 0; x < cols; x++) { const int x0 = (int)((long long)x * w / cols), x1 = std::max(x0 + 1, (int)((long long)(x + 1) * w / cols)); double a[3] = {0, 0, 0}; int cnt = 0;
            for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) { const unsigned char* q = rgba + ((size_t)yy * w + xx) * 4; a[0] += q[0]; a[1] += q[1]; a[2] += q[2]; cnt++; }
            for (int k = 0; k < 3; k++) px[((size_t)y * cols + x) * 3 + k] = (float)(a[k] / cnt / 255.0); } }
    const double f0 = 440.0 * std::pow(2.0, (p.note - 69) / 12.0); double dur = p.duration;
    if (p.loop) dur = std::max(1.0, std::round(dur * f0)) / f0;   // whole number of fundamental cycles: every harmonic loops seamlessly
    const int S = std::max(1, (int)std::lround(dur * sr)); std::vector<float> L(S, 0.f), R(S, 0.f); const double colSamples = (double)S / cols;
    const int maxN = std::min(N, (int)std::floor(sr * 0.45 / f0)); std::vector<float> ampC(cols), panC(cols), phC(cols);
    for (int n = 1; n <= maxN; n++) {
        const int row = N - n; const double w0 = 2 * M_PI * f0 * n / sr, cw = std::cos(w0), sw = std::sin(w0), tilt = std::pow((double)n, -p.tilt);
        for (int x = 0; x < cols; x++) { const float* q = &px[((size_t)row * cols + x) * 3]; double lum = 0.299 * q[0] + 0.587 * q[1] + 0.114 * q[2]; if (p.invert) lum = 1 - lum; lum = std::pow(lum, p.gamma);
            ampC[x] = (float)(lum * tilt); if (p.rgbStereo) { panC[x] = q[0] - q[2]; phC[x] = (float)(q[1] * M_PI * 0.5); } }
        double zr = std::cos(p.rgbStereo ? phC[0] : 0.0), zi = std::sin(p.rgbStereo ? phC[0] : 0.0);
        for (int i = 0; i < S; i++) {
            const double cf = i / colSamples - 0.5; int c0 = (int)std::floor(cf), c1 = c0 + 1; const double fr = cf - c0;
            if (p.loop) { c0 = ((c0 % cols) + cols) % cols; c1 = ((c1 % cols) + cols) % cols; } else { c0 = std::max(0, std::min(cols - 1, c0)); c1 = std::max(0, std::min(cols - 1, c1)); }
            const double a = ampC[c0] * (1 - fr) + ampC[c1] * fr; const double nzr = zr * cw - zi * sw, nzi = zr * sw + zi * cw; zr = nzr; zi = nzi; const double v = a * zi;
            if (p.rgbStereo) { const double pan = panC[c0] * (1 - fr) + panC[c1] * fr, ang = (pan + 1) * M_PI / 4; L[i] += (float)(v * std::cos(ang)); R[i] += (float)(v * std::sin(ang)); } else L[i] += (float)v;
        }
    }
    if (!p.rgbStereo) R = L;
    double pk = 0; for (int i = 0; i < S; i++) pk = std::max(pk, std::max((double)std::fabs(L[i]), (double)std::fabs(R[i]))); const double k = pk > 0 ? 0.9 / pk : 1;
    const int fade = std::min(S >> 2, (int)std::lround(sr * 0.004));
    for (int i = 0; i < S; i++) { double e = 1; if (!p.loop && fade > 0) { if (i < fade) e = (double)i / fade; else if (S - i < fade) e = (double)(S - i) / fade; } L[i] = (float)(L[i] * k * e); R[i] = (float)(R[i] * k * e); }
    out.ch = { std::move(L), std::move(R) }; return out;
}
}}
