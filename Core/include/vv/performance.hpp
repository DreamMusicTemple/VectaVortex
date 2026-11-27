// Performance layer on top of Synth: held / latched notes, polyphony + mono-legato, layer Hold, the arpeggiator and the step sequencer
// (pattern chains, ratchets, probability, tempo ratio, swing, SMP transient p-locks). A port of handleNoteDown/Up, enforcePolyphony,
// startArpClock and startTransport - but clocked by the audio sample clock instead of setInterval/setTimeout, so timing is exact and
// tempo-correct (see docs/WEB_QUIRKS.md). Call advance() once per 128-frame quantum, right before Synth::process().
#pragma once
#include "arp_adv.hpp"
#include "arp_harmony.hpp"
#include "roll_rec.hpp"
#include <atomic>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include "synth.hpp"
#include "clips.hpp"

namespace vv {

class Performance {
public:
    int layersForNoteMask(int note) const { return layersForNote(note, false); }   // layers that sound for a key (zones + XFX per-key switches)
    void attach(Synth* s) { synth_ = s; sr_ = s->sampleRate(); held_.reserve(256); latched_.reserve(256); poly_.reserve(256); }
    void setPreset(std::shared_ptr<const Preset> p) {
        preset_ = std::move(p); if (!preset_) return;
        bpm = preset_->bpm; arp = preset_->arp; swingOn = preset_->swingOn; swingAmount = preset_->swingAmount; maxPolyphony = preset_->maxPolyphony; legato = preset_->legato;
        for (int l = 0; l < kNumLayers; l++) { seq_[l] = SeqState{}; seq_[l].slot = preset_->layers[l].currentSlot; }
        seq_[kNumLayers] = SeqState{}; seq_[kNumLayers].slot = preset_->hasXfxSeq ? preset_->xfxSeq.currentSlot : 0;
        arpNext_ = (double)synth_->clock() / sr_ + arpInterval();
    }
    // Live preset edit: keep the running sequencer / arp clock, adopt changed settings only.
    void updatePreset(std::shared_ptr<const Preset> p) {
        if (!p) return; if (!preset_) { setPreset(std::move(p)); return; }
        const Preset& O = *preset_; const Preset& N = *p;
        if (O.bpm != N.bpm) bpm = N.bpm;
        if (O.arp.order != N.arp.order || O.arp.rateMode != N.arp.rateMode || O.arp.division != N.arp.division || O.arp.hz != N.arp.hz || O.arp.octRange != N.arp.octRange || O.arp.mode != N.arp.mode || O.arp.gate != N.arp.gate || O.arp.swing != N.arp.swing || O.arp.velocity != N.arp.velocity || O.arp.repeat != N.arp.repeat || O.arp.transpose != N.arp.transpose || O.arp.latch != N.arp.latch || advDiffers(O.arp.adv, N.arp.adv) || advDiffers(O.arp.advOther, N.arp.advOther) || O.arp.hasOther != N.arp.hasOther || O.arp.curIsA != N.arp.curIsA || O.arp.morphOn != N.arp.morphOn || O.arp.morphAmt != N.arp.morphAmt || O.arp.target != N.arp.target || O.arp.targets != N.arp.targets) { const bool on = arp.on; arp = N.arp; arp.on = on; }
        if (O.swingOn != N.swingOn) swingOn = N.swingOn; if (O.swingAmount != N.swingAmount) swingAmount = N.swingAmount;
        if (O.maxPolyphony != N.maxPolyphony) maxPolyphony = N.maxPolyphony; if (O.legato != N.legato) legato = N.legato;
        preset_ = std::move(p);
    }
    // ---- settings (call from the audio thread / between advance() calls)
    double bpm = 120; int maxPolyphony = 64; bool legato = false, swingOn = false; double swingAmount = 50;
    ArpCfg arp;
    double seqTickSeconds = 0;   // 0: exact (sample-accurate); 0.01: emulate the browser's 10 ms sequencer tick (parity tests)
    bool transportRunning() const { return running_; }

    // ---- piano-roll RECORD: the user's own note / bend / mod input is stamped with the heard beat into a lock-free ring (roll_rec.hpp); the UI thread drains it.
    void recArm(bool on) { if (on) recRing_.clear(); recOn_.store(on, std::memory_order_relaxed); }
    bool recArmed() const { return recOn_.load(std::memory_order_relaxed); }
    rollrec::Ring& recRing() { return recRing_; }
    void recPush(int kind, int note, double x) {
        if (!recOn_.load(std::memory_order_relaxed) || !running_) return;
        if (kind >= 2) recLastCc_ = beat_;
        rollrec::Ev e; e.beat = beatHeard(); e.x = (float)x; e.kind = kind; e.note = note; recRing_.push(e);
    }

