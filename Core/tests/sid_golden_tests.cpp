// The C++ SID chip must reproduce the web app's sid-processor (golden_sid/*.f32, interleaved 3 voices).
#include "vv/sid.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
using namespace vv;
int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "golden_sid"; std::ifstream cs(dir + "/cases.txt"); std::string line; int fails = 0;
    while (std::getline(cs, line)) {
        std::istringstream ss(line); std::string id; int frames, gateOff; ss >> id >> frames >> gateOff; SidVoiceParams p[3];
        for (int v = 0; v < 3; ++v) { int w, r, s; ss >> p[v].freq >> w >> p[v].pw >> r >> s >> p[v].atk >> p[v].dec >> p[v].sus >> p[v].rel; p[v].wave = w; p[v].ring = r; p[v].sync = s; }
        std::ifstream f(dir + "/" + id + ".f32", std::ios::binary); std::vector<float> g((size_t)frames * 3); f.read((char*)g.data(), g.size() * 4);
        SidChip chip; std::vector<float> o0(128), o1(128), o2(128); float* outs[3] = { o0.data(), o1.data(), o2.data() }; double maxErr = 0, peak = 0;
        for (int pos = 0; pos < frames; pos += 128) {
            for (int v = 0; v < 3; ++v) p[v].gate = pos < gateOff;
            chip.process(p, 48000.0, outs, 128);
            for (int i = 0; i < 128; ++i) for (int v = 0; v < 3; ++v) { const double e = std::fabs((double)outs[v][i] - g[(size_t)(pos + i) * 3 + v]); if (e > maxErr) maxErr = e; peak = std::fmax(peak, std::fabs(g[(size_t)(pos + i) * 3 + v])); }
        }
        const bool ok = maxErr < 1e-5 && peak > 0.05; std::printf("sid %-20s maxErr %.3g peak %.3f %s\n", id.c_str(), maxErr, peak, ok ? "OK" : "FAIL"); if (!ok) ++fails;
    }
    std::printf(fails ? "SID TESTS FAILED\n" : "SID golden tests passed\n"); return fails ? 1 : 0;
}
