// Chord analyzer (web detectChordLabel / updateChordAnalyzer): names the chord formed by the currently sounding (non-arp) notes.
// The label only changes when a NEW note starts and the set matches a known shape; otherwise the last label stays (as in the web app).
#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace vv {
struct ChordAnalyzer {
    struct Tpl { const char* name; int n; int iv[5]; };
    static const Tpl* templates(int& count) {
        static const Tpl T[] = {
            {"Dominant 9", 5, {0,2,4,7,10}}, {"Major 9", 5, {0,2,4,7,11}}, {"Minor 9", 5, {0,2,3,7,10}}, {"Minor-Major 9", 5, {0,2,3,7,11}},
            {"6/9", 5, {0,2,4,7,9}}, {"Minor 6/9", 5, {0,2,3,7,9}},
            {"Dominant 7", 4, {0,4,7,10}}, {"Major 7", 4, {0,4,7,11}}, {"Minor 7", 4, {0,3,7,10}}, {"Minor-Major 7", 4, {0,3,7,11}},
            {"Diminished 7", 4, {0,3,6,9}}, {"Half-Diminished 7 (m7\xE2\x99\xAD" "5)", 4, {0,3,6,10}}, {"Augmented 7 (7\xE2\x99\xAF" "5)", 4, {0,4,8,10}},
            {"Dominant 7\xE2\x99\xAD" "5", 4, {0,4,6,10}}, {"Dominant 7sus4", 4, {0,5,7,10}}, {"Dominant 7sus2", 4, {0,2,7,10}},
            {"Major 6", 4, {0,4,7,9}}, {"Minor 6", 4, {0,3,7,9}}, {"Add9", 4, {0,2,4,7}}, {"Minor Add9", 4, {0,2,3,7}},
            {"Major 7\xE2\x99\xAF" "5", 4, {0,4,8,11}}, {"Major 7\xE2\x99\xAD" "5", 4, {0,4,6,11}},
            {"Major", 3, {0,4,7}}, {"Minor", 3, {0,3,7}}, {"Diminished", 3, {0,3,6}}, {"Augmented", 3, {0,4,8}}, {"Sus2", 3, {0,2,7}}, {"Sus4", 3, {0,5,7}},
        };
        count = (int)(sizeof(T) / sizeof(T[0])); return T;
    }
    static const char* noteName(int pc) { static const char* N[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}; return N[((pc % 12) + 12) % 12]; }
    static const char* dyad(int iv) {
        static const char* D[12] = {"", "(minor 2nd)", "(major 2nd)", "(minor 3rd)", "(major 3rd)", "(perfect 4th)", "(tritone)", "(perfect 5th)", "(minor 6th)", "(major 6th)", "(minor 7th)", "(major 7th)"};
        return D[((iv % 12) + 12) % 12];
    }
    /// notes: bit n of mask[n>>6] = MIDI note n sounding. Returns "" when nothing recognisable.
    static std::string detect(const uint64_t mask[2]) {
        bool seen[12] = {false}; std::vector<int> pcs;   // pitch classes in first-seen (ascending note) order
        for (int n = 0; n < 128; n++) if ((mask[n >> 6] >> (n & 63)) & 1) { const int p = n % 12; if (!seen[p]) { seen[p] = true; pcs.push_back(p); } }
        if (pcs.size() < 2) return "";
        int cnt; const Tpl* T = templates(cnt);
        for (int root : pcs) {
            bool rel[12] = {false}; int size = 0;
            for (int p : pcs) { const int r = (p - root + 12) % 12; if (!rel[r]) { rel[r] = true; size++; } }
            for (int t = 0; t < cnt; t++) {
                if (T[t].n != size) continue;
                bool ok = true; for (int k = 0; k < T[t].n; k++) if (!rel[T[t].iv[k]]) { ok = false; break; }
                if (ok) return std::string(noteName(root)) + " " + T[t].name;
            }
        }
        if (pcs.size() == 2) return std::string(noteName(pcs[0])) + " " + dyad(pcs[1] - pcs[0]);
        return "";
    }
    // stateful (UI thread): re-analyses only when a note starts that was not sounding before
    std::string last; uint64_t prev[2] = {0, 0};
    const std::string& update(const uint64_t mask[2]) {
        const bool added = (mask[0] & ~prev[0]) || (mask[1] & ~prev[1]);
        prev[0] = mask[0]; prev[1] = mask[1];
        if (added) { std::string l = detect(mask); if (!l.empty() && l != last) last = l; }
        return last;
    }
};
}
