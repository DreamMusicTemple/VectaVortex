// Parity of the 35 generative arp algorithms with the web implementation (golden dump from the browser: algo x 2 parameter sets x 70 steps) + safety properties.
#include "vv/arp_adv.hpp"
#include "vv/json.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace vv;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void cfgFrom(const Json& p, ArpAdvCfg& A) {
    A.algo = p.str("algo", "fractal"); A.algoId = arpadv::algoIndex(A.algo); A.scale = p.str("scale", "major"); A.scaleId = arpadv::scaleIndex(A.scale);
    A.seed = p.num("seed", 1234); A.probability = p.num("probability", 1); A.density = p.num("density", 0.6); A.complexity = p.num("complexity", 0.5); A.mutation = p.num("mutation", 0.2); A.variation = p.num("variation", 0); A.randomness = p.num("randomness", 0);
    A.humanise = p.num("humanise", 0); A.octSpread = p.num("octSpread", 0); A.ratchet = p.num("ratchet", 0); A.velVar = p.num("velVar", 0); A.accent = p.num("accent", 0.3); A.gateVar = p.num("gateVar", 0); A.groove = p.num("groove", 0.5);
    A.patLen = p.num("patLen", 16); A.cycleVar = p.num("cycleVar", 0); A.rotation = p.num("rotation", 0); A.skipProb = p.num("skipProb", 0); A.rhythmDisp = p.num("rhythmDisp", 0); A.mutSalt = p.num("mutSalt", 0); A.mutAmt = p.num("mutAmt", 0); A.varSeed = p.num("varSeed", 0);
}
int main() {
    std::ifstream f("tests/data/arp_adv_golden.json"); if (!f) f.open("../tests/data/arp_adv_golden.json"); if (!f) { printf("FAIL golden file missing\n"); return 1; }
    std::stringstream ss; ss << f.rdbuf(); Json root = JsonParser::parse(ss.str()); const Json& g = *root.get("cases");
    int cases = 0, steps = 0, bad = 0;
    for (auto& cs : g.a) {
        ArpAdvCfg A; cfgFrom(*cs.get("P"), A); std::vector<int> E; for (auto& n : cs.get("E")->a) E.push_back((int)n.asNum());
        const Json* st = cs.get("steps"); int caseBad = 0;
        for (size_t k = 0; k < st->a.size(); k++) {
            arpadv::Ev out[arpadv::kMaxEv]; const int n = arpadv::gen(A, E.data(), (int)E.size(), (int)k, out); const Json& ex = st->a[k];
            bool ok = (int)ex.a.size() == n;
            for (int q = 0; ok && q < n; q++) { const Json& e = ex.a[(size_t)q];
                ok = out[q].i == (int)e.a[0].asNum() && out[q].ds == (int)e.a[1].asNum() && std::fabs(out[q].v - e.a[2].asNum()) < 1e-9 && std::fabs(out[q].g - e.a[3].asNum()) < 1e-9 && std::fabs(out[q].o - e.a[4].asNum()) < 1e-9 && out[q].r == (int)e.a[5].asNum() && out[q].dec == (int)e.a[6].asNum(); }
            if (!ok) caseBad++; steps++;
        }
        CHECK(caseBad == 0, "algo %s seed %g: %d steps differ from the web output", cs.str("algo", "?").c_str(), A.seed, caseBad); bad += caseBad; cases++;
    }
    // A/B morph parity (amount 0 = pure current slot's A, 1 = pure B)
    int mcases = 0;
    for (auto& mc : root.get("morph")->a) {
        ArpAdvCfg A, B; cfgFrom(*mc.get("A"), A); cfgFrom(*mc.get("B"), B); std::vector<int> E; for (auto& n : mc.get("E")->a) E.push_back((int)n.asNum()); const double amt = mc.num("amt", 0);
        const Json* st = mc.get("steps"); int mb = 0;
        for (size_t k = 0; k < st->a.size(); k++) { arpadv::Ev out[arpadv::kMaxEv]; const int n = arpadv::genMorph(A, B, amt, E.data(), (int)E.size(), (int)k, out); const Json& ex = st->a[k]; bool ok = (int)ex.a.size() == n;
            for (int q = 0; ok && q < n; q++) { const Json& e = ex.a[(size_t)q]; ok = out[q].i == (int)e.a[0].asNum() && out[q].ds == (int)e.a[1].asNum() && std::fabs(out[q].v - e.a[2].asNum()) < 1e-9 && std::fabs(out[q].o - e.a[4].asNum()) < 1e-9; }
            if (!ok) mb++; }
        CHECK(mb == 0, "morph amt %g: %d steps differ", amt, mb); mcases++;
    }
    printf("morph cases: %d\n", mcases);
    // Mutate re-rolls some steps (not all, not none); Regenerate Variation keeps every note and only re-rolls dynamics
    { int E[5] = {55, 60, 64, 67, 72}; ArpAdvCfg A; A.algoId = arpadv::algoIndex("markov"); A.seed = 21; A.velVar = 0.8; A.gateVar = 0.8; A.probability = 1; ArpAdvCfg M = A; M.mutSalt = 1; M.mutAmt = 0.4; ArpAdvCfg V = A; V.varSeed = 1;
      int diffM = 0, diffNoteV = 0, diffVelV = 0, total = 0;
      for (int k = 0; k < 64; k++) { arpadv::Ev a[3], m[3], v[3]; const int na = arpadv::gen(A, E, 5, k, a), nm = arpadv::gen(M, E, 5, k, m), nv = arpadv::gen(V, E, 5, k, v); total++;
          if (na != nm || (na && a[0].i != m[0].i)) diffM++; if (na != nv || (na && a[0].i != v[0].i)) diffNoteV++; if (na && nv && std::fabs(a[0].v - v[0].v) > 1e-9) diffVelV++; }
      CHECK(diffM > 0 && diffM < total, "mutate changes some steps (%d of %d)", diffM, total); CHECK(diffNoteV == 0, "regenerate variation keeps the notes (%d differ)", diffNoteV); CHECK(diffVelV > 0, "regenerate variation re-rolls dynamics"); }
    // morph amount 0 = pure A, 1 = pure B
    { int E[4] = {60, 64, 67, 71}; ArpAdvCfg A, B; A.algoId = arpadv::algoIndex("fractal"); B.algoId = arpadv::algoIndex("cascade"); int bad = 0;
      for (int k = 0; k < 32; k++) { arpadv::Ev x[3], y[3], z[3], w[3]; const int nx = arpadv::genMorph(A, B, 0, E, 4, k, x), ny = arpadv::gen(A, E, 4, k, y), nz = arpadv::genMorph(A, B, 1.01, E, 4, k, z), nw = arpadv::gen(B, E, 4, k, w);
          if (nx != ny || (nx && (x[0].i != y[0].i || x[0].ds != y[0].ds))) bad++; if (nz != nw || (nz && (z[0].i != w[0].i || z[0].ds != w[0].ds))) bad++; }
      CHECK(bad == 0, "morph 0 / 1 equal pure A / B (%d)", bad); }
    // safety: every algorithm x chord sizes x many steps stays in range, finite, and never exceeds kMaxEv events
    for (int a = 0; a < arpadv::kNumAlgos; a++) for (int N : {1, 2, 3, 8, 40}) {
        ArpAdvCfg A; A.algoId = a; A.octSpread = 0.7; A.randomness = 0.3; A.variation = 0.5; A.ratchet = 0.6; A.humanise = 1; A.rhythmDisp = 1; A.cycleVar = 0.7; A.patLen = 64; A.rotation = 5; A.complexity = 1; A.density = 1;
        int E[64]; for (int i = 0; i < N; i++) E[i] = 36 + i * 3;
        for (int k = 0; k < 400; k++) { arpadv::Ev out[arpadv::kMaxEv]; const int n = arpadv::gen(A, E, N, k, out); bool ok = n >= 0 && n <= arpadv::kMaxEv;
            for (int q = 0; ok && q < n; q++) ok = out[q].i >= 0 && out[q].i < N && std::isfinite(out[q].v) && std::isfinite(out[q].g) && out[q].o >= 0 && out[q].o <= 0.9 && out[q].r >= 1 && out[q].r <= 4;
            if (!ok) { CHECK(false, "algo %d N=%d step %d out of range", a, N, k); goto next; } }
        next:;
    }
    printf("arp advanced parity: %d cases, %d steps, %d differing\n", cases, steps, bad);
    printf(failures ? "arp adv tests: %d FAILED\n" : "arp adv tests: all passed\n", failures); return failures ? 1 : 0;
}
