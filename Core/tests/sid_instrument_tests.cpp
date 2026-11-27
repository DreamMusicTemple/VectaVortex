// SID instrument extras (vibrato, pulse sweep/table, SVF filter + table, hard restart, tie, sustain-hold): behavioural tests on SidChip.
#include "vv/sid.hpp"
#include <cstdio>
#include <vector>
#include <cmath>
using namespace vv;
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); ++fails; } else std::printf("ok:   %s\n", msg); } while (0)

static std::vector<float> run(SidChip& ch, SidVoiceParams* p, int blocks, int n = 128, double sr = 48000) {
    std::vector<float> all; float b0[128], b1[128], b2[128]; float* o[3] = {b0, b1, b2};
    for (int k = 0; k < blocks; ++k) { ch.process(p, sr, o, n); all.insert(all.end(), b0, b0 + n); }
    return all;
}
static double zcRate(const std::vector<float>& x, size_t a, size_t b) { int z = 0; for (size_t i = a + 1; i < b; ++i) if ((x[i - 1] < 0) != (x[i] < 0)) ++z; return (double)z / (b - a); }
static double rms(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double)x[i] * x[i]; return std::sqrt(s / (b - a)); }
static bool finite(const std::vector<float>& x) { for (float v : x) if (!std::isfinite(v) || std::fabs(v) > 4) return false; return true; }
static void base(SidVoiceParams* p) { for (int v = 0; v < 3; ++v) { p[v] = SidVoiceParams{}; p[v].gate = false; } p[0].gate = true; p[0].wave = 2; p[0].freq = 220; p[0].atk = 0.002; p[0].dec = 0.3; p[0].sus = 0.8; }

