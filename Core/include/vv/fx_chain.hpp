// FX rack: the web app's FX chain (createFxNodes / rebuildFxGraph) as C++.
//   * up to 10 stages, each fed by (previous enabled stage) + (the layers that "send" to it); the last stage feeds the master bus
//   * a layer with at least one active send reaches the master ONLY through the chain (insert behaviour); otherwise straight from its EQ
//   * Power Panning multi-band: other stages can be patched into its four bands and are then taken out of the main chain
// All signals are stereo pairs. A worklet with an explicit 1-channel input sees 0.5*(L+R); nodes with the default 2-channel input
// (delay, panning) only read channel 0 (L), exactly like the browser.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "dsp_common.hpp"
#include "fx_basic.hpp"
#include "fx_spectral_granular.hpp"
#include "granular_x.hpp"
#include "fx_mbcomp.hpp"
#include "interstellar.hpp"
#include "legacy_reverb.hpp"
#include "adv_reverb.hpp"
#include "echo.hpp"
#include "ir.hpp"
#include "phaser_flanger.hpp"
#include "synth_common.hpp"
#include "voice_dsp.hpp"

namespace vv {
constexpr int kFxLayers = 7;   // sources that can send to an FX stage: WT, FM, SMP, PRO, SID, LUA and XFX (the XFX-zone tap); must equal kFxSources

// Chromium BiquadFilterNode (lowpass / highpass: Q in dB; allpass: linear Q). cutoff relative to Nyquist.
struct ChromBiquad {
    enum Type { LP, HP, AP };
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1[2] = {0, 0}, x2[2] = {0, 0}, y1[2] = {0, 0}, y2[2] = {0, 0};
    void set(Type t, double freq, double q, double sr) {
        double c = std::max(0.0, std::min(1.0, freq / (sr * 0.5))); double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
        if (t == AP) {
            if (c <= 0 || c >= 1) { B0 = 1; }
            else { const double Q = q, w = 3.141592653589793 * c, alpha = std::sin(w) / (2 * Q), cs = std::cos(w); B0 = 1 - alpha; B1 = -2 * cs; B2 = 1 + alpha; A0 = 1 + alpha; A1 = -2 * cs; A2 = 1 - alpha; }
        } else if (c >= 1) { if (t == LP) B0 = 1; else B0 = 0; B1 = B2 = 0; }
        else if (c <= 0) { if (t == LP) B0 = 0; else B0 = 1; B1 = B2 = 0; }
        else {
            const double Q = std::pow(10.0, q / 20.0), w = 3.141592653589793 * c, alpha = std::sin(w) / (2 * Q), cs = std::cos(w);
            const double beta = t == LP ? (1 - cs) / 2 : (1 + cs) / 2;
            B0 = beta; B1 = t == LP ? 2 * beta : -2 * beta; B2 = beta; A0 = 1 + alpha; A1 = -2 * cs; A2 = 1 - alpha;
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    inline float tick(int ch, float xin) {
        const double x = xin, y = b0 * x + b1 * x1[ch] + b2 * x2[ch] - a1 * y1[ch] - a2 * y2[ch];
        x2[ch] = x1[ch]; x1[ch] = x; y2[ch] = y1[ch]; y1[ch] = y; return (float)y;
    }
};

// A block-rate (k-rate) parameter with setTargetAtTime smoothing: value() = what the worklet reads at the start of the quantum.
struct KParam {
    Smoother s; double lo = 0, hi = 1, tc = 0.01;
    void init(double l, double h, double t, double v) { lo = l; hi = h; tc = t; s.set(clampd(v, l, h)); }
    void apply(double v, bool instant, double sr) { v = clampd(v, lo, hi); if (instant) s.set(v); else s.setTarget(v, sr, tc); }
    double blk(int n) { return s.advance(n); }
    double sample() { return s.tick(); }
};

struct FxEnv { double sr = 48000, bpm = 120; int oversample = 0; };

inline double syncDivToSeconds(const std::string& div, double bpm) {
    double q = 0.5;
    static const struct { const char* n; double q; } t[] = {{"1/1", 4}, {"1/2", 2}, {"1/4", 1}, {"1/8", 0.5}, {"1/16", 0.25}, {"1/4.", 1.5}, {"1/8.", 0.75}, {"1/4t", 2.0 / 3}, {"1/8t", 1.0 / 3}};
    for (auto& e : t) if (div == e.n) q = e.q;
    return clampd((60.0 / bpm) * q, 0.01, 1.5);
}

class FxStage {
public:
    virtual ~FxStage() {}
    std::string type; int id = 0; bool enabled = true; bool sends[kFxLayers] = {};
    int quietBlocks = 0; bool asleep = false;   // Battery Saving Mode idle tracking (see FxChain::process)
    virtual void configure(const Json& p, bool instant, const FxEnv& e) = 0;
    virtual void process(const float* iL, const float* iR, float* oL, float* oR, int n) = 0;
    virtual void tickClock(int /*n*/) {}
    static void mono(const float* iL, const float* iR, float* m, int n) { for (int i = 0; i < n; i++) m[i] = 0.5f * (iL[i] + iR[i]); }
    static void dup(const float* m, float* oL, float* oR, int n) { for (int i = 0; i < n; i++) { oL[i] = m[i]; oR[i] = m[i]; } }
protected:
    float m_[kQuantum], a_[kQuantum], b_[kQuantum];
};

namespace fxdetail {
inline double num(const Json& p, const char* k, double d) { return p.num(k, d); }
inline bool flag(const Json& p, const char* k, bool d) { auto v = p.get(k); if (!v) return d; if (v->type == Json::Bool) return v->b; if (v->type == Json::Num) return v->n != 0; return d; }
}

// ------------------------------------------------------------------------------------------------------------------ reverb (legacy + advanced engine)
class ReverbStage : public FxStage {
public:
    ReverbStage(double sr) { legacy.prepare(sr, 1); adv.prepare(sr); kd.init(0, 0.97, 0.01, 0.5); ks.init(0.1, 2, 0.01, 0.5); kp.init(0, 0.3, 0.01, 0.02); km.init(0, 1, 0.01, 0.3); ka.init(-24, 24, 0.01, 12); kb.init(-24, 24, 0.01, -12); kmod.init(0, 1, 0.01, 0.4); kc.init(0, 1, 0.01, 0.5); sr_ = sr; }
    void configure(const Json& p, bool instant, const FxEnv&) override {
        using namespace fxdetail;
        const std::string algo = p.str("algo", "hall");
        int la = -1; if (algo == "room") la = 0; else if (algo == "spring") la = 1; else if (algo == "plate") la = 2; else if (algo == "shimmer") la = 4; else if (algo == "reverse") la = 5;
        int aa = -1; if (algo == "hall") aa = 0; else if (algo == "plate ambient") aa = 1; else if (algo == "kristal") aa = 2; else if (algo == "flow") aa = 3; else if (algo == "mothership") aa = 4; else if (algo == "formant") aa = 5;
        const bool useAdv = aa >= 0 || la < 0;
        legacyAlgo = useAdv ? 9 : la; advAlgo = useAdv ? (aa >= 0 ? aa : 0) : 9;
        kd.apply(clampd(num(p, "decay", 0.5), 0, 0.97), instant, sr_); ks.apply(num(p, "size", 0.5), instant, sr_); kp.apply(num(p, "predelay", 0.02), instant, sr_); km.apply(num(p, "mix", 0.3), instant, sr_);
        rvTail_ = clampd(num(p, "rvTail", 1), 0, 1); rvTrans_ = clampd(num(p, "rvTrans", 0.5), 0, 1);
        ka.apply(num(p, "shimA", 12), instant, sr_); kb.apply(num(p, "shimB", -12), instant, sr_); kmod.apply(num(p, "mod", 0.4), instant, sr_); kc.apply(num(p, "colour", 0.5), instant, sr_);
    }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        mono(iL, iR, m_, n);
        LegacyReverb::Params lp; lp.algo = legacyAlgo; lp.decay = kd.s.v; lp.size = ks.s.v; lp.predelay = kp.s.v; lp.mix = km.s.v; lp.shimA = ka.s.v; lp.shimB = kb.s.v; lp.rvTail = rvTail_; lp.rvTrans = rvTrans_;
        AdvReverb::Params ap; ap.algo = advAlgo; ap.decay = kd.s.v; ap.size = ks.s.v; ap.predelay = kp.s.v; ap.mix = km.s.v; ap.mod = kmod.s.v; ap.colour = kc.s.v;
        legacy.process(m_, a_, n, lp); adv.process(m_, oL, oR, n, ap);
        for (int i = 0; i < n; i++) { oL[i] += a_[i]; oR[i] += a_[i]; }
        kd.blk(n); ks.blk(n); kp.blk(n); km.blk(n); ka.blk(n); kb.blk(n); kmod.blk(n); kc.blk(n);
    }
    LegacyReverb legacy; AdvReverb adv;
private:
    KParam kd, ks, kp, km, ka, kb, kmod, kc; int legacyAlgo = 9, advAlgo = 0; double sr_; double rvTail_ = 1, rvTrans_ = 0.5;
};

// ------------------------------------------------------------------------------------------------------------------ echo (natural spatial echo; own engine, see echo.hpp)
class EchoStage : public FxStage {
public:
    explicit EchoStage(double sr) : core_(sr) {}
    void configure(const Json& p, bool instant, const FxEnv& e) override {
        using namespace fxdetail; echo::Params q;
        q.env = (int)std::lround(num(p, "env", 2)); q.count = (int)std::lround(num(p, "count", 8));
        static const struct { const char* n; double q; } dv[] = {{"1/1", 4}, {"1/2", 2}, {"1/4", 1}, {"1/8", 0.5}, {"1/16", 0.25}, {"1/4.", 1.5}, {"1/8.", 0.75}, {"1/4t", 2.0 / 3}, {"1/8t", 1.0 / 3}};
        double t = num(p, "time", 0.42);
        if (flag(p, "sync", false)) { double qq = 0.5; const std::string d = p.str("syncDiv", "1/8"); for (auto& x : dv) if (d == x.n) qq = x.q; t = clampd((60.0 / std::max(20.0, e.bpm)) * qq, 0.02, 4.0); }
        q.time = clampd(t, 0.02, 4); q.density = clampd(num(p, "density", 0.4), 0, 1); q.feedback = clampd(num(p, "feedback", 0.4), 0, 0.95); q.decay = clampd(num(p, "decay", 0.5), 0, 1);
        q.diffusion = clampd(num(p, "diffusion", 0.55), 0, 1); q.width = clampd(num(p, "width", 1), 0, 1.5); q.spread = clampd(num(p, "spread", 0.7), 0, 1);
        q.early = clampd(num(p, "early", 0.6), 0, 1); q.late = clampd(num(p, "late", 0.8), 0, 1); q.hfDamp = clampd(num(p, "hfDamp", 0.45), 0, 1); q.lfDamp = clampd(num(p, "lfDamp", 0.15), 0, 1);
        q.modDepth = clampd(num(p, "modDepth", 0.25), 0, 1); q.modRate = clampd(num(p, "modRate", 0.45), 0.05, 8); q.movement = clampd(num(p, "movement", 0.35), 0, 1);
        q.mix = clampd(num(p, "mix", 0.35), 0, 1); q.level = clampd(num(p, "level", 0), -24, 12);
        core_.setParams(q); if (instant) core_.snap();
    }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override { core_.process(iL, iR, oL, oR, n); }
private:
    echo::Core core_;
};

// ------------------------------------------------------------------------------------------------------------------ delay
class DelayStage : public FxStage {
public:
    DelayStage(double sr) : sr_(sr) { d.prepare(sr); kt.init(0.01, 1.5, 0.01, 0.35); kf.init(0, 0.9, 0.01, 0.35); km.init(0, 1, 0.01, 0.25); }
    void configure(const Json& p, bool instant, const FxEnv& e) override {
        using namespace fxdetail;
        const std::string md = p.str("mode", "stereo"); mode = md == "mono" ? 0 : md == "stereo" ? 1 : md == "pingpong" ? 2 : md == "reverse" ? 3 : 1;
        const double t = flag(p, "sync", false) ? syncDivToSeconds(p.str("syncDiv", "1/8"), e.bpm) : num(p, "time", 0.35);
        kt.apply(t, instant, sr_); kf.apply(num(p, "feedback", 0.35), instant, sr_); km.apply(num(p, "mix", 0.25), instant, sr_);
    }
    void process(const float* iL, const float*, float* oL, float* oR, int n) override {
        Delay::Params q; q.mode = mode; q.time = kt.s.v; q.feedback = kf.s.v; q.mix = km.s.v; d.process(iL, oL, oR, n, q);
        kt.blk(n); kf.blk(n); km.blk(n);
    }
private:
    Delay d; KParam kt, kf, km; int mode = 1; double sr_;
};

// ------------------------------------------------------------------------------------------------------------------ mono-in / dup-out processors
class SaturationStage : public FxStage {
public:
    SaturationStage(double sr) : sr_(sr) {
        s.prepare(sr); kd.init(0, 1, 0.01, 0.4); kt.init(300, 14000, 0.01, 6000); km.init(0, 1, 0.01, 0.5);
        for (int b = 0; b < 3; b++) { band[b].prepare(sr); kbd[b].init(0, 1, 0.01, 0.4); }
        klo.init(60, 2000, 0.01, 300); khi.init(600, 16000, 0.01, 3000); kmbs.init(0, 1, 0.015, 1); kmbm.init(0, 1, 0.015, 0);
    }
    void configure(const Json& p, bool instant, const FxEnv& e) override {
        using namespace fxdetail;
        const std::string t = p.str("type", "normal"); hard = t == "hard" ? 1 : t == "soft" ? 2 : t == "tube" ? 3 : 0; os = e.oversample;
        const double drive = num(p, "drive", 0.4);
        kd.apply(drive, instant, sr_); kt.apply(num(p, "tone", 6000), instant, sr_); km.apply(num(p, "mix", 0.5), instant, sr_);
        // 3-band multiband path (web): crossover -> one saturator per band (Drive x band amount, fully wet) -> blended once against the stereo dry with Mix
        const double amt[3] = { num(p, "bandLow", 1), num(p, "bandMid", 1), num(p, "bandHigh", 1) };
        for (int b = 0; b < 3; b++) kbd[b].apply(clampd(drive * amt[b], 0, 1), instant || !cfgd_, sr_);
        const bool mb = p.boolean("multiband", false); const bool inst = instant || !cfgd_;
        kmbs.apply(mb ? 0 : 1, inst, sr_); kmbm.apply(mb ? 1 : 0, inst, sr_);
        klo.apply(num(p, "bandLowFreq", 300), inst, sr_); khi.apply(num(p, "bandHighFreq", 3000), inst, sr_); cfgd_ = true;
    }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        mono(iL, iR, m_, n);
        Saturation::Params q; q.drive = kd.s.v; q.hard = hard; q.tone = kt.s.v; q.mix = km.s.v; q.oversample = os;
        s.process(m_, a_, n, q);
        const double mix = km.s.v, fLo = klo.s.v, fHi = khi.s.v, gS = kmbs.s.v, gM = kmbm.s.v;
        lowF.setLowpassDb(fLo, 0.707, sr_); midHp.setHighpassDb(fLo, 0.707, sr_); midLp.setLowpassDb(fHi, 0.707, sr_); highF.setHighpassDb(fHi, 0.707, sr_);
        float* bo[3] = { b0_, b1_, b2_ };
        for (int i = 0; i < n; i++) { const double x = m_[i]; bin_[0][i] = (float)lowF.process(x); bin_[1][i] = (float)midLp.process(midHp.process(x)); bin_[2][i] = (float)highF.process(x); }
        for (int b = 0; b < 3; b++) { Saturation::Params bq; bq.drive = kbd[b].s.v; bq.hard = hard; bq.tone = kt.s.v; bq.mix = 1; bq.oversample = os; band[b].process(bin_[b], bo[b], n, bq); }
        for (int i = 0; i < n; i++) {   // the multiband dry tap is the stereo input itself; the wet bands are mono
            const double wet = ((double)bo[0][i] + (double)bo[1][i] + (double)bo[2][i]) * mix, sg = (double)a_[i] * gS;
            oL[i] = (float)(sg + ((double)iL[i] * (1 - mix) + wet) * gM); oR[i] = (float)(sg + ((double)iR[i] * (1 - mix) + wet) * gM);
        }
        kd.blk(n); kt.blk(n); km.blk(n); for (int b = 0; b < 3; b++) kbd[b].blk(n); klo.blk(n); khi.blk(n); kmbs.blk(n); kmbm.blk(n);
    }
private:
    bool cfgd_ = false;
    Saturation s, band[3]; KParam kd, kt, km, kbd[3], klo, khi, kmbs, kmbm; int hard = 0, os = 0; double sr_;
    Biquad lowF, midHp, midLp, highF; float bin_[3][kQuantum], b0_[kQuantum], b1_[kQuantum], b2_[kQuantum];
};

// ------------------------------------------------------------------------------------------------------------------ Rotary Speaker and Amp + Cab (rotary_amp.hpp; web rotary-processor / amp-processor)
class RotaryStage : public FxStage {
public:
    explicit RotaryStage(double sr) : sr_(sr) { r_.prepare(sr); kd.init(0, 1, 0.01, 0.3); kb.init(0, 1, 0.01, 0.5); kdep.init(0, 1, 0.01, 0.7); kw.init(0, 1, 0.01, 0.8); kac.init(0.2, 4, 0.01, 1); km.init(0, 1, 0.01, 1); kl.init(-24, 12, 0.01, 0); }
    void configure(const Json& p, bool instant, const FxEnv&) override {
        using namespace fxdetail; const std::string sp = p.str("speed", "slow"); speed_ = sp == "fast" ? 1 : sp == "stop" ? 2 : 0;
        kd.apply(num(p, "drive", 0.3), instant, sr_); kb.apply(num(p, "balance", 0.5), instant, sr_); kdep.apply(num(p, "depth", 0.7), instant, sr_); kw.apply(num(p, "width", 0.8), instant, sr_);
        kac.apply(num(p, "accel", 1), instant, sr_); km.apply(num(p, "mix", 1), instant, sr_); kl.apply(num(p, "level", 0), instant, sr_);
    }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        mono(iL, iR, m_, n); r_.setParams(speed_, kd.s.v, kb.s.v, kdep.s.v, kw.s.v, kac.s.v, km.s.v, kl.s.v);
        for (int i = 0; i < n; i++) { r_.tick((double)m_[i]); oL[i] = (float)r_.oL; oR[i] = (float)r_.oR; }
        kd.blk(n); kb.blk(n); kdep.blk(n); kw.blk(n); kac.blk(n); km.blk(n); kl.blk(n);
    }
private:
    rotamp::Rotary r_; KParam kd, kb, kdep, kw, kac, km, kl; double sr_, speed_ = 0;
};
class AmpStage : public FxStage {
public:
    explicit AmpStage(double sr) : sr_(sr) { a_x.prepare(sr); kg.init(0, 1, 0.01, 0.4); kba.init(0, 1, 0.01, 0.5); kmi.init(0, 1, 0.01, 0.5); kt.init(0, 1, 0.01, 0.5); kpr.init(0, 1, 0.01, 0.4); kma.init(0, 1, 0.01, 0.5); kch.init(0, 1, 0.01, 0.5); km.init(0, 1, 0.01, 1); kl.init(-24, 12, 0.01, 0); }
    void configure(const Json& p, bool instant, const FxEnv&) override {
        using namespace fxdetail; const std::string md = p.str("model", "twin"); model_ = md == "jc" ? 1 : md == "small" ? 2 : md == "brit" ? 3 : 0; cab_ = flag(p, "cab", true) ? 1 : 0;
        kg.apply(num(p, "gain", 0.4), instant, sr_); kba.apply(num(p, "bass", 0.5), instant, sr_); kmi.apply(num(p, "mid", 0.5), instant, sr_); kt.apply(num(p, "treble", 0.5), instant, sr_); kpr.apply(num(p, "presence", 0.4), instant, sr_);
        kma.apply(num(p, "master", 0.5), instant, sr_); kch.apply(num(p, "chorus", 0.5), instant, sr_); km.apply(num(p, "mix", 1), instant, sr_); kl.apply(num(p, "level", 0), instant, sr_);
    }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        mono(iL, iR, m_, n); a_x.setParams(model_, kg.s.v, kba.s.v, kmi.s.v, kt.s.v, kpr.s.v, kma.s.v, cab_, kch.s.v, km.s.v, kl.s.v);
        for (int i = 0; i < n; i++) { a_x.tick((double)m_[i]); oL[i] = (float)a_x.oL; oR[i] = (float)a_x.oR; }
        kg.blk(n); kba.blk(n); kmi.blk(n); kt.blk(n); kpr.blk(n); kma.blk(n); kch.blk(n); km.blk(n); kl.blk(n);
    }
