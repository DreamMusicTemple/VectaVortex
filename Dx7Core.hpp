// Vektur Vortex - DX7-compatible 6-operator FM core, C++17 port of the web app's Dx7Core (same maths, same tables).
//
// Patch format = the 156-byte "unpacked voice" used by the web app (155 DX7 parameter bytes + one operator-enable mask):
//   op n (n=0 is OP6 ... 5 is OP1) at offset n*21:
//     +0..3 R1..R4  +4..7 L1..L4  +8 breakpoint  +9 left depth  +10 right depth  +11 left curve  +12 right curve
//     +13 rate scaling  +14 amp-mod sens  +15 key-vel sens  +16 output level  +17 mode (0 ratio, 1 fixed)
//     +18 coarse  +19 fine  +20 detune (0..14)
//   126..129 pitch-EG R1..R4, 130..133 pitch-EG L1..L4, 134 algorithm (0..31), 135 feedback, 136 osc key sync,
//   137 LFO speed, 138 LFO delay, 139 LFO PMD, 140 LFO AMD, 141 LFO sync, 142 LFO wave, 143 LFO pitch-mod sens,
//   144 transpose (24 = C3), 145..154 name, 155 operator mask.
//
// Real-time safety: Voice::renderBlock() performs no allocation, locking or I/O. Everything lives in fixed-size members.
// Hardware behaviour tables follow the measured DX7 behaviour documented by the open-source Dexed / music-synthesizer-for-android
// projects (Apache-2.0); the code itself is original.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include "dx7_tables.hpp"

namespace vv { namespace dx7 {

constexpr int      N          = 64;          // internal control block: envelopes and LFO update once per block
constexpr int      PATCH_SIZE = 156;
constexpr double   TWO24      = 16777216.0;
constexpr int64_t  TWO24i     = 16777216;
constexpr double   TWO_PI     = 6.283185307179586;
constexpr double   OUT_SCALE  = 0.15;        // final headroom trim
static const int64_t AMS_TAB[4] = { 0, 4342338, 7171437, 16777216 };
static const int64_t PMS_TAB[8] = { 0, 10, 20, 33, 55, 92, 153, 255 };

inline int scaleOutLevel(int l) { return l >= 20 ? 28 + l : dx7tab::levellut[l]; }

// ---- four-rate / four-level envelope generator, log-amplitude domain (1 unit = 0.75 dB) ----
struct Env {
    int rates[4] {0,0,0,0}, levels[4] {0,0,0,0};
    int64_t outlevel = 0, rateScaling = 0, srMult = TWO24i;
    int64_t level = 0, target = 0, inc = 0;
    bool rising = false, down = true;
    int ix = 0;

