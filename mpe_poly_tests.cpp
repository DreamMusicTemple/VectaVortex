// Round 7: MPE is polyphonic - up to 10 simultaneous per-channel notes; the 11th steals the oldest.
#include <cstdio>
#include <cstring>
#include <vector>
#include "vv/full_engine.hpp"
static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { fails++; printf("FAIL %s\n", m); } } while (0)
int main() {
    vv::FullEngine e; e.prepare(48000);
    e.loadPresetJson("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"env\":{\"a\":0.005,\"d\":0.1,\"s\":0.8,\"r\":0.05},\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.3,\"env\":{\"a\":0.005,\"d\":0.1,\"s\":0.8,\"r\":0.05}}]}},\"bpm\":120}");
    e.setMpe(true);
    std::vector<float> l(128), r(128);
    auto pump = [&] { for (int i = 0; i < 4; i++) { e.render(l.data(), r.data(), 128); } };
    pump();
    for (int k = 0; k < 14; k++) { uint8_t m[3] = { (uint8_t)(0x90 | ((k % 15) + 1)), (uint8_t)(48 + k), 100 }; e.midi(m, 3); pump(); if (k < 10) CHECK(e.mpeActiveCount() == k + 1, "grows to 10"); }
    CHECK(e.mpeActiveCount() == 10, "capped at 10 MPE notes");
    uint8_t off[3] = { 0x80 | 14, (uint8_t)(48 + 13), 0 }; e.midi(off, 3); pump();
    CHECK(e.mpeActiveCount() == 9, "note off frees a slot");
    printf(fails ? "FAILED\n" : "all passed\n"); return fails ? 1 : 0;
}
