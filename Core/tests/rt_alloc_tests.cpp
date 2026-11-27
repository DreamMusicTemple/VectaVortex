// Real-time safety check: counts heap allocations (operator new / malloc via a global hook) made inside vv_engine_render()
// while notes, arp, sequencer and note churn run. Steady-state rendering must allocate nothing; note starts are reported separately.
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <new>
#include <dirent.h>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include "vv_engine.h"
#include "vv/assets.hpp"
using namespace vv;
#ifndef VV_ASSETS
#define VV_ASSETS "../../Assets"
#endif
static thread_local bool g_count = false;
static std::atomic<long> g_allocs{0}, g_bytes{0};
static void note(size_t n) { if (g_count) { g_allocs++; g_bytes += (long)n; } }
void* operator new(size_t n) { note(n); if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](size_t n) { note(n); if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

struct Counter { long a0, b0; Counter() { a0 = g_allocs; b0 = g_bytes; g_count = true; } ~Counter() { g_count = false; } long allocs() const { return g_allocs - a0; } long bytes() const { return g_bytes - b0; } };

int main(int argc, char**) {
    const std::string A = VV_ASSETS; int fails = 0;
    { Counter c; int* p = new int[16]; p[0] = 1; delete[] p; std::string big(200, (char)(argc + 100)); if (big.size() != 200) return 2; if (c.allocs() < 1) { printf("FAIL: allocation hook does not see operator new (%ld)\n", c.allocs()); return 1; } }   // prove the counter works
    VVEngine* e = vv_engine_create(48000);
    DIR* d = opendir((A + "/Wavetables").c_str());
    if (d) { while (dirent* de = readdir(d)) { std::string f = de->d_name; if (f.size() > 5 && f.substr(f.size() - 5) == ".vvwt") { std::vector<unsigned char> b; readFile(A + "/Wavetables/" + f, b); vv_engine_load_wavetable(e, f.substr(0, f.size() - 5).c_str(), b.data(), b.size()); } } closedir(d); }
    Json bm = JsonParser::parse(readTextFile(A + "/builtin_samples.json"));
    for (auto& m : bm.a) { std::vector<unsigned char> b; if (readFile(A + "/Samples/" + m.str("file"), b)) vv_engine_load_sample_wav(e, m.str("name").c_str(), b.data(), b.size()); }
    std::vector<std::string> files = { A + "/default_preset.json" };
    d = opendir((A + "/Presets").c_str());
    if (d) { while (dirent* de = readdir(d)) { std::string f = de->d_name; if (f.size() > 5 && f.substr(f.size() - 5) == ".json") files.push_back(A + "/Presets/" + f); } closedir(d); }
    std::sort(files.begin(), files.end());
    std::vector<float> L(512), R(512);
    long adopt = 0, worstSteady = 0, worstChurn = 0, totalSteady = 0, totalChurn = 0;
    for (auto& f : files) {
        const std::string txt = readTextFile(f);
        if (vv_engine_load_preset_json(e, txt.c_str(), txt.size()) != 1) { printf("FAIL load %s\n", f.c_str()); fails++; continue; }
        { Counter c; vv_engine_render(e, L.data(), R.data(), 512); adopt = std::max(adopt, c.allocs()); }   // first render after a preset load adopts the staged preset (known non-steady-state)
        for (int k = 0; k < 3; k++) vv_engine_render(e, L.data(), R.data(), 512);   // the staged preset is adopted by the first render (not steady state)
        const uint8_t on[3][3] = {{0x90, 57, 100}, {0x90, 60, 100}, {0x90, 64, 100}}, off[3][3] = {{0x80, 57, 0}, {0x80, 60, 0}, {0x80, 64, 0}};
        for (auto& m : on) vv_engine_midi(e, m, 3);
        for (int k = 0; k < 40; k++) vv_engine_render(e, L.data(), R.data(), 512);  // warm-up (voices start, buffers touched)
        long steady;
        { Counter c; for (int k = 0; k < 200; k++) vv_engine_render(e, L.data(), R.data(), 512); steady = c.allocs(); }   // held chord, ~2 s
        long churn;
        { Counter c; for (int r = 0; r < 20; r++) { for (auto& m : on) vv_engine_midi(e, m, 3); for (int k = 0; k < 8; k++) vv_engine_render(e, L.data(), R.data(), 512); for (auto& m : off) vv_engine_midi(e, m, 3); for (int k = 0; k < 8; k++) vv_engine_render(e, L.data(), R.data(), 512); } churn = c.allocs(); }
        // arp + sequencer running
        vv_engine_set_arp(e, 1, 0); vv_engine_start_sequencer(e, 1);
        for (auto& m : on) vv_engine_midi(e, m, 3);
        for (int k = 0; k < 40; k++) vv_engine_render(e, L.data(), R.data(), 512);
        long arp; { Counter c; for (int k = 0; k < 200; k++) vv_engine_render(e, L.data(), R.data(), 512); arp = c.allocs(); }
        vv_engine_set_arp(e, 0, 0); vv_engine_start_sequencer(e, 0); vv_engine_all_notes_off(e);
        for (int k = 0; k < 60; k++) vv_engine_render(e, L.data(), R.data(), 512);
        printf("%-30s steady=%ld  note-churn=%ld  arp+seq=%ld\n", f.substr(f.find_last_of('/') + 1).c_str(), steady, churn, arp);
        worstSteady = std::max(worstSteady, std::max(steady, arp)); worstChurn = std::max(worstChurn, churn); totalSteady += steady + arp; totalChurn += churn;
    }
    printf("render-thread allocations: steady/arp worst=%ld total=%ld; note-start churn worst=%ld total=%ld\n", worstSteady, totalSteady, worstChurn, totalChurn);
    printf("first render after a preset load (staged preset adoption, not steady state): up to %ld allocations\n", adopt);
    if (worstSteady != 0) { printf("FAIL: steady-state render allocates\n"); fails++; }
    vv_engine_destroy(e);
    return fails ? 1 : 0;
}