    // ---- keyboard / MIDI input
    void noteDown(int note, double velocity) {
        if (!preset_) return;
        clips_.noteDown(note, velocity, beat_);   // records into an armed clip (no-op otherwise)
        // Hold latches the arp, per layer: with the arp on, a note played into a layer that has Hold on stays in the arp after the key is released -
        // but only that layer keeps arpeggiating it. Tapping a latched note again drops it. (No separate Latch switch any more.)
        if (arp.on && contains(latched_, note)) { erase(latched_, note); erase(held_, note); return; }
        if (!contains(held_, note)) held_.push_back(note);
        if (arp.on && (arp.latch || holdArpMask(note)) && !contains(latched_, note)) latched_.push_back(note);
        const bool arpNow = arp.on;
        const bool legatoTransition = legato && !poly_.empty() && !contains(poly_, note);
        enforcePolyphony(note, legatoTransition);
        const int all = layersForNote(note, false); int normal = 0;
        for (int l = 0; l < kNumLayers; l++) if (all & (1 << l)) {
            if (arpNow && !layerDirectOsc(l, note)) continue;   // every oscillator of this layer is driven by the arpeggiator
            PLocks pl; const PLocks* plp = nullptr; if (arpNow) { pl.arpMode = 2; pl.arpMask = arpMask(l); plp = &pl; }
            if (layerHold(l, note)) { if (synth_->hasVoice(l, note)) synth_->noteOff(1 << l, note); else synth_->noteOn(1 << l, note, velocity, plp, legatoTransition); }
            else if (arpNow) synth_->noteOn(1 << l, note, velocity, plp, false);
            else normal |= 1 << l;
        }
        if (normal) synth_->noteOn(normal, note, velocity, nullptr, legatoTransition);
    }
    void noteUp(int note) {
        if (!preset_) return;
        clips_.noteUp(note, beat_);
        erase(held_, note); erase(poly_, note);   // physical key state; latched notes stay in latched_
        int m = 0;
        for (int l = 0; l < kNumLayers; l++) { if (!(layersForNote(note, false) & (1 << l))) continue; if (layerHold(l, note)) continue; if (arp.on && !layerDirectOsc(l, note)) continue; m |= 1 << l; }
        if (m) synth_->noteOff(m, note);
    }
    // MPC pads: play one sampler slot (slot >= 0) or a whole layer's zone-less one-shot at a fixed pitch, keyed by `padKey` (use a value outside 0..127).
    void padDown(int layer, int slot, int padKey, int pitchNote, double vel) {
        if (!preset_ || layer < 0 || layer >= kNumLayers) return;
        PLocks pl; pl.pitchNote = pitchNote; if (slot >= 0) { pl.arpMode = 1; pl.arpMask = 1u << slot; } else pl.arpMode = 1, pl.arpMask = 0xFFFFFFFFu;
        synth_->noteOn(1 << layer, padKey, vel, &pl, false);
    }
    // Sample-editor audition: plays sampler slot `slot` at its original pitch; slice 0 = the whole start..end region, 1..N = that transient slice
    // (position, end = next transient or the slot's end marker, reverse, pan - the same p-locks the sequencer's slice column uses).
    void padSlice(int slot, int slice, int padKey, double vel) {
        if (!preset_ || slot < 0 || slot >= (int)preset_->layers[L_SMP].oscs.size()) return;
        const OscCfg& o = preset_->layers[L_SMP].oscs[(size_t)slot];
        PLocks pl; pl.pitchNote = 60; pl.arpMode = 1; pl.arpMask = 1u << slot;
        if (slice > 0 && slice <= (int)o.trans.size()) {
            const int ti = slice - 1;
            pl.hasSamplePosition = true; pl.samplePosition = o.trans[(size_t)ti].pos; pl.hasSampleEnd = true;
            pl.sampleEndPosition = ti + 1 < (int)o.trans.size() ? o.trans[(size_t)ti + 1].pos : o.end;
            pl.hasSampleReverse = true; pl.sampleReverse = o.trans[(size_t)ti].reverse; pl.hasSamplePan = true; pl.samplePan = o.trans[(size_t)ti].pan;
        }
        synth_->noteOn(1 << L_SMP, padKey, vel, &pl, false);
    }
    void padUp(int layer, int padKey) { if (layer >= 0 && layer < kNumLayers) synth_->noteOff(1 << layer, padKey, true); }
    void setLatch(bool) {}   // removed feature (kept so the C ABI / old callers still link): Hold is the per-layer latch now
    // Hold switched off for a layer: release every note it was sustaining.
    void releaseLayerHold(int layer) { for (auto& v : synth_->voices()) if (v.alive && !v.released && v.layer == layer) synth_->noteOff(1 << layer, v.note); refreshLatched(); }
    // Latched notes whose layer no longer has Hold on (or that the arp no longer drives) fall out of the arp.
    void refreshLatched() { for (size_t i = 0; i < latched_.size();) { if (!arp.on || (!arp.latch && !holdArpMask(latched_[i]))) latched_.erase(latched_.begin() + (long)i); else i++; } }
    void setArpOn(bool on) { arp.on = on; if (!on) latched_.clear(); }
    void panic() {
        synth_->clearPending(); synth_->stopAllVoices(0.01);
        held_.clear(); latched_.clear(); poly_.clear(); stopTransport();
    }
    void startTransport() {
        if (!preset_) return; synth_->rrReset(); running_ = true; tickAcc_ = 0; chordReset(); drumReset(); beat_ = hostValid_ ? hostBeat_ : 0; for (int l = 0; l <= kNumLayers; l++) rollPrev_[l] = -1;
        for (int l = 0; l <= kNumLayers; l++) { SeqState& s = seq_[l]; const LayerCfg& L = seqLayer(l); s.acc = 0; s.stepIdx = 0;
            if (L.chainMode != "off" && !L.chainOrder.empty()) { s.chainPos = 0; s.finished = false; s.slot = L.chainOrder[0]; } }
    }
    void stopTransport() { running_ = false; clips_.stopAll(); if (synth_) synth_->setClipLaneValues(nullptr, 0); chordStop(); drumStop(); rollStop(); if (synth_) synth_->setAutoClock(beat_, false); }
    // AUv3: the host owns transport and tempo. Call once per render quantum with the host's playhead (quarter notes) before advance(); standalone leaves this unused.
    void setHostTransport(bool valid, double beat, double hostBpm) { hostValid_ = valid; hostBeat_ = beat; if (valid && hostBpm > 0) bpm = hostBpm; }
    double beat() const { return beat_; }   // transport beat (quarter notes) the piano roll and automation lanes follow

