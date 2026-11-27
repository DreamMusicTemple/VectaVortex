// sid_render: offline SID -> WAV renderer built on vv::C64SidPlayer (libsidplayfp).  usage: sid_render in.sid out.wav subtune seconds rate bits(16|24|32)
#include "vv/c64_sid_player.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
using namespace vv;
static void w32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); } static void w16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }
int main(int argc, char** argv) {
    if (argc < 7) { std::fprintf(stderr, "usage: sid_render in.sid out.wav subtune seconds rate bits\n"); return 2; }
    const double sr = atof(argv[5]); const int bits = atoi(argv[6]), sub = atoi(argv[3]); const double secs = atof(argv[4]);
    C64SidPlayer p; C64TuneInfo ti = p.load(argv[1], sr); if (!ti.ok) { std::fprintf(stderr, "load failed: %s\n", ti.error.c_str()); return 3; }
    if (sub > 0 && !p.selectSubtune(sub)) { std::fprintf(stderr, "bad subtune\n"); return 4; }
    std::printf("title=%s author=%s released=%s format=%s songs=%u model=%s clock=%s size=%u\n", ti.title.c_str(), ti.author.c_str(), ti.released.c_str(), ti.format.c_str(), ti.songs, ti.model.c_str(), ti.clock.c_str(), ti.fileSize);
    const size_t n = (size_t)(secs * sr); std::vector<float> x(n); for (size_t i = 0; i < n; i += 4096) p.render(x.data() + i, (unsigned)std::min<size_t>(4096, n - i));
    const unsigned bytes = bits == 16 ? 2 : bits == 24 ? 3 : 4; FILE* f = fopen(argv[2], "wb"); fwrite("RIFF", 1, 4, f); w32(f, (uint32_t)(36 + n * bytes)); fwrite("WAVEfmt ", 1, 8, f); w32(f, 16); w16(f, bits == 32 ? 3 : 1); w16(f, 1); w32(f, (uint32_t)sr); w32(f, (uint32_t)(sr * bytes)); w16(f, bytes); w16(f, bytes * 8); fwrite("data", 1, 4, f); w32(f, (uint32_t)(n * bytes));
    for (size_t i = 0; i < n; ++i) { float v = std::fmax(-1.f, std::fmin(1.f, x[i])); if (bits == 16) { int16_t s = (int16_t)lrintf(v * 32767); fwrite(&s, 2, 1, f); } else if (bits == 24) { int32_t s = lrintf(v * 8388607); fwrite(&s, 3, 1, f); } else fwrite(&x[i], 4, 1, f); }
    fclose(f); return 0;
}