    void init(const int* r, const int* l, int64_t ol, int64_t rs, int64_t sm) {
        for (int i = 0; i < 4; ++i) { rates[i] = r[i]; levels[i] = l[i]; }
        outlevel = ol; rateScaling = rs; srMult = sm; level = 0; down = true; advance(0);
    }
    void update(const int* r, const int* l, int64_t ol, int64_t rs) {
        for (int i = 0; i < 4; ++i) { rates[i] = r[i]; levels[i] = l[i]; }
        outlevel = ol; rateScaling = rs;
        if (ix < 4) advance(ix);
    }
    void advance(int newix) {
        ix = newix;
        if (newix < 4) {
            int64_t al = scaleOutLevel(levels[newix]) >> 1;
            al = (al * 64) + outlevel - 4256;
            if (al < 16) al = 16;
            target = al * 65536;
            rising = target > level;
            int64_t q = ((int64_t(rates[newix]) * 41) >> 6) + rateScaling;
            if (q > 63) q = 63;
            int64_t i2 = (4 + (q & 3)) << (2 + 6 + (q >> 2));
            inc = (i2 * srMult) / TWO24i;
        }
    }
    int64_t getsample() {
        if (ix < 3 || (ix < 4 && !down)) {
            if (rising) {
                const int64_t jump = 1716LL * 65536;
                if (level < jump) level = jump;
                level += int64_t(int32_t(285212672 - level) >> 24) * inc;
                if (level >= target) { level = target; advance(ix + 1); }
            } else {
                level -= inc;
                if (level <= target) { level = target; advance(ix + 1); }
            }
        }
        return level;
    }
    void keydown(bool d) { if (down != d) { down = d; advance(d ? 0 : 3); } }
};

// ---- pitch envelope (+-4 octaves) ----
struct PitchEnv {
    int rates[4] {0,0,0,0}, levels[4] {0,0,0,0};
    int64_t level = 0, target = 0, inc = 0, unit = 0;
    bool rising = false, down = true;
    int ix = 0;
    void setSampleRate(double sr) { unit = int64_t(std::floor(N * TWO24 / (21.3 * sr) + 0.5)); }
    void set(const int* r, const int* l) {
        for (int i = 0; i < 4; ++i) { rates[i] = r[i]; levels[i] = l[i]; }
        level = int64_t(dx7tab::pitchenv_tab[l[3]]) * 524288; down = true; advance(0);
    }
    void advance(int newix) {
        ix = newix;
        if (newix < 4) {
            target = int64_t(dx7tab::pitchenv_tab[levels[newix]]) * 524288;
            rising = target > level;
            inc = int64_t(dx7tab::pitchenv_rate[rates[newix]]) * unit;
        }
    }
    int64_t getsample() {
        if (ix < 3 || (ix < 4 && !down)) {
            if (rising) { level += inc; if (level >= target) { level = target; advance(ix + 1); } }
            else        { level -= inc; if (level <= target) { level = target; advance(ix + 1); } }
        }
        return level;
    }
    void keydown(bool d) { if (down != d) { down = d; advance(d ? 0 : 3); } }
};

// ---- LFO (DX7 speed table, delay ramp, six waveforms) ----
struct Lfo {
    uint32_t phase = 0; uint64_t delta = 0; int waveform = 0; bool sync = false; int randstate = 0;
    uint64_t delaystate = 0, delayinc = 0, delayinc2 = 0; int64_t unit = 0; double ratio = 0;
    void setSampleRate(double sr) { unit = int64_t(std::floor(N * 25190424.0 / sr + 0.5)); ratio = std::floor(4437500000.0 * N / sr); }
    void reset(const uint8_t* p, int o) {
        delta = uint64_t(std::floor(dx7tab::lfoSource[p[o]] * ratio));
        int a = 99 - p[o + 1];
        if (a == 99) { delayinc = 4294967295ULL; delayinc2 = 4294967295ULL; }
        else {
            a = (16 + (a & 15)) << (1 + (a >> 4));
            delayinc = uint64_t(unit) * uint64_t(a);
            a &= 0xff80; if (a < 0x80) a = 0x80;
            delayinc2 = uint64_t(unit) * uint64_t(a);
        }
        waveform = p[o + 5]; sync = p[o + 4] != 0;
    }
    int64_t getsample() {
        phase = uint32_t(uint64_t(phase) + delta);
        const uint32_t ph = phase;
        switch (waveform) {
            case 0: { uint32_t x = ph >> 7; if (ph >> 31) x = ~x; return int64_t(x & 0xFFFFFFu); }
            case 1: return int64_t(((~ph) ^ 0x80000000u) >> 8);
            case 2: return int64_t((ph ^ 0x80000000u) >> 8);
            case 3: return int64_t(((~ph) >> 7) & 0x1000000u);
            case 4: return 8388608 + int64_t(std::floor(std::sin(double(ph) / 4294967296.0 * TWO_PI) * 8388608.0));
            case 5: {
                if (uint64_t(ph) < delta) randstate = (randstate * 179 + 17) & 0xff;
                const int x = randstate ^ 0x80; return int64_t(x + 1) << 16;
            }
        }
        return 8388608;
    }
    int64_t getdelay() {
        const uint64_t dl = delaystate < 2147483648ULL ? delayinc : delayinc2;
        const uint64_t d = delaystate + dl;
        if (d > 4294967295ULL) return TWO24i;
        delaystate = d;
        if (d < 2147483648ULL) return 0;
        return int64_t((d / 128) % uint64_t(TWO24i));
    }
    void keydown(uint32_t freePhase) { phase = sync ? 2147483647u : freePhase; delaystate = 0; }
};

// ---- scaling helpers ----
inline int scaleVelocity(int velocity, int sens) {
    const int cv = velocity < 0 ? 0 : (velocity > 127 ? 127 : velocity);
    const int v = dx7tab::velocity_data[cv >> 1] - 239;
    return ((sens * v + 7) >> 3) * 16;
}
inline int scaleRate(int midi, int sens) {
    int x = midi / 3 - 7; if (x < 0) x = 0; if (x > 31) x = 31;
    return (sens * x) >> 3;
}
inline int scaleCurve(int group, int depth, int curve) {
    int s;
    if (curve == 0 || curve == 3) s = (group * depth * 329) >> 12;
    else { const int raw = dx7tab::exp_scale_data[group < 32 ? group : 32]; s = (raw * depth * 329) >> 15; }
    if (curve < 2) s = -s;
    return s;
}
inline int floorDiv3(int v) { return v >= 0 ? v / 3 : -((-v + 2) / 3); }
inline int scaleLevel(int midi, int bp, int ld, int rd, int lc, int rc) {
    const int offset = midi - bp - 17;
    if (offset >= 0) return scaleCurve(floorDiv3(offset + 1), rd, rc);
    return scaleCurve(floorDiv3(1 - offset), ld, lc);
}

// ---- one polyphonic voice: 6 operators routed through one of the 32 algorithms ----
class Voice {
public:
    explicit Voice(double sampleRate = 44100.0) { setSampleRate(sampleRate); }
    void setSampleRate(double s) {
        sr = s; srMult = int64_t(std::floor((44100.0 / sr) * TWO24));
        lfo.setSampleRate(sr); peg.setSampleRate(sr);
    }
    bool active = false, keyIsDown = false;