    // ---- clock: schedules arp / sequencer events for the quantum [qStart, qStart + n)
    void advance(uint64_t qStart, int n) { advanceCore(qStart, n); recordHist(qStart + (uint64_t)n); publishArpSnapshot(); }
    // ---- read-only view of what the arp is playing, for the UI thread (RANDOMISE fits new notes to it). Written once per audio quantum with relaxed atomics: no locks, no allocation.
    struct ArpSnap { uint64_t lo = 0, hi = 0; bool playing = false; int transpose = 0, advRel = 0; };
    ArpSnap arpSnapshot() const { ArpSnap s; s.lo = snapLo_.load(std::memory_order_relaxed); s.hi = snapHi_.load(std::memory_order_relaxed); s.playing = snapOn_.load(std::memory_order_relaxed); s.transpose = snapTr_.load(std::memory_order_relaxed); s.advRel = snapRel_.load(std::memory_order_relaxed); return s; }
    // Notes -> palette -> melodic pitches (strong[i] marks strong beats). Returns false when the arp is not playing (caller keeps its normal behaviour).
    bool arpFitPitches(int count, const uint8_t* strong, int kbMask, uint32_t seed, int lo, int hi, int* out, harmony::Palette* palOut = nullptr) const {
        const ArpSnap s = arpSnapshot(); if (!s.playing || count <= 0) return false;
        int notes[128]; int n = 0; for (int i = 0; i < 128 && n < 128; i++) if ((i < 64 ? (s.lo >> i) : (s.hi >> (i - 64))) & 1ull) notes[n++] = i;
        const harmony::Palette p = harmony::analyse(notes, n, s.transpose, s.advRel, kbMask); if (!p.active) return false;
        harmony::Rng rng{seed ? seed : 0x9E3779B9u}; int prev = -1; for (int i = 0; i < count; i++) { prev = harmony::pick(p, prev, strong ? strong[i] != 0 : (i % 4 == 0), rng, lo, hi); out[i] = prev; }
        if (palOut) *palOut = p; return true;
    }
    // ---- display latency: the engine runs ahead of what is audible by the output latency, so the playheads the UI shows (beat, step indices) are read `latSamples` in the past
    void setDisplayLatency(int samples) { latSamples_.store(samples < 0 ? 0 : samples, std::memory_order_relaxed); }
    bool running() const { return running_; }
    double beatNow() const { return beat_; }
    double beatHeard() const { return std::max(0.0, beat_ - latSamples_.load(std::memory_order_relaxed) / sr_ * bpm / 60.0); }
    int drumStepHeard(int lane) const { return lane >= 0 && lane < 16 ? histAt(0, lane, drumIdx_[lane]) : -1; }
    int stepHeard(int layer) const { return (layer >= 0 && layer < kNumLayers) ? histAt(1, layer, seq_[layer].stepIdx) : 0; }
private:
    static constexpr int kHist = 512;   // quanta of history (>= 1 s at 48 kHz / 128)
    struct HistEntry { uint64_t clock = 0; int16_t drum[16] = {}, seq[kNumLayers] = {}; };
    void recordHist(uint64_t clock) {
        const int w = (histW_.load(std::memory_order_relaxed) + 1) % kHist; HistEntry& h = hist_[w]; h.clock = clock;
        for (int i = 0; i < 16; i++) h.drum[i] = (int16_t)drumIdx_[i]; for (int l = 0; l < kNumLayers; l++) h.seq[l] = (int16_t)seq_[l].stepIdx;
        histW_.store(w, std::memory_order_release);
    }
    // value of kind (0 drum lane / 1 sequencer layer) as it was one display-latency ago; `now` when there is no latency or no history yet
    int histAt(int kind, int idx, int now) const {
        const int lat = latSamples_.load(std::memory_order_relaxed); if (lat <= 0) return now;
        const int w = histW_.load(std::memory_order_acquire); const uint64_t latest = hist_[w].clock; if (latest == 0) return now;
        const uint64_t target = latest > (uint64_t)lat ? latest - (uint64_t)lat : 0;
        for (int k = 0; k < kHist; k++) { const HistEntry& h = hist_[(w - k + kHist) % kHist]; if (h.clock == 0) break; if (h.clock <= target) return kind == 0 ? h.drum[idx] : h.seq[idx]; }
        return now;
    }
    std::atomic<int> latSamples_{0}; std::atomic<int> histW_{0}; HistEntry hist_[kHist];
public:
    void advanceCore(uint64_t qStart, int n) {
        if (!preset_) return;
        const double t0 = (double)qStart / sr_, t1 = (double)(qStart + n) / sr_;
        // arpeggiator (free-running clock, like the always-on setInterval)
        if (t1 >= refreshNext_) { refreshLatched(); refreshNext_ = t1 + 0.4; }
        while (arpNext_ < t1) { const double tf = std::max(arpNext_, t0); arpStep(tf); arpNext_ += arpInterval(); }
        if (!running_) { if (synth_) synth_->setAutoClock(beat_, false); return; }
        const double b0 = hostValid_ ? hostBeat_ : beat_, b1 = b0 + (t1 - t0) * bpm / 60.0; beat_ = b0;
        if (synth_) synth_->setAutoClock(b0, true);
        chordAdvance(t0, t1);
        drumAdvance(t0, t1);
        rollAdvance(t0, t1, b0, b1);
        clipAdvance(t0, b0, b1);
        beat_ = b1; if (hostValid_) hostBeat_ = b1;
        if (seqTickSeconds > 0) {   // browser-parity mode: 10 ms ticks
            while (tickNext_ <= t0 - 1e-12) tickNext_ += seqTickSeconds;   // (re)sync after stop/start
            while (tickNext_ < t1) { const double tf = tickNext_; for (int l = 0; l < kNumLayers; l++) seqAdvanceLayer(l, seqTickSeconds, tf); tickNext_ += seqTickSeconds; }
        } else {
            for (int l = 0; l < kNumLayers; l++) {
                double t = t0; const double end = t1;
                for (int guard = 0; guard < 64 && t < end; guard++) {
                    const double need = seqStepDur(l) - seq_[l].acc;
                    if (need > end - t) { seq_[l].acc += end - t; break; }
                    t += std::max(0.0, need); seq_[l].acc += std::max(0.0, need);
                    if (!seqFire(l, t)) break;
                }
            }
        }
    }

