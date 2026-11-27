// Battery Saving Mode: the engine-side policy. One packed atomic word carries the user's choice from the UI thread to the audio thread (no locks, no allocation);
// the audio thread reads it once per 128-frame quantum. Profile 0 = off: every code path below is then skipped and rendering is identical to before.
// Everything here only ever skips work whose output is already below the threshold for a guard time - it never gates audible signal, and held notes are never touched.
#pragma once
#include <atomic>
#include <cstdint>
#include <cmath>
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
namespace vv {

enum PowerFlag : uint32_t { PW_VOICES = 1, PW_DSP = 2, PW_FX = 4, PW_MOD = 8, PW_VIS = 16 };

// User-facing configuration (not part of a preset). profile: 0 off, 1 Light, 2 Balanced, 3 Maximum Efficiency. flags: PowerFlag bits (which optimisations are active).
struct PowerCfg {
    int profile = 0; uint32_t flags = PW_VOICES | PW_DSP | PW_FX | PW_MOD | PW_VIS;
    uint32_t pack() const { return (uint32_t)profile | (flags << 4); }
    static PowerCfg unpack(uint32_t w) { PowerCfg c; c.profile = (int)(w & 15u); c.flags = (w >> 4) & 31u; return c; }
};

// What a profile means (seconds of sub-threshold output before something is put to sleep, and the level that counts as silent).
struct PowerPolicy {
    int voiceBlocks = 0; float voiceThresh = 0;    // released voice reaped after this many quiet quanta
    int fxBlocks = 0, eqBlocks = 0; float fxThresh = 0;   // FX stage / layer EQ skipped after this many quiet quanta (input AND output below threshold)
    float modSettleTaus = 1e9f;                     // a modulation glide older than this many time constants is treated as arrived
    int uiDivider = 1;                              // UI refresh rate is divided by this (visualisers, meters)
    static PowerPolicy forConfig(const PowerCfg& c, double sr) {
        PowerPolicy p; if (c.profile <= 0) return p;
        const double blk = 128.0 / sr;
        struct Row { double voiceS; float voiceT; double fxS; float fxT; float taus; int ui; };
        static const Row rows[3] = { {0.50, 1e-7f, 4.0, 1e-7f, 30.f, 2},     // Light: long guards, tiny thresholds, UI only slightly slower
                                      {0.25, 3e-7f, 1.5, 3e-7f, 16.f, 3},     // Balanced
                                      {0.10, 1e-6f, 0.5, 1e-6f, 10.f, 6} };   // Maximum efficiency
        const Row& r = rows[c.profile > 3 ? 2 : c.profile - 1];
        if (c.flags & PW_VOICES) { p.voiceBlocks = (int)std::ceil(r.voiceS / blk); p.voiceThresh = r.voiceT; }
        if (c.flags & PW_FX) { p.fxBlocks = (int)std::ceil(r.fxS / blk); p.fxThresh = r.fxT; }
        if (c.flags & PW_DSP) { p.eqBlocks = (int)std::ceil(r.fxS / blk); if (p.fxThresh == 0) p.fxThresh = r.fxT; }
        if (c.flags & PW_MOD) p.modSettleTaus = r.taus;
        if (c.flags & PW_VIS) p.uiDivider = r.ui;
        return p;
    }
};

// Counters the UI / tests can read (relaxed atomics; written only by the audio thread).
struct PowerStats { std::atomic<uint64_t> voicesReaped{0}, fxBlocksSkipped{0}, eqBlocksSkipped{0}, quanta{0}; };

// Flush-to-zero / denormals-are-zero for the duration of one render call (restores the caller's mode afterwards). Decaying filter / reverb / delay states otherwise
// linger in the denormal range, which is very slow on x86 and can cost CPU on ARM; values below ~1e-38 (-760 dB) are the only thing it changes.
struct ScopedFlushDenormals {
    explicit ScopedFlushDenormals(bool on) : on_(on) {
        if (!on_) return;
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
        saved_ = _mm_getcsr(); _mm_setcsr(saved_ | 0x8040u);
#elif defined(__aarch64__)
        uint64_t f; __asm__ __volatile__("mrs %0, fpcr" : "=r"(f)); saved_ = f; f |= (1ull << 24); __asm__ __volatile__("msr fpcr, %0" : : "r"(f));
#else
        on_ = false;
#endif
    }
    ~ScopedFlushDenormals() {
        if (!on_) return;
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
        _mm_setcsr((unsigned)saved_);
#elif defined(__aarch64__)
        uint64_t f = saved_; __asm__ __volatile__("msr fpcr, %0" : : "r"(f));
#endif
    }
    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete; ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;
private: bool on_ = false; uint64_t saved_ = 0;
};

// Shared with ModSlot::at (a free inline function): glide age, in time constants, after which the slot is considered settled. 1e9 = never (off).
inline std::atomic<float> gModSettleTaus{1e9f};
inline std::atomic<float>& modSettleTaus() { return gModSettleTaus; }

}  // namespace vv
