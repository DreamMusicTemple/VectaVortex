// Modulation matrix vocabulary: sources, destinations and the web app's per-destination scale factors (MATRIX_DESTS / scheduler tick).
#pragma once
#include <string>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include "model.hpp"

namespace vv {

enum ModSrc { S_NONE = -1, S_LFO1 = 0, S_LFO2, S_LFO3, S_LFO4, S_LFO5, S_LFO6, S_VELOCITY, S_MODWHEEL, S_RANDOM, S_ENV_WT, S_ENV_FM, S_ENV_SMP, S_ENV_FOLLOW,
              S_MACRO1, S_MACRO2, S_MACRO3, S_MACRO4, S_MPE_TIMBRE, S_MPE_PRESSURE, S_WT1POS, S_WT2POS, S_WT1SHAPE, S_WT2SHAPE, S_NOTE,
              S_XY1X, S_XY1Y, S_XY2X, S_XY2Y, S_XY3X, S_XY3Y, S_XY4X, S_XY4Y, S_LORENZ, S_ROSSLER };

inline int parseModSource(const std::string& s) {
    static const char* n[] = {"lfo1", "lfo2", "lfo3", "lfo4", "lfo5", "lfo6", "velocity", "modwheel", "random", "envWT", "envFM", "envSMP", "envFollow",
                              "macro1", "macro2", "macro3", "macro4", "mpeTimbre", "mpePressure", "wtOsc1Pos", "wtOsc2Pos", "wtOsc1Shape", "wtOsc2Shape", "note",
                              "xy1x", "xy1y", "xy2x", "xy2y", "xy3x", "xy3y", "xy4x", "xy4y", "lorenz", "rossler"};
    for (int i = 0; i < 34; i++) if (s == n[i]) return i;
    return S_NONE;
}


// ---- generic destinations (web: FXP_DEFS / GEN_DEFS). FXP:<type>:<key> drives one effect parameter on every stage of that type; GEN:<LAYER>.<name> drives
// one per-oscillator / envelope parameter across a whole layer. Offsets are applied around the preset's own value: exp ? base * 2^(off*3) : base + off * range * (min < 0 ? 0.5 : 1).
struct FxpDef { const char* type; const char* key; double mn, mx, step; bool exp; };
static const FxpDef kFxpDefs[] = {
    {"reverb", "size", 0.1, 2, 0.01, false}, {"reverb", "decay", 0, 0.97, 0.01, false}, {"reverb", "predelay", 0, 0.3, 0.005, false}, {"reverb", "mod", 0, 1, 0.01, false}, {"reverb", "colour", 0, 1, 0.01, false}, {"reverb", "rvTail", 0, 1, 0.01, false}, {"reverb", "rvTrans", 0, 1, 0.01, false},
    {"delay", "time", 0.01, 1.5, 0.01, true}, {"saturation", "tone", 300, 14000, 10, true}, {"spectral", "freeze", 0, 1, 1, false},
    {"granular", "spread", 0, 1, 0.01, false}, {"granular", "followerAmount", 0, 1, 0.01, false}, {"granular", "mix", 0, 1, 0.01, false},
    {"granular", "delay", 0, 1500, 1, true}, {"granular", "posRand", 0, 8, 0.01, false}, {"granular", "sizeRand", 0, 1, 0.01, false}, {"granular", "jitter", 0, 1, 0.01, false}, {"granular", "fine", -100, 100, 1, false}, {"granular", "pitchRand", 0, 12, 0.1, false}, {"granular", "reverse", 0, 1, 0.01, false}, {"granular", "volRand", 0, 1, 0.01, false}, {"granular", "width", 0, 2, 0.01, false}, {"granular", "drift", 0, 1, 0.01, false}, {"granular", "lrSpread", 0, 80, 0.1, false}, {"granular", "outLevel", -24, 12, 0.1, false}, {"granular", "curve", 0, 1, 0.01, false}, {"granular", "symmetry", 0, 1, 0.01, false}, {"granular", "atk", 0, 0.5, 0.01, false}, {"granular", "dec", 0, 0.5, 0.01, false}, {"granular", "sus", 0, 1, 0.01, false}, {"granular", "rel", 0, 0.5, 0.01, false}, {"granular", "fb", 0, 0.95, 0.01, false}, {"granular", "fbDamp", 0, 1, 0.01, false}, {"granular", "fbLow", 0, 1, 0.01, false}, {"granular", "fbSat", 0, 1, 0.01, false}, {"granular", "fbDiff", 0, 1, 0.01, false}, {"granular", "pingPong", 0, 1, 0.01, false}, {"granular", "rndDepth", 0, 1, 0.01, false}, {"granular", "rndRate", 0.05, 20, 0.01, true}, {"granular", "seed", 0, 999, 1, false},
    {"tape", "wowRate", 0.05, 5, 0.01, true}, {"tape", "wowDepth", 0, 1, 0.01, false}, {"tape", "flutterRate", 2, 20, 0.1, true}, {"tape", "flutterDepth", 0, 1, 0.01, false},
    {"tape", "dust", 0, 1, 0.01, false}, {"tape", "crackle", 0, 1, 0.01, false}, {"tape", "mix", 0, 1, 0.01, false},
    {"phaser", "depth", 0, 1, 0.01, false}, {"phaser", "stages", 2, 12, 2, false}, {"flanger", "depth", 0, 1, 0.01, false},
    {"glitch", "srReduction", 1, 50, 1, true}, {"glitch", "stutterAmount", 0, 1, 0.01, false}, {"glitch", "mix", 0, 1, 0.01, false},
    {"chorus", "mix", 0, 1, 0.01, false},
    {"echo", "time", 0.02, 4, 0.005, true}, {"echo", "count", 1, 16, 1, false}, {"echo", "density", 0, 1, 0.01, false}, {"echo", "early", 0, 1, 0.01, false}, {"echo", "late", 0, 1, 0.01, false},
    {"echo", "feedback", 0, 0.95, 0.01, false}, {"echo", "decay", 0, 1, 0.01, false}, {"echo", "diffusion", 0, 1, 0.01, false}, {"echo", "hfDamp", 0, 1, 0.01, false}, {"echo", "lfDamp", 0, 1, 0.01, false},
    {"echo", "width", 0, 1.5, 0.01, false}, {"echo", "spread", 0, 1, 0.01, false}, {"echo", "modDepth", 0, 1, 0.01, false}, {"echo", "modRate", 0.05, 8, 0.01, true}, {"echo", "movement", 0, 1, 0.01, false},
    {"echo", "mix", 0, 1, 0.01, false}, {"echo", "level", -24, 12, 0.1, false}, {"spread", "spread", 0, 2, 0.01, false}, {"spread", "mix", 0, 1, 0.01, false},
    {"panning", "width", 0, 2, 0.01, false}, {"panning", "rate", 0.02, 10, 0.01, true}, {"panning", "mix", 0, 1, 0.01, false}, {"panning", "distance", 0, 1, 0.01, false},
    {"filter", "mix", 0, 1, 0.01, false},
    // Interstellar (every continuous dial)
    {"interstellar", "tonal", 0, 1, 0.01, false}, {"interstellar", "band1", -48, 12, 0.5, false}, {"interstellar", "band2", -48, 12, 0.5, false}, {"interstellar", "band3", -48, 12, 0.5, false},
    {"interstellar", "band4", -48, 12, 0.5, false}, {"interstellar", "band5", -48, 12, 0.5, false}, {"interstellar", "band6", -48, 12, 0.5, false},
    {"interstellar", "span", 1, 16, 0.1, true}, {"interstellar", "segments", 1, 16, 1, false}, {"interstellar", "segTime", 0.25, 16, 0.01, true}, {"interstellar", "stretch", 1, 50, 0.1, true}, {"interstellar", "bloom", 0, 1, 0.01, false},
    {"interstellar", "density", 0, 1, 0.01, false}, {"interstellar", "life", 0.5, 20, 0.1, true}, {"interstellar", "pitchUp", 0, 24, 0.1, false}, {"interstellar", "upProb", 0, 1, 0.01, false},
    {"interstellar", "pitchDown", 0, 24, 0.1, false}, {"interstellar", "downProb", 0, 1, 0.01, false}, {"interstellar", "body", 0, 1, 0.01, false}, {"interstellar", "particles", 0, 1, 0.01, false}, {"interstellar", "spread", 0, 1, 0.01, false},
    {"interstellar", "resonance", 0, 1, 0.01, false}, {"interstellar", "ring", 0.2, 12, 0.01, true}, {"interstellar", "strum", 0, 1, 0.01, false}, {"interstellar", "evolve", 0, 1, 0.01, false}, {"interstellar", "drift", 0, 1, 0.01, false},
    {"interstellar", "space", 0, 1, 0.01, false}, {"interstellar", "size", 0, 1, 0.01, false}, {"interstellar", "decay", 0, 1, 0.01, false}, {"interstellar", "predelay", 0, 0.35, 0.005, false}, {"interstellar", "damp", 0, 1, 0.01, false},
    {"interstellar", "revMod", 0, 1, 0.01, false}, {"interstellar", "width", 0, 1, 0.01, false}, {"interstellar", "shimmer", 0, 1, 0.01, false}, {"interstellar", "character", 0, 1, 0.01, false},
    {"interstellar", "mix", 0, 1, 0.01, false}, {"interstellar", "level", -36, 12, 0.1, false}, {"interstellar", "freeze", 0, 1, 1, false}, {"interstellar", "revFreeze", 0, 1, 1, false},
    // EQ rack slot and IR convolution reverb (web: FXP_DEFS eq / ir)
    {"eq", "f1", 20, 18000, 1, true}, {"eq", "g1", -15, 15, 0.1, false}, {"eq", "f2", 20, 18000, 1, true}, {"eq", "g2", -15, 15, 0.1, false}, {"eq", "f3", 20, 18000, 1, true}, {"eq", "g3", -15, 15, 0.1, false},
    {"eq", "f4", 20, 18000, 1, true}, {"eq", "g4", -15, 15, 0.1, false}, {"eq", "out", -24, 24, 0.1, false}, {"eq", "mix", 0, 1, 0.01, false},
    {"ir", "mix", 0, 1, 0.01, false}, {"ir", "predelay", 0, 500, 1, false}, {"ir", "lenPct", 1, 100, 1, false}, {"ir", "earlyLate", -1, 1, 0.01, false}, {"ir", "inLevel", -24, 12, 0.1, false}, {"ir", "outLevel", -24, 12, 0.1, false},
    {"ir", "lowCut", 20, 2000, 1, true}, {"ir", "highCut", 1000, 20000, 10, true}, {"ir", "damping", 0, 1, 0.01, false}, {"ir", "tone", -1, 1, 0.01, false}, {"ir", "width", 0, 2, 0.01, false}, {"ir", "balance", -1, 1, 0.01, false},
    {"ir", "start", 0, 0.98, 0.001, false}, {"ir", "end", 0.02, 1, 0.001, false}, {"ir", "irGain", -24, 24, 0.1, false},
    // Rotary Speaker and Amp + Cab (appended last so earlier destination ids do not move)
    {"rotary", "drive", 0, 1, 0.01, false}, {"rotary", "balance", 0, 1, 0.01, false}, {"rotary", "depth", 0, 1, 0.01, false}, {"rotary", "width", 0, 1, 0.01, false}, {"rotary", "accel", 0.2, 4, 0.01, true}, {"rotary", "mix", 0, 1, 0.01, false}, {"rotary", "level", -24, 12, 0.1, false},
    {"amp", "gain", 0, 1, 0.01, false}, {"amp", "bass", 0, 1, 0.01, false}, {"amp", "mid", 0, 1, 0.01, false}, {"amp", "treble", 0, 1, 0.01, false}, {"amp", "presence", 0, 1, 0.01, false}, {"amp", "master", 0, 1, 0.01, false}, {"amp", "chorus", 0, 1, 0.01, false}, {"amp", "mix", 0, 1, 0.01, false}, {"amp", "level", -24, 12, 0.1, false},
};
constexpr int kNumFxp = (int)(sizeof(kFxpDefs) / sizeof(kFxpDefs[0]));
enum GenField { GF_PAN = 0, GF_ATTACK, GF_DECAY, GF_SUSTAIN, GF_RELEASE, GF_WIDTH, GF_DETUNE, GF_DRIFT, GF_LEVEL, GF_OCT, GF_SEMI, GF_FINE, GF_MICMIX, GF_SPKMIX, GF_UBLEND, GF_UPHASE, GF_VIB, GF_EQFREQ, GF_EQGAIN, GF_FLUTE };
// sub = 1: the layer's SECOND envelope group (web WT2 / FM2 = OSC2 / Engine B); band = EQ band index for GF_EQFREQ / GF_EQGAIN
struct GenDef { int layer; const char* name; int field; double mn, mx, step; bool exp; int sub = 0, band = 0; };
static const GenDef kGenDefs[] = {
    {0, "pan", GF_PAN, -1, 1, 0.01, false}, {0, "attack", GF_ATTACK, 0.001, 3, 0.001, true}, {0, "decay", GF_DECAY, 0.001, 3, 0.001, true}, {0, "sustain", GF_SUSTAIN, 0, 1, 0.001, false}, {0, "release", GF_RELEASE, 0.001, 3, 0.001, true},
    {1, "pan", GF_PAN, -1, 1, 0.01, false}, {1, "attack", GF_ATTACK, 0.001, 3, 0.001, true}, {1, "decay", GF_DECAY, 0.001, 3, 0.001, true}, {1, "sustain", GF_SUSTAIN, 0, 1, 0.001, false}, {1, "release", GF_RELEASE, 0.001, 3, 0.001, true},
    {2, "pan", GF_PAN, -1, 1, 0.01, false}, {2, "attack", GF_ATTACK, 0.001, 3, 0.001, true}, {2, "decay", GF_DECAY, 0.001, 3, 0.001, true}, {2, "sustain", GF_SUSTAIN, 0, 1, 0.001, false}, {2, "release", GF_RELEASE, 0.001, 3, 0.001, true},
    {3, "pan", GF_PAN, -1, 1, 0.01, false}, {3, "attack", GF_ATTACK, 0.001, 3, 0.001, true}, {3, "decay", GF_DECAY, 0.001, 3, 0.001, true}, {3, "sustain", GF_SUSTAIN, 0, 1, 0.001, false}, {3, "release", GF_RELEASE, 0.001, 3, 0.001, true},
    {0, "width", GF_WIDTH, 0, 1, 0.01, false}, {0, "detune", GF_DETUNE, 0, 50, 0.5, false}, {0, "drift", GF_DRIFT, 0, 1, 0.01, false},
    {3, "drift", GF_DRIFT, 0, 1, 0.01, false}, {3, "level", GF_LEVEL, 0, 1, 0.01, false},
    {2, "micMix", GF_MICMIX, 0, 1, 0.01, false}, {2, "spkMix", GF_SPKMIX, 0, 1, 0.01, false},
    {2, "octave", GF_OCT, -4, 4, 1, false}, {2, "semi", GF_SEMI, -12, 12, 1, false}, {2, "fine", GF_FINE, -100, 100, 1, false},
    {3, "octave", GF_OCT, -4, 4, 1, false}, {3, "semi", GF_SEMI, -12, 12, 1, false}, {3, "fine", GF_FINE, -100, 100, 1, false},
    // SID voices (layer 4): each of the 3 voices carries its own pan / ADSR / tuning
    {4, "pan", GF_PAN, -1, 1, 0.01, false}, {4, "attack", GF_ATTACK, 0.001, 3, 0.001, true}, {4, "decay", GF_DECAY, 0.001, 3, 0.001, true}, {4, "sustain", GF_SUSTAIN, 0, 1, 0.001, false}, {4, "release", GF_RELEASE, 0.001, 3, 0.001, true},
    {4, "octave", GF_OCT, -4, 4, 1, false}, {4, "semi", GF_SEMI, -12, 12, 1, false}, {4, "fine", GF_FINE, -100, 100, 1, false}, {4, "vibrato", GF_VIB, 0, 100, 1, false},
    // OSC2 / Engine B own amp envelope (web WT2 / FM2)
    {0, "attack", GF_ATTACK, 0.001, 3, 0.001, true, 1}, {0, "decay", GF_DECAY, 0.001, 3, 0.001, true, 1}, {0, "sustain", GF_SUSTAIN, 0, 1, 0.001, false, 1}, {0, "release", GF_RELEASE, 0.001, 3, 0.001, true, 1},
    {1, "attack", GF_ATTACK, 0.001, 3, 0.001, true, 1}, {1, "decay", GF_DECAY, 0.001, 3, 0.001, true, 1}, {1, "sustain", GF_SUSTAIN, 0, 1, 0.001, false, 1}, {1, "release", GF_RELEASE, 0.001, 3, 0.001, true, 1},
    {0, "unisonBlend", GF_UBLEND, 0, 1, 0.01, false}, {0, "unisonPhase", GF_UPHASE, 0, 1, 0.01, false},
    // wooden-flute / physical-breath controls on the PRO layer (band = index into flute::kParams): GEN:PRO.fl_<key>
    {3, "fl_pressure", GF_FLUTE, 0, 1, 0.001, false, 0, 0}, {3, "fl_velocity", GF_FLUTE, 0, 1, 0.001, false, 0, 1}, {3, "fl_intensity", GF_FLUTE, 0, 1, 0.001, false, 0, 2}, {3, "fl_noise", GF_FLUTE, 0, 1, 0.001, false, 0, 3}, {3, "fl_turbulence", GF_FLUTE, 0, 1, 0.001, false, 0, 4}, {3, "fl_colour", GF_FLUTE, 0, 1, 0.001, false, 0, 5}, {3, "fl_irregularity", GF_FLUTE, 0, 1, 0.001, false, 0, 6}, {3, "fl_stability", GF_FLUTE, 0, 1, 0.001, false, 0, 7}, {3, "fl_attack", GF_FLUTE, 0, 1, 0.001, false, 0, 8}, {3, "fl_release", GF_FLUTE, 0, 1, 0.001, false, 0, 9}, {3, "fl_continuity", GF_FLUTE, 0, 1, 0.001, false, 0, 10}, {3, "fl_aperture", GF_FLUTE, 0, 1, 0.001, false, 0, 11}, {3, "fl_tension", GF_FLUTE, 0, 1, 0.001, false, 0, 12}, {3, "fl_angle", GF_FLUTE, 0, 1, 0.001, false, 0, 13}, {3, "fl_width", GF_FLUTE, 0, 1, 0.001, false, 0, 14}, {3, "fl_thickness", GF_FLUTE, 0, 1, 0.001, false, 0, 15}, {3, "fl_position", GF_FLUTE, 0, 1, 0.001, false, 0, 16}, {3, "fl_coverage", GF_FLUTE, 0, 1, 0.001, false, 0, 17}, {3, "fl_edge", GF_FLUTE, 0, 1, 0.001, false, 0, 18}, {3, "fl_boreLength", GF_FLUTE, 0, 1, 0.001, false, 0, 19}, {3, "fl_boreDiameter", GF_FLUTE, 0, 1, 0.001, false, 0, 20}, {3, "fl_taper", GF_FLUTE, 0, 1, 0.001, false, 0, 21}, {3, "fl_flare", GF_FLUTE, 0, 1, 0.001, false, 0, 22}, {3, "fl_resonance", GF_FLUTE, 0, 1, 0.001, false, 0, 23}, {3, "fl_damping", GF_FLUTE, 0, 1, 0.001, false, 0, 24}, {3, "fl_boreLoss", GF_FLUTE, 0, 1, 0.001, false, 0, 25}, {3, "fl_airColumn", GF_FLUTE, 0, 1, 0.001, false, 0, 26}, {3, "fl_endReflection", GF_FLUTE, 0, 1, 0.001, false, 0, 27}, {3, "fl_radiation", GF_FLUTE, 0, 1, 0.001, false, 0, 28}, {3, "fl_holeCover", GF_FLUTE, 0, 1, 0.001, false, 0, 29}, {3, "fl_meri", GF_FLUTE, 0, 1, 0.001, false, 0, 30}, {3, "fl_kari", GF_FLUTE, 0, 1, 0.001, false, 0, 31}, {3, "fl_muraIki", GF_FLUTE, 0, 1, 0.001, false, 0, 32}, {3, "fl_koroKoro", GF_FLUTE, 0, 1, 0.001, false, 0, 33}, {3, "fl_tamane", GF_FLUTE, 0, 1, 0.001, false, 0, 34}, {3, "fl_vibrato", GF_FLUTE, 0, 1, 0.001, false, 0, 35}, {3, "fl_vibRate", GF_FLUTE, 0, 1, 0.001, false, 0, 36}, {3, "fl_pitchInstability", GF_FLUTE, 0, 1, 0.001, false, 0, 37}, {3, "fl_flutter", GF_FLUTE, 0, 1, 0.001, false, 0, 38}, {3, "fl_dynInstability", GF_FLUTE, 0, 1, 0.001, false, 0, 39}, {3, "fl_articulation", GF_FLUTE, 0, 1, 0.001, false, 0, 40}, {3, "fl_microtonal", GF_FLUTE, -1, 1, 0.001, false, 0, 41}, {3, "fl_natural", GF_FLUTE, 0, 1, 0.001, false, 0, 42},
    // each layer's own 4-band EQ (WT, FM, SMP, PRO)
#define VV_EQ(L) {L, "eqBand1Freq", GF_EQFREQ, 40, 16000, 10, true, 0, 0}, {L, "eqBand1Gain", GF_EQGAIN, -15, 15, 0.5, false, 0, 0}, {L, "eqBand2Freq", GF_EQFREQ, 40, 16000, 10, true, 0, 1}, {L, "eqBand2Gain", GF_EQGAIN, -15, 15, 0.5, false, 0, 1}, \
                 {L, "eqBand3Freq", GF_EQFREQ, 40, 16000, 10, true, 0, 2}, {L, "eqBand3Gain", GF_EQGAIN, -15, 15, 0.5, false, 0, 2}, {L, "eqBand4Freq", GF_EQFREQ, 40, 16000, 10, true, 0, 3}, {L, "eqBand4Gain", GF_EQGAIN, -15, 15, 0.5, false, 0, 3}
    VV_EQ(0), VV_EQ(1), VV_EQ(2), VV_EQ(3), VV_EQ(4), VV_EQ(5),   // SID and LUA layers added in round 25 (appended so earlier ids do not move)
#undef VV_EQ
};
constexpr int kNumGen = (int)(sizeof(kGenDefs) / sizeof(kGenDefs[0]));
// Vocoder matrix destinations (web GEN:VOC.voc_<key>): only the live-adjustable controls; indices follow this table.
struct VocDef { const char* key; double mn, mx, step; bool exp, dflt_is_set; double dflt; };
static const VocDef kVocDefs[] = {
    {"mix", 0, 1, 0.01, false, true, 0.8}, {"formantSt", -12, 12, 0.1, false, true, 0}, {"q", 0.5, 20, 0.1, true, true, 4}, {"releaseMs", 2, 500, 1, true, true, 18},
    {"hpf", 80, 150, 1, false, true, 100}, {"micGainDb", -24, 36, 0.5, false, true, 0}, {"noiseDb", -60, 0, 0.5, false, true, -18}, {"sibSens", 0, 1, 0.01, false, true, 0.5},
    {"pitchSpeed", 0, 400, 1, false, true, 20}, {"chorusRate", 0.1, 5, 0.01, true, true, 0.8},
};
constexpr int kNumVoc = (int)(sizeof(kVocDefs) / sizeof(kVocDefs[0]));

// Destinations that are accumulated once per tick (global), then applied to oscillators / FX / master.
enum ModDest {
    D_NONE = 0,
    D_MACRO1, D_MACRO2, D_MACRO3, D_MACRO4, D_WTFM, D_TONE, D_SMPMIX,
    D_WT_O1OCT, D_WT_O2OCT, D_WT_O1SEMI, D_WT_O2SEMI, D_WT_O1FINE, D_WT_O2FINE,
    D_FM_AOCT, D_FM_BOCT, D_FM_ASEMI, D_FM_BSEMI, D_FM_AFINE, D_FM_BFINE,
    D_XMOD1, D_XMOD2, D_XMOD3, D_XMOD4,
    D_FM_OPR1, D_FM_OPR2, D_FM_OPR3, D_FM_OPR4, D_FM_OPR5, D_FM_OPR6, D_FM_OPR7, D_FM_OPR8,
    D_FM_FB_A, D_FM_FB_B,
    D_LFO_RATE1, D_LFO_RATE2, D_LFO_RATE3, D_LFO_RATE4, D_LFO_RATE5, D_LFO_RATE6, D_LFO_AMT1, D_LFO_AMT2, D_LFO_AMT3, D_LFO_AMT4, D_LFO_AMT5, D_LFO_AMT6, D_LFO_PH1, D_LFO_PH2, D_LFO_PH3, D_LFO_PH4, D_LFO_PH5, D_LFO_PH6,
    D_LFO_SM1, D_LFO_SM2, D_LFO_SM3, D_LFO_SM4, D_LFO_SM5, D_LFO_SM6, D_LFO_MORPH1, D_LFO_MORPH2, D_LFO_MORPH3, D_LFO_MORPH4, D_LFO_MORPH5, D_LFO_MORPH6,
    D_CHAOS_LORENZ_RATE, D_CHAOS_ROSSLER_RATE,
    D_DX_FINE, D_DX_DETUNE, D_DX_LEVEL, D_DX_AMD, D_DX_R2, D_DX_R3, D_DX_R4, D_DX_L1, D_DX_L2,   // FM 6-OP (DX7): offset applied to every operator's patch byte
    D_MIX0, D_MIX_END = D_MIX0 + kNumMix - 1,
    D_WT_START, D_WT_SCAN, D_WT_PWM, D_WT_PHASE, D_WT_STARTEND,
    D_WT_O1SCAN, D_WT_O2SCAN, D_WT_O1PWM, D_WT_O2PWM, D_WT_O1PHASE, D_WT_O2PHASE, D_WT_O1SE, D_WT_O2SE, D_WT_O1SEG, D_WT_O2SEG, D_WT_O1LVL, D_WT_O2LVL, D_WT_O1PWMHZ, D_WT_O2PWMHZ,
    D_SMP_START, D_SMP_END,
    D_FX_REVMIX, D_FX_SATMIX, D_FX_SATDRIVE, D_FX_FLGRATE, D_FX_FOLDDRIVE, D_FX_FOLDMIX, D_FX_FOLDSYM, D_FX_FOLDPWM,
    D_FX_SPECBLUR, D_FX_SPECSHIFT, D_FX_SPECTHR, D_FX_SPECSPREAD, D_FX_SPECMIX, D_FX_FILTCUT, D_FX_FILTRESO,
    D_FX_GRSIZE, D_FX_GRPITCH, D_FX_GRPAN, D_FX_GRDENS, D_FX_DELFB, D_FX_DELMIX, D_FX_STUTDIV, D_FX_BITDEPTH,
    D_FX_FLGMIX, D_FX_FLGFB, D_FX_PHMIX, D_FX_PHRATE, D_FX_PHFB, D_FX_RESFREQ, D_FX_RESRES, D_FX_RESMIX,
    D_ARP_RATE,
    D_FXP0, D_FXP_END = D_FXP0 + kNumFxp - 1, D_GEN0, D_GEN_END = D_GEN0 + kNumGen - 1, D_VOC0, D_VOC_END = D_VOC0 + kNumVoc - 1,   // generic FX-parameter and layer-parameter destinations (global, accumulated per tick)
    // per-voice (only routes whose layer matches the voice's layer apply)
    D_V_CUTOFF, D_V_RESO, D_V_LEVEL, D_V_PITCH, D_V_FVOL, D_V_GEN, D_V_BLEED, D_V_PWM,
    D_COUNT
};

struct ModDestInfo { const char* name; int dest; double scale; };

// "LAYER:param" -> (dest, scale). Scale factors are exactly the ones in the web scheduler's accumulation step.
inline bool parseModDest(const std::string& str, int& layer, int& dest, int& g, double& scale) {
    layer = -1; dest = D_NONE; g = 0; scale = 1;
    const size_t c = str.find(':'); if (c == std::string::npos) return false;
    const std::string L = str.substr(0, c), P = str.substr(c + 1);
    static const char* lk[] = {"WT", "FM", "SMP", "PRO", "SID", "LUA"};
    for (int i = 0; i < 6; i++) if (L == lk[i]) layer = i;
    // ---- per-voice destinations (any of the four layers)
    if (layer >= 0) {
        auto filt = [&](const std::string& p, const char* suf) -> int {   // -> group index or -1
            if (p == "cutoff" || p == "reso") return 0;
            if (layer == 2 && p.size() >= 7 && p[0] == 's' && p.compare(p.size() - std::strlen(suf), std::string::npos, suf) == 0) {   // sampler slot filters: s1Cutoff .. s16Reso
                const std::string num = p.substr(1, p.size() - 1 - std::strlen(suf)); if (!num.empty() && num.find_first_not_of("0123456789") == std::string::npos) { const int n = std::atoi(num.c_str()); if (n >= 1 && n <= 16) return n - 1; }
            }
            static const char* pre[] = {"osc1", "osc2", "engA", "engB"};
            for (int i = 0; i < 4; i++) if (p == std::string(pre[i]) + suf) return (i == 0 || i == 2) ? 0 : 1;
            return -1;
        };
        if (P == "cutoff" || filt(P, "Cutoff") >= 0) { if (P != "reso") { int gi = filt(P, "Cutoff"); if (gi >= 0) { dest = D_V_CUTOFF; g = gi; scale = 4000; return true; } } }
        if (P == "reso" || filt(P, "Reso") >= 0) { int gi = filt(P, "Reso"); if (gi >= 0) { dest = D_V_RESO; g = gi; scale = 30; return true; } }
        if (layer == 4 && (P == "voice1Pwm" || P == "voice2Pwm" || P == "voice3Pwm")) { dest = D_V_PWM; g = P[5] - '1'; scale = 1; return true; }   // SID live pulse-width modulation
        if (layer == 2 && P.size() >= 7 && P[0] == 's' && P.compare(P.size() - 5, 5, "Bleed") == 0) {
            const std::string num = P.substr(1, P.size() - 6);
            if (!num.empty() && num.find_first_not_of("0123456789") == std::string::npos) { const int n = std::atoi(num.c_str()); if (n >= 1 && n <= 16) { dest = D_V_BLEED; g = n - 1; scale = 1; return true; } }
        }
        if (P == "level") { dest = D_V_LEVEL; scale = 0.5; return true; }
        if (P == "pitch") { dest = D_V_PITCH; scale = 200; return true; }
        if (P == "filterVolume") { dest = D_V_FVOL; return true; }
        if (P.size() >= 7 && P.compare(0, 3, "gen") == 0 && (P[3] == 'A' || P[3] == 'B')) {
            static const char* k[] = {"Tone", "Damp", "Body", "Chaos"};
            for (int i = 0; i < 4; i++) if (P.compare(4, std::string::npos, k[i]) == 0) { dest = D_V_GEN; g = (P[3] == 'A' ? 0 : 4) + i; return true; }
        }
    }
    if (L == "MIX") { const int mi = mixIndex(P); if (mi >= 0) { dest = D_MIX0 + mi; scale = 24; return true; } return false; }   // global: +-24 dB swing at full amount
    // generic destinations: "FXP:<type>:<key>" and "GEN:<LAYER>.<name>" (layer = -1, applied through the generic tables)
    if (L == "FXP") { const size_t c2 = P.find(':'); if (c2 != std::string::npos) { const std::string ty = P.substr(0, c2), ky = P.substr(c2 + 1); for (int i = 0; i < kNumFxp; i++) if (ty == kFxpDefs[i].type && ky == kFxpDefs[i].key) { dest = D_FXP0 + i; return true; } } return false; }
    if (L == "GEN") { const size_t d2 = P.find('.'); if (d2 != std::string::npos) { const std::string ly = P.substr(0, d2), nm = P.substr(d2 + 1); int li = -1, sub = 0; if (ly == "VOC") { if (nm.rfind("voc_", 0) == 0) for (int i = 0; i < kNumVoc; i++) if (nm.compare(4, std::string::npos, kVocDefs[i].key) == 0) { dest = D_VOC0 + i; return true; } return false; }
        for (int i = 0; i < 6; i++) if (ly == lk[i]) li = i;
        if (ly == "WT2") { li = 0; sub = 1; } else if (ly == "FM2") { li = 1; sub = 1; }
        for (int i = 0; i < kNumGen; i++) if (li == kGenDefs[i].layer && sub == kGenDefs[i].sub && nm == kGenDefs[i].name) { dest = D_GEN0 + i; return true; } } return false; }
    struct T { const char* l; const char* p; int d; double s; };
    static const T tab[] = {
        {"GLOBAL", "macro1", D_MACRO1, 1}, {"GLOBAL", "macro2", D_MACRO2, 1}, {"GLOBAL", "macro3", D_MACRO3, 1}, {"GLOBAL", "macro4", D_MACRO4, 1},
        {"GLOBAL", "wtFmMix", D_WTFM, 1}, {"GLOBAL", "masterTone", D_TONE, 1}, {"GLOBAL", "sampleMix", D_SMPMIX, 1},
        {"GLOBAL", "xmod1", D_XMOD1, 1}, {"GLOBAL", "xmod2", D_XMOD2, 1}, {"GLOBAL", "xmod3", D_XMOD3, 1}, {"GLOBAL", "xmod4", D_XMOD4, 1},
        {"WT", "osc1Octave", D_WT_O1OCT, 2}, {"WT", "osc2Octave", D_WT_O2OCT, 2}, {"WT", "osc1Semi", D_WT_O1SEMI, 12}, {"WT", "osc2Semi", D_WT_O2SEMI, 12},
        {"WT", "osc1Fine", D_WT_O1FINE, 100}, {"WT", "osc2Fine", D_WT_O2FINE, 100},
        {"FM", "engAOctave", D_FM_AOCT, 4}, {"FM", "engBOctave", D_FM_BOCT, 4}, {"FM", "engASemi", D_FM_ASEMI, 12}, {"FM", "engBSemi", D_FM_BSEMI, 12},
        {"FM", "engAFine", D_FM_AFINE, 100}, {"FM", "engBFine", D_FM_BFINE, 100},
        {"FM6OP", "fine", D_DX_FINE, 50}, {"FM6OP", "detune", D_DX_DETUNE, 7}, {"FM6OP", "level", D_DX_LEVEL, 50}, {"FM6OP", "ampModDepth", D_DX_AMD, 50},
        {"FM6OP", "r2", D_DX_R2, 50}, {"FM6OP", "r3", D_DX_R3, 50}, {"FM6OP", "r4", D_DX_R4, 50}, {"FM6OP", "l1", D_DX_L1, 50}, {"FM6OP", "l2", D_DX_L2, 50},
        {"FM", "engAFeedback", D_FM_FB_A, 1}, {"FM", "engBFeedback", D_FM_FB_B, 1},
        {"LFO1", "rate", D_LFO_RATE1, 1}, {"LFO2", "rate", D_LFO_RATE2, 1}, {"LFO3", "rate", D_LFO_RATE3, 1}, {"LFO4", "rate", D_LFO_RATE4, 1}, {"LFO5", "rate", D_LFO_RATE5, 1}, {"LFO6", "rate", D_LFO_RATE6, 1},
        {"LFO1", "amount", D_LFO_AMT1, 1}, {"LFO2", "amount", D_LFO_AMT2, 1}, {"LFO3", "amount", D_LFO_AMT3, 1}, {"LFO4", "amount", D_LFO_AMT4, 1}, {"LFO5", "amount", D_LFO_AMT5, 1}, {"LFO6", "amount", D_LFO_AMT6, 1},
        {"LFO1", "phase", D_LFO_PH1, 0.5}, {"LFO2", "phase", D_LFO_PH2, 0.5}, {"LFO3", "phase", D_LFO_PH3, 0.5}, {"LFO4", "phase", D_LFO_PH4, 0.5}, {"LFO5", "phase", D_LFO_PH5, 0.5}, {"LFO6", "phase", D_LFO_PH6, 0.5},
        {"CHAOS", "lorenzRate", D_CHAOS_LORENZ_RATE, 1}, {"CHAOS", "rosslerRate", D_CHAOS_ROSSLER_RATE, 1},
        {"LFO1", "smoothing", D_LFO_SM1, 1}, {"LFO2", "smoothing", D_LFO_SM2, 1}, {"LFO3", "smoothing", D_LFO_SM3, 1}, {"LFO4", "smoothing", D_LFO_SM4, 1}, {"LFO5", "smoothing", D_LFO_SM5, 1}, {"LFO6", "smoothing", D_LFO_SM6, 1},
        {"LFO1", "morph", D_LFO_MORPH1, 1}, {"LFO2", "morph", D_LFO_MORPH2, 1}, {"LFO3", "morph", D_LFO_MORPH3, 1}, {"LFO4", "morph", D_LFO_MORPH4, 1}, {"LFO5", "morph", D_LFO_MORPH5, 1}, {"LFO6", "morph", D_LFO_MORPH6, 1},
        {"WT", "start", D_WT_START, 1}, {"WT", "scan", D_WT_SCAN, 1}, {"WT", "pwm", D_WT_PWM, 1}, {"WT", "phase", D_WT_PHASE, 1}, {"WT", "startEnd", D_WT_STARTEND, 1},
        {"WT", "osc1Scan", D_WT_O1SCAN, 1}, {"WT", "osc2Scan", D_WT_O2SCAN, 1}, {"WT", "osc1Pwm", D_WT_O1PWM, 1}, {"WT", "osc2Pwm", D_WT_O2PWM, 1}, {"WT", "osc1PwmRate", D_WT_O1PWMHZ, 10}, {"WT", "osc2PwmRate", D_WT_O2PWMHZ, 10},
        {"WT", "osc1Phase", D_WT_O1PHASE, 1}, {"WT", "osc2Phase", D_WT_O2PHASE, 1}, {"WT", "osc1StartEnd", D_WT_O1SE, 1}, {"WT", "osc2StartEnd", D_WT_O2SE, 1},
        {"WT", "osc1Segment", D_WT_O1SEG, 1}, {"WT", "osc2Segment", D_WT_O2SEG, 1}, {"WT", "osc1Level", D_WT_O1LVL, 1}, {"WT", "osc2Level", D_WT_O2LVL, 1},
        {"SMP", "start", D_SMP_START, 0.5}, {"SMP", "end", D_SMP_END, 0.5},
        {"FX", "reverbMix", D_FX_REVMIX, 1}, {"FX", "satMix", D_FX_SATMIX, 1}, {"FX", "satDrive", D_FX_SATDRIVE, 1}, {"FX", "flangerRate", D_FX_FLGRATE, 1},
        {"FX", "foldDrive", D_FX_FOLDDRIVE, 1}, {"FX", "foldMix", D_FX_FOLDMIX, 1}, {"FX", "foldSymmetry", D_FX_FOLDSYM, 1}, {"FX", "foldPwm", D_FX_FOLDPWM, 1},
        {"FX", "specBlur", D_FX_SPECBLUR, 1}, {"FX", "specShift", D_FX_SPECSHIFT, 1}, {"FX", "specThreshold", D_FX_SPECTHR, 1}, {"FX", "specSpread", D_FX_SPECSPREAD, 1}, {"FX", "specMix", D_FX_SPECMIX, 1},
        {"FX", "filterCutoff", D_FX_FILTCUT, 4000}, {"FX", "filterResonance", D_FX_FILTRESO, 30},
        {"FX", "grainSize", D_FX_GRSIZE, 200}, {"FX", "grainPitch", D_FX_GRPITCH, 24}, {"FX", "grainPan", D_FX_GRPAN, 1}, {"FX", "grainDensity", D_FX_GRDENS, 40},
        {"FX", "delayFeedback", D_FX_DELFB, 1}, {"FX", "delayMix", D_FX_DELMIX, 1}, {"FX", "stutterDivision", D_FX_STUTDIV, 1}, {"FX", "bitDepth", D_FX_BITDEPTH, 1},
        {"FX", "flangerMix", D_FX_FLGMIX, 1}, {"FX", "flangerFeedback", D_FX_FLGFB, 1}, {"FX", "phaserMix", D_FX_PHMIX, 1}, {"FX", "phaserRate", D_FX_PHRATE, 1}, {"FX", "phaserFeedback", D_FX_PHFB, 1},
        {"FX", "resonatorFreq", D_FX_RESFREQ, 1}, {"FX", "resonatorResonance", D_FX_RESRES, 1}, {"FX", "resonatorMix", D_FX_RESMIX, 1},
        {"ARP", "rate", D_ARP_RATE, 10},
    };
    for (auto& t : tab) if (L == t.l && P == t.p) { dest = t.d; scale = t.s; return true; }
    if (L == "FM" && P.size() == 8 && P[0] == 'o' && P[1] == 'p' && P[2] >= '1' && P[2] <= '8' && P.compare(3, 5, "Ratio") == 0) { dest = D_FM_OPR1 + (P[2] - '1'); scale = 5.75; return true; }
    return false;
}

struct ModRoute { int src = S_NONE, layer = -1, dest = D_NONE, g = 0; double amt = 0, scale = 1; };

}  // namespace vv