    // ---- inspection (tests / UI)
    const std::vector<int>& heldNotes() const { return held_; }
    int currentStep(int layer) const { return seq_[layer].stepIdx; }
    int currentSlot(int layer) const { return seq_[layer].slot; }
    int stepIndex(int layer) const { return seq_[layer].stepIdx; }   // for playhead display (benign unsynchronised read)

private:
    struct SeqState { double acc = 0; int stepIdx = 0, slot = 0, chainPos = 0; bool finished = false; };
    double beat_ = 0, hostBeat_ = 0; bool hostValid_ = false; double rollPrev_[kNumLayers + 1] = {-1, -1, -1, -1, -1, -1, -1};   /* + index 6: the XFX pseudo layer (extra.xfxSeq) */
    static constexpr int kDrumKey = 1100;
    double drumAcc_[16] = {}; int drumIdx_[16] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}; bool drumHeld_[16] = {};
    Synth* synth_ = nullptr; double sr_ = 48000; std::shared_ptr<const Preset> preset_;
    std::vector<int> held_, latched_, poly_; SeqState seq_[kNumLayers + 1]; bool running_ = false; double arpNext_ = 0, refreshNext_ = 0, tickNext_ = 0, tickAcc_ = 0; int arpIdx_ = 0; uint32_t rng_ = 12345;
    double rand01() { rng_ = rng_ * 1664525u + 1013904223u; return (double)rng_ / 4294967296.0; }
    static bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }
    static void erase(std::vector<int>& v, int x) { v.erase(std::remove(v.begin(), v.end(), x), v.end()); }
    uint64_t frameAt(double t) const { const double f = std::ceil(t * sr_ - 1e-6); return f < 0 ? 0 : (uint64_t)f; }

    // layers whose keyboard zones contain the note (optionally excluding layers on Hold)
    // XFX: the layer as this key sees it (per-key zones / hold / arp assignment), else the normal layer
    const LayerCfg& keyL(int l, int note) const { if (preset_->xfx) { if (const LayerCfg* x = preset_->xfx->find(l, note)) return *x; } return preset_->layers[l]; }
    // XFX solo / mute (web xfxGated): SOLO XFX keeps only the XFX-zone keys audible (layer solos still apply per layer); MUTE XFX silences the zone keys.
    bool xfxGated(int note) const {
        if (!preset_ || !preset_->xfx || !synth_) return false;
        const bool z = preset_->xfx->inZone(note); const int so = synth_->soloMask(), mu = synth_->muteMask();
        if (z && (mu & kXfxBit)) return true;
        if ((so & kXfxBit) && !z) return (so & (kXfxBit - 1)) == 0;
        return false;
    }
    int layersForNote(int note, bool excludeHold) const {
        if (xfxGated(note)) return 0;
        int m = 0;
        for (int l = 0; l < kNumLayers; l++) {
            bool in = false; for (auto& o : keyL(l, note).oscs) if (note >= o.rangeLow && note <= o.rangeHigh) in = true;
            if (preset_->xfx) { const int e = preset_->xfx->enable(l, note); if (e == 0) in = false; else if (e == 1 && l < (int)kNumLayers) in = true; }   // XFX per-key layer switch
            if (in && !(excludeHold && layerHold(l, note))) m |= 1 << l;
        }
        return m;
    }
    bool layerArpOsc(int l, int note) const { const unsigned a = arpMask(l); const auto& os = keyL(l, note).oscs; for (int i = 0; i < (int)os.size() && i < 32; i++) if (note >= os[i].rangeLow && note <= os[i].rangeHigh && ((a >> i) & 1u)) return true; return false; }
    bool layerDirectOsc(int l, int note) const { const unsigned a = arpMask(l); const auto& os = keyL(l, note).oscs; for (int i = 0; i < (int)os.size() && i < 32; i++) if (note >= os[i].rangeLow && note <= os[i].rangeHigh && !((a >> i) & 1u)) return true; return false; }
    int holdArpMask(int note) const { int m = 0; for (int l = 0; l < kNumLayers; l++) if (layerArpOsc(l, note) && layerHold(l, note)) m |= 1 << l; return m; }
    bool layerHold(int l, int note) const { if (preset_->xfx && preset_->xfx->hold && preset_->xfx->inZone(note)) return true;   // HOLD XFX latches the XFX zone on every layer
        for (auto& o : keyL(l, note).oscs) if (note >= o.rangeLow && note <= o.rangeHigh && o.hold) return true; return false; }

    void enforcePolyphony(int newNote, bool legatoTransition) {
        const int mx = maxPolyphony > 0 ? maxPolyphony : 64;
        if (mx == 1) {
            for (int n : poly_) { if (n == newNote) continue;
                if (legatoTransition) synth_->quickStop(0x3F, n, 0.008); else synth_->noteOff(0x3F, n); }
            poly_.clear(); poly_.push_back(newNote);
        } else {
            while ((int)poly_.size() >= mx && !contains(poly_, newNote)) { const int oldest = poly_.front(); poly_.erase(poly_.begin()); synth_->noteOff(0x3F, oldest); }
        }
        if (!contains(poly_, newNote)) poly_.push_back(newNote);
    }

    // ---- arpeggiator
    double arpHz() const {
        const double base = arp.rateMode == "hz" ? arp.hz : 1.0 / ((60.0 / bpm) * arp.division);
        return clampd(base + synth_->arpRateOffset(), 0.02, 80);
    }
    double arpInterval() const { return 1.0 / arpHz(); }
    // Which oscillators of a layer the arpeggiator drives: arp on, matching target, and (samples) the per-sample Arp On/Off box.
    bool targetMatch(int l, int idx, const OscCfg& o) const {
        (void)o; if (arp.targets.empty()) return true;
        for (const std::string& t : arp.targets) {
            if (t.empty() || t == "ALL") return true;
            if (l == L_WT) { if (t == (idx == 0 ? "WT1" : "WT2")) return true; }
            else if (l == L_FM) { if (t == (idx / 4 == 0 ? "FMA" : "FMB")) return true; }
            else if (l == L_PRO) { if (t == "PRO") return true; }
            else if (l == L_SID) { if (t == "SID") return true; }
            else if (l == L_LUA) { if (t == "LUA") return true; }
            else { const int num = idx + 1; if (t.size() == (num < 10 ? 2u : 3u) && t[0] == 'S' && std::atoi(t.c_str() + 1) == num) return true; }
        }
        return false;
    }
    unsigned arpMask(int l) const {
        unsigned m = 0; if (!arp.on || !preset_) return 0;
        const auto& os = preset_->layers[l].oscs;
        for (int i = 0; i < (int)os.size() && i < 32; i++) { if (!targetMatch(l, i, os[i])) continue; if (l == L_SMP && !os[i].arpOn) continue; m |= 1u << i; }
        return m;
    }
    unsigned directMask(int l) const {
        unsigned m = 0; const auto& os = preset_->layers[l].oscs; const unsigned a = arpMask(l);
        for (int i = 0; i < (int)os.size() && i < 32; i++) if (!((a >> i) & 1u)) m |= 1u << i;
        return m;
    }
    void playArpNote(int packed, double t, double vel = 0.9, double gateSec = -1) {   // packed = expanded note * 256 + the key it came from (octave copies inherit the key's down / latched state)
        const int n = std::max(0, std::min(127, (packed >> 8) + arp.transpose)), src = packed & 255, base = packed >> 8;
        if (gateSec < 0) gateSec = arpInterval() * 0.8;
        const int m = layersForNote(n, false); if (!m) return;
        const bool down = contains(held_, src), lat = contains(latched_, src); (void)base;
        for (int l = 0; l < kNumLayers; l++) if (m & (1 << l)) {
            const unsigned am = arpMask(l); if (!am || !layerArpOsc(l, n)) continue;
            if (!(down || (lat && (arp.latch || layerHold(l, src))))) continue;
            PLocks pl; pl.arpMode = 1; pl.arpMask = am;
            synth_->noteOnAt(frameAt(t), 1 << l, n, vel, &pl);
            synth_->noteOffAt(frameAt(t + std::max(0.03, gateSec)), 1 << l, n, true);
        }
    }
    // Fixed-size scratch (no heap use on the audio thread): up to 128 held notes x octave range, capped at kArpMax steps.
    static constexpr int kArpMax = 1024;
    int arpNotes_[128], arpExp_[kArpMax], arpSeq_[kArpMax];
    static bool advDiffers(const ArpAdvCfg& a, const ArpAdvCfg& b) { return a.algo != b.algo || a.scale != b.scale || a.seed != b.seed || a.probability != b.probability || a.density != b.density || a.complexity != b.complexity || a.mutation != b.mutation || a.variation != b.variation || a.randomness != b.randomness || a.humanise != b.humanise || a.octSpread != b.octSpread || a.ratchet != b.ratchet || a.velVar != b.velVar || a.accent != b.accent || a.gateVar != b.gateVar || a.groove != b.groove || a.patLen != b.patLen || a.cycleVar != b.cycleVar || a.rotation != b.rotation || a.skipProb != b.skipProb || a.rhythmDisp != b.rhythmDisp || a.mutSalt != b.mutSalt || a.mutAmt != b.mutAmt || a.varSeed != b.varSeed; }
    std::atomic<uint64_t> snapLo_{0}, snapHi_{0}; std::atomic<bool> snapOn_{false}; std::atomic<int> snapTr_{0}, snapRel_{0};
    void publishArpSnapshot() {
        uint64_t lo = 0, hi = 0; bool on = arp.on && (!held_.empty() || !latched_.empty());
        if (on) { for (int n : held_) { if (n >= 0 && n < 128) (n < 64 ? lo : hi) |= 1ull << (n & 63); } for (int n : latched_) { if (n >= 0 && n < 128) (n < 64 ? lo : hi) |= 1ull << (n & 63); } }
        int rel = 0; if (on && arp.mode.size() == 8 && arpadv::algoIndex(arp.adv.algo) == 8) rel = arpadv::scaleRelMask(arpadv::scaleIndex(arp.adv.scale));
        snapLo_.store(lo, std::memory_order_relaxed); snapHi_.store(hi, std::memory_order_relaxed); snapOn_.store(on, std::memory_order_relaxed); snapTr_.store(arp.transpose, std::memory_order_relaxed); snapRel_.store(rel, std::memory_order_relaxed);
    }
    int arpWalk_ = 0, arpStrumDir_ = 1, arpOrbit_ = 0;
    // The step engine shared by STANDARD and (next stage) ADVANCED. Mirrors the web arpNextEvents(): builds this step's events into evN_/evNote_/evOff_.
    static constexpr int kEvMax = kArpMax;
    int evN_ = 0, evNote_[kEvMax], evR_[kEvMax], evDec_[kEvMax]; double evOff_[kEvMax], evV_[kEvMax], evG_[kEvMax];
    int arpNextEvents(int nn, bool asPlayed) {
        const std::string& ord = arp.order; const int oct = std::max(1, std::min(4, arp.octRange));
        if (!asPlayed) std::sort(arpNotes_, arpNotes_ + nn);
        int ne = 0; for (int o = 0; o < oct; o++) for (int i = 0; i < nn; i++) if (ne < kArpMax) arpExp_[ne++] = (arpNotes_[i] + 12 * o) * 256 + arpNotes_[i];
        evN_ = 0; if (ne == 0) return 0;
        for (int q = 0; q < kEvMax; q++) { evV_[q] = 1; evG_[q] = 1; evR_[q] = 1; evDec_[q] = 0; }
        if (arp.mode == "advanced") {   // generative algorithms: ascending expanded list, one pure call per step
            static thread_local int E[kArpMax]; int en = 0; for (int i = 0; i < ne; i++) E[en++] = arpExp_[i] >> 8; std::sort(E, E + en);
            arp.adv.algoId = arpadv::algoIndex(arp.adv.algo); arp.adv.scaleId = arpadv::scaleIndex(arp.adv.scale);
            arpadv::Ev evs[arpadv::kMaxEv]; const int k = arpIdx_++; int cnt;
            if (arp.morphOn && arp.hasOther) { arp.advOther.algoId = arpadv::algoIndex(arp.advOther.algo); arp.advOther.scaleId = arpadv::scaleIndex(arp.advOther.scale);
                cnt = arp.curIsA ? arpadv::genMorph(arp.adv, arp.advOther, arp.morphAmt, E, en, k, evs) : arpadv::genMorph(arp.advOther, arp.adv, arp.morphAmt, E, en, k, evs); }
            else cnt = arpadv::gen(arp.adv, E, en, k, evs);
            for (int q = 0; q < cnt; q++) {
                const int base = E[evs[q].i]; int src = base; for (int i = 0; i < ne; i++) if ((arpExp_[i] >> 8) == base) { src = arpExp_[i] & 255; break; }
                evNote_[evN_] = std::max(0, std::min(127, base + evs[q].ds)) * 256 + src; evOff_[evN_] = evs[q].o; evV_[evN_] = evs[q].v; evG_[evN_] = evs[q].g; evR_[evN_] = evs[q].r; evDec_[evN_] = evs[q].dec; evN_++;
            }
            return k;
        }
        auto one = [&](int v) { evNote_[0] = v; evOff_[0] = 0; evN_ = 1; };
        auto rnd = [&](int k) { return (int)(rand01() * k) % k; };
        int ns = 0;
        auto up = [&]() { for (int i = 0; i < ne; i++) arpSeq_[ns++] = arpExp_[i]; };
        auto down = [&]() { for (int i = ne - 1; i >= 0; i--) arpSeq_[ns++] = arpExp_[i]; };
        if (ord == "DOWN") down();
        else if (ord == "UP/DOWN") { up(); for (int i = ne - 2; i >= 1 && ns < kArpMax; i--) arpSeq_[ns++] = arpExp_[i]; }
        else if (ord == "DOWN/UP") { down(); for (int i = 1; i <= ne - 2 && ns < kArpMax; i++) arpSeq_[ns++] = arpExp_[i]; }
        else if (ord == "PENDULUM") { up(); for (int i = ne - 1; i >= 0 && ns < kArpMax; i--) arpSeq_[ns++] = arpExp_[i]; }
        else if (ord == "SPIRAL") { for (int i = 0, j = ne - 1; i <= j; i++, j--) { arpSeq_[ns++] = arpExp_[i]; if (j != i) arpSeq_[ns++] = arpExp_[j]; } }
        else if (ord == "RANDOM") { one(arpExp_[rnd(ne)]); arpIdx_++; return arpIdx_ - 1; }
        else if (ord == "RANDOM WALK") { arpWalk_ = std::max(0, std::min(ne - 1, arpWalk_)); const int d = rnd(3) - 1; int w = arpWalk_ + d; if (w < 0 || w > ne - 1) w = arpWalk_ - d; arpWalk_ = std::max(0, std::min(ne - 1, w)); one(arpExp_[arpWalk_]); arpIdx_++; return arpIdx_ - 1; }
        else if (ord == "CHORD") { for (int i = 0; i < ne; i++) { evNote_[evN_] = arpExp_[i]; evOff_[evN_++] = 0; } arpIdx_++; return arpIdx_ - 1; }
        else if (ord == "STRUM") { const int dir = arpStrumDir_; arpStrumDir_ = -arpStrumDir_; for (int i = 0; i < ne; i++) { evNote_[evN_] = arpExp_[dir > 0 ? i : ne - 1 - i]; evOff_[evN_++] = ((double)i / ne) * 0.5; } arpIdx_++; return arpIdx_ - 1; }
        else if (ord == "OCTAVE ORBIT") {
            const int k = arpOrbit_++, pass = k / nn; int o = 0; if (oct > 1) { const int per = 2 * (oct - 1), pos = pass % per; o = pos < oct ? pos : per - pos; }
            const int b = arpNotes_[k % nn]; one((b + 12 * o) * 256 + b); arpIdx_ = k + 1; return k;
        } else up();
        if (ns == 0) return 0;
        if (arpIdx_ >= ns) arpIdx_ = 0;
        one(arpSeq_[arpIdx_]); return arpIdx_++;
    }
    void arpStep(double t) {
        if (!arp.on || (held_.empty() && latched_.empty())) return;
        int nn = 0; for (int n : held_) { if (nn < 128) arpNotes_[nn++] = n; }
        for (int n : latched_) { if (!contains(held_, n) && nn < 128) arpNotes_[nn++] = n; }
        const int idx = arpNextEvents(nn, arp.order == "AS PLAYED");
        const double iv = arpInterval(); const int rep = std::max(1, std::min(4, arp.repeat));
        const double gate = std::max(0.05, std::min(0.95, arp.gate));
        const double swingPct = arp.swing > 50 ? arp.swing : (swingOn ? swingAmount : 50);
        const double sw = (idx % 2 == 1) ? iv * (swingPct - 50) / 100.0 : 0.0;
        for (int e = 0; e < evN_; e++) {
            const int hits = std::max(1, evR_[e]) * rep; const double sub = iv / hits; const double gm = std::min(0.95, gate * evG_[e]);
            for (int r = 0; r < hits; r++) playArpNote(evNote_[e], t + sw + evOff_[e] * iv + r * sub, std::min(1.0, arp.velocity * evV_[e] * (evDec_[e] ? std::pow(0.78, r) : 1.0)), sub * gm);
        }
    }

    // ---- chord maker (generative chords on the transport clock; the progression lives in the preset, see cm::generate)
    double chordAcc_ = 0; bool chordFirst_ = true; int chordStep_ = 0, chordOnset_ = 0, chordPlaying_ = -1;
    cm::Rng chordRng_{7};
    int chordActive_[16]; int chordActiveN_ = 0, chordMask_ = 0;