private:
    rotamp::Amp a_x; KParam kg, kba, kmi, kt, kpr, kma, kch, km, kl; double sr_; int model_ = 0, cab_ = 1;
};

class WavefolderStage : public FxStage {
public:
    WavefolderStage(double sr) : sr_(sr) { w.prepare(sr); kd.init(1, 20, 0.01, 3); ksy.init(-1, 1, 0.01, 0); km.init(0, 1, 0.01, 1); kp.init(0.02, 0.98, 0.01, 0.5); }
    void configure(const Json& p, bool instant, const FxEnv& e) override {
        using namespace fxdetail;
        const std::string md = p.str("mode", "fold"); mode = md == "fold" ? 0 : md == "mirror" ? 1 : md == "sine" ? 2 : md == "pwm" ? 3 : 0; os = e.oversample;
        kd.apply(num(p, "drive", 3), instant, sr_); ksy.apply(num(p, "symmetry", 0), instant, sr_); km.apply(num(p, "mix", 1), instant, sr_); kp.apply(num(p, "pwm", 0.5), instant, sr_);
    }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        mono(iL, iR, m_, n); Wavefolder::Params q; q.drive = kd.s.v; q.symmetry = ksy.s.v; q.mix = km.s.v; q.oversample = os; q.mode = mode; q.pwm = kp.s.v;
        w.process(m_, a_, n, q); dup(a_, oL, oR, n); kd.blk(n); ksy.blk(n); km.blk(n); kp.blk(n);
    }
