// Vektur Void: the C++ game logic must follow the web's shmupStep frame for frame (golden trace from tools/make_golden_void.js).
#include "vv/vektur_void.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <string>
using namespace vv::vvoid;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)
struct Row { std::string name; long f; double v[23]; };
static vv::vvoid::Input inMortal(long f) { Input i; i.dx = (double)(((f >> 5) % 3) - 1); i.dy = (double)(((f >> 6) % 3) - 1); i.plasma = f > 5; i.bomb = (f % 50) < 3; i.start = (f % 600) == 5; return i; }
static vv::vvoid::Input inBoss(long f) { Input i; i.dx = std::sin((double)f * 0.02) * 0.9; i.dy = std::cos((double)f * 0.013) * 0.9; i.plasma = true; i.bomb = (f % 40) < 2; i.start = f == 3; return i; }
int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "golden_void.txt";
    std::ifstream in(path); if (!in) { std::printf("cannot open %s\n", path); return 2; }
    std::map<std::string, std::vector<Row>> runs; std::string line;
    while (std::getline(in, line)) { std::istringstream ss(line); Row r; ss >> r.name >> r.f; for (int i = 0; i < 23; i++) ss >> r.v[i]; runs[r.name].push_back(r); }
    struct Cfg { const char* name; uint32_t seed; bool god, gate; long frames; bool boss; };   // gate: this run must reach and beat the final boss
    const Cfg cfgs[] = {{"mortal", 12345, false, false, 14000, false}, {"god", 777, true, false, 60000, true}, {"gated", 4242, true, true, 260000, true}};
    for (const Cfg& c : cfgs) {
        auto& rows = runs[c.name]; CHECK(!rows.empty(), "%s: no golden rows", c.name); if (rows.empty()) continue;
        Game G = newGame(); G.god = c.god; Lcg rnd(c.seed); size_t ri = 0; int bad = 0; int maxLevel = 0, victory = 0;
        for (long f = 0; f < c.frames; f++) {
            step(G, c.boss ? inBoss(f) : inMortal(f), 1.0 / 60, [&] { return rnd(); });
            if (G.level > maxLevel) maxLevel = G.level; if (G.mode == M_VICTORY) victory = 1;
            if (f % 40 == 0 && ri < rows.size()) {
                const Row& r = rows[ri++]; if (r.f != f) { CHECK(false, "%s: row misaligned at %ld", c.name, f); break; }
                const double got[23] = {(double)G.mode, (double)G.level, (double)G.score, (double)G.kills, (double)G.spares, (double)G.continues, (double)G.wLvl, G.ship.x, G.ship.y, G.ship.alive ? 1.0 : 0.0,
                    (double)G.bullets.size(), (double)G.bombs.size(), (double)G.enemies.size(), (double)G.ebul.size(), (double)G.pick.size(), (double)G.parts.size(), (double)G.obstacles.size(), (double)G.beams.size(),
                    G.hasBoss ? (double)G.boss.hits : -1.0, G.hasBoss ? G.boss.y : 0.0, G.spread, G.shake, G.t};
                for (int i = 0; i < 23; i++) { const double tol = i >= 7 && (i < 9 || i >= 19) ? 1e-6 * std::max(1.0, std::fabs(r.v[i])) : 0.0;
                    if (std::fabs(got[i] - r.v[i]) > tol) { if (bad++ < 5) std::printf("  %s f=%ld col %d: web %.10g native %.10g\n", c.name, f, i, r.v[i], got[i]); } }
            }
        }
        CHECK(bad == 0, "%s: %d mismatching values", c.name, bad); CHECK(ri == rows.size(), "%s: compared %zu of %zu rows", c.name, ri, rows.size());
        std::printf("%s: %zu rows, reached level %d%s, %d mismatches\n", c.name, rows.size(), maxLevel, victory ? ", victory" : "", bad);
        if (c.gate) { CHECK(maxLevel == 7 && victory, "gated run should beat the final boss"); }
    }
    std::printf("%s\n", fails ? "VOID GOLDEN TESTS FAILED" : "void golden tests passed"); return fails ? 1 : 0;
}
