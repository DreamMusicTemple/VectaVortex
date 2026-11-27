// Plays a reference scenario (see tools/ref_render.js) through the C++ engine and compares with the real app's audio.
// usage: vv_ref_compare <scenario-base-path (without .json)> <wavetable-dir> [tolRel=1e-3] [wavetable-less flag]
#include "vv/synth.hpp"
#include "vv/assets.hpp"
#include <cstdio>
#include <cstdlib>
using namespace vv;
int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: vv_ref_compare <base> <wtdir> [tol]\n"); return 2; }
    std::string base = argv[1], wtdir = argv[2]; std::string refBase = base; { size_t sl = base.find_last_of("/"); refBase = (sl == std::string::npos ? "" : base.substr(0, sl + 1)) + "ref/" + base.substr(sl == std::string::npos ? 0 : sl + 1); } double tol = argc > 3 ? atof(argv[3]) : 1e-3;
    Json sc = JsonParser::parse(readTextFile(base + ".json"));
    auto preset = parsePresetText(readTextFile(refBase + ".resolved.json"));
    std::vector<unsigned char> refb; if (!readFile(refBase + ".ref.f32", refb)) { printf("no reference\n"); return 2; }
    const double sr = sc.num("sr", 48000); const int frames = (int)(refb.size() / 8);   // reference file = all L samples, then all R samples
    const float* ref = (const float*)refb.data();
    Synth s; s.setSafetyCeiling(false);   // goldens were rendered by the JS chain without the final safety ceiling
    const Json* st = sc.get("settings");
    if (st) { s.masterVolume = st->num("masterVolume", 0.8); s.masterTone = st->num("masterTone", 0); s.wtFmMix = st->num("wtFmMix", 0.5); }
    s.prepare(sr);
    Json man = JsonParser::parse(readTextFile(wtdir + "/manifest.json"));
    for (auto& m : man.a) { std::string n = m.str("name"); auto t = loadWavetableFile(wtdir + "/" + n + ".vvwt"); if (t) s.setWavetable(n, t); }
    { const char* sd = getenv("VV_SAMPLES"); std::string dir = sd ? sd : wtdir + "/../Samples"; Json bm = JsonParser::parse(readTextFile(dir + "/../builtin_samples.json"));
      for (auto& m : bm.a) { auto b = loadWavFile(dir + "/" + m.str("file")); if (b) s.setSample(m.str("name"), b); } }
    if (st && st->has("sampleMix")) s.setSampleMix(true, st->num("sampleMix", 0));
    for (int i = 0; i < 16; i++) { auto b = loadWavFile(refBase + ".smp" + std::to_string(i) + ".wav"); if (b && i < (int)preset->layers[L_SMP].oscs.size()) s.setSample(preset->layers[L_SMP].oscs[i].name, b); }   // exact audio the browser played
    s.seedRandom(777); s.seedNoise(777);
    s.tickSeconds = sc.boolean("ticks", false) ? 0.1 : 1e9;   // the reference only runs the web matrix tick when the scenario asks for it
    if (st && st->has("modwheel")) s.modwheel = st->num("modwheel", 0);
    if (st && st->has("mpe")) s.mpeEnabled = st->boolean("mpe", false);
    s.setPreset(preset);
    struct E { long frame; bool on; int mask; int note; double vel; bool legato; PLocks pl; int midi[3] = {-1, 0, 0}; }; std::vector<E> evs;
    if (auto e = sc.get("events")) for (auto& j : e->a) {
        E x; x.frame = (long)(j.num("t", 0) * sr / 128) * 128; x.on = j.boolean("on", false); x.mask = 0; x.note = (int)j.num("note", 60); x.vel = j.num("vel", 0.8); x.legato = j.boolean("legato", false);
        if (auto l = j.get("layers")) for (auto& k : l->a) for (int i = 0; i < kNumLayers; i++) if (k.s == kLayerKeys[i]) x.mask |= 1 << i;
        if (auto p = j.get("plocks")) { if (p->has("position")) { x.pl.hasPosition = true; x.pl.position = p->num("position", 0); } if (p->has("cutoff")) { x.pl.hasCutoff = true; x.pl.cutoff = p->num("cutoff", 0); } if (p->has("level")) { x.pl.hasLevel = true; x.pl.level = p->num("level", 1); }
            if (p->has("samplePosition")) { x.pl.hasSamplePosition = true; x.pl.samplePosition = p->num("samplePosition", 0); } if (p->has("sampleEndPosition")) { x.pl.hasSampleEnd = true; x.pl.sampleEndPosition = p->num("sampleEndPosition", 1); }
            if (p->has("samplePan")) { x.pl.hasSamplePan = true; x.pl.samplePan = p->num("samplePan", 0); } if (p->has("sampleReverse")) { x.pl.hasSampleReverse = true; x.pl.sampleReverse = p->boolean("sampleReverse", false); } }
        if (auto m = j.get("midi")) { x.midi[0] = (int)m->a[0].asNum(); x.midi[1] = (int)m->a[1].asNum(); x.midi[2] = (int)m->a[2].asNum(); }
        evs.push_back(x);
    }
    std::vector<float> L(frames), R(frames);
    for (int f = 0; f < frames; f += 128) {
        for (auto& e : evs) if (e.frame == f && e.midi[0] >= 0) {   // raw MIDI (MPE tests): same routing as the web app's handleMidiMessage
            const int type = e.midi[0] & 0xF0, ch = e.midi[0] & 15, d1 = e.midi[1], d2 = e.midi[2];
            if (type == 0x90 && d2 > 0) { s.mpeNoteOn(ch, d1); s.noteOn(e.mask, d1, d2 / 127.0); }
            else if (type == 0x80 || (type == 0x90 && d2 == 0)) { s.mpeNoteOff(ch, d1); s.noteOff(e.mask, d1); }
            else if (type == 0xB0 && d1 == 74) s.mpeController(ch, 1, d2 / 127.0);
            else if (type == 0xD0) s.mpeController(ch, 2, d1 / 127.0);
            else if (type == 0xE0) s.mpeController(ch, 0, (((d2 << 7) | d1) - 8192) / 8192.0);
        }
        for (auto& e : evs) if (e.frame == f && e.midi[0] < 0) { if (e.on) s.noteOn(e.mask, e.note, e.vel, &e.pl, e.legato); else s.noteOff(e.mask, e.note); }
        s.process(L.data() + f, R.data() + f, std::min(128, frames - f));
    }
    // compareUntil: ignore audio after this time (the browser harness keeps finished voices connected, which only matters when a level offset keeps them audible)
    const int cmpN = sc.has("compareUntil") ? std::min(frames, (int)(sc.num("compareUntil", 1e9) * sr)) : frames;
    double se = 0, sr2 = 0, mx = 0, peakRef = 0; int firstBad = -1;
    for (int ch = 0; ch < 2; ch++) for (int i = 0; i < cmpN; i++) {
        const double r = ref[ch * frames + i], m = ch ? R[i] : L[i]; const double e = m - r; se += e * e; sr2 += r * r; mx = std::max(mx, std::fabs(e)); peakRef = std::max(peakRef, std::fabs(r));
        if (firstBad < 0 && std::fabs(e) > 1e-3 + 1e-2 * std::fabs(r)) firstBad = i; }
    double rel = std::sqrt(se / (sr2 + 1e-30));
    printf("%-28s frames=%d peakRef=%.4f maxAbsErr=%.3e relRMS=%.3e firstBad=%d\n", sc.str("name").c_str(), frames, peakRef, mx, rel, firstBad);
    if (getenv("VV_DUMP")) { FILE* f = fopen(getenv("VV_DUMP"), "wb"); fwrite(L.data(), 4, frames, f); fwrite(R.data(), 4, frames, f); fclose(f); }
    return rel < tol ? 0 : 1;
}