private:
    Wavefolder w; KParam kd, ksy, km, kp; int mode = 0, os = 0; double sr_;
};

class ResonatorStage : public FxStage {
public:
    ResonatorStage(double sr) : sr_(sr) { r.prepare(sr); kf.init(20, 8000, 0.01, 440); kr.init(1, 40, 0.01, 15); km.init(0, 1, 0.01, 0.4); }
    void configure(const Json& p, bool instant, const FxEnv&) override { using namespace fxdetail; kf.apply(num(p, "freq", 440), instant, sr_); kr.apply(num(p, "resonance", 15), instant, sr_); km.apply(num(p, "mix", 0.4), instant, sr_); }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        mono(iL, iR, m_, n); Resonator::Params q; q.freq = kf.s.v; q.resonance = kr.s.v; q.mix = km.s.v; r.process(m_, a_, n, q); dup(a_, oL, oR, n); kf.blk(n); kr.blk(n); km.blk(n);
    }
private:
    Resonator r; KParam kf, kr, km; double sr_;
};

class MbCompStage : public FxStage {
public:
    explicit MbCompStage(double sr) { c.prepare(sr); }
    void configure(const Json& p, bool, const FxEnv&) override { c.setParams(MbCompParams::fromJson(p)); }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override { c.process(iL, iR ? iR : iL, oL, oR, n); }
private:
    MbComp c;
};

class ChorusStage : public FxStage {
public:
    ChorusStage(double sr) : sr_(sr) { c.prepare(sr); km.init(0, 1, 0.01, 0.5); }
    void configure(const Json& p, bool instant, const FxEnv&) override { using namespace fxdetail; c1 = flag(p, "chorus1On", true); c2 = flag(p, "chorus2On", false); km.apply(num(p, "mix", 0.5), instant, sr_); }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        mono(iL, iR, m_, n); Chorus::Params q; q.c1 = c1; q.c2 = c2; q.mix = km.s.v; c.process(m_, a_, n, q); dup(a_, oL, oR, n); km.blk(n);
    }
private:
    Chorus c; KParam km; bool c1 = true, c2 = false; double sr_;
};

// ------------------------------------------------------------------------------------------------------------------ mono-in / stereo-out processors
class PhaserStage : public FxStage {
public:
    PhaserStage(double sr, bool flanger) : mode(flanger ? 1 : 0), sr_(sr) { p.prepare(sr); kr.init(0.05, 5, 0.01, 0.5); kd.init(0, 1, 0.01, 0.7); kf.init(0, 0.95, 0.01, 0.3); km.init(0, 1, 0.01, 0.5); }
    void configure(const Json& j, bool instant, const FxEnv&) override {
        using namespace fxdetail; stages = (int)clampd(num(j, "stages", 8), 2, 12);
        kr.apply(num(j, "rate", 0.5), instant, sr_); kd.apply(num(j, "depth", 0.7), instant, sr_); kf.apply(num(j, "feedback", 0.3), instant, sr_); km.apply(num(j, "mix", 0.5), instant, sr_);
    }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        mono(iL, iR, m_, n); PhaserFlanger::Params q; q.mode = mode; q.rate = kr.s.v; q.depth = kd.s.v; q.feedback = kf.s.v; q.mix = km.s.v; q.stages = stages;
        p.process(m_, oL, oR, n, q); kr.blk(n); kd.blk(n); kf.blk(n); km.blk(n);
    }
private:
    PhaserFlanger p; KParam kr, kd, kf, km; int mode, stages = 8; double sr_;
};

// ------------------------------------------------------------------------------------------------------------------ stereo spread
// Web "spread" stage: a single-purpose widener. L passes straight through, R gets a Haas delay of spread*10 ms (spread 0..2, so 0..20 ms),
// blended with the untouched input by mix. A mono source arrives as two identical copies, which is what gets decorrelated.
class SpreadStage : public FxStage {
public:
    explicit SpreadStage(double sr) : sr_(sr) {
        len_ = (size_t)(sr * 0.05) + 8; buf_.assign(len_, 0.f);
        kd_.init(0, 0.04, 0.01, 0.01); kdry_.init(0, 1, 0.01, 0); kwet_.init(0, 1, 0.01, 1);
    }
    void configure(const Json& p, bool instant, const FxEnv&) override {
        using namespace fxdetail;
        const double spread = clampd(num(p, "spread", 1), 0, 2), mix = clampd(num(p, "mix", 1), 0, 1);
        kd_.apply(spread * 0.5 * 0.02, instant, sr_); kdry_.apply(1 - mix, instant, sr_); kwet_.apply(mix, instant, sr_);
    }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        for (int i = 0; i < n; i++) {
            const double dsm = kd_.sample() * sr_, gd = kdry_.sample(), gw = kwet_.sample();
            buf_[w_] = iR[i];
            double rp = (double)w_ - dsm; if (rp < 0) rp += (double)len_;
            size_t i0 = (size_t)rp; if (i0 >= len_) i0 -= len_;
            const size_t i1 = i0 + 1 >= len_ ? 0 : i0 + 1; const double fr = rp - std::floor(rp);
            const float dR = (float)(buf_[i0] * (1.0 - fr) + buf_[i1] * fr);
            oL[i] = (float)(iL[i] * gd + iL[i] * gw);
            oR[i] = (float)(iR[i] * gd + dR * gw);
            if (++w_ >= len_) w_ = 0;
        }
        kd_.blk(n); kdry_.blk(n); kwet_.blk(n);
    }
private:
    double sr_; size_t len_ = 0, w_ = 0; std::vector<float> buf_; KParam kd_, kdry_, kwet_;
};

