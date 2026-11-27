// Compares the C++ DSP against reference output rendered from the JavaScript AudioWorklets (tools/make_golden.js).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "vv/adv_reverb.hpp"
#include "vv/phaser_flanger.hpp"

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

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "tests/golden";
    int fails = 0; auto cases = readCases(dir);
    if (cases.empty()) { std::printf("no golden cases found in %s\n", dir.c_str()); return 2; }
    for (auto& c : cases) {
        const int frames = (int)c.kv["frames"], noiseUntil = (int)c.kv["noiseUntil"]; const double sr = c.kv["sr"], amp = c.kv["amp"];
        std::vector<float> ref(frames * 2);
        { std::ifstream b(dir + "/" + c.id + ".f32", std::ios::binary); b.read((char*)ref.data(), ref.size() * 4); if (!b) { std::printf("%-28s MISSING DATA\n", c.id.c_str()); fails++; continue; } }
        // identical input to the JS harness: LCG noise burst then silence
        std::vector<float> in(frames); uint32_t s = (uint32_t)c.kv["seed"];
        for (int i = 0; i < frames; i++) { s = s * 1664525u + 1013904223u; double v = ((double)s / 4294967296.0 * 2 - 1) * amp; in[i] = i < noiseUntil ? (float)v : 0.0f; }
        std::vector<float> L(frames), R(frames);
        const int B = 128;
        if (c.proc == "phaser-flanger-processor") {
            vv::PhaserFlanger pf; pf.prepare(sr); vv::PhaserFlanger::Params p;
            p.mode = (int)c.kv["mode"]; p.rate = c.kv["rate"]; p.depth = c.kv["depth"]; p.feedback = c.kv["feedback"]; p.mix = c.kv["mix"]; p.stages = (int)c.kv["stages"];
            for (int i = 0; i < frames; i += B) pf.process(&in[i], &L[i], &R[i], B, p);
        } else {
            vv::AdvReverb rv; rv.prepare(sr); vv::AdvReverb::Params p;
            p.algo = (int)c.kv["algo"]; p.decay = c.kv["decay"]; p.size = c.kv["size"]; p.predelay = c.kv["predelay"]; p.mix = c.kv["mix"]; p.mod = c.kv["mod"]; p.colour = c.kv["colour"];
            for (int i = 0; i < frames; i += B) rv.process(&in[i], &L[i], &R[i], B, p);
        }
        double peak = 1e-9, maxErr = 0, se = 0, sr2 = 0;
        for (int i = 0; i < frames; i++) for (int ch = 0; ch < 2; ch++) {
            const double r = ref[i * 2 + ch], o = ch ? R[i] : L[i];
            peak = std::max(peak, std::fabs(r)); maxErr = std::max(maxErr, std::fabs(r - o)); se += (r - o) * (r - o); sr2 += r * r;
        }
        const double relMax = maxErr / peak, relRms = std::sqrt(se / (sr2 + 1e-30));
        const bool ok = relMax < 1e-3 && relRms < 1e-4;
        std::printf("%-28s peak %.3f  max err %.2e (rel)  rms err %.2e (rel)  %s\n", c.id.c_str(), peak, relMax, relRms, ok ? "OK" : "FAIL");
        if (!ok) fails++;
    }
    std::printf("%d of %zu cases failed\n", fails, cases.size());
    return fails ? 1 : 0;
}
