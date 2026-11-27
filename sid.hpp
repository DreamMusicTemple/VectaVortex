// Vektur Vortex SID layer DSP: C++ port of the web app's 'sid-processor' (three voices computed together so ring mod and hard
// sync see the neighbour voice's phase in the very same sample). Original SID-inspired design (bitmask waveforms with the chip's
// AND-combination, 23-bit noise LFSR, top-bit XOR ring mod, bucketed non-linear envelope) - not a reSID port.
// Real-time safe: fixed state, no allocation.
#pragma once
#include <cmath>
#include <cstdint>

namespace vv {

// Tracker-style instrument extras (port of the web app's SID instrument panel): vibrato, pulse-width sweep/table, SVF filter with sweep/table,
// hard restart, tie and sustain-hold. `active` false (the default) leaves the original chip output bit-for-bit unchanged.
constexpr int kSidTableMax = 32;
struct SidPwStep { int len = 8; double sweep = 0; bool set = false; double val = 0.5; };
struct SidFStep { int len = 8; double cut = 512, sweep = 0; bool set = false, hp = false, bp = false, lp = false; };
struct SidInstr {
    bool active = false;
    int hr = 0; bool tie = false, susHold = false;
    bool vibOn = true; double vibDelay = 0, vibRate = 6, vibAmp = 0, vibInc = 0;
    double pwSweep = 0, pwMin = 0.1, pwMax = 0.9; bool pwTab = false, pwLoop = true, pwSync = true; int pwN = 0; SidPwStep pwS[kSidTableMax];
    bool fRoute = true, lp = false, bp = false, hp = false; double fCut = 1024, fSweep = 0, fMin = 0, fMax = 2047, fReso = 0;
    bool fTab = false, fLoop = true, fSync = true; int fN = 0; SidFStep fS[kSidTableMax];
};

struct SidVoiceParams {
    SidInstr ins;
    double freq = 220, pw = 0.5, atk = 0.01, dec = 0.1, sus = 0.7, rel = 0.2;
    int wave = 1;               // bitmask: 1 triangle, 2 saw, 4 pulse, 8 noise (bits may combine)
    bool ring = false, sync = false, gate = false;
    int rt = 0;                 // envelope-repeat retrigger counter: a change while the gate is on restarts Attack from the current level
};

class SidChip {
public:
    SidChip() { reset(); }
    void pulseReset() { for (int v = 0; v < 3; ++v) resetPulse(v); }
    void filterReset() { for (int v = 0; v < 3; ++v) resetFilter(v); }
    void reset() { frameAcc = 0; for (int v = 0; v < 3; ++v) { prevGate[v] = false; vibT[v] = 0; vibPh[v] = 0; vibMult[v] = 1; pwOff[v] = 0; fOff[v] = 0; resetPulse(v); resetFilter(v); svfI1[v] = svfI2[v] = 0; svfK[v] = 1; svfA[v][0] = svfA[v][1] = svfA[v][2] = 0; hrCount[v] = hrStep[v] = 0; pwOut[v] = 0.5; fModeSet[v] = false; phase[v] = 0; lfsr[v] = 0x7ffff8u; envLevel[v] = 0; envStage[v] = 0; decCount[v] = 0; relCount[v] = 0; lastRt[v] = 0; } }
    // Renders n samples; out[v][i] is voice v's own output (before the layer's per-voice level / pan).
    void process(const SidVoiceParams* p, double sr, float* const* out, int n) {
        double pwP[3];
        for (int v = 0; v < 3; ++v) {
            pwP[v] = p[v].pw;
            if (p[v].gate && !prevGate[v]) {
                gateOn(v, p[v].ins, p[v].sus);
                if (p[v].ins.active && p[v].ins.hr > 0 && envLevel[v] > 0.02) startHardRestart(v, p[v].ins.hr, sr);
            }
            prevGate[v] = p[v].gate;
            if (p[v].rt != lastRt[v] && p[v].gate && envStage[v] != 0 && p[v].ins.active && p[v].ins.hr > 0 && envLevel[v] > 0.02) { lastRt[v] = p[v].rt; startHardRestart(v, p[v].ins.hr, sr); vibT[v] = 0; }
        }
        for (int i = 0; i < n; ++i) {
            frameAcc += 50.0 / sr;
            if (frameAcc >= 1) { frameAcc -= 1; for (int v = 0; v < 3; ++v) if (p[v].ins.active && p[v].gate) { framePulse(v, p[v].ins, pwP[v]); frameFilter(v, p[v].ins); } }
            if ((i & 31) == 0) {
                for (int v = 0; v < 3; ++v) {
                    const SidInstr& c = p[v].ins; double m = 1;
                    if (!c.active) { vibMult[v] = 1; pwOut[v] = pwP[v]; continue; }
                    vibT[v] += 32 / sr;
                    if (c.vibOn && c.vibAmp > 0) {
                        const double dly = c.vibDelay * 0.1, ramp = c.vibInc > 0 ? (16 - c.vibInc) * 0.06 : 0;
                        if (vibT[v] >= dly) {
                            const double g = ramp > 0 ? std::fmin(1.0, (vibT[v] - dly) / ramp) : 1.0;
                            vibPh[v] += (1 + c.vibRate) * 32 / sr; if (vibPh[v] > 1) vibPh[v] -= 1;
                            m = std::pow(2.0, (c.vibAmp * 10 * g * std::sin(6.283185307 * vibPh[v])) / 1200.0);
                        }
                    }
                    vibMult[v] = m;
                    pwOut[v] = std::fmin(0.99, std::fmax(0.01, pwP[v] + pwOff[v]));
                    const double fc = std::fmin(2047.0, std::fmax(0.0, c.fCut + fOff[v]));
                    const double hz = std::fmin(sr * 0.45, 30 * std::pow(2.0, fc / 2047 * 8.5));
                    const double gg = std::tan(3.14159265358979323846 * hz / sr), k = 2 - std::fmin(15.0, std::fmax(0.0, c.fReso)) / 15 * 1.93;
                    const double a1 = 1 / (1 + gg * (gg + k)); svfK[v] = k; svfA[v][0] = a1; svfA[v][1] = gg * a1; svfA[v][2] = gg * gg * a1;
                }
            }
            bool wrapped[3] = {false, false, false}; int topBit[3] = {0, 0, 0};
            for (int v = 0; v < 3; ++v) {
                phase[v] += p[v].freq * vibMult[v] / sr;
                if (phase[v] >= 1) { phase[v] -= 1; wrapped[v] = true; }
                topBit[v] = phase[v] >= 0.5 ? 1 : 0;
            }
            for (int v = 0; v < 3; ++v) { const int nb = (v + 2) % 3; if (p[v].sync && wrapped[nb]) phase[v] = 0; }
            for (int v = 0; v < 3; ++v) {
                double s = 0; const int w = p[v].wave;
                const bool hasTri = w & 1, hasSaw = w & 2, hasPulse = w & 4, hasNoise = w & 8;
                if (hasNoise) {
                    const uint32_t bit = ((lfsr[v] >> 22) ^ (lfsr[v] >> 17)) & 1u;
                    lfsr[v] = (((lfsr[v] << 1) | bit)) & 0x7fffffu;
                    const uint32_t L = lfsr[v];
                    const uint32_t o = (((L >> 20) & 1u) << 7) | (((L >> 18) & 1u) << 6) | (((L >> 14) & 1u) << 5) | (((L >> 11) & 1u) << 4) | (((L >> 9) & 1u) << 3) | (((L >> 5) & 1u) << 2) | (((L >> 2) & 1u) << 1) | (L & 1u);
                    s = (o / 127.5) - 1;
                } else if (hasTri || hasSaw || hasPulse) {
                    const int nb = (v + 2) % 3;
                    const double lowPart = std::fmod(phase[v] * 2, 1.0);
                    const int msb = p[v].ring ? (topBit[v] ^ topBit[nb]) : topBit[v];
                    const double triRamp = msb ? (1 - lowPart) : lowPart;
                    const double sawRamp = phase[v];
                    const bool pulseHigh = phase[v] < pwOut[v];
                    const int active = (hasTri ? 1 : 0) + (hasSaw ? 1 : 0) + (hasPulse ? 1 : 0);
                    if (active == 1) { if (hasTri) s = triRamp * 2 - 1; else if (hasSaw) s = sawRamp * 2 - 1; else s = pulseHigh ? 1 : -1; }
                    else {
                        const int TRI12 = (int)std::floor(triRamp * 4095 + 0.5), SAW12 = (int)std::floor(sawRamp * 4095 + 0.5), PUL12 = pulseHigh ? 4095 : 0;
                        int combined = 4095; if (hasTri) combined &= TRI12; if (hasSaw) combined &= SAW12; if (hasPulse) combined &= PUL12;
                        s = (combined / 2047.5) - 1;
                    }
                }
                const double env = stepEnvelope(v, p[v], sr);
                double o = s * env;
                const SidInstr& cF = p[v].ins;
                if (cF.active && cF.fRoute) {
                    bool l = cF.lp, b = cF.bp, h = cF.hp; if (fModeSet[v]) { l = fmLP[v]; b = fmBP[v]; h = fmHP[v]; }
                    if (l || b || h) {
                        const double k = svfK[v], A0 = svfA[v][0], A1 = svfA[v][1], A2 = svfA[v][2];
                        const double v3 = o - svfI2[v], v1 = A0 * svfI1[v] + A1 * v3, v2 = svfI2[v] + A1 * svfI1[v] + A2 * v3;
                        svfI1[v] = 2 * v1 - svfI1[v]; svfI2[v] = 2 * v2 - svfI2[v];
                        double y = 0; if (l) y += v2; if (b) y += v1; if (h) y += o - k * v1 - v2;
                        o = (y > 1 || y < -1) ? std::tanh(y) : y;
                    }
                }
                out[v][i] = (float)o;
            }
        }
    }
    bool silent() const { for (int v = 0; v < 3; ++v) if (envStage[v] != 0) return false; return true; }

private:
    void startHardRestart(int v, int hr, double sr) { envStage[v] = 5; hrCount[v] = std::fmax(1.0, hr * 0.02 * sr); hrStep[v] = envLevel[v] / hrCount[v]; }
    void resetPulse(int v) { pwOff[v] = 0; pwDir[v] = 1; pwIdx[v] = 0; pwCnt[v] = 0; pwEnt[v] = true; pwDone[v] = false; }
    void resetFilter(int v) { fOff[v] = 0; fDir[v] = 1; fIdx[v] = 0; fCnt[v] = 0; fEnt[v] = true; fDone[v] = false; fModeSet[v] = false; }
    void gateOn(int v, const SidInstr& c, double sus) {
        vibT[v] = 0;
        if (!c.active) return;
        if (c.pwSync) { pwIdx[v] = 0; pwCnt[v] = 0; pwEnt[v] = true; pwDone[v] = false; }
        if (c.fSync) { fIdx[v] = 0; fCnt[v] = 0; fEnt[v] = true; fDone[v] = false; }
        if (c.tie) { envLevel[v] = (float)sus; envStage[v] = 3; }
    }
    void framePulse(int v, const SidInstr& c, double base) {
        if (c.pwTab && c.pwN > 0) {
            const int n = c.pwN;
            if (pwIdx[v] >= n) { pwIdx[v] = c.pwLoop ? 0 : n - 1; pwCnt[v] = 0; pwEnt[v] = true; }
            const SidPwStep& st = c.pwS[pwIdx[v]];
            if (pwEnt[v]) { pwEnt[v] = false; if (st.set) pwOff[v] = std::fmin(0.99, std::fmax(0.01, st.val)) - base; }
            if (!pwDone[v]) {
                pwOff[v] += st.sweep / 127 * 0.04;
                const double ab = base + pwOff[v]; if (ab > 0.99) pwOff[v] = 0.99 - base; if (ab < 0.01) pwOff[v] = 0.01 - base;
                pwCnt[v] += 1;
                if (pwCnt[v] >= std::max(1, st.len)) { pwCnt[v] = 0; pwIdx[v] += 1; pwEnt[v] = true; if (pwIdx[v] >= n) { if (c.pwLoop) pwIdx[v] = 0; else { pwIdx[v] = n - 1; pwDone[v] = true; pwEnt[v] = false; } } }
            }
        } else if (c.pwSweep != 0) {
            double lo = std::fmin(c.pwMin, c.pwMax), hi = std::fmax(c.pwMin, c.pwMax); if (hi - lo < 0.01) hi = lo + 0.01;
            double ab = base + pwOff[v] + (c.pwSweep > 0 ? 1 : -1) * pwDir[v] * std::fabs(c.pwSweep) / 127 * 0.04;
            if (ab > hi) { ab = hi; pwDir[v] = -pwDir[v]; } else if (ab < lo) { ab = lo; pwDir[v] = -pwDir[v]; }
            pwOff[v] = ab - base;
        } else pwOff[v] = 0;
    }
    void frameFilter(int v, const SidInstr& c) {
        const double base = c.fCut;
        if (c.fTab && c.fN > 0) {
            const int n = c.fN;
            if (fIdx[v] >= n) { fIdx[v] = c.fLoop ? 0 : n - 1; fCnt[v] = 0; fEnt[v] = true; }
            const SidFStep& st = c.fS[fIdx[v]];
            if (fEnt[v]) { fEnt[v] = false; if (st.set) fOff[v] = std::fmin(2047.0, std::fmax(0.0, st.cut)) - base; if (st.hp || st.bp || st.lp) { fModeSet[v] = true; fmLP[v] = st.lp; fmBP[v] = st.bp; fmHP[v] = st.hp; } }
            if (!fDone[v]) {
                fOff[v] += st.sweep * 0.6;
                const double ab = base + fOff[v]; if (ab > 2047) fOff[v] = 2047 - base; if (ab < 0) fOff[v] = -base;
                fCnt[v] += 1;
                if (fCnt[v] >= std::max(1, st.len)) { fCnt[v] = 0; fIdx[v] += 1; fEnt[v] = true; if (fIdx[v] >= n) { if (c.fLoop) fIdx[v] = 0; else { fIdx[v] = n - 1; fDone[v] = true; fEnt[v] = false; } } }
            }
        } else if (c.fSweep != 0) {
            double lo = std::fmin(c.fMin, c.fMax), hi = std::fmax(c.fMin, c.fMax); if (hi - lo < 8) hi = lo + 8;
            double ab = base + fOff[v] + (c.fSweep > 0 ? 1 : -1) * fDir[v] * std::fabs(c.fSweep) * 0.6;
            if (ab > hi) { ab = hi; fDir[v] = -fDir[v]; } else if (ab < lo) { ab = lo; fDir[v] = -fDir[v]; }
            fOff[v] = ab - base; fModeSet[v] = false;
        } else { fOff[v] = 0; fModeSet[v] = false; }
    }
    bool prevGate[3]; double vibT[3], vibPh[3], vibMult[3], pwOff[3], fOff[3], svfI1[3], svfI2[3], svfK[3], svfA[3][3], hrCount[3], hrStep[3], pwOut[3], frameAcc = 0;
    int pwDir[3], pwIdx[3], pwCnt[3], fDir[3], fIdx[3], fCnt[3]; bool pwEnt[3], pwDone[3], fEnt[3], fDone[3], fModeSet[3], fmLP[3] = {false,false,false}, fmBP[3] = {false,false,false}, fmHP[3] = {false,false,false};
    double stepEnvelope(int v, const SidVoiceParams& P, double sr) {
        const bool gate = P.gate;
        if (envStage[v] == 5) {
            hrCount[v] -= 1; envLevel[v] = (float)std::fmax(0.0, (double)envLevel[v] - hrStep[v]);
            if (hrCount[v] <= 0) { envLevel[v] = 0; envStage[v] = gate ? 1 : 0; }
            return std::fmax(0.0, (double)envLevel[v]);
        }
        if (P.rt != lastRt[v]) { lastRt[v] = P.rt; if (gate && envStage[v] != 0) { envStage[v] = 1; decCount[v] = 0; } }
        if (gate && envStage[v] == 0) envStage[v] = 1;
        if (!gate && envStage[v] != 0 && envStage[v] != 4) envStage[v] = 4;
        if (!gate && envStage[v] == 0) return 0;
        const int levelByte = (int)std::floor((double)envLevel[v] * 255 + 0.5);
        int div = 1;
        if (levelByte < 6) div = 30; else if (levelByte < 14) div = 16; else if (levelByte < 26) div = 8; else if (levelByte < 54) div = 4; else if (levelByte < 93) div = 2;
        if (envStage[v] == 1) {
            envLevel[v] = (float)((double)envLevel[v] + 1 / std::fmax(0.001, P.atk) / sr);
            if (envLevel[v] >= 1) { envLevel[v] = 1; envStage[v] = 2; }
        } else if (envStage[v] == 2) {
            const double stepEvery = std::fmax(1.0, std::floor((P.dec * sr / 255) * div + 0.5));
            if (P.ins.active && P.ins.susHold) { envLevel[v] = (float)std::fmax((double)envLevel[v], P.sus); envStage[v] = 3; return (double)envLevel[v]; }
            decCount[v] += 1;
            if (decCount[v] >= stepEvery) { decCount[v] = 0; envLevel[v] = (float)((double)envLevel[v] - 1.0 / 255); if ((double)envLevel[v] <= P.sus) { envLevel[v] = (float)P.sus; envStage[v] = 3; } }
        } else if (envStage[v] == 3) { if (!(P.ins.active && P.ins.susHold)) envLevel[v] = (float)P.sus; }
        else if (envStage[v] == 4) {
            const double stepEvery = std::fmax(1.0, std::floor((P.rel * sr / 255) * div + 0.5));
            relCount[v] += 1;
            if (relCount[v] >= stepEvery) { relCount[v] = 0; envLevel[v] = (float)((double)envLevel[v] - 1.0 / 255); if (envLevel[v] <= 0) { envLevel[v] = 0; envStage[v] = 0; } }
        }
        return std::fmax(0.0, (double)envLevel[v]);
    }
    double phase[3]; uint32_t lfsr[3]; float envLevel[3]; int envStage[3]; int lastRt[3] = {0, 0, 0}; float decCount[3], relCount[3];
};

} // namespace vv
