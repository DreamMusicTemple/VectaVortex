#!/usr/bin/env python3
"""Single source of truth for the GENERATE button (web + native). Writes gen/recipes.json.

Recipe grammar (resolved by genResolve() in the web app and presetgen.hpp in the native Core, one interpreter each):
  value specs   literal | {"r":[lo,hi]} float | {"ri":[lo,hi]} int | {"rl":[lo,hi]} log-uniform | {"p":[a,b,..]} pick | {"pu":[..]} pick without repeats
                | {"pi":[a,b,..]} pick (alias for objects) ; any other object is resolved field by field
  "set"   {dotted preset path: spec}          (applied on a fresh INIT preset)
  "conn"  [[source, dest, spec]]              matrix routes
  "fx"    [{"type","params":{..},"sends":[..]}]
  "seq"   {"layer","len","count":[lo,hi],"pitches":[fallback pitches],"gate","ratchet":[..]}   step sequencer pattern 1; pitches come from the active scale
  "wtsteps" {"n":[lo,hi],"waves":[..],"pitches":[fallback]}                                      SID wavetable-arp steps (scale aware)
  "xfx"   {"bank","range":[lo,hi],"fill","mode":"cycle|random|band","palette":[{"en":{..},"o":{path:spec}}]}   per-key programming (scale aware)
"""
import json, sys

def R(a, b): return {"r": [a, b]}
def RI(a, b): return {"ri": [a, b]}
def RL(a, b): return {"rl": [a, b]}
def P(*x): return {"p": list(x)}
def PU(*x): return {"pu": list(x)}

def is_spec(v):  # a value spec is a one-key dict whose value is a list ({"r":[lo,hi]}); an env dict {"a":..,"r":..} is not
    return len(v) == 1 and next(iter(v)) in ("r", "ri", "rl", "p", "pu", "sd") and isinstance(next(iter(v.values())), list)

def flat(prefix, d, out=None):
    out = {} if out is None else out
    for k, v in d.items():
        key = prefix + "." + k if prefix else k
        if isinstance(v, dict) and not is_spec(v):
            flat(key, v, out)
        else:
            out[key] = v
    return out

def layer(L, **kw):            # layers.<L>.<kw...>
    return flat("layers." + L, kw)

def osc(L, i, **kw):           # layers.<L>.oscs.<i>.<kw...>
    return flat("layers.%s.oscs.%d" % (L, i), kw)

def filt(L, i, type, cutoff, reso, kt=None, env=None):
    f = {"type": type, "cutoff": cutoff, "reso": reso, "on": True}
    if kt is not None: f["keytrack"] = kt
    o = osc(L, i, filter=f) if i is not None else layer(L, filter=f)
    if env: o.update(flat(("layers.%s.oscs.%d.filterEnv" % (L, i)) if i is not None else ("layers." + L + ".filterEnv"), env))
    return o

def env(L, a, d, s, r, rep=0):
    return flat("layers." + L + ".env", {"a": a, "d": d, "s": s, "r": r, "rep": rep})

def merge(*ds):
    o = {}
    for d in ds: o.update(d)
    return o

def fx(type, sends, **params): return {"type": type, "params": params, "sends": sends}

# ---------------------------------------------------------------- sound pools (all exist in the web app and the native assets)
BASS_T = ["dnbReese", "dnbNeuroFold", "dnbTechStep", "dnbTalkingGrowl", "bassGrowlFangs", "bassAcidSquelch", "bassDistortedSaw", "bassWobbleCrunch", "bassMetalGrind", "bassSubCrush", "bassFeedbackRoar", "bassIndustrialStomp", "subBass", "subGrowl", "growlBass"]
JUNGLE_T = ["dnbReese", "dnbNeuroFold", "dnbTechStep", "dnbTalkingGrowl", "bassGrowlFangs", "bassWobbleCrunch", "bassMetalGrind", "bassSubCrush", "bassFeedbackRoar", "subGrowl", "growlBass", "dnbDemonChoir"]
DRONE_T = ["droneWarmPad", "droneSubChoir", "droneAirDrift", "ambientGlassPad", "ambientCloudDrift", "ambientSineStack", "ambientBreath", "ambientCrystalline", "ambientVelvetDrone", "ambientHalo", "ambientMistVeil", "nebulaDrift"]
CHOIR_T = ["vocalIRChoir", "vocalIRFemale", "vocalIRMale", "dnbDarkChoir", "dnbHollowChoir", "dnbCathedralChoir", "vocalOoh", "formantAEIOU"]
GLITCH_T = ["glitchStutter", "digitalAlias", "bitcrushed8bit", "quantumFlutter", "origamiFold", "wavefolded", "noisySpectral", "metallicClang", "prismRefraction", "vortexSpiral", "chipNesVoices", "chipSidCombined", "squareMorph", "pwmLike", "digitalMorph"]
LEAD_T = ["supersaw", "sawBright", "screechLead", "squareMorph", "warmPWM", "pwmLike", "resoSweep", "talkingFormant", "vowelDiphthong"]
KEY_T = ["rhodesTine", "rhodesBark", "organDrawbars", "glassHarmonica", "fmLikeBell", "pluckDecay", "percWoodBurst", "stringsEnsemble", "crystalShimmer"]
CHOIR_S = ["Synth Choir Ahh Wide (synthesized)", "Synth Choir Ooh Soft (synthesized)", "Synth Choir Minor Dark (synthesized)", "Synth Choir Hollow Fifths (synthesized)", "Synth Choir Cathedral (synthesized)", "Synth Choir Breathy Air (synthesized)", "Synth Choir Rising Swell (synthesized)", "Space Choir Pad (synthesized)"]
PAD_S = ["Warm Pad (synthesized)", "Dark Pad (synthesized)", "Harmonium Drone (synthesized)", "Space Choir Pad (synthesized)"]
KICK = ["Industrial Kick Slam", "Sub Kick Thud", "Distorted Kick Crush", "IDM Clicky Kick", "DnB Reese Kick", "Metallic Kick Hit", "Gated Kick Punch", "Lo-Fi Bitcrushed Kick"]
SNARE = ["Industrial Snare Crack", "Noise Snare Wall", "DnB Snappy Snare", "IDM Glitch Snare", "Metallic Snare Ring", "Gated Reverb Snare", "Layered Snare Stack", "Bitcrushed Snare Snap"]
CLAP = ["Analog Clap Burst", "Industrial Clap Smear", "DnB Tight Clap", "Layered Clap Pile", "Metallic Clap Zap", "Lo-Fi Clap Dust", "Wide Clap Spread"]
HAT = ["Closed Hat Tick", "Open Hat Sizzle", "Metallic Hat Ring", "Industrial Hat Hiss", "DnB Hat Shuffle", "IDM Glitch Hat", "Bitcrushed Hat Click", "Foot Hat Chick"]
CYMB = ["Crash Cymbal Metal", "Ride Cymbal Wash", "China Cymbal Trash", "Splash Cymbal Short", "Industrial Cymbal Grind"]
TOM = ["Low Tom Boom", "Mid Tom Punch", "High Tom Snap", "Industrial Tom Clang", "Synth Tom Pitch Drop", "Gated Tom Hit", "Metallic Tom Ring", "Lo-Fi Tom Thud"]
PERC = ["Metallic Perc Hit", "Industrial Scrape", "Glitch Perc Blip", "Woodblock Click", "Noise Perc Burst", "Resonant Perc Ping", "DnB Perc Shaker", "IDM Perc Stutter"]
DFX = ["Glitch Stutter Burst", "Digital Artifact Zap", "White Noise Burst", "Noise Sweep Impact"]
def S(n): return n + " (synthesized)"
def SF(n): return n + " (synthesized, foley)"
def pool(names, foley=()):
    return [SF(n) if n in foley else S(n) for n in names]
D_KICK, D_SNARE, D_CLAP, D_HAT, D_CYMB, D_TOM, D_PERC = [pool(x) for x in (KICK, SNARE, CLAP, HAT, CYMB, TOM, PERC)]
D_FX = [SF(n) for n in DFX]

ALL5 = ["WT", "FM", "SMP", "PRO", "SID"]
def only(*on): return {L: (L in on) for L in ALL5}

recipes = {}

