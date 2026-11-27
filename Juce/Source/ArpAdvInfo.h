// Generated from Apple/Shared/ArpAdvInfo.swift (which mirrors the web ARP_ALGOS table). ADVANCED arpeggiator algorithm list + parameter table for the JUCE panel.
#pragma once
#include <juce_core/juce_core.h>
namespace vv { namespace arpadv {
struct Algo { const char* id; const char* name; const char* group; const char* desc; juce::StringArray keys; };
struct Param { const char* key; const char* label; double lo, hi, step, def; bool percent; };
inline const std::vector<Algo>& algos() { static const std::vector<Algo> a = {
    { "fractal", "Fractal", "Generative note ordering", "Recursively develops a motif, repeating it at larger scales with variations.", { "complexity", "variation", "cycleVar", "octSpread" } },
    { "fibonacci", "Fibonacci", "Generative note ordering", "Moves through the notes by Fibonacci-sized steps.", { "variation", "cycleVar", "octSpread" } },
    { "brownian", "Brownian Motion", "Generative note ordering", "Wanders up and down the notes, with occasional larger jumps.", { "complexity", "variation", "cycleVar" } },
    { "kaleido", "Kaleidoscope", "Generative note ordering", "Plays groups of notes forwards then mirrored, rotating each group.", { "cycleVar", "octSpread" } },
    { "cascade", "Cascade", "Generative note ordering", "Cascades downward through the notes, dropping an octave each pass.", { "cycleVar", "octSpread" } },
    { "callresp", "Call & Response", "Generative note ordering", "Alternates a generated phrase with its reversed, shifted answer.", { "variation", "cycleVar" } },
    { "intjump", "Interval Jump", "Generative note ordering", "Jumps through the notes by a fixed interval (set by Complexity).", { "complexity", "cycleVar" } },
    { "pedal", "Pedal Tone", "Generative note ordering", "Alternates melody notes with a repeated root note.", { "variation", "cycleVar", "octSpread" } },
    { "scalewalk", "Scale Walk", "Generative note ordering", "Walks up and down a musical scale from the lowest held note.", { "scale", "variation", "octSpread" } },
    { "inversion", "Chord Inversion", "Generative note ordering", "Cycles through the inversions of the held chord.", { "cycleVar", "octSpread" } },
    { "euclid", "Euclidean", "Mathematical and generative", "Distributes note triggers evenly using Euclidean rhythm mathematics (Density = pulses).", { "density", "cycleVar" } },
    { "markov", "Markov Chain", "Mathematical and generative", "Chooses each note from probabilities that depend on the previous note.", { "complexity", "variation", "cycleVar" } },
    { "cellular", "Cellular Automata", "Mathematical and generative", "Reads notes from an evolving cellular automaton (Complexity chooses the rule).", { "complexity", "cycleVar" } },
    { "chaos", "Chaos", "Mathematical and generative", "Follows a chaotic logistic-map function (Complexity = chaos amount).", { "complexity", "cycleVar" } },
    { "golden", "Golden Ratio", "Mathematical and generative", "Picks notes and timing displacement from golden-ratio fractions.", { "cycleVar", "octSpread" } },
    { "probabilistic", "Probability", "Mathematical and generative", "Each note has its own likelihood of playing (Density scales it).", { "density", "variation", "cycleVar" } },
    { "neural", "Neural", "Mathematical and generative", "A tiny seeded network predicts each note from the last three (no AI service).", { "complexity", "variation", "cycleVar" } },
    { "mutation", "Random Mutation", "Mathematical and generative", "Ascending pattern whose notes are replaced over successive cycles (Mutation = chance).", { "mutation", "cycleVar" } },
    { "morph", "Morph", "Mathematical and generative", "Gradually morphs from an ascending pattern to a random one and back.", { "variation", "cycleVar" } },
    { "timewarp", "Time Warp", "Mathematical and generative", "Slowly bends gate length and timing while staying on the grid.", { "cycleVar" } },
    { "ratchetA", "Ratchet", "Rhythmic", "Repeats each note several times before advancing (Ratchet Amount).", { "ratchet" } },
    { "stutter", "Stutter", "Rhythmic", "Rapid decaying bursts of repeated notes.", { "density" } },
    { "offbeat", "Offbeat", "Rhythmic", "Favours the off-beat positions.", { "density", "rhythmDisp" } },
    { "syncopated", "Syncopated", "Rhythmic", "Displaced rhythmic accents on a syncopated mask.", { "density", "accent" } },
    { "polyrhythm", "Polyrhythm", "Rhythmic", "Lower notes play every 3 steps, higher notes every 4.", { "density" } },
    { "triplet", "Triplet Shuffle", "Rhythmic", "Triplet-based shuffle inside each step.", { "groove" } },
    { "groove", "Groove", "Rhythmic", "Swing and timing variation with accented beats (Groove amount).", { "groove", "accent" } },
    { "burst", "Burst", "Rhythmic", "Clusters of three quick notes separated by gaps (Density shortens the gap).", { "density" } },
    { "revgate", "Reverse Gate", "Rhythmic", "Alternates short and long note durations.", { "gateVar" } },
    { "ghost", "Ghost Notes", "Rhythmic", "Main notes with quieter ghost notes in between (Density = ghost chance).", { "density" } },
    { "broken", "Broken Chord", "Harmonic", "Unconventional chord fragments that avoid immediate repeats.", { "variation", "cycleVar" } },
    { "harmonic", "Harmonic", "Harmonic", "Adds a harmonic interval (fifth, octave, twelfth or third) to each note.", { "variation", "octSpread" } },
    { "voicelead", "Voice Leading", "Harmonic", "Moves to the nearest chord tone each step, minimising pitch movement.", { "variation", "cycleVar" } },
    { "octjump", "Octave Jump", "Harmonic", "Ascending pattern with octave jumps up and down (Octave Spread adds more).", { "octSpread" } },
    { "intseq", "Interval Sequence", "Harmonic", "Builds melodies from stacked interval patterns (Complexity picks the pattern).", { "complexity", "cycleVar" } },
}; return a; }
inline const juce::StringArray& coreKeys() { static const juce::StringArray c = { "probability", "patLen", "rotation", "humanise", "velVar", "gateVar", "skipProb" }; return c; }
inline const std::vector<Param>& params() { static const std::vector<Param> p = {
    { "probability", "Probability", 0, 1, 0.01, 1, true },
    { "density", "Note Density", 0, 1, 0.01, 0.6, true },
    { "complexity", "Complexity", 0, 1, 0.01, 0.5, true },
    { "mutation", "Mutation", 0, 1, 0.01, 0.2, true },
    { "variation", "Variation", 0, 1, 0.01, 0, true },
    { "randomness", "Randomness", 0, 1, 0.01, 0, true },
    { "humanise", "Humanisation", 0, 1, 0.01, 0, true },
    { "octSpread", "Octave Spread", 0, 1, 0.01, 0, true },
    { "ratchet", "Ratchet Amount", 0, 1, 0.01, 0, true },
    { "velVar", "Velocity Var.", 0, 1, 0.01, 0, true },
    { "accent", "Accent", 0, 1, 0.01, 0.3, true },
    { "gateVar", "Gate Variation", 0, 1, 0.01, 0, true },
    { "groove", "Groove", 0, 1, 0.01, 0.5, true },
    { "patLen", "Pattern Length", 2, 64, 1, 16, false },
    { "cycleVar", "Cycle Variation", 0, 1, 0.01, 0, true },
    { "rotation", "Seq. Rotation", 0, 15, 1, 0, false },
    { "skipProb", "Note Skip", 0, 1, 0.01, 0, true },
    { "rhythmDisp", "Rhythm Displace", 0, 1, 0.01, 0, true },
}; return p; }
inline const juce::StringArray& scales() { static const juce::StringArray s{ "major", "minor", "dorian", "pentatonic", "blues", "chromatic" }; return s; }
} }
