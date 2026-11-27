// The C++ GENERATE interpreter must give the same presets as the web app's genBuild() for the same seed: Core/tests/data/gen_parity.json holds, for 118 cases
// (every category variant with three seeds/scales, plus the four fixed XFX presets), the leaves in which the web output differs from the INIT preset
// (regenerate with: node tools/export_gen_parity.js). Numbers must agree to 1e-9 (relative); strings and booleans exactly.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <map>
#include <cmath>
#include "vv/presetgen.hpp"
using namespace vv;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; if (failures < 60) { printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } } while (0)
static void flat(const Json& o, const std::string& pre, std::map<std::string, Json>& m) {
    if (o.type == Json::Arr) { if (o.a.empty()) { Json z; z.type = Json::Num; z.n = 0; m[pre + "#len"] = z; } for (size_t i = 0; i < o.a.size(); i++) flat(o.a[i], pre + "." + std::to_string(i), m); }
    else if (o.type == Json::Obj) { if (o.o.empty()) { Json z; z.type = Json::Num; z.n = 1; m[pre + "#empty"] = z; } for (auto& kv : o.o) flat(kv.second, pre.empty() ? kv.first : pre + "." + kv.first, m); }
    else m[pre] = o;
}
static bool same(const Json& a, const Json& b) {
    if (a.type != b.type) { if ((a.type == Json::Num && b.type == Json::Bool) || (a.type == Json::Bool && b.type == Json::Num)) return a.asNum() == b.asNum(); return false; }
    if (a.type == Json::Num) return std::fabs(a.n - b.n) <= 1e-9 * std::max(1.0, std::fabs(a.n));
    if (a.type == Json::Str) return a.s == b.s; if (a.type == Json::Bool) return a.b == b.b; return true;
}
int main() {
    std::ifstream f("tests/data/gen_parity.json"); if (!f) f.open("Core/tests/data/gen_parity.json"); if (!f) f.open("../Core/tests/data/gen_parity.json");
    std::stringstream ss; ss << f.rdbuf(); CHECK(ss.str().size() > 1000, "parity data readable");
    Json cases = JsonParser::parse(ss.str());
    std::map<std::string, Json> base; flat(JsonParser::parse(presetgen::kInitPreset), "", base);
    int ran = 0, bad = 0;
    for (auto& c : cases.a) {
        presetgen::ScaleCtx sc; if (const Json* s = c.get("scale")) { sc.on = s->boolean("on", false); sc.root = (int)s->num("root", 0); if (const Json* iv = s->get("ints")) for (auto& v : iv->a) sc.ints.push_back((int)v.n); sc.label = "x"; }
        Json out; std::vector<std::string> warn; bool ok;
        if (c.has("named")) ok = presetgen::buildNamed(out, (size_t)c.num("named", 0), nullptr, &warn);
        else ok = presetgen::build(out, c.str("cat"), (uint32_t)c.num("seed", 1), sc, nullptr, &warn, (int)c.num("variant", 0));
        const std::string nm = c.str("name") + " seed " + std::to_string((long long)c.num("seed", 0)) + (sc.on ? " scale" : "");
        CHECK(ok, "%s builds", nm.c_str()); if (!ok) continue; ran++;
        CHECK((int)warn.size() == (int)c.num("warn", 0), "%s: %d path warnings (web %d)", nm.c_str(), (int)warn.size(), (int)c.num("warn", 0));
        std::map<std::string, Json> m; flat(out, "", m); std::map<std::string, Json> d;
        for (auto& kv : m) { if (kv.first == "notes" || kv.first.rfind("gen", 0) == 0) continue; auto it = base.find(kv.first); if (it == base.end() || !same(it->second, kv.second)) d[kv.first] = kv.second; }
        const Json& exp = *c.get("diff"); int mism = 0;
        for (auto& kv : exp.o) { auto it = d.find(kv.first); if (it == d.end()) { if (mism++ < 3) printf("  %s: web has %s, C++ has nothing\n", nm.c_str(), kv.first.c_str()); } else if (!same(kv.second, it->second)) { if (mism++ < 3) printf("  %s: %s web %s C++ %s\n", nm.c_str(), kv.first.c_str(), jsonDump(kv.second).c_str(), jsonDump(it->second).c_str()); } }
        for (auto& kv : d) if (!exp.has(kv.first.c_str())) { if (mism++ < 3) printf("  %s: C++ has extra %s = %s\n", nm.c_str(), kv.first.c_str(), jsonDump(kv.second).c_str()); }
        if (mism) { bad++; failures++; printf("FAIL %s: %d mismatching leaves\n", nm.c_str(), mism); }
    }
    printf("parity: %d cases built, %d differ from the web output\n", ran, bad);
    CHECK(ran == (int)cases.a.size(), "all cases ran");
    printf(failures ? "preset generator parity: %d FAILED\n" : "preset generator parity: all passed\n", failures); return failures ? 1 : 0;
}
