// Clip launcher (web: state.clips): 8 banks x 8 slots of loopable note + automation clips, launched on the transport beat clock.
//
//  ClipSet     immutable-by-convention snapshot of every clip. Built on the UI thread, handed to the audio thread as shared_ptr<const ClipSet>.
//  ClipPlayer  audio-thread state: fixed arrays only, no allocation, no locks, no file access. Quantised start / stop, one clip per layer,
//              transposition at playback (stored notes never change), note scheduling through a callback, recording of played notes into a
//              fixed ring that the UI thread drains.
//  ClipModel   UI-thread editing (create, duplicate, delete, copy / paste, move / swap, rename, length, layer, colour, transpose, quantise, undo,
//              recording merge, JSON in / out with validation). The JSON is the same shape the web app saves in its preset extras ("clips").
#pragma once
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include "json.hpp"

namespace vv { namespace clips {

constexpr int kBanks = 8, kSlots = 8, kMaxClips = kBanks * kSlots, kLayers = 6, kAutoRate = 8, kMaxLaneRefs = 32, kRecCap = 4096, kQueueCap = 32;
inline const char* const kLayerNames[kLayers] = {"WT", "FM", "SMP", "PRO", "SID", "LUA"};
/* Round 6: sequencers can also target the pseudo layer XFX (index 6 = kXfxSeq): each note is expanded to the layers the XFX key programs (web xfxExpand). */
constexpr int kSeqLayers = 7, kXfxSeq = 6;
inline const char* const kSeqLayerNames[kSeqLayers] = {"WT", "FM", "SMP", "PRO", "SID", "LUA", "XFX"};
inline int layerIndex(const std::string& s) { for (int i = 0; i < kSeqLayers; i++) if (s == kSeqLayerNames[i]) return i; return -1; }
inline const double kQuantBeats[8] = {0, 0.25, 0.5, 1, 2, 4, 8, 16};
inline const char* const kQuantNames[8] = {"imm", "1/16", "1/8", "1/4", "1/2", "1bar", "2bar", "4bar"};
inline const int kLens[7] = {1, 2, 4, 8, 16, 32, 64};
inline const char* const kLayerColors[kSeqLayers] = {"#4ad4d9", "#e2a04a", "#d94a9e", "#b026ff", "#f0e020", "#6cff7a", "#ff8a3d"};

struct Note { double t = 0, l = 0.25, v = 0.8; int p = 60; };
struct Lane { std::string key; std::vector<float> v; };   // 8 samples per beat, 0..1, 0.5 = no change (same as the web automation lanes)
struct Clip {
    bool present = false, factory = false; int id = 0, layer = 0, bars = 4, tr = 0; std::string name = "CLIP", color = "#4ad4d9";
    std::vector<Note> notes; std::vector<Lane> lanes;
    double lenBeats() const { return bars * 4.0; }
};
struct Bank { std::string name; std::string color; Clip slot[kSlots]; };
struct LaneRef { int slot = 0, lane = 0; };
struct ClipSet {
    int quant = 5 /* 1 bar */, mode = 0 /* 0 replace, 1 overdub */, nid = 1, bank = 0, selSlot = 0;
    Bank banks[kBanks];
    std::vector<LaneRef> laneRefs;   // every automation lane of every clip (<= kMaxLaneRefs): one hidden modulation route each
    const Clip& at(int slot) const { return banks[slot / kSlots].slot[slot % kSlots]; }
    Clip& at(int slot) { return banks[slot / kSlots].slot[slot % kSlots]; }
    void rebuildLaneRefs() { laneRefs.clear(); for (int s = 0; s < kMaxClips; s++) { const Clip& c = at(s); if (!c.present) continue; for (size_t i = 0; i < c.lanes.size(); i++) { if ((int)laneRefs.size() >= kMaxLaneRefs) return; if (!c.lanes[i].v.empty()) laneRefs.push_back({s, (int)i}); } } }
};

// ---------------------------------------------------------------------------------------------------- audio thread
struct RecNote { int slot = 0, p = 60; double t = 0, l = 0.25, v = 0.8; };

class ClipPlayer {
public:
    ClipPlayer() { for (auto& p : phase_) p.store(-1.f, std::memory_order_relaxed); }
    void setSet(std::shared_ptr<const ClipSet> s) { set_ = std::move(s); for (int i = 0; i < kMaxClips; i++) if (play_[i].on && (!set_ || !set_->at(i).present)) { play_[i].on = false; } nq_ = 0; for (int i = 0; i < kMaxClips; i++) phase_[i].store(play_[i].on ? 0.f : -1.f, std::memory_order_relaxed); recSlot_ = recSlot_ >= 0 && set_ && set_->at(recSlot_).present ? recSlot_ : -1; publishMasks(); }
    const ClipSet* set() const { return set_.get(); }
    // transport beat `beat` is "now" (block start). Quantised on the set's launch quantise; transport start is the caller's job.
    void launch(int slot, double beat) { enqueue(slot, 1, nextBoundary(beat)); publishMasks(); }
    void stop(int slot, double beat, bool immediate) { if (slot < 0 || slot >= kMaxClips || !play_[slot].on) { dequeue(slot); publishMasks(); return; } dequeue(slot); enqueue(slot, 0, immediate ? beat : nextBoundary(beat)); publishMasks(); }
    void stopAll() { for (int i = 0; i < kMaxClips; i++) { play_[i].on = false; phase_[i].store(-1.f, std::memory_order_relaxed); } nq_ = 0; recSlot_ = -1; for (auto& h : held_) h.on = false; publishMasks(); }
    bool playing(int slot) const { return slot >= 0 && slot < kMaxClips && play_[slot].on; }
    bool queuedStart(int slot) const { for (int i = 0; i < nq_; i++) if (q_[i].slot == slot && q_[i].kind == 1) return true; return false; }
    // UI read: phase in beats inside the clip loop, -1 when not playing (benign relaxed atomic).
    float phaseUI(int slot) const { return slot >= 0 && slot < kMaxClips ? phase_[slot].load(std::memory_order_relaxed) : -1.f; }
    uint64_t playingMaskUI() const { return playMask_.load(std::memory_order_relaxed); }   // UI reads (relaxed atomics, written by the audio thread)
    uint64_t queuedStartMaskUI() const { return startMask_.load(std::memory_order_relaxed); }
    uint64_t queuedStopMaskUI() const { return stopMask_.load(std::memory_order_relaxed); }
    int recordingSlotUI() const { return recUI_.load(std::memory_order_relaxed); }