public:
    int chordPlaying() const { return chordPlaying_; }   // index of the chord now sounding (UI highlight; benign unsynchronised read)
private:
    int chordNoteMask(int note) const { return chordMask_ < 0 ? layersForNote(note, false) : chordMask_; }
    void chordReset() { chordAcc_ = 0; chordFirst_ = true; chordStep_ = 0; chordOnset_ = 0; chordPlaying_ = -1; chordActiveN_ = 0; }
    void chordStop() { if (chordActiveN_ > 0 && synth_) { for (int i = 0; i < chordActiveN_; i++) { const int mk = chordNoteMask(chordActive_[i]); if (mk) synth_->noteOff(mk, chordActive_[i]); } } chordActiveN_ = 0; chordPlaying_ = -1; }
    static int chordMaskFor(const std::string& t) { if (t == "XFX") return -1; /* per-note: the XFX key's layers */ if (t == "WT") return 1 << L_WT; if (t == "FM") return 1 << L_FM; if (t == "SMP") return 1 << L_SMP; if (t == "PRO") return 1 << L_PRO; if (t == "SID") return 1 << L_SID; if (t == "LUA") return 1 << L_LUA; return 0x3F; }
    void chordAdvance(double t0, double t1) {
        const cm::Cfg& c = preset_->chord; if (!c.on || c.prog.empty()) return;
        const double stepDur = (60.0 / bpm) * 2.0 / c.rate;
        double t = t0;
        for (int guard = 0; guard < 16; guard++) {
            if (chordFirst_) { chordFirst_ = false; chordAcc_ = 0; chordFire(t, stepDur); continue; }
            const double need = stepDur - chordAcc_;
            if (need > t1 - t) { chordAcc_ += t1 - t; break; }
            t += std::max(0.0, need); chordAcc_ = 0; chordFire(t, stepDur);
        }
    }
    void chordFire(double t, double stepDur) {
        const cm::Cfg& c = preset_->chord; const int G = c.euOn ? c.euN : c.steps; if (G <= 0) return;
        const int idx = chordStep_ % G; chordStep_++;
        int ci, hold = 1;
        if (c.euOn) {
            auto eu = [&](int p) { const int j = (((p - c.euR) % c.euN) + c.euN) % c.euN; return (j * c.euP) % c.euN < c.euP; };
            if (!eu(idx)) return;
            ci = chordOnset_ % (int)c.prog.size(); chordOnset_++;
            for (int k = 1; k <= G; k++) if (eu((idx + k) % G)) { hold = k; break; }
        } else ci = idx % (int)c.prog.size();
        chordPlaying_ = ci;
        const uint64_t at = frameAt(t);
        for (int i = 0; i < chordActiveN_; i++) { const int mk = chordNoteMask(chordActive_[i]); if (mk) synth_->noteOffAt(at, mk, chordActive_[i]); }   // the previous chord ends where the next begins
        chordActiveN_ = 0;
        const cm::Chord& ch = c.prog[(size_t)ci]; if (ch.rest) return;
        if (rand01() * 100 >= c.prob) return;
        chordMask_ = chordMaskFor(c.target);
        int notes[16]; cm::Rng rr(rngSeedFromClock()); const int nn = cm::playNotes(ch, c, rr, notes, 16);
        const double strum = c.strum / 1000.0, endT = t + std::max(0.03, stepDur * hold * c.gate);
        for (int k = 0; k < nn; k++) {
            const double tk = (k == 0 || strum <= 0) ? t : t + k * strum; if (tk >= endT) continue;
            const double vel = std::min(1.0, std::max(0.05, c.vel * (0.92 + rand01() * 0.16)));
            { const int mk = chordNoteMask(notes[k]); if (mk) { synth_->noteOnAt(frameAt(tk), mk, notes[k], vel); synth_->noteOffAt(frameAt(endT), mk, notes[k]); } } chordActive_[chordActiveN_++] = notes[k];
        }
    }
    uint32_t rngSeedFromClock() { rng_ = rng_ * 1664525u + 1013904223u; return rng_ ^ 0x9E3779B9u; }

    // ---- step sequencer
    double seqStepDur(int l) const {   // duration of the step the clock is currently counting down (pre-increment index, like the web)
        const LayerCfg& L = preset_->layers[l]; const SeqState& s = seq_[l];
        if (L.patterns.empty() || s.slot < 0 || s.slot >= (int)L.patterns.size()) return 1e30;
        const SeqPattern& pat = L.patterns[s.slot]; if (pat.roll.present || pat.length <= 0 || pat.steps.empty()) return 1e30;
        const SeqStep& cur = pat.steps[s.stepIdx % pat.length];
        const double sixteenth = (60.0 / bpm) / 4;
        const double swingMult = swingOn ? (s.stepIdx % 2 == 0 ? (2 * swingAmount / 100.0) : (2 * (100 - swingAmount) / 100.0)) : 1.0;
        return sixteenth * (1.0 / cur.tempoRatio) * swingMult;
    }
    void seqAdvanceLayer(int l, double dt, double tFire) {   // parity mode: acc += dt then fire if due
        const LayerCfg& L = preset_->layers[l]; if (L.patterns.empty()) return; const SeqPattern& pat = L.patterns[seq_[l].slot]; if (pat.roll.present || pat.length <= 0) return;
        seq_[l].acc += dt;
        if (seq_[l].acc >= seqStepDur(l)) { seq_[l].acc -= seqStepDur(l); fireAfterAcc(l, tFire); }
    }
    bool seqFire(int l, double tFire) {   // exact mode: acc already reached the step duration
        const LayerCfg& L = preset_->layers[l]; if (L.patterns.empty()) return false; const SeqPattern& pat = L.patterns[seq_[l].slot]; if (pat.length <= 0) return false;
        const double dur = seqStepDur(l); seq_[l].acc -= dur; if (seq_[l].acc < 0) seq_[l].acc = 0;
        fireAfterAcc(l, tFire, dur); return true;
    }
    void fireAfterAcc(int l, double tFire, double durOverride = -1) {
        const LayerCfg& L = preset_->layers[l]; SeqState& s = seq_[l];
        const SeqPattern& pat = L.patterns[s.slot];
        const double stepDur = durOverride > 0 ? durOverride : seqStepDurFor(l);
        const bool wasLast = s.stepIdx == pat.length - 1;
        s.stepIdx = (s.stepIdx + 1) % pat.length;
        const SeqPattern* active = &pat;
        if (wasLast && L.chainMode != "off" && !L.chainOrder.empty() && !s.finished) {
            s.chainPos += 1;
            if (s.chainPos >= (int)L.chainOrder.size()) { if (L.chainMode == "loop") s.chainPos = 0; else { s.chainPos = (int)L.chainOrder.size() - 1; s.finished = true; } }
            s.slot = L.chainOrder[s.chainPos]; if (s.slot < 0 || s.slot >= (int)L.patterns.size()) s.slot = 0;
            active = &L.patterns[s.slot]; s.stepIdx = 0;
        }
        if (s.stepIdx >= (int)active->steps.size()) return;
        const SeqStep& st = active->steps[s.stepIdx];
        if (!(st.on && rand01() * 100 < st.prob)) return;
        const int note = 60 + (int)st.pitch; PLocks pl;
        if (st.pl.hasLevel) { pl.hasLevel = true; pl.level = st.pl.level; }
        if (st.pl.hasCutoff) { pl.hasCutoff = true; pl.cutoff = st.pl.cutoff; }
        if (st.pl.hasPosition) { pl.hasPosition = true; pl.position = st.pl.position; }
        if (l == L_SMP && st.transientIdx > 0 && !preset_->layers[L_SMP].oscs.empty()) {
            const OscCfg& o0 = preset_->layers[L_SMP].oscs[0]; const int ti = st.transientIdx - 1;
            if (ti < (int)o0.trans.size()) {
                pl.hasSamplePosition = true; pl.samplePosition = o0.trans[ti].pos; pl.hasSampleEnd = true;
                pl.sampleEndPosition = ti + 1 < (int)o0.trans.size() ? o0.trans[ti + 1].pos : o0.end;
                pl.hasSampleReverse = true; pl.sampleReverse = o0.trans[ti].reverse; pl.hasSamplePan = true; pl.samplePan = o0.trans[ti].pan;
            }
        }
        const int r = std::max(1, st.ratchet);
        for (int k = 0; k < r; k++) {
            const double t = tFire + k * (stepDur / r);
            synth_->noteOnAt(frameAt(t), 1 << l, note, 0.9, &pl);
            synth_->noteOffAt(frameAt(t + (stepDur / r) * L.gate), 1 << l, note);
        }
    }
    // ---- piano roll: a pattern that carries a roll (web pat.roll) is played by the transport beat clock instead of the step grid. Notes are placed in beats; the loop
    // region (in bars) repeats; a chain advances at each loop wrap. Probability, ratchet and per-note p-locks (incl. a locked sampler slot for MPC-recorded pads) apply.
    void rollStop() { for (int l = 0; l <= kNumLayers; l++) rollPrev_[l] = -1; if (synth_) { if (laneApplied_[0]) synth_->pitchBend = 0; if (laneApplied_[1]) synth_->modwheel = 0; } laneApplied_[0] = laneApplied_[1] = false; }
    // recorded bend / mod lanes replay with the roll (zero-order hold: the last point inside the range wins). The player's own hand wins while recording.
    void applyRollLanes(const RollData& RD, double from, double to) {
        if (!synth_ || (RD.bend.empty() && RD.mod.empty())) return;
        if (recOn_.load(std::memory_order_relaxed) && std::fabs(beat_ - recLastCc_) < 0.5) return;
        for (int k = 0; k < 2; k++) { const std::vector<LanePt>& L = k ? RD.mod : RD.bend; const LanePt* last = nullptr;
            for (const LanePt& q : L) if (q.t >= from - 1e-9 && q.t < to - 1e-9) last = &q;
            if (last) { if (k) synth_->modwheel = std::min(1.0, std::max(0.0, last->v)); else synth_->pitchBend = std::min(1.0, std::max(-1.0, last->v)); laneApplied_[k] = true; } }
    }
    rollrec::Ring recRing_; std::atomic<bool> recOn_{false}; double recLastCc_ = -1e9; bool laneApplied_[2] = {false, false};
    /* index kNumLayers (6) is the XFX pseudo layer: its patterns live in Preset::xfxSeq and its notes expand to the layers each XFX key programs */
    const LayerCfg& seqLayer(int l) const { return l >= kNumLayers ? preset_->xfxSeq : preset_->layers[l]; }
    int seqMask(int l, int note) const { return l >= kNumLayers ? layersForNote(note, false) : (1 << l); }
    void rollAdvance(double t0, double t1, double b0, double b1) {
        const double spb = 60.0 / std::max(1.0, bpm);
        for (int l = 0; l <= kNumLayers; l++) {
            if (l == kNumLayers && !preset_->hasXfxSeq) continue;
            const LayerCfg& L = seqLayer(l); SeqState& s = seq_[l];
            if (L.patterns.empty() || s.slot < 0 || s.slot >= (int)L.patterns.size()) continue;
            const RollData* R = &L.patterns[s.slot].roll; if (!R->present) continue;
            const double ls = R->loopOn ? R->loopStart * 4 : 0, le = R->loopOn ? std::max(ls + 4, R->loopEnd * 4) : R->bars * 4, len = le - ls;
            auto posOf = [&](double b) { double m = std::fmod(b, len); if (m < 0) m += len; return ls + m; };
            const double p0 = posOf(b0), span = b1 - b0;
            // the beat at which each loop-relative position is reached inside this quantum
            auto fireRange = [&](const RollData& RD, double from, double to, double posAtStart) {   // notes with t in [from, to)
                for (const RollNote& n : RD.notes) {
                    if (n.t < from - 1e-9 || n.t >= to - 1e-9) continue;
                    const double dtBeats = n.t - posAtStart; fireRollNote(l, n, t0 + std::max(0.0, dtBeats) * spb, spb);
                }
                applyRollLanes(RD, from, to);
            };
            const bool first = rollPrev_[l] < 0;
            double q = p0, remaining = span, posStart = p0;
            const double startFrom = first ? p0 - 1e-6 : p0;   // the very first quantum includes notes sitting exactly on the downbeat
            double from = startFrom;
            for (int guard = 0; guard < 8 && remaining > 1e-12; guard++) {
                const double room = le - q;
                if (remaining < room - 1e-12) { fireRange(*R, from, q + remaining, posStart); q += remaining; remaining = 0; break; }
                fireRange(*R, from, le, posStart);   // up to the loop end, then wrap (chain moves to its next slot)
                remaining -= room; q = ls; from = ls - 1e-6; posStart = ls - (b1 - b0 - remaining);
                if (L.chainMode != "off" && !L.chainOrder.empty() && !s.finished) {
                    s.chainPos += 1;
                    if (s.chainPos >= (int)L.chainOrder.size()) { if (L.chainMode == "loop") s.chainPos = 0; else { s.chainPos = (int)L.chainOrder.size() - 1; s.finished = true; } }
                    s.slot = L.chainOrder[s.chainPos]; if (s.slot < 0 || s.slot >= (int)L.patterns.size()) s.slot = 0;
                    const RollData* N = &L.patterns[s.slot].roll; if (!N->present) { remaining = 0; break; } R = N;
                }
            }
            rollPrev_[l] = p0;
        }
    }
    void fireRollNote(int l, const RollNote& n, double t, double spb) {
        if (n.prob < 100 && rand01() * 100 >= n.prob) return;
        PLocks pl; if (n.pl.hasLevel) { pl.hasLevel = true; pl.level = n.pl.level; } if (n.pl.hasCutoff) { pl.hasCutoff = true; pl.cutoff = n.pl.cutoff; } if (n.pl.hasPosition) { pl.hasPosition = true; pl.position = n.pl.position; }
        if (l == L_SMP && n.slot >= 0 && n.slot < 32) { pl.arpMode = 1; pl.arpMask = 1u << n.slot; pl.slot = n.slot; }   // an MPC-recorded pad plays only its own sampler slot
        else if (l == L_SMP && n.tr > 0 && !preset_->layers[L_SMP].oscs.empty()) {
            const OscCfg& o0 = preset_->layers[L_SMP].oscs[0]; const int ti = n.tr - 1;
            if (ti < (int)o0.trans.size()) { pl.hasSamplePosition = true; pl.samplePosition = o0.trans[ti].pos; pl.hasSampleEnd = true; pl.sampleEndPosition = ti + 1 < (int)o0.trans.size() ? o0.trans[ti + 1].pos : o0.end;
                pl.hasSampleReverse = true; pl.sampleReverse = o0.trans[ti].reverse; pl.hasSamplePan = true; pl.samplePan = o0.trans[ti].pan; }
        }
        const int R = std::max(1, n.rat); const double sub = n.l * spb / R, vel = std::min(1.0, std::max(0.02, n.v));
        for (int k = 0; k < R; k++) {
            const double tk = t + k * sub;
            const int mk = seqMask(l, n.p); if (!mk) continue;
            synth_->noteOnAt(frameAt(tk), mk, n.p, vel, &pl);
            synth_->noteOffAt(frameAt(tk + std::max(0.02, sub * (R > 1 ? 0.9 : 1.0))), mk, n.p, pl.arpMode == 1);
        }
    }
    // ---- clip launcher (see clips.hpp). Commands arrive on the audio thread through the engine's command ring.
