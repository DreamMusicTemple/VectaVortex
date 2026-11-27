// Piano-roll RECORD (web: ROLLREC). Two halves:
//  * Ring: a fixed-size, lock-free single-producer / single-consumer queue. The audio thread pushes the user's note-on / note-off / pitch-bend / mod-wheel events
//    (stamped with the heard transport beat) and never blocks or allocates. The UI thread pops them.
//  * Recorder: UI-thread side. Turns events into finished roll data: notes {t,l,p,v} placed at the loop position (output latency already removed by the producer),
//    optional grid quantise, pitch-bend / mod lanes as sparse [beat, value] points (thinned to ~12 per beat) plus the spans they replace (overdub).
#pragma once
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace vv { namespace rollrec {
struct Ev { double beat = 0; float x = 0; int32_t kind = 0, note = 0; };   // kind 0 note-on (x = velocity), 1 note-off, 2 pitch bend (-1..1), 3 mod wheel (0..1)
class Ring {
public:
    static constexpr uint32_t kCap = 4096;   // power of two
    bool push(const Ev& e) { const uint32_t w = w_.load(std::memory_order_relaxed), r = r_.load(std::memory_order_acquire); if (w - r >= kCap) { dropped_.fetch_add(1, std::memory_order_relaxed); return false; } buf_[w & (kCap - 1)] = e; w_.store(w + 1, std::memory_order_release); return true; }
    bool pop(Ev& e) { const uint32_t r = r_.load(std::memory_order_relaxed); if (r == w_.load(std::memory_order_acquire)) return false; e = buf_[r & (kCap - 1)]; r_.store(r + 1, std::memory_order_release); return true; }
    void clear() { Ev e; while (pop(e)) {} }
    uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
private:
    Ev buf_[kCap]; std::atomic<uint32_t> w_{0}, r_{0}, dropped_{0};
};
struct Cfg { double ls = 0, le = 16, roomEnd = 16, grid = 0 /* 0 = no quantise */; };   // loop start / end and roll end in beats; grid in beats
struct Note { double t = 0, l = 0.25, v = 0.9; int p = 60; };
struct Pt { double t, v; };
struct Out {
    std::vector<Note> notes; std::vector<Pt> bend, mod; std::vector<Pt> bendCut, modCut;   // *Cut: [a,b] spans of the old lane to remove before adding the new points
    void clear() { notes.clear(); bend.clear(); mod.clear(); bendCut.clear(); modCut.clear(); }
    bool empty() const { return notes.empty() && bend.empty() && mod.empty() && bendCut.empty() && modCut.empty(); }
};
class Recorder {
public:
    void begin() { for (auto& h : held_) h.on = false; for (int k = 0; k < 2; k++) { pend_[k].on = false; lastT_[k] = -1; } nNotes_ = 0; }
    int notesRecorded() const { return nNotes_; }
    static double round3(double x) { return std::round(x * 1000.0) / 1000.0; }
    double pos(double beat, const Cfg& c) const { const double len = std::max(1e-6, c.le - c.ls); double m = std::fmod(std::max(0.0, beat), len); if (m < 0) m += len; return c.ls + m; }
    void feed(const Ev& e, const Cfg& c, Out& out) {
        if (e.kind == 0) {
            const int n = e.note & 127; if (held_[n].on) closeNote(n, e.beat, c, out);
            double t = pos(e.beat, c); if (c.grid > 0) { t = std::round(t / c.grid) * c.grid; if (t >= c.le - 1e-6) t = c.ls; }
            held_[n].on = true; held_[n].t = round3(t); held_[n].beat = e.beat; held_[n].v = std::min(1.0, std::max(0.02, (double)e.x));
        } else if (e.kind == 1) { const int n = e.note & 127; if (held_[n].on) closeNote(n, e.beat, c, out); }
        else if (e.kind == 2 || e.kind == 3) lane(e.kind - 2, pos(e.beat, c), e.kind == 2 ? std::min(1.0, std::max(-1.0, (double)e.x)) : std::min(1.0, std::max(0.0, (double)e.x)), c, out, false);
    }
    // finalise whatever is waiting: a thinned lane point older than the thinning window (nowBeat given), or everything when `all` (recording stopped)
    void settle(double nowBeat, const Cfg& c, Out& out, bool all) {
        for (int k = 0; k < 2; k++) if (pend_[k].on && (all || pos(nowBeat, c) - pend_[k].t > 1.0 / 48 || pos(nowBeat, c) < pend_[k].t)) emit(k, c, out);
        if (all) for (int n = 0; n < 128; n++) if (held_[n].on) closeNote(n, held_[n].beat + 0.25, c, out);
    }
private:
    struct Held { bool on = false; double t = 0, beat = 0, v = 0.9; } held_[128];
    struct Pend { bool on = false; double t = 0, v = 0; } pend_[2]; double lastT_[2] = {-1, -1}; int nNotes_ = 0;
    void closeNote(int n, double beatOff, const Cfg& c, Out& out) {
        Held& h = held_[n]; h.on = false; double l = beatOff - h.beat; const double room = std::max(0.03, c.roomEnd - h.t);
        if (c.grid > 0) l = std::max(c.grid, std::round(l / c.grid) * c.grid);
        Note nt; nt.t = std::max(0.0, h.t); nt.p = n; nt.v = h.v; nt.l = std::max(0.03, std::min(room, round3(l) > 0 ? round3(l) : 0.1)); out.notes.push_back(nt); nNotes_++;
    }
    void emit(int k, const Cfg& c, Out& out) {
        Pend& p = pend_[k]; p.on = false; std::vector<Pt>& pts = k == 0 ? out.bend : out.mod; std::vector<Pt>& cut = k == 0 ? out.bendCut : out.modCut;
        if (lastT_[k] < 0) cut.push_back({ p.t, p.t });
        else if (p.t < lastT_[k]) { cut.push_back({ lastT_[k], c.le }); cut.push_back({ c.ls, p.t }); }   // the loop wrapped
        else cut.push_back({ lastT_[k], p.t });
        pts.push_back({ round3(p.t), round3(p.v) }); lastT_[k] = p.t;
    }
    void lane(int k, double t, double v, const Cfg& c, Out& out, bool) {
        Pend& p = pend_[k];
        if (p.on && t >= p.t && t - p.t < 1.0 / 48) { p.v = v; return; }   // thin a fast sweep
        if (p.on) emit(k, c, out);
        p.on = true; p.t = t; p.v = v;
    }
};
}}   // namespace vv::rollrec
