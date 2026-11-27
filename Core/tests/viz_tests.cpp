// Native LUA Visual engine test: every factory script loads, runs 240 frames with a synthetic audio signal and no faults,
// every param moves something (uniforms/post/commands), and instruction use stays inside the budget.  usage: vv_viz_tests <factory dir>
#include "vv/lx/lx_viz.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <dirent.h>
#include <algorithm>
using namespace lx; using namespace lx::viz;
static std::string slurp(const std::string& p) { FILE* f = fopen(p.c_str(), "rb"); if (!f) return ""; std::string s; char b[8192]; size_t n; while ((n = fread(b, 1, sizeof b, f))) s.append(b, n); fclose(f); return s; }
static unsigned long hashState(Engine& E) { unsigned long h = 1469598103934665603ul; auto mix = [&](const void* d, size_t n) { const unsigned char* c = (const unsigned char*)d; for (size_t i = 0; i < n; i++) { h ^= c[i]; h *= 1099511628211ul; } };
    mix(E.u, sizeof E.u); mix(E.post, sizeof E.post); mix(&E.ncmd, sizeof E.ncmd); mix(E.cmds, (size_t)E.ncmd * sizeof(Cmd8)); return h; }
static void feed(Engine& E, int f, bool loud) {
    float t = f / 60.f; for (int i = 0; i < kAudioN; i++) E.audio[i] = 0;
    float k = loud ? 1.f : 0.f; for (int b = 0; b < 9; b++) E.audio[b] = k * (.45f + .4f * std::sin(t * (1.1f + b * .37f) + b));
    E.audio[9] = k * .5f; E.audio[10] = k * .7f; E.audio[11] = (f % 30 == 0) ? 1.f : 0.f; E.audio[12] = k * .3f; E.audio[13] = k * .5f; E.audio[14] = k * .5f; E.audio[15] = .4f; E.audio[16] = t; E.audio[17] = .4f; E.audio[18] = 220.f + 40.f * std::sin(t);
    for (int i = 0; i < kSpecN; i++) E.spec[i] = k * (.5f + .5f * std::sin(t * 2 + i * .5f));
    E.host[0] = 2; E.host[1] = 640; E.host[2] = 360; E.host[3] = 60; E.host[4] = (float)f; E.host[5] = 1; E.host[6] = 1.f / 60.f;
}
int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "viz/factory"; std::vector<std::string> files; if (DIR* d = opendir(dir.c_str())) { while (dirent* e = readdir(d)) { std::string n = e->d_name; if (n.size() > 4 && n.substr(n.size() - 4) == ".lua") files.push_back(n); } closedir(d); }
    std::sort(files.begin(), files.end()); int bad = 0; size_t pool = 6u << 20; void* mem = malloc(pool); Engine* E = (Engine*)aligned_alloc(64, (sizeof(Engine) + 63) & ~(size_t)63);
    if (files.size() < 18) { printf("FAIL expected >= 18 factory scripts, found %zu\n", files.size()); return 1; }
    for (auto& fn : files) {
        std::string src = slurp(dir + "/" + fn); memset((void*)E, 0, sizeof(Engine)); new (E, PlaceTag{}) Engine(); E->init(mem, pool);
        if (!E->load(src.c_str(), src.size())) { printf("FAIL %s load: %s (line %d)\n", fn.c_str(), E->err, E->errLine); bad++; continue; }
        long maxU = 0, maxR = 0; bool changed = false; unsigned long h0 = 0; int maxCmd = 0;
        for (int f = 0; f < 240; f++) { feed(*E, f, f < 200); for (int i = 0; i < E->nparams; i++) E->pv[i] = E->params[i].def; if (!E->frame(1.f / 60.f)) { printf("FAIL %s frame %d fault: %s\n", fn.c_str(), f, E->err); bad++; break; }
            maxU = std::max(maxU, E->lastUpdate); maxR = std::max(maxR, E->lastRender); maxCmd = std::max(maxCmd, E->ncmd); unsigned long h = hashState(*E); if (f == 30) h0 = h; if (f > 30 && h != h0) changed = true; }
        // every parameter must change the output (switches excepted when the script gates them)
        int dead = 0; std::string deadNames;
        for (int pi = 0; pi < E->nparams; pi++) { VParam& p = E->params[pi]; bool any = false; unsigned long ref = 0;
            for (int pass = 0; pass < 3 && !any; pass++) { float vals[3] = { p.mn, p.mx, .5f * (p.mn + p.mx) }; unsigned long hs[2];
                for (int r = 0; r < 2; r++) { memset((void*)E, 0, sizeof(Engine)); new (E, PlaceTag{}) Engine(); E->init(mem, pool); E->load(src.c_str(), src.size());
                    for (int f = 0; f < 36; f++) { feed(*E, f, true); for (int i = 0; i < E->nparams; i++) E->pv[i] = E->params[i].def; E->pv[pi] = r ? vals[pass] : p.def; if (strcmp(p.id, "freezeColor") == 0) { for (int i = 0; i < E->nparams; i++) if (!strcmp(E->params[i].id, "freeze")) E->pv[i] = 1; } E->frame(1.f / 60.f); }
                    hs[r] = hashState(*E); }
                if (hs[0] != hs[1]) any = true; (void)ref; }
            if (!any) { dead++; deadNames += std::string(" ") + p.id; } }
        printf("%s %-28s params=%2d maxUpd=%6ld maxRen=%6ld cmds=%5d animated=%d dead=%d%s\n", (dead || !changed) ? "FAIL" : "OK  ", fn.c_str(), E->nparams, maxU, maxR, maxCmd, (int)changed, dead, deadNames.c_str());
        if (dead || !changed || maxR > 450000 || maxU > 250000) bad++;
    }
    printf(bad ? "SOME FAILED (%d)\n" : "ALL OK (%d scripts)\n", bad ? bad : (int)files.size()); return bad ? 1 : 0;
}