    void noteOn(const uint8_t* p, int noteNum, int velocity, uint32_t freePhase) {
        const int trn = int(p[144]) - 24;
        int m = noteNum + trn; if (m < 0) m = 0; if (m > 127) m = 127;
        midi = m; vel = velocity; transposeOct = trn / 12.0;
        for (int o = 0; o < 6; ++o) {
            opParams(p, midi, vel, o, true); phase[o] = 0; gain[o] = 0;
            if (mode[o] == 0) deltaOct[o] += transposeOct;
        }
        const int r[4] = { p[126], p[127], p[128], p[129] }, l[4] = { p[130], p[131], p[132], p[133] };
        peg.set(r, l);
        globals(p);
        lfo.reset(p, 137); lfo.keydown(freePhase);
        fb0 = fb1 = 0; active = true; keyIsDown = true;
    }
    void updatePatch(const uint8_t* p) {
        for (int o = 0; o < 6; ++o) { opParams(p, midi, vel, o, false); if (mode[o] == 0) deltaOct[o] += transposeOct; }
        globals(p); lfo.reset(p, 137);
        for (int i = 0; i < 4; ++i) { peg.rates[i] = p[126 + i]; peg.levels[i] = p[130 + i]; }
    }
    void noteOff() { for (int o = 0; o < 6; ++o) env[o].keydown(false); peg.keydown(false); keyIsDown = false; }
    bool isSilent() const {
        if (keyIsDown) return false;
        const uint8_t* flags = dx7tab::algorithms[algorithm];
        for (int o = 0; o < 6; ++o) if ((flags[o] & 7) == 4 && ((opMask >> o) & 1) && gain[o] > 1.5e-4) return false;
        return true;
    }
    // Renders exactly N samples into out[0..N-1]. baseFreq: Hz of the played note; cents: extra pitch offset.
    void renderBlock(float* out, double baseFreq, double cents) {
        const int64_t lfoVal = lfo.getsample(), lfoDelay = lfo.getdelay();
        const double pmd = double(pmdDepth) * double(lfoDelay);
        const double senslfo = double(pms) * double(lfoVal - 8388608);
        double pm1 = std::floor(std::fabs(pmd * senslfo) / 549755813888.0);
        if (senslfo < 0) pm1 = -pm1;
        const double pitchMod = (double(peg.getsample()) + pm1) / TWO24;
        const double lfoInv = TWO24 - double(lfoVal);
        double amod = std::floor(double(amdDepth) * double(lfoDelay) / 256.0);
        amod = std::floor(amod * lfoInv / TWO24);
        const double centsMul = std::pow(2.0, cents / 1200.0);
        const uint8_t* flagsArr = dx7tab::algorithms[algorithm];
        double* b0 = bus[0]; double* b1 = bus[1]; double* b2 = bus[2];
        for (int i = 0; i < N; ++i) { b0[i] = 0; b1[i] = 0; b2[i] = 0; }
        const double inv = 1.0 / sr;
        for (int o = 0; o < 6; ++o) {
            const int flags = flagsArr[o];
            Env& e = env[o];
            double freq, levelIn;
            if (!((opMask >> o) & 1)) { e.getsample(); levelIn = 0; freq = 0; }
            else {
                freq = mode[o] ? fixedHz[o] * centsMul : baseFreq * std::pow(2.0, deltaOct[o] + pitchMod) * centsMul;
                double level = double(e.getsample());
                if (ams[o] != 0) {
                    const double sens = std::floor(amod * double(ams[o]) / TWO24);
                    const double pt = std::floor(std::exp(sens / 262144.0 * 0.07 + 12.2));
                    level -= std::floor(level * pt * 16.0 / 268435456.0);
                }
                levelIn = level;
            }
            const double g1 = gain[o];
            const double g2 = std::pow(2.0, levelIn / TWO24 - 14.0);
            gain[o] = g2;
            const bool add = (flags & 4) != 0;
            const int inbus = (flags >> 4) & 3, outbus = flags & 3;
            double* outArr = bus[outbus]; const double* inArr = inbus ? bus[inbus] : nullptr;
            const bool fbOp = (flags & 0xc0) == 0xc0 && fbShift < 16;
            const double f = freq * inv;
            if (g1 >= 6.7e-5 || g2 >= 6.7e-5) {
                const double dg = (g2 - g1) / N;
                double g = g1, ph = phase[o];
                if (fbOp) {
                    double y0 = fb0, y1 = fb1; const double fbScale = 1.0 / double(1 << (fbShift + 1));
                    for (int i = 0; i < N; ++i) {
                        g += dg;
                        const double y = std::sin(TWO_PI * (ph + (y0 + y1) * fbScale)) * g;
                        y0 = y1; y1 = y;
                        if (add) outArr[i] += y; else outArr[i] = y;
                        ph += f;
                    }
                    fb0 = y0; fb1 = y1;
                } else if (inArr) {
                    for (int i = 0; i < N; ++i) {
                        g += dg;
                        const double y = std::sin(TWO_PI * (ph + inArr[i])) * g;
                        if (add) outArr[i] += y; else outArr[i] = y;
                        ph += f;
                    }
                } else {
                    for (int i = 0; i < N; ++i) {
                        g += dg;
                        const double y = std::sin(TWO_PI * ph) * g;
                        if (add) outArr[i] += y; else outArr[i] = y;
                        ph += f;
                    }
                }
            } else if (!add) { for (int i = 0; i < N; ++i) outArr[i] = 0; }
            double np = phase[o] + f * N; np -= std::floor(np); phase[o] = np;
        }
        for (int i = 0; i < N; ++i) out[i] = float(b0[i] * OUT_SCALE);
    }