class SpectralStage : public FxStage {
public:
    SpectralStage(double sr) : sr_(sr) { s.prepare(sr); kb.init(0, 1, 0.01, 0); ksh.init(-200, 200, 0.01, 0); kt.init(0, 1, 0.01, 0); ksp.init(0, 1, 0.01, 0); ktilt.init(-12, 12, 0.01, 0); kdis.init(0, 1, 0.01, 0); km.init(0, 1, 0.01, 1); }
    void configure(const Json& p, bool instant, const FxEnv&) override {
        using namespace fxdetail; ktilt.apply(num(p, "tilt", 0), instant, sr_); kdis.apply(num(p, "disperse", 0), instant, sr_); freeze = flag(p, "freeze", false); kb.apply(num(p, "blur", 0), instant, sr_); ksh.apply(num(p, "shift", 0), instant, sr_);
        kt.apply(num(p, "threshold", 0), instant, sr_); ksp.apply(num(p, "spread", 0), instant, sr_); km.apply(num(p, "mix", 1), instant, sr_);
    }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        mono(iL, iR, m_, n); SpectralFX::Params q; q.freeze = freeze; q.blur = kb.s.v; q.shift = ksh.s.v; q.threshold = kt.s.v; q.spread = ksp.s.v; q.tilt = ktilt.s.v; q.disperse = kdis.s.v; q.mix = km.s.v;
        s.process(m_, oL, oR, n, q); kb.blk(n); ksh.blk(n); kt.blk(n); ksp.blk(n); ktilt.blk(n); kdis.blk(n); km.blk(n);
    }
private:
    SpectralFX s; KParam kb, ksh, kt, ksp, ktilt, kdis, km; bool freeze = false; double sr_;
};

class GranularStage : public FxStage {
public:
    GranularStage(double sr, uint32_t seed) : sr_(sr) { g.prepare(sr, seed); gx.prepare(sr, 1 + (seed % 97) * 7919u); kg.init(5, 500, 0.01, 80); kde.init(0.5, 100, 0.01, 20); kp.init(-24, 24, 0.01, 0); ksp.init(0, 1, 0.01, 0.5); kpc.init(-1, 1, 0.01, 0); kf.init(0, 1, 0.01, 0); km.init(0, 1, 0.01, 1); }
    void configure(const Json& p, bool instant, const FxEnv& e) override {
        using namespace fxdetail; env = (int)clampd(num(p, "envShape", 0), 0, 3);
        kg.apply(num(p, "grainSize", 80), instant, sr_); kde.apply(num(p, "density", 20), instant, sr_); kp.apply(num(p, "pitch", 0), instant, sr_); ksp.apply(num(p, "spread", 0.5), instant, sr_);
        kpc.apply(num(p, "panCenter", 0), instant, sr_); kf.apply(num(p, "followerAmount", 0), instant, sr_); km.apply(num(p, "mix", 1), instant, sr_);
        // extended controls (every one is neutral at its default, so presets saved before they existed sound exactly as before)
        GranularX::P x;
        x.mode = (int)clampd(num(p, "mode", 0), 0, 13); x.window = (int)clampd(num(p, "window", -1), -1, 7); x.rndType = (int)clampd(num(p, "rndType", 0), 0, 5); x.sync = (int)clampd(num(p, "sync", 0), 0, 9); x.delaySync = (int)clampd(num(p, "delaySync", 0), 0, 9);
        x.freeze = flag(p, "freeze", false); x.autoGain = flag(p, "autoGain", false); x.seed = (int)clampd(num(p, "seed", 0), 0, 999);
        x.delay = clampd(num(p, "delay", 0), 0, 1500); x.posRand = clampd(num(p, "posRand", 1), 0, 8); x.sizeRand = clampd(num(p, "sizeRand", 0), 0, 1); x.jitter = clampd(num(p, "jitter", 0), 0, 1);
        x.fine = clampd(num(p, "fine", 0), -100, 100); x.pitchRand = clampd(num(p, "pitchRand", 0), 0, 12); x.reverse = clampd(num(p, "reverse", 0), 0, 1);
        x.volRand = clampd(num(p, "volRand", 0), 0, 1); x.width = clampd(num(p, "width", 1), 0, 2); x.drift = clampd(num(p, "drift", 0), 0, 1); x.lrSpread = clampd(num(p, "lrSpread", 0), 0, 80); x.outLevel = clampd(num(p, "outLevel", 0), -24, 12);
        x.curve = clampd(num(p, "curve", 0.5), 0, 1); x.symmetry = clampd(num(p, "symmetry", 0.5), 0, 1); x.atk = clampd(num(p, "atk", 0), 0, 0.5); x.dec = clampd(num(p, "dec", 0), 0, 0.5); x.sus = clampd(num(p, "sus", 1), 0, 1); x.rel = clampd(num(p, "rel", 0), 0, 0.5);
        x.fb = clampd(num(p, "fb", 0), 0, 0.95); x.fbDamp = clampd(num(p, "fbDamp", 0), 0, 1); x.fbLow = clampd(num(p, "fbLow", 0), 0, 1); x.fbSat = clampd(num(p, "fbSat", 0), 0, 1); x.fbDiff = clampd(num(p, "fbDiff", 0), 0, 1); x.pingPong = clampd(num(p, "pingPong", 0), 0, 1);
        x.rndDepth = clampd(num(p, "rndDepth", 1), 0, 1); x.rndRate = clampd(num(p, "rndRate", 2), 0.05, 20);
        gx.set(x); gx.setLegacyShape(env); bpm_ = e.bpm > 0 ? e.bpm : 120;
    }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        mono(iL, iR, m_, n); Granular::Params q; q.grainSize = kg.s.v; q.density = kde.s.v; q.pitch = kp.s.v; q.spread = ksp.s.v; q.panCenter = kpc.s.v; q.envShape = env; q.followerAmount = kf.s.v; q.mix = km.s.v;
        q.noSpawn = gx.extOn();
        g.process(m_, oL, oR, n, q);
        if (gx.busy()) {
            GranularX::Base b; b.grainSize = q.grainSize; b.density = q.density; b.pitch = q.pitch; b.spread = q.spread; b.panCenter = q.panCenter; b.followerAmount = q.followerAmount; b.mix = q.mix; b.legacyShape = env; b.bpm = bpm_;
            gx.process(m_, wl_, wr_, n, b);
            for (int i = 0; i < n; i++) { oL[i] += (float)(wl_[i] * q.mix); oR[i] += (float)(wr_[i] * q.mix); }
        } else gx.prime(m_, n);
        const double og = gx.outGain(); if (og != 1.0) for (int i = 0; i < n; i++) { oL[i] = (float)(oL[i] * og); oR[i] = (float)(oR[i] * og); }
        kg.blk(n); kde.blk(n); kp.blk(n); ksp.blk(n); kpc.blk(n); kf.blk(n); km.blk(n);
    }
    // grain display snapshot (see GranularX::viz)
    int vizSnapshot(float* out, int cap) const { return gx.viz(out, cap); }
private:
    Granular g; GranularX gx; KParam kg, kde, kp, ksp, kpc, kf, km; int env = 0; double sr_, bpm_ = 120; float m_[kQuantum], wl_[kQuantum], wr_[kQuantum];
};

class TapeStage : public FxStage {
public:
    TapeStage(double sr, uint32_t seed) : sr_(sr) { t.prepare(sr, seed); kwr.init(0.05, 5, 0.01, 0.7); kwd.init(0, 1, 0.01, 0.3); kfr.init(2, 20, 0.01, 8); kfd.init(0, 1, 0.01, 0.2); kdu.init(0, 1, 0.01, 0.2); kcr.init(0, 1, 0.01, 0.2); km.init(0, 1, 0.01, 1); }
    void configure(const Json& p, bool instant, const FxEnv&) override {
        using namespace fxdetail;
        kwr.apply(num(p, "wowRate", 0.7), instant, sr_); kwd.apply(num(p, "wowDepth", 0.3), instant, sr_); kfr.apply(num(p, "flutterRate", 8), instant, sr_); kfd.apply(num(p, "flutterDepth", 0.2), instant, sr_);
        kdu.apply(num(p, "dust", 0.2), instant, sr_); kcr.apply(num(p, "crackle", 0.2), instant, sr_); km.apply(num(p, "mix", 1), instant, sr_);
    }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        mono(iL, iR, m_, n); Tape::Params q; q.wowRate = kwr.s.v; q.wowDepth = kwd.s.v; q.flutterRate = kfr.s.v; q.flutterDepth = kfd.s.v; q.dust = kdu.s.v; q.crackle = kcr.s.v; q.mix = km.s.v;
        t.process(m_, oL, oR, n, q); kwr.blk(n); kwd.blk(n); kfr.blk(n); kfd.blk(n); kdu.blk(n); kcr.blk(n); km.blk(n);
    }
private:
    Tape t; KParam kwr, kwd, kfr, kfd, kdu, kcr, km; double sr_;
};

// ------------------------------------------------------------------------------------------------------------------ dry/wet stages built from gain nodes
class FilterStage : public FxStage {
public:
    FilterStage(double sr) : sr_(sr) { f.prepare(sr); kc.init(20, 18000, 0.01, 2000); kr.init(0, 130, 0.01, 20); kdry.init(0, 1e9, 0.01, 0); kwet.init(0, 1e9, 0.01, 1); kdry.lo = -1e9; kwet.lo = -1e9; }
    void configure(const Json& p, bool instant, const FxEnv&) override {
        using namespace fxdetail; type = filterTypeIndex(p.str("filterType", "moog4pole")); formantRatiosFor(p.str("formantTable", "classic"), p.str("vowel", "a"), f.fr2, f.fr3);
        kc.apply(num(p, "cutoff", 2000), instant, sr_); kr.apply(num(p, "resonance", 20), instant, sr_);
        const double mx = num(p, "mix", 1); kdry.apply(1 - mx, instant, sr_); kwet.apply(mx, instant, sr_);
    }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        mono(iL, iR, m_, n);
        for (int i = 0; i < n; i++) {
            const double cut = kc.sample(), res = kr.sample(); const float w = (float)f.tick(m_[i], cut, res, type);
            const double gd = kdry.sample(), gw = kwet.sample();
            oL[i] = (float)(iL[i] * gd + w * gw); oR[i] = (float)(iR[i] * gd + w * gw);
        }
    }
