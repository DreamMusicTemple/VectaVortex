// Generated from the web ARP_ALGOS table (id, name, group, description, parameters it uses besides the core set). Keep in sync with Core/include/vv/arp_adv.hpp.
import Foundation

struct ArpAlgoInfo: Identifiable { let id: String; let name: String; let group: String; let desc: String; let keys: [String]; var index: Int { ArpAdvInfo.all.firstIndex { $0.id == id } ?? 0 } }
enum ArpAdvInfo {
    static let all: [ArpAlgoInfo] = [
        ArpAlgoInfo(id: "fractal", name: "Fractal", group: "Generative note ordering", desc: "Recursively develops a motif, repeating it at larger scales with variations.", keys: ["complexity", "variation", "cycleVar", "octSpread"]),
        ArpAlgoInfo(id: "fibonacci", name: "Fibonacci", group: "Generative note ordering", desc: "Moves through the notes by Fibonacci-sized steps.", keys: ["variation", "cycleVar", "octSpread"]),
        ArpAlgoInfo(id: "brownian", name: "Brownian Motion", group: "Generative note ordering", desc: "Wanders up and down the notes, with occasional larger jumps.", keys: ["complexity", "variation", "cycleVar"]),
        ArpAlgoInfo(id: "kaleido", name: "Kaleidoscope", group: "Generative note ordering", desc: "Plays groups of notes forwards then mirrored, rotating each group.", keys: ["cycleVar", "octSpread"]),
        ArpAlgoInfo(id: "cascade", name: "Cascade", group: "Generative note ordering", desc: "Cascades downward through the notes, dropping an octave each pass.", keys: ["cycleVar", "octSpread"]),
        ArpAlgoInfo(id: "callresp", name: "Call & Response", group: "Generative note ordering", desc: "Alternates a generated phrase with its reversed, shifted answer.", keys: ["variation", "cycleVar"]),
        ArpAlgoInfo(id: "intjump", name: "Interval Jump", group: "Generative note ordering", desc: "Jumps through the notes by a fixed interval (set by Complexity).", keys: ["complexity", "cycleVar"]),
        ArpAlgoInfo(id: "pedal", name: "Pedal Tone", group: "Generative note ordering", desc: "Alternates melody notes with a repeated root note.", keys: ["variation", "cycleVar", "octSpread"]),
        ArpAlgoInfo(id: "scalewalk", name: "Scale Walk", group: "Generative note ordering", desc: "Walks up and down a musical scale from the lowest held note.", keys: ["scale", "variation", "octSpread"]),
        ArpAlgoInfo(id: "inversion", name: "Chord Inversion", group: "Generative note ordering", desc: "Cycles through the inversions of the held chord.", keys: ["cycleVar", "octSpread"]),
        ArpAlgoInfo(id: "euclid", name: "Euclidean", group: "Mathematical and generative", desc: "Distributes note triggers evenly using Euclidean rhythm mathematics (Density = pulses).", keys: ["density", "cycleVar"]),
        ArpAlgoInfo(id: "markov", name: "Markov Chain", group: "Mathematical and generative", desc: "Chooses each note from probabilities that depend on the previous note.", keys: ["complexity", "variation", "cycleVar"]),
        ArpAlgoInfo(id: "cellular", name: "Cellular Automata", group: "Mathematical and generative", desc: "Reads notes from an evolving cellular automaton (Complexity chooses the rule).", keys: ["complexity", "cycleVar"]),
        ArpAlgoInfo(id: "chaos", name: "Chaos", group: "Mathematical and generative", desc: "Follows a chaotic logistic-map function (Complexity = chaos amount).", keys: ["complexity", "cycleVar"]),
        ArpAlgoInfo(id: "golden", name: "Golden Ratio", group: "Mathematical and generative", desc: "Picks notes and timing displacement from golden-ratio fractions.", keys: ["cycleVar", "octSpread"]),
        ArpAlgoInfo(id: "probabilistic", name: "Probability", group: "Mathematical and generative", desc: "Each note has its own likelihood of playing (Density scales it).", keys: ["density", "variation", "cycleVar"]),
        ArpAlgoInfo(id: "neural", name: "Neural", group: "Mathematical and generative", desc: "A tiny seeded network predicts each note from the last three (no AI service).", keys: ["complexity", "variation", "cycleVar"]),
        ArpAlgoInfo(id: "mutation", name: "Random Mutation", group: "Mathematical and generative", desc: "Ascending pattern whose notes are replaced over successive cycles (Mutation = chance).", keys: ["mutation", "cycleVar"]),
        ArpAlgoInfo(id: "morph", name: "Morph", group: "Mathematical and generative", desc: "Gradually morphs from an ascending pattern to a random one and back.", keys: ["variation", "cycleVar"]),
        ArpAlgoInfo(id: "timewarp", name: "Time Warp", group: "Mathematical and generative", desc: "Slowly bends gate length and timing while staying on the grid.", keys: ["cycleVar"]),
        ArpAlgoInfo(id: "ratchetA", name: "Ratchet", group: "Rhythmic", desc: "Repeats each note several times before advancing (Ratchet Amount).", keys: ["ratchet"]),
        ArpAlgoInfo(id: "stutter", name: "Stutter", group: "Rhythmic", desc: "Rapid decaying bursts of repeated notes.", keys: ["density"]),
        ArpAlgoInfo(id: "offbeat", name: "Offbeat", group: "Rhythmic", desc: "Favours the off-beat positions.", keys: ["density", "rhythmDisp"]),
        ArpAlgoInfo(id: "syncopated", name: "Syncopated", group: "Rhythmic", desc: "Displaced rhythmic accents on a syncopated mask.", keys: ["density", "accent"]),
        ArpAlgoInfo(id: "polyrhythm", name: "Polyrhythm", group: "Rhythmic", desc: "Lower notes play every 3 steps, higher notes every 4.", keys: ["density"]),
        ArpAlgoInfo(id: "triplet", name: "Triplet Shuffle", group: "Rhythmic", desc: "Triplet-based shuffle inside each step.", keys: ["groove"]),
        ArpAlgoInfo(id: "groove", name: "Groove", group: "Rhythmic", desc: "Swing and timing variation with accented beats (Groove amount).", keys: ["groove", "accent"]),
        ArpAlgoInfo(id: "burst", name: "Burst", group: "Rhythmic", desc: "Clusters of three quick notes separated by gaps (Density shortens the gap).", keys: ["density"]),
        ArpAlgoInfo(id: "revgate", name: "Reverse Gate", group: "Rhythmic", desc: "Alternates short and long note durations.", keys: ["gateVar"]),
        ArpAlgoInfo(id: "ghost", name: "Ghost Notes", group: "Rhythmic", desc: "Main notes with quieter ghost notes in between (Density = ghost chance).", keys: ["density"]),
        ArpAlgoInfo(id: "broken", name: "Broken Chord", group: "Harmonic", desc: "Unconventional chord fragments that avoid immediate repeats.", keys: ["variation", "cycleVar"]),
        ArpAlgoInfo(id: "harmonic", name: "Harmonic", group: "Harmonic", desc: "Adds a harmonic interval (fifth, octave, twelfth or third) to each note.", keys: ["variation", "octSpread"]),
        ArpAlgoInfo(id: "voicelead", name: "Voice Leading", group: "Harmonic", desc: "Moves to the nearest chord tone each step, minimising pitch movement.", keys: ["variation", "cycleVar"]),
        ArpAlgoInfo(id: "octjump", name: "Octave Jump", group: "Harmonic", desc: "Ascending pattern with octave jumps up and down (Octave Spread adds more).", keys: ["octSpread"]),
        ArpAlgoInfo(id: "intseq", name: "Interval Sequence", group: "Harmonic", desc: "Builds melodies from stacked interval patterns (Complexity picks the pattern).", keys: ["complexity", "cycleVar"]),
    ]
    static let core = ["probability", "patLen", "rotation", "humanise", "velVar", "gateVar", "skipProb"]
    /// key, label, min, max, step, isPercent
    static let params: [(String, String, Double, Double, Double, Bool)] = [
        ("probability", "Probability", 0, 1, 0.01, true), ("density", "Note Density", 0, 1, 0.01, true), ("complexity", "Complexity", 0, 1, 0.01, true), ("mutation", "Mutation", 0, 1, 0.01, true), ("variation", "Variation", 0, 1, 0.01, true),
        ("randomness", "Randomness", 0, 1, 0.01, true), ("humanise", "Humanisation", 0, 1, 0.01, true), ("octSpread", "Octave Spread", 0, 1, 0.01, true), ("ratchet", "Ratchet Amount", 0, 1, 0.01, true), ("velVar", "Velocity Var.", 0, 1, 0.01, true),
        ("accent", "Accent", 0, 1, 0.01, true), ("gateVar", "Gate Variation", 0, 1, 0.01, true), ("groove", "Groove", 0, 1, 0.01, true), ("patLen", "Pattern Length", 2, 64, 1, false), ("cycleVar", "Cycle Variation", 0, 1, 0.01, true),
        ("rotation", "Seq. Rotation", 0, 15, 1, false), ("skipProb", "Note Skip", 0, 1, 0.01, true), ("rhythmDisp", "Rhythm Displace", 0, 1, 0.01, true)]
    static let scales = ["major", "minor", "dorian", "pentatonic", "blues", "chromatic"]
    static let defaults: [String: Double] = ["seed": 1234, "probability": 1, "density": 0.6, "complexity": 0.5, "mutation": 0.2, "variation": 0, "randomness": 0, "humanise": 0, "octSpread": 0, "ratchet": 0, "velVar": 0, "accent": 0.3, "gateVar": 0, "groove": 0.5, "patLen": 16, "cycleVar": 0, "rotation": 0, "skipProb": 0, "rhythmDisp": 0]
}