public:
    void setClipSet(std::shared_ptr<const clips::ClipSet> s) { clips_.setSet(std::move(s)); }
    void clipLaunch(int slot) { clips_.launch(slot, beat_); }
    void clipStop(int slot, bool immediate) { clips_.stop(slot, beat_, immediate); }
    void clipStopAll() { clips_.stopAll(); }
    void clipArm(int slot) { if (slot < 0) clips_.disarmRecord(beat_); else clips_.armRecord(slot); }
    bool hostControlled() const { return hostValid_; }
    clips::ClipPlayer& clipPlayer() { return clips_; }
    const clips::ClipPlayer& clipPlayerConst() const { return clips_; }
    clips::ClipPlayer& clipPlayerMut() { return clips_; }
private:
    clips::ClipPlayer clips_;
    void clipAdvance(double t0, double b0, double b1) {
        const double spb = 60.0 / std::max(1.0, bpm);
        clips_.advance(b0, b1, [&](int layer, const clips::Note& n, double absBeat, int tr) { fireClipNote(layer, n, t0 + std::max(0.0, absBeat - b0) * spb, spb, tr); });
        if (synth_) { double v[clips::kMaxLaneRefs]; int cnt = 0; if (const clips::ClipSet* cs = clips_.set()) for (const clips::LaneRef& r : cs->laneRefs) { if (cnt >= clips::kMaxLaneRefs) break; v[cnt++] = clips_.laneValue(r, b0); } synth_->setClipLaneValues(v, cnt); }
    }
    void fireClipNote(int layer, const clips::Note& n, double t, double spb, int tr) {
        if (layer < 0 || layer > kNumLayers) return; const int p = std::max(0, std::min(127, n.p + tr)); const double vel = std::min(1.0, std::max(0.02, n.v)); PLocks pl;
        const int mk = seqMask(layer, p); if (!mk) return;   /* layer 6 = XFX: the key's own layers */
        synth_->noteOnAt(frameAt(t), mk, p, vel, &pl);
        synth_->noteOffAt(frameAt(t + std::max(0.02, n.l * spb)), mk, p, false);
    }