    // Advance over the beat span [b0, b1). fire(layer, note, absBeat, transpose) is called for every note starting in the span.
    template <class F> void advance(double b0, double b1, F&& fire) {
        if (!set_) return;
        for (int i = 0; i < kMaxClips; i++) if (play_[i].on && b0 < play_[i].from - 0.05) { play_[i].on = false; }   // the host looped / jumped back: clips stop, the user relaunches
        // due operations in time order (tiny insertion sort over at most kQueueCap entries)
        for (int guard = 0; guard < kQueueCap; guard++) {
            int bi = -1; for (int i = 0; i < nq_; i++) if (q_[i].at < b1 - 1e-9 && (bi < 0 || q_[i].at < q_[bi].at)) bi = i;
            if (bi < 0) break;
            const Op op = q_[bi]; q_[bi] = q_[--nq_];
            const double at = std::max(op.at, b0);
            runTo(at, b0, fire);
            const Clip& c = set_->at(op.slot);
            if (op.kind == 1 && c.present) {
                for (int i = 0; i < kMaxClips; i++) if (play_[i].on && i != op.slot && set_->at(i).layer == c.layer) { play_[i].on = false; if (recSlot_ == i) recSlot_ = -1; }   // one clip per layer
                play_[op.slot].on = true; play_[op.slot].start = at; play_[op.slot].from = at;
            } else if (op.kind == 0) { play_[op.slot].on = false; if (recSlot_ == op.slot) finishRec(op.slot, at); }
        }
        runTo(b1, b0, fire); publishMasks();
        for (int i = 0; i < kMaxClips; i++) { if (!play_[i].on) { phase_[i].store(-1.f, std::memory_order_relaxed); continue; } const double len = set_->at(i).lenBeats(); const double r = b1 - play_[i].start; phase_[i].store(r < 0 ? -1.f : (float)std::fmod(r, len), std::memory_order_relaxed); }
    }
    // Value (-1..1, 0 = no change) of lane `ref` right now; 0 while its clip is not playing.
    double laneValue(const LaneRef& r, double beat) const {
        if (!set_ || !play_[r.slot].on || beat < play_[r.slot].start) return 0;
        const Clip& c = set_->at(r.slot); const Lane& l = c.lanes[(size_t)r.lane]; const size_t n = l.v.size(); if (!n) return 0;
        const double len = c.lenBeats(); double ph = std::fmod(beat - play_[r.slot].start, len); if (ph < 0) ph += len;
        const double p = ph * kAutoRate; const size_t i = (size_t)p % n; const double f = p - std::floor(p);
        const double a = l.v[i], b = l.v[(i + 1) % n]; return ((a + (b - a) * f) - 0.5) * 2;
    }
    // ---- recording (audio thread writes, UI thread drains)
    void armRecord(int slot) { recSlot_ = slot; recStarted_ = false; for (auto& h : held_) h.on = false; publishMasks(); }
    void disarmRecord(double beat) { if (recSlot_ >= 0) finishRec(recSlot_, beat); recSlot_ = -1; publishMasks(); }
    int recordingSlot() const { return recSlot_; }
    void noteDown(int note, double vel, double beat) {
        if (recSlot_ < 0 || note < 0 || note > 127 || !play_[recSlot_].on || beat < play_[recSlot_].start || !set_) return;
        const double len = set_->at(recSlot_).lenBeats(); double ph = std::fmod(beat - play_[recSlot_].start, len); if (ph < 0) ph += len;
        held_[note] = {true, ph, beat, vel};
    }
    void noteUp(int note, double beat) {
        if (recSlot_ < 0 || note < 0 || note > 127 || !held_[note].on) return; push(recSlot_, note, held_[note], beat); held_[note].on = false;
    }
    int drainRec(RecNote* out, int cap) { int n = 0; size_t r = recR_.load(std::memory_order_relaxed); const size_t w = recW_.load(std::memory_order_acquire); while (r != w && n < cap) { out[n++] = rec_[r % kRecCap]; r++; } recR_.store(r, std::memory_order_release); return n; }

private:
    struct Play { bool on = false; double start = 0, from = 0; };
    struct Op { int slot = 0, kind = 0; double at = 0; };   // kind 1 start, 0 stop
    struct Held { bool on = false; double ph = 0, t0 = 0, vel = 0.8; };
    std::shared_ptr<const ClipSet> set_; Play play_[kMaxClips]; Op q_[kQueueCap]; int nq_ = 0; std::atomic<float> phase_[kMaxClips];
    std::atomic<uint64_t> playMask_{0}, startMask_{0}, stopMask_{0}; std::atomic<int> recUI_{-1};
    void publishMasks() { uint64_t p = 0, a = 0, b = 0; for (int i = 0; i < kMaxClips; i++) if (play_[i].on) p |= 1ull << i; for (int i = 0; i < nq_; i++) { if (q_[i].kind == 1) a |= 1ull << q_[i].slot; else b |= 1ull << q_[i].slot; } playMask_.store(p, std::memory_order_relaxed); startMask_.store(a, std::memory_order_relaxed); stopMask_.store(b, std::memory_order_relaxed); recUI_.store(recSlot_, std::memory_order_relaxed); }
    int recSlot_ = -1; bool recStarted_ = false; Held held_[128]; RecNote rec_[kRecCap]; std::atomic<size_t> recW_{0}, recR_{0};
    double nextBoundary(double beat) const { const double q = set_ ? kQuantBeats[std::max(0, std::min(7, set_->quant))] : 4.0; return q > 0 ? std::ceil(beat / q - 1e-6) * q : beat; }
    void dequeue(int slot) { for (int i = 0; i < nq_;) { if (q_[i].slot == slot) q_[i] = q_[--nq_]; else i++; } }
    void enqueue(int slot, int kind, double at) { if (slot < 0 || slot >= kMaxClips || !set_ || (kind == 1 && !set_->at(slot).present)) return; dequeue(slot); if (nq_ < kQueueCap) q_[nq_++] = {slot, kind, at}; }
    void push(int slot, int note, const Held& h, double beat) {
        const double len = set_ ? set_->at(slot).lenBeats() : 4.0; RecNote r; r.slot = slot; r.p = note; r.t = h.ph; r.v = std::max(0.02, std::min(1.0, h.vel));
        r.l = std::max(0.03, std::min(len - h.ph, beat - h.t0)); const size_t w = recW_.load(std::memory_order_relaxed);
        if (w - recR_.load(std::memory_order_acquire) >= (size_t)kRecCap) return;   // ring full: drop (the UI drains every few ms)
        rec_[w % kRecCap] = r; recW_.store(w + 1, std::memory_order_release);
    }
    void finishRec(int slot, double beat) { for (int n = 0; n < 128; n++) if (held_[n].on) { push(slot, n, held_[n], beat); held_[n].on = false; } }
    // fire the notes of every playing clip in [from, upTo), then move `from` to upTo
    template <class F> void runTo(double upTo, double b0, F& fire) {
        for (int s = 0; s < kMaxClips; s++) {
            Play& p = play_[s]; if (!p.on) continue; const Clip& c = set_->at(s); const double len = c.lenBeats(), from = p.from;
            if (upTo > from) {
                const int k0 = std::max(0, (int)std::floor((from - p.start) / len)), k1 = (int)std::floor((upTo - p.start) / len);
                for (int k = k0; k <= k1 && k <= k0 + 4; k++) for (const Note& n : c.notes) {
                    if (n.t >= len || n.t < 0) continue; const double a = p.start + k * len + n.t;
                    if (a >= from - 1e-9 && a < upTo - 1e-9) fire(c.layer, n, std::max(a, b0), c.tr);
                }
                p.from = upTo;
            }
        }
    }
};

// ---------------------------------------------------------------------------------------------------- JSON <-> model
inline std::string esc(const std::string& s) { std::string o; for (unsigned char ch : s) { if (ch == '"' || ch == '\\') { o += '\\'; o += (char)ch; } else if (ch < 0x20) o += ' '; else o += (char)ch; } return o; }
inline std::string num(double d) { char b[48]; std::snprintf(b, sizeof b, "%.6g", d); return b; }
inline bool validColor(const std::string& c) { if (c.size() != 7 || c[0] != '#') return false; for (size_t i = 1; i < 7; i++) if (!std::isxdigit((unsigned char)c[i])) return false; return true; }
inline std::string clipJson(const Clip& c) {
    std::string o = "{\"id\":\"c" + std::to_string(c.id) + "\",\"name\":\"" + esc(c.name) + "\",\"color\":\"" + c.color + "\",\"layer\":\"" + kSeqLayerNames[std::max(0, std::min(kSeqLayers - 1, c.layer))] + "\",\"bars\":" + std::to_string(c.bars) + ",\"tr\":" + std::to_string(c.tr) + ",\"factory\":" + (c.factory ? "true" : "false") + ",\"notes\":[";
    for (size_t i = 0; i < c.notes.size(); i++) { const Note& n = c.notes[i]; o += (i ? "," : ""); o += "{\"t\":" + num(n.t) + ",\"l\":" + num(n.l) + ",\"p\":" + std::to_string(n.p) + ",\"v\":" + num(n.v) + "}"; }
    o += "],\"auto\":{"; for (size_t i = 0; i < c.lanes.size(); i++) { o += (i ? "," : ""); o += "\"" + esc(c.lanes[i].key) + "\":{\"v\":["; for (size_t k = 0; k < c.lanes[i].v.size(); k++) { o += (k ? "," : ""); o += num(c.lanes[i].v[k]); } o += "]}"; }
    return o + "}}";
}
inline std::string setJson(const ClipSet& s) {
    std::string o = "{\"v\":1,\"quant\":\"" + std::string(kQuantNames[std::max(0, std::min(7, s.quant))]) + "\",\"mode\":\"" + (s.mode ? "overdub" : "replace") + "\",\"bank\":" + std::to_string(s.bank) + ",\"sel\":{\"b\":" + std::to_string(s.selSlot / kSlots) + ",\"s\":" + std::to_string(s.selSlot % kSlots) + "},\"nid\":" + std::to_string(s.nid) + ",\"banks\":[";
    for (int b = 0; b < kBanks; b++) { o += (b ? "," : ""); o += "{\"name\":\"" + esc(s.banks[b].name) + "\",\"color\":\"" + s.banks[b].color + "\",\"slots\":["; for (int i = 0; i < kSlots; i++) { o += (i ? "," : ""); o += s.banks[b].slot[i].present ? clipJson(s.banks[b].slot[i]) : "null"; } o += "]}"; }
    return o + "]}";
}
// Validated clip from JSON (returns false when it is not a clip). The id is only kept when keepId is true.
inline bool parseClip(const Json& j, Clip& c, bool keepId, int freshId) {
    if (!j.isObj()) return false; const Json* ns = j.get("notes"); if (!ns || !ns->isArr()) return false;
    Clip o; o.present = true; o.name = j.str("name", "CLIP").substr(0, 24); if (o.name.empty()) o.name = "CLIP";
    const int li = layerIndex(j.str("layer", "WT")); o.layer = li < 0 ? 0 : li;
    const double b = j.num("bars", 4); o.bars = std::max(1, std::min(64, (int)std::lround(b))); o.tr = std::max(-48, std::min(48, (int)std::lround(j.num("tr", 0))));
    o.color = validColor(j.str("color")) ? j.str("color") : std::string(kLayerColors[o.layer]); o.factory = keepId && j.boolean("factory", false);
    int id = freshId; if (keepId) { std::string s = j.str("id"); if (s.size() > 1 && s[0] == 'c') { char* e = nullptr; long v = std::strtol(s.c_str() + 1, &e, 10); if (e && !*e && v > 0) id = (int)v; } } o.id = id;
    for (const Json& n : ns->a) { if (!n.isObj() || !n.has("t") || !n.has("p")) continue; Note x; x.t = n.num("t", 0); x.p = (int)std::lround(n.num("p", 60)); if (!std::isfinite(x.t) || x.t < 0) continue;
        x.t = std::round(x.t * 1000) / 1000; x.l = std::max(0.01, std::min(256.0, n.num("l", 0.25))); x.v = std::max(0.01, std::min(1.0, n.num("v", 0.8))); x.p = std::max(0, std::min(127, x.p)); o.notes.push_back(x); if (o.notes.size() >= 20000) break; }
    if (const Json* au = j.get("auto")) if (au->isObj()) for (auto& kv : au->o) { const Json* vv = kv.second.get("v"); if (!vv || !vv->isArr() || vv->a.empty() || vv->a.size() > (size_t)(64 * 4 * kAutoRate)) continue; Lane l; l.key = kv.first; bool ok = true;
        for (const Json& x : vv->a) { if (!x.isNum() || !std::isfinite(x.n)) { ok = false; break; } l.v.push_back((float)std::max(0.0, std::min(1.0, x.n))); } if (ok) o.lanes.push_back(std::move(l)); }
    c = std::move(o); return true;
}
inline std::vector<Clip> factoryClips() {
    std::vector<Clip> out; auto mk = [&](const char* name, int layer, int bars) { Clip c; c.present = true; c.factory = true; c.name = name; c.layer = layer; c.bars = bars; c.color = kLayerColors[layer]; return c; };
    Clip a = mk("BASS PULSE", 0, 2); for (int i = 0; i < 16; i++) a.notes.push_back({i * 0.5, 0.4, i % 2 ? 0.6 : 0.9, i % 4 == 3 ? 43 : 36}); out.push_back(a);
    Clip b = mk("MINOR ARP", 1, 2); const int arp[8] = {57, 60, 64, 69, 64, 60, 52, 55}; for (int i = 0; i < 32; i++) b.notes.push_back({i * 0.25, 0.2, 0.55 + 0.25 * ((i % 4) == 0), arp[i % 8] + (i >= 16 && i % 3 == 0 ? 5 : 0)}); out.push_back(b);
    Clip c = mk("PAD CHORDS", 0, 4); const int ch[4][3] = {{57, 60, 64}, {53, 57, 60}, {48, 55, 60}, {55, 59, 62}}; for (int i = 0; i < 4; i++) for (int k = 0; k < 3; k++) c.notes.push_back({i * 4.0, 3.8, 0.6, ch[i][k]}); out.push_back(c);
    Clip d = mk("PLUCK LINE", 3, 2); const double dt[8] = {0, 1, 1.5, 2.5, 4, 5, 6, 7}; const int dp[8] = {69, 72, 71, 67, 64, 67, 69, 64}; for (int i = 0; i < 8; i++) d.notes.push_back({dt[i], 0.45, 0.75, dp[i]}); out.push_back(d);
    Clip e = mk("SUB DRONE", 4, 1); e.notes.push_back({0, 3.9, 0.7, 33}); out.push_back(e);
    Clip f = mk("LEAD HOOK", 5, 2); const double ft[9] = {0, 1, 1.5, 2, 4, 5, 6, 6.5, 7}, fl[9] = {1, 0.5, 0.5, 1.5, 1, 1, 0.5, 0.5, 1}; const int fp[9] = {76, 74, 72, 69, 76, 79, 77, 76, 72}; for (int i = 0; i < 9; i++) f.notes.push_back({ft[i], fl[i] * 0.9, 0.8, fp[i]}); out.push_back(f);
    return out;
}

class ClipModel {
public:
    ClipModel() { reset(); }
    void reset() { set_ = ClipSet(); for (int b = 0; b < kBanks; b++) { set_.banks[b].name = "BANK " + std::to_string(b + 1); set_.banks[b].color = kLayerColors[b % kLayers]; } hist_.clear(); fillFactory(0); }
    const ClipSet& set() const { return set_; }
    std::shared_ptr<const ClipSet> snapshot() { set_.rebuildLaneRefs(); return std::make_shared<const ClipSet>(set_); }
    std::string json() const { return setJson(set_); }
    // ---- load / validate: returns the number of damaged clips that were skipped (-1 = not a clip document, model left unchanged)
    int load(const Json& j) {
        if (!j.isObj()) return -1; const Json* bs = j.get("banks"); if (!bs || !bs->isArr()) return -1;
        ClipSet s; s.nid = std::max(1, (int)j.num("nid", 1)); const std::string q = j.str("quant", "1bar"); s.quant = 5; for (int i = 0; i < 8; i++) if (q == kQuantNames[i]) s.quant = i;
        s.mode = j.str("mode") == "overdub" ? 1 : 0; s.bank = std::max(0, std::min(kBanks - 1, (int)j.num("bank", 0)));
        if (const Json* sl = j.get("sel")) s.selSlot = std::max(0, std::min(kBanks - 1, (int)sl->num("b", 0))) * kSlots + std::max(0, std::min(kSlots - 1, (int)sl->num("s", 0)));
        int bad = 0; std::vector<int> used;
        for (int b = 0; b < kBanks; b++) {
            const Json& sb = bs->at((size_t)b); s.banks[b].name = sb.isObj() && !sb.str("name").empty() ? sb.str("name").substr(0, 16) : "BANK " + std::to_string(b + 1);
            s.banks[b].color = sb.isObj() && validColor(sb.str("color")) ? sb.str("color") : std::string(kLayerColors[b % kLayers]);
            const Json* sls = sb.isObj() ? sb.get("slots") : nullptr;
            for (int i = 0; i < kSlots; i++) { const Json& cj = sls ? sls->at((size_t)i) : Json(); if (cj.isNull()) continue; Clip c;
                if (!parseClip(cj, c, true, s.nid)) { bad++; continue; }
                if (std::find(used.begin(), used.end(), c.id) != used.end()) c.id = s.nid; used.push_back(c.id); s.nid = std::max(s.nid, c.id + 1); s.banks[b].slot[i] = std::move(c); }
        }
        set_ = std::move(s); hist_.clear(); return bad;
    }
    int loadText(const std::string& text) { try { return load(JsonParser::parse(text)); } catch (...) { return -1; } }
    // ---- ops (every mutating op snapshots for undo first)
    bool occupied(int slot) const { return valid(slot) && set_.at(slot).present; }
    void snap() { hist_.push_back(set_); if (hist_.size() > 40) hist_.pop_front(); }
    bool undo() { if (hist_.empty()) return false; const int nid = set_.nid; set_ = hist_.back(); hist_.pop_back(); set_.nid = std::max(set_.nid, nid); return true; }
    int newClip(int slot, int layer, int bars) { if (!valid(slot)) return -1; snap(); Clip c; c.present = true; c.id = set_.nid++; c.layer = std::max(0, std::min(kSeqLayers - 1, layer)); c.bars = std::max(1, std::min(64, bars)); c.color = kLayerColors[c.layer]; c.name = "CLIP " + std::to_string(c.id); set_.at(slot) = c; set_.selSlot = slot; return c.id; }
    int duplicate(int slot) { if (!occupied(slot)) return -1; int dst = -1; const int b = slot / kSlots; for (int i = slot % kSlots + 1; i < kSlots && dst < 0; i++) if (!set_.banks[b].slot[i].present) dst = b * kSlots + i; for (int i = 0; i < kSlots && dst < 0; i++) if (!set_.banks[b].slot[i].present) dst = b * kSlots + i; if (dst < 0) return -1;
        snap(); Clip c = set_.at(slot); c.id = set_.nid++; c.factory = false; c.name = (c.name + " 2").substr(0, 24); set_.at(dst) = c; set_.selSlot = dst; return dst; }
    bool remove(int slot) { if (!occupied(slot)) return false; snap(); set_.at(slot) = Clip(); return true; }
    bool move(int from, int to, int mode /* 0 move, 1 copy, 2 swap */) {
        if (!occupied(from) || !valid(to) || from == to) return false; snap(); Clip a = set_.at(from);
        if (mode == 2) { Clip t = set_.at(to); set_.at(from) = t; set_.at(to) = a; }
        else if (mode == 1) { a.id = set_.nid++; a.factory = false; set_.at(to) = a; } else { set_.at(to) = a; set_.at(from) = Clip(); }
        set_.selSlot = to; return true;
    }
    bool paste(int slot, const std::string& clipJsonText) { if (!valid(slot)) return false; Json j; try { j = JsonParser::parse(clipJsonText); } catch (...) { return false; } if (const Json* c = j.get("clip")) j = *c; Clip c; if (!parseClip(j, c, false, set_.nid)) return false; snap(); c.id = set_.nid++; set_.at(slot) = c; set_.selSlot = slot; return true; }
    std::string exportClip(int slot) const { return occupied(slot) ? "{\"vvClip\":1,\"clip\":" + clipJson(set_.at(slot)) + "}" : std::string(); }
    bool setName(int slot, const std::string& n) { if (!occupied(slot)) return false; snap(); set_.at(slot).name = n.empty() ? "CLIP" : n.substr(0, 24); return true; }
    bool setBankName(int b, const std::string& n) { if (b < 0 || b >= kBanks) return false; snap(); set_.banks[b].name = n.empty() ? "BANK " + std::to_string(b + 1) : n.substr(0, 16); return true; }
    bool setBars(int slot, int bars) { if (!occupied(slot)) return false; snap(); set_.at(slot).bars = std::max(1, std::min(64, bars)); return true; }   // notes and lanes stay stored beyond the end
    bool setLayer(int slot, int layer) { if (!occupied(slot) || layer < 0 || layer >= kSeqLayers) return false; snap(); Clip& c = set_.at(slot); c.layer = layer; c.color = kLayerColors[layer]; return true; }
    bool setColor(int slot, const std::string& col) { if (!occupied(slot) || !validColor(col)) return false; set_.at(slot).color = col; return true; }
    bool transpose(int slot, int d) { if (!occupied(slot)) return false; Clip& c = set_.at(slot); c.tr = std::max(-48, std::min(48, c.tr + d)); return true; }
    bool setTranspose(int slot, int t) { if (!occupied(slot)) return false; set_.at(slot).tr = std::max(-48, std::min(48, t)); return true; }
    bool quantiseNotes(int slot) { if (!occupied(slot)) return false; snap(); Clip& c = set_.at(slot); for (Note& n : c.notes) n.t = std::min(c.lenBeats() - 0.25, std::max(0.0, std::round(n.t / 0.25) * 0.25)); return true; }
    bool clearAuto(int slot) { if (!occupied(slot)) return false; snap(); set_.at(slot).lanes.clear(); return true; }
    void setQuant(int q) { set_.quant = std::max(0, std::min(7, q)); }
    void setMode(int m) { set_.mode = m ? 1 : 0; }
    void select(int slot) { if (valid(slot)) { set_.selSlot = slot; set_.bank = slot / kSlots; } }
    void setBank(int b) { if (b >= 0 && b < kBanks) set_.bank = b; }
    void restoreFactory(int bank) { if (bank < 0 || bank >= kBanks) return; snap(); fillFactory(bank); }
    bool fromRoll(int slot, int layer, int bars, const std::vector<Note>& notes) { if (!valid(slot) || layer < 0 || layer >= kSeqLayers) return false; snap(); Clip& c = set_.at(slot); if (!c.present) { c = Clip(); c.present = true; c.id = set_.nid++; c.name = "ROLL CLIP"; } c.layer = layer; c.bars = std::max(1, std::min(64, bars)); c.color = kLayerColors[layer]; c.notes.clear(); for (const Note& n : notes) if (n.t < c.lenBeats()) c.notes.push_back(n); set_.selSlot = slot; return true; }
    // recording: replace mode clears the clip's notes once at the start (undoable); overdub keeps them.
    void beginRecord(int slot) { if (!occupied(slot)) return; snap(); if (set_.mode == 0) { set_.at(slot).notes.clear(); set_.at(slot).lanes.clear(); } recTouched_.clear(); }
    void mergeRec(const RecNote* r, int n) { for (int i = 0; i < n; i++) { if (!occupied(r[i].slot)) continue; Clip& c = set_.at(r[i].slot); Note x; x.t = std::round(r[i].t * 1000) / 1000; x.l = r[i].l; x.v = r[i].v; x.p = r[i].p; c.notes.push_back(x); } }
    // automation recorded on the UI thread: write value `val01` for `key` at clip phase `phaseBeats` (fills from the previous index so fast moves leave no gaps)
    void recordAuto(int slot, const std::string& key, double phaseBeats, double val01) {
        if (!occupied(slot)) return; Clip& c = set_.at(slot); const size_t need = (size_t)(c.bars * 4 * kAutoRate); Lane* l = nullptr; for (Lane& x : c.lanes) if (x.key == key) l = &x;
        bool first = false; if (!l) { Lane nl; nl.key = key; nl.v.assign(need, 0.5f); c.lanes.push_back(std::move(nl)); l = &c.lanes.back(); first = true; }
        bool seen = false; for (auto& k : recTouched_) if (k.first == slot && k.second == key) seen = true;
        if (!seen) { recTouched_.push_back({slot, key}); if (!first && set_.mode == 0) l->v.assign(need, 0.5f); }
        if (l->v.size() < need) { const std::vector<float> o = l->v; l->v.resize(need); for (size_t i = 0; i < need; i++) l->v[i] = o[i % o.size()]; }
        const size_t n = l->v.size(); const size_t idx = (size_t)std::floor(phaseBeats * kAutoRate) % n; auto it = lastIdx_.find(slot, key); size_t i = it < 0 ? (idx + n - 1) % n : (size_t)it, g = 0;
        const float v = (float)std::max(0.0, std::min(1.0, val01)); do { i = (i + 1) % n; l->v[i] = v; g++; } while (i != idx && g < n); lastIdx_.set(slot, key, (long)idx);
    }
    void endAuto(int slot, const std::string& key) { lastIdx_.clear(slot, key); }
    bool dirtyLanes(int) { return true; }

private:
    ClipSet set_; std::deque<ClipSet> hist_; std::vector<std::pair<int, std::string>> recTouched_;
    struct LastIdx { std::vector<std::pair<std::pair<int, std::string>, long>> v;
        long find(int s, const std::string& k) const { for (auto& e : v) if (e.first.first == s && e.first.second == k) return e.second; return -1; }
        void set(int s, const std::string& k, long x) { for (auto& e : v) if (e.first.first == s && e.first.second == k) { e.second = x; return; } v.push_back({{s, k}, x}); }
        void clear(int s, const std::string& k) { for (size_t i = 0; i < v.size(); i++) if (v[i].first.first == s && v[i].first.second == k) { v.erase(v.begin() + (long)i); return; } } } lastIdx_;
    static bool valid(int slot) { return slot >= 0 && slot < kMaxClips; }
    void fillFactory(int bank) { auto f = factoryClips(); for (size_t i = 0; i < f.size() && i < (size_t)kSlots; i++) { f[i].id = set_.nid++; set_.banks[bank].slot[i] = f[i]; } }
};

}}  // namespace vv::clips
