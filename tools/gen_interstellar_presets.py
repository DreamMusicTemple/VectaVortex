#!/usr/bin/env python3
"""Writes the INTERSTELLAR presets:
   Assets/Interstellar/presets.json            - stage presets (full parameter sets) for the in-panel preset menu on every platform
   Assets/Presets/INTERSTELLAR - <Cat> - <Name>.json - full patches (source sound + Interstellar stage) for the preset browser
Run from the repo root:  python3 tools/gen_interstellar_presets.py
"""
import json, copy, os

DEFAULTS = dict(source="live", freeze=0, capture=0, captureLen=4, span=8, tonal=0.5,
    band1=0, band2=0, band3=0, band4=0, band5=0, band6=0,
    segments=4, segSync=0, segDiv="1/1", segTime=4, segOrder="forward", stretch=8, bloom=0.5,
    density=0.5, life=6, pitchUp=12, upProb=0.4, pitchDown=12, downProb=0.3, body=0.7, particles=0.8, spread=0.8,
    snap="key", scale="auto", key=-1, resonance=0.3, ring=3, strum=0.3, resTune="segment",
    evolve=0.4, drift=0.4, space=0.5, size=0.6, decay=0.6, predelay=0.03, damp=0.4, revMod=0.4, width=0.8, shimmer=0.2, revFreeze=0,
    character=0, mix=1, level=0, quality="normal")

# name, category, source table, source env (a, r), notes, overrides
P = [
 ("First Light", "Pad", "droneWarmPad", (0.8, 2.5), "Balanced starting point: warm, slow, gently moving.",
    dict(character=0, stretch=8, density=0.5, space=0.5)),
 ("Aurora Choir", "Pad", "formantAEIOU", (1.2, 3.0), "A drifting vowel choir that rises and falls with the key.",
    dict(character=0.75, stretch=12, tonal=0.6, density=0.6, pitchUp=12, upProb=0.5, pitchDown=7, downProb=0.25, resonance=0.35, ring=4, strum=0.4, space=0.6, decay=0.65, shimmer=0.3, evolve=0.5)),
 ("Event Horizon", "Pad", "droneSubChoir", (2.0, 4.0), "Slow, heavy and dark: everything falls toward a deep centre.",
    dict(character=0.5, stretch=30, bloom=0.8, density=0.4, pitchUp=0, upProb=0, pitchDown=24, downProb=0.7, body=0.8, resonance=0.2, resTune="fifths", space=0.75, decay=0.85, damp=0.6, size=0.85, evolve=0.3)),
 ("Glass Cathedral", "Pad", "glassHarmonica", (0.9, 3.5), "Bell-bright harmonics ringing in a vast stone space.",
    dict(character=1, tonal=0.9, density=0.5, pitchUp=19, upProb=0.6, resonance=0.6, ring=6, resTune="key", strum=0.5, space=0.7, decay=0.8, shimmer=0.4, size=0.9)),
 ("Tidal Shimmer", "Pad", "ensembleShimmer", (0.8, 2.5), "Segments wash back and forth like a tide, with octave halos.",
    dict(character=0.25, stretch=6, density=0.7, upProb=0.7, segOrder="pingpong", segments=6, segTime=3, space=0.65, shimmer=0.8, decay=0.7, evolve=0.6, drift=0.6)),
 ("Dead Star Drone", "Pad", "droneSubChoir", (2.5, 5.0), "Near-frozen, enormous and dim. A single held breath.",
    dict(character=0.5, stretch=50, bloom=1, tonal=0.4, density=0.3, pitchUp=0, upProb=0, downProb=0.5, body=1, particles=0.4, resonance=0.1, decay=0.95, size=1, damp=0.7, span=16)),
 ("Nebula Bloom", "Pad", "nebulaDrift", (1.5, 3.5), "Colour opening slowly in every direction.",
    dict(stretch=16, bloom=0.9, density=0.8, pitchUp=7, upProb=0.5, pitchDown=5, downProb=0.3, space=0.6, shimmer=0.35, width=1, evolve=0.7)),
 ("Stardust Veil", "Pad", "crystalShimmer", (0.6, 2.5), "A fine curtain of high particles over a faint body.",
    dict(character=0.25, tonal=0.8, density=1, particles=1, life=2.5, pitchUp=24, upProb=0.8, snap="fifths", body=0.3, space=0.5, shimmer=0.6, decay=0.55, drift=0.8)),
 ("Solar Wind", "FX", "droneAirDrift", (0.5, 2.0), "Segments jump unpredictably; resonators strum through them.",
    dict(segOrder="random", segments=8, segTime=1.5, stretch=4, density=0.6, evolve=0.9, drift=0.7, resonance=0.4, strum=0.8, ring=2, space=0.5)),
 ("Cosmic Strings", "Pad", "glassHarmonica", (0.7, 3.0), "Mostly resonators: the source rings like a harp of light.",
    dict(resonance=1, ring=8, resTune="key", strum=0.9, body=0.3, particles=0.4, density=0.3, tonal=0.9, character=1, space=0.6, decay=0.75)),
 ("Whale Song", "Pad", "droneSubChoir", (1.5, 4.0), "Long falling voices in deep water.",
    dict(character=0.75, stretch=20, bloom=0.7, pitchUp=0, upProb=0.1, pitchDown=12, downProb=0.8, space=0.8, decay=0.8, shimmer=0.1, size=0.9, width=1)),
 ("Ice Planet", "Pad", "crystalShimmer", (0.5, 2.0), "Hard, clear, brittle highs under a frozen sky.",
    dict(character=1, tonal=1, bloom=0.3, stretch=3, density=0.4, pitchUp=24, upProb=0.5, damp=0.1, space=0.55, shimmer=0.5)),
 ("Moonlit Lake", "Pad", "droneWarmPad", (1.5, 3.5), "Soft and still, with a slow ripple.",
    dict(character=0.5, stretch=10, density=0.5, upProb=0.3, downProb=0.3, resonance=0.25, ring=5, space=0.7, decay=0.75, damp=0.5, evolve=0.3)),
 ("Orbit Loop", "FX", "droneWarmPad", (0.3, 1.5), "Tempo-locked quarter-note chops of the pad, circling.",
    dict(segSync=1, segDiv="1/4", segments=8, segOrder="forward", stretch=2, bloom=0.2, density=0.4, resonance=0.3, strum=0.6, space=0.4, decay=0.4, evolve=0.2)),
 ("Pulsar Gate", "FX", "glassHarmonica", (0.2, 1.0), "Fast eighth-note ping-pong gating of the harmonic core.",
    dict(segSync=1, segDiv="1/8", segments=16, segOrder="pingpong", stretch=1.5, bloom=0.1, tonal=0.3, density=0.3, space=0.35, decay=0.35, evolve=0.1, drift=0.2)),
 ("Gravity Well", "Pad", "droneSubChoir", (2.0, 4.5), "Every particle is pulled down; the space gets bigger as it falls.",
    dict(pitchUp=0, upProb=0, pitchDown=24, downProb=1, stretch=25, body=0.5, particles=0.9, density=0.8, life=10, character=0.5, space=0.8, decay=0.9)),
 ("Light Years", "Pad", "nebulaDrift", (2.0, 5.0), "Very slow. Very long. Everything changes and nothing hurries.",
    dict(stretch=40, bloom=1, density=0.7, upProb=0.5, downProb=0.5, life=14, space=0.85, decay=0.95, size=1, shimmer=0.5, evolve=0.8)),
 ("Silk Nebula", "Pad", "ensembleShimmer", (1.2, 3.0), "Smooth, vocal and wide, with almost no edges.",
    dict(character=0.75, bloom=0.6, tonal=0.35, density=0.5, stretch=9, space=0.55, decay=0.6, shimmer=0.2, width=1, resonance=0.15)),
 ("Singing Satellites", "Pad", "formantAEIOU", (0.8, 3.0), "Resonators sing the harmony the source is playing.",
    dict(resonance=0.8, ring=3.5, resTune="segment", strum=0.7, density=0.5, upProb=0.5, pitchDown=5, downProb=0.2, character=1, tonal=0.7, space=0.6)),
 ("Deep Field", "Pad", "nebulaDrift", (2.0, 5.0), "The big one: Lush quality, everything on, everything drifting.",
    dict(quality="lush", density=1, stretch=18, bloom=0.8, evolve=0.9, drift=0.9, upProb=0.4, downProb=0.4, space=0.8, decay=0.9, shimmer=0.35, size=0.95, width=1, life=12)),
 ("Wishing Star", "Pad", "crystalShimmer", (0.5, 2.5), "Rising fifths and octaves, like sparks leaving a fire.",
    dict(character=0.25, pitchUp=19, upProb=0.9, snap="fifths", density=0.6, life=4, stretch=7, shimmer=0.9, space=0.7, decay=0.7)),
 ("Cathedral of Dust", "Pad", "droneWarmPad", (1.0, 3.0), "Capture mode: records 6 seconds, then lives inside it.",
    dict(source="capture", captureLen=6, stretch=14, bloom=0.8, tonal=0.6, character=0.75, space=0.7, decay=0.8)),
 ("Cryo Sleep", "Pad", "droneAirDrift", (3.0, 5.0), "One 16-second segment, stretched until it barely moves.",
    dict(stretch=50, span=16, segments=1, bloom=1, character=0.5, density=0.2, space=0.7, decay=0.85, evolve=0.2)),
 ("Eclipse", "Pad", "droneSubChoir", (1.8, 4.0), "Dark chords with open-fifth resonators ringing underneath.",
    dict(character=0.5, stretch=14, density=0.5, pitchDown=12, downProb=0.6, resonance=0.5, resTune="fifths", ring=6, space=0.75, decay=0.85, damp=0.7)),
]

