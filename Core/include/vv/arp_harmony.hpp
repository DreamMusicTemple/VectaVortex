// Arp-aware note picking for the RANDOMISE buttons (piano roll / step sequencer): while the arpeggiator is playing, random notes are drawn from the arp's own
// harmony - its chord tones (as transposed) plus a scale that contains them - and chosen melodically (chord tones on strong beats, stepwise motion, no
// semitone clashes with the chord on strong beats). Pure functions, no allocation, no shared state: safe from any thread. Mirrors the web hmAnalyse / hmPick
// (checked by Core/tests/arp_harmony_tests.cpp against a golden dump from the web).
#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cstdio>
namespace vv { namespace harmony {

struct Palette { bool active = false; int root = 0; int chord = 0; int scale = 0; char name[28] = {0}; };   // chord / scale: 12-bit pitch-class masks
inline int rot(int m, int r) { r = ((r % 12) + 12) % 12; return ((m << r) | (m >> (12 - r))) & 0xFFF; }
inline int maskOf(const int* iv, int n) { int m = 0; for (int i = 0; i < n; i++) m |= 1 << (((iv[i] % 12) + 12) % 12); return m; }
inline const char* noteName(int pc) { static const char* n[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}; return n[((pc % 12) + 12) % 12]; }

// held: MIDI notes sounding in the arp (held + latched); transpose: the arp's Transpose; advRel: the Scale Walk scale as an interval mask (bit i = i semitones above the lowest note) or 0;
// kbMask: the keyboard scale as an absolute pitch-class mask (0 = none).
inline Palette analyse(const int* held, int n, int transpose, int advRel, int kbMask) {
    Palette p; if (n <= 0) return p;
    int lowest = held[0]; for (int i = 0; i < n; i++) { lowest = std::min(lowest, held[i]); p.chord |= 1 << ((((held[i] + transpose) % 12) + 12) % 12); }
    p.root = (((lowest + transpose) % 12) + 12) % 12; p.active = true;
    if (advRel) { p.scale = rot(advRel, p.root); std::snprintf(p.name, sizeof p.name, "%s scale walk", noteName(p.root)); return p; }
    if (kbMask && (p.chord & ~kbMask) == 0) { p.scale = kbMask; std::snprintf(p.name, sizeof p.name, "keyboard scale"); return p; }
    static const int S[7][7] = {{0,2,4,5,7,9,11},{0,2,3,5,7,8,10},{0,2,3,5,7,9,10},{0,2,4,5,7,9,10},{0,2,3,5,7,8,11},{0,1,3,5,7,8,10},{0,2,4,6,7,9,11}};
    static const char* N[7] = {"major","minor","dorian","mixolydian","harmonic minor","phrygian","lydian"};
    double best = -1e9; int bs = -1, br = 0;
    for (int si = 0; si < 7; si++) { const int base = maskOf(S[si], 7); for (int r = 0; r < 12; r++) { const int m = rot(base, r); if (p.chord & ~m) continue;
        const double sc = 10 - si * 0.5 + (r == p.root ? 4 : 0) + ((p.chord >> r) & 1 ? 2 : 0); if (sc > best) { best = sc; bs = si; br = r; } } }
    if (bs >= 0) { p.scale = rot(maskOf(S[bs], 7), br); std::snprintf(p.name, sizeof p.name, "%s %s", noteName(br), N[bs]); }
    else { p.scale = p.chord | (1 << ((p.root + 7) % 12)) | (1 << ((p.root + 5) % 12)); std::snprintf(p.name, sizeof p.name, "arp notes"); }   // clusters no scale contains: chord tones + root's 4th/5th
    return p;
}
struct Rng { uint32_t s; double next() { s = s * 1664525u + 1013904223u; return (double)s / 4294967296.0; } };
inline int circ(int a, int b) { int d = ((a - b) % 12 + 12) % 12; return d > 6 ? 12 - d : d; }
// One melodic pick in [lo, hi]. prev = previous note (-1 = none). strong = on a strong beat.
inline int pick(const Palette& p, int prev, bool strong, Rng& rng, int lo, int hi) {
    const double center = (lo + hi) / 2.0, half = (hi - lo) / 2.0 + 1; double total = 0;
    auto weight = [&](int n) -> double { const int pc = ((n % 12) + 12) % 12; const bool ch = (p.chord >> pc) & 1; if (!ch && !((p.scale >> pc) & 1)) return 0;
        double w = ch ? (strong ? 4.0 : 1.8) : 1.0;
        if (!ch) { int dmin = 12; for (int c = 0; c < 12; c++) if ((p.chord >> c) & 1) dmin = std::min(dmin, circ(pc, c)); if (dmin == 1) w *= strong ? 0.3 : 0.7; }
        if (prev >= 0) { const int d = std::abs(n - prev); w *= 1.0 / (1.0 + 0.5 * d); if (d == 0) w *= 0.4; }
        return w * (1.0 - 0.5 * std::fabs(n - center) / half); };
    for (int n = lo; n <= hi; n++) total += weight(n);
    if (total <= 0) return (lo + hi) / 2;
    double r = rng.next() * total; int last = lo;
    for (int n = lo; n <= hi; n++) { const double w = weight(n); if (w <= 0) continue; last = n; r -= w; if (r <= 0) return n; }
    return last;
}
} }
