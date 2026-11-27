#include "vv/compressor.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
static std::vector<float> rd(const std::string& p) { FILE* f = fopen(p.c_str(), "rb"); if (!f) return {}; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); std::vector<float> v(n / 4); fread(v.data(), 4, v.size(), f); fclose(f); return v; }
int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "tests/ref";
    auto in = rd(dir + "/comp_in.f32"), ref = rd(dir + "/comp_out.f32");
    if (in.empty() || in.size() != ref.size()) { printf("missing reference\n"); return 1; }
    vv::Compressor c; c.prepare(48000); vv::Compressor::Params p; p.thresholdDb = -8; p.kneeDb = 3; p.ratio = 20; p.attack = 0.003f; p.release = 0.15f;
    std::vector<float> out(in.size()); c.process(in.data(), nullptr, out.data(), nullptr, (int)in.size() / 128 * 128, p);
    double mx = 0, se = 0, sr = 0; int n = (int)in.size() / 128 * 128;
    for (int i = 0; i < n; i++) { double e = out[i] - ref[i]; mx = std::max(mx, std::fabs(e)); se += e * e; sr += (double)ref[i] * ref[i]; }
    double rel = std::sqrt(se / (sr + 1e-30));
    printf("compressor: maxAbsErr=%.3e relRMS=%.3e\n", mx, rel);
    return (mx < 2e-3 && rel < 1e-3) ? 0 : 1;
}