private:
    MultiFilter f; KParam kc, kr, kdry, kwet; int type = 0; double sr_;
};

class GlitchStage : public FxStage {
public:
    GlitchStage(double sr, uint32_t seed) : sr_(sr) { rng_.s = seed ? seed : 1; g.prepare(sr, seed); kdry.init(-1e9, 1e9, 0.02, 0); kwet.init(-1e9, 1e9, 0.02, 1); }
    void configure(const Json& p, bool instant, const FxEnv& e) override {
        using namespace fxdetail;
        bitDepth = (int)clampd(num(p, "bitDepth", 16), 1, 16); srRed = clampd(num(p, "srReduction", 1), 1, 50); comp = clampd(num(p, "companding", 0), 0, 1); stutterOn = flag(p, "stutterOn", false); amount = clampd(num(p, "stutterAmount", 0.5), 0, 1);
        const double sixteenth = (60.0 / e.bpm) / 4; const double div = num(p, "stutterDivision", 0.25);
        static const struct { const char* n; double m; } tm[] = {{"1", 1}, {"1/2", 0.5}, {"1/4", 0.25}, {"2x", 2}};
        double mult = 1; const std::string td = p.str("seqTempoDiv", "1"); for (auto& t : tm) if (td == t.n) mult = t.m;
        interval = (int)clampd(jsRound(sixteenth * (1.0 / div) * (1.0 / mult) * sr_), 64, 200000);
        const double mx = num(p, "mix", 1); kdry.apply(1 - mx, instant, sr_); kwet.apply(mx, instant, sr_);
        // mini sequencer
        seqOn = flag(p, "seqOn", false); intensity = num(p, "seqIntensity", 0.3);
        nSteps = 0; if (auto st = p.get("seqSteps")) for (size_t i = 0; i < 6 && i < st->a.size(); i++) steps[i] = st->a[i].type == Json::Bool ? st->a[i].b : st->a[i].asNum() != 0; else for (bool& b : steps) b = true;
        seqPeriod = std::max(0.03, syncDivToSeconds(p.str("seqRate", "1/16"), e.bpm) / (mult ? mult : 1));
        if (!seqOn) { modes[0] = modes[1] = modes[2] = 0; seqCount = 0; curStep = -1; }
    }
    void setModes(int a, int b, int c) { modes[0] = a; modes[1] = b; modes[2] = c; }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        mono(iL, iR, m_, n);
        if (seqOn) { seqCount += n; const double per = seqPeriod * sr_; while (seqCount >= per) { seqCount -= per; seqTick(); } }
        Glitch::Params q; q.bitDepth = bitDepth; q.srReduction = srRed; q.companding = comp; q.stutterOn = stutterOn; q.stutterInterval = interval; q.stutterAmount = amount; q.modeA = modes[0]; q.modeB = modes[1]; q.modeC = modes[2];
        g.process(m_, a_, n, q);
        for (int i = 0; i < n; i++) { const double gd = kdry.sample(), gw = kwet.sample(); oL[i] = (float)(iL[i] * gd + a_[i] * gw); oR[i] = (float)(iR[i] * gd + a_[i] * gw); }
    }
    Glitch g;
private:
    void seqTick() {   // glitchSeqTick()
        int en[6], ne = 0; for (int i = 0; i < 6; i++) if (steps[i]) en[ne++] = i;
        if (!ne) { modes[0] = modes[1] = modes[2] = 0; return; }
        int idx = -1; for (int i = 0; i < ne; i++) if (en[i] == curStep) idx = i;
        idx = (idx + 1) % ne; curStep = en[idx];
        const int primary = curStep + 1, maxCombine = 1 + std::min(2, (int)std::floor(intensity * 3));
        int out[3] = {primary, 0, 0}, cnt = 1; int others[5], no = 0; for (int m = 1; m <= 6; m++) if (m != primary) others[no++] = m;
        for (int i = 1; i < maxCombine; i++) { if (!no) break; const int k = (int)std::floor(rng_.next() * no) % no; out[cnt++] = others[k]; for (int j = k; j + 1 < no; j++) others[j] = others[j + 1]; no--; }
        modes[0] = out[0]; modes[1] = out[1]; modes[2] = out[2];
    }
    KParam kdry, kwet; double sr_; int bitDepth = 16, interval = 11025; double srRed = 1, comp = 0, amount = 0.5; bool stutterOn = false;
    int modes[3] = {0, 0, 0}; bool seqOn = false, steps[6] = {true, true, true, true, true, true}; double intensity = 0.3, seqPeriod = 0.1, seqCount = 0; int curStep = -1, nSteps = 0; LcgRandom rng_;
};

// ------------------------------------------------------------------------------------------------------------------ Power Panning (+ multi-band)
class PanningStage : public FxStage {
public:
    PanningStage(double sr) : sr_(sr) {
        pan.prepare(sr); kw.init(0, 2, 0.01, 0); kr.init(0.02, 10, 0.01, 1); kd.init(0, 1, 0.01, 0);   // worklet defaults: the browser only ever setTargetAtTime()s these
        kdry.init(-1e9, 1e9, 0.02, 0); kwet.init(-1e9, 1e9, 0.02, 1); kcl.init(-1e9, 1e9, 0.02, 1); kmb.init(-1e9, 1e9, 0.02, 1);
        for (int i = 0; i < 4; i++) { bp[i].prepare(sr); kon[i].init(0, 1, 0.01, 1); kbw[i].init(0, 1, 0.02, 0.5); kbd[i].init(0, 1, 0.02, 0); kbs[i].init(0.02, 10, 0.02, 0.5); }
    }
    void configure(const Json& p, bool instant, const FxEnv&) override {
        using namespace fxdetail;
        kw.apply(num(p, "width", 1), false, sr_); autoPan = flag(p, "autoPanOn", false); kr.apply(num(p, "rate", 1), false, sr_);
        const std::string sh = p.str("shape", "square"); shape = sh == "square" ? 0 : sh == "triangle" ? 1 : sh == "exponential" ? 2 : 0;
        const std::string md = p.str("mode", "stereo"); mode = md == "stereo" ? 0 : md == "3dspace" ? 1 : 0; kd.apply(num(p, "distance", 0.3), false, sr_);
        const double mx = num(p, "mix", 1); kdry.apply(1 - mx, instant, sr_); kwet.apply(mx, instant, sr_);
        static const double dx[3] = {200, 1500, 6000}, dw[4] = {0.4, 0.7, 0.9, 1.0}, dp[4] = {0, 0.5, 0.6, 0.8}, ds[4] = {0.25, 0.4, 0.65, 0.9};
        const Json* mb = p.get("mb"); mbOn = mb ? flag(*mb, "on", false) : false;
        for (int i = 0; i < 3; i++) { xo[i] = dx[i]; if (mb) if (auto x = mb->get("xover")) if (x->isArr() && x->a.size() == 3) xo[i] = x->a[i].asNum(dx[i]); }
        for (int i = 0; i < 4; i++) {
            const Json* b = nullptr; if (mb) if (auto bs = mb->get("bands")) if (bs->isArr() && (size_t)i < bs->a.size()) b = &bs->a[i];
            bandOn[i] = b ? flag(*b, "on", true) : true; bandFx[i] = b ? (int)num(*b, "fx", 0) : 0;
            kon[i].apply(bandOn[i] ? 1 : 0, false, sr_); kbw[i].apply(b ? num(*b, "width", dw[i]) : dw[i], false, sr_); kbd[i].apply(b ? num(*b, "depth", dp[i]) : dp[i], false, sr_); kbs[i].apply(b ? num(*b, "speed", ds[i]) : ds[i], false, sr_);
        }
        if (mbOn) mbBuilt = true;
        kcl.apply(mbOn ? 0 : 1, false, sr_); kmb.apply(mbOn ? 1 : 0, false, sr_);   // built with gain 1 and faded by setTargetAtTime (tc 0.02), also at creation
        if (mbBuilt) setCrossovers();
    }
    bool mbActive() const { return mbOn; }
    int bandFxId(int i) const { return bandFx[i]; }
    bool bandEnabled(int i) const { return bandOn[i]; }
    FxStage* route[4] = {nullptr, nullptr, nullptr, nullptr};   // stage patched into band i (set by the chain), null = straight through
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        float cl[kQuantum], cr[kQuantum];
        Panning::Params q; q.width = kw.s.v; q.autoPan = autoPan; q.rate = kr.s.v; q.shape = shape; q.mode = mode; q.distance = kd.s.v;
        pan.process(iL, cl, cr, n, q);
        float ml[kQuantum], mr[kQuantum]; const bool haveMb = mbBuilt; if (haveMb) multiband(iL, iR, ml, mr, n);
        for (int i = 0; i < n; i++) {
            const double gd = kdry.sample(), gw = kwet.sample(), gc = kcl.sample(), gm = kmb.sample();
            const float wl = (float)(cl[i] * gc + (haveMb ? ml[i] * gm : 0.0)), wr = (float)(cr[i] * gc + (haveMb ? mr[i] * gm : 0.0));
            oL[i] = (float)(iL[i] * gd + wl * gw); oR[i] = (float)(iR[i] * gd + wr * gw);
        }
        kw.blk(n); kr.blk(n); kd.blk(n);
    }
