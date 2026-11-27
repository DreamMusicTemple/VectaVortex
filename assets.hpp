// Asset loading helpers (non-realtime): wavetable files (.vvwt) and small file utilities.
#pragma once
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "voice_dsp.hpp"
#include "json.hpp"

namespace vv {
inline bool readFile(const std::string& path, std::vector<unsigned char>& out) {
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); out.resize((size_t)n);
    size_t r = n ? fread(out.data(), 1, (size_t)n, f) : 0; fclose(f); return r == (size_t)n;
}
inline std::string readTextFile(const std::string& path) { std::vector<unsigned char> b; if (!readFile(path, b)) return {}; return std::string(b.begin(), b.end()); }
// .vvwt: uint32 frameCount, uint32 frameSize, then frameCount*frameSize float32 (little endian)
inline std::shared_ptr<WtTable> parseWavetable(const unsigned char* d, size_t n) {
    if (n < 8) return nullptr; uint32_t fc, fs; std::memcpy(&fc, d, 4); std::memcpy(&fs, d + 4, 4);
    if (!fc || !fs || n < 8 + (size_t)fc * fs * 4) return nullptr;
    auto t = std::make_shared<WtTable>(); t->tableSize = (int)fs; t->frames.resize(fc);
    for (uint32_t i = 0; i < fc; i++) { t->frames[i].resize(fs); std::memcpy(t->frames[i].data(), d + 8 + (size_t)i * fs * 4, (size_t)fs * 4); }
    return t;
}
inline std::shared_ptr<WtTable> loadWavetableFile(const std::string& path) { std::vector<unsigned char> b; if (!readFile(path, b)) return nullptr; return parseWavetable(b.data(), b.size()); }
}

// ---- WAV loading (PCM 8/16/24/32-bit int, float32; mono or stereo kept, >2 channels truncated) ----
#include "sample_player.hpp"
namespace vv {
inline std::shared_ptr<SampleBuf> parseWav(const unsigned char* d, size_t n) {
    auto u16 = [&](size_t o) { return (uint32_t)d[o] | ((uint32_t)d[o + 1] << 8); };
    auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
    if (n < 44 || std::memcmp(d, "RIFF", 4) || std::memcmp(d + 8, "WAVE", 4)) return nullptr;
    size_t p = 12; int fmt = 0, ch = 0, bits = 0, sr = 0; const unsigned char* data = nullptr; size_t dlen = 0;
    while (p + 8 <= n) {
        const uint32_t sz = u32(p + 4);
        if (!std::memcmp(d + p, "fmt ", 4) && p + 8 + 16 <= n) { fmt = (int)u16(p + 8); ch = (int)u16(p + 10); sr = (int)u32(p + 12); bits = (int)u16(p + 22); if (fmt == 0xFFFE && sz >= 26) fmt = (int)u16(p + 8 + 24); }
        else if (!std::memcmp(d + p, "data", 4)) { data = d + p + 8; dlen = std::min<size_t>(sz, n - p - 8); break; }
        p += (size_t)8 + (size_t)sz + (size_t)(sz & 1);   // 64-bit: a 32-bit sum wrapped to 0 for sz=0xFFFFFFF7/8 and the scan never advanced
    }
    if (!data || ch < 1 || !sr || !bits) return nullptr;
    const int bps = bits / 8; if (bps < 1 || bps > 4) return nullptr;
    auto b = std::make_shared<SampleBuf>(); b->sr = sr; b->ch = std::min(ch, 2); b->len = (int)(dlen / ((size_t)bps * ch));
    for (int c = 0; c < b->ch; c++) b->d[c].resize((size_t)b->len);
    for (int i = 0; i < b->len; i++) for (int c = 0; c < b->ch; c++) {
        const unsigned char* q = data + ((size_t)i * ch + c) * bps; float v = 0;
        if (fmt == 3 && bps == 4) { uint32_t u = (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24); std::memcpy(&v, &u, 4); }
        else if (bps == 1) v = ((int)q[0] - 128) / 128.f;
        else if (bps == 2) v = (int16_t)(q[0] | (q[1] << 8)) / 32768.f;
        else if (bps == 3) { int32_t x = (q[0] << 8) | (q[1] << 16) | ((uint32_t)q[2] << 24); v = (float)(x / 2147483648.0); }
        else { int32_t x = (int32_t)((uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24)); v = (float)(x / 2147483648.0); }
        b->d[c][(size_t)i] = v;
    }
    return b;
}
inline std::shared_ptr<SampleBuf> loadWavFile(const std::string& path) { std::vector<unsigned char> b; if (!readFile(path, b)) return nullptr; return parseWav(b.data(), b.size()); }
// Sanitised file name the exporter uses for a sample called `name` ("Choir Ahh (synthesized)" -> "Choir_Ahh_synthesized.wav").
inline std::string sampleFileName(const std::string& name) {
    std::string o; bool us = false;
    for (unsigned char c : name) { if (std::isalnum(c)) { if (us && !o.empty()) o += '_'; us = false; o += (char)c; } else us = true; }
    return o + ".wav";
}
}  // namespace vv
