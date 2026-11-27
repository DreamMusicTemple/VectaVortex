// lua_host.hpp - hosts the four LUA-layer script engines (lx::eng::Engine) for the native synth.
//
// Threads: load()/clear() run on a NON-audio thread (script compile + setup + wavetable generation happen there, bounded by the VM's
// instruction/memory limits). The audio thread only calls voiceOn/voiceOff/voiceKill/setHost/render/acquire on an engine that is already
// loaded. A freshly loaded engine is published with one atomic pointer swap; the previous engine is destroyed only after the audio thread
// has left it (an in-use counter), so a script can be replaced while notes play without locks or allocation in the audio callback.
#pragma once
#include "lx/lx_engine.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vv {

struct LuaParamInfo { std::string id, label, unit, group, kind; double mn = 0, mx = 1, def = 0, step = 0; std::vector<std::string> options; };
struct LuaSlotInfo { bool ok = false; std::string error, name, category, author, desc; int line = 0, col = 0, setupInstr = 0; std::vector<LuaParamInfo> params; int tables = 0; };

class LuaHost {
public:
    static constexpr int kSlots = 4;
    static constexpr size_t kPool = 4u << 20;
    struct Eng {
        lx::eng::Engine e; std::unique_ptr<unsigned char[]> mem; uint32_t gen = 0; std::string src; uint32_t seed = 1;
        float outL[lx::eng::kBlock] = {}, outR[lx::eng::kBlock] = {};
    };
    LuaHost() { for (auto& r : live_) for (auto& x : r) x.store(0.f); for (auto& c : cur_) c.store(nullptr); for (auto& u : busy_) u.store(0); }
    ~LuaHost() { for (int i = 0; i < kSlots; i++) { Eng* p = cur_[i].exchange(nullptr); if (p){ p->e.clearScript(); delete p; } } for (auto& g : grave_){ g.second->e.clearScript(); delete g.second; } }
    LuaHost(const LuaHost&) = delete; LuaHost& operator=(const LuaHost&) = delete;

    void setSampleRate(double sr) { sr_ = (float)sr; }

    // ---- non-audio thread
    LuaSlotInfo load(int slot, const std::string& src, uint32_t seed, const std::vector<std::pair<std::string, double>>& saved = {}) {
        LuaSlotInfo info; if (slot < 0 || slot >= kSlots) { info.error = "bad slot"; return info; }
        if (src.empty()) { clear(slot); info.error = "empty"; return info; }
        std::unique_ptr<Eng> n(new Eng());
        n->mem.reset(new unsigned char[kPool + 16]); n->src = src; n->seed = seed; n->gen = ++genCounter_;
        n->e.init(n->mem.get(), kPool, sr_);
        n->e.allocFloats = [](size_t k) -> float* { float* p = (float*)std::calloc(k, 4); return p; };
        n->e.freeFloats = [](float* p, size_t) { std::free(p); };
        n->e.layerSeed = seed;
        const bool ok = n->e.load(src.data(), src.size(), false);
        if (!ok) { info.error = n->e.err; info.line = n->e.errLine; info.col = n->e.errCol; n->e.clearScript(); return info; }
        fill(n->e, info); info.ok = true; info.setupInstr = (int)n->e.totalInstr;
        for (int k = 0; k < lx::eng::kMaxParams; k++) live_[slot][k].store(k < n->e.nparams ? n->e.params[k].def : 0.f, std::memory_order_relaxed);
        for (const auto& kv : saved) for (int k = 0; k < n->e.nparams; k++) if (kv.first == n->e.params[k].id) live_[slot][k].store((float)std::min<double>(n->e.params[k].mx, std::max<double>(n->e.params[k].mn, kv.second)), std::memory_order_relaxed);
        Eng* old = cur_[slot].exchange(n.release(), std::memory_order_acq_rel);
        retire(slot, old); return info;
    }
    void clear(int slot) { if (slot < 0 || slot >= kSlots) return; Eng* old = cur_[slot].exchange(nullptr, std::memory_order_acq_rel); retire(slot, old); }
    LuaSlotInfo info(int slot) const { LuaSlotInfo i; Eng* p = slot >= 0 && slot < kSlots ? cur_[slot].load() : nullptr; if (p) { fill(p->e, i); i.ok = true; } return i; }
    // Validate a script without installing it (editor VALIDATE): returns the info and discards the engine.
    LuaSlotInfo validate(const std::string& src, uint32_t seed) const {
        LuaSlotInfo info; std::unique_ptr<Eng> n(new Eng()); n->mem.reset(new unsigned char[kPool + 16]); n->e.init(n->mem.get(), kPool, sr_);
        n->e.allocFloats = [](size_t k) -> float* { return (float*)std::calloc(k, 4); }; n->e.freeFloats = [](float* p, size_t) { std::free(p); }; n->e.layerSeed = seed;
        if (!n->e.load(src.data(), src.size(), false)) { info.error = n->e.err; info.line = n->e.errLine; info.col = n->e.errCol; n->e.clearScript(); return info; }
        fill(n->e, info); info.ok = true; info.setupInstr = (int)n->e.totalInstr; n->e.clearScript(); return info;
    }

