// Chord maker (generative chord progressions) - C++ port of tools/chordmaker_core.js (same algorithm, same PRNG, so a seed + settings
// give the same progression as the web app; Core/tests/chordmaker_tests.cpp checks that against golden output rendered by the JavaScript).
#pragma once
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <set>

namespace vv { namespace cm {

struct ScaleDef { const char* id; const char* label; const char* group; int n; int iv[12]; };
static const ScaleDef kScales[] = {
    {"major", "Major (Ionian)", "Western", 7, {0, 2, 4, 5, 7, 9, 11, 0, 0, 0, 0, 0}},
    {"dorian", "Dorian", "Western", 7, {0, 2, 3, 5, 7, 9, 10, 0, 0, 0, 0, 0}},
    {"phrygian", "Phrygian", "Western", 7, {0, 1, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}},
    {"lydian", "Lydian", "Western", 7, {0, 2, 4, 6, 7, 9, 11, 0, 0, 0, 0, 0}},
    {"mixolydian", "Mixolydian", "Western", 7, {0, 2, 4, 5, 7, 9, 10, 0, 0, 0, 0, 0}},
    {"minor", "Natural minor (Aeolian)", "Western", 7, {0, 2, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}},
    {"locrian", "Locrian", "Western", 7, {0, 1, 3, 5, 6, 8, 10, 0, 0, 0, 0, 0}},
    {"harmonicMinor", "Harmonic minor", "Western", 7, {0, 2, 3, 5, 7, 8, 11, 0, 0, 0, 0, 0}},
    {"melodicMinor", "Melodic minor", "Western", 7, {0, 2, 3, 5, 7, 9, 11, 0, 0, 0, 0, 0}},
    {"majorPent", "Major pentatonic", "Western", 5, {0, 2, 4, 7, 9, 0, 0, 0, 0, 0, 0, 0}},
    {"minorPent", "Minor pentatonic", "Western", 5, {0, 3, 5, 7, 10, 0, 0, 0, 0, 0, 0, 0}},
    {"blues", "Blues", "Western", 6, {0, 3, 5, 6, 7, 10, 0, 0, 0, 0, 0, 0}},
    {"wholeTone", "Whole tone", "Western", 6, {0, 2, 4, 6, 8, 10, 0, 0, 0, 0, 0, 0}},
    {"diminished", "Diminished (whole-half)", "Western", 8, {0, 2, 3, 5, 6, 8, 9, 11, 0, 0, 0, 0}},
    {"lydianDominant", "Lydian dominant", "Western", 7, {0, 2, 4, 6, 7, 9, 10, 0, 0, 0, 0, 0}},
    {"altered", "Altered (super-locrian)", "Western", 7, {0, 1, 3, 4, 6, 8, 10, 0, 0, 0, 0, 0}},
    {"neapolitanMinor", "Neapolitan minor", "Western", 7, {0, 1, 3, 5, 7, 8, 11, 0, 0, 0, 0, 0}},
    {"hijaz", "Hijaz / Phrygian dominant (Arabic, Flamenco)", "World", 7, {0, 1, 4, 5, 7, 8, 10, 0, 0, 0, 0, 0}},
    {"doubleHarmonic", "Double harmonic (Byzantine, Bhairav)", "World", 7, {0, 1, 4, 5, 7, 8, 11, 0, 0, 0, 0, 0}},
    {"hungarianMinor", "Hungarian minor (Romani)", "World", 7, {0, 2, 3, 6, 7, 8, 11, 0, 0, 0, 0, 0}},
    {"persian", "Persian", "World", 7, {0, 1, 4, 5, 6, 8, 11, 0, 0, 0, 0, 0}},
    {"enigmatic", "Enigmatic", "World", 7, {0, 1, 4, 6, 8, 10, 11, 0, 0, 0, 0, 0}},
    {"hirajoshi", "Hirajoshi (Japan)", "World", 5, {0, 2, 3, 7, 8, 0, 0, 0, 0, 0, 0, 0}},
    {"inSen", "In Sen (Japan)", "World", 5, {0, 1, 5, 7, 10, 0, 0, 0, 0, 0, 0, 0}},
    {"iwato", "Iwato (Japan)", "World", 5, {0, 1, 5, 6, 10, 0, 0, 0, 0, 0, 0, 0}},
    {"yo", "Yo (Japan)", "World", 5, {0, 2, 5, 7, 9, 0, 0, 0, 0, 0, 0, 0}},
    {"kumoi", "Kumoi (Japan)", "World", 5, {0, 2, 3, 7, 9, 0, 0, 0, 0, 0, 0, 0}},
    {"chineseGong", "Chinese Gong", "World", 5, {0, 4, 6, 7, 11, 0, 0, 0, 0, 0, 0, 0}},
    {"pelog", "Pelog (Bali / Java, approx.)", "World", 5, {0, 1, 3, 7, 8, 0, 0, 0, 0, 0, 0, 0}},
    {"slendro", "Slendro (Java, approx.)", "World", 5, {0, 2, 5, 7, 9, 0, 0, 0, 0, 0, 0, 0}},
    {"ragaTodi", "Raga Todi (India)", "World", 7, {0, 1, 3, 6, 7, 8, 11, 0, 0, 0, 0, 0}},
    {"ragaMarwa", "Raga Marwa (India)", "World", 6, {0, 1, 4, 6, 9, 11, 0, 0, 0, 0, 0, 0}},
    {"ragaPurvi", "Raga Purvi (India)", "World", 7, {0, 1, 4, 6, 7, 8, 11, 0, 0, 0, 0, 0}},
    {"maqamRast", "Maqam Rast (approx.)", "World", 7, {0, 2, 4, 5, 7, 9, 10, 0, 0, 0, 0, 0}},
    {"maqamSaba", "Maqam Saba (approx.)", "World", 7, {0, 2, 3, 4, 6, 8, 10, 0, 0, 0, 0, 0}},
    {"egyptian", "Egyptian (Suspended pentatonic)", "World", 5, {0, 2, 5, 7, 10, 0, 0, 0, 0, 0, 0, 0}},
};
constexpr int kNumScales = (int)(sizeof(kScales) / sizeof(kScales[0]));

struct Chord { int deg = 0; std::vector<int> tones; bool rest = false, locked = false; };
struct Cfg {
    bool on = false; int key = 0; std::string scale = "major";
    double naturalness = 0.7, density = 0.4, sparseness = 0.15, randomness = 0.1, prob = 100;
    int steps = 8, octave = 3; std::string voicing = "close"; double rate = 1, gate = 0.9, strum = 0, vel = 0.8; std::string target = "ALL";
    bool euOn = false; int euP = 5, euN = 8, euR = 0; uint32_t seed = 0;
    std::vector<Chord> prog; std::vector<std::string> history;
};

// mulberry32, bit-identical to cmRng() in the web app
struct Rng {
    uint32_t a;
    explicit Rng(uint32_t seed = 1) : a(seed) {}
    double operator()() {
        a += 0x6D2B79F5u; uint32_t t = a;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return (double)(t ^ (t >> 14)) / 4294967296.0;
    }
};

inline const ScaleDef& scaleOf(const std::string& id) { for (int i = 0; i < kNumScales; i++) if (id == kScales[i].id) return kScales[i]; return kScales[0]; }
static const double kFunc[12] = {1.0, 0.15, 0.4, 0.55, 0.6, 0.8, 0.2, 0.85, 0.5, 0.55, 0.45, 0.3};
static const double kMotion[12] = {0.1, 0.3, 0.6, 0.5, 0.45, 1.0, 0.2, 0.7, 0.5, 0.6, 0.55, 0.4};
static const char* const kNames[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

inline int floorDiv(int a, int b) { int q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }
inline int pcOf(const ScaleDef& S, int idx) { const int o = floorDiv(idx, S.n); return S.iv[idx - o * S.n] + 12 * o; }
inline int baseOf(const Cfg& c) { return 12 * (c.octave + 1) + c.key; }
inline int midiOf(const ScaleDef& S, const Cfg& c, int idx) { return baseOf(c) + pcOf(S, idx); }
inline int chordCount(const Cfg& c) { return c.euOn ? std::max(1, std::min(c.euP, c.euN)) : c.steps; }
inline std::vector<bool> euclid(int pulses, int steps, int rotate) {
    std::vector<bool> out; for (int i = 0; i < steps; i++) { const int j = (((i - rotate) % steps) + steps) % steps; out.push_back(((j * pulses) % steps) < pulses); } return out;
}
struct Weights { std::vector<double> w; double sum = 0; };
inline Weights degWeights(const ScaleDef& S, int from, double w) {
    Weights o;
    for (int b = 0; b < S.n; b++) {
        double nat;
        if (from < 0) nat = kFunc[S.iv[b] % 12];
        else { const int d = ((S.iv[b] - S.iv[from]) % 12 + 12) % 12; nat = kFunc[S.iv[b] % 12] * kMotion[d]; }
        double v = (1 - w) + w * nat; if (b == from) v *= 0.3;
        o.w.push_back(v); o.sum += v;
    }
    return o;
}
inline int pick(Rng& rnd, const Weights& ws) { double r = rnd() * ws.sum; for (size_t i = 0; i < ws.w.size(); i++) { r -= ws.w[i]; if (r < 0) return (int)i; } return (int)ws.w.size() - 1; }
inline int dominant(const ScaleDef& S) { int best = -1; double bv = -1; for (int d = 1; d < S.n; d++) { const double v = kFunc[S.iv[d] % 12]; if (v > bv) { bv = v; best = d; } } return best < 0 ? 0 : best; }
inline int sizeFor(const Cfg& c, Rng& rnd) {
    int size = 3 + (int)std::floor(c.density * 3 + 0.5);
    if (c.naturalness < 0.6 && rnd() < (1 - c.naturalness) * 0.5) size += (rnd() < 0.5 ? -1 : 1);
    return std::max(2, std::min(7, size));
}
inline std::vector<int> tonesFor(const ScaleDef& S, const Cfg& c, int deg, int size, Rng& rnd) {
    std::vector<int> t; for (int k = 0; k < size; k++) t.push_back(deg + 2 * k);
    for (int k = (int)t.size() - 1; k >= 1; k--) if (t.size() > 2 && rnd() < c.sparseness * 0.5) t.erase(t.begin() + k);
    const int n = S.n;
    if (c.voicing == "open") { if (t.size() > 1) t[1] += n; if (t.size() > 3) t[3] += n; }
    else if (c.voicing == "drop2") { if (t.size() > 2) t[t.size() - 2] -= n; }
    return t;
}
inline void voiceLead(std::vector<Chord>& prog, const ScaleDef& S, const Cfg& cfg, Rng& rnd) {
    const int n = S.n, center = baseOf(cfg) + 7; std::vector<int> prev; bool havePrev = false;
    for (auto& c : prog) {
        if (c.rest) continue;
        if (c.locked) { prev.clear(); for (int t : c.tones) prev.push_back(midiOf(S, cfg, t)); std::sort(prev.begin(), prev.end()); havePrev = true; continue; }
        std::vector<int> base = c.tones; std::sort(base.begin(), base.end());
        double bestCost = 1e18; std::vector<int> best;
        for (size_t inv = 0; inv < base.size(); inv++) {
            std::vector<int> cs; for (size_t k = 0; k < base.size(); k++) cs.push_back(k < inv ? base[k] + n : base[k]);
            std::sort(cs.begin(), cs.end());
            std::vector<int> mid; for (int t : cs) mid.push_back(midiOf(S, cfg, t));
            double mean = 0; for (int m : mid) mean += m; mean /= (double)mid.size();
            double cost;
            if (!havePrev) cost = std::fabs(mean - center);
            else {
                double pm = 0; for (int m : prev) pm += m; pm /= (double)prev.size();
                double near = 0; for (int m : mid) { double mn = 1e9; for (int p : prev) { const double d = std::fabs((double)(m - p)); if (d < mn) mn = d; } near += mn; }
                cost = std::fabs(mean - pm) + near / (double)mid.size();
            }
            cost += (1 - cfg.naturalness) * rnd() * 6;
            if (cost < bestCost) { bestCost = cost; best = cs; }
        }
        std::vector<int> t = best; int guard = 0;
        for (;;) {
            double m = 0; for (int x : t) m += midiOf(S, cfg, x); m /= (double)t.size(); if (guard++ > 6) break;
            if (m - center > 6) { for (int& x : t) x -= n; } else if (center - m > 6) { for (int& x : t) x += n; } else break;
        }
        std::sort(t.begin(), t.end()); c.tones = t; prev.clear(); for (int x : c.tones) prev.push_back(midiOf(S, cfg, x)); havePrev = true;
    }
}
inline std::string signature(const std::vector<Chord>& prog) {
    std::string s; for (size_t i = 0; i < prog.size(); i++) { if (i) s += ","; s += prog[i].rest ? std::string("-") : std::to_string(prog[i].deg) + ":" + std::to_string(prog[i].tones.size()); } return s;
}
inline std::vector<Chord> generate(const Cfg& cfg, Rng& rnd, const std::vector<std::string>& history) {
    const ScaleDef& S = scaleOf(cfg.scale); const int count = chordCount(cfg); const double w = cfg.naturalness; std::vector<Chord> prog;
    for (int attempt = 0; attempt < 10; attempt++) {
        std::vector<int> degs;
        for (int i = 0; i < count; i++) {
            if (i == 0) { if (rnd() < w) degs.push_back(0); else degs.push_back(pick(rnd, degWeights(S, -1, w))); }
            else degs.push_back(pick(rnd, degWeights(S, degs[i - 1], w)));
        }
        if (count >= 4 && w > 0.4) { if (rnd() < w) degs[count - 2] = dominant(S); if (rnd() < w) degs[count - 1] = 0; }
        prog.clear();
        for (int i = 0; i < count; i++) {
            bool rest = false; if (i > 0 && !cfg.euOn && cfg.sparseness > 0 && rnd() < cfg.sparseness * 0.5) rest = true;
            const int size = sizeFor(cfg, rnd);
            Chord c; c.deg = degs[i]; c.rest = rest; if (!rest) c.tones = tonesFor(S, cfg, degs[i], size, rnd); prog.push_back(c);
        }
        voiceLead(prog, S, cfg, rnd);
        std::set<int> dist; for (auto& c : prog) if (!c.rest) dist.insert(c.deg);
        const std::string sig = signature(prog);
        const bool ok = (count < 3 || (int)dist.size() >= std::min(3, S.n)) && std::find(history.begin(), history.end(), sig) == history.end();
        if (ok) break;
    }
    return prog;
}
inline void mutate(std::vector<Chord>& prog, const Cfg& cfg, Rng& rnd, double amount) {
    const ScaleDef& S = scaleOf(cfg.scale); const double w = cfg.naturalness; int changed = 0;
    for (size_t i = 0; i < prog.size(); i++) {
        Chord& c = prog[i]; if (c.locked) continue;
        if (rnd() < amount) {
            const int from = i > 0 ? prog[i - 1].deg : -1;
            c.deg = pick(rnd, degWeights(S, from, w)); c.rest = false; c.tones = tonesFor(S, cfg, c.deg, sizeFor(cfg, rnd), rnd); changed++;
        }
    }
    if (!changed) for (size_t i = 0; i < prog.size(); i++) { Chord& c = prog[i]; if (!c.locked) { c.deg = pick(rnd, degWeights(S, i > 0 ? prog[i - 1].deg : -1, w)); c.rest = false; c.tones = tonesFor(S, cfg, c.deg, sizeFor(cfg, rnd), rnd); break; } }
    voiceLead(prog, S, cfg, rnd);
}
inline void revoice(std::vector<Chord>& prog, const Cfg& cfg, Rng& rnd) { voiceLead(prog, scaleOf(cfg.scale), cfg, rnd); }
inline void rerollChord(std::vector<Chord>& prog, size_t i, const Cfg& cfg, Rng& rnd) {
    if (i >= prog.size()) return; const ScaleDef& S = scaleOf(cfg.scale); Chord& c = prog[i];
    c.deg = pick(rnd, degWeights(S, i > 0 ? prog[i - 1].deg : -1, cfg.naturalness)); c.rest = false; c.tones = tonesFor(S, cfg, c.deg, sizeFor(cfg, rnd), rnd);
    voiceLead(prog, S, cfg, rnd);
}
// MIDI notes for one trigger (Randomness moves notes to a neighbouring degree of the same scale); no allocation beyond `out` (caller reserves 8)
inline int playNotes(const Chord& c, const Cfg& cfg, Rng& rnd, int* out, int maxOut) {
    const ScaleDef& S = scaleOf(cfg.scale); int n = 0;
    for (size_t k = 0; k < c.tones.size(); k++) {
        int idx = c.tones[k];
        if (rnd() < cfg.randomness) { const int dir = rnd() < 0.5 ? -1 : 1; const double big = rnd(); idx += dir * ((cfg.randomness > 0.7 && big < 0.3) ? 2 : 1); }
        const int m = midiOf(S, cfg, idx); bool dup = false; for (int i = 0; i < n; i++) if (out[i] == m) dup = true;
        if (!dup && n < maxOut) out[n++] = m;
    }
    return n;
}
inline std::string chordName(const Chord& c, const Cfg& cfg) {
    if (c.rest || c.tones.empty()) return "\xE2\x80\x94";   // em dash, like the web app
    const ScaleDef& S = scaleOf(cfg.scale); const int root = (((baseOf(cfg) + pcOf(S, c.deg)) % 12) + 12) % 12;
    std::vector<int> set; for (int t : c.tones) { const int iv = (((midiOf(S, cfg, t) - root) % 12) + 12) % 12; if (std::find(set.begin(), set.end(), iv) == set.end()) set.push_back(iv); }
    std::sort(set.begin(), set.end()); std::string key; for (size_t i = 0; i < set.size(); i++) { if (i) key += ","; key += std::to_string(set[i]); }
    static const struct { const char* k; const char* q; } Q[] = {
        {"0,4,7", ""}, {"0,3,7", "m"}, {"0,3,6", "dim"}, {"0,4,8", "aug"}, {"0,2,7", "sus2"}, {"0,5,7", "sus4"}, {"0,4,7,10", "7"}, {"0,4,7,11", "maj7"}, {"0,3,7,10", "m7"}, {"0,3,7,11", "m(maj7)"},
        {"0,3,6,10", "m7b5"}, {"0,3,6,9", "dim7"}, {"0,2,4,7", "add9"}, {"0,2,3,7", "m(add9)"}, {"0,2,4,7,11", "maj9"}, {"0,2,4,7,10", "9"}, {"0,2,3,7,10", "m9"}, {"0,4,7,9", "6"}, {"0,3,7,9", "m6"},
        {"0,4,6,7", "(#11)"}, {"0,1,4,7", "(b9)"}, {"0,7,11", "maj7(no3)"}, {"0,7,10", "7(no3)"}, {"0,4,11", "maj7(no5)"}, {"0,3,10", "m7(no5)"}, {"0,4,10", "7(no5)"}, {"0,3,11", "m(maj7)(no5)"}};
    for (auto& q : Q) if (key == q.k) return std::string(kNames[root]) + q.q;
    std::string s = std::string(kNames[root]) + "("; for (size_t i = 1; i < set.size(); i++) { if (i > 1) s += " "; s += std::to_string(set[i]); } return s + ")";
}

}}  // namespace vv::cm
