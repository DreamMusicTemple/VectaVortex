// Native synth engine: a C++ port of the web app's main-thread voice graph (buildLayerVoice), layer buses, EQ and master chain.
// Everything here runs in 128-frame quanta, exactly like the Web Audio render quantum, so the behaviour (and the
// offline-render reference tests) line up block for block. No allocation in process().
#pragma once
#include <atomic>
#include "sample_player.hpp"
#include "synth_common.hpp"
#include "compressor.hpp"
#include "master_out.hpp"
#include "procedural.hpp"
#include "flute.hpp"
#include "fx_chain.hpp"
#include "mod_defs.hpp"
#include "vocoder.hpp"
#include "hybrid_vocoder.hpp"
#include "colouring.hpp"
#include "Dx7Core.hpp"
#include "sid.hpp"
#include "lua_host.hpp"
#include "clips.hpp"

namespace vv {

// Chromium BiquadFilterNode lowpass (cutoff relative to Nyquist, Q in dB).
struct ChromiumLowpass {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    // Web Audio spec / Chromium: RBJ lowpass with Q given in dB (Q_linear = 10^(Q/20)).
    // copy coefficients (not filter state) from another instance
    ChromiumLowpass coeffsOnly(const ChromiumLowpass& st) const { ChromiumLowpass r = st; r.b0 = b0; r.b1 = b1; r.b2 = b2; r.a1 = a1; r.a2 = a2; return r; }
    void set(double freq, double qDb, double sr) {
        double cutoff = freq / (sr * 0.5); cutoff = std::max(0.0, std::min(1.0, cutoff));
        double B0, B1, B2, A0 = 1, A1 = 0, A2 = 0;
        if (cutoff == 1) { B0 = 1; B1 = B2 = 0; }
        else if (cutoff == 0) { B0 = B1 = B2 = 0; }
        else {
            const double Q = std::pow(10.0, qDb / 20.0);
            const double theta = 3.141592653589793 * cutoff, alpha = std::sin(theta) / (2 * Q), cosw = std::cos(theta), beta = (1 - cosw) / 2;
            B0 = beta; B1 = 2 * beta; B2 = beta; A0 = 1 + alpha; A1 = -2 * cosw; A2 = 1 - alpha;
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    inline double tick(double x) {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y; return y;
    }
};

inline double masterToneToFreq(double t) { return 20000.0 * std::pow(400.0 / 20000.0, std::max(0.0, std::min(1.0, t))); }

static_assert(kFxLayers == kFxSources, "FX send tables must cover every layer plus the XFX tap");
class Synth {
public:
    static constexpr int kMaxVoices = 128, kMaxGroups = 16, kMaxUnison = 16;

    // ------------------------------------------------------------------ setup (non-realtime)
    void prepare(double sr);
    void setWavetable(const std::string& name, std::shared_ptr<const WtTable> t) { tables_[name] = std::move(t); }
    void setPreset(std::shared_ptr<const Preset> p);
    // LUA layer script engines (loaded on a non-audio thread, played from the audio thread): see lua_host.hpp
    LuaHost& luaHost() { return lua_; }
    void setLuaTransport(bool playing, double beat, double bpm) { luaHostPlaying_.store(playing, std::memory_order_relaxed); luaHostBeat_.store(beat, std::memory_order_relaxed); luaHostBpm_.store((float)bpm, std::memory_order_relaxed); }
    // Compile + install every LUA slot script of a preset (non-audio thread; call before or right after setPreset).
    void loadLuaFromPreset(const Preset& p);
    // Live edit: swaps in a changed preset without resetting LFO phases, FX tails or sounding voices. Held notes keep what they were built with
    // (except modulated destinations); new notes, the FX chain, EQ, cross-mod, vocoder and the matrix pick the change up immediately.
    void updatePreset(std::shared_ptr<const Preset> p);
    // Sample library: samples are looked up by the slot's `name` when a note starts (like state.sampleLibrary in the web app).
    void setSample(const std::string& name, std::shared_ptr<const SampleBuf> b) { samples_[name] = std::move(b); }
    bool hasSample(const std::string& name) const { auto it = samples_.find(name); return it != samples_.end() && it->second; }
    // Sampler 'SAMPLES' focus fader (state.sampleMix): pos 0..15. Applied to new notes and glided (tc 20 ms) on sounding ones.
    void setSampleMix(bool on, double pos);
    bool sampleMixOn = false; double sampleMixPos = 0;
    const Preset* preset() const { return preset_.get(); }
    FxChain& fx() { return fx_; }   // live FX control (stage params / enable / sends)

    // ------------------------------------------------------------------ modulation matrix (modulation.hpp)
    // The web app evaluates the matrix in a 100 ms scheduler tick; tickSeconds keeps that cadence (tickSeconds <= 0: every quantum).
    double tickSeconds = 0.1;
    // ------------------------------------------------------------------ Battery Saving Mode (power.hpp). Callable from any thread; takes effect at the next quantum boundary.
    void setPower(const PowerCfg& c) { power_.store(c.pack(), std::memory_order_relaxed); }
    PowerCfg power() const { return PowerCfg::unpack(power_.load(std::memory_order_relaxed)); }
    const PowerStats& powerStats() const { return powerStats_; }
    int fxParamLatency = 0;   // frames between a matrix tick and the FX stages seeing the new values (matches the browser's AudioWorklet param hand-off)
    double modwheel = 0, pitchBend = 0 /* -1..1 */, bendRange = 2 /* semitones */;
    // MPE per-note expression (channels 1..15). Controller kinds: 0 pitch bend (-1..1), 1 timbre (CC74, 0..1), 2 pressure (0..1)
    bool mpeEnabled = false;
    void mpeNoteOn(int ch, int note) { if (mpeEnabled && ch >= 0 && ch < 16) mpeChanNote_[ch] = note; }
    void mpeNoteOff(int ch, int note) { if (mpeEnabled) { if (ch >= 0 && ch < 16) mpeChanNote_[ch] = -1; mpeExpr_[note & 127] = MpeExpr{}; } }
    void mpeGlide(int note, double semis) { mpeExpr_[note & 127].glide = semis; }   // touch-keyboard drag glide (continuous semitone offset)
    bool mpeController(int ch, int kind, double v) {
        if (!mpeEnabled || ch <= 0 || ch >= 16 || mpeChanNote_[ch] < 0) return false;
        MpeExpr& e = mpeExpr_[mpeChanNote_[ch] & 127]; e.has = true; (kind == 0 ? e.pitchBend : kind == 1 ? e.timbre : e.pressure) = v; return true;
    }
    void setMacro(int i, double v) { if (i >= 0 && i < 4) macroBase_[i] = v; }
    double macroValue(int i) const { return clampd(macroBase_[i] + macroOff_[i], 0, 1); }
    // XY pad positions (0..1; index = pad * 2 + axis) feed the matrix as modulation sources.
    void setXy(int i, double v) { if (i >= 0 && i < 8) xy_[i] = clampd(v, 0, 1); }
    double lfoPhase(int k) const { return lfo_[k].phase; }
    double arpRateOffset() const { return arpRateOff_; }
    double xmodOffset(int i) const { return xmodOff_[i]; }
    bool xmodModulated(int i) const { return xmodHas_[i]; }

    // ------------------------------------------------------------------ control (call between process() calls)
    // layerMask bit i = layer i (WT, FM, SMP, PRO). Events take effect at the next 128-frame boundary.
    void noteOn(int layerMask, int note, double velocity, const PLocks* plocks = nullptr, bool legato = false);
    void noteOff(int layerMask, int note, bool arpV = false);
    // sample-timed variants (events take effect at the first 128-frame boundary at or after `at`)
    void noteOnAt(uint64_t at, int layerMask, int note, double velocity, const PLocks* plocks = nullptr, bool legato = false) { events_.push_back(Ev{at, 0, layerMask, note, velocity, plocks ? *plocks : PLocks{}, legato}); }
    void noteOffAt(uint64_t at, int layerMask, int note, bool arpV = false) { PLocks p; p.arpMode = arpV ? 1 : 0; events_.push_back(Ev{at, 1, layerMask, note, 0, p, false}); }
    void quickStop(int layerMask, int note, double relTime) { events_.push_back(Ev{clock_, 2, layerMask, note, relTime, PLocks{}, false}); }   // legato hand-over: very fast cut, voice leaves the pool
    bool hasVoice(int layer, int note, bool arpV = false) { return findVoice(layer, note, arpV) != nullptr; }
    void clearPending() { events_.clear(); }
    uint64_t clock() const { return clock_; }
    bool atQuantumBoundary() const { return outPos_ >= kQuantum; }   // next process() call renders a fresh 128-frame quantum
    int quantumRemaining() const { return outPos_ >= kQuantum ? kQuantum : kQuantum - outPos_; }
    double sampleRate() const { return sr_; }
    void stopAllVoices(double relTime = 0.01);
    int activeVoiceCount() const { int n = 0; for (auto& v : voices_) if (v.alive) n++; return n; }

    double masterVolume = 0.8, masterTone = 0, wtFmMix = 0.5;
    // Automation clock: the transport beat (quarter notes) the lanes read, and whether the transport is running (lanes read neutral while stopped).
    void setAutoClock(double beat, bool running);
    // Clip launcher automation: every lane of every clip is a hidden modulation route (source kClipSrcBase + i). Performance feeds the current value of each lane per block.
    void setClipSet(std::shared_ptr<const clips::ClipSet> set) {   // runs where preset updates run (render thread under the engine's staging lock); routes only rebuild when the set of automated destinations changed
        std::vector<std::string> keys; if (set) for (const clips::LaneRef& lr : set->laneRefs) keys.push_back(set->at(lr.slot).lanes[(size_t)lr.lane].key);
        const bool changed = keys != clipKeys_; clipSet_ = std::move(set); if (!changed) return; clipKeys_ = std::move(keys);
        if (preset_) modReset(); else rebuildClipRoutes();
    }
    void setClipLaneValues(const double* v, int n) { for (int i = 0; i < clips::kMaxLaneRefs; i++) clipLane_[i] = i < n ? v[i] : 0.0; }
    void setWtFmMix(double v) { wtFmMix = v; applyWtFmMix(clampd(v, 0, 1)); }   // equal-power WT/FM crossfade
    // deterministic randomness for parity tests (voice seeds, noise buffers); production just keeps the defaults
    void seedRandom(uint32_t s) { rng_.s = s; }
    void seedNoise(uint32_t s);

    // ------------------------------------------------------------------ audio
    void process(float* outL, float* outR, int n);
    double now() const { return (double)clock_ / sr_; }
    // Web SOLO / MUTE buttons: bit l = layer l (L_WT..L_SID). A layer is audible when it is not muted and (nothing is soloed or it is soloed). Any thread.
    int soloMask() const { return soloMask_.load(std::memory_order_relaxed); }
    int muteMask() const { return muteMask_.load(std::memory_order_relaxed); }
    void setSoloMute(int soloMask, int muteMask) { soloMask_.store(soloMask, std::memory_order_relaxed); muteMask_.store(muteMask, std::memory_order_relaxed); }
    // Notes of sounding (alive, not released, not arp-generated) voices, bit n of word n>>6; any thread (chord analyzer)
    void activeNoteMask(uint64_t out[2]) const { out[0] = noteMask_[0].load(std::memory_order_relaxed); out[1] = noteMask_[1].load(std::memory_order_relaxed); }
    float sampleHead(int slot) const { return slot >= 0 && slot < 16 ? smpHead_[slot].load(std::memory_order_relaxed) : -1.f; }   // any thread
    void setScopeTapMask(int layer, uint32_t groups) { if (layer >= 0 && layer < kNumLayers) tapMask_[layer].store(groups, std::memory_order_relaxed); }
    // Copies the newest n (<= kTapN) samples of a tapped group, oldest first; returns false when the lane is not tapped. Any thread.
    bool scopeTapRead(int layer, int group, float* out, int n) const {
        if (layer < 0 || layer >= 5 || group < 0 || group >= 16 || n <= 0 || n > kTapN || !(tapMask_[layer].load(std::memory_order_relaxed) & (1u << group))) return false;
        const float* r = &tapRing_[(size_t)(layer * 16 + group) * kTapN]; const int w = tapW_[layer][group].load(std::memory_order_acquire);
        for (int i = 0; i < n; i++) out[i] = r[(w - n + i + kTapN * 2) & (kTapN - 1)]; return true;
    }
    // R11: lock-free LFO phase snapshot for display-rate UI (written by the control tick, read from any thread). hz = 0 while the LFO is held, so a UI can extrapolate between ticks.
    float lfoPhaseSnap(int k) const { return k >= 0 && k < kNumLfo ? lfoSnapPhase_[k].load(std::memory_order_relaxed) : 0.f; }
    float lfoRateHz(int k) const { return k >= 0 && k < kNumLfo ? lfoSnapHz_[k].load(std::memory_order_relaxed) : 0.f; }
    float mixerLevel(int i) const { return i >= 0 && i < kNumMix ? mixMeter_[i].load(std::memory_order_relaxed) : 0.f; }   // peak 0..~4, decays; any thread

    // Amp-envelope repeat: after the first Attack+Decay, schedule extra A+D cycles (rep 1..8) or an endless run (rep -1), just in time.
    // Mirrors the web app (cycle = max(10 ms, a+d+2 ms); each cycle dips to 0 over 2 ms, rises to peak over a, falls to peak*s over d).
    struct EnvRep {
        bool on = false, inf = false; int left = 0; double nextT = 0, a = 0, d = 0, s = 1, peak = 1;
        static double cycleLen(double a, double d) { return std::max(0.01, a + d + 0.002); }
        void arm(const EnvCfg& e, double pk, double t0) {
            on = e.rep != 0; if (!on) return;
            inf = e.rep < 0; left = inf ? 0 : std::min(8, e.rep);
            a = std::max(0.0, e.a); d = std::max(0.0, e.d); s = e.s; peak = pk; nextT = t0 + cycleLen(a, d);
        }
        // true when a cycle was scheduled this call (fires `cb` for each one; Param version below)
        template <class F> void service(double now, double horizon, F&& fire) {
            while (on && nextT <= now + horizon) { fire(nextT); nextT += cycleLen(a, d); if (!inf && --left <= 0) on = false; }
        }
        void scheduleOn(Param& p, double now, double horizon) {
            service(now, horizon, [&](double T) {
                p.setValueAtTime(peak * s, T); p.linearRampToValueAtTime(0, T + 0.002, now);
                p.linearRampToValueAtTime(peak, T + 0.002 + a, now); p.linearRampToValueAtTime(peak * s, T + 0.002 + a + d, now); });
        }
    };
    struct Group {
        bool active = false; EnvRep rep;
        MultiFilter filt; int ftype = 0; double reso = 20;
        Param cutoff{4000}, amp{0};
        double ckt = 4000; bool lpg = false; double lpgDecay = 0.8; float lpgCurve[400]; int lpgPoints = 0;
        FilterEnvCfg fe; double fvol = 1, panGL = 1, panGR = 1; bool panned = false; double envR = 0.3;
        int mixKey = -1; double mixCur = 1;   // Mixer window track (index into mixerGainDb) and its smoothed linear gain
        double panCur = 0, panStart = 0, panRef = 0, panTgt = 0; bool panLive = false;   // live pan (follows the pan fields, tau 15 ms) after note-on
        ModSlot cutM, resoM, fvolM, levelM;   // matrix modulation inputs (tau 20 ms)
        double filterOut = 0; int combSlot = -1; bool bypass = false; double panVal = 0;
    };
    struct WtOsc {
        bool on = false, sync = false; int idx = 0, group = 0, n = 1;
        Wavetable w[kMaxUnison]; double freq[kMaxUnison]; double monoPan[kMaxUnison], weights[kMaxUnison], panL[kMaxUnison], panR[kMaxUnison], phaseV[kMaxUnison]; PitchOffset pm[kMaxUnison]; double cents[kMaxUnison]; int bOct = 0, bSemi = 0; double bFine = 0, bDetune = 0;
        bool uLive = false; double tW[kMaxUnison], tPL[kMaxUnison], tPR[kMaxUnison], tPh[kMaxUnison];   // live Stack&Spread targets (Width/Blend/Phase), slewed per quantum   // tuning the stack was built with (GEN retune of held notes is relative to it)
        double oscGain = 1; double pwmPh = 0; double pos = 0, pwm = 0.5, phase = 0, startEnd = 1, segment = 1; bool pingPong = false;
        ModSlot ms[5]; bool modded = false;   // live pos/pwm/phase/startEnd/segment (tau 10 ms)
        bool sub = false; int subShape = 0; double subDiv = 1, subLevel = 0, subFreq = 0; SineOsc subOsc;   // subShape 0 sine / 1 triangle / 2 square
        const WtTable* table = nullptr; double level = 0;
    };
    struct FmEng {
        bool on = false; int base = 0, group = 0, algo = 2; FMVoice fm;
        Param baseFreq{440}; double ratio[4], level[4]; int wave[4]; double fb0 = 0; PitchOffset pc[4];
        bool sub = false; double subDiv = 1, subLevel = 0, subFreq = 0; SineOsc subOsc; double subGlideFrom = 0, subGlideT0 = 0, subGlideDur = 0, subTo = 0; bool subGlide = false;
        double tunedFreq = 440, freqBase = 440;
        ModSlot ratioM[4], fbM, subM; bool modded = false, subMod = false;
    };
    // WT ring-mod pair (carrier x modulator through a keytracked low-pass), feeds group 0. Waveforms are PolyBLEP-corrected (WebAudio's are wavetable band-limited).
    struct RingEng {
        bool on = false; int wave = 0; double fc = 440, fm = 440, phC = 0, phM = 0, t0 = 0, envCents = 0, envTc = 0.12, level = 1, bleed = 0, velDepth = 0; Biquad lpg;
    };
    struct ProGen { double lvTgt = 0.8, monoTgt = 1; double mixCur = 1; bool on = false; int idx = 0; Procedural proc; bool isFlute = false; flute::Core fl; double freq = 220, level = 0.8, mono = 1; PitchOffset pm; Procedural::Params pr; ModSlot gm[4]; bool modded = false; double bOct = 0, bSemi = 0, bFine = 0; /* tuning the note was built with (live retune is relative to it) */
                     float ring[1024] = {}; int rw = 0; int64_t ringN = 0; float xin[kQuantum] = {}; /* coupling: delayed copy of this generator's raw output */ };
    // LUA layer: one scripted voice (an lx::eng voice inside the slot's engine) per LUA slot that matches the note
    struct LuaSrc { bool on = false; int slot = -1; uint32_t gen = 0; double freq = 220, level = 0.8, width = 1, pan = 0; bool pol = false; PitchOffset pc; };
    struct NoiseSrc { bool on = false; int group = 0; double gain = 0; int type = 0; uint32_t pos = 0; };
    struct SmpSlot { EnvRep rep; bool on = false; int group = 0; SamplePlayer pl; Param fade{1}, env{1}; Smoother mix; double baseRate = 1; PitchOffset pm; double relTime = 0.05;
                     bool bleedOn = false; ModSlot bleedM; MicSpkChain col; int fxIdx[2] = {-1, -1}; };
    // 6-operator DX7 voice of the FM layer (replaces the two 4-operator engines when LayerCfg::sixOp is set)
    struct DxEng {
        bool on = false, started = false; dx7::Voice v{48000.0}; int note = 60; double freq = 440, trim = 1, releaseSec = 1; PitchOffset pc;
        float blk[dx7::N]; int blkPos = dx7::N;
    };
    // SID layer: three voices computed together by one SidChip (shared ring mod / sync); each voice has its own group (level, pan, filter copy)
    struct SidEng {
        bool on = false, tieNote = false; SidChip chip; SidVoiceParams p[3]; double freq[3] = {220, 220, 220}, level[3] = {0, 0, 0}, levelT[3] = {0, 0, 0}, fb = 220; PitchOffset pm[3]; ModSlot pwM[3]; bool pwMod[3] = {false, false, false};
        float out[3][kQuantum];
        // WT arp state (per held note, advanced once per modulation tick)
        double arpPhase = 0, lastT = -1, gateHigh[3] = {-1, -1, -1}; int arpIdx = -2; bool arpRestore = false;
    };
    struct Voice {
        SidEng sid;
        DxEng dx;
        RingEng ring;
        bool alive = false, released = false, arpVoice = false; int layer = 0, note = 0; double startT = 0, stopAt = 0; uint64_t age = 0;
        int ng = 0; Group g[kMaxGroups];
        PegRt peg[6];   // pitch envelopes (R11): 0/1 WT OSC1/2, 2/3 FM A/B, 4/5 Procedural A/B
        WtOsc wt[2]; WtOsc* wtSyncPair = nullptr; WavetableSync sync;
        FmEng fm[2]; ProGen pro[2]; LuaSrc lua[4]; NoiseSrc noise[8]; int nNoise = 0; double velocity = 0.8;
        double levelBase = 1; double noteVel = 0.8;   // note-on velocity (breath pressure of a flute voice)
        float outPk = 1.f; int quietBlocks = 0;   // Battery Saving: peak of this voice's last quantum (1 = unknown) and how many quanta in a row it was silent
        SmpSlot smp[16];
        double rnd = 0; bool rndSet = false, fmRetuned = false;
        EnvRep sidRep[3];                       // SID envelope repeat (drives SidVoiceParams::rt)
        const LayerCfg* xcfg = nullptr;         // XFX: the per-key resolved layer this voice was started with (null = the normal layer)
    };
    Voice* findVoice(int layer, int note, bool arpV = false) { for (auto& v : voices_) if (v.alive && !v.released && v.layer == layer && v.note == note && v.arpVoice == arpV) return &v; return nullptr; }
    std::vector<Voice>& voices() { return voices_; }

private:
    friend struct SynthTestAccess;
    struct Ev { uint64_t at; int kind; int mask; int note; double vel; PLocks pl; bool legato; };
    static double groupBasePan(const LayerCfg& LP, int gi);
    void applyEvents(uint64_t quantumStart);
    void doNoteOn(int layer, int note, double vel, const PLocks& pl, bool legato);
    void doNoteOff(int layer, int note, bool arpV = false);
    void startVoice(Voice& v, int layer, int note, const PLocks& pl, bool legato, double t);
    void stopVoice(Voice& v, double relTime, double t);
    void renderQuantum(float* L, float* R);
    void renderVoice(Voice& v, double t0, float* busL, float* busR);
    double midi(int layer, int note, const OscCfg& o) const { return midiToFreq(60 + (note - 60) * (o.pitchKeytrack / 100.0), preset_->a4); }
    const WtTable* table(const std::string& n) const { auto it = tables_.find(n); if (it != tables_.end()) return it->second.get(); auto d = tables_.find("analogMorph"); return d == tables_.end() ? nullptr : d->second.get(); }
    void setupGroup(Voice& v, int gi, const LayerCfg& L, const FilterCfg& gf, const FilterEnvCfg& fe, const EnvCfg& env, int note, const PLocks& pl, bool legato, double t, double panValue, bool applyPan, bool plockCutoff, bool sampler);

    // ---- modulation state
    struct LfoRt { double phase = 0; bool holdCycle = false; };
    struct EnvInfo { bool valid = false; double start = 0, a = 0, d = 0, s = 0, peak = 0; };
    struct WtLive { double pos = 0, pwm = 0.5, phase = 0, se = 1, seg = 1, hz = 0; bool mod = false; };
    LfoRt lfo_[kNumLfo]; std::vector<ModRoute> routes_; bool hasVoiceRoutes_ = false;
    double xy_[8] = {0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5};
    double mixOffDb_[kNumMix] = {}, mixTgt_[kNumMix];   // matrix offset (dB) and resulting linear gain per Mixer track
    float mixPeak_[kNumMix] = {};                       // audio-thread peak accumulator for the meters
    std::atomic<float> mixMeter_[kNumMix];
    std::atomic<float> lfoSnapPhase_[kNumLfo] = {}, lfoSnapHz_[kNumLfo] = {};
    // Scope lanes (web: the Zones tab SCOPE pill): pre-filter signal of one filter group (= oscillator; FM = engine) per layer, captured only while its bit is set.
    // The audio thread sums the voices into tapAcc_ and appends one quantum to the ring; the UI reads the ring lock-free. Fixed storage, no allocation in process().
    static constexpr int kTapN = 2048;
    LuaHost lua_; std::atomic<float> luaHostBpm_{120.f}; std::atomic<double> luaHostBeat_{0.0}; std::atomic<bool> luaHostPlaying_{false};
    void killLuaVoices(Voice& v);
    std::atomic<uint32_t> tapMask_[kNumLayers] = {};
    std::atomic<int> tapW_[kNumLayers][16] = {};
    std::vector<float> tapRing_ = std::vector<float>(kNumLayers * 16 * kTapN, 0.f);
    float tapAcc_[kNumLayers][16][kQuantum] = {};
    std::atomic<float> smpHead_[16];   // sampler playhead per slot (fraction of the source buffer, -1 idle) for the sample editor
    float smpHeadAcc_[16];
    std::atomic<int> soloMask_{0}, muteMask_{0};
    std::atomic<uint64_t> noteMask_[2] = {{0}, {0}};
    uint8_t dx7Live_[dx7::PATCH_SIZE] = {0}; bool dx7ModOn_ = false, dx7LiveValid_ = false;   // FM 6-OP patch with matrix offsets applied
    const uint8_t* dxPatch(const LayerCfg& L) const { return dx7ModOn_ && dx7LiveValid_ ? dx7Live_ : L.dx7Patch; }
    double macroBase_[4] = {0, 0, 0, 0}, macroOff_[4] = {0, 0, 0, 0}, lfoRateOff_[kNumLfo] = {}, lfoAmtOff_[kNumLfo] = {}, lfoPhaseOff_[kNumLfo] = {}, chaos_[2][3] = {{0.1, 0, 0}, {0.1, 0, 0}}, chaosRateOff_[2] = {0, 0}, lfoSmOff_[kNumLfo] = {}, lfoMorphOff_[kNumLfo] = {}, arpRateOff_ = 0;
    struct MpeExpr { bool has = false; double pitchBend = 0, timbre = 0, pressure = 0, glide = 0; };   // glide = on-screen keyboard drag, semitones
    MpeExpr mpeExpr_[128]; int mpeChanNote_[16] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    double wtShapeValue(int oi) const;
    double xmodOff_[4] = {0, 0, 0, 0}; bool xmodHas_[4] = {false, false, false, false};
    EnvInfo envInfo_[kNumLayers], envFollowInfo_;
    double wtNoteStart_ = -1e9;   // time of the latest WT note-on: origin of the position auto-scan filter / pitch envelope sources
    // Position auto-scan value 0..1 for WT oscillator i from its positionLfoSource (ampEnv / filterEnv / pitchEnv / any matrix source); returns false when Off or unknown.
    bool wtAutoScan(const OscCfg& o, double T, double& out);
    double lastTickT_ = 0; uint64_t tickOrigin_ = 0; long tickIndex_ = 1;
    WtLive wtLive_[2]; int wtOct_[2] = {0, 0}, wtSemi_[2] = {0, 0}; double wtFine_[2] = {0, 0}; bool wtTuneMod_ = false;
    int fmOct_[2] = {0, 0}, fmSemi_[2] = {0, 0}; double fmFine_[2] = {0, 0}; bool fmTuneMod_ = false;
    double fmRatioLive_[8]; bool fmRatioRoute_[8], fmFbRoute_[2], genRoute_ = false;
    bool wtFmModOn_ = false, toneModOn_ = false, smpMixModOn_ = false; double toneOff_ = 0;
    std::vector<Json> fxScratch_; std::vector<char> fxModded_;
    void modReset();
    void modTick(double T);
    void modVoice(Voice& v, double T);
    void modInitVoice(Voice& v, double t);
    void modFx(const double* acc);
    double fxAccPending_[D_COUNT]; bool fxPending_ = false; uint64_t fxDue_ = 0;
    void applyWtFmMix(double v) { busGain_[L_WT].setTarget(std::cos(v * 1.5707963267948966), sr_, 0.01); busGain_[L_FM].setTarget(std::sin(v * 1.5707963267948966), sr_, 0.01); }
    double modSource(int src, const Voice* v, double T);
    double modSourceRaw(int src, const Voice* v, double T);
    double autoLaneAt(const AutoLane& l) const;
    double autoBeat_ = 0; bool autoRun_ = false; bool autoSrcAny_ = false; double autoSrcOff_[40] = {};
    static constexpr int kAutoSrcBase = 1000;
    static constexpr int kClipSrcBase = 2000; std::shared_ptr<const clips::ClipSet> clipSet_; double clipLane_[clips::kMaxLaneRefs] = {}; void rebuildClipRoutes(); std::vector<std::string> clipKeys_;
    double lfoValue(int k, double phase, double smOff = 0, double morphOff = 0) const;
    double lfoHz(int k) const;

    double sr_ = 48000; uint64_t clock_ = 0;
    std::shared_ptr<const Preset> preset_;
    std::map<std::string, std::shared_ptr<const WtTable>> tables_;
    std::map<std::string, std::shared_ptr<const SampleBuf>> samples_;
    double sampleMixGainFor(int idx, double pos) const { return sampleMixOn ? clampd(1 - std::fabs(pos - idx), 0, 1) : 1.0; }
    std::vector<Voice> voices_; uint64_t ageCounter_ = 0;
    std::vector<Ev> events_;
    std::vector<double> lastPlayed_[kNumLayers];      // per osc, <0 = none
    double engineLast_[2] = {-1, -1};
    std::vector<float> noise_[5];                      // white, pink, brown, blue, violet
    // buses and master
    float bus_[kNumLayers][2][kQuantum];
    float xfxBus_[2][kQuantum];   // XFX tap: a copy of every voice that plays an XFX-zone key (send-only channel, heard only through the effects it is sent to)
    Smoother busGain_[kNumLayers], soloMute_[kNumLayers], scDuck_[kNumLayers];
    double scEnv_[kNumLayers] = {0, 0, 0, 0};   // sidechain peak followers on each layer's raw bus
    EQ eq_[kNumLayers];
    FxChain fx_; ChromiumLowpass tone_[2]; Compressor comp_; MasterOut mout_; Smoother masterGain_;
    float outBuf_[2][kQuantum]; int outPos_ = kQuantum;
    bool muted_[kNumLayers] = {false, false, false, false}, soloed_[kNumLayers] = {false, false, false, false};
    Smoother eqFreq_[kNumLayers][4], eqGain_[kNumLayers][4]; Smoother toneFreq_;
    JsRandom rng_;
    std::atomic<uint32_t> power_{0}; uint32_t powerApplied_ = 0xFFFFFFFFu; PowerPolicy policy_; PowerStats powerStats_; uint64_t fxSkipLocal_ = 0; int eqQuiet_[kNumLayers] = {}; bool eqAsleep_[kNumLayers] = {};
    int nextSlot_ = 0;   // New Note Next Sample cursor
    // Sampler Round Robin runtime state (never saved): rotation position per slot (and per key for scope "per key"), plus the take last played per slot for the UI.
    struct RrState { int pos = 0, last = -1, dir = 1; uint32_t epoch = 0; double t = -1e9; };
    RrState rrSlot_[32]; RrState rrKey_[16][128]; uint32_t rrEpoch_ = 0; std::atomic<int> rrLast_[32] = {};   // take + 1 (0 = none yet)
    int rrPick(const LayerCfg& L, int idx, int note, double& cents, double& gain);
public:
    void rrReset() { rrEpoch_++; for (auto& r : rrSlot_) { r.pos = 0; r.last = -1; r.dir = 1; } for (auto& k : rrKey_) for (auto& r : k) { r.pos = 0; r.last = -1; r.dir = 1; } }   // transport start / UI reset
    int rrLastTake(int slot) const { return slot >= 0 && slot < 32 ? rrLast_[slot].load(std::memory_order_relaxed) - 1 : -1; }
private:
    int pickAdvanceSlot(int note, const PLocks& pl);
    std::vector<std::vector<float>> combPool_; std::vector<int> combFree_;
    std::vector<ColFx> fxPool_; std::vector<int> fxFree_;   // mic / speaker character-FX state (pool: no allocation on the audio thread)
    int fxAcquire() { if (fxFree_.empty()) return -1; const int i = fxFree_.back(); fxFree_.pop_back(); return i; }
    void releaseVoiceFx(Voice& v) { for (auto& q : v.smp) for (int k = 0; k < 2; k++) if (q.fxIdx[k] >= 0) { fxFree_.push_back(q.fxIdx[k]); q.fxIdx[k] = -1; } }
    // generic (GEN:*) layer-parameter modulation: a working copy of a layer's config whose numeric fields follow the matrix; new notes are built from it
    LayerCfg genL_[kNumLayers]; bool genOn_[kNumLayers] = {}; bool genHas_[kNumGen] = {};
    const LayerCfg& effL(int l) const { return genOn_[l] ? genL_[l] : preset_->layers[l]; }
    // the layer a running voice plays: its XFX per-key view when it has one, else the live layer
    const LayerCfg& vL(const Voice& v) const { return v.xcfg ? *v.xcfg : effL(v.layer); }
    void serviceEnvRepeat(Voice& v, double t0);
public:
    // true while any sounding voice points into the current preset's XFX table (the control thread must not free that preset)
    void dropXfxRefs() { for (auto& v : voices_) v.xcfg = nullptr; xfxVoices_.store(0, std::memory_order_relaxed); }   // audio thread: when a retired preset could not be kept alive
    bool anyXfxVoice() const { return xfxVoices_.load(std::memory_order_relaxed) != 0; }
private:
    std::atomic<int> xfxVoices_{0};
    void genApply(const double* acc);
    void vocApply(const double* acc); bool vocHas_[kNumVoc] = {}; bool vocOn_ = false;   // GEN:VOC.* matrix destinations (live vocoder controls)
    bool genRouteFor(int layer, int field) const { for (int i = 0; i < kNumGen; i++) if (genHas_[i] && kGenDefs[i].layer == layer && kGenDefs[i].field == field) return true; return false; }
    ChromiumLowpass toneRef_;

    // ---- cross-modulation (RM / AM / PM between layers; FM is applied per voice through PitchOffset::xr) and vocoder
    struct XmodRt {
        bool active = false; int type = 0 /*0 RM 1 AM 2 PM*/, src = 0, dst = 0; Smoother pg, sg; bool offOn = false;
        float dl[2][4096]; uint32_t w = 0;
    };
    XmodRt xm_[4]; int xmChain_[kNumLayers][4]; int xmChainN_[kNumLayers] = {0, 0, 0, 0};
    TapRing tap_[kNumLayers];
    HybridVocoder voc_; float carBus_[2][kQuantum]; bool carOn_ = false; float extIn_[8192]; uint32_t extW_ = 0, extR_ = 0;   // external audio input ring (host/mic -> vocoder modulator/carrier)
    void xmodRebuild();
    void xmodApply(int i, double amount);
    void xmodChainProcess(int layer, const float* inL, const float* inR, float* outL, float* outR, int n);
public:
    MasterOut& masterOut() { return mout_; }
    void setSafetyCeiling(bool on) { safetyCeiling_ = on; }   // final +/-0.95 soft ceiling + 0.995 clamp; ON by default (golden tests switch it off)
    bool safetyCeiling_ = true;
    const HybridVocoder& vocoder() const { return voc_; }
    HybridVocoder& vocoderMut() { return voc_; }
    // Host/mic audio for the vocoder's external modulator / sidechain carrier (call from the audio callback before rendering; mono mix of the given channels).
    void feedInput(const float* l, const float* r, int n) { for (int i = 0; i < n; i++) { extIn_[extW_ & 8191] = r ? 0.5f * (l[i] + r[i]) : l[i]; extW_++; } }
    void rebuildVocoder();   // re-reads the preset's vocoder section (the web app only does this when a vocoder control is touched)
};

}  // namespace vv


#include "synth_impl.hpp"
