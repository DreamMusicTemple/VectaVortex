// Null test: the C++ Dx7Core must reproduce the web app's JavaScript Dx7Core render (golden_dx7/*.bin), plus SysEx round trips.
#include "vv/Dx7Core.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
using namespace vv::dx7;
static std::vector<uint8_t> slurp(const std::string& f) { std::ifstream i(f, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(i)), std::istreambuf_iterator<char>()); }
int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "golden_dx7"; int fails = 0;
    std::ifstream idx(dir + "/index.txt"); std::string line; const double sr = 48000;
    while (std::getline(idx, line)) {
        std::istringstream ss(line); int n, note, vel, blocks, off; double cents; ss >> n >> note >> vel >> cents >> blocks >> off;
        auto patch = slurp(dir + "/case" + std::to_string(n) + ".patch"); auto gold = slurp(dir + "/case" + std::to_string(n) + ".bin");
        const float* g = reinterpret_cast<const float*>(gold.data());
        Voice v(sr); v.noteOn(patch.data(), note, vel, 12345);
        double maxErr = 0, peak = 0; float out[N];
        for (int b = 0; b < blocks; ++b) {
            if (b == off) v.noteOff();
            v.renderBlock(out, 440.0 * std::pow(2.0, (note - 69) / 12.0), cents);
            for (int i = 0; i < N; ++i) { double e = std::fabs(double(out[i]) - g[b * N + i]); if (e > maxErr) maxErr = e; peak = std::fmax(peak, std::fabs(g[b * N + i])); }
        }
        const bool ok = maxErr <= 2e-4 && peak > 1e-4;
        std::printf("case %d note %d vel %d: maxErr %.3g peak %.3f %s\n", n, note, vel, maxErr, peak, ok ? "OK" : "FAIL");
        if (!ok) ++fails;
    }
    // SysEx round trip on the bundled ROM bank
    auto rom = slurp(dir + "/../../../Assets/DX7/ROM1A.syx");
    auto res = parseSysex(rom.data(), rom.size());
    if (res.voices.size() != 32) { std::printf("FAIL: ROM1A parsed %zu voices\n", res.voices.size()); ++fails; }
    else {
        uint8_t packed[128], re[PATCH_SIZE]; packVoice(res.voices[0].data(), packed); unpackVoice(packed, re);
        if (std::memcmp(re, res.voices[0].data(), 155) != 0) { std::printf("FAIL: pack/unpack mismatch\n"); ++fails; }
        auto syx = exportSingleSysex(res.voices[5].data(), 0); auto r2 = parseSysex(syx.data(), syx.size());
        if (r2.voices.size() != 1 || std::memcmp(r2.voices[0].data(), res.voices[5].data(), 155) != 0 || !r2.warnings.empty()) { std::printf("FAIL: single sysex round trip\n"); ++fails; }
        std::printf("sysex: 32 voices, name[0]='%s'\n", getName(res.voices[0].data()).c_str());
    }
    std::printf(fails ? "DX7 TESTS FAILED\n" : "DX7 tests passed\n");
    return fails ? 1 : 0;
}