# ================================================================ BASS
recipes["Bass"] = [
 {"name": "Reese Sub", "view": "WT", "desc": "Detuned Reese table with a sub-bass oscillator underneath, a slowly moving ladder filter and saturation.",
  "set": merge(osc("WT", 0, table=P("dnbReese", "bassDistortedSaw", "dnbTechStep"), unison=RI(3, 5), detune=R(10, 24), width=R(0.5, 0.8), level=0.8),
               osc("WT", 1, enabled=True, table="subBass", oct=-1, level=R(0.55, 0.75), unison=1),
               filt("WT", 0, "moog4pole", RL(380, 900), R(14, 32), RI(30, 70)), env("WT", 0.003, R(0.2, 0.4), R(0.78, 0.92), R(0.12, 0.25)),
               {"lfo.lfo1.hz": R(0.1, 0.4)}),
  "conn": [["lfo1", "WT:osc1Cutoff", R(18, 34)]], "fx": [fx("saturation", ["WT"], drive=R(0.35, 0.65), mix=0.6)]},
 {"name": "Wobble", "view": "WT", "desc": "Growl table through a high-resonance ladder filter swept by a tempo-feel LFO.",
  "set": merge(osc("WT", 0, table=P("bassWobbleCrunch", "growlBass", "subGrowl", "bassGrowlFangs"), unison=RI(1, 3), detune=R(6, 14), level=0.85),
               osc("WT", 1, enabled=True, table="subBass", oct=-1, level=0.6),
               filt("WT", 0, "moog4pole", RL(260, 520), R(36, 58), 40), env("WT", 0.002, 0.25, R(0.8, 0.95), 0.12),
               {"lfo.lfo1.hz": R(1.5, 4.2), "lfo.lfo1.waveform": P("sine", "triangle", "pulse", "sawDown")}),
  "conn": [["lfo1", "WT:osc1Cutoff", R(38, 60)]], "fx": [fx("saturation", ["WT"], drive=R(0.4, 0.7), mix=0.5), fx("wavefolder", ["WT"], drive=R(1.5, 3), mix=R(0.1, 0.3))]},
 {"name": "FM Growl", "view": "FM", "desc": "Three-operator FM growl: square modulator with feedback, a wavefolder and a diode filter, plus a clean sub.",
  "set": merge({"layers.FM.engineEnabled.0": True, "layers.FM.fmAlgorithms.0": 2},
               osc("FM", 0, ratio=0.5, level=0.9, wave="sine"), osc("FM", 1, ratio=P(1, 2), level=R(0.4, 0.7), wave=P("sine", "square"), feedback=R(0.2, 0.5)),
               osc("FM", 2, ratio=P(3, 4, 5), level=R(0.15, 0.4)),
               filt("FM", 0, "diode4pole", RL(700, 1600), R(20, 40), 30), env("FM", 0.002, R(0.18, 0.35), R(0.6, 0.8), 0.14),
               osc("WT", 0, table="subBass", unison=1, level=0.6), {"lfo.lfo2.hz": R(0.5, 3)}),
  "conn": [["lfo2", "FM:engACutoff", R(15, 30)]], "fx": [fx("wavefolder", ["FM"], drive=R(2, 5), mix=R(0.3, 0.5)), fx("saturation", ["FM", "WT"], drive=0.4, mix=0.5)]},
 {"name": "Acid Squelch", "view": "WT", "desc": "Resonant squelch with a fast filter envelope, glide and a scale-aware 16-step pattern. Press PLAY.",
  "set": merge(osc("WT", 0, table=P("bassAcidSquelch", "sawBright", "bassDistortedSaw"), unison=1, level=0.85, glideTime=R(0.04, 0.09)),
               filt("WT", 0, "moog4pole", RL(280, 460), R(70, 100), 40, env={"amount": R(2500, 5000), "a": 0.001, "d": R(0.14, 0.24), "s": 0, "r": 0.1}),
               env("WT", 0.001, R(0.16, 0.26), R(0.4, 0.6), 0.1), {"layers.WT.gate": 0.55}),
  "seq": {"layer": "WT", "len": 16, "count": [8, 12], "pitches": [0, 0, 0, 3, 5, 7, 10, 12], "ratchet": [1, 1, 1, 2]},
  "fx": [fx("delay", ["WT"], time=0.375, feedback=R(0.25, 0.4), mix=R(0.15, 0.28)), fx("saturation", ["WT"], drive=0.4, mix=0.4)]},
 {"name": "808 Sub", "view": "SMP", "desc": "Long synthesized 808 sub sample with a pure sub oscillator layered under it and light saturation.",
  "set": merge(osc("SMP", 0, name=S("808 Sub Kick Long"), level=0.9, root=36, env={"a": 0.001, "d": 1.4, "s": 0.35, "r": 0.4, "rep": 0}),
               osc("WT", 0, table="subBass", unison=1, level=R(0.4, 0.6)), env("WT", 0.002, 0.9, 0.5, 0.35)),
  "fx": [fx("saturation", ["SMP", "WT"], drive=R(0.3, 0.55), mix=0.5)]},
 {"name": "Chip Bass", "view": "SID", "desc": "Three SID voices: pulse bass, octave-down saw and a narrow pulse, through the 6581 filter.",
  "set": merge(osc("SID", 0, enabled=True, wave="pulse", pulseWidth=R(0.3, 0.5), level=0.9, oct=-1, env={"a": 0.001, "d": R(0.15, 0.25), "s": R(0.45, 0.6), "r": 0.08, "rep": 0}),
               osc("SID", 1, enabled=True, wave="sawtooth", level=0.65, oct=-2, env={"a": 0.001, "d": 0.2, "s": 0.6, "r": 0.1, "rep": 0}),
               osc("SID", 2, enabled=True, wave="pulse", pulseWidth=0.12, level=0.35, oct=-1, semitone=P(0, 7, 12), env={"a": 0.001, "d": 0.1, "s": 0.3, "r": 0.06, "rep": 0}),
               filt("SID", None, P("sid6581", "sid8580"), RL(500, 1300), R(30, 60), 20)),
  "conn": [["lfo3", "SID:voice1Pwm", R(15, 35)]]},
 {"name": "Bowed Sub", "view": "PRO", "desc": "Physically-modelled bowed string an octave down with a sine sub; slow bow attack.",
  "set": merge(osc("PRO", 0, enabled=True, model="bow", level=0.85, oct=-1, bright=R(0.2, 0.4), damp=R(0.3, 0.45), body=R(0.5, 0.7), stiffness=R(0.3, 0.5)),
               osc("WT", 0, table="subBass", unison=1, level=0.55), env("PRO", R(0.02, 0.06), 0.3, 0.85, 0.2), env("WT", 0.03, 0.3, 0.9, 0.2)),
  "fx": [fx("reverb", ["PRO"], algo="plate", decay=0.4, mix=0.15)]},
]

