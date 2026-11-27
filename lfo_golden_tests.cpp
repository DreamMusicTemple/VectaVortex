// The LFO shapes (incl. Morph, Morph 2, Organic, Stroll, stairs, smoothing) must match the web app's lfoWaveValue.
#include "vv/synth.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
int main(int argc, char** argv) {
    std::ifstream f(argc > 1 ? argv[1] : "tests/golden_lfo.txt"); std::string line; int n = 0, bad = 0; double worst = 0;
    while (std::getline(f, line)) {
        std::istringstream ss(line); std::string w; double morph, sm, ph, ref; int bip; ss >> w >> morph >> sm >> bip >> ph >> ref;
        vv::LfoCfg l; l.waveform = w; l.phaseOffset = 0.07; l.bipolar = bip; l.smoothing = sm; l.morph = morph;
        const double v = vv::lfoEval(l, ph, 0, 0), e = std::fabs(v - ref); worst = std::fmax(worst, e); ++n; if (e > 1e-9) { if (bad < 5) std::printf("MISMATCH %s morph=%g sm=%g ph=%g: %g vs %g\n", w.c_str(), morph, sm, ph, v, ref); ++bad; }
    }
    std::printf("lfo shapes: %d values, worst error %.3g -> %s\n", n, worst, bad ? "FAILED" : "OK"); return bad ? 1 : 0;
}