private:
    struct LR { ChromBiquad a, b; void set(ChromBiquad::Type t, double f, double sr) { const double bw = -3.0102999566398116; a.set(t, f, bw, sr); b.set(t, f, bw, sr); } inline float tick(int ch, float x) { return b.tick(ch, a.tick(ch, x)); } };
    void setCrossovers() {
        lp1.set(ChromBiquad::LP, xo[0], sr_); hp1.set(ChromBiquad::HP, xo[0], sr_); lp2.set(ChromBiquad::LP, xo[1], sr_); hp2.set(ChromBiquad::HP, xo[1], sr_); lp3.set(ChromBiquad::LP, xo[2], sr_); hp3.set(ChromBiquad::HP, xo[2], sr_);
        ap2.set(ChromBiquad::AP, xo[1], 0.7071, sr_); ap3a.set(ChromBiquad::AP, xo[2], 0.7071, sr_); ap3b.set(ChromBiquad::AP, xo[2], 0.7071, sr_);
    }
    void multiband(const float* iL, const float* iR, float* mL, float* mR, int n) {
        float bL[4][kQuantum], bR[4][kQuantum];
        for (int i = 0; i < n; i++) for (int ch = 0; ch < 2; ch++) {
            const float x = ch == 0 ? iL[i] : iR[i];
            const float l1 = lp1.tick(ch, x), h1 = hp1.tick(ch, x);
            const float v0 = ap3a.tick(ch, ap2.tick(ch, l1));
            const float l2 = lp2.tick(ch, h1), h2 = hp2.tick(ch, h1);
            const float v1 = ap3b.tick(ch, l2);
            const float v2 = lp3.tick(ch, h2), v3 = hp3.tick(ch, h2);
            (ch == 0 ? bL[0] : bR[0])[i] = v0; (ch == 0 ? bL[1] : bR[1])[i] = v1; (ch == 0 ? bL[2] : bR[2])[i] = v2; (ch == 0 ? bL[3] : bR[3])[i] = v3;
        }
        std::fill(mL, mL + n, 0.f); std::fill(mR, mR + n, 0.f);
        for (int k = 0; k < 4; k++) {
            if (route[k]) { float tl[kQuantum], tr[kQuantum]; route[k]->process(bL[k], bR[k], tl, tr, n); std::copy(tl, tl + n, bL[k]); std::copy(tr, tr + n, bR[k]); }
            BandPan::Params q; q.on = kon[k].s.v; q.width = kbw[k].s.v; q.depth = kbd[k].s.v; q.speed = kbs[k].s.v; q.phase0 = k * 0.25; q.decor = kDecor[k];
            float ol[kQuantum], orr[kQuantum]; bp[k].process(bL[k], bR[k], ol, orr, n, q);
            for (int i = 0; i < n; i++) { mL[i] += ol[i]; mR[i] += orr[i]; }
            kon[k].blk(n); kbw[k].blk(n); kbd[k].blk(n); kbs[k].blk(n);
        }
    }
    Panning pan; BandPan bp[4]; KParam kw, kr, kd, kdry, kwet, kcl, kmb, kon[4], kbw[4], kbd[4], kbs[4];
    LR lp1, hp1, lp2, hp2, lp3, hp3; ChromBiquad ap2, ap3a, ap3b;
    bool autoPan = false, mbOn = false, mbBuilt = false, bandOn[4] = {true, true, true, true}; int shape = 0, mode = 0, bandFx[4] = {0, 0, 0, 0}; double xo[3] = {200, 1500, 6000}, sr_;
    static constexpr double kDecor[4] = {7, 9.5, 12, 15};
};

// ------------------------------------------------------------------------------------------------------------------ interstellar (spectral particle ambient FX)
class InterstellarStage : public FxStage {
public:
    InterstellarStage(double sr, uint32_t seed) { eng.prepare(sr, seed); }
    void configure(const Json& j, bool, const FxEnv& e) override {
        using namespace fxdetail; is::Params p;
        const std::string src = j.str("source", "live"); p.source = src == "capture" ? 1 : 0;
        p.freeze = flag(j, "freeze", false); p.capture = (int)std::lround(num(j, "capture", 0)); p.captureLen = num(j, "captureLen", 4); p.span = num(j, "span", 8);
        p.tonal = num(j, "tonal", 0.5);
        for (int b = 0; b < is::kBands; b++) { char k[8]; snprintf(k, sizeof k, "band%d", b + 1); p.band[b] = num(j, k, 0); }
        p.segments = (int)std::lround(num(j, "segments", 4)); p.segSync = flag(j, "segSync", false); p.segDiv = j.str("segDiv", "1/1"); p.segTime = num(j, "segTime", 4);
        const std::string ord = j.str("segOrder", "forward"); p.segOrder = ord == "reverse" ? 1 : ord == "random" ? 2 : ord == "walk" ? 3 : ord == "pingpong" ? 4 : 0;
        p.stretch = num(j, "stretch", 8); p.bloom = num(j, "bloom", 0.5);
        p.density = num(j, "density", 0.5); p.life = num(j, "life", 6); p.pitchUp = num(j, "pitchUp", 12); p.upProb = num(j, "upProb", 0.4); p.pitchDown = num(j, "pitchDown", 12); p.downProb = num(j, "downProb", 0.3);
        p.body = num(j, "body", 0.7); p.particles = num(j, "particles", 0.8); p.spread = num(j, "spread", 0.8);
        const std::string sn = j.str("snap", "key"); p.snap = sn == "fifths" ? 1 : sn == "free" ? 2 : 0;
        const std::string sc = j.str("scale", "auto"); p.scale = sc == "major" ? 1 : sc == "minor" ? 2 : sc == "dorian" ? 3 : sc == "lydian" ? 4 : sc == "pentMajor" ? 5 : sc == "pentMinor" ? 6 : sc == "whole" ? 7 : 0;
        p.key = (int)std::lround(num(j, "key", -1));
        p.resonance = num(j, "resonance", 0.3); p.ring = num(j, "ring", 3); p.strum = num(j, "strum", 0.3);
        const std::string rt = j.str("resTune", "segment"); p.resTune = rt == "key" ? 1 : rt == "fifths" ? 2 : 0;
        p.evolve = num(j, "evolve", 0.4); p.drift = num(j, "drift", 0.4);
        p.space = num(j, "space", 0.5); p.size = num(j, "size", 0.6); p.decay = num(j, "decay", 0.6); p.predelay = num(j, "predelay", 0.03); p.damp = num(j, "damp", 0.4);
        p.revMod = num(j, "revMod", 0.4); p.width = num(j, "width", 0.8); p.shimmer = num(j, "shimmer", 0.2); p.revFreeze = flag(j, "revFreeze", false);
        p.character = num(j, "character", 0); p.mix = num(j, "mix", 1); p.level = num(j, "level", 0); p.bpm = e.bpm;
        const std::string q = j.str("quality", "normal"); p.quality = q == "eco" ? 0 : q == "lush" ? 2 : 1;
        eng.setParams(p);
    }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override { eng.process(iL, iR, oL, oR, n); }
    is::Interstellar eng;
};

// ------------------------------------------------------------------------------------------------------------------ EQ rack slot
// The existing 4-band EQ (EQ in fx_basic.hpp, the same code the per-layer EQ and the web eq-processor use) as a rack effect: one EQ per channel so a
// stereo signal keeps its image, Mix = wet/dry, Output trim, bypass = the stage's enabled flag. Flat params f1..f4 / g1..g4 so every dial is an FXP target.
class EqStage : public FxStage {
public:
    explicit EqStage(double sr) : sr_(sr) {
        eq_[0].prepare(sr); eq_[1].prepare(sr); const double df[4] = {120, 600, 2500, 8000};
        for (int b = 0; b < 4; b++) { kf_[b].init(20, 18000, 0.01, df[b]); kg_[b].init(-15, 15, 0.01, 0); } ko_.init(-24, 24, 0.01, 0); km_.init(0, 1, 0.01, 1);
    }
    void configure(const Json& p, bool instant, const FxEnv&) override {
        using namespace fxdetail; static const char* fk[4] = {"f1", "f2", "f3", "f4"}; static const char* gk[4] = {"g1", "g2", "g3", "g4"}; const double df[4] = {120, 600, 2500, 8000};
        for (int b = 0; b < 4; b++) { kf_[b].apply(num(p, fk[b], df[b]), instant, sr_); kg_[b].apply(num(p, gk[b], 0), instant, sr_); }
        ko_.apply(num(p, "out", 0), instant, sr_); km_.apply(num(p, "mix", 1), instant, sr_);
    }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        EQ::Params ep; for (int b = 0; b < 4; b++) { ep.freq[b] = kf_[b].blk(n); ep.gain[b] = kg_[b].blk(n); }
        const double mix = km_.blk(n), trim = std::pow(10.0, ko_.blk(n) / 20);
        eq_[0].process(iL, a_, n, ep); eq_[1].process(iR, b_, n, ep);
        const float w = (float)(mix * trim), d = (float)(1 - mix);
        for (int i = 0; i < n; i++) { oL[i] = a_[i] * w + iL[i] * d; oR[i] = b_[i] * w + iR[i] * d; }
    }
private:
    double sr_; EQ eq_[2]; KParam kf_[4], kg_[4], ko_, km_;
};