# ================================================================ PAD
recipes["Pad"] = [
 {"name": "Choir Pad", "view": "WT", "desc": "Vocal-impulse-response choir table with detuned unison, an octave-up female layer and a slow vowel scan.",
  "set": merge(osc("WT", 0, table=P("vocalIRChoir", "dnbCathedralChoir", "vocalIRMale"), unison=RI(5, 7), detune=R(12, 24), width=R(0.7, 0.95), drift=R(0.1, 0.25), level=0.7),
               osc("WT", 1, enabled=True, table="vocalIRFemale", oct=1, level=R(0.2, 0.32), unison=3, detune=8, pan=0.4),
               filt("WT", 0, "statevariable", RL(2500, 5000), R(0, 12)), env("WT", R(1.2, 2.4), 1.4, R(0.8, 0.92), R(2.5, 4)),
               {"lfo.lfo1.hz": R(0.05, 0.12)}),
  "conn": [["lfo1", "WT:osc1Scan", R(20, 36)]], "fx": [fx("chorus", ["WT"], mix=0.3), fx("reverb", ["WT"], algo="hall", decay=R(0.75, 0.9), size=R(1, 1.3), mix=R(0.3, 0.42))]},
 {"name": "Glass Pad", "view": "FM", "desc": "Additive FM bell partials over a crystalline wavetable, tape wobble and a plate reverb.",
  "set": merge({"layers.FM.engineEnabled.0": True, "layers.FM.fmAlgorithms.0": 5},
               osc("FM", 0, ratio=1, level=0.7, wave="sine"), osc("FM", 1, ratio=P(2, 3), level=0.3, wave="sine"), osc("FM", 2, ratio=P(4.2, 5.4), level=R(0.12, 0.22)), osc("FM", 3, ratio=P(7, 9.3), level=R(0.06, 0.12)),
               filt("FM", 0, "twopole12db", RL(3500, 7000), 6), env("FM", R(1.4, 2.4), 1.6, 0.85, R(3, 4.5)),
               osc("WT", 0, table=P("ambientGlassPad", "ambientCrystalline", "crystalShimmer"), unison=5, detune=14, width=0.9, level=0.5), env("WT", 1.8, 1.4, 0.85, 3.5),
               {"lfo.lfo2.hz": R(0.04, 0.09)}),
  "conn": [["lfo2", "FM:engACutoff", 18]], "fx": [fx("tape", ["FM", "WT"], mix=0.45), fx("reverb", ["FM", "WT"], algo="plate", decay=0.8, mix=0.38)]},
 {"name": "Drone Bed", "view": "WT", "desc": "Dark evolving drone: warm drone table, halo octave, breathing noise and a very slow filter.",
  "set": merge(osc("WT", 0, table=P("droneWarmPad", "droneSubChoir", "ambientVelvetDrone"), unison=7, detune=R(6, 12), width=1, drift=0.3, level=0.7),
               osc("WT", 1, enabled=True, table=P("ambientHalo", "droneAirDrift"), oct=1, level=0.3, unison=3, detune=9, pan=-0.3),
               filt("WT", 0, "statevariable", RL(900, 2200), R(2, 14)), env("WT", R(3, 5), 2, 0.9, R(5, 8)), {"lfo.lfo1.hz": R(0.02, 0.05), "lfo.lfo2.hz": R(0.03, 0.07)}),
  "conn": [["lfo1", "WT:osc1Cutoff", 30], ["lfo2", "WT:osc2Scan", 25]], "fx": [fx("phaser", ["WT"], rate=0.1, depth=0.6, feedback=0.4, mix=0.3), fx("reverb", ["WT"], algo="hall", decay=0.9, size=1.4, mix=0.5)]},
 {"name": "Choir Sample Pad", "view": "SMP", "desc": "Two looped synthesized choir samples with slow envelopes under a cloud-drift wavetable.",
  "set": merge(osc("SMP", 0, name=PU(*CHOIR_S), level=0.75, loop=True, env={"a": R(1.2, 2), "d": 1, "s": 0.9, "r": R(2.5, 4), "rep": 0}),
               osc("SMP", 1, name=PU(*CHOIR_S), level=0.5, loop=True, pan=0.4, fineTune=R(4, 9), env={"a": R(1.6, 2.6), "d": 1, "s": 0.9, "r": R(2.5, 4), "rep": 0}),
               osc("WT", 0, table="ambientCloudDrift", unison=3, detune=10, level=0.35), env("WT", 2, 1.5, 0.9, 3.5)),
  "fx": [fx("chorus", ["SMP", "WT"], mix=0.25), fx("reverb", ["SMP", "WT"], algo="hall", decay=0.85, size=1.3, mix=0.4)]},
 {"name": "Chip Pad", "view": "SID", "desc": "Three detuned SID pulse voices with moving pulse widths, soft attack and a 8580 filter.",
  "set": merge(osc("SID", 0, enabled=True, wave="pulse", pulseWidth=0.3, level=0.6, fineTune=-9, pan=-0.6, env={"a": R(0.6, 1), "d": 0.5, "s": 0.8, "r": R(1, 1.6), "rep": 0}),
               osc("SID", 1, enabled=True, wave="pulse", pulseWidth=0.5, level=0.6, fineTune=0, pan=0, env={"a": R(0.7, 1.1), "d": 0.5, "s": 0.8, "r": R(1, 1.6), "rep": 0}),
               osc("SID", 2, enabled=True, wave=P("pulse", "triangle"), pulseWidth=0.7, level=0.5, fineTune=9, pan=0.6, oct=P(0, 1), env={"a": R(0.8, 1.2), "d": 0.5, "s": 0.8, "r": R(1, 1.6), "rep": 0}),
               filt("SID", None, "sid8580", RL(1400, 2800), R(8, 22)), {"lfo.lfo3.hz": R(0.15, 0.35)}),
  "conn": [["lfo3", "SID:voice1Pwm", 30], ["lfo3", "SID:voice2Pwm", 24]], "fx": [fx("chorus", ["SID"], mix=0.4), fx("reverb", ["SID"], algo="hall", decay=0.7, mix=0.3)]},
 {"name": "Bowed Air", "view": "PRO", "desc": "Breath and bow models blended into a soft, airy pad with granular-style shimmer from the spectral blur effect.",
  "set": merge(osc("PRO", 0, enabled=True, model="breath", level=0.7, bright=R(0.4, 0.6), damp=0.4, bore=R(0.5, 0.8)), osc("PRO", 1, enabled=True, model="bow", level=0.55, oct=1, bright=0.45, damp=0.45, body=0.6, pan=0.3),
               env("PRO", R(1.2, 2), 1, 0.85, R(2.5, 4)), {"layers.PRO.proCoupling": R(0.2, 0.5)}),
  "fx": [fx("spectral", ["PRO"], blur=R(0.15, 0.35), mix=0.5), fx("reverb", ["PRO"], algo="hall", decay=0.85, mix=0.4)]},
]

# ================================================================ KEY
recipes["Key"] = [
 {"name": "Tine Electric Piano", "view": "FM", "desc": "Classic tine electric piano: sine carrier, bright inharmonic modulator, rhodes wavetable under it, chorus and tape.",
  "set": merge({"layers.FM.engineEnabled.0": True, "layers.FM.fmAlgorithms.0": 2}, osc("FM", 0, ratio=1, level=0.85, wave="sine"), osc("FM", 1, ratio=P(14, 7, 3), level=R(0.1, 0.22), wave="sine"), osc("FM", 2, ratio=2, level=0.18),
               filt("FM", 0, "twopole12db", RL(4500, 8000), 4), env("FM", 0.003, R(0.9, 1.6), R(0.15, 0.3), R(0.35, 0.7)),
               osc("WT", 0, table=P("rhodesTine", "rhodesBark"), unison=1, level=0.45), env("WT", 0.003, 1.2, 0.2, 0.5)),
  "fx": [fx("chorus", ["FM", "WT"], mix=0.35), fx("tape", ["FM", "WT"], mix=0.3), fx("reverb", ["FM", "WT"], algo="plate", decay=0.35, mix=0.2)]},
 {"name": "Drawbar Organ", "view": "WT", "desc": "Drawbar organ with an octave-up second oscillator, full sustain and a rotating phaser.",
  "set": merge(osc("WT", 0, table="organDrawbars", unison=1, level=0.7), osc("WT", 1, enabled=True, table="organDrawbars", oct=1, level=0.35), filt("WT", 0, "twopole12db", RL(5000, 9000), 3),
               env("WT", 0.008, 0.2, 0.96, 0.09), {"layers.WT.oscs.0.drift": 0.12}),
  "fx": [fx("phaser", ["WT"], rate=R(0.4, 0.9), depth=0.5, feedback=0.3, mix=0.4), fx("reverb", ["WT"], algo="plate", decay=0.3, mix=0.15)]},
 {"name": "Bell Keys", "view": "FM", "desc": "Struck-bell FM with inharmonic partials and a long decay, doubled by a bell wavetable.",
  "set": merge({"layers.FM.engineEnabled.0": True, "layers.FM.fmAlgorithms.0": 5}, osc("FM", 0, ratio=1, level=0.8), osc("FM", 1, ratio=P(3.5, 2.76), level=0.35), osc("FM", 2, ratio=P(5.4, 5.95), level=0.22), osc("FM", 3, ratio=P(8.9, 7.1), level=0.12),
               filt("FM", 0, "twopole12db", RL(6000, 11000), 3), env("FM", 0.002, R(1.8, 2.8), 0.02, R(1.2, 2)),
               osc("WT", 0, table=P("fmLikeBell", "bellMetallic", "glassHarmonica"), unison=1, level=0.35), env("WT", 0.002, 2, 0, 1.4)),
  "fx": [fx("reverb", ["FM", "WT"], algo="hall", decay=0.7, size=1.1, mix=0.35), fx("delay", ["FM", "WT"], time=0.375, feedback=0.3, mix=0.18)]},
 {"name": "Pluck Keys", "view": "PRO", "desc": "Plucked-string model with a wavetable pluck transient and a short stereo delay.",
  "set": merge(osc("PRO", 0, enabled=True, model="pluck", level=0.85, bright=R(0.5, 0.75), damp=R(0.3, 0.5), body=0.4, excite=P(0, 1, 3)), osc("WT", 0, table="pluckDecay", unison=1, level=0.35),
               env("PRO", 0.002, 1.2, 0.2, 0.5), env("WT", 0.002, 0.4, 0, 0.3)),
  "fx": [fx("delay", ["PRO", "WT"], time=0.28, feedback=0.28, mix=0.22), fx("reverb", ["PRO", "WT"], algo="plate", decay=0.4, mix=0.2)]},
 {"name": "Soft Strings", "view": "WT", "desc": "Strings-ensemble table with wide detuned unison, medium attack, flanger and hall.",
  "set": merge(osc("WT", 0, table="stringsEnsemble", unison=RI(5, 7), detune=R(10, 18), width=0.85, drift=0.2, level=0.75), osc("WT", 1, enabled=True, table="ensembleShimmer", oct=1, level=0.18),
               filt("WT", 0, "statevariable", RL(3500, 6500), 6), env("WT", R(0.35, 0.7), 0.6, 0.85, R(0.9, 1.6))),
  "fx": [fx("flanger", ["WT"], rate=0.2, depth=0.4, feedback=0.25, mix=0.3), fx("reverb", ["WT"], algo="hall", decay=0.65, mix=0.3)]},
]

