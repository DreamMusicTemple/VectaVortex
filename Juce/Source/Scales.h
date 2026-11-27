// The web app's keyboard scale table (id, label, intervals). Index 0 = "Off". Shared by the keyboard (highlight / scale lock) and the sequencer's RANDOMISE.
#pragma once
#include <juce_core/juce_core.h>
namespace vv {
struct KbScale { const char* id; const char* label; int n; int iv[8]; };
inline const KbScale* kbScales(int& count) {
    static const KbScale t[] = {
        { "", "Off", 0, {} },
        { "major", "Major", 7, { 0, 2, 4, 5, 7, 9, 11 } }, { "fSharpMajor", "F# Major", 7, { 1, 3, 5, 6, 8, 10, 11 } }, { "minor", "Natural minor", 7, { 0, 2, 3, 5, 7, 8, 10 } },
        { "harmonicMinor", "Harmonic minor", 7, { 0, 2, 3, 5, 7, 8, 11 } }, { "dorian", "Dorian", 7, { 0, 2, 3, 5, 7, 9, 10 } }, { "mixolydian", "Mixolydian", 7, { 0, 2, 4, 5, 7, 9, 10 } },
        { "majorPent", "Major pentatonic", 5, { 0, 2, 4, 7, 9 } }, { "minorPent", "Minor pentatonic", 5, { 0, 3, 5, 7, 10 } }, { "blues", "Blues", 6, { 0, 3, 5, 6, 7, 10 } },
        { "majorBlues", "Major blues", 6, { 0, 2, 3, 4, 7, 9 } }, { "jazz", "Jazz (bebop dominant)", 8, { 0, 2, 4, 5, 7, 9, 10, 11 } }, { "altered", "Jazz (altered)", 7, { 0, 1, 3, 4, 6, 8, 10 } },
        { "japanese", "Japanese (Hirajoshi)", 5, { 0, 2, 3, 7, 8 } }, { "kumoi", "Japanese (Kumoi)", 5, { 0, 2, 3, 7, 9 } }, { "inSen", "Japanese (In Sen)", 5, { 0, 1, 5, 7, 10 } },
        { "chineseGong", "Chinese (Gong)", 5, { 0, 4, 6, 7, 11 } }, { "chineseShang", "Chinese (Shang)", 5, { 0, 2, 5, 7, 10 } }, { "spanish", "Spanish (Phrygian dominant)", 7, { 0, 1, 4, 5, 7, 8, 10 } },
        { "egyptian", "Egyptian", 5, { 0, 2, 5, 7, 10 } }, { "indonesian", "Indonesian (Pelog-ish)", 5, { 0, 1, 3, 7, 8 } }, { "filipino", "Filipino (Kulintang-ish)", 5, { 0, 2, 3, 7, 9 } } };
    count = (int) (sizeof(t) / sizeof(t[0])); return t;
}
inline bool kbInScale(int idx, int root, int note) { int c; auto* t = kbScales(c); if (idx <= 0 || idx >= c) return false; const int rel = ((note - root) % 12 + 12) % 12; for (int i = 0; i < t[idx].n; ++i) if (t[idx].iv[i] == rel) return true; return false; }
}
