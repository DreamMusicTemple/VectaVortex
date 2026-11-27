// Formant-filter vowel tables (F1/F2/F3 in Hz, adult male / adult female) — same data as FORMANT_VOWELS in the web app
// (Peterson & Barney 1952 averages; the last five are textbook approximations).
#pragma once
#include <string>
namespace vv {
struct FormantVowel { const char* id; double male[3]; double female[3]; };
inline const FormantVowel* formantVowels(int& n) {
    static const FormantVowel t[] = {
        {"a", {730, 1090, 2440}, {850, 1220, 2810}}, {"e", {530, 1840, 2480}, {610, 2330, 2990}}, {"i", {270, 2290, 3010}, {310, 2790, 3310}},
        {"o", {570, 840, 2410}, {590, 920, 2710}}, {"u", {300, 870, 2240}, {370, 950, 2670}}, {"ae", {660, 1720, 2410}, {860, 2050, 2850}},
        {"er", {490, 1350, 1690}, {600, 1480, 1980}}, {"schwa", {500, 1500, 2500}, {560, 1650, 2850}}, {"ih", {390, 1990, 2550}, {430, 2480, 3070}},
        {"uh", {440, 1020, 2240}, {470, 1160, 2680}}, {"oh", {590, 880, 2540}, {610, 1030, 2860}}, {"turn", {640, 1190, 2390}, {760, 1400, 2780}},
        {"y", {270, 1900, 2200}, {300, 2200, 2700}}, {"oe", {370, 1500, 2300}, {420, 1900, 2650}}};
    n = (int)(sizeof(t) / sizeof(t[0])); return t;
}
// formantRatiosFor(table, vowel): returns {F2/F1, F3/F1}; 'classic' (or empty) keeps the original fixed 1.74 / 3.71.
inline void formantRatiosFor(const std::string& table, const std::string& vowel, double& r2, double& r3) {
    r2 = 1.74; r3 = 3.71; if (table.empty() || table == "classic") return;
    int n; const FormantVowel* t = formantVowels(n); const FormantVowel* v = &t[0];
    for (int i = 0; i < n; i++) if (vowel == t[i].id) v = &t[i];
    const double* f = table == "female" ? v->female : v->male; r2 = f[1] / f[0]; r3 = f[2] / f[0];
}
}  // namespace vv
