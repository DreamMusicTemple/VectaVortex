// Golden test set 2: simple effects + voice DSP vs the JavaScript worklets (tools/make_golden2.js).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "vv/fx_basic.hpp"
#include "vv/voice_dsp.hpp"
#include "vv/fx_spectral_granular.hpp"
#include "vv/procedural.hpp"
#include "vv/legacy_reverb.hpp"

struct Case { std::string id, proc; std::map<std::string, double> kv; };
static std::vector<Case> readCases(const std::string& dir) {
    std::ifstream f(dir + "/cases.txt"); std::vector<Case> v; std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line); Case c; ss >> c.id >> c.proc; std::string tok;
        while (ss >> tok) { auto e = tok.find('='); if (e != std::string::npos) c.kv[tok.substr(0, e)] = std::atof(tok.c_str() + e + 1); }
        v.push_back(c);
    }
    return v;
}
static vv::WtTable loadTable(const std::string& dir, const char* name, int nF, int size) {
    vv::WtTable t; t.tableSize = size; std::vector<float> raw((size_t)nF * size);
    std::ifstream b(dir + "/" + name + ".f32", std::ios::binary); b.read((char*)raw.data(), raw.size() * 4);
    for (int f = 0; f < nF; f++) t.frames.emplace_back(raw.begin() + (size_t)f * size, raw.begin() + (size_t)(f + 1) * size);
    return t;
}
struct Lcg { uint32_t s; double next() { s = s * 1664525u + 1013904223u; return (double)s / 4294967296.0; } };

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "tests/golden2";
    auto cases = readCases(dir); int fails = 0;
    if (cases.empty()) { std::printf("no cases in %s\n", dir.c_str()); return 2; }
    for (auto& c : cases) {
        auto& k = c.kv;
        const int frames = (int)k["frames"], nin = (int)k["nin"], nout = (int)k["nout"], noiseUntil = (int)k["noiseUntil"];
        const double sr = k["sr"], amp = k["amp"];
        std::vector<float> ref(frames * nout);
        { std::ifstream b(dir + "/" + c.id + ".f32", std::ios::binary); b.read((char*)ref.data(), ref.size() * 4); if (!b) { std::printf("%-28s MISSING DATA\n", c.id.c_str()); fails++; continue; } }
        std::vector<std::vector<float>> in(std::max(1, nin), std::vector<float>(frames, 0.f));
        for (int ch = 0; ch < nin; ch++) { uint32_t s = (uint32_t)k["seed"] + ch * 7919u; for (int i = 0; i < std::min(noiseUntil, frames); i++) { s = s * 1664525u + 1013904223u; in[ch][i] = (float)(((double)s / 4294967296.0 * 2 - 1) * amp); } }
        std::vector<std::vector<float>> out(2, std::vector<float>(frames, 0.f));
        const int B = 128;
        const std::string& p = c.proc;
        auto blocks = [&](auto&& fn) { for (int i = 0; i < frames; i += B) fn(i); };
        if (p == "eq-processor") {
            vv::EQ e; e.prepare(sr); vv::EQ::Params q; for (int i = 0; i < 4; i++) { q.freq[i] = k["freq" + std::to_string(i + 1)]; q.gain[i] = k["gain" + std::to_string(i + 1)]; }
            blocks([&](int i) { e.process(&in[0][i], &out[0][i], B, q); });
        } else if (p == "resonator-processor") {
            vv::Resonator e; e.prepare(sr); vv::Resonator::Params q; q.freq = k["freq"]; q.resonance = k["resonance"]; q.mix = k["mix"];
            blocks([&](int i) { e.process(&in[0][i], &out[0][i], B, q); });
        } else if (p == "delay-processor") {
            vv::Delay e; e.prepare(sr); vv::Delay::Params q; q.mode = (int)k["mode"]; q.time = k["time"]; q.feedback = k["feedback"]; q.mix = k["mix"];
            blocks([&](int i) { e.process(&in[0][i], &out[0][i], &out[1][i], B, q); });
        } else if (p == "chorus-processor") {
            vv::Chorus e; e.prepare(sr); vv::Chorus::Params q; q.c1 = k["chorus1On"] > 0.5; q.c2 = k["chorus2On"] > 0.5; q.mix = k["mix"];
            blocks([&](int i) { e.process(&in[0][i], &out[0][i], B, q); });
        } else if (p == "panning-processor") {
            vv::Panning e; e.prepare(sr); vv::Panning::Params q; q.width = k["width"]; q.autoPan = k["autoPanOn"] > 0.5; q.rate = k["rate"]; q.shape = (int)k["shape"]; q.mode = (int)k["mode"]; q.distance = k["distance"];
            blocks([&](int i) { e.process(&in[0][i], &out[0][i], &out[1][i], B, q); });
        } else if (p == "band-pan-processor") {
            vv::BandPan e; e.prepare(sr); vv::BandPan::Params q; q.on = k["on"]; q.width = k["width"]; q.depth = k["depth"]; q.speed = k["speed"]; q.phase0 = k["phase0"]; q.decor = k["decor"];
            blocks([&](int i) { e.process(&in[0][i], &in[1][i], &out[0][i], &out[1][i], B, q); });
        } else if (p == "saturation-processor") {
            vv::Saturation e; e.prepare(sr); vv::Saturation::Params q; q.drive = k["drive"]; q.hard = (int)k["hard"]; q.tone = k["tone"]; q.mix = k["mix"]; q.oversample = (int)k["oversample"];
            blocks([&](int i) { e.process(&in[0][i], &out[0][i], B, q); });
        } else if (p == "wavefolder-processor") {
            vv::Wavefolder e; e.prepare(sr); vv::Wavefolder::Params q; q.drive = k["drive"]; q.symmetry = k["symmetry"]; q.mix = k["mix"]; q.oversample = (int)k["oversample"]; q.mode = (int)k["mode"]; q.pwm = k["pwm"];
            blocks([&](int i) { e.process(&in[0][i], &out[0][i], B, q); });
        } else if (p == "tape-processor") {
            vv::Tape e; e.prepare(sr, 777); vv::Tape::Params q; q.wowRate = k["wowRate"]; q.wowDepth = k["wowDepth"]; q.flutterRate = k["flutterRate"]; q.flutterDepth = k["flutterDepth"]; q.dust = k["dust"]; q.crackle = k["crackle"]; q.mix = k["mix"];
            blocks([&](int i) { e.process(&in[0][i], &out[0][i], &out[1][i], B, q); });
        } else if (p == "wavetable-processor") {
            static const vv::WtTable T0 = loadTable(dir, "table0", 8, 512);
            vv::Wavetable w; w.prepare(sr); w.setTable(&T0);
            for (int i = 0; i < frames; i += B) {
                double freq = k["frequency"], pos = k["position"], pw = k["pulseWidth"], po = k["phaseOffset"], se = k["startEnd"], seg = k["segment"]; bool pp = k["pingPong"] > 0.5;
                if (k["sweep"] == 1) { freq = 261.6; pos = (double)i / frames * 1.4 - 0.2; pw = 0.5; po = 0; se = 1; seg = 1; pp = false; }
                if (k["sweep"] == 2) { freq = 261.6; pos = (double)i / frames * 5 - 1; pw = 0.4; po = 0; se = 1; seg = 0.6; pp = true; }
                for (int j = 0; j < B; j++) out[0][i + j] = (float)w.tick(freq, pos, pw, po, se, seg, pp);
            }
        } else if (p == "wavetable-sync-processor") {
            static const vv::WtTable T0 = loadTable(dir, "table0", 8, 512), T1 = loadTable(dir, "table1", 5, 256);
            vv::WavetableSync w; w.prepare(sr); w.setTables(&T0, &T1);
            vv::WavetableSync::Osc a, b2;
            a.freq = k["frequency0"]; a.pos = k["position0"]; a.pw = k["pulseWidth0"]; a.phOff = k["phaseOffset0"]; a.se = k["startEnd0"]; a.seg = k["segment0"]; a.pingPong = k["pingPong0"] > 0.5;
            b2.freq = k["frequency1"]; b2.pos = k["position1"]; b2.pw = k["pulseWidth1"]; b2.phOff = k["phaseOffset1"]; b2.se = k["startEnd1"]; b2.seg = k["segment1"]; b2.pingPong = k["pingPong1"] > 0.5;
            for (int i = 0; i < frames; i++) { double o0, o1; w.tick(a, b2, o0, o1); out[0][i] = (float)o0; out[1][i] = (float)o1; }
        } else if (p == "multi-filter-processor") {
            vv::MultiFilter f; f.prepare(sr);
            for (int i = 0; i < frames; i += B) {
                double cutoff = k["cutoff"], reso = k["resonance"]; int type = (int)k["filterType"];
                if (k.count("base")) { cutoff = k["base"] * std::pow(2.0, 3.0 * i / frames); reso = k["reso"]; type = (int)k["type"]; }
                f.process(&in[0][i], &out[0][i], B, cutoff, reso, type);
            }
        } else if (p == "fm-processor") {
            vv::FMVoice f; f.prepare(sr);
            double r[4], l[4], c[4]; int w[4];
            for (int i = 0; i < 4; i++) { r[i] = k["ratio" + std::to_string(i)]; l[i] = k["level" + std::to_string(i)]; w[i] = (int)vv::jsRound(k["wave" + std::to_string(i)]); c[i] = k["pitchCents" + std::to_string(i)]; }
            const int algo = (int)vv::jsRound(k["algorithm"]);
            for (int i = 0; i < frames; i++) out[0][i] = (float)f.tick(k["baseFreq"], r, l, w, k["feedback0"], c, algo);
        } else if (p == "spectralfx-processor") {
            vv::SpectralFX e; e.prepare(sr);
            for (int i = 0; i < frames; i += B) {
                vv::SpectralFX::Params q;
                if (k.count("freezeAt")) { q.freeze = i >= frames * 0.3; q.blur = k["blur"]; q.shift = k["shift"]; q.threshold = k["threshold"]; q.spread = k["spread"]; q.mix = k["mix"]; }
                else { q.freeze = k["freeze"] > 0.5; q.blur = k["blur"]; q.shift = k["shift"]; q.threshold = k["threshold"]; q.spread = k["spread"]; q.mix = k["mix"]; }
                e.process(&in[0][i], &out[0][i], &out[1][i], B, q);
            }
        } else if (p == "granular-processor") {
            vv::Granular e; e.prepare(sr, 777);
            for (int i = 0; i < frames; i += B) {
                vv::Granular::Params q; q.grainSize = k["grainSize"]; q.density = k["density"]; q.pitch = k["pitch"]; q.spread = k["spread"]; q.panCenter = k["panCenter"]; q.envShape = k["envShape"]; q.followerAmount = k["followerAmount"]; q.mix = k["mix"];
                if (k.count("sweepEnv")) q.envShape = std::floor((double)i / frames * 4);
                e.process(&in[0][i], &out[0][i], &out[1][i], B, q);
            }
        } else if (p == "reverb-processor") {
            vv::LegacyReverb e; e.prepare(sr, 777); if (k.count("hit")) { e.tap(0.8); e.stroke(0.6); }
            vv::LegacyReverb::Params q; q.algo = (int)k["algo"]; q.decay = k["decay"]; q.size = k["size"]; q.predelay = k["predelay"]; q.mix = k["mix"]; q.shimA = k["shimA"]; q.shimB = k["shimB"];
            blocks([&](int i) { e.process(&in[0][i], &out[0][i], B, q); });
        } else if (p == "procedural-processor") {
            vv::Procedural e; e.prepare(sr, k.count("pseed") ? (uint32_t)k["pseed"] : 12345u);
            for (int i = 0; i < frames; i += B) {
                vv::Procedural::Params q; q.frequency = k["frequency"]; q.model = (int)k["model"]; q.bright = k["bright"]; q.damp = k["damp"]; q.body = k["body"]; q.chaos = k["chaos"]; q.stiffness = k.count("stiffness") ? k["stiffness"] : 0; q.excite = k.count("excite") ? (int)k["excite"] : 0; q.bore = k.count("bore") ? k["bore"] : 0;
                if (k.count("switch")) q.model = (int)std::floor((double)i / frames * 4);
                e.process(&out[0][i], B, q, nullptr, &in[0][i]);   // the harness feeds its noise burst on the worklet's (coupling) input, which the web now mixes into the exciter
            }
        } else if (p == "glitch-processor") {
            vv::Glitch g; g.prepare(sr, 777);
            for (int i = 0; i < frames; i += B) {
                vv::Glitch::Params q;
                if (k.count("switchAt")) { q.bitDepth = (int)k["bit"]; q.stutterInterval = 4000; q.stutterAmount = 0.5; if (i >= frames / 2) { q.modeA = (int)k["modeA"]; q.modeB = (int)k["modeB"]; q.modeC = (int)k["modeC"]; } }
                else { q.bitDepth = (int)vv::jsRound(k["bitDepth"]); q.srReduction = k["srReduction"]; q.stutterOn = k["stutterOn"] > 0.5; q.stutterInterval = (int)vv::jsRound(k["stutterIntervalSamples"]); q.stutterAmount = k["stutterAmount"]; q.modeA = (int)k["modeA"]; q.modeB = (int)k["modeB"]; q.modeC = (int)k["modeC"]; }
                g.process(&in[0][i], &out[0][i], B, q);
            }
        } else { std::printf("%-28s unknown processor %s\n", c.id.c_str(), p.c_str()); fails++; continue; }
        double peak = 1e-9, maxErr = 0, se = 0, sr2 = 0;
        for (int i = 0; i < frames; i++) for (int ch = 0; ch < nout; ch++) {
            const double r = ref[i * nout + ch], o = out[ch][i];
            peak = std::max(peak, std::fabs(r)); maxErr = std::max(maxErr, std::fabs(r - o)); se += (r - o) * (r - o); sr2 += r * r;
        }
        const double relMax = maxErr / peak, relRms = std::sqrt(se / (sr2 + 1e-30));
        const bool ok = relMax < 1e-4 && relRms < 2e-5;
        std::printf("%-28s peak %.3f  max err %.2e  rms err %.2e  %s\n", c.id.c_str(), peak, relMax, relRms, ok ? "OK" : "FAIL");
        if (!ok) fails++;
    }
    std::printf("%d of %zu cases failed\n", fails, cases.size());
    return fails ? 1 : 0;
}
