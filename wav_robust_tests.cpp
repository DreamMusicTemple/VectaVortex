// Audit regression: parseWav must terminate on hostile chunk sizes and still decode valid files.
#include <cstdio>
#include <cstring>
#include <vector>
#include <future>
#include <chrono>
#include "vv/assets.hpp"
static int fails = 0;
static void check(const char* n, bool ok) { std::printf("%-58s %s\n", n, ok ? "OK" : "FAIL"); if (!ok) fails++; }
static std::vector<unsigned char> riff(size_t n) { std::vector<unsigned char> d(n, 0); std::memcpy(&d[0], "RIFF", 4); std::memcpy(&d[8], "WAVE", 4); return d; }
static void put32(std::vector<unsigned char>& d, size_t o, uint32_t v) { std::memcpy(&d[o], &v, 4); }
// parse on a worker so a regression to the old infinite loop FAILS instead of hanging the suite
static bool finishes(std::vector<unsigned char> d, std::shared_ptr<vv::SampleBuf>* out = nullptr) {
    auto fut = std::async(std::launch::async, [d]() { return vv::parseWav(d.data(), d.size()); });
    if (fut.wait_for(std::chrono::seconds(3)) != std::future_status::ready) { std::printf("  (parse still running after 3 s - hung)\n"); std::_Exit(fails + 1); }
    auto r = fut.get(); if (out) *out = r; return true;
}
int main() {
    for (uint32_t sz : {0xFFFFFFF7u, 0xFFFFFFF8u, 0xFFFFFFF9u, 0xFFFFFFFFu, 0x80000000u}) {
        auto d = riff(64); std::memcpy(&d[12], "junk", 4); put32(d, 16, sz);
        char nm[80]; std::snprintf(nm, sizeof nm, "unknown chunk size 0x%08X terminates, rejected", sz);
        std::shared_ptr<vv::SampleBuf> r; finishes(d, &r); check(nm, r == nullptr);
        auto d2 = riff(64); std::memcpy(&d2[12], "fmt ", 4); put32(d2, 16, sz); std::snprintf(nm, sizeof nm, "fmt chunk size 0x%08X terminates", sz); finishes(d2, nullptr); check(nm, true);
    }
    // a valid 16-bit mono file with an odd-sized unknown chunk before the data still decodes
    std::vector<unsigned char> w = riff(12); auto app = [&](const char* id, std::vector<unsigned char> body) { size_t o = w.size(); w.resize(o + 8); std::memcpy(&w[o], id, 4); put32(w, o + 4, (uint32_t)body.size()); w.insert(w.end(), body.begin(), body.end()); if (body.size() & 1) w.push_back(0); };
    std::vector<unsigned char> fmt(16, 0); fmt[0] = 1; fmt[2] = 1; put32(fmt, 4, 44100); put32(fmt, 8, 88200); fmt[12] = 2; fmt[14] = 16;
    app("fmt ", fmt); app("LIST", {1, 2, 3}); std::vector<unsigned char> pcm = {0, 0, 0, 64, 0, 192, 255, 127}; app("data", pcm);
    std::shared_ptr<vv::SampleBuf> good; finishes(w, &good);
    check("valid WAV (odd chunk padding) decodes", good && good->len == 4 && good->sr == 44100 && good->ch == 1);
    check("decoded sample values correct", good && good->len == 4 && std::abs(good->d[0][1] - 0.5f) < 1e-6f && std::abs(good->d[0][2] + 0.5f) < 1e-6f);
    std::printf("%s (%d failures)\n", fails ? "FAILED" : "PASSED", fails); return fails ? 1 : 0;
}