# ================================================================ LEAD
recipes["Lead"] = [
 {"name": "Super Saw Lead", "view": "WT", "desc": "Wide supersaw lead with vibrato, ladder filter and a dotted delay.",
  "set": merge(osc("WT", 0, table="supersaw", unison=RI(5, 7), detune=R(14, 24), width=0.8, level=0.8, glideTime=R(0, 0.06)), filt("WT", 0, "moog4pole", RL(5000, 9000), R(10, 25), 40),
               env("WT", R(0.005, 0.03), 0.2, R(0.7, 0.9), R(0.2, 0.4)), {"lfo.lfo1.hz": R(4.8, 6.2)}),
  "conn": [["lfo1", "WT:pitch", R(2, 5)], ["modwheel", "WT:osc1Cutoff", 30]], "fx": [fx("delay", ["WT"], time=0.375, feedback=0.35, mix=0.22), fx("reverb", ["WT"], algo="hall", decay=0.5, mix=0.2)]},
 {"name": "FM Whistle", "view": "FM", "desc": "Pure sine-on-sine FM lead with vibrato and a gentle octave shimmer.",
  "set": merge({"layers.FM.engineEnabled.0": True, "layers.FM.fmAlgorithms.0": 2}, osc("FM", 0, ratio=1, level=0.9, wave=P("sine", "triangle")), osc("FM", 1, ratio=P(1, 2), level=R(0.25, 0.5)),
               filt("FM", 0, "moog4pole", RL(4500, 9000), R(10, 30)), env("FM", R(0.008, 0.05), 0.15, R(0.65, 0.9), R(0.2, 0.45)), {"lfo.lfo1.hz": R(4.5, 6.5)}),
  "conn": [["lfo1", "FM:pitch", R(2, 6)]], "fx": [fx("delay", ["FM"], time=0.3, feedback=0.3, mix=0.2), fx("reverb", ["FM"], algo="plate", decay=0.45, mix=0.18)]},
 {"name": "Screech", "view": "WT", "desc": "Aggressive screech table with a resonant sweep, wavefolder and a bright, short release.",
  "set": merge(osc("WT", 0, table=P("screechLead", "resoSweep"), unison=RI(2, 4), detune=R(8, 16), width=0.6, level=0.8), filt("WT", 0, "diode4pole", RL(3500, 7000), R(35, 60), 60, env={"amount": R(1500, 4000), "a": 0.002, "d": 0.3, "s": 0.3, "r": 0.2}),
               env("WT", 0.004, 0.2, 0.8, 0.15)), "fx": [fx("wavefolder", ["WT"], drive=R(2, 5), mix=R(0.25, 0.5)), fx("delay", ["WT"], time=0.25, feedback=0.3, mix=0.15)]},
 {"name": "Chip Lead", "view": "SID", "desc": "Pulse lead with PWM and vibrato, a detuned saw behind it and a retro arpeggio-free tone.",
  "set": merge(osc("SID", 0, enabled=True, wave="pulse", pulseWidth=R(0.3, 0.5), level=0.85, vibratoDepth=R(8, 25), env={"a": 0.003, "d": 0.12, "s": 0.75, "r": 0.15, "rep": 0}),
               osc("SID", 1, enabled=True, wave="sawtooth", level=0.4, fineTune=R(5, 11), env={"a": 0.003, "d": 0.12, "s": 0.7, "r": 0.15, "rep": 0}),
               filt("SID", None, "sid6581", RL(2500, 5000), R(8, 24)), {"lfo.lfo3.hz": R(0.3, 1.1)}),
  "conn": [["lfo3", "SID:voice1Pwm", R(20, 40)]], "fx": [fx("delay", ["SID"], time=0.25, feedback=0.3, mix=0.2)]},
 {"name": "Talking Lead", "view": "WT", "desc": "Formant tables that speak as an LFO scans the vowels; slow vibrato and a hall tail.",
  "set": merge(osc("WT", 0, table=P("talkingFormant", "vowelDiphthong", "formantAEIOU"), unison=RI(1, 3), detune=R(5, 12), level=0.85), filt("WT", 0, "formant", RL(1800, 3200), R(15, 35)),
               env("WT", 0.01, 0.2, 0.85, 0.3), {"lfo.lfo1.hz": R(0.4, 1.4), "lfo.lfo2.hz": R(5, 6)}),
  "conn": [["lfo1", "WT:osc1Scan", R(35, 60)], ["lfo2", "WT:pitch", 3]], "fx": [fx("reverb", ["WT"], algo="hall", decay=0.6, mix=0.25)]},
]


# ---------------------------------------------------------------- arpeggiator settings (STANDARD / ADVANCED, see docs Round 14-16)
DIV = {"64": 0.0625, "32T": 0.08333333333333333, "32": 0.125, "16T": 0.16666666666666666, "16": 0.25, "8T": 0.3333333333333333, "8": 0.5, "4T": 0.6666666666666666, "4": 1}
STD_ORDERS = ["UP", "DOWN", "UP/DOWN", "DOWN/UP", "RANDOM", "RANDOM WALK", "PENDULUM", "SPIRAL", "STRUM", "OCTAVE ORBIT"]
def arp_set(orders, divs, octs, algos, adv_weight=5, std_weight=2, gate=(0.4, 0.8), swing=(50, 50, 54, 58), vel=(0.8, 1.0), repeat=(1, 1, 1, 2), adv=None, scales=("major", "minor", "dorian", "pentatonic", "blues")):
    """Arpeggiator block for a recipe: STANDARD (playback mode from `orders`) or ADVANCED (one of `algos` with musical parameter ranges) - weighted by adv_weight : std_weight,
    plus the new gate / swing / velocity / note-repeat controls. `adv` overrides individual advanced parameter specs."""
    a = {"probability": R(0.85, 1), "density": R(0.45, 0.85), "complexity": R(0.2, 0.8), "mutation": R(0.1, 0.4), "variation": R(0, 0.3), "randomness": R(0, 0.1), "humanise": R(0, 0.2),
         "octSpread": R(0, 0.35), "ratchet": R(0, 0.3), "velVar": R(0, 0.3), "accent": R(0.2, 0.6), "gateVar": R(0, 0.3), "groove": R(0.2, 0.7), "patLen": P(8, 12, 16, 16, 24, 32),
         "cycleVar": R(0, 0.4), "rotation": RI(0, 3), "skipProb": R(0, 0.12), "rhythmDisp": R(0, 0.3), "scale": P(*scales)}
    if adv: a.update(adv)
    out = {"arp.on": True, "arp.mode": P(*(["advanced"] * adv_weight + ["standard"] * std_weight)), "arp.order": P(*orders), "arp.division": P(*[DIV[d] for d in divs]), "arp.octRange": RI(*octs),
           "arp.gate": R(*gate), "arp.swing": P(*swing), "arp.velocity": R(*vel), "arp.repeat": P(*repeat), "arp.transpose": 0, "arp.latch": False,
           "arp.adv.algo": P(*algos), "arp.adv.seed": RI(1, 999999)}
    for k, v in a.items(): out["arp.adv." + k] = v
    return out