// ------------------------------------------------------------------------------------------------------------------ IR convolution reverb
// Signal path (as the web stage): input -> in level -> pre-delay -> convolver (two engines crossfade when the IR changes) -> low cut / high cut -> tone shelves
// -> width / balance matrix -> out level -> equal-power wet, parallel to the dry signal. IR preparation happens on ir::Worker; the audio thread never allocates.
class IrStage : public FxStage {
public:
    explicit IrStage(double sr) : sr_(sr), sh_(std::make_shared<ir::Shared>()) {
        dlLen_ = (int)(0.5 * sr) + 8; for (int c = 0; c < 2; c++) dl_[c].assign((size_t)dlLen_, 0.f);
        kmix_.init(0, 1, 0.015, 0.3); kin_.init(-24, 12, 0.015, 0); kout_.init(-24, 12, 0.015, 0); kpre_.init(0, 500, 0.015, 0); klc_.init(20, 2000, 0.015, 20); khc_.init(1000, 20000, 0.015, 20000);
        ktone_.init(-1, 1, 0.015, 0); kwid_.init(0, 2, 0.015, 1); kbal_.init(-1, 1, 0.015, 0); ir::Worker::get().add(sh_);
    }
    ~IrStage() override { delete cur_; delete old_; }
    void configure(const Json& p, bool instant, const FxEnv&) override {
        using namespace fxdetail;
        kmix_.apply(num(p, "mix", 0.3), instant, sr_); kin_.apply(num(p, "inLevel", 0), instant, sr_); kout_.apply(num(p, "outLevel", 0), instant, sr_); kpre_.apply(num(p, "predelay", 0), instant, sr_);
        klc_.apply(num(p, "lowCut", 20), instant, sr_); khc_.apply(num(p, "highCut", 20000), instant, sr_); ktone_.apply(num(p, "tone", 0), instant, sr_); kwid_.apply(num(p, "width", 1), instant, sr_); kbal_.apply(num(p, "balance", 0), instant, sr_);
        ir::Params q; q.sr = sr_; const std::string id = p.str("irId", ir::kDefaultId); std::snprintf(q.id, sizeof q.id, "%s", id.c_str());
        q.shape.start = num(p, "start", 0); q.shape.end = num(p, "end", 1); q.shape.lenPct = num(p, "lenPct", 100); q.shape.earlyLate = num(p, "earlyLate", 0); q.shape.damping = num(p, "damping", 0);
        q.shape.irGain = num(p, "irGain", 0); q.shape.reverse = flag(p, "reverse", false); q.shape.normalise = flag(p, "normalise", true);
        if (!posted_ || std::strcmp(q.id, last_.id) != 0 || !(q.shape == last_.shape)) { sh_->post(q); last_ = q; posted_ = true; }
    }
    void process(const float* iL, const float* iR, float* oL, float* oR, int n) override {
        if (ir::Engine* e = sh_->pending.exchange(nullptr)) {
            if (old_) { if (!sh_->retire(old_)) delete old_; old_ = nullptr; }
            old_ = cur_; cur_ = e; fadePos_ = 0; fadeLen_ = std::max(1, (int)(0.08 * sr_)); if (!old_) fadePos_ = fadeLen_;
        }
        const double mix = kmix_.blk(n), inG = std::pow(10.0, kin_.blk(n) / 20), outG = std::pow(10.0, kout_.blk(n) / 20), pre = kpre_.blk(n) / 1000.0, lc = klc_.blk(n), hc = khc_.blk(n), tone = ktone_.blk(n), wid = kwid_.blk(n), bal = kbal_.blk(n);
        if (lc != cLc_ || hc != cHc_ || tone != cTone_) { cLc_ = lc; cHc_ = hc; cTone_ = tone;
            for (int c = 0; c < 2; c++) { hp_[c].setHighpassDb(clampd(lc, 20, 2000), 0.707, sr_); lp_[c].setLowpassDb(clampd(hc, 1000, std::min(20000.0, sr_ * 0.49)), 0.707, sr_); shelf(lo_[c], true, 250, -tone * 9); shelf(hi_[c], false, 3500, tone * 9); } }
        const double a = (1 + clampd(wid, 0, 2)) / 2, b = (1 - clampd(wid, 0, 2)) / 2, gl = bal > 0 ? 1 - bal : 1, gr = bal < 0 ? 1 + bal : 1;
        const double wetG = std::sin(clampd(mix, 0, 1) * M_PI / 2), dryG = std::cos(clampd(mix, 0, 1) * M_PI / 2), dsm = clampd(pre, 0, 0.5) * sr_;
        for (int i = 0; i < n; i++) {   // input level, then the pre-delay line (linear interpolation, like a DelayNode)
            dl_[0][(size_t)wp_] = (float)(iL[i] * inG); dl_[1][(size_t)wp_] = (float)(iR[i] * inG);
            double rp = (double)wp_ - dsm; while (rp < 0) rp += dlLen_; const int i0 = (int)rp; const double fr = rp - i0; const int i1 = i0 + 1 >= dlLen_ ? 0 : i0 + 1;
            dA_[i] = (float)(dl_[0][(size_t)i0] * (1 - fr) + dl_[0][(size_t)i1] * fr); dB_[i] = (float)(dl_[1][(size_t)i0] * (1 - fr) + dl_[1][(size_t)i1] * fr);
            if (++wp_ >= dlLen_) wp_ = 0;
        }
        if (cur_) cur_->conv.process(dA_, dB_, wA_, wB_, n); else { std::fill(wA_, wA_ + n, 0.f); std::fill(wB_, wB_ + n, 0.f); }
        if (old_) {
            old_->conv.process(dA_, dB_, xA_, xB_, n);
            for (int i = 0; i < n; i++) { const float g = fadePos_ >= fadeLen_ ? 1.f : (float)fadePos_ / (float)fadeLen_; wA_[i] = wA_[i] * g + xA_[i] * (1 - g); wB_[i] = wB_[i] * g + xB_[i] * (1 - g); if (fadePos_ < fadeLen_) fadePos_++; }
            if (fadePos_ >= fadeLen_) { if (!sh_->retire(old_)) delete old_; old_ = nullptr; }
        }
        for (int i = 0; i < n; i++) {
            double l = wA_[i], r = wB_[i];
            l = hp_[0].process(l); l = lp_[0].process(l); l = lo_[0].process(l); l = hi_[0].process(l);
            r = hp_[1].process(r); r = lp_[1].process(r); r = lo_[1].process(r); r = hi_[1].process(r);
            const double wl = gl * (a * l + b * r), wr = gr * (b * l + a * r);
            oL[i] = (float)(wl * outG * wetG + iL[i] * dryG); oR[i] = (float)(wr * outG * wetG + iR[i] * dryG);
        }
    }
    // status for the UI / tests
    bool missingIr() const { return sh_->missingFlag.load() != 0; }
    double irSeconds() const { return (double)sh_->lenSamples.load() / sr_; }
    uint32_t builtSerial() const { return sh_->built.load(); }
    bool engineActive() const { return cur_ != nullptr; }
private:
    // Chromium-style shelving filter (S = 1), as the web BiquadFilterNode uses
    void shelf(Biquad& bq, bool low, double freq, double gainDb) {
        const double A = std::pow(10.0, gainDb / 40), w0 = 2 * M_PI * freq / sr_, cw = std::cos(w0), sw = std::sin(w0), al = sw / 2 * std::sqrt(2.0), sq = std::sqrt(A);
        if (low) bq.norm(A * ((A + 1) - (A - 1) * cw + 2 * sq * al), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - 2 * sq * al), (A + 1) + (A - 1) * cw + 2 * sq * al, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - 2 * sq * al);
        else bq.norm(A * ((A + 1) + (A - 1) * cw + 2 * sq * al), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - 2 * sq * al), (A + 1) - (A - 1) * cw + 2 * sq * al, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - 2 * sq * al);
    }
    double sr_; std::shared_ptr<ir::Shared> sh_; ir::Engine *cur_ = nullptr, *old_ = nullptr; int fadePos_ = 0, fadeLen_ = 1; ir::Params last_; bool posted_ = false;
    KParam kmix_, kin_, kout_, kpre_, klc_, khc_, ktone_, kwid_, kbal_; double cLc_ = -1, cHc_ = -1, cTone_ = 1e9;
    Biquad hp_[2], lp_[2], lo_[2], hi_[2]; std::vector<float> dl_[2]; int dlLen_ = 0, wp_ = 0;
    float dA_[kQuantum], dB_[kQuantum], wA_[kQuantum], wB_[kQuantum], xA_[kQuantum], xB_[kQuantum];
};