def stage(over):
    p = copy.deepcopy(DEFAULTS); p.update(over); return p

def build_patch(name, cat, table, env, over, base):
    j = copy.deepcopy(base)
    wt = j["layers"]["WT"]; wt["oscs"][0]["table"] = table; wt["oscs"][0]["unison"] = 3; wt["oscs"][0]["detune"] = 14; wt["oscs"][0]["width"] = 0.6
    wt["oscs"][0]["level"] = 0.8
    wt["env"] = {"a": env[0], "d": 1.0, "s": 0.85, "r": env[1]}
    j["fxStages"] = [{"type": "interstellar", "enabled": True, "params": stage(over), "sends": {"WT": True, "FM": False, "SMP": False, "PRO": False}}]
    return j

if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
    base = json.load(open(os.path.join(root, "Assets", "default_preset.json")))
    out = {"defaults": DEFAULTS, "presets": [{"name": n, "category": c, "notes": notes, "params": stage(o)} for (n, c, t, e, notes, o) in P]}
    json.dump(out, open(os.path.join(root, "Assets", "Interstellar", "presets.json"), "w"), indent=1)
    for old in os.listdir(os.path.join(root, "Assets", "Presets")):
        if old.startswith("INTERSTELLAR - "): os.remove(os.path.join(root, "Assets", "Presets", old))
    for (n, c, t, e, notes, o) in P:
        json.dump(build_patch(n, c, t, e, o, base), open(os.path.join(root, "Assets", "Presets", "INTERSTELLAR - %s - %s.json" % (c, n)), "w"))
    print(len(P), "presets written")