    // ---- audio thread
    // RAII guard: while alive the engine cannot be destroyed by a concurrent load().
    struct Guard { LuaHost* h = nullptr; int slot = -1; Eng* e = nullptr; ~Guard() { if (h && e) h->busy_[slot].fetch_sub(1, std::memory_order_release); } Guard() = default; Guard(Guard&& o) noexcept : h(o.h), slot(o.slot), e(o.e) { o.e = nullptr; } Guard(const Guard&) = delete; };
    Guard acquire(int slot) { Guard g; if (slot < 0 || slot >= kSlots) return g; busy_[slot].fetch_add(1, std::memory_order_acquire); Eng* p = cur_[slot].load(std::memory_order_acquire); if (!p) { busy_[slot].fetch_sub(1, std::memory_order_release); return g; } g.h = this; g.slot = slot; g.e = p; return g; }
    uint32_t generation(int slot) const { Eng* p = slot >= 0 && slot < kSlots ? cur_[slot].load(std::memory_order_acquire) : nullptr; return p ? p->gen : 0; }
    bool loaded(int slot) const { return generation(slot) != 0; }

    int paramIndex(int slot, const char* id) const { Eng* p = slot >= 0 && slot < kSlots ? cur_[slot].load() : nullptr; if (!p) return -1; for (int k = 0; k < p->e.nparams; k++) if (std::string(p->e.params[k].id) == id) return k; return -1; }
    // live script-parameter values (any thread writes; the audio thread copies them into each voice every block)
    void setParam(int slot, int i, float v) { if (slot >= 0 && slot < kSlots && i >= 0 && i < lx::eng::kMaxParams) live_[slot][i].store(v, std::memory_order_relaxed); }
    float getParam(int slot, int i) const { return slot >= 0 && slot < kSlots && i >= 0 && i < lx::eng::kMaxParams ? live_[slot][i].load(std::memory_order_relaxed) : 0.f; }
    void copyParams(int slot, float* dst) const { for (int k = 0; k < lx::eng::kMaxParams; k++) dst[k] = live_[slot][k].load(std::memory_order_relaxed); }

    // telemetry for the UI (relaxed reads of plain fields; display only)
    int activeVoices(int slot) const { Eng* p = slot >= 0 && slot < kSlots ? cur_[slot].load() : nullptr; return p ? p->e.activeVoices : 0; }
    int lastBlockInstr(int slot) const { Eng* p = slot >= 0 && slot < kSlots ? cur_[slot].load() : nullptr; return p ? (int)p->e.lastBlockInstr : 0; }
    int faults(int slot) const { Eng* p = slot >= 0 && slot < kSlots ? cur_[slot].load() : nullptr; return p ? p->e.faultCount : 0; }
    std::string lastError(int slot) const { Eng* p = slot >= 0 && slot < kSlots ? cur_[slot].load() : nullptr; return p && p->e.hasError ? std::string(p->e.err) : std::string(); }
    std::string log(int slot) const { Eng* p = slot >= 0 && slot < kSlots ? cur_[slot].load() : nullptr; return p ? std::string(p->e.logbuf) : std::string(); }

private:
    static void fill(const lx::eng::Engine& e, LuaSlotInfo& i) {
        i.name = e.meta.name; i.category = e.meta.category; i.author = e.meta.author; i.desc = e.meta.desc; i.tables = e.ntables;
        static const char* kinds[] = {"knob", "slider", "switch", "menu", "number"};
        for (int k = 0; k < e.nparams; k++) { const lx::eng::Param& p = e.params[k]; LuaParamInfo q; q.id = p.id; q.label = p.label; q.unit = p.unit; q.group = p.group; q.kind = kinds[p.kind >= 0 && p.kind < 5 ? p.kind : 0]; q.mn = p.mn; q.mx = p.mx; q.def = p.def; q.step = p.step; for (int o = 0; o < p.nopts; o++) q.options.push_back(p.opts[o]); i.params.push_back(q); }
    }
    void retire(int slot, Eng* old) {   // wait (bounded) until the audio thread has left the old engine, then free it on this non-audio thread
        std::lock_guard<std::mutex> lk(graveM_);
        for (size_t i = 0; i < grave_.size();) { if (busy_[grave_[i].first].load(std::memory_order_acquire) == 0) { grave_[i].second->e.clearScript(); delete grave_[i].second; grave_.erase(grave_.begin() + i); } else i++; }
        if (!old) return;
        for (int spin = 0; spin < 200 && busy_[slot].load(std::memory_order_acquire) > 0; spin++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (busy_[slot].load(std::memory_order_acquire) > 0) { grave_.emplace_back(slot, old); return; }   // never free an engine in use: park it for the next load
        old->e.clearScript(); delete old;
    }
    std::atomic<float> live_[kSlots][lx::eng::kMaxParams];
    std::atomic<Eng*> cur_[kSlots]; std::atomic<int> busy_[kSlots]; float sr_ = 48000.f; std::atomic<uint32_t> genCounter_{0};
    std::mutex graveM_; std::vector<std::pair<int, Eng*>> grave_;
};

}  // namespace vv
