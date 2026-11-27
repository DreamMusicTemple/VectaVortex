// Wooden flutes / physical breath: C++ port of FluteCore (web app) - same algorithm, same constants, same seeded noise.
//   breath pressure -> jet (delay + instability gain + cubic labium nonlinearity) -> bidirectional waveguide bore
//   (4-point Lagrange fractional delay, three-port tone-hole junctions with shunt inertance, dispersion, wall loss, end reflection) -> radiation.
// Honest scope: the oscillation loop is a physical model; instrument profiles, breath-noise colouring, hole charts and the player gestures
// (meri, kari, mura-iki, koro-koro, tamane) are tuned approximations, and the pitch is kept in tune with a calibrated per-instrument pull table.
// No allocation after construction.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace vv {
namespace flute {

struct Profile { const char* name; int id; double asym; int ends; double lip, root; int nHoles; int holes[8]; double jet, gain, nl, noise, direct, chiff, bias, mouthR, taper, flare, diam; int plenum; int nPull; double pull[32][2]; };
inline const Profile kProfiles[7] = {
  { "shakuhachi", 0, 0.55, 0, 1, 293.66, 5, {3,5,7,10,12,0,0,0}, 0.3, 1, 1, 1, 1, 0.15, -0.1, 0.62, 0.6, 0.55, 0.55, 0, 30, {{6,0.054},{6.61,0.054},{6.8,0.0577},{6.9,0.0418},{7,0.0277},{7.1,0.0181},{7.2,0.0136},{7.3,0.0136},{7.4,0.0132},{7.5,0.0124},{7.6,0.014},{7.7,0.0126},{7.8,0.0128},{7.9,0.0181},{8,0.0259},{8.1,0.0408},{8.2,0.0372},{8.3,0.0335},{8.4,0.031},{8.5,0.028},{8.6,0.027},{8.7,0.0255},{9.03,0.024},{9.2,0.023},{9.45,0.0224},{9.78,0.019},{10.03,0.0163},{10.37,0.0173},{10.78,0.0238},{11.5,0.03}} },
  { "nativeAmerican", 1, 0.22, 0, 0.12, 440, 6, {3,5,7,10,12,15,0,0}, 0.32, 0.85, 0.8, 0.45, 0.45, 0.35, 0, 0.9, 0.2, 0.1, 0.65, 1, 17, {{6.8,0.0255},{7.05,0.0252},{7.3,0.027},{7.55,0.0288},{7.8,0.0284},{8.05,0.0292},{8.3,0.0266},{8.55,0.0243},{8.8,0.0206},{9.05,0.0168},{9.3,0.0146},{9.55,0.0137},{9.8,0.0137},{10.05,0.0145},{10.3,0.0164},{10.55,0.0195},{10.8,0.0245}} },
  { "shinobue", 2, 0.38, 0, 0.85, 587.33, 6, {2,4,5,7,9,11,0,0}, 0.29, 1.1, 1.15, 0.5, 0.55, 0.2, 0.12, 0.7, 0, 0.05, 0.35, 0, 17, {{6.8,0.039},{7.05,0.0377},{7.3,0.0388},{7.55,0.0396},{7.8,0.0386},{8.05,0.0403},{8.3,0.0387},{8.55,0.0382},{8.8,0.0362},{9.05,0.0327},{9.3,0.0294},{9.55,0.0279},{9.8,0.0277},{10.05,0.0287},{10.3,0.0308},{10.55,0.0339},{10.8,0.0405}} },
  { "quena", 3, 0.45, 0, 0.95, 392, 6, {2,4,5,7,9,11,0,0}, 0.31, 0.95, 0.95, 0.85, 0.85, 0.18, -0.05, 0.66, 0.1, 0.15, 0.6, 0, 17, {{6.8,0.0476},{7.05,0.0392},{7.3,0.0425},{7.55,0.0478},{7.8,0.0465},{8.05,0.0463},{8.3,0.039},{8.55,0.0328},{8.8,0.0272},{9.05,0.0236},{9.3,0.0224},{9.55,0.0222},{9.8,0.0219},{10.05,0.0213},{10.3,0.0223},{10.55,0.0255},{10.8,0.0339}} },
  { "recorder", 4, 0.22, 0, 0.1, 523.25, 7, {2,4,5,7,9,11,12,0}, 0.33, 0.9, 0.9, 0.35, 0.35, 0.55, 0, 0.92, 0.35, 0, 0.4, 0, 17, {{6.8,0.023},{7.05,0.0233},{7.3,0.025},{7.55,0.0271},{7.8,0.0273},{8.05,0.0288},{8.3,0.0275},{8.55,0.0262},{8.8,0.0228},{9.05,0.0183},{9.3,0.0144},{9.55,0.0129},{9.8,0.0122},{10.05,0.0133},{10.3,0.0149},{10.55,0.0172},{10.8,0.0211}} },
  { "bambooFlute", 5, 0.38, 0, 0.9, 293.66, 6, {2,4,5,7,9,11,0,0}, 0.3, 1, 1.05, 0.6, 0.65, 0.15, 0.06, 0.72, 0, 0.08, 0.45, 0, 17, {{6.8,0.0596},{7.05,0.0574},{7.3,0.0595},{7.55,0.0562},{7.8,0.0496},{8.05,0.0426},{8.3,0.0313},{8.55,0.0253},{8.8,0.0229},{9.05,0.0224},{9.3,0.0228},{9.55,0.0237},{9.8,0.0247},{10.05,0.0256},{10.3,0.0273},{10.55,0.0303},{10.8,0.0366}} },
  { "panFlute", 6, 0.35, 1, 0.9, 261.63, 0, {0,0,0,0,0,0,0,0}, 0.27, 1.2, 1.1, 1.25, 1.1, 0.3, 0.04, 0.6, 0, 0, 0.5, 0, 17, {{6.8,0.0855},{7.05,0.0803},{7.3,0.0738},{7.55,0.0659},{7.8,0.0632},{8.05,0.0581},{8.3,0.0526},{8.55,0.0505},{8.8,0.0486},{9.05,0.0475},{9.3,0.0475},{9.55,0.049},{9.8,0.0512},{10.05,0.0547},{10.3,0.0601},{10.55,0.0683},{10.8,0.0816}} },
};
inline const double kDefaultPull[25][2] = {{6,0.054},{6.61,0.054},{6.78,0.0575},{6.95,0.0334},{7.03,0.0227},{7.2,0.0147},{7.37,0.0141},{7.45,0.0112},{7.61,0.0134},{7.78,0.0125},{7.95,0.0245},{8.03,0.0384},{8.2,0.0364},{8.36,0.0315},{8.45,0.0292},{8.61,0.0266},{8.78,0.0247},{9.03,0.024},{9.2,0.023},{9.45,0.0224},{9.78,0.019},{10.03,0.0163},{10.37,0.0173},{10.78,0.0238},{11.5,0.03}};
struct ParamDef { const char* section; const char* key; const char* label; double def, lo, hi; };
inline const ParamDef kParams[43] = {
  { "Breath", "pressure", "Breath Pressure", 0.5, 0, 1 },
  { "Breath", "velocity", "Breath Velocity", 0.5, 0, 1 },
  { "Breath", "intensity", "Breath Intensity", 0.6, 0, 1 },
  { "Breath", "noise", "Breath Noise", 0.35, 0, 1 },
  { "Breath", "turbulence", "Breath Turbulence", 0.4, 0, 1 },
  { "Breath", "colour", "Breath Colour", 0.5, 0, 1 },
  { "Breath", "irregularity", "Breath Irregularity", 0.3, 0, 1 },
  { "Breath", "stability", "Breath Stability", 0.7, 0, 1 },
  { "Breath", "attack", "Breath Attack", 0.3, 0, 1 },
  { "Breath", "release", "Breath Release", 0.3, 0, 1 },
  { "Breath", "continuity", "Breath Continuity", 0.85, 0, 1 },
  { "Embouchure", "aperture", "Lip Aperture", 0.5, 0, 1 },
  { "Embouchure", "tension", "Lip Tension", 0.5, 0, 1 },
  { "Embouchure", "angle", "Jet Angle", 0.5, 0, 1 },
  { "Embouchure", "width", "Jet Width", 0.5, 0, 1 },
  { "Embouchure", "thickness", "Jet Thickness", 0.45, 0, 1 },
  { "Embouchure", "position", "Embouchure Position", 0.5, 0, 1 },
  { "Embouchure", "coverage", "Embouchure Coverage", 0.4, 0, 1 },
  { "Embouchure", "edge", "Edge Interaction", 0.5, 0, 1 },
  { "Resonator", "boreLength", "Bore Length", 0.5, 0, 1 },
  { "Resonator", "boreDiameter", "Bore Diameter", 0.5, 0, 1 },
  { "Resonator", "taper", "Bore Taper", 0.5, 0, 1 },
  { "Resonator", "flare", "Bore Flare", 0.4, 0, 1 },
  { "Resonator", "resonance", "Resonance", 0.6, 0, 1 },
  { "Resonator", "damping", "Resonance Damping", 0.4, 0, 1 },
  { "Resonator", "boreLoss", "Bore Loss", 0.4, 0, 1 },
  { "Resonator", "airColumn", "Air Column Feedback", 0.6, 0, 1 },
  { "Resonator", "endReflection", "End Reflection", 0.8, 0, 1 },
  { "Resonator", "radiation", "Radiation", 0.5, 0, 1 },
  { "Resonator", "holeCover", "Partial Hole Cover", 0, 0, 1 },
  { "Expressive", "meri", "Meri", 0, 0, 1 },
  { "Expressive", "kari", "Kari", 0, 0, 1 },
  { "Expressive", "muraIki", "Mura-iki", 0, 0, 1 },
  { "Expressive", "koroKoro", "Koro-koro", 0, 0, 1 },
  { "Expressive", "tamane", "Tamane", 0, 0, 1 },
  { "Expressive", "vibrato", "Breath Vibrato", 0.15, 0, 1 },
  { "Expressive", "vibRate", "Vibrato Rate", 0.35, 0, 1 },
  { "Expressive", "pitchInstability", "Pitch Instability", 0.2, 0, 1 },
  { "Expressive", "flutter", "Airflow Flutter", 0, 0, 1 },
  { "Expressive", "dynInstability", "Dynamic Instability", 0.2, 0, 1 },
  { "Expressive", "articulation", "Articulation", 0.5, 0, 1 },
  { "Expressive", "microtonal", "Microtonal Expression", 0, -1, 1 },
  { "Expressive", "natural", "Natural Variation", 0.5, 0, 1 }
};

constexpr int kNumParams = 43;
inline int paramIndex(const std::string& k) { for (int i = 0; i < kNumParams; i++) if (k == kParams[i].key) return i; return -1; }
inline int profileIndex(const std::string& n) { for (int i = 0; i < 7; i++) if (n == kProfiles[i].name) return i; return 0; }

struct Telemetry { double env = 0, pressure = 0, velocity = 0, turb = 0, jet = 0, bore = 0, f = 0, mura = 0, mode = 0; };

class Core {
public:
    Core() : Core(48000.0, 12345u) {}
    void setSeed(uint32_t seed) { rng_ = seed ? seed : 1u; }
    Core(double sr, uint32_t seed) : sr_(sr) {
        int N = 1024; while (N < sr / 14 + 256) N <<= 1; N_ = N; mask_ = N - 1;
        rg_.assign((size_t)N, 0.f); lg_.assign((size_t)N, 0.f); jd_.assign((size_t)N, 0.f);
        rng_ = (seed ? seed : 1u);
        for (int i = 0; i < kNumParams; i++) P_[i] = T_[i] = kParams[i].def;
        prof_ = &kProfiles[0]; smK_ = 1 - std::exp(-16 / (sr * 0.02));
    }
    void setInstrument(int idx) { prof_ = &kProfiles[std::max(0, std::min(6, idx))]; }
    void setParam(int i, double v) { if (i >= 0 && i < kNumParams && std::isfinite(v)) T_[i] = v; }
    void snapParams() { for (int i = 0; i < kNumParams; i++) P_[i] = T_[i]; }
    double param(int i) const { return T_[i]; }
    void noteOn(double vel = 0.8) { gate_ = 1; t_ = 0; burst_ = 1; mura_ = 1; vel_ = vel; }
    void noteOff() { gate_ = 0; }
    void reset() { std::fill(rg_.begin(), rg_.end(), 0.f); std::fill(lg_.begin(), lg_.end(), 0.f); std::fill(jd_.begin(), jd_.end(), 0.f); for (int i = 0; i < 8; i++) holeU_[i] = 0;
        lossY_ = endY_ = apX_ = apY_ = dcX_ = dcY_ = 0; srcDc_ = srcDcY_ = 0; s1_ = s2_ = 0; jl1_ = jl2_ = 0; radPrev_ = 0; }
    bool gate() const { return gate_ != 0; }
    double envelope() const { return env_; }
    Telemetry telemetry() { Telemetry T; const double n = std::max(1, taN_); T.env = env_; T.pressure = pRel_; T.velocity = uj_; T.turb = std::sqrt(taTurb_ / n); T.jet = std::sqrt(taJet_ / n); T.bore = std::sqrt(taBore_ / n); T.f = fHz_; T.mura = mura_; T.mode = tauJ_ / std::max(1.0, sr_ / fHz_);
        taTurb_ = taJet_ = taBore_ = 0; taN_ = 0; return T; }
    // render n samples; fArr = per-sample frequency or null (then f0 is used)
    void render(float* out, int n, const float* fArr, double f0) {
        const int mask = mask_; int i = 0;
        while (i < n) {
            const int m = std::min(16, n - i);
            control(fArr ? fArr[i] : f0);
            const double dLr = (LrT_ - Lr_) / m, dTau = (tauT_ - tauJ_) / m;
            const double g = svfG_, kq = svfK_, a1 = 1 / (1 + g * (g + kq)), a2 = g * a1, a3 = g * a2;
            for (int s = 0; s < m; s++, i++) {
                Lr_ += dLr; tauJ_ += dTau; const int wi = wi_;
                const double w = rnd();
                pk0_ = 0.99765 * pk0_ + w * 0.0990460; pk1_ = 0.96300 * pk1_ + w * 0.2965164; pk2_ = 0.57000 * pk2_ + w * 1.0526913;
                const double pink = (pk0_ + pk1_ + pk2_ + w * 0.1848) * 0.18, cl = colour_;
                const double aN = 1 - std::exp(-2 * M_PI * (700 + 9000 * cl * cl) * (0.5 + 0.5 * std::min(1.5, uj_)) / sr_);
                nLp_ += (w - nLp_) * aN; const double hi = (w - nPrev_) * 0.5; nPrev_ = w;
                const double tn = (0.55 * pink * (1 - cl) + 1.1 * nLp_) * (1 - 0.3 * cl) + hi * cl * cl * 0.6;
                const double v3 = tn - s2_, v1 = a1 * s1_ + a2 * v3, v2 = s2_ + a2 * s1_ + a3 * v3; s1_ = 2 * v1 - s1_; s2_ = 2 * v2 - s2_; const double bpOut = v1 * kq;
                const double lgOut = lg_[(wi - Ll_) & mask];
                const double pm = lgOut * (1 - rMouth_) * sgn_;
                jd_[wi] = (float)(cJ_ * pm + turbG_ * 0.060 * bpOut + 0.002 * lamG_ * lamN_);
                const int ij = (int)std::floor(tauJ_); const double fj = tauJ_ - ij;
                const double j0 = jd_[(wi - ij) & mask], j1 = jd_[(wi - ij - 1) & mask]; const double jIn = j0 + (j1 - j0) * fj;
                jl1_ = (1 - jA_) * jIn + jA_ * jl1_; jl2_ = (1 - jA_) * jl1_ + jA_ * jl2_;
                const double jOut = jl2_ * gAmp_;
                double x = jOut / bJet_ * edgeK_ + bias_; if (x > 1.5) x = 1.5; else if (x < -1.5) x = -1.5;
                const double bb = bias_; const double F = (x - (x * x * x) / 3) - (bb - (bb * bb * bb) / 3) + asym_ * (x * x - asymMean_);
                double src = F * excite_ * 0.25; srcDcY_ = src - srcDc_ + 0.995 * srcDcY_; srcDc_ = src; src = srcDcY_;
                rg_[wi] = (float)(rMouth_ * lgOut + src);
                for (int h = 0; h < nHoles_; h++) {
                    const int ir = (wi - holeP_[h]) & mask, il = (wi - holeQ_[h]) & mask; const double aP = rg_[ir], aM = lg_[il], dlt = -0.5 * holeU_[h];
                    rg_[ir] = (float)(aP + dlt); lg_[il] = (float)(aM + dlt); holeU_[h] = 0.992 * holeU_[h] + holeG_[h] * (aP + aM + dlt);
                }
                const int id = (int)std::floor(Lr_); const double t = Lr_ - id + 1;
                const double L0 = rg_[(wi - id + 1) & mask], L1 = rg_[(wi - id) & mask], L2 = rg_[(wi - id - 1) & mask], L3 = rg_[(wi - id - 2) & mask];
                const double rgEnd = (-(t - 1) * (t - 2) * (t - 3) / 6) * L0 + (t * (t - 2) * (t - 3) / 2) * L1 + (-t * (t - 1) * (t - 3) / 2) * L2 + (t * (t - 1) * (t - 2) / 6) * L3;
                double y = rgEnd; const double ga = apG_, ap = ga * (y - apY_) + apX_; apX_ = y; apY_ = ap; y = ap;
                lossY_ = (1 - lossA_) * y + lossA_ * lossY_; y = lossY_; endY_ = (1 - endA_) * y + endA_ * endY_; y = endY_;
                const double lgIn = rEnd_ * y; lg_[wi] = (float)lgIn;
                const double uEnd = rgEnd - lgIn, radHp = uEnd - radPrev_; radPrev_ = uEnd;
                double o = (0.45 * uEnd + 0.9 * radHp) * radGain_;
                for (int h = 0; h < nHoles_; h++) o += 0.45 * holeG_[h] * holeU_[h];
                const double fr = w - frLp_; frLp_ += (w - frLp_) * 0.45;
                if (closed_) { const double dp = pm - pmPrev_; o += (0.45 * pm + 0.9 * dp) * radGain_ * 1.3; } pmPrev_ = pm;
                o += (dirG_ * tn + fricG_ * fr * 0.5) * 1.4 + pm * 0.02;
                const double dcy = o - dcX_ + 0.9975 * dcY_; dcX_ = o; dcY_ = dcy;
                out[i] = (float)(std::tanh(dcy * 0.30) * 0.85);
                taTurb_ += tn * tn * turbG_ * turbG_; taJet_ += F * F; taBore_ += rgEnd * rgEnd; taN_++;
                wi_ = (wi + 1) & mask;
            }
        }
        if (!std::isfinite(out[n - 1])) { reset(); for (int q = 0; q < n; q++) out[q] = 0; }
    }
private:
    double rnd() { uint32_t x = rng_; x ^= x << 13; x ^= x >> 17; x ^= x << 5; rng_ = x; return (double)x / 2147483648.0 - 1; }
    static double pd(double b0, double b1, double a1, double w) { const double c = std::cos(w), s = std::sin(w); const double ph = std::atan2(-b1 * s, b0 + b1 * c) - std::atan2(-a1 * s, 1 + a1 * c); return -ph / w; }
    enum { Pressure, Velocity, Intensity, Noise, Turbulence, Colour, Irregularity, Stability, Attack, Release, Continuity, Aperture, Tension, Angle, Width, Thickness, Position, Coverage, Edge,
           BoreLength, BoreDiameter, Taper, Flare, Resonance, Damping, BoreLoss, AirColumn, EndReflection, Radiation, HoleCover, Meri, Kari, MuraIki, KoroKoro, Tamane, Vibrato, VibRate,
           PitchInstability, Flutter, DynInstability, Articulation, Microtonal, Natural };
    void control(double f) {
        const Profile& pr = *prof_; const double sr = sr_, k = firstControl_ ? 1.0 : smK_; firstControl_ = false;
        for (int i = 0; i < kNumParams; i++) P_[i] += (T_[i] - P_[i]) * k;
        const double* P = P_; const double nat = 2 * P[Natural], lip = pr.lip, dt = 16 / sr;
        const double d = (P[Meri] - P[Kari]) * lip;
        const double coverage = std::min(1.0, std::max(0.0, P[Coverage] + 0.40 * d)), angle = std::min(1.0, std::max(0.0, P[Angle] - 0.30 * d)), aperture = std::min(1.0, std::max(0.05, P[Aperture] - 0.18 * d));
        const double t = t_, mura = P[MuraIki] * mura_, slow = slowN_, fast = fastN_;
        const double vibD = P[Vibrato] * std::min(1.0, t / 0.35), vibS = std::sin(vibPh_);
        const double tm = P[Tamane] * (0.6 * std::sin(tmPh_) + 0.4 * slow), fl = P[Flutter] * std::sin(flPh_);
        double mod = 1 + (1 - P[Stability]) * 0.05 * (0.7 * slow + 0.3 * fast) * (0.5 + 0.5 * nat) + vibD * 0.10 * vibS + nat * P[Irregularity] * (0.05 * slow + 0.02 * fast) + 0.18 * tm + 0.22 * fl + 0.45 * mura
                     - (1 - P[Continuity]) * 0.25 * (1 - std::exp(-t / 2.5)) + burst_ * (0.25 + 0.6 * P[Articulation]) * pr.chiff * 3;
        mod *= 1 - 0.10 * std::max(d, 0.0);
        const double pRel = std::max(0.0, P[Pressure] * 1.6 * env_ * mod); pRel_ = pRel;
        const double uj = std::sqrt(pRel) * (0.5 + P[Velocity]); uj_ = uj;
        double cents = P[Microtonal] * 50 + nat * P[PitchInstability] * (30 * driftN_ + 9 * fast) + vibD * 14 * vibS;
        cents += (pRel - 0.8) * 40 * P[DynInstability] * nat + P[DynInstability] * nat * 6 * slow;
        const double fT = f * std::pow(2.0, cents / 1200); fHz_ = fT;
        const double rn = (P[BoreDiameter] + pr.diam) * 0.5, rad = 0.35 + 0.65 * rn;
        const double taper = std::min(1.0, std::max(0.0, (P[Taper] - 0.5) * 0.9 + pr.taper)), flare = std::min(1.0, std::max(0.0, P[Flare] * 0.7 + pr.flare * 0.6));
        const bool closed = pr.ends == 1; closed_ = closed; const double period = sr / fT;
        const double lossHz = 700 + (1 - P[BoreLoss]) * 7000 * (0.4 + rn * 1.4) * (1 - 0.5 * P[Damping]); lossA_ = std::exp(-2 * M_PI * std::min(lossHz, sr * 0.45) / sr);
        const double endHz = (900 + 9000 * std::pow(1 - rn, 1.6)) * (1 + 1.5 * flare); endA_ = std::exp(-2 * M_PI * std::min(endHz, sr * 0.45) / sr);
        const double rLoop = 0.94 + 0.05 * P[EndReflection] * (0.2 + 0.8 * P[Resonance]) + 0.012 * flare - 0.03 * P[Damping];
        rEnd_ = (closed ? 1 : -1) * std::min(0.995, rLoop); rMouth_ = -std::min(0.985, 0.86 + 0.12 * pr.mouthR + 0.07 * coverage); apG_ = 0.55 * taper * (1 - 0.5 * flare);
        const double w0 = 2 * M_PI * fT / sr, roundTrip = closed ? period / 2 : period;
        const double comp = pd(1 - lossA_, 0, -lossA_, w0) + pd(1 - endA_, 0, -endA_, w0) + pd(apG_, 1, apG_, w0);
        const double endCorr = period * lip * 0.012 * (0.3 + P[Coverage]), meriExtra = period * lip * (d > 0 ? 0.045 : 0.030) * d;
        const double lf = std::log2(std::max(40.0, fT)); const double (*PT)[2] = pr.nPull ? pr.pull : kDefaultPull; const int nPT = pr.nPull ? pr.nPull : 25;
        double pull = PT[nPT - 1][1]; if (lf <= PT[0][0]) pull = PT[0][1]; else for (int q = 1; q < nPT; q++) if (lf <= PT[q][0]) { pull = PT[q - 1][1] + (PT[q][1] - PT[q - 1][1]) * (lf - PT[q - 1][0]) / (PT[q][0] - PT[q - 1][0]); break; }
        if (closed) pull += std::max(0.0, 0.0112 * (lf - 7.1));
        const double total = std::max(10.0, (roundTrip - comp - endCorr + meriExtra) * (1 + pull));
        Ll_ = std::max(3, (int)std::floor(total / 2)); LrT_ = std::max(6.0, total - Ll_); if (Lr_ == 100) Lr_ = LrT_;
        const double lenScale = std::pow(2.0, -(P[BoreLength] - 0.5)); const double Lroot = (closed ? sr / (4 * pr.root) : sr / (2 * pr.root)) * lenScale;
        int nh = 0, best = -1; double bx = -1;
        for (int h = 0; h < pr.nHoles && nh < 8; h++) {
            const double x = Lroot * std::pow(2.0, -pr.holes[h] / 12.0);
            if (x > 6 && x < LrT_ - 3) { holeP_[nh] = (int)jsRound(x); holeQ_[nh] = std::max(1, Ll_ - (int)jsRound(x * Ll_ / LrT_)); holeG_[nh] = 0.0004; if (x > bx) { bx = x; best = nh; } nh++; }
        }
        nHoles_ = nh; if (best >= 0) holeG_[best] = 0.0004 + 0.08 * P[HoleCover] * P[HoleCover];
        const double posF = 1.25 - 0.5 * P[Position], angF = 1 + 0.30 * (angle - 0.5) * lip, ujRef = 0.894;
        double rJ = pr.jet * posF * angF * std::min(1.5, std::pow(ujRef / std::max(0.3, uj), 0.72)) * (1 + (d > 0 ? 0.24 : -0.20) * d) * (1 + 0.04 * lamN_ * nat * P[Irregularity]);
        rJ *= 1 + 0.45 * P[KoroKoro] * std::sin(kkPh_) * (std::sin(kkPh_) > 0 ? 1 : 0.4) + 0.35 * mura;
        tauT_ = std::max(2.0, std::min(rJ * period, (double)N_ - 8)); bJet_ = 0.25 + 1.5 * P[Thickness];
        gAmp_ = (1.0 + 1.5 * pr.nl * (0.5 + P[Edge])) * (0.6 + 0.8 * (1 - P[Thickness])) * (0.6 + 0.8 * P[Width]);
        asym_ = pr.asym * (0.4 + 1.2 * std::fabs(angle - 0.5) * 2 * lip + 0.6 * P[Edge]) * (1 - 0.35 * d); asymMean_ = 0.12; edgeK_ = 0.65 + 0.7 * P[Edge];
        bias_ = (pr.bias + 0.45 * (angle - 0.5) * lip) * (0.5 + P[Edge]);
        { const double fcj = fT * 2.2 * (0.6 + 0.6 * std::min(1.5, uj)) * (1.6 - P[Thickness]); jA_ = std::exp(-2 * M_PI * std::min(fcj, sr * 0.45) / sr); }
        cJ_ = 0.30 * P[AirColumn] * (1 + 1.6 * (1 - P[Tension]) * lip + 0.8 * (1 - lip)) * (1 - 0.92 * std::min(1.0, mura)) / (0.5 + rad * 0.5);
        excite_ = P[Intensity] * 2.2 * uj * (0.35 + aperture) * pr.gain;
        turbG_ = P[Turbulence] * (1 + 4 * mura) * pr.noise * std::pow(uj, 1.5) * (0.4 + 0.6 * (1 - aperture * 0.5));
        dirG_ = P[Noise] * pr.direct * std::pow(uj, 1.1) * (0.5 + 0.7 * (1 - angle * 0.6) + 0.2 * d) * 1.3;
        fricG_ = P[Noise] * 0.35 * uj * uj * (0.5 + 0.5 * P[Turbulence]); radGain_ = (0.4 + 1.2 * P[Radiation]) * (1 + 0.5 * flare); lamG_ = 0.05 * (1 - P[Stability]);
        colour_ = std::min(1.0, std::max(0.0, P[Colour] - 0.35 * d)); const double edgeHz = fT * (1.2 + 2.5 * P[Colour]);
        svfG_ = std::tan(M_PI * std::min(sr * 0.4, edgeHz) / sr); svfK_ = 1 / 1.6;
        t_ += dt;
        vibPh_ += 2 * M_PI * (3 + 4.5 * P[VibRate]) * (1 + 0.04 * nat * slow) * dt; if (vibPh_ > 6.2832) vibPh_ -= 6.2832;
        flPh_ += 2 * M_PI * (8 + 14 * (0.5 + 0.5 * fast * nat)) * dt; if (flPh_ > 6.2832) flPh_ -= 6.2832;
        kkPh_ += 2 * M_PI * (7 + 5 * P[KoroKoro]) * dt; if (kkPh_ > 6.2832) kkPh_ -= 6.2832;
        tmPh_ += 2 * M_PI * (4.5 + nat * 1.5 * slow) * dt; if (tmPh_ > 6.2832) tmPh_ -= 6.2832;
        const double aS = 1 - std::exp(-2 * M_PI * 0.7 * dt), aF = 1 - std::exp(-2 * M_PI * 9 * dt), aD = 1 - std::exp(-2 * M_PI * 0.3 * dt), aL = 1 - std::exp(-2 * M_PI * 25 * dt);
        slowN_ += (rnd() * 1.7 * std::sqrt((2 - aS) / aS) * 0.55 - slowN_) * aS; fastN_ += (rnd() * 1.7 - fastN_) * aF; driftN_ += (rnd() * 1.7 * std::sqrt((2 - aD) / aD) * 0.55 - driftN_) * aD; lamN_ += (rnd() * 1.7 - lamN_) * aL;
        const double att = 0.01 + P[Attack] * P[Attack] * 0.9, rel = 0.01 + P[Release] * P[Release] * 1.2;
        if (gate_) env_ += (1 - env_) * (1 - std::exp(-dt / (att * 0.45))); else env_ *= std::exp(-dt / (rel * 0.45));
        burst_ *= std::exp(-dt / (0.03 + 0.05 * (1 - P[Articulation]))); mura_ *= std::exp(-dt / (0.18 + 0.18 * pRel));
    }
    static double jsRound(double x) { return std::floor(x + 0.5); }
    double sr_; int N_ = 0, mask_ = 0, wi_ = 0; std::vector<float> rg_, lg_, jd_; uint32_t rng_ = 1;
    double P_[kNumParams], T_[kNumParams]; const Profile* prof_ = nullptr; double gate_ = 0, t_ = 0, vel_ = 0.8, env_ = 0, burst_ = 0, mura_ = 0, pRel_ = 0, uj_ = 0, smK_ = 0;
    double pk0_ = 0, pk1_ = 0, pk2_ = 0, nLp_ = 0, nPrev_ = 0, slowN_ = 0, fastN_ = 0, lamN_ = 0, driftN_ = 0, frLp_ = 0, jl1_ = 0, jl2_ = 0, jA_ = 0.5, s1_ = 0, s2_ = 0, svfG_ = 0.1, svfK_ = 0.6;
    double vibPh_ = 0, flPh_ = 0, kkPh_ = 0, tmPh_ = 0; double holeU_[8] = {0}, holeG_[8] = {0}; int holeP_[8] = {0}, holeQ_[8] = {0}, nHoles_ = 0;
    double lossY_ = 0, endY_ = 0, apX_ = 0, apY_ = 0, dcX_ = 0, dcY_ = 0, radPrev_ = 0, srcDc_ = 0, srcDcY_ = 0, lossA_ = 0.3, endA_ = 0.3, apG_ = 0, rEnd_ = -0.95, rMouth_ = -0.6;
    double Lr_ = 100, LrT_ = 100, tauJ_ = 20, tauT_ = 20; int Ll_ = 50; double cJ_ = 0, gAmp_ = 1, bJet_ = 0.5, bias_ = 0, excite_ = 0, radGain_ = 1, turbG_ = 0, dirG_ = 0, fricG_ = 0, lamG_ = 0, colour_ = 0.5;
    double edgeK_ = 1, asym_ = 0.3, asymMean_ = 0.12, fHz_ = 220, sgn_ = 1, pmPrev_ = 0; bool firstControl_ = true, closed_ = false;
    double taTurb_ = 0, taJet_ = 0, taBore_ = 0; int taN_ = 0;
};

} // namespace flute
} // namespace vv
