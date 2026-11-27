// Interstellar: the C++ stage must reproduce the web AudioWorklet (tools/make_golden_interstellar.js) for the same input and settings.
#include <cstdio>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include "vv/fx_chain.hpp"
#include "vv/json.hpp"
using namespace vv;
static int failures = 0;
int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "tests/golden_is";
    std::ifstream cs(dir + "/cases.txt"); std::string id; int n = 0;
    while (std::getline(cs, id)) {
        if (id.empty()) continue;
        std::ifstream jf(dir + "/" + id + ".json"); std::stringstream ss; ss << jf.rdbuf(); Json j = JsonParser::parse(ss.str());
        const double sr = j.num("sr", 48000), bpm = j.num("bpm", 120); const int frames = (int)j.num("frames", 0); const uint32_t seed = (uint32_t)j.num("seed", 1);
        std::vector<float> g((size_t)frames * 3); { std::ifstream f(dir + "/" + id + ".f32", std::ios::binary); f.read((char*)g.data(), g.size() * 4); }
        InterstellarStage st(sr, seed); FxEnv env; env.sr = sr; env.bpm = bpm; st.configure(*j.get("params"), true, env);
        std::vector<float> in(frames), oL(frames), oR(frames); for (int i = 0; i < frames; i++) in[i] = g[(size_t)i * 3];
        for (int i = 0; i < frames; i += 128) st.process(&in[i], &in[i], &oL[i], &oR[i], 128);
        double eMax = 0, num = 0, den = 0, pk = 0;
        for (int i = 0; i < frames; i++) for (int c = 0; c < 2; c++) { const double a = c ? oR[i] : oL[i], b = g[(size_t)i * 3 + 1 + c]; eMax = std::max(eMax, std::fabs(a - b)); num += (a - b) * (a - b); den += b * b; pk = std::max(pk, std::fabs(b)); }
        const double rel = std::sqrt(num / (den + 1e-30)); const bool ok = rel < 1e-5 && pk > 1e-3;
        printf("%-28s peak %.3f  maxAbsErr %.3e  relRMS %.3e  %s\n", id.c_str(), pk, eMax, rel, ok ? "ok" : "MISMATCH");
        if (!ok) failures++; n++;
    }
    printf("interstellar golden: %d cases, %d failed\n", n, failures); return failures ? 1 : 0;
}