# ================================================================ ARP
recipes["Arp"] = [
 {"name": "FM Pluck Arp", "view": "FM", "desc": "Bright FM pluck with the arpeggiator running (UP/DOWN, two octaves).",
  "set": merge({"layers.FM.engineEnabled.0": True, "layers.FM.fmAlgorithms.0": 2}, osc("FM", 0, ratio=1, level=0.9, wave=P("sine", "triangle", "square")), osc("FM", 1, ratio=P(1, 2, 3), level=R(0.1, 0.4)),
               filt("FM", 0, P("moog4pole", "twopole12db"), RL(1800, 6000), R(5, 30)), env("FM", 0.002, R(0.08, 0.22), R(0.05, 0.25), R(0.06, 0.2)),
               arp_set(["UP", "UP/DOWN", "RANDOM", "PENDULUM", "SPIRAL"], ["16", "16", "8", "16T"], (2, 3), ["pedal", "broken", "harmonic", "voicelead", "callresp", "intseq", "fractal"], gate=(0.35, 0.7), adv={"complexity": R(0.3, 0.9)})),
  "fx": [fx("delay", ["FM"], time=0.375, feedback=0.35, mix=0.25), fx("reverb", ["FM"], algo="plate", decay=0.4, mix=0.18)]},
 {"name": "Acid Arp", "view": "WT", "desc": "Saw arp with a snappy filter envelope; arp order and rate are randomised.",
  "set": merge(osc("WT", 0, table=P("sawBright", "bassAcidSquelch", "squareMorph"), unison=1, level=0.85), filt("WT", 0, "moog4pole", RL(500, 1200), R(45, 80), 30, env={"amount": R(2000, 4500), "a": 0.001, "d": R(0.1, 0.2), "s": 0.1, "r": 0.1}),
               env("WT", 0.001, R(0.1, 0.2), 0.3, 0.1), arp_set(["UP", "UP/DOWN", "RANDOM", "RANDOM WALK", "OCTAVE ORBIT"], ["16", "32", "16T", "16"], (1, 3), ["euclid", "syncopated", "ratchetA", "markov", "offbeat", "stutter", "groove", "chaos"], gate=(0.25, 0.55), swing=(50, 54, 58, 62, 66), adv={"density": R(0.5, 0.9), "ratchet": R(0.1, 0.5), "groove": R(0.4, 0.9)})),
  "fx": [fx("delay", ["WT"], time=0.375, feedback=0.4, mix=0.22), fx("saturation", ["WT"], drive=0.35, mix=0.4)]},
 {"name": "SID Chip Arp", "view": "SID", "desc": "The SID wavetable-arp: steps change waveform and pitch at the chip rate; the steps follow the active scale.",
  "set": merge(osc("SID", 0, enabled=True, wave="pulse", pulseWidth=0.5, level=0.85, arpOn=True, env={"a": 0.001, "d": 0.2, "s": 0.8, "r": 0.1, "rep": 0}),
               osc("SID", 1, enabled=True, wave="triangle", level=0.45, oct=1, arpOn=True, env={"a": 0.001, "d": 0.2, "s": 0.8, "r": 0.1, "rep": 0}),
               filt("SID", None, "sid6581", RL(2500, 6000), R(8, 30)), {"layers.SID.wtArp.on": True, "layers.SID.wtArp.speedHz": RI(18, 50)}),
  "wtsteps": {"n": [4, 8], "waves": ["pulse", "triangle", "sawtooth", "pulse"], "pitches": [0, 3, 7, 12, 10, 5]}, "fx": [fx("delay", ["SID"], time=0.1875, feedback=0.3, mix=0.2)]},
 {"name": "Glass Arp", "view": "WT", "desc": "Crystalline wavetable pluck arpeggiated in 16ths with a long shimmering delay.",
  "set": merge(osc("WT", 0, table=P("crystalShimmer", "glassHarmonica", "ambientCrystalline"), unison=2, detune=8, width=0.6, level=0.8), filt("WT", 0, "twopole12db", RL(3500, 9000), 6),
               env("WT", 0.002, R(0.2, 0.5), 0.05, R(0.3, 0.7)), arp_set(["UP", "UP/DOWN", "SPIRAL", "DOWN/UP"], ["16", "16", "8", "32"], (2, 4), ["fractal", "kaleido", "fibonacci", "scalewalk", "cascade", "brownian", "golden", "morph"], gate=(0.5, 0.9), swing=(50, 50, 54), adv={"complexity": R(0.3, 0.8), "humanise": R(0, 0.15)})),
  "fx": [fx("delay", ["WT"], time=0.375, feedback=0.45, mix=0.3), fx("reverb", ["WT"], algo="hall", decay=0.8, mix=0.35)]},
 {"name": "Pattern Pluck", "view": "PRO", "desc": "Physical pluck driven by its own scale-aware 16-step pattern (press PLAY).",
  "set": merge(osc("PRO", 0, enabled=True, model=P("pluck", "modal"), level=0.85, bright=R(0.5, 0.8), damp=R(0.3, 0.5), body=0.4), env("PRO", 0.002, R(0.3, 0.7), 0.05, 0.3), {"layers.PRO.gate": 0.5}),
  "seq": {"layer": "PRO", "len": 16, "count": [7, 11], "pitches": [0, 3, 7, 10, 12, 15, 19], "ratchet": [1, 1, 2]}, "fx": [fx("delay", ["PRO"], time=0.375, feedback=0.35, mix=0.25)]},
]

# ================================================================ FX
recipes["FX"] = [
 {"name": "Noise Riser", "view": "WT", "desc": "White-noise riser: a slow rising filter sweep and a long swell envelope into a big reverb.",
  "set": merge(osc("WT", 0, table=P("whiteNoise", "noisySpectral", "pinkNoise"), unison=1, level=0.8), filt("WT", 0, "bandpass", RL(300, 700), R(25, 55)), env("WT", R(2.5, 4), 0.5, 1, R(1.5, 3)),
               {"lfo.lfo1.hz": R(0.06, 0.12), "lfo.lfo1.waveform": "sawUp"}),
  "conn": [["lfo1", "WT:osc1Cutoff", 85]], "fx": [fx("reverb", ["WT"], algo="hall", decay=0.9, size=1.4, mix=0.45)]},
 {"name": "Metal Hit", "view": "FM", "desc": "Inharmonic FM struck-metal hit through a comb filter and a resonator.",
  "set": merge({"layers.FM.engineEnabled.0": True, "layers.FM.fmAlgorithms.0": P(0, 1, 3, 4)}, osc("FM", 0, ratio=R(0.8, 3), level=0.85), osc("FM", 1, ratio=R(1.5, 7), level=R(0.4, 0.7)), osc("FM", 2, ratio=R(2, 9), level=R(0.3, 0.6)), osc("FM", 3, ratio=R(3, 11), level=R(0.2, 0.5)),
               filt("FM", 0, "comb", RL(600, 3500), R(40, 80)), env("FM", 0.001, R(0.6, 1.6), 0.02, R(0.6, 1.6))),
  "fx": [fx("resonator", ["FM"], freq=RL(200, 900), resonance=R(20, 45), mix=R(0.25, 0.5)), fx("reverb", ["FM"], algo="plate", decay=0.55, mix=0.3)]},
 {"name": "Glitch Burst", "view": "WT", "desc": "Digital-alias wavetable through the Glitch effect with bitcrush, stutter and the mini glitch sequencer.",
  "set": merge(osc("WT", 0, table=P("glitchStutter", "digitalAlias", "bitcrushed8bit"), unison=1, level=0.8), filt("WT", 0, "highpass", RL(300, 900), R(5, 20)), env("WT", 0.001, R(0.15, 0.4), R(0.1, 0.4), R(0.1, 0.3))),
  "fx": [fx("glitch", ["WT"], bitDepth=RI(5, 10), srReduction=RI(2, 6), stutterOn=True, stutterDivision=P(0.125, 0.25), stutterAmount=R(0.5, 0.9), seqOn=True, seqRate=P("1/16", "1/8"), seqIntensity=R(0.3, 0.7), mix=1), fx("delay", ["WT"], time=0.1875, feedback=0.4, mix=0.2)]},
 {"name": "Granular Cloud", "view": "SMP", "desc": "Foley sample (wind / rain / paper) smeared by the granular effect into an evolving cloud.",
  "set": merge(osc("SMP", 0, name=P(SF("Wind Whoosh"), SF("Rain"), SF("Paper Rustle")), level=0.8, loop=True, env={"a": R(0.6, 1.5), "d": 1, "s": 0.9, "r": R(1.5, 3), "rep": 0})),
  "fx": [fx("granular", ["SMP"], grainSize=RI(40, 140), density=RI(14, 40), pitch=P(0, 0, 7, -12), spread=R(0.4, 0.9), mix=R(0.6, 1)), fx("reverb", ["SMP"], algo="hall", decay=0.8, mix=0.4)]},
 {"name": "Chaos Texture", "view": "PRO", "desc": "Chaotic procedural generators coupled together, blurred by the spectral effect.",
  "set": merge(osc("PRO", 0, enabled=True, model="chaos", level=0.8, chaos=R(0.5, 0.9), bright=R(0.4, 0.8), damp=R(0.2, 0.5), body=R(0.3, 0.8)), osc("PRO", 1, enabled=True, model="modal", level=0.5, oct=P(0, 1), chaos=R(0.2, 0.5), bright=0.6, pan=0.4),
               env("PRO", R(0.2, 1), 1, R(0.4, 0.8), R(1, 2.5)), {"layers.PRO.proCoupling": R(0.4, 0.8)}),
  "fx": [fx("spectral", ["PRO"], blur=R(0.2, 0.5), shift=R(-0.2, 0.2), mix=0.7), fx("reverb", ["PRO"], algo="plate", decay=0.7, mix=0.35)]},
 {"name": "Wavefold Portal", "view": "FM", "desc": "Four-operator FM pushed through the wavefolder, a comb or formant filter and a long envelope.",
  "set": merge({"layers.FM.engineEnabled.0": True, "layers.FM.fmAlgorithms.0": P(0, 1, 3, 4)}, osc("FM", 0, ratio=R(0.3, 4), level=0.85, wave=P("sine", "triangle", "square")), osc("FM", 1, ratio=R(0.3, 7), level=R(0.3, 0.7)), osc("FM", 2, ratio=R(0.3, 7), level=R(0.2, 0.6)), osc("FM", 3, ratio=R(0.3, 7), level=R(0.1, 0.5)),
               filt("FM", 0, P("comb", "bandpass", "formant"), RL(500, 6000), R(30, 80)), env("FM", R(0, 0.5) if False else R(0.001, 0.5), R(0.2, 1.5), R(0, 0.6), R(0.3, 2))),
  "fx": [fx("wavefolder", ["FM"], drive=R(3, 15), mix=R(0.3, 0.8)), fx("reverb", ["FM"], algo="hall", decay=0.7, mix=0.3)]},
]