public:
    // ---- 16-lane drum sequencer: every lane runs straight sixteenths (no swing) over its own length (polyrhythms), one-shotting sampler slot `lane`
    void drumReset() { for (int i = 0; i < 16; i++) { drumAcc_[i] = 0; drumIdx_[i] = -1; } }
    void drumStop() { if (!synth_) return; for (int i = 0; i < 16; i++) if (drumHeld_[i]) { synth_->noteOff(1 << L_SMP, kDrumKey + i, true); drumHeld_[i] = false; } }
    void drumAdvance(double t0, double t1) {
        if (!preset_->drumOn) return;
        const double sixteenth = (60.0 / std::max(1.0, bpm)) / 4.0;
        for (int i = 0; i < 16; i++) {
            const int len = preset_->drumLen[i]; if (len <= 0) continue;
            double t = t0;
            for (int guard = 0; guard < 64 && t < t1; guard++) {
                const double need = sixteenth - drumAcc_[i];
                if (need > t1 - t) { drumAcc_[i] += t1 - t; break; }
                t += std::max(0.0, need); drumAcc_[i] = 0;
                drumIdx_[i] = (drumIdx_[i] + 1) % len;
                if (preset_->drumSteps[i][drumIdx_[i]] && preset_->drumXfx) {   /* lane i plays XFX key (first key + i): the key's own voice, pitch, envelope and repeat */
                    const int key = std::max(0, std::min(127, preset_->drumBase + i)), mk = layersForNote(key, false);
                    if (mk) { synth_->noteOnAt(frameAt(t), mk, key, 0.9); synth_->noteOffAt(frameAt(t + std::max(0.04, sixteenth * 0.9)), mk, key); }
                } else if (preset_->drumSteps[i][drumIdx_[i]]) {
                    PLocks pl; pl.pitchNote = 60; pl.arpMode = 1; pl.arpMask = 1u << i;
                    if (drumHeld_[i] && i < (int)preset_->layers[L_SMP].oscs.size() && (preset_->layers[L_SMP].oscs[i].loop || preset_->layers[L_SMP].oscs[i].pingPong)) synth_->noteOffAt(frameAt(t), 1 << L_SMP, kDrumKey + i, true);   // a looping slot must not stack
                    synth_->noteOnAt(frameAt(t), 1 << L_SMP, kDrumKey + i, 0.9, &pl); drumHeld_[i] = true;
                }
            }
        }
    }
public:
    int drumStep(int lane) const { return lane >= 0 && lane < 16 ? drumIdx_[lane] : -1; }   // playhead for the UI
private:
    double seqStepDurFor(int l) const { return seqStepDur(l); }
};

}  // namespace vv