    // Seconds the amplitude takes to die away after key-off (largest over all carriers).
    static double estimateRelease(const uint8_t* p, int noteNum, int velocity, double sr) {
        Voice v(sr); v.noteOn(p, noteNum, velocity, 0);
        const uint8_t* flags = dx7tab::algorithms[v.algorithm];
        double worst = 0;
        for (int o = 0; o < 6; ++o) {
            if ((flags[o] & 7) != 4 || !((v.opMask >> o) & 1)) continue;
            Env& e = v.env[o];
            e.level = e.target = 0; e.down = true; e.advance(2); e.level = e.target; e.keydown(false);
            int blocks = 0; const int maxBlocks = int(std::ceil(20 * v.sr / N));
            while (blocks < maxBlocks) { const int64_t lv = e.getsample(); ++blocks; if (lv < 23500000) break; if (e.ix >= 4) break; }
            worst = std::fmax(worst, blocks * double(N) / v.sr);
        }
        return worst;
    }
    int algorithmIndex() const { return algorithm; }
    Env env[6];

private:
    void opParams(const uint8_t* p, int midi_, int vel_, int o, bool initEnv) {
        const int off = o * 21;
        const int rates[4]  = { p[off], p[off + 1], p[off + 2], p[off + 3] };
        const int levels[4] = { p[off + 4], p[off + 5], p[off + 6], p[off + 7] };
        int ol = scaleOutLevel(p[off + 16]);
        ol += scaleLevel(midi_, p[off + 8], p[off + 9], p[off + 10], p[off + 11], p[off + 12]);
        if (ol > 127) ol = 127;
        ol = ol * 32;
        ol += scaleVelocity(vel_, p[off + 15]);
        if (ol < 0) ol = 0;
        const int rs = scaleRate(midi_, p[off + 13]);
        if (initEnv) env[o].init(rates, levels, ol, rs, srMult); else env[o].update(rates, levels, ol, rs);
        const int md = p[off + 17], coarse = p[off + 18], fine = p[off + 19], det = p[off + 20];
        mode[o] = md;
        if (md == 0) {
            const double L = std::log2(440.0) + (midi_ - 69) / 12.0;
            double oct = (0.0209 * std::exp(-0.396 * L) / 7.0) * L * (det - 7);
            oct += double(dx7tab::coarsemul[coarse & 31]) / TWO24;
            if (fine) oct += std::log2(1.0 + 0.01 * fine);
            deltaOct[o] = oct;
        } else {
            int64_t lf = int64_t(std::floor(double(int64_t(4458616) * int64_t((coarse & 3) * 100 + fine)) / 8.0));
            if (det > 7) lf += int64_t(13457) * (det - 7);
            fixedHz[o] = std::pow(2.0, double(lf) / TWO24);
            deltaOct[o] = 0;
        }
        ams[o] = AMS_TAB[p[off + 14] & 3];
    }
    void globals(const uint8_t* p) {
        algorithm = p[134] & 31;
        const int fb = p[135] & 7; fbShift = fb != 0 ? 8 - fb : 16;
        pmdDepth = (int64_t(p[139]) * 165) >> 6; pms = PMS_TAB[p[143] & 7]; amdDepth = (int64_t(p[140]) * 165) >> 6;
        opMask = p[155] & 63;
    }