# ================================================================ DRUM (a playable kit: one sample per key, each slot's zone is its own key, root = key so samples play unpitched)
def kit(names_by_key, extra=None):
    o = {}
    for i, (key, pool_) in enumerate(names_by_key):
        o.update(osc("SMP", i, name=PU(*pool_), level=R(0.75, 0.9), root=key, rangeLow=key, rangeHigh=key, env={"a": 0.001, "d": 0.6, "s": 0.8, "r": 0.15, "rep": 0}))
    if extra: o.update(extra)
    return o
KIT_KEYS = [(36, D_KICK), (37, D_PERC), (38, D_SNARE), (39, D_CLAP), (40, D_SNARE), (41, D_TOM), (42, D_HAT), (43, D_TOM), (44, D_HAT), (45, D_TOM), (46, D_HAT), (47, D_PERC), (48, D_TOM), (49, D_CYMB), (50, D_PERC), (51, D_CYMB)]
recipes["Drum"] = [
 {"name": "Mixed Kit", "view": "SMP", "desc": "A playable 16-pad drum kit from the synthesized drum set: kick on C2, snares, claps, hats, toms, percussion and cymbals across the keys above it.",
  "set": kit(KIT_KEYS), "fx": [fx("saturation", ["SMP"], drive=0.25, mix=0.3), fx("reverb", ["SMP"], algo="plate", decay=0.25, mix=0.1)]},
 {"name": "IDM Stutter Kit", "view": "SMP", "desc": "Glitchy kit whose slots repeat their own amp envelope (2-4 extra hits) for IDM-style stutters.",
  "set": kit([(k, p) for k, p in KIT_KEYS], {}),
  "post": "idm", "fx": [fx("glitch", ["SMP"], bitDepth=RI(8, 12), srReduction=RI(1, 3), stutterOn=False, seqOn=False, mix=0.6), fx("delay", ["SMP"], time=0.1875, feedback=0.3, mix=0.15)]},
]

# Per-slot repeat for the IDM kit (applied by 'post': idm -> every slot env gets a short decay and rep 2..4)
for v in recipes["Drum"]:
    if v.get("post") == "idm":
        for i in range(16):
            v["set"]["layers.SMP.oscs.%d.env.d" % i] = R(0.05, 0.14)
            v["set"]["layers.SMP.oscs.%d.env.s" % i] = 0
            v["set"]["layers.SMP.oscs.%d.env.rep" % i] = RI(0, 4)
        del v["post"]

# ================================================================ XFX (per-key programming)
def SD(*fb): return {"sd": list(fb)}
def slots_pools(pools, **common):
    o = {}
    for i, pl in enumerate(pools):
        o.update(osc("SMP", i, name=PU(*pl), level=0.85 if i == 0 else 0, root=60, pitchKeytrack=0, env={"a": 0.001, "d": 0.5, "s": 0.7, "r": 0.1, "rep": 0}, **common))
    return o
def slots_named(names, **common):
    o = {}
    for i, n in enumerate(names):
        o.update(osc("SMP", i, name=n, level=0.85 if i == 0 else 0, root=60, **common, env={"a": 0.001, "d": 0.5, "s": 0.7, "r": 0.1, "rep": 0}))
    return o
def smp_key(**kw):  # per-key override ids for the sampler voice (slot 0 plays whichever slot's sample is chosen)
    return {"SMP.oscs.0." + k: v for k, v in kw.items()}
def flatov(d):  # nested env/filter dicts -> dotted override ids
    out = {}
    for k, v in d.items():
        if isinstance(v, dict) and not is_spec(v):
            for k2, v2 in flatov(v).items(): out[k + "." + k2] = v2
        else: out[k] = v
    return out
def ov(layer, idx, **kw):
    return {("%s.oscs.%d.%s" % (layer, idx, k)): v for k, v in flatov(kw).items()}
def lov(layer, **kw):
    return {("%s.%s" % (layer, k)): v for k, v in flatov(kw).items()}

DRUM_POOLS = [D_KICK] * 4 + [D_SNARE + D_CLAP] * 4 + [D_HAT + D_CYMB] * 4 + [D_PERC + D_TOM] * 4
recipes["XFX"] = [
 {"name": "Drum Kit Per Key", "view": "SMP", "desc": "XFX bank: every key is a different synthesized drum voice with its own pitch, envelope length and repeats.",
  "set": merge(slots_pools(DRUM_POOLS), {"bpm": RI(120, 168)}),
  "fx": [fx("saturation", ["SMP"], drive=0.3, mix=0.35), fx("reverb", ["SMP"], algo="plate", decay=0.3, mix=0.1)],
  "xfx": {"bank": "Drum Keys", "range": [36, 96], "fill": 0.96, "mode": "band", "palette": [
     {"en": only("SMP"), "o": ov("SMP", 0, useSlot=RI(0, 3), semitone=RI(-3, 3), env={"d": R(0.12, 0.4), "s": 0, "rep": P(0, 0, 1)}, level=R(0.8, 0.95))},
     {"en": only("SMP"), "o": ov("SMP", 0, useSlot=RI(4, 7), semitone=RI(-2, 2), env={"d": R(0.08, 0.25), "s": 0, "rep": RI(0, 3)}, pan=R(-0.3, 0.3))},
     {"en": only("SMP"), "o": ov("SMP", 0, useSlot=RI(8, 11), semitone=RI(-2, 5), env={"d": R(0.03, 0.12), "s": 0, "rep": RI(1, 5)}, pan=R(-0.6, 0.6))},
     {"en": only("SMP"), "o": ov("SMP", 0, useSlot=RI(12, 15), semitone=RI(-12, 12), env={"d": R(0.05, 0.3), "s": 0, "rep": RI(0, 4)}, pan=R(-0.8, 0.8))}]}},
 {"name": "Bass Library Per Key", "view": "WT", "desc": "XFX bank: each key is a different bass voice - wavetable growls, FM basses and sub layers with their own filters.",
  "set": merge(osc("WT", 0, table="dnbReese", level=0.8, unison=3), osc("WT", 1, enabled=True, table="subBass", level=0.5, oct=-1), env("WT", 0.002, 0.3, 0.8, 0.15), filt("WT", 0, "moog4pole", 700, 25, 50),
               {"layers.FM.engineEnabled.0": True, "layers.FM.fmAlgorithms.0": 2}, osc("FM", 0, ratio=0.5, level=0.9), osc("FM", 1, ratio=1, level=0.4), env("FM", 0.002, 0.3, 0.7, 0.15)),
  "fx": [fx("saturation", ["WT", "FM"], drive=0.4, mix=0.5)],
  "xfx": {"bank": "Bass Keys", "range": [24, 60], "fill": 1, "mode": "cycle", "palette": [
     {"en": only("WT"), "o": merge(ov("WT", 0, table=P(*BASS_T), unison=RI(1, 5), detune=R(6, 22), filter={"cutoff": RL(300, 1400), "reso": R(10, 50), "on": True}), lov("WT", env={"d": R(0.15, 0.5), "s": R(0.5, 0.95)}))},
     {"en": only("FM"), "o": merge(ov("FM", 1, level=R(0.3, 0.8), ratio=P(1, 2, 3), wave=P("sine", "square"), feedback=R(0, 0.5)), ov("FM", 2, level=R(0.1, 0.4), ratio=P(3, 4, 5)), ov("FM", 0, filter={"type": "diode4pole", "cutoff": RL(500, 1800), "reso": R(15, 45), "on": True}))},
     {"en": only("WT", "FM"), "o": merge(ov("WT", 0, table="subBass", level=0.6, unison=1), ov("FM", 1, level=R(0.3, 0.6), ratio=P(1, 2)))}]}},
]