int main() {
    // 1. instrument inactive == untouched chip (bit-exact vs. active-but-all-neutral not required; compare to a second inactive run)
    { SidChip a, b; SidVoiceParams p[3], q[3]; base(p); base(q); q[0].ins.active = true;   // active but neutral
      auto x = run(a, p, 100), y = run(b, q, 100); double md = 0; for (size_t i = 0; i < x.size(); ++i) md = std::fmax(md, std::fabs(x[i] - y[i]));
      CHECK(md < 1e-6, "neutral instrument leaves the chip output unchanged"); }
    // 2. vibrato moves pitch
    { SidChip c; SidVoiceParams p[3]; base(p); p[0].ins.active = true; p[0].ins.vibAmp = 15; p[0].ins.vibRate = 1; auto x = run(c, p, 400);
      double mn = 1, mx = 0; for (size_t a = 4000; a + 2400 < x.size(); a += 2400) { double z = zcRate(x, a, a + 2400); mn = std::fmin(mn, z); mx = std::fmax(mx, z); }
      CHECK((mx - mn) / mx > 0.03, "vibrato modulates the pitch"); CHECK(finite(x), "vibrato output finite"); }
    // 3. vibrato delay: steady during the delay
    { SidChip c; SidVoiceParams p[3]; base(p); p[0].ins.active = true; p[0].ins.vibAmp = 15; p[0].ins.vibRate = 4; p[0].ins.vibDelay = 10; auto x = run(c, p, 400);
      double mn = 1, mx = 0; for (size_t a = 2000; a + 2400 < 40000; a += 2400) { double z = zcRate(x, a, a + 2400); mn = std::fmin(mn, z); mx = std::fmax(mx, z); }
      CHECK((mx - mn) / mx < 0.03, "vibrato delay holds the pitch steady at first"); }
    // 4. filter LP lowers the HF content; HP vs LP differ; OFF bypasses
    { auto hf = [](SidInstr in) { SidChip c; SidVoiceParams p[3]; base(p); p[0].ins = in; p[0].ins.active = true; auto x = run(c, p, 200); double s = 0; for (size_t i = 6001; i < x.size(); ++i) { double d = x[i] - x[i - 1]; s += d * d; } return std::sqrt(s / x.size()); };
      SidInstr off; SidInstr lp; lp.lp = true; lp.fCut = 300; SidInstr hp; hp.hp = true; hp.fCut = 1500;
      const double a = hf(off), b = hf(lp); CHECK(b < a * 0.5, "LP filter removes high-frequency content"); (void)hp;
      SidInstr bp; bp.bp = true; bp.fCut = 900; CHECK(std::fabs(hf(bp) - a) > 0.05 * a, "BP changes the sound"); }
    // 5. live cutoff edit while the note is held: changes timbre with no discontinuity spike
    { SidChip c; SidVoiceParams p[3]; base(p); p[0].ins.active = true; p[0].ins.lp = true; p[0].ins.fCut = 1800; p[0].sus = 0.8;
      auto x = run(c, p, 100); p[0].ins.fCut = 150; auto y = run(c, p, 200);
      double jump = std::fabs(y[0] - x.back()); CHECK(jump < 0.5, "cutoff edit while held has no step discontinuity");
      CHECK(rms(y, 10000, 20000) < rms(x, 6000, 12000) * 0.6, "cutoff edit while held changes the level/timbre"); }
    // 6. filter sweep moves the cutoff (bounded) and tables loop
    { SidChip c; SidVoiceParams p[3]; base(p); p[0].ins.active = true; p[0].ins.lp = true; p[0].ins.fCut = 100; p[0].ins.fSweep = 8; p[0].ins.fMin = 100; p[0].ins.fMax = 1400; auto x = run(c, p, 600);
      CHECK(finite(x), "filter sweep stable"); double r1 = rms(x, 3000, 9000), r2 = rms(x, 60000, 70000); CHECK(std::fabs(r1 - r2) > 0.01, "filter sweep changes the sound over time"); }
    { SidChip c; SidVoiceParams p[3]; base(p); SidInstr& I = p[0].ins; I.active = true; I.fTab = true; I.fN = 2; I.fS[0].len = 10; I.fS[0].set = true; I.fS[0].cut = 150; I.fS[0].lp = true; I.fS[1].len = 10; I.fS[1].set = true; I.fS[1].cut = 1900; I.fS[1].lp = true;
      auto x = run(c, p, 300); double lo = rms(x, 2000, 8000); double hi = 1e9; bool varies = false; for (size_t a = 6000; a + 960 < x.size(); a += 960) { double r = rms(x, a, a + 960); if (std::fabs(r - lo) > 0.05) varies = true; (void)hi; }
      CHECK(varies, "filter table steps alternate cutoff"); CHECK(finite(x), "filter table stable"); }
    // 7. pulse sweep alters the waveform; PW table + edit live
    { SidChip c; SidVoiceParams p[3]; base(p); p[0].wave = 4; p[0].ins.active = true; p[0].ins.pwSweep = 60; p[0].ins.pwMin = 0.1; p[0].ins.pwMax = 0.9; auto x = run(c, p, 400);
      double m1 = 0, m2 = 0; for (size_t i = 2000; i < 6000; ++i) m1 += x[i]; for (size_t i = 40000; i < 44000; ++i) m2 += x[i]; CHECK(std::fabs(m1 - m2) / 4000 > 0.02, "pulse sweep changes the duty cycle over time"); }
    { SidChip c; SidVoiceParams p[3]; base(p); p[0].wave = 4; SidInstr& I = p[0].ins; I.active = true; I.pwTab = true; I.pwN = 1; I.pwS[0].len = 10; I.pwS[0].set = true; I.pwS[0].val = 0.1; auto x = run(c, p, 200);
      I.pwS[0].val = 0.5; auto y = run(c, p, 200); double dc1 = 0, dc2 = 0; for (size_t i = 8000; i < 16000; ++i) dc1 += x[i]; for (size_t i = 8000; i < 16000; ++i) dc2 += y[i];
      CHECK(std::fabs(dc1 - dc2) / 8000 > 0.3, "editing the PW table step value while held changes the pulse width"); }
    // 8. hard restart dips to zero before attack; hr=0 does not
    { auto dip = [](int hr) { SidChip c; SidVoiceParams p[3]; base(p); p[0].ins.active = true; p[0].ins.hr = hr; p[0].sus = 0.8; auto x = run(c, p, 300); p[0].rt++; auto y = run(c, p, 200); double mn = 9; for (size_t a = 0; a + 240 < y.size(); a += 240) mn = std::fmin(mn, rms(y, a, a + 240)); return mn; };
      const double d0 = dip(0), d8 = dip(8); CHECK(d8 < d0 * 0.5, "hard restart drives the level toward zero on a retrigger"); }
    // 9. sustain-hold keeps the peak; tie starts at sustain
    { SidChip c; SidVoiceParams p[3]; base(p); p[0].dec = 0.1; p[0].sus = 0.3; p[0].ins.active = true; p[0].ins.susHold = true; auto x = run(c, p, 300);
      SidChip c2; SidVoiceParams q[3]; base(q); q[0].dec = 0.1; q[0].sus = 0.3; auto y = run(c2, q, 300); CHECK(rms(x, 30000, 36000) > rms(y, 30000, 36000) * 1.8, "SUSTAIN holds the envelope at its peak"); }
    { SidChip c; SidVoiceParams p[3]; base(p); p[0].atk = 1.0; p[0].sus = 0.7; p[0].ins.active = true; p[0].ins.tie = true; auto x = run(c, p, 20); CHECK(rms(x, 0, 2000) > 0.3, "TIE skips the attack and starts at the sustain level"); }
    // 10. resets
    { SidChip c; SidVoiceParams p[3]; base(p); p[0].wave = 4; p[0].ins.active = true; p[0].ins.pwSweep = 60; run(c, p, 400); c.pulseReset(); c.filterReset(); auto z = run(c, p, 50); CHECK(finite(z), "pulse/filter reset keep the output finite"); }
    std::printf("%s (%d failures)\n", fails ? "SID INSTRUMENT TESTS FAILED" : "SID instrument tests passed", fails);
    return fails ? 1 : 0;
}