// ------------------------------------------------------------------------------------------------------------------ the chain
class FxChain {
public:
    static constexpr int kMaxStages = 10;
    void prepare(double sr) { sr_ = sr; env_.sr = sr; stages_.clear(); }
    // Rebuilds every stage from the preset (like loadPresetData -> rebuildFxGraph). `seed` makes the random FX (tape / granular / glitch) repeatable.
    void configure(const std::vector<FxStageCfg>& cfg, double bpm, const std::string& oversample, uint32_t seed = 1) {
        env_.bpm = bpm; env_.oversample = oversample == "2x" ? 1 : oversample == "4x" ? 2 : 0;
        stages_.clear(); extra_.clear(); params_.clear(); seeds_.clear();
        for (size_t i = 0; i < cfg.size() && (int)i < kMaxStages; i++) {
            std::unique_ptr<FxStage> st = make(cfg[i].type, seed + (uint32_t)i * 7919u);
            if (!st) continue;
            st->type = cfg[i].type; st->id = cfg[i].id; st->enabled = cfg[i].enabled; for (int k = 0; k < kFxLayers; k++) st->sends[k] = cfg[i].sends[k];
            st->configure(cfg[i].params, true, env_); stages_.push_back(std::move(st)); params_.push_back(cfg[i].params); seeds_.push_back(seed + (uint32_t)i * 7919u);
        }
        route();
    }
    // live parameter change (UI knob / automation): same as the browser's refresh() - exponential approach, tc 10 ms
    void setStageParams(int index, const Json& p) {   // every instance of the stage follows the dial (the web fans refresh() out to its copies)
        if (index < 0 || index >= (int)stages_.size()) return;
        params_[index] = p; stages_[index]->configure(p, false, env_);
        if (index < (int)extra_.size()) for (auto& e : extra_[index]) e->configure(p, false, env_);
    }
    void setStageEnabled(int index, bool on) { if (index >= 0 && index < (int)stages_.size()) { stages_[index]->enabled = on; route(); } }
    void setStageSend(int index, int layer, bool on) { if (index >= 0 && index < (int)stages_.size() && layer >= 0 && layer < kFxLayers) { stages_[index]->sends[layer] = on; route(); } }
    void setBpm(double bpm) { env_.bpm = bpm; }
    // Battery Saving Mode: a stage whose input AND output have been below `thresh` for `blocks` quanta is skipped (its output is exactly zero) until signal returns.
    // blocks <= 0 disables it and wakes everything. Stages whose output never falls silent (noise, DC, free-running generators) simply never sleep.
    void setIdleSleep(int blocks, float thresh, uint64_t* skipped) { sleepBlocks_ = blocks; sleepThresh_ = thresh; skipped_ = skipped; if (blocks <= 0) wakeAll(); }
    void wakeAll() { for (auto& s : stages_) { s->asleep = false; s->quietBlocks = 0; } for (auto& v : extra_) for (auto& s : v) { s->asleep = false; s->quietBlocks = 0; } }
    int stageCount() const { return (int)stages_.size(); }
    FxStage* stage(int i) { return i >= 0 && i < (int)stages_.size() ? stages_[i].get() : nullptr; }
    // true when this layer reaches the master only through the chain
    bool layerHasActiveSend(int k) const { for (size_t i = 0; i < stages_.size(); i++) if (stages_[i]->enabled && !patched_[i] && stages_[i]->sends[k]) return true; return false; }
    // sendL/R[k]: the layer's post solo/mute bus (pre-EQ), stereo. Output: the chain's stereo result (zeros without an active chain).
    void process(const float* const sendL[kFxLayers], const float* const sendR[kFxLayers], float* outL, float* outR, int n) {
        std::fill(outL, outL + n, 0.f); std::fill(outR, outR + n, 0.f);
        // Per-layer signal chains (as the web): each group of layers runs through ONLY the stages those layers have ticked, in chain order. Layers with the same stage list
        // share one run of instances; a stage used by two different lists has one instance per list.
        float aL[kQuantum], aR[kQuantum], bL[kQuantum], bR[kQuantum];
        for (const Path& p : plan_) {
            std::fill(aL, aL + n, 0.f); std::fill(aR, aR + n, 0.f);
            for (int k = 0; k < kFxLayers; k++) if (p.layers & (1 << k)) for (int i = 0; i < n; i++) { aL[i] += sendL[k][i]; aR[i] += sendR[k][i]; }
            float *iL = aL, *iR = aR, *oL = bL, *oR = bR;
            for (size_t q = 0; q < p.stage.size(); q++) {
                FxStage& st = p.inst[q] == 0 ? *stages_[p.stage[q]] : *extra_[p.stage[q]][p.inst[q] - 1];
                if (sleepBlocks_ > 0 && st.type != "panning" && st.type != "interstellar" && st.type != "mbcomp") {
                    float inPk = 0; for (int i = 0; i < n; i++) { const float a = std::fabs(iL[i]), b = std::fabs(iR[i]); inPk = std::max(inPk, std::max(a, b)); }
                    if (st.asleep && inPk <= sleepThresh_) { std::fill(oL, oL + n, 0.f); std::fill(oR, oR + n, 0.f); std::swap(iL, oL); std::swap(iR, oR); if (skipped_) ++*skipped_; continue; }
                    st.asleep = false;
                    st.process(iL, iR, oL, oR, n);
                    float outPk = 0; for (int i = 0; i < n; i++) { const float a = std::fabs(oL[i]), b = std::fabs(oR[i]); outPk = std::max(outPk, std::max(a, b)); }
                    if (inPk <= sleepThresh_ && outPk <= sleepThresh_) { if (++st.quietBlocks >= sleepBlocks_) st.asleep = true; } else st.quietBlocks = 0;
                    std::swap(iL, oL); std::swap(iR, oR); continue;
                }
                st.process(iL, iR, oL, oR, n); std::swap(iL, oL); std::swap(iR, oR);
            }
            for (int i = 0; i < n; i++) { outL[i] += iL[i]; outR[i] += iR[i]; }
        }
    }
private:
    std::unique_ptr<FxStage> make(const std::string& t, uint32_t seed) {
        if (t == "eq") return std::make_unique<EqStage>(sr_);
        if (t == "ir") return std::make_unique<IrStage>(sr_);
        if (t == "reverb") return std::make_unique<ReverbStage>(sr_);
        if (t == "panning") return std::make_unique<PanningStage>(sr_);
        if (t == "delay") return std::make_unique<DelayStage>(sr_);
        if (t == "saturation") return std::make_unique<SaturationStage>(sr_);
        if (t == "wavefolder") return std::make_unique<WavefolderStage>(sr_);
        if (t == "filter") return std::make_unique<FilterStage>(sr_);
        if (t == "granular") return std::make_unique<GranularStage>(sr_, seed);
        if (t == "tape") return std::make_unique<TapeStage>(sr_, seed);
        if (t == "spectral") return std::make_unique<SpectralStage>(sr_);
        if (t == "mbcomp") return std::make_unique<MbCompStage>(sr_);
        if (t == "phaser") return std::make_unique<PhaserStage>(sr_, false);
        if (t == "flanger") return std::make_unique<PhaserStage>(sr_, true);
        if (t == "resonator") return std::make_unique<ResonatorStage>(sr_);
        if (t == "glitch") return std::make_unique<GlitchStage>(sr_, seed);
        if (t == "chorus") return std::make_unique<ChorusStage>(sr_);
        if (t == "echo") return std::make_unique<EchoStage>(sr_);
        if (t == "spread") return std::make_unique<SpreadStage>(sr_);
        if (t == "rotary") return std::make_unique<RotaryStage>(sr_);
        if (t == "amp") return std::make_unique<AmpStage>(sr_);
        if (t == "interstellar") return std::make_unique<InterstellarStage>(sr_, seed);
        return nullptr;
    }
    // multi-band patching: a stage named by an active panner's band is taken out of the main chain and processed inside that band
    void route() {
        patched_.assign(stages_.size(), false); extra_.resize(stages_.size());
        for (auto& st : stages_) if (st->type == "panning") for (int i = 0; i < 4; i++) static_cast<PanningStage*>(st.get())->route[i] = nullptr;
        for (auto& st : stages_) {
            if (st->type != "panning") continue; auto* pn = static_cast<PanningStage*>(st.get()); if (!(pn->enabled && pn->mbActive())) continue;
            for (int i = 0; i < 4; i++) {
                const int id = pn->bandFxId(i); if (!id) continue;
                for (size_t t = 0; t < stages_.size(); t++) if (stages_[t]->id == id && stages_[t]->type != "panning" && !patched_[t]) {
                    patched_[t] = true; if (pn->bandEnabled(i) && stages_[t]->enabled) pn->route[i] = stages_[t].get(); break;
                }
            }
        }
        plan();
    }
    // the chain plan: which stages (and which instance of each) every group of layers passes through
    struct Path { unsigned layers = 0; std::vector<int> stage, inst; };
    void plan() {
        plan_.clear(); std::vector<int> used(stages_.size(), 0); std::vector<std::vector<int>> sigs;
        for (int k = 0; k < kFxLayers; k++) {
            std::vector<int> list; for (size_t i = 0; i < stages_.size(); i++) if (stages_[i]->enabled && !patched_[i] && stages_[i]->sends[k]) list.push_back((int)i);
            if (list.empty()) continue;
            size_t g = 0; for (; g < sigs.size(); g++) if (sigs[g] == list) break;
            if (g == sigs.size()) { sigs.push_back(list); Path p; p.stage = list; plan_.push_back(p); }
            plan_[g].layers |= 1u << k;
        }
        for (Path& p : plan_) for (int si : p.stage) {
            const int n = used[si]++; p.inst.push_back(n);
            while ((int)extra_[si].size() < n) { auto e = make(stages_[si]->type, seeds_[si] + 104729u * (uint32_t)(extra_[si].size() + 1)); if (!e) break; e->type = stages_[si]->type; e->id = stages_[si]->id; e->enabled = true; e->configure(params_[si], true, env_); extra_[si].push_back(std::move(e)); }
        }
    }
    int sleepBlocks_ = 0; float sleepThresh_ = 0; uint64_t* skipped_ = nullptr;
    double sr_ = 48000; FxEnv env_; std::vector<std::unique_ptr<FxStage>> stages_; std::vector<bool> patched_;
    std::vector<std::vector<std::unique_ptr<FxStage>>> extra_; std::vector<Json> params_; std::vector<uint32_t> seeds_; std::vector<Path> plan_;
};

}  // namespace vv