# ================================================================ NAMED XFX PRESETS (go into the DMT CYMATICS bank, built by the same interpreter with a fixed seed)
AY_SLOTS = [S(n) for n in ["IDM Clicky Kick", "Industrial Kick Slam", "Gated Kick Punch", "IDM Glitch Snare", "DnB Snappy Snare", "Bitcrushed Snare Snap", "Metallic Clap Zap", "IDM Glitch Hat", "Bitcrushed Hat Click", "Closed Hat Tick", "IDM Perc Stutter", "Glitch Perc Blip", "Woodblock Click", "Resonant Perc Ping"]] + [SF("Glitch Stutter Burst"), SF("Digital Artifact Zap")]
def ay(lo, hi, **kw): return merge(ov("SMP", 0, useSlot=RI(lo, hi), **kw))
AYFXTWN = {"name": "AYFXTWN", "cat": "XFX", "seed": 20261110, "view": "SMP",
  "desc": "AYFXTWN - XFX MODE, IDM drums. Every key from C2 to C7 is its own snappy drum voice: the keys climb through kicks, snares, claps and hats, tuned percussion and glitch bursts, and each key has its own pitch, tight decay, level, pan and amp-envelope REPEAT count (1-8 extra hits while held, a few keys roll continuously). Hold a key for a stutter roll; tap it for a one-shot. Built from the synthesized drum set, which loads with the preset.",
  "set": merge(slots_named(AY_SLOTS, pitchKeytrack=0), {"bpm": 140}),
  "fx": [fx("saturation", ["SMP"], drive=0.3, mix=0.35), fx("delay", ["SMP"], time=0.1875, feedback=0.32, mix=0.16), fx("glitch", ["SMP"], bitDepth=13, srReduction=1, stutterOn=False, seqOn=False, mix=0.45), fx("reverb", ["SMP"], algo="plate", decay=0.28, mix=0.1)],
  "xfx": {"bank": "AYFXTWN", "range": [36, 96], "fill": 1, "mode": "band", "palette": [
     {"en": only("SMP"), "o": ay(0, 2, semitone=RI(-3, 3), level=R(0.82, 0.95), pan=R(-0.1, 0.1), env={"a": 0.001, "d": R(0.12, 0.3), "s": 0, "r": 0.05, "rep": P(0, 0, 0, 1, 2)})},
     {"en": only("SMP"), "o": ay(3, 5, semitone=RI(-2, 3), level=R(0.8, 0.92), pan=R(-0.25, 0.25), env={"a": 0.001, "d": R(0.08, 0.2), "s": 0, "r": 0.04, "rep": P(0, 1, 2, 3, 4)})},
     {"en": only("SMP"), "o": ay(6, 9, semitone=RI(-2, 6), level=R(0.7, 0.88), pan=R(-0.6, 0.6), env={"a": 0.001, "d": R(0.03, 0.08), "s": 0, "r": 0.03, "rep": P(2, 3, 4, 6, 8)})},
     {"en": only("SMP"), "o": ay(10, 13, semitone=RI(-12, 12), level=R(0.7, 0.9), pan=R(-0.8, 0.8), reverse=P(False, False, True), env={"a": 0.001, "d": R(0.04, 0.14), "s": 0, "r": 0.04, "rep": P(0, 2, 3, 5)})},
     {"en": only("SMP"), "o": ay(10, 15, semitone=RI(-24, 12), level=R(0.65, 0.85), pan=R(-1, 1), end=R(0.12, 0.55), env={"a": 0.001, "d": R(0.02, 0.06), "s": 0, "r": 0.03, "rep": P(3, 4, 6, 8, -1)})}]}}


ENO_SLOTS = [S(n) for n in ["Choir Ahh", "Choir Ooh", "Synth Choir Ahh Wide", "Synth Choir Ooh Soft", "Synth Choir Minor Dark", "Synth Choir Hollow Fifths", "Synth Choir Cathedral", "Synth Choir Breathy Air", "Synth Choir Rising Swell", "Space Choir Pad", "Warm Pad", "Dark Pad", "Harmonium Drone", "Glass Harmonica", "Analog String Machine", "Cathedral Organ Chord"]]
def eno_entry(wt, fm, smp, slot_lo, slot_hi, tables, fmr):
    return {"en": only("WT", "FM", "SMP"), "o": merge(
      ov("WT", 0, table=P(*tables), unison=RI(4, 8), detune=R(5, 14), width=R(0.7, 1), drift=R(0.1, 0.35), level=wt, pan=R(-0.3, 0.3), filter={"on": True, "type": "statevariable", "cutoff": RL(1200, 5000), "reso": R(0, 10)}),
      ov("WT", 1, enabled=True, table=P(*DRONE_T), oct=P(0, 1, -1), level=R(0.1, 0.35), unison=RI(3, 5), detune=R(5, 12), pan=R(-0.5, 0.5)),
      lov("WT", env={"a": R(1.2, 3), "d": 1.5, "s": R(0.8, 0.95), "r": R(2.5, 3), "rep": 0}),
      ov("FM", 0, level=fm, ratio=1), ov("FM", 1, ratio=P(*fmr), level=R(0.05, 0.22)), ov("FM", 2, ratio=P(2, 3, 5), level=R(0, 0.12)), lov("FM", env={"a": R(1.4, 3), "d": 1.6, "s": R(0.8, 0.95), "r": R(2.5, 3)}),
      ov("SMP", 0, useSlot=RI(slot_lo, slot_hi), level=smp, loop=True, pan=R(-0.4, 0.4), fineTune=RI(-9, 9), env={"a": R(1.5, 3), "d": 1, "s": R(0.85, 0.95), "r": R(2.5, 3), "rep": 0}))}