    double sr = 44100.0; int64_t srMult = TWO24i;
    double phase[6] {}, gain[6] {}, deltaOct[6] {}, fixedHz[6] {};
    int mode[6] {}; int64_t ams[6] {};
    Lfo lfo; PitchEnv peg;
    double bus[3][N] {};
    double fb0 = 0, fb1 = 0, transposeOct = 0;
    int algorithm = 0, fbShift = 16, opMask = 63, midi = 60, vel = 100;
    int64_t pmdDepth = 0, pms = 0, amdDepth = 0;
};

// ---- patch utilities and SysEx (control thread only; these allocate) ----
inline void setName(uint8_t* p, const std::string& name) {
    for (int i = 0; i < 10; ++i) { const int c = i < int(name.size()) ? uint8_t(name[i]) : 32; p[145 + i] = (c >= 32 && c < 127) ? uint8_t(c) : 32; }
}
inline std::string getName(const uint8_t* p) {
    std::string s; for (int i = 145; i < 155; ++i) { const int c = p[i]; s += (c >= 32 && c < 127) ? char(c) : ' '; }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
inline void initPatch(uint8_t* p) {
    std::memset(p, 0, PATCH_SIZE);
    for (int o = 0; o < 6; ++o) {
        const int b = o * 21;
        p[b] = p[b + 1] = p[b + 2] = p[b + 3] = 99;
        p[b + 4] = p[b + 5] = p[b + 6] = 99; p[b + 7] = 0;
        p[b + 8] = 39; p[b + 16] = (o == 5) ? 99 : 0; p[b + 18] = 1; p[b + 20] = 7;
    }
    p[126] = p[127] = p[128] = p[129] = 99; p[130] = p[131] = p[132] = p[133] = 50;
    p[134] = 0; p[136] = 1; p[137] = 35; p[141] = 1; p[143] = 3; p[144] = 24;
    setName(p, "INIT VOICE"); p[155] = 63;
}
inline void unpackVoice(const uint8_t* b, uint8_t* p) {
    std::memset(p, 0, PATCH_SIZE);
    for (int op = 0; op < 6; ++op) {
        const int s = op * 17, d = op * 21;
        for (int i = 0; i < 11; ++i) p[d + i] = b[s + i] & 127;
        const int lr = b[s + 11]; p[d + 11] = lr & 3; p[d + 12] = (lr >> 2) & 3;
        const int dr = b[s + 12]; p[d + 13] = dr & 7; p[d + 20] = (dr >> 3) & 15;
        const int ak = b[s + 13]; p[d + 14] = ak & 3; p[d + 15] = (ak >> 2) & 7;
        p[d + 16] = b[s + 14] & 127;
        const int fm = b[s + 15]; p[d + 17] = fm & 1; p[d + 18] = (fm >> 1) & 31;
        p[d + 19] = b[s + 16] & 127;
    }
    for (int i = 0; i < 8; ++i) p[126 + i] = b[102 + i] & 127;
    p[134] = b[110] & 31;
    const int ofb = b[111]; p[135] = ofb & 7; p[136] = (ofb >> 3) & 1;
    for (int i = 0; i < 4; ++i) p[137 + i] = b[112 + i] & 127;
    const int lf = b[116]; p[141] = lf & 1; p[142] = (lf >> 1) & 7; p[143] = (lf >> 4) & 7;
    p[144] = b[117] & 127;
    for (int i = 0; i < 10; ++i) p[145 + i] = b[118 + i] & 127;
    p[155] = 63;
}
inline void packVoice(const uint8_t* p, uint8_t* b) {
    std::memset(b, 0, 128);
    for (int op = 0; op < 6; ++op) {
        const int s = op * 17, d = op * 21;
        for (int i = 0; i < 11; ++i) b[s + i] = p[d + i];
        b[s + 11] = (p[d + 11] & 3) | ((p[d + 12] & 3) << 2);
        b[s + 12] = (p[d + 13] & 7) | ((p[d + 20] & 15) << 3);
        b[s + 13] = (p[d + 14] & 3) | ((p[d + 15] & 7) << 2);
        b[s + 14] = p[d + 16];
        b[s + 15] = (p[d + 17] & 1) | ((p[d + 18] & 31) << 1);
        b[s + 16] = p[d + 19];
    }
    for (int i = 0; i < 8; ++i) b[102 + i] = p[126 + i];
    b[110] = p[134]; b[111] = (p[135] & 7) | ((p[136] & 1) << 3);
    for (int i = 0; i < 4; ++i) b[112 + i] = p[137 + i];
    b[116] = (p[141] & 1) | ((p[142] & 7) << 1) | ((p[143] & 7) << 4);
    b[117] = p[144];
    for (int i = 0; i < 10; ++i) b[118 + i] = p[145 + i];
}
inline int checksum(const uint8_t* u8, size_t start, size_t len) { int s = 0; for (size_t i = 0; i < len; ++i) s += u8[start + i]; return (-s) & 127; }

struct SysexResult { std::vector<std::vector<uint8_t>> voices; std::vector<std::string> warnings; };
// Accepts 32-voice bulk dumps (4104 bytes), single-voice dumps (163 bytes), several messages concatenated, and headerless
// raw dumps of 4096 bytes / multiples of 128 bytes.
inline SysexResult parseSysex(const uint8_t* u8, size_t len) {
    SysexResult res; size_t i = 0;
    while (i < len) {
        if (u8[i] != 0xF0) { ++i; continue; }
        if (i + 6 <= len && u8[i + 1] == 0x43) {
            const int fmt = u8[i + 3];
            if (fmt == 0x09 && i + 4104 <= len) {
                if (checksum(u8, i + 6, 4096) != u8[i + 6 + 4096]) res.warnings.push_back("checksum mismatch in bulk dump (loaded anyway)");
                for (int v = 0; v < 32; ++v) { std::vector<uint8_t> p(PATCH_SIZE); unpackVoice(u8 + i + 6 + v * 128, p.data()); res.voices.push_back(std::move(p)); }
                i += 4104; continue;
            }
            if (fmt == 0x00 && i + 163 <= len) {
                std::vector<uint8_t> p(PATCH_SIZE, 0);
                for (int k = 0; k < 155; ++k) p[k] = u8[i + 6 + k] & 127;
                p[155] = 63;
                if (checksum(u8, i + 6, 155) != u8[i + 6 + 155]) res.warnings.push_back("checksum mismatch in single voice (loaded anyway)");
                res.voices.push_back(std::move(p)); i += 163; continue;
            }
        }
        ++i;
    }
    if (res.voices.empty() && (len == 4096 || (len > 0 && len % 128 == 0 && len <= 4096))) {
        for (size_t v = 0; v < len / 128; ++v) { std::vector<uint8_t> p(PATCH_SIZE); unpackVoice(u8 + v * 128, p.data()); res.voices.push_back(std::move(p)); }
        res.warnings.push_back("headerless dump interpreted as packed voices");
    }
    return res;
}
inline std::vector<uint8_t> exportSingleSysex(const uint8_t* p, int channel) {
    std::vector<uint8_t> m(163, 0);
    m[0] = 0xF0; m[1] = 0x43; m[2] = uint8_t(channel & 15); m[3] = 0; m[4] = 1; m[5] = 27;
    for (int i = 0; i < 155; ++i) m[6 + i] = p[i] & 127;
    m[161] = uint8_t(checksum(m.data(), 6, 155)); m[162] = 0xF7;
    return m;
}

}} // namespace vv::dx7
