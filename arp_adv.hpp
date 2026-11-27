// ARP ADVANCED: 35 seeded generative algorithms. A pure function of (seed, params, held notes, step counter) - no allocation, no state, safe on the audio thread.
// Mirrors the web ARP_ALGOS / arpAdvGen() exactly (integer hash, same operation order); Core/tests/arp_adv_tests.cpp checks it against a golden dump made from the web code.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <string>
#include "model.hpp"
#pragma STDC FP_CONTRACT OFF
namespace vv { namespace arpadv {

struct Ev { int i = 0, ds = 0; double v = 1, g = 1, o = 0; int r = 1; int dec = 0; };
constexpr int kMaxEv = 3, kNumAlgos = 35;
inline const char* const* algoIds() { static const char* const ids[kNumAlgos] = {"fractal","fibonacci","brownian","kaleido","cascade","callresp","intjump","pedal","scalewalk","inversion","euclid","markov","cellular","chaos","golden","probabilistic","neural","mutation","morph","timewarp","ratchetA","stutter","offbeat","syncopated","polyrhythm","triplet","groove","burst","revgate","ghost","broken","harmonic","voicelead","octjump","intseq"}; return ids; }
inline int algoIndex(const std::string& id) { for (int i = 0; i < kNumAlgos; i++) if (id == algoIds()[i]) return i; return 0; }
inline int scaleIndex(const std::string& s) { static const char* n[6] = {"major","minor","dorian","pentatonic","blues","chromatic"}; for (int i = 0; i < 6; i++) if (s == n[i]) return i; return 0; }

// The Scale Walk scale (by index) as an interval mask relative to the lowest held note (used by arp_harmony).
inline int scaleRelMask(int id) { static const int sc[6][12] = {{0,2,4,5,7,9,11},{0,2,3,5,7,8,10},{0,2,3,5,7,9,10},{0,2,4,7,9},{0,3,5,6,7,10},{0,1,2,3,4,5,6,7,8,9,10,11}}; static const int len[6] = {7,7,7,5,6,12};
    id = std::max(0, std::min(5, id)); int m = 0; for (int i = 0; i < len[id]; i++) m |= 1 << sc[id][i]; return m; }
inline uint32_t hash(int32_t a, int32_t b, int32_t c) { uint32_t h = (uint32_t)a * 0x9E3779B1u ^ (uint32_t)b * 0x85EBCA6Bu ^ (uint32_t)c * 0xC2B2AE35u; h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15; return h; }
inline double R(int32_t a, int32_t b, int32_t c) { return (double)hash(a, b, c) / 4294967296.0; }
inline int imod(int a, int n) { return ((a % n) + n) % n; }
inline int rnd(double x) { return (int)std::floor(x + 0.5); }
inline double tri(double x) { const double f = x - std::floor(x); return 1 - std::fabs(2 * f - 1); }
inline Ev mk(int i) { Ev e; e.i = i; return e; }

struct Ctx { int32_t sd; int N, L, cyc, k; const int* E; double x, d, m, rat, gr; int sc, ratio; };

inline int genBase(int algo, int pk, const Ctx& c, Ev* out) {
    static const int scales[6][12] = {{0,2,4,5,7,9,11},{0,2,3,5,7,8,10},{0,2,3,5,7,9,10},{0,2,4,7,9},{0,3,5,6,7,10},{0,1,2,3,4,5,6,7,8,9,10,11}};
    static const int scaleLen[6] = {7, 7, 7, 5, 6, 12};
    static const int fibn[8] = {1, 1, 2, 3, 5, 8, 13, 21};
    const int N = c.N;
    switch (algo) {
    case 0: { static const int m[4] = {0, 1, 2, 1}; out[0] = mk(m[pk & 3] + m[(pk >> 2) & 3] * (1 + (int)std::floor(c.x * 2)) + ((pk >> 4) & 3) * (int)std::floor(c.x * 3)); return 1; }
    case 1: { int i = 0; for (int j = 0; j <= pk; j++) i += fibn[j % 8]; out[0] = mk(i); return 1; }
    case 2: { int pos = 0; for (int j = 0; j <= pk; j++) { if (R(c.sd, j, 21) < 0.1 + c.x * 0.2) pos += (R(c.sd, j, 22) < 0.5 ? -1 : 1) * (2 + (int)std::floor(R(c.sd, j, 23) * std::max(1, N >> 1))); else pos += R(c.sd, j, 24) < 0.5 ? -1 : 1; }
              const int per = std::max(1, 2 * N - 2), p = imod(pos, per); out[0] = mk(p < N ? p : per - p); return 1; }
    case 3: { const int g = std::max(2, std::min(4, N)), grp = pk / (2 * g), j = pk % (2 * g); out[0] = mk((j < g ? j : 2 * g - 1 - j) + grp); return 1; }
    case 4: { out[0] = mk(N - 1 - (pk % N)); out[0].ds = -12 * ((pk / N) % 3); return 1; }
    case 5: { const int L2 = std::max(2, c.L >> 1), a = pk % L2;
              if (imod(pk, c.L) < L2) { out[0] = mk((int)std::floor(R(c.sd, a, 31) * N)); } else { out[0] = mk((int)std::floor(R(c.sd, L2 - 1 - a, 31) * N) + 1); out[0].v = 0.85; } return 1; }
    case 6: { int j = 2 + (int)std::floor(c.x * 3); if (N > 1 && N % j == 0) j++; out[0] = mk(pk * j); return 1; }
    case 7: { if (pk % 2 == 0) { out[0] = mk(0); out[0].v = 1.1; } else out[0] = mk(1 + ((pk >> 1) % std::max(1, N - 1))); return 1; }
    case 8: { const int* s = scales[c.sc]; const int len = scaleLen[c.sc], M = len * 2, q = imod(pk, M), pos = q < len ? q : M - 1 - q; out[0] = mk(0); out[0].ds = s[pos] + 12 * ((pk / M) % 2); return 1; }
    case 9: { const int n = std::min(N, 1024); int arr[1024]; const int inv = (pk / n) % n; for (int j = 0; j < n; j++) arr[j] = c.E[j] + (j < inv ? 12 : 0); std::sort(arr, arr + n); out[0] = mk(0); out[0].ds = arr[pk % n] - c.E[0]; return 1; }
    case 10: { const int pl = std::max(1, rnd(c.d * c.L)); if ((pk * pl) % c.L < pl) { out[0] = mk((pk * pl) / c.L); return 1; } return 0; }
    case 11: { int prev = 0, idx = 0; for (int j = 0; j <= pk; j++) { const double r = R(c.sd, j, 41); int st; if (prev % 2 == 0) st = r < 0.5 - c.x * 0.2 ? 1 : r < 0.75 ? 2 : r < 0.9 ? -1 : 0; else st = r < 0.3 ? -1 : r < 0.6 ? 1 : r < 0.8 ? -2 : 0; idx = imod(idx + st, N); prev = idx; } out[0] = mk(idx); return 1; }
    case 12: { static const int rules[4] = {30, 90, 110, 150}; const int rule = rules[std::min(3, (int)std::floor(c.x * 4))]; int row = (int)(hash(c.sd, 0, 43) & 255u); if (row == 0) row = 1;
               for (int g = 0; g < pk; g++) { int nw = 0; for (int b = 0; b < 8; b++) { const int l = (row >> ((b + 1) % 8)) & 1, ce = (row >> b) & 1, rr = (row >> ((b + 7) % 8)) & 1; if ((rule >> (l * 4 + ce * 2 + rr)) & 1) nw |= 1 << b; } row = nw; }
               if (row == 0) return 0; out[0] = mk(row % N); return 1; }
    case 13: { double x = 0.1 + 0.8 * R(c.sd, 0, 51); const double r = 3.57 + c.x * 0.42; for (int j = 0; j <= pk; j++) { const double t = r * x; x = t * (1 - x); } out[0] = mk(std::min(N - 1, (int)std::floor(x * N))); return 1; }
    case 14: { const double f = std::fmod(pk * 0.6180339887498949, 1.0); out[0] = mk(std::min(N - 1, (int)std::floor(f * N))); out[0].o = std::max(0.0, (f - 0.5) * 0.5); return 1; }
    case 15: { const int i = (int)std::floor(R(c.sd, pk, 61) * N); const double w = 0.4 + 0.6 * R(c.sd, i, 62); if (R(c.sd, pk, 63) < c.d * w * 1.5) { out[0] = mk(i); return 1; } return 0; }
    case 16: { double W[20]; for (int q = 0; q < 20; q++) W[q] = (R(c.sd, q, 71) - 0.5) * 2; auto ss = [](double v) { return v / (1 + std::fabs(v)); };
               double h0 = 0, h1 = 0, h2 = 0; int i = 0;
               for (int j = 0; j <= pk; j++) { double hid[4]; for (int k = 0; k < 4; k++) hid[k] = ss(W[k * 4] * h0 + W[k * 4 + 1] * h1 + W[k * 4 + 2] * h2 + W[k * 4 + 3]);
                   double o = 0; for (int k = 0; k < 4; k++) o += W[16 + k] * hid[k]; o = ss(o * 2 + (R(c.sd, j, 72) - 0.5) * c.x * 2);
                   i = std::min(N - 1, (int)std::floor((o * 0.5 + 0.5) * N)); h2 = h1; h1 = h0; h0 = (double)i / N; }
               out[0] = mk(i); return 1; }
    case 17: { int i = pk % N; for (int cc = c.cyc; cc >= 1 && cc > c.cyc - 16; cc--) { if (R(c.sd, pk, 80 + cc) < c.m) { i = (int)std::floor(R(c.sd, pk, 300 + cc) * N); break; } } out[0] = mk(i); return 1; }
    case 18: { const double t = (c.cyc % 8) / 4.0, m = t <= 1 ? t : 2 - t; out[0] = mk(R(c.sd, pk, 92) < m ? (int)std::floor(R(c.sd, pk, 91) * N) : pk % N); return 1; }
    case 19: { const double tw = tri((double)c.k / (c.L * 2)); out[0] = mk(pk % N); out[0].g = 0.3 + 0.9 * tw; out[0].o = (pk % 2) * tw * 0.3; out[0].r = tw > 0.8 ? 2 : 1; return 1; }
    case 20: { out[0] = mk(pk % N); out[0].r = 2 + (int)std::floor(c.rat * 2); return 1; }
    case 21: { if (pk % 2 == 0) { out[0] = mk((pk / 2) % N); out[0].r = 4; out[0].dec = 1; return 1; } return 0; }
    case 22: { if (pk % 2 == 1 || R(c.sd, pk, 101) < c.d * 0.3) { out[0] = mk((pk / 2) % N); return 1; } return 0; }
    case 23: { static const int mask[16] = {1,0,0,1,0,0,1,0,1,0,0,1,0,1,0,0}; const int mk_ = mask[pk % 16]; if (mk_ || R(c.sd, pk, 111) < c.d * 0.25) { out[0] = mk((pk / 2) % N); out[0].v = mk_ ? 1.15 : 0.8; return 1; } return 0; }
    case 24: { const int nl = std::max(1, N >> 1); int n = 0; if (pk % 3 == 0) out[n++] = mk((pk / 3) % nl); if (pk % 4 == 0) { out[n] = mk(nl + (pk / 4) % std::max(1, N - nl)); out[n].o = 0.05; n++; } return n; }
    case 25: { out[0] = mk((3 * pk) % N); out[1] = mk((3 * pk + 2) % N); out[1].o = 0.667; out[1].v = 0.8; return 2; }
    case 26: { static const double vv[4] = {1, 0.6, 0.85, 0.6}; out[0] = mk(pk % N); out[0].o = (pk % 2) ? c.gr * 0.33 : 0; out[0].v = vv[pk % 4]; return 1; }
    case 27: { const int gap = 1 + rnd((1 - c.d) * 4); if (pk % (1 + gap) != 0) return 0; const int b = pk / (1 + gap); out[0] = mk(b); out[1] = mk(b + 1); out[1].o = 0.15; out[1].v = 0.8; out[2] = mk(b + 2); out[2].o = 0.3; out[2].v = 0.65; return 3; }
    case 28: { out[0] = mk(pk % N); out[0].g = (pk % 2) ? 0.95 : 0.25; return 1; }
    case 29: { if (pk % 2 == 0) { out[0] = mk((pk / 2) % N); return 1; } if (R(c.sd, pk, 141) < c.d + 0.2) { out[0] = mk((pk / 2) % N + 1); out[0].v = 0.3; out[0].g = 0.5; return 1; } return 0; }
    case 30: { int a = (int)std::floor(R(c.sd, pk, 151) * N); const int b = (int)std::floor(R(c.sd, pk - 1, 151) * N); if (a == b && N > 1) a = (a + 1) % N; out[0] = mk(a); return 1; }
    case 31: { static const int d[4] = {7, 12, 19, 4}; const int dd = d[(int)std::floor(R(c.sd, pk, 161) * 4)]; out[0] = mk(pk % N); out[1] = mk(pk % N); out[1].ds = dd; out[1].v = 0.6; out[1].o = 0.5; return 2; }
    case 32: { int prev = c.E[0], pi = 0, pd = 0; for (int j = 0; j <= pk; j++) { int b1 = 1000000000, b2 = 1000000000, i1 = 0, d1 = 0, i2 = 0, d2 = 0;
                   for (int e = 0; e < N; e++) for (int s = -1; s <= 1; s++) { const int n = c.E[e] + 12 * s, dist = std::abs(n - prev); if (dist == 0) continue; if (dist < b1) { b2 = b1; i2 = i1; d2 = d1; b1 = dist; i1 = e; d1 = 12 * s; } else if (dist < b2) { b2 = dist; i2 = e; d2 = 12 * s; } }
                   const bool useB = R(c.sd, j, 171) < 0.7 || b2 >= 1000000000; pi = useB ? i1 : i2; pd = useB ? d1 : d2; prev = c.E[pi] + pd; }
               out[0] = mk(pi); out[0].ds = pd; return 1; }
    case 33: { out[0] = mk(pk % N); out[0].ds = (pk % 3 == 2 ? 12 : 0) + (pk % 5 == 4 ? -12 : 0); return 1; }
    case 34: { static const int pats[4][4] = {{3,4,5,0},{7,5,0,0},{4,3,5,2},{2,2,3,0}}; static const int plen[4] = {3, 2, 4, 3}; const int pi = std::min(3, (int)std::floor(c.x * 4)); int cum = 0; for (int j = 0; j < pk; j++) cum += pats[pi][j % plen[pi]]; out[0] = mk(0); out[0].ds = cum % 25; return 1; }
    }
    return 0;
}

// P = parameters, E = ascending expanded note list (N entries), k = step counter. Returns the number of events written to out (kMaxEv); 0 = rest.
inline int gen(const ArpAdvCfg& P, const int* E, int N, int k, Ev* out) {
    if (N <= 0) return 0;
    const int L = std::max(2, std::min(64, rnd(P.patLen))), kk = k + rnd(P.rotation), pk = imod(kk, L), cyc = (int)std::floor((double)kk / L);
    const int32_t seed = (int32_t)rnd(P.seed); const int32_t sd0 = (P.cycleVar > 0 && R(seed, cyc, 11) < P.cycleVar) ? (int32_t)((uint32_t)seed + (uint32_t)cyc * 7919u) : seed;
    const int32_t sd = (P.mutSalt != 0 && R(seed, pk, 401) < P.mutAmt) ? (int32_t)((uint32_t)sd0 + (uint32_t)rnd(P.mutSalt) * 104729u) : sd0;   // Mutate: some steps are re-rolled
    const int32_t sp = P.varSeed != 0 ? (int32_t)((uint32_t)sd + (uint32_t)rnd(P.varSeed) * 15485863u) : sd;                                      // Regenerate Variation: re-rolls only the variation layer
    Ctx c{sd, N, L, cyc, k, E, P.complexity, P.density, P.mutation, P.ratchet, P.groove, P.scaleId, 0};
    const int n = genBase(P.algoId, pk, c, out); if (n == 0) return 0;
    if (R(sp, kk, 3) > P.probability || R(sp, kk, 5) < P.skipProb) return 0;
    for (int q = 0; q < n; q++) { Ev& e = out[q];
        e.i = imod(e.i, N);
        if (R(sp, kk, 201 + q) < P.randomness) e.i = (int)std::floor(R(sp, kk, 202 + q) * N);
        if (R(sp, kk, 211 + q) < P.variation * 0.5) e.i = imod(e.i + (R(sp, kk, 212 + q) < 0.5 ? -1 : 1), N);
        if (R(sp, kk, 221 + q) < P.octSpread * 0.6) e.ds += (R(sp, kk, 222 + q) < 0.5 ? -12 : 12);
        if (e.r == 1 && R(sp, kk, 231 + q) < P.ratchet) e.r = 2 + (int)std::floor(R(sp, kk, 232 + q) * 2);
        if (pk % 4 == 0) e.v *= 1 + P.accent * 0.4;
        e.v *= 1 - P.velVar * R(sp, kk, 241 + q); e.g *= 1 - P.gateVar * 0.8 * R(sp, kk, 251 + q);
        e.o += P.humanise * 0.25 * R(sp, kk, 261 + q); if (pk % 2 == 1) e.o += P.rhythmDisp * 0.45;
        e.o = std::min(0.9, e.o);
    }
    return n;
}
// A/B morph: each step is drawn from pattern A or B (B with probability amt; deterministic per seed of A and step). Mirrors web arpAdvGenMorph().
inline int genMorph(const ArpAdvCfg& A, const ArpAdvCfg& B, double amt, const int* E, int N, int k, Ev* out) {
    const int32_t sA = (int32_t)rnd(A.seed); return gen(R(sA, k, 501) < amt ? B : A, E, N, k, out);
}
} }