ENO = {"name": "ENO XFX", "cat": "XFX", "seed": 19781101, "view": "WT",
  "desc": "ENO XFX - XFX MODE, long ambient drones. Every key from C1 to C7 layers three sources at once: a drone wavetable with an octave partner, a quiet FM bell-pad engine and a looped synthesized choir or pad sample, each with its own slow 1-3 second attack and 2.5-3 second release. Four flavours (wavetable-led, FM-led, choir-led, pad-led) are scattered across the keys with different unison, detune, filter and mix, so holding a chord of neighbouring keys gives a slowly shifting generative texture. Hold keys, add the sustain pedal, and let the hall reverb do the rest.",
  "set": merge(osc("WT", 0, table="droneWarmPad", unison=5, detune=9, width=0.9, level=0.6), osc("WT", 1, enabled=True, table="ambientHalo", oct=1, level=0.2, unison=3, detune=8), filt("WT", 0, "statevariable", 2200, 4), env("WT", 2, 1.5, 0.9, 3),
               {"layers.FM.engineEnabled.0": True, "layers.FM.fmAlgorithms.0": 5}, osc("FM", 0, ratio=1, level=0.35), osc("FM", 1, ratio=2, level=0.12), filt("FM", 0, "twopole12db", 3500, 4), env("FM", 2, 1.6, 0.9, 3),
               slots_named(ENO_SLOTS, loop=True), {"bpm": 70, "lfo.lfo1.hz": 0.04, "lfo.lfo2.hz": 0.06}),
  "conn": [["lfo1", "WT:osc1Scan", 28], ["lfo2", "WT:osc1Cutoff", 22]],
  "fx": [fx("chorus", ["WT", "FM", "SMP"], mix=0.3), fx("tape", ["WT", "FM", "SMP"], mix=0.3), fx("reverb", ["WT", "FM", "SMP"], algo="hall", decay=0.93, size=1.5, mix=0.5)],
  "xfx": {"bank": "ENO", "range": [24, 96], "fill": 1, "mode": "random", "palette": [
     eno_entry(R(0.5, 0.75), R(0.08, 0.2), R(0.15, 0.3), 0, 15, DRONE_T, (1, 2, 3)),
     eno_entry(R(0.2, 0.35), R(0.4, 0.65), R(0.15, 0.3), 10, 15, ["ambientGlassPad", "ambientCrystalline", "ambientSineStack", "crystalShimmer"], (1, 1.5, 2, 3)),
     eno_entry(R(0.2, 0.4), R(0.08, 0.2), R(0.55, 0.8), 0, 9, CHOIR_T, (1, 2)),
     eno_entry(R(0.3, 0.5), R(0.1, 0.25), R(0.45, 0.65), 10, 15, ["droneAirDrift", "ambientCloudDrift", "ambientBreath", "ambientMistVeil", "nebulaDrift"], (1, 2, 3))]}}

GLITCH_VARIANTS = [  # (name, [(wave, value fallback, ringMod, sync)])x8, extra per-variant
  ("Triad climb", [("pulse", 0), ("pulse", 4), ("pulse", 7), ("triangle", 12), ("pulse", 16), ("triangle", 12), ("pulse", 7), ("pulse", 4)]),
  ("Minor spiral", [("pulse", 0), ("sawtooth", 3), ("pulse", 7), ("sawtooth", 10), ("triangle", 12), ("sawtooth", 10), ("pulse", 7), ("sawtooth", 3)]),
  ("Octave stutter", [("pulse", 0), ("pulse", 12), ("pulse", 0), ("pulse", 24), ("sawtooth", 0), ("pulse", 12), ("pulse", -12), ("pulse", 12)]),
  ("Noise burst", [("noise", 0), ("pulse", 0), ("noise", 12), ("pulse", 0), ("noise", 0), ("pulse", 7), ("noise", 5), ("pulse", 0)]),
  ("Fifth ladder", [("pulse", 0), ("pulse", 7), ("triangle", 14), ("pulse", 7), ("pulse", 0), ("sawtooth", -5), ("pulse", 2), ("triangle", 9)]),
  ("Chromatic crawl", [("sawtooth", 0), ("sawtooth", 1), ("pulse", 2), ("pulse", 3), ("triangle", 4), ("pulse", 3), ("sawtooth", 2), ("sawtooth", 1)]),
  ("Sync lead", [("sawtooth", 0), ("sawtooth", 5), ("sawtooth", 7), ("sawtooth", 12), ("sawtooth", 0), ("sawtooth", 5), ("sawtooth", 7), ("sawtooth", 19)]),
  ("Ring bell", [("triangle", 0), ("pulse", 7), ("triangle", 12), ("pulse", 19), ("triangle", 12), ("pulse", 7), ("triangle", 0), ("pulse", -7)])]
def glitch_entry(vi):
    name, steps = GLITCH_VARIANTS[vi]
    o = {}
    for i, (w, v) in enumerate(steps):
        o["SID.wtArp.steps.%d.wave" % i] = w
        o["SID.wtArp.steps.%d.value" % i] = SD(v) if vi not in (2, 3) else SD(v)
        o["SID.wtArp.steps.%d.gate" % i] = P(True, True, True, False) if vi == 3 else True
        o["SID.wtArp.steps.%d.ringMod" % i] = (i % 2 == 0) if name == "Ring bell" else False
        o["SID.wtArp.steps.%d.sync" % i] = (i % 2 == 1) if name == "Sync lead" else False
    o["SID.wtArp.speedHz"] = RI(12, 60); o["SID.wtArp.on"] = True; o["SID.wtArp.hardRestart"] = P(True, False)
    o.update(ov("SID", 0, pulseWidth=R(0.1, 0.9), level=R(0.6, 0.85)))
    o.update(ov("SID", 1, fineTune=RI(-12, 12), level=R(0.3, 0.6)))
    o.update({"SID.filter.type": P("sid6581", "sid8580"), "SID.filter.cutoff": RL(500, 6000), "SID.filter.reso": R(5, 55), "SID.filter.on": True})
    o.update(ov("WT", 0, table=P(*GLITCH_T), level=R(0.25, 0.7), oct=P(-1, 0, 0, 1), unison=RI(1, 3), detune=R(6, 20), pan=R(-0.6, 0.6), filter={"on": True, "type": P("bandpass", "highpass", "notch", "moog4pole"), "cutoff": RL(300, 7000), "reso": R(5, 45)}))
    o.update(lov("WT", env={"a": 0.001, "d": R(0.05, 0.5), "s": R(0, 0.5), "r": R(0.05, 0.3), "rep": P(0, 0, 2, 3, 4)}))
    return {"en": only("SID", "WT"), "o": o}
GLITCH = {"name": "Glitch XFX", "cat": "XFX", "seed": 19990909, "view": "SID",
  "desc": "Glitch XFX - XFX MODE, glitch and chip-arp textures. The SID layer's wavetable arp is on for all three voices and every key from C2 to C7 gets one of eight arp variants (triad climb, minor spiral, octave stutter, noise burst, fifth ladder, chromatic crawl, sync lead, ring bell) with its own speed, pulse widths and SID filter, while the Wavetable layer adds a different glitch / digital / metallic table on each key with its own filter and a short, sometimes repeating envelope. Hold a key to hear its arp.",
  "set": merge(osc("SID", 0, enabled=True, wave="pulse", pulseWidth=0.5, level=0.8, arpOn=True), osc("SID", 1, enabled=True, wave="sawtooth", level=0.45, arpOn=True, pan=-0.4), osc("SID", 2, enabled=True, wave="triangle", level=0.4, arpOn=True, oct=1, pan=0.4),
               filt("SID", None, "sid6581", 2400, 22, 20), {"layers.SID.wtArp.on": True, "layers.SID.wtArp.speedHz": 30, "bpm": 150},
               osc("WT", 0, table="glitchStutter", unison=1, level=0.5), filt("WT", 0, "bandpass", 1800, 14), env("WT", 0.001, 0.2, 0.2, 0.1)),
  "wtsteps": {"n": [8, 8], "waves": ["pulse", "triangle", "sawtooth"], "pitches": [0, 4, 7, 12]},
  "fx": [fx("glitch", ["WT", "SID"], bitDepth=11, srReduction=2, stutterOn=True, stutterDivision=0.125, stutterAmount=0.5, seqOn=True, seqRate="1/16", seqIntensity=0.45, mix=0.8), fx("delay", ["WT", "SID"], time=0.1875, feedback=0.38, mix=0.2), fx("reverb", ["WT", "SID"], algo="plate", decay=0.3, mix=0.12)],
  "xfx": {"bank": "Glitch", "range": [36, 96], "fill": 1, "mode": "cycle", "palette": [glitch_entry(i) for i in range(8)]}}

NAMED = [AYFXTWN, ENO, GLITCH]  # Jungle XFX presets removed (Round 20); legacy + new IDM presets are appended by idm_named.extra_named()

def ordered(x):
    if isinstance(x, dict):
        out = {}
        for k, v in x.items():
            if k in ("set", "o", "params") and isinstance(v, dict):
                out[k] = [[a, ordered(b)] for a, b in v.items()]
            else:
                out[k] = ordered(v)
        return out
    if isinstance(x, list): return [ordered(i) for i in x]
    return x

def export():
    import os
    fxd = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fx_defaults.json")))
    import idm_named
    return ordered({"cats": recipes, "named": NAMED + idm_named.extra_named(), "fxDefaults": fxd})

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "recipes.json"
    json.dump(export(), open(out, "w"), separators=(",", ":"))
    n = sum(len(v) for v in recipes.values())
    print("wrote", out, n, "variants in", len(recipes), "categories +", len(NAMED), "named presets")
