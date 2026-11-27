// Preset / state model - a C++ mirror of the web app's snapshotState() JSON ("layers", "connections", "lfo", "arp",
// "fxStages", "xmod", ...). Loaded on a non-realtime thread; the audio thread only ever reads an immutable Preset.
#pragma once
#include "sid.hpp"
#include "flute.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include "json.hpp"
#include "formant_tables.hpp"
#include "chordmaker.hpp"
#include "Dx7Core.hpp"
#include "dx7_roms.hpp"

namespace vv {

enum class LayerType { Wavetable, FM, Sample, Procedural, SID, Lua };
enum LayerIndex { L_WT = 0, L_FM = 1, L_SMP = 2, L_PRO = 3, L_SID = 4, L_LUA = 5, kNumLayers = 6 };
// XFX is the 7th layer of the UI (after LUA): it has no oscillators of its own, so it is not a LayerIndex. It exists as an FX send source (index kXfxSend) and as the solo / mute / hold bit 6 (kXfxBit).
constexpr int kXfxSend = kNumLayers, kFxSources = kNumLayers + 1, kXfxBit = 1 << kNumLayers;
static constexpr int kNumLfo = 6;
static const char* const kLayerKeys[kNumLayers] = {"WT", "FM", "SMP", "PRO", "SID", "LUA"};
inline int layerIndex(const std::string& k) { for (int i = 0; i < kNumLayers; i++) if (k == kLayerKeys[i]) return i; return -1; }

struct EnvCfg { double a = 0.01, d = 0.2, s = 0.7, r = 0.3; int rep = 0; /* amp-envelope repeat (web env.rep): 0 off, 1..8 extra Attack+Decay cycles while held, -1 continuous */ };
struct FilterEnvCfg { double amount = 0, a = 0.01, d = 0.2, s = 0.3, r = 0.3; };
// index into the multi-filter worklet: moog4pole 0, diode4pole 1, onepole6db 2, twopole12db 3, statevariable 4, comb 5, highpass 6, bandpass 7, formant 8, notch 9, isem 10, sid6581 11, sid8580 12
struct FilterCfg { int typeIdx = 0; bool lpg = false; double cutoff = 4000, reso = 20, keytrack = 0, volume = 1, lpgDecay = 0.8, fr2 = 1.74, fr3 = 3.71; bool on = true; };

inline int filterTypeIndex(const std::string& t) {
    static const char* names[] = {"moog4pole", "diode4pole", "onepole6db", "twopole12db", "statevariable", "comb", "highpass", "bandpass", "formant", "notch", "isem", "sid6581", "sid8580"};
    if (t == "lpg") return 2;
    for (int i = 0; i < 13; i++) if (t == names[i]) return i;
    return -1;   // indexOf() === -1 in the web app, passed straight to the worklet (which then falls through to its default)
}

// Mixer tracks (web MIXER_TRACKS order): one fader per WT osc, FM engine / DX7, sampler slot, procedural generator and the SID layer.
constexpr int kNumMix = 24;
inline const char* mixKey(int i) {
    static const char* const k[kNumMix] = {"WT1", "WT2", "FMA", "FMB", "FM6OP", "SMP1", "SMP2", "SMP3", "SMP4", "SMP5", "SMP6", "SMP7", "SMP8", "SMP9", "SMP10", "SMP11", "SMP12", "SMP13", "SMP14", "SMP15", "SMP16", "PRC1", "PRC2", "SID"};
    return i >= 0 && i < kNumMix ? k[i] : "";
}
inline int mixIndex(const std::string& s) { for (int i = 0; i < kNumMix; i++) if (s == mixKey(i)) return i; return -1; }
struct Transient { double pos = 0, pan = 0; bool reverse = false; };
// Sampler Round Robin (web osc.rr): the slot rotates through its own sample + the alternate takes (other slots' samples). order 0 sequential, 1 random, 2 random no repeat, 3 ping-pong;
// scope 0 continuous, 1 per key, 2 reset on transport start, 3 reset after 1.5 s idle. Takes keep their own start / end / root; everything else comes from the playing slot.
struct RrCfg { bool on = false; int order = 0, scope = 0; double variation = 0.3; std::vector<int> takes; };
struct CurvePt { double x = 0, y = 0, cx = 0, cy = 0; bool lin = false; };   // lin: straight segment leaving this point (Round 11 STRAIGHT mode)
// Round 11 pitch envelope (web osc.pitchEnv): a drawn curve over `time` seconds from note-on. bipolar: y 0.5 = no shift, 1 = +amount, 0 = -amount; unipolar: 0..1 -> 0..+amount (semitones).
struct PegCfg { bool on = false, bipolar = true, retrig = true; double amount = 12, time = 0.35; std::vector<CurvePt> pts; };
struct OscCfg {
    PegCfg peg;   // pitch envelope (WT OSC1/2, FM engine A/B = oscs 0/4, Procedural A/B)
    // keyboard zone / glide
    bool hold = false; int rangeLow = 0, rangeHigh = 127; double glideTime = 0, pitchKeytrack = 100;
    // shared
    double level = 0.8; int oct = 0, semitone = 0; double fineTune = 0, pan = 0, drift = 0;
    bool noiseOn = false; std::string noiseType = "white";
    bool enabled = true; double lastPlayedFreq = -1;   // <0: null (no previous note)
    // wavetable
    std::string table = "analogMorph"; double position = 0, detune = 0; int unison = 1; double width = 0, segment = 1; bool pingPong = false; int sub = 0; std::string subWave = "sine";
    double unisonBlend = 0, unisonPhase = 0; std::string positionLfoSource; double pwm = 0.5, pwmRate = 0, phase = 0, startEnd = 1;
    // SID voice (3 per layer): waveform name (triangle sawtooth pulse noise tri_saw tri_pulse saw_pulse), pulse width, ring mod, hard sync
    SidInstr sidIns;   // tracker-style instrument extras for this SID voice (vibrato, pulse/filter sweep + tables, hard restart); active=false when the preset has none
    std::string sidWave = "triangle"; double pulseWidth = 0.5; bool ringModOn = false, syncOn = false; double vibratoDepth = 0;
    // fm operator
    double ratio = 1; int wave = 0; double feedback = 0;
    // procedural
    std::string model = "pluck"; double bright = 0.5, damp = 0.4, body = 0.3, chaos = 0.4, stiffness = 0, bore = 0, brightKeyTrack = 0, dampKeyTrack = 0; int excite = 0;
    // wooden flutes / physical breath (model == "flute"): instrument profile + the 43 normalised controls (order of flute::kParams)
    int fluteInst = 0; double flute[flute::kNumParams] = {};
    // LUA scripted oscillator slot (one of four per LUA layer): script source + saved parameter values + per-slot voice settings
    bool luaPolarity = false, luaMono = false, luaSeedLock = true; int velLow = 1, velHigh = 127; double velCurve = 0, velSens = 1, luaWidth = 1; uint32_t luaSeed = 1;
    std::string luaSrc, luaId, luaName; std::vector<std::pair<std::string, double>> luaParams; bool luaMissing = false;
    // sampler
    std::string name; int root = 60; double start = 0, end = 1; bool loop = true, reverse = false; double fadeIn = 0, fadeOut = 0;
    std::vector<double> transients; std::vector<Transient> trans; EnvCfg env{0.005, 0.2, 0.8, 0.3}; bool hasEnv = false;
    RrCfg rr;
    bool arpOn = false, bleedOn = false; double bleed = 0.5; std::string mic = "none", spk = "none"; double micMix = 0, spkMix = 0;   // + the mic / speaker character-FX mix dials   // sampler: per-slot Arp, Bleed group, microphone / speaker colouring
    double loopStartFrac = -1, loopEndFrac = -1;   // <0: not set
    // own filter (WT osc / FM op0 of each engine / every sampler slot)
    bool hasFilter = false; FilterCfg filter; FilterEnvCfg filterEnv;
};

struct EqBand { double freq, gain; };
// Step sequencer data (layer.patterns in snapshotState()).
struct StepPLocks { bool hasLevel = false, hasCutoff = false, hasPosition = false; double level = 1, cutoff = 4000, position = 0; };
struct SeqStep { bool on = false; double pitch = 0, prob = 100, tempoRatio = 1; int ratchet = 1, transientIdx = 0; StepPLocks pl; };
// Piano roll (web pat.roll): notes in beats (quarter notes), loop region in bars. A pattern that carries a roll is played by the roll, not the step grid.
struct RollNote { double t = 0, l = 0.25, v = 0.9, prob = 100; int p = 60, rat = 1, tr = 0, slot = -1 /* SMP: play only this slot (pads recorded to the roll) */; StepPLocks pl; };
struct LanePt { double t = 0, v = 0; };   // recorded pitch-bend (-1..1) / mod-wheel (0..1) point, in beats
struct RollData { bool present = false, loopOn = true; int bars = 4; double loopStart = 0, loopEnd = 4; std::vector<RollNote> notes; std::vector<LanePt> bend, mod; /* r.lanes (piano-roll REC) */ };
struct SeqPattern { int length = 16; std::vector<SeqStep> steps; RollData roll; };

// SID "wavetable" arpeggiator (web layer.wtArp): one shared step list rewrites freq / wave / ring / sync / gate of every SID voice whose own arpOn is set
struct WtArpStep { std::string wave = "triangle", mode = "relative"; double value = 0; bool gate = true, ringMod = false, sync = false; int lengthTicks = 1; bool tempoSync = false; double syncDivision = 0.25; };
struct WtArpCfg { bool on = false; double speedHz = 50; bool hardRestart = false; std::vector<WtArpStep> steps; };
struct SidPerfCfg { bool vibrato = true, glide = false, autoGlide = false, tie = false, sustain = false, listen = false; double glideTime = 0.12; };
struct LayerCfg {
    SidPerfCfg sidPerf;
    LayerType type = LayerType::Wavetable;
    std::vector<OscCfg> oscs;
    FilterCfg filter; FilterEnvCfg filterEnv;     // layer-level (Procedural; legacy)
    EnvCfg env{0.01, 0.2, 0.7, 0.3}; EnvCfg env2; bool hasEnv2 = false;
    int fmAlgorithms[2] = {2, 2}; bool engineEnabled[2] = {false, false};
    double engineOctave[2] = {0, 0}, engineSemitone[2] = {0, 0}, engineFine[2] = {0, 0}, engineGlide[2] = {0, 0}; int fmSub[2] = {0, 0};
    // FM layer 6-operator (DX7-compatible) mode: one complete voice replaces the two 4-operator engines
    bool sixOp = false, sixOpEnabled = false; uint8_t dx7Patch[dx7::PATCH_SIZE] = {0}; bool hasDx7Patch = false;
    std::string dx7Bank = "ROM1A"; int dx7PatchIdx = 0; double dx7Transpose = 0, dx7OutputTrim = 1, dx7Pan = 0;
    double enginePan[2] = {0, 0};   // FM engine A/B pan trim (web layer.enginePan), added to the oscillator pan
    double pan = 0;   // SID layer: post-filter balance (the web app's layer.pan)
    bool globalAdsrActive = false; EnvCfg globalAdsr{0.005, 0.2, 0.8, 0.3};
    int slotAdvance = 0;   // sampler 'New Note Next / Random Sample': 0 off, 1 next, 2 random (web layer.slotAdvance)
    EqBand eq[4] = {{120, 0}, {600, 0}, {2500, 0}, {8000, 0}};
    WtArpCfg wtArp;
    bool syncEnabled = false;
    // WT dedicated OSC1 x OSC2 ring mod (web layer.ringModEnabled / layer.ringMod); OSC2's normal voice is replaced by the modulator
    double proCoupling = 0; bool ringModEnabled = false; std::string rmInterval = "p5", rmWave = "sine";
    double rmDetuneCents = 0, rmPitchEnvAmt = 0, rmPitchEnvDecay = 0.12, rmBleed = 0.15, rmVelDepth = 0.3, rmLpgCutoff = 4000, rmLpgKeytrack = 0.5;
    std::vector<SeqPattern> patterns; int currentSlot = 0; std::vector<int> chainOrder; std::string chainMode = "off";
    double gate = 0.7, scThresh = 0.25, scAmount = 0.8; int scSrc = -1;   // sequencer gate (fraction of a step) + sidechain (source layer index, -1 = off)
    const EnvCfg& envForGroup(int g) const { return (g == 0 || !hasEnv2) ? env : env2; }
};

struct Connection { std::string source, dest; double amount = 0; };
// Automation lanes (web state.automation): 8 samples per beat in 0..1, 0.5 = no change. Destination lanes become hidden matrix routes (source "auto:<dest>");
// source lanes ("S:<source>") add to that source's live value. A lane plays only while one of its mixer channels has A on.
constexpr int kAutoR = 8;
struct AutoLane { std::string key; std::vector<float> v; bool on = false, isSource = false; };
struct AutoCfg { int bars = 4; std::vector<AutoLane> lanes; };
struct LfoCfg { std::string rateMode = "hz"; double hz = 4, division = 0.25, phase = 0; std::string mode = "free"; bool held = false; std::string waveform = "sine"; bool bipolar = true; double amount = 1, phaseOffset = 0, smoothing = 0, morph = 0.5; std::vector<double> shape; std::vector<CurvePt> curve; };
struct ArpAdvCfg { std::string algo = "fractal", scale = "major"; int algoId = 0, scaleId = 0; double seed = 1234, probability = 1, density = 0.6, complexity = 0.5, mutation = 0.2, variation = 0, randomness = 0, humanise = 0, octSpread = 0, ratchet = 0, velVar = 0, accent = 0.3, gateVar = 0, groove = 0.5, patLen = 16, cycleVar = 0, rotation = 0, skipProb = 0, rhythmDisp = 0, mutSalt = 0, mutAmt = 0, varSeed = 0; };
struct ArpCfg { bool on = false, latch = false; std::string order = "UP", rateMode = "sync", target = "ALL"; std::vector<std::string> targets; /* several arp layers at once (web: state.arp.targets); empty or {"ALL"} = all layers */ double division = 0.5, hz = 4; int octRange = 1; std::string mode = "standard"; /* "standard" | "advanced" */ double gate = 0.8, swing = 50, velocity = 0.9; int repeat = 1, transpose = 0; ArpAdvCfg adv; /* current A/B slot */ ArpAdvCfg advOther; bool hasOther = false, curIsA = true, morphOn = false; double morphAmt = 0.5; /* other slot + morph (web state.arp.ab / morph) */ };
struct FxStageCfg { int id = 0; std::string type; bool enabled = true; Json params; bool sends[kFxSources] = {}; };
struct XmodCfg { bool on = false; std::string src, dst, type; double amount = 0; };
struct SampleRef { std::string name, b64; };   // preset-embedded audio (web snap.sampleLibrary: WAV, base64); decoded by FullEngine when it changes
struct CustomWt { std::string id; std::vector<std::vector<float>> frames; };   // web snap.customWavetables (imported / drawn / harmonic / formula tables, id "custom:...")

// XFX MODE (per-key sound design, web state.xfx / extra.xfx). Built once on the control thread when a preset is parsed: for every key of the ACTIVE bank
// that overrides something on a layer, a complete resolved LayerCfg (the layer JSON with that key's overrides applied, parsed by the normal layer parser).
// The audio thread only does a hash lookup; nothing is allocated or parsed while playing. Keys without overrides use the ordinary layer.
struct XfxTable {
    int8_t en[128][kNumLayers];                 // per-key layer switch: -1 inherit, 0 off, 1 force on
    std::unordered_map<int, LayerCfg> cfg;      // key = note * kNumLayers + layer
    bool any = false, anyHold = false; int bank = 0; std::string bankName;
    bool zone[128] = {}; bool hold = false;     // the XFX zone: keys that carry any override / layer switch in the active bank; hold = HOLD XFX (latch those keys)
    bool inZone(int note) const { return note >= 0 && note < 128 && zone[note]; }
    XfxTable() { for (auto& r : en) for (auto& e : r) e = -1; }
    const LayerCfg* find(int layer, int note) const { if (!any) return nullptr; auto it = cfg.find(note * kNumLayers + layer); return it == cfg.end() ? nullptr : &it->second; }
    int enable(int layer, int note) const { return (note >= 0 && note < 128) ? en[note][layer] : -1; }
};

struct Preset {
    LayerCfg layers[kNumLayers];
    LayerCfg xfxSeq; bool hasXfxSeq = false;   /* Round 6: patterns (piano roll) of the XFX pseudo layer, parsed from extra.xfxSeq; notes are expanded per key to the XFX layers */
    std::shared_ptr<const XfxTable> xfx;   // null when the preset has no XFX programming (or XFX is bypassed)
    std::vector<Connection> connections;
    LfoCfg lfo[kNumLfo];
    ArpCfg arp;
    bool bendTargets[kNumLayers][16];   // pitch-bend targets: WT osc 1/2, FM engine A/B, SMP slots 1-16, PRO gen A/B (web: extra.pitchBendTargets); default all true
    Preset() { for (auto& l : bendTargets) for (bool& b : l) b = true; for (int i = 0; i < 16; i++) { drumLen[i] = 16; for (int k = 0; k < 32; k++) drumSteps[i][k] = false; } }
    std::vector<FxStageCfg> fx;
    XmodCfg xmod[4];
    double bpm = 120, a4 = 440; std::string oversample = "normal";
    bool drumOn = false; int drumLen[16]; bool drumSteps[16][32]; bool drumXfx = false; int drumBase = 36;   /* drumXfx: lane i plays XFX key drumBase + i instead of sampler slot i */   // 16-lane drum sequencer (lane i triggers sampler slot i)
    double mixerGainDb[kNumMix] = {};   // Mixer window faders (web state.mixerGains), dB
    bool mixMute[kNumMix + 1] = {}, mixSolo[kNumMix + 1] = {}, mixAuto[kNumMix + 1] = {};   // M / S / A per Mixer track; index kNumMix = Master (no solo)
    AutoCfg automation;
    double chaosRate[2] = {0.5, 0.5};   // Lorenz / Rossler attractor rate dials
    double macros[4] = {0, 0, 0, 0};
    bool swingOn = false; double swingAmount = 50; int maxPolyphony = 64; bool legato = false;
    Json envFollow, noteRepeat, vocoder;
    cm::Cfg chord;   // generative chord maker (settings + current progression)
    std::vector<SampleRef> samples;   // embedded base64 audio
    std::vector<CustomWt> customTables;
};

namespace modelio {

inline cm::Cfg chordCfg(const Json* j) {
    cm::Cfg c; if (!j || !j->isObj()) return c;
    c.on = j->boolean("on", false); c.key = (int)std::min(11.0, std::max(0.0, j->num("key", 0))); c.scale = j->str("scale", "major");
    c.naturalness = std::min(1.0, std::max(0.0, j->num("naturalness", 0.7))); c.density = std::min(1.0, std::max(0.0, j->num("density", 0.4)));
    c.sparseness = std::min(1.0, std::max(0.0, j->num("sparseness", 0.15))); c.randomness = std::min(1.0, std::max(0.0, j->num("randomness", 0.1)));
    c.prob = std::min(100.0, std::max(0.0, j->num("prob", 100))); c.steps = (int)std::min(16.0, std::max(1.0, j->num("steps", 8))); c.octave = (int)std::min(6.0, std::max(1.0, j->num("octave", 3)));
    c.voicing = j->str("voicing", "close"); c.rate = std::min(8.0, std::max(0.05, j->num("rate", 1))); c.gate = std::min(1.0, std::max(0.05, j->num("gate", 0.9)));
    c.strum = std::min(120.0, std::max(0.0, j->num("strum", 0))); c.vel = std::min(1.0, std::max(0.05, j->num("vel", 0.8))); c.target = j->str("target", "ALL");
    c.seed = (uint32_t)j->num("seed", 0);
    if (auto e = j->get("euclid")) if (e->isObj()) { c.euOn = e->boolean("on", false); c.euN = (int)std::min(32.0, std::max(1.0, e->num("steps", 8))); c.euP = (int)std::min((double)c.euN, std::max(1.0, e->num("pulses", 5))); c.euR = (int)std::min(31.0, std::max(0.0, e->num("rotate", 0))); }
    if (auto pr = j->get("progression")) if (pr->isArr()) for (auto& x : pr->a) {
        cm::Chord ch; ch.deg = (int)x.num("deg", 0); ch.rest = x.boolean("rest", false); ch.locked = x.boolean("locked", false);
        if (auto t = x.get("tones")) if (t->isArr()) for (auto& v : t->a) ch.tones.push_back((int)v.asNum());
        if (!ch.rest && ch.tones.empty()) ch.rest = true;
        c.prog.push_back(ch); if (c.prog.size() >= 32) break;
    }
    if (auto h = j->get("history")) if (h->isArr()) for (auto& v : h->a) if (v.isStr()) c.history.push_back(v.s);
    return c;
}
inline Json chordToJson(const cm::Cfg& c, const Json* base) {
    Json o; o.type = Json::Obj; if (base && base->isObj()) o = *base;
    auto set = [&](const char* k, Json v) { for (auto& kv : o.o) if (kv.first == k) { kv.second = v; return; } o.o.push_back({k, v}); };
    Json pr; pr.type = Json::Arr;
    for (auto& ch : c.prog) { Json x; x.type = Json::Obj; Json d; d.type = Json::Num; d.n = ch.deg; x.o.push_back({"deg", d}); Json t; t.type = Json::Arr; for (int v : ch.tones) { Json n; n.type = Json::Num; n.n = v; t.a.push_back(n); } x.o.push_back({"tones", t});
        Json r; r.type = Json::Bool; r.b = ch.rest; x.o.push_back({"rest", r}); Json l; l.type = Json::Bool; l.b = ch.locked; x.o.push_back({"locked", l}); pr.a.push_back(x); }
    set("progression", pr);
    Json h; h.type = Json::Arr; for (auto& sg : c.history) { Json v; v.type = Json::Str; v.s = sg; h.a.push_back(v); } set("history", h);
    Json sd; sd.type = Json::Num; sd.n = (double)c.seed; set("seed", sd);
    return o;
}
inline EnvCfg env(const Json* j, EnvCfg d) { if (!j || !j->isObj()) return d; d.a = j->num("a", d.a); d.d = j->num("d", d.d); d.s = j->num("s", d.s); d.r = j->num("r", d.r); { const int r = (int)j->num("rep", d.rep); d.rep = r < 0 ? -1 : std::min(8, r); } return d; }
inline FilterCfg filter(const Json* j, FilterCfg d) {
    if (!j || !j->isObj()) return d;
    std::string t = j->str("type", ""); if (!t.empty()) { d.typeIdx = filterTypeIndex(t); d.lpg = (t == "lpg"); }
    d.cutoff = j->num("cutoff", d.cutoff); d.reso = j->num("reso", d.reso); d.keytrack = j->num("keytrack", d.keytrack); d.volume = j->num("volume", d.volume); d.lpgDecay = j->num("lpgDecay", d.lpgDecay); d.on = j->boolean("on", true);
    formantRatiosFor(j->str("formantTable", "classic"), j->str("vowel", "a"), d.fr2, d.fr3);
    return d;
}
inline FilterEnvCfg fenv(const Json* j, FilterEnvCfg d) {
    if (!j || !j->isObj()) return d;
    d.amount = j->num("amount", d.amount); d.a = j->num("a", d.a); d.d = j->num("d", d.d); d.s = j->num("s", d.s); d.r = j->num("r", d.r); return d;
}
inline OscCfg osc(const Json& j, LayerType lt) {
    OscCfg o;
    if (lt == LayerType::Sample) { o.level = 0; }
    o.hold = j.boolean("hold", false); o.rangeLow = (int)j.num("rangeLow", 0); o.rangeHigh = (int)j.num("rangeHigh", 127);
    o.glideTime = j.num("glideTime", 0); o.pitchKeytrack = j.num("pitchKeytrack", 100);
    o.level = j.num("level", o.level); o.oct = (int)j.num("oct", 0); o.semitone = (int)j.num("semitone", 0); o.fineTune = j.num("fineTune", 0);
    o.pan = j.num("pan", 0); o.drift = j.num("drift", 0);
    o.noiseOn = j.boolean("noiseOn", false); o.noiseType = j.str("noiseType", "white");
    o.table = j.str("table", "analogMorph"); o.position = j.num("position", 0); o.detune = j.num("detune", 0); o.unison = (int)j.num("unison", 1);
    o.width = j.num("width", 0); o.segment = j.num("segment", 1); o.pingPong = j.boolean("pingPong", false); o.sub = (int)j.num("sub", 0); o.subWave = j.str("subWave", "sine");
    o.positionLfoSource = j.str("positionLfoSource", ""); o.pwm = j.num("pwm", 0.5); o.pwmRate = j.num("pwmRate", 0); o.phase = j.num("phase", 0); o.startEnd = j.num("startEnd", 1);
    o.enabled = j.has("enabled") ? j.boolean("enabled", true) : (lt != LayerType::Procedural && lt != LayerType::SID && lt != LayerType::Lua);
    if (j.has("lastPlayedFreq") && j.get("lastPlayedFreq")->isNum()) o.lastPlayedFreq = j.num("lastPlayedFreq", -1);
    o.ratio = j.num("ratio", 1);
    if (const Json* pe = j.get("pitchEnv")) if (pe->isObj()) {   // Round 11 pitch envelope
        o.peg.on = pe->boolean("on", false); o.peg.amount = pe->num("amount", 12); o.peg.time = pe->num("time", 0.35); o.peg.bipolar = pe->boolean("bipolar", true); o.peg.retrig = pe->boolean("retrig", true);
        if (const Json* pp = pe->get("pts")) if (pp->isArr()) {
            for (const auto& v : pp->a) { CurvePt pt; pt.x = v.num("x", 0); pt.y = v.num("y", 0); pt.cx = v.num("cx", NAN); pt.cy = v.num("cy", NAN); pt.lin = v.boolean("lin", false); o.peg.pts.push_back(pt); }
            for (size_t k = 0; k + 1 < o.peg.pts.size(); k++) if (std::isnan(o.peg.pts[k].cx)) { o.peg.pts[k].cx = (o.peg.pts[k].x + o.peg.pts[k + 1].x) / 2; o.peg.pts[k].cy = (o.peg.pts[k].y + o.peg.pts[k + 1].y) / 2; }
            if (!o.peg.pts.empty()) { o.peg.pts.back().cx = o.peg.pts.back().x; o.peg.pts.back().cy = o.peg.pts.back().y; }
        }
    }
    if (lt == LayerType::Lua) {
        o.luaPolarity = j.boolean("polarity", false); o.luaMono = j.boolean("mono", false); o.velLow = (int)j.num("velLow", 1); o.velHigh = (int)j.num("velHigh", 127);
        o.velCurve = j.num("velCurve", 0); o.velSens = j.num("velSens", 1); o.luaWidth = j.num("width", 1);
        if (const Json* lu = j.get("lua")) if (lu->isObj()) {
            o.luaSrc = lu->str("src", ""); o.luaId = lu->str("id", ""); o.luaName = lu->str("name", ""); o.luaSeed = (uint32_t)std::max(0.0, lu->num("seed", 1)); o.luaSeedLock = lu->boolean("seedLock", true);
            if (const Json* pp = lu->get("params")) if (pp->isObj()) for (const auto& kv : pp->o) o.luaParams.emplace_back(kv.first, kv.second.asNum());
        }
    }
    if (lt == LayerType::SID) { o.sidWave = j.str("wave", "triangle"); o.pulseWidth = j.num("pulseWidth", 0.5); o.ringModOn = j.boolean("ringModOn", false); o.syncOn = j.boolean("syncOn", false); o.vibratoDepth = j.num("vibratoDepth", 0); }
    if (lt == LayerType::SID && j.has("sid") && j.get("sid")->isObj()) {   // presets saved before the SID instrument panel simply lack this and keep the neutral defaults
        const Json& q = *j.get("sid"); SidInstr& I = o.sidIns; I.active = true;
        I.hr = (int)q.num("hr", 0); I.vibDelay = q.num("vibDelay", 0); I.vibRate = q.num("vibRate", 6); I.vibAmp = q.num("vibAmp", 0); I.vibInc = q.num("vibInc", 0);
        I.pwSweep = q.num("pwSweep", 0); I.pwMin = q.num("pwMin", 0.1); I.pwMax = q.num("pwMax", 0.9); I.pwTab = q.boolean("pwTab", false);
        I.fCut = q.num("fCut", 1024); I.fSweep = q.num("fSweep", 0); I.fMin = q.num("fMin", 0); I.fMax = q.num("fMax", 2047); I.fReso = q.num("fReso", 0); I.fTab = q.boolean("fTab", false); I.fRoute = q.boolean("fRoute", true);
        if (const Json* fm = q.get("fMode")) { I.lp = fm->boolean("lp", false); I.bp = fm->boolean("bp", false); I.hp = fm->boolean("hp", false); }
        if (const Json* t = q.get("pwTable")) { I.pwLoop = t->boolean("loop", true); I.pwSync = t->boolean("sync", true); if (const Json* st = t->get("steps")) { I.pwN = 0; for (const Json& e : st->a) { if (I.pwN >= kSidTableMax) break; SidPwStep& d = I.pwS[I.pwN++]; d.len = std::max(1, (int)e.num("len", 8)); d.sweep = e.num("sweep", 0); d.set = e.boolean("set", false); d.val = e.num("val", 0.5); } } }
        if (const Json* t = q.get("fTable")) { I.fLoop = t->boolean("loop", true); I.fSync = t->boolean("sync", true); if (const Json* st = t->get("steps")) { I.fN = 0; for (const Json& e : st->a) { if (I.fN >= kSidTableMax) break; SidFStep& d = I.fS[I.fN++]; d.len = std::max(1, (int)e.num("len", 8)); d.cut = e.num("cut", 0); d.sweep = e.num("sweep", 0); d.set = e.boolean("set", false); d.hp = e.boolean("hp", false); d.bp = e.boolean("bp", false); d.lp = e.boolean("lp", false); } } }
    }
    { std::string w = j.str("wave", "sine"); o.wave = (w == "triangle") ? 1 : (w == "square") ? 2 : 0; }
    o.feedback = j.num("feedback", 0);
    o.model = j.str("model", "pluck"); o.bright = j.num("bright", 0.5); o.damp = j.num("damp", 0.4); o.body = j.num("body", 0.3); o.chaos = j.num("chaos", 0.4);
    if (o.model == "flute") { const Json* fl = j.get("flute"); o.fluteInst = fl ? flute::profileIndex(fl->str("instrument", "shakuhachi")) : 0; for (int k = 0; k < flute::kNumParams; k++) o.flute[k] = fl ? fl->num(flute::kParams[k].key, flute::kParams[k].def) : flute::kParams[k].def; }
    o.stiffness = j.num("stiffness", 0); o.bore = j.num("bore", 0); o.brightKeyTrack = j.num("brightKeyTrack", 0); o.dampKeyTrack = j.num("dampKeyTrack", 0); o.excite = (int)j.num("excite", 0);
    o.unisonBlend = j.num("unisonBlend", 0); o.unisonPhase = j.num("unisonPhase", 0);
    if (const Json* rj = j.get("rr")) if (rj->isObj()) { RrCfg& r = o.rr; r.on = rj->boolean("on", false); const std::string od = rj->str("order", "seq"), sc = rj->str("scope", "free");
        r.order = od == "random" ? 1 : od == "norep" ? 2 : od == "pingpong" ? 3 : 0; r.scope = sc == "key" ? 1 : sc == "transport" ? 2 : sc == "idle" ? 3 : 0; r.variation = std::max(0.0, std::min(1.0, rj->num("variation", 0.3)));
        if (const Json* tk = rj->get("takes")) if (tk->isArr()) for (const Json& x : tk->a) { const int t = (int)x.asNum(-1); if (t >= 0 && t < 32 && std::find(r.takes.begin(), r.takes.end(), t) == r.takes.end() && r.takes.size() < 31) r.takes.push_back(t); } }
    o.arpOn = j.boolean("arpOn", false); o.bleedOn = j.boolean("bleedOn", false); o.bleed = j.num("bleed", 0.5); o.mic = j.str("mic", "none"); o.spk = j.str("spk", "none"); o.micMix = j.num("micMix", 0); o.spkMix = j.num("spkMix", 0);
    o.name = j.str("name", ""); o.root = (int)j.num("root", 60); o.start = j.num("start", 0); o.end = j.num("end", 1);
    o.loop = j.boolean("loop", true); o.reverse = j.boolean("reverse", false); o.fadeIn = j.num("fadeIn", 0); o.fadeOut = j.num("fadeOut", 0);
    if (auto t = j.get("transients")) for (auto& v : t->a) { o.transients.push_back(v.asNum()); Transient tr; tr.pos = v.num("pos", 0); tr.pan = v.num("pan", 0); tr.reverse = v.boolean("reverse", false); o.trans.push_back(tr); }
    if (auto e = j.get("env")) { o.env = env(e, o.env); o.hasEnv = e->isObj(); }
    if (j.has("loopStartFrac") && j.get("loopStartFrac")->isNum()) o.loopStartFrac = j.num("loopStartFrac", -1);
    if (j.has("loopEndFrac") && j.get("loopEndFrac")->isNum()) o.loopEndFrac = j.num("loopEndFrac", -1);
    if (auto f = j.get("filter")) { o.hasFilter = f->isObj(); o.filter = filter(f, lt == LayerType::Sample ? FilterCfg{0, false, 18000, 20, 0, 1, 0.8} : FilterCfg{0, false, 4000, 20, 0, 1, 0.8}); }
    if (auto f = j.get("filterEnv")) o.filterEnv = fenv(f, FilterEnvCfg{0, 0.01, 0.2, 0.3, 0.3});
    return o;
}
inline LayerCfg layer(const Json& j, LayerType lt) {
    LayerCfg L; L.type = lt;
    int count = lt == LayerType::Sample ? 16 : lt == LayerType::Wavetable ? 2 : lt == LayerType::Procedural ? 2 : lt == LayerType::SID ? 3 : lt == LayerType::Lua ? 4 : 8;
    auto oscs = j.get("oscs");
    for (int i = 0; i < count; i++) {
        OscCfg o;
        if (oscs && (size_t)i < oscs->a.size()) o = osc(oscs->a[i], lt);
        else { Json empty; empty.type = Json::Obj; o = osc(empty, lt); if (lt == LayerType::Wavetable) o.enabled = (i == 0); if (lt == LayerType::FM) { o.ratio = (i % 4 == 0) ? 1 : 2; o.level = (i % 4 == 0) ? 0.9 : 0; } if (lt == LayerType::Sample) o.level = (i == 0) ? 0.8 : 0; }
        if (lt == LayerType::Sample && !(oscs && (size_t)i < oscs->a.size() && oscs->a[i].has("arpOn"))) o.arpOn = (i == 0);   // web default: only sample 1 follows the arp
        L.oscs.push_back(o);
    }
    L.filter = filter(j.get("filter"), FilterCfg{0, false, 18000, 20, 0, 1, 0.8});
    L.filterEnv = fenv(j.get("filterEnv"), FilterEnvCfg{0, 0.01, 0.2, 0.3, 0.3});
    L.env = env(j.get("env"), L.env);
    if (auto e2 = j.get("env2")) if (e2->isObj()) { L.env2 = env(e2, L.env); L.hasEnv2 = true; }
    if (!L.hasEnv2) L.env2 = L.env;
    auto arr2 = [&](const char* k, double* dst) { if (auto a = j.get(k)) for (size_t i = 0; i < 2 && i < a->a.size(); i++) dst[i] = a->a[i].asNum(); };
    arr2("engineOctave", L.engineOctave); arr2("engineSemitone", L.engineSemitone); arr2("engineFine", L.engineFine); arr2("engineGlide", L.engineGlide);
    if (auto a = j.get("fmAlgorithms")) for (size_t i = 0; i < 2 && i < a->a.size(); i++) L.fmAlgorithms[i] = (int)a->a[i].asNum(2);
    if (auto a = j.get("fmSub")) for (size_t i = 0; i < 2 && i < a->a.size(); i++) L.fmSub[i] = (int)a->a[i].asNum();
    if (auto a = j.get("engineEnabled")) for (size_t i = 0; i < 2 && i < a->a.size(); i++) L.engineEnabled[i] = a->a[i].type == Json::Bool ? a->a[i].b : (a->a[i].asNum() != 0);
    L.pan = j.num("pan", 0);
    if (auto a = j.get("enginePan")) for (size_t i = 0; i < 2 && i < a->a.size(); i++) L.enginePan[i] = a->a[i].asNum();
    L.sixOp = j.boolean("sixOp", false); L.sixOpEnabled = j.boolean("sixOpEnabled", false);
    L.dx7Bank = j.str("dx7Bank", "ROM1A"); L.dx7PatchIdx = (int)j.num("dx7PatchIdx", 0);
    L.dx7Transpose = j.num("dx7Transpose", 0); L.dx7OutputTrim = j.num("dx7OutputTrim", 1); L.dx7Pan = j.num("dx7Pan", 0);
    if (L.sixOp) {   // resolve the patch now (control thread): explicit bytes, else the chosen bank slot (ROM or imported custom bank)
        const Json* pj = j.get("dx7Patch");
        if (pj && pj->isArr() && pj->a.size() >= 155) { for (int i = 0; i < dx7::PATCH_SIZE; i++) L.dx7Patch[i] = i < (int)pj->a.size() ? (uint8_t)((int)pj->a[i].asNum() & 0xff) : (i == 155 ? 63 : 0); if (pj->a.size() < 156) L.dx7Patch[155] = 63; L.hasDx7Patch = true; }
        else if (L.dx7Bank.rfind("custom:", 0) == 0) {
            const Json* cb = j.get("dx7CustomBanks"); const int ci = std::atoi(L.dx7Bank.c_str() + 7);
            if (cb && cb->isArr() && ci >= 0 && ci < (int)cb->a.size()) if (auto vv_ = cb->a[ci].get("voices")) if (vv_->isArr() && L.dx7PatchIdx >= 0 && L.dx7PatchIdx < (int)vv_->a.size()) {
                const Json& vj = vv_->a[L.dx7PatchIdx]; for (int i = 0; i < dx7::PATCH_SIZE; i++) L.dx7Patch[i] = i < (int)vj.a.size() ? (uint8_t)((int)vj.a[i].asNum() & 0xff) : (i == 155 ? 63 : 0); L.hasDx7Patch = true; }
        }
        if (!L.hasDx7Patch) {
            const dx7roms::Bank* b = dx7roms::find(L.dx7Bank.c_str()); if (!b) b = dx7roms::find("ROM1A");
            auto r = dx7::parseSysex(b->data, b->size); const int ix = L.dx7PatchIdx >= 0 && L.dx7PatchIdx < (int)r.voices.size() ? L.dx7PatchIdx : 0;
            if (!r.voices.empty()) { std::memcpy(L.dx7Patch, r.voices[ix].data(), dx7::PATCH_SIZE); L.hasDx7Patch = true; }
        }
    }
    { const std::string sa = j.str("slotAdvance", "off"); L.slotAdvance = sa == "next" ? 1 : sa == "random" ? 2 : 0; }
    L.globalAdsrActive = j.boolean("globalAdsrActive", false); L.globalAdsr = env(j.get("globalAdsr"), L.globalAdsr);
    if (auto e = j.get("eq")) for (int b = 0; b < 4; b++) { std::string k = "band" + std::to_string(b + 1); if (auto bj = e->get(k.c_str())) { L.eq[b].freq = bj->num("freq", L.eq[b].freq); L.eq[b].gain = bj->num("gain", L.eq[b].gain); } }
    if (const Json* sp = j.get("sidPerf")) { SidPerfCfg& P = L.sidPerf; P.vibrato = sp->boolean("vibrato", true); P.glide = sp->boolean("glide", false); P.autoGlide = sp->boolean("autoGlide", false); P.tie = sp->boolean("tie", false); P.sustain = sp->boolean("sustain", false); P.listen = sp->boolean("listen", false); P.glideTime = sp->num("glideTime", 0.12); }
    if (const Json* wa = j.get("wtArp")) { L.wtArp.on = wa->boolean("on", false); L.wtArp.speedHz = wa->num("speedHz", 50); L.wtArp.hardRestart = wa->boolean("hardRestart", false);
        if (const Json* st = wa->get("steps")) for (const Json& e : st->a) { WtArpStep s; s.wave = e.str("wave", "triangle"); s.mode = e.str("mode", "relative"); s.value = e.num("value", 0); s.gate = e.has("gate") ? e.boolean("gate", true) : true;
            s.ringMod = e.boolean("ringMod", false); s.sync = e.boolean("sync", false); s.lengthTicks = std::max(1, (int)e.num("lengthTicks", 1)); s.tempoSync = e.boolean("tempoSync", false); s.syncDivision = e.num("syncDivision", 0.25); L.wtArp.steps.push_back(s); } }
    L.syncEnabled = j.boolean("syncEnabled", false);
    L.proCoupling = j.num("proCoupling", 0); L.ringModEnabled = j.boolean("ringModEnabled", false) && !L.syncEnabled;   // the web UI makes the two mutually exclusive
    if (const Json* rm = j.get("ringMod")) { L.rmInterval = rm->str("interval", "p5"); L.rmWave = rm->str("wave", "sine"); L.rmDetuneCents = rm->num("detuneCents", 0); L.rmPitchEnvAmt = rm->num("pitchEnvAmt", 0);
        L.rmPitchEnvDecay = rm->num("pitchEnvDecay", 0.12); L.rmBleed = rm->num("bleed", 0.15); L.rmVelDepth = rm->num("velDepth", 0.3); L.rmLpgCutoff = rm->num("lpgCutoff", 4000); L.rmLpgKeytrack = rm->num("lpgKeytrack", 0.5); }
    if (auto pats = j.get("patterns")) for (auto& pj : pats->a) {
        SeqPattern sp; sp.length = (int)pj.num("length", 16);
        if (auto st = pj.get("steps")) for (auto& sj : st->a) {
            SeqStep s; s.on = sj.boolean("on", false); s.pitch = sj.num("pitch", 0); s.ratchet = std::max(1, (int)sj.num("ratchet", 1)); s.prob = sj.num("prob", 100);
            s.tempoRatio = sj.num("tempoRatio", 1); if (!(s.tempoRatio > 0)) s.tempoRatio = 1; s.transientIdx = (int)sj.num("transientIdx", 0);
            if (auto pl = sj.get("plocks")) { if (pl->has("level")) { s.pl.hasLevel = true; s.pl.level = pl->num("level", 1); } if (pl->has("cutoff")) { s.pl.hasCutoff = true; s.pl.cutoff = pl->num("cutoff", 4000); } if (pl->has("position")) { s.pl.hasPosition = true; s.pl.position = pl->num("position", 0); } }
            sp.steps.push_back(s);
        }
        if (auto rj = pj.get("roll")) { RollData& R = sp.roll; R.present = true; R.bars = std::max(1, (int)rj->num("bars", 4)); R.loopOn = rj->boolean("loopOn", true); R.loopStart = rj->num("loopStart", 0); R.loopEnd = rj->num("loopEnd", R.bars);
            if (auto nj = rj->get("notes")) for (auto& n : nj->a) { RollNote rn; rn.t = n.num("t", 0); rn.l = std::max(0.03, n.num("l", 0.25)); rn.p = (int)n.num("p", 60); rn.v = n.num("v", 0.9); rn.prob = n.num("prob", 100); rn.rat = std::max(1, (int)n.num("rat", 1)); rn.tr = (int)n.num("tr", 0);
                if (auto pl = n.get("pl")) { if (pl->has("level")) { rn.pl.hasLevel = true; rn.pl.level = pl->num("level", 1); } if (pl->has("cutoff")) { rn.pl.hasCutoff = true; rn.pl.cutoff = pl->num("cutoff", 4000); } if (pl->has("position")) { rn.pl.hasPosition = true; rn.pl.position = pl->num("position", 0); } if (pl->has("slot")) rn.slot = (int)pl->num("slot", -1); }
                R.notes.push_back(rn); }
            if (auto lj = rj->get("lanes")) for (int k = 0; k < 2; k++) if (auto aj = lj->get(k ? "mod" : "bend")) for (auto& q : aj->a) if (q.a.size() >= 2) (k ? R.mod : R.bend).push_back({ q.a[0].asNum(), q.a[1].asNum() });
            auto byT = [](const LanePt& a, const LanePt& b) { return a.t < b.t; }; std::stable_sort(R.bend.begin(), R.bend.end(), byT); std::stable_sort(R.mod.begin(), R.mod.end(), byT); }
        L.patterns.push_back(std::move(sp));
    }
    L.currentSlot = (int)j.num("currentSlot", 0);
    if (auto co = j.get("chainOrder")) for (auto& v : co->a) L.chainOrder.push_back((int)v.asNum());
    L.chainMode = j.str("chainMode", "off");
    L.gate = std::min(1.0, std::max(0.02, j.num("gate", 0.7))); L.scThresh = j.num("scThresh", 0.25); L.scAmount = j.num("scAmount", 0.8);
    { const std::string ss = j.str("scSrc", "none"); L.scSrc = ss == "WT" ? L_WT : ss == "FM" ? L_FM : ss == "SMP" ? L_SMP : ss == "PRO" ? L_PRO : ss == "SID" ? L_SID : -1; }
    // Sampler slots without their own filter inherit the old shared layer-level filter (same back-fill the web app does).
    if (lt == LayerType::Sample) for (auto& o : L.oscs) if (!o.hasFilter) { o.filter = L.filter; o.filterEnv = L.filterEnv; o.hasFilter = true; }
    return L;
}
// Which Mixer channels a lane belongs to (web autoChannelsOf): mixer track indices, kNumMix = Master (owns FX / global / source lanes).
inline std::vector<int> autoLaneChannels(const std::string& key) {
    std::vector<int> r; auto add = [&](const char* k) { const int i = mixIndex(k); if (i >= 0) r.push_back(i); };
    auto smp = [&]() { for (int i = 5; i < 21; i++) r.push_back(i); };
    if (key.rfind("S:", 0) == 0) { r.push_back(kNumMix); return r; }
    const size_t c = key.find(':'); const std::string dl = key.substr(0, c), dp = c == std::string::npos ? "" : key.substr(c + 1);
    if (dl == "MIX") { if (dp == "MASTER") r.push_back(kNumMix); else add(dp.c_str()); }
    else if (dl == "GEN") { const std::string g = dp.substr(0, dp.find('.')); if (g == "WT") { add("WT1"); add("WT2"); } else if (g == "WT2") add("WT2"); else if (g == "FM") { add("FMA"); add("FMB"); add("FM6OP"); } else if (g == "FM2") add("FMB"); else if (g == "SMP") smp(); else if (g == "PRO") { add("PRC1"); add("PRC2"); } else if (g == "SID") add("SID"); else r.push_back(kNumMix); }
    else if (dl == "WT") { if (dp.rfind("osc1", 0) == 0) add("WT1"); else if (dp.rfind("osc2", 0) == 0) add("WT2"); else { add("WT1"); add("WT2"); } }
    else if (dl == "FM") { if (dp.size() == 8 && dp.rfind("op", 0) == 0 && dp.substr(3) == "Ratio" && dp[2] >= '1' && dp[2] <= '8') add(dp[2] <= '4' ? "FMA" : "FMB"); else if (dp.rfind("engA", 0) == 0) add("FMA"); else if (dp.rfind("engB", 0) == 0) add("FMB"); else { add("FMA"); add("FMB"); add("FM6OP"); } }
    else if (dl == "FM6OP") add("FM6OP");
    else if (dl == "SMP") { if (dp.size() >= 2 && dp[0] == 's' && dp[1] >= '0' && dp[1] <= '9') { const int n = std::atoi(dp.c_str() + 1); if (n >= 1 && n <= 16) { r.push_back(4 + n); return r; } } smp(); }
    else if (dl == "PRO") { add("PRC1"); add("PRC2"); }
    else if (dl == "SID") add("SID");
    else r.push_back(kNumMix);
    return r;
}
inline bool autoLaneEnabled(const Preset& P, const std::string& key) { for (int i : autoLaneChannels(key)) if (i >= 0 && i <= kNumMix && P.mixAuto[i]) return true; return false; }
}  // namespace modelio

// Parses a snapshotState() document. Missing pieces take the same defaults as the web app.
namespace modelio {
// Applies one XFX override path (relative to the layer, e.g. "oscs.0.filter.cutoff") to a copy of the layer JSON. Pseudo-parameters: oscs.N.slotOn (false silences the slot),
// oscs.N.useSlot (this slot plays the sample of slot K: name + transients + loop points, never the audio data). Bank/patch changes drop the stored 6-OP patch bytes so the parser re-reads the bank.
inline void xfxApply(Json& L, const std::string& rel, const Json& v) {
    const size_t dot = rel.rfind('.'); const std::string leaf = dot == std::string::npos ? rel : rel.substr(dot + 1);
    if (leaf == "slotOn" || leaf == "useSlot") {
        int oi = -1; if (rel.rfind("oscs.", 0) == 0) oi = std::atoi(rel.c_str() + 5);
        Json* oscs = jsonPathPtr(L, "oscs", false); if (!oscs || !oscs->isArr() || oi < 0 || oi >= (int)oscs->a.size()) return;
        if (leaf == "slotOn") { if (v.type == Json::Bool ? !v.b : v.asNum(1) == 0) { Json* lv = jsonPathPtr(L, "oscs." + std::to_string(oi) + ".level", true); if (lv) { lv->type = Json::Num; lv->n = 0; } } }
        else { const int k = std::max(0, std::min((int)oscs->a.size() - 1, (int)v.asNum(oi))); const Json src = oscs->a[(size_t)k]; Json& dst = oscs->a[(size_t)oi];
            for (const char* key : {"name", "transients", "loopStartFrac", "loopEndFrac"}) if (const Json* f = src.get(key)) { Json* d = jsonPathPtr(dst, key, true); if (d) *d = *f; } }
        return;
    }
    Json* t = jsonPathPtr(L, rel, true); if (t) *t = v;
    if (rel == "dx7Bank" || rel == "dx7PatchIdx") { for (size_t i = 0; i < L.o.size(); i++) if (L.o[i].first == "dx7Patch") { L.o.erase(L.o.begin() + (long)i); break; } }
}
// Builds the resolved table for the ACTIVE bank of extra.xfx (same format as the web app: {v, enabled, active, banks:[{name, keys:{note:{en:{WT:bool}, o:{"WT.oscs.0.level":0.3}}}}]}).
inline std::shared_ptr<const XfxTable> buildXfx(const Json& j) {
    const Json* ex = j.get("extra"); const Json* x = ex ? ex->get("xfx") : nullptr; if (!x || !x->isObj() || !x->boolean("enabled", true)) return nullptr;
    const Json* banks = x->get("banks"); if (!banks || !banks->isArr()) return nullptr;
    const int active = std::max(0, std::min((int)banks->a.size() - 1, (int)x->num("active", 0))); const Json* keys = banks->a[(size_t)active].get("keys"); if (!keys || !keys->isObj()) return nullptr;
    static const LayerType types[kNumLayers] = {LayerType::Wavetable, LayerType::FM, LayerType::Sample, LayerType::Procedural, LayerType::SID, LayerType::Lua};
    const Json* layers = j.get("layers"); auto T = std::make_shared<XfxTable>(); T->hold = x->boolean("hold", false); T->bank = active; T->bankName = banks->a[(size_t)active].str("name", "");
    for (const auto& kv : keys->o) {
        const int note = std::atoi(kv.first.c_str()); if (note < 0 || note > 127) continue; const Json& rec = kv.second;
        if (const Json* en = rec.get("en")) for (int l = 0; l < kNumLayers; l++) if (const Json* e = en->get(kLayerKeys[l])) { T->en[note][l] = (e->type == Json::Bool ? e->b : e->asNum() != 0) ? 1 : 0; T->any = true; T->zone[note] = true; }
        const Json* o = rec.get("o"); if (o && o->isObj() && !o->o.empty()) T->zone[note] = true;
        if (!o || !o->isObj()) continue;
        for (int l = 0; l < kNumLayers; l++) {
            const std::string pre = std::string(kLayerKeys[l]) + ".";
            Json copy; bool have = false;
            for (const auto& ov : o->o) {
                if (ov.first.compare(0, pre.size(), pre) != 0) continue;
                if (!have) { const Json* lj = layers ? layers->get(kLayerKeys[l]) : nullptr; if (lj) copy = *lj; else copy.type = Json::Obj; have = true; }
                xfxApply(copy, ov.first.substr(pre.size()), ov.second);
                if (ov.first.size() > 5 && ov.first.compare(ov.first.size() - 5, 5, ".hold") == 0 && ov.second.type == Json::Bool && ov.second.b) T->anyHold = true;
            }
            if (have) { T->cfg.emplace(note * kNumLayers + l, layer(copy, types[l])); T->any = true; }
        }
    }
    return T->any ? std::shared_ptr<const XfxTable>(T) : nullptr;
}
}  // namespace modelio
inline std::shared_ptr<Preset> parsePreset(const Json& j) {
    auto P = std::make_shared<Preset>();
    static const LayerType types[kNumLayers] = {LayerType::Wavetable, LayerType::FM, LayerType::Sample, LayerType::Procedural, LayerType::SID, LayerType::Lua};
    const Json* layers = j.get("layers");
    for (int i = 0; i < kNumLayers; i++) {
        Json empty; empty.type = Json::Obj;
        const Json* lj = layers ? layers->get(kLayerKeys[i]) : nullptr;
        P->layers[i] = modelio::layer(lj ? *lj : empty, types[i]);
    }
    if (auto c = j.get("connections")) for (auto& cj : c->a) { Connection cn; cn.source = cj.str("source"); cn.dest = cj.str("dest"); cn.amount = cj.num("amount", 0); P->connections.push_back(cn); }
    { static const double dh[kNumLfo] = {0.25, 0.5, 2, 6, 1, 3}, dd[kNumLfo] = {0.25, 1, 0.5, 0.125, 0.25, 0.5}; for (int i = 0; i < kNumLfo; i++) { P->lfo[i].hz = dh[i]; P->lfo[i].division = dd[i]; } }   // web-app defaults (LFO 1 = 0.25 Hz)
    for (int i = 0; i < kNumLfo; i++) {
        std::string k = "lfo" + std::to_string(i + 1); const Json* l = j.get("lfo") ? j.get("lfo")->get(k.c_str()) : nullptr; if (!l) continue;
        LfoCfg& c = P->lfo[i]; c.rateMode = l->str("rateMode", c.rateMode); c.hz = l->num("hz", c.hz); c.division = l->num("division", c.division); c.phase = l->num("phase", 0);
        c.mode = l->str("mode", "free"); c.held = l->boolean("held", false); c.waveform = l->str("waveform", "sine"); c.bipolar = l->boolean("bipolar", true);
        c.amount = l->num("amount", 1); c.phaseOffset = l->num("phaseOffset", 0); c.smoothing = l->num("smoothing", 0); c.morph = l->num("morph", 0.5);
        if (auto s = l->get("shape")) if (s->isArr()) {
            for (auto& v : s->a) { c.shape.push_back(v.asNum()); CurvePt pt; pt.x = v.num("x", 0); pt.y = v.num("y", 0); pt.cx = v.num("cx", NAN); pt.cy = v.num("cy", NAN); pt.lin = v.boolean("lin", false); c.curve.push_back(pt); }
            for (size_t k = 0; k + 1 < c.curve.size(); k++) if (std::isnan(c.curve[k].cx)) { c.curve[k].cx = (c.curve[k].x + c.curve[k + 1].x) / 2; c.curve[k].cy = (c.curve[k].y + c.curve[k + 1].y) / 2; }   // ensureCurveControlPoints
        }
    }
    if (auto a = j.get("arp")) { P->arp.on = a->boolean("on", false); P->arp.latch = a->boolean("latch", false); P->arp.order = a->str("order", "UP"); P->arp.rateMode = a->str("rateMode", "sync"); P->arp.division = a->num("division", 0.5); P->arp.hz = a->num("hz", 4); P->arp.octRange = (int)a->num("octRange", 1); P->arp.mode = a->str("mode", "standard"); P->arp.gate = a->num("gate", 0.8); P->arp.swing = a->num("swing", 50); P->arp.velocity = a->num("velocity", 0.9); P->arp.repeat = std::max(1, std::min(4, (int)a->num("repeat", 1))); P->arp.transpose = (int)a->num("transpose", 0); P->arp.target = a->str("target", "ALL"); P->arp.targets.clear();
        if (auto tg = a->get("targets"); tg && tg->isArr()) { for (size_t k = 0; k < tg->size(); k++) if (tg->at(k).isStr()) P->arp.targets.push_back(tg->at(k).s); }
        else if (P->arp.target != "ALL" && !P->arp.target.empty()) P->arp.targets.push_back(P->arp.target);
        auto parseAdv = [](const Json& ad, ArpAdvCfg& A) { A.algo = ad.str("algo", "fractal"); A.algoId = 0; A.scale = ad.str("scale", "major");
            A.seed = ad.num("seed", 1234); A.probability = ad.num("probability", 1); A.density = ad.num("density", 0.6); A.complexity = ad.num("complexity", 0.5); A.mutation = ad.num("mutation", 0.2); A.variation = ad.num("variation", 0); A.randomness = ad.num("randomness", 0);
            A.humanise = ad.num("humanise", 0); A.octSpread = ad.num("octSpread", 0); A.ratchet = ad.num("ratchet", 0); A.velVar = ad.num("velVar", 0); A.accent = ad.num("accent", 0.3); A.gateVar = ad.num("gateVar", 0); A.groove = ad.num("groove", 0.5);
            A.patLen = ad.num("patLen", 16); A.cycleVar = ad.num("cycleVar", 0); A.rotation = ad.num("rotation", 0); A.skipProb = ad.num("skipProb", 0); A.rhythmDisp = ad.num("rhythmDisp", 0); A.mutSalt = ad.num("mutSalt", 0); A.mutAmt = ad.num("mutAmt", 0); A.varSeed = ad.num("varSeed", 0); };
        if (auto ad = a->get("adv")) parseAdv(*ad, P->arp.adv);
        if (auto ab = a->get("ab")) { const std::string cur = ab->str("cur", "a"); P->arp.curIsA = cur != "b"; if (auto o = ab->get(P->arp.curIsA ? "b" : "a"); o && o->isObj()) { parseAdv(*o, P->arp.advOther); P->arp.hasOther = true; } }
        if (auto mo = a->get("morph")) { P->arp.morphOn = mo->boolean("on", false); P->arp.morphAmt = mo->num("amt", 0.5); } }
    if (auto f = j.get("fxStages")) for (auto& fj : f->a) {
        FxStageCfg s; s.id = (int)P->fx.size() + 1; /* the web app numbers stages on load (fxIdCounter) and stores no ids in presets */ s.type = fj.str("type"); s.enabled = fj.boolean("enabled", true); if (auto p = fj.get("params")) s.params = *p; else s.params.type = Json::Obj;
        if (auto sd = fj.get("sends")) { for (int i = 0; i < kNumLayers; i++) s.sends[i] = sd->boolean(kLayerKeys[i], false); s.sends[kXfxSend] = sd->boolean("XFX", false); }
        P->fx.push_back(std::move(s));
    }
    if (auto x = j.get("xmod")) for (size_t i = 0; i < 4 && i < x->a.size(); i++) { auto& xj = x->a[i]; XmodCfg& c = P->xmod[i]; c.on = xj.boolean("on", false); c.src = xj.str("src"); c.dst = xj.str("dst"); c.type = xj.str("type"); c.amount = xj.num("amount", 0); }
    P->swingOn = j.boolean("globalSwingOn", false); P->swingAmount = j.num("globalSwingAmount", 50); P->maxPolyphony = (int)j.num("maxPolyphony", 64); P->legato = j.boolean("legatoMode", false);
    if (auto ex = j.get("extra")) if (auto pb = ex->get("pitchBendTargets")) for (int li = 0; li < kNumLayers; li++) if (auto a = pb->get(kLayerKeys[li]))
        for (size_t i = 0; i < 16 && i < a->a.size(); i++) P->bendTargets[li][i] = a->a[i].type == Json::Bool ? a->a[i].b : a->a[i].asNum(1) != 0;
    if (const Json* sl = j.get("sampleLibrary")) if (sl->isArr()) for (const Json& e : sl->a) { SampleRef r; r.name = e.str("name", ""); r.b64 = e.str("base64", ""); if (!r.name.empty() && !r.b64.empty()) P->samples.push_back(std::move(r)); }
    if (const Json* cw = j.get("customWavetables")) if (cw->isArr()) for (const Json& e : cw->a) {
        CustomWt t; t.id = e.str("id", ""); if (t.id.empty()) continue;
        if (const Json* fr = e.get("frames")) if (fr->isArr()) for (const Json& f : fr->a) { if (!f.isArr() || f.a.empty()) continue; std::vector<float> v(f.a.size()); for (size_t i = 0; i < v.size(); i++) v[i] = (float)f.a[i].asNum(0); t.frames.push_back(std::move(v)); }
        if (!t.frames.empty()) P->customTables.push_back(std::move(t));
    }
    P->bpm = j.num("bpm", 120); P->a4 = j.num("a4Reference", 440); P->oversample = j.str("oversample", "normal");
    if (auto ch = j.get("chaos")) { if (auto l = ch->get("lorenz")) P->chaosRate[0] = l->num("rate", 0.5); if (auto r = ch->get("rossler")) P->chaosRate[1] = r->num("rate", 0.5); }
    if (auto ex = j.get("extra")) {
        auto flags = [&](const char* k, bool* dst) { if (auto o = ex->get(k)) for (int i = 0; i <= kNumMix; i++) dst[i] = o->boolean(i == kNumMix ? "MASTER" : mixKey(i), false); };
        flags("mixerMute", P->mixMute); flags("mixerSolo", P->mixSolo); flags("mixerAuto", P->mixAuto);
        if (auto au = ex->get("automation")) { P->automation.bars = std::max(1, (int)au->num("bars", 4));
            if (auto lj = au->get("lanes")) for (auto& kv : lj->o) { if (!kv.second.boolean("d", false)) continue; AutoLane ln; ln.key = kv.first; ln.isSource = ln.key.rfind("S:", 0) == 0;
                if (auto va = kv.second.get("v")) for (auto& x : va->a) ln.v.push_back((float)x.asNum(0.5)); if (ln.v.empty()) continue;
                ln.on = modelio::autoLaneEnabled(*P, ln.key); P->automation.lanes.push_back(std::move(ln)); } }
    }
    if (auto mg = j.get("mixerGains")) for (int i = 0; i < kNumMix; i++) P->mixerGainDb[i] = std::min(12.0, std::max(-60.0, mg->num(mixKey(i), 0)));
    const Json* drumJ = j.get("drumSeq"); if (!drumJ) if (const Json* ex_ = j.get("extra")) drumJ = ex_->get("drumSeq");   /* web presets carry it in extra.drumSeq */
    if (auto d = drumJ) { P->drumOn = d->boolean("on", false); P->drumXfx = d->str("target", "smp") == "xfx"; P->drumBase = std::max(0, std::min(112, (int)d->num("xfxBase", 36)));
        if (auto ln = d->get("lanes")) for (size_t i = 0; i < 16 && i < ln->a.size(); i++) { const Json& l = ln->a[i]; P->drumLen[i] = std::max(1, std::min(32, (int)l.num("length", 16)));
            if (auto st = l.get("steps")) for (size_t k = 0; k < 32 && k < st->a.size(); k++) P->drumSteps[i][k] = st->a[k].type == Json::Bool ? st->a[k].b : st->a[k].asNum(0) != 0; } }
    if (auto m = j.get("macros")) for (size_t i = 0; i < 4 && i < m->a.size(); i++) P->macros[i] = m->a[i].asNum();
    if (auto e = j.get("envFollow")) P->envFollow = *e; if (auto e = j.get("noteRepeat")) P->noteRepeat = *e; if (auto e = j.get("vocoder")) P->vocoder = *e;
    P->chord = modelio::chordCfg(j.get("chordMaker"));
    if (const Json* ex_ = j.get("extra")) if (const Json* xs = ex_->get("xfxSeq")) if (xs->isObj()) { try { P->xfxSeq = modelio::layer(*xs, LayerType::Wavetable); P->hasXfxSeq = true; } catch (...) { P->hasXfxSeq = false; } }
    try { P->xfx = modelio::buildXfx(j); } catch (...) { P->xfx.reset(); }   // a malformed XFX block never prevents the preset from loading
    return P;
}
// Accepts either a bare preset or the web app's single-preset file {vectaVortexPreset:1, name, category, data:{...}}.
inline std::shared_ptr<Preset> parsePresetText(const std::string& text) {
    Json j = JsonParser::parse(text);
    if (j.isObj() && j.get("vectaVortexPreset")) if (const Json* d = j.get("data")) if (d->isObj()) return parsePreset(*d);
    return parsePreset(j);
}
}  // namespace vv
