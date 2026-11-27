// Implementation of vv::Synth (see synth.hpp).
#pragma once
namespace vv {

inline double F32(double x) { return (double)(float)x; }   // AudioParam values are float32

inline void Synth::prepare(double sr) {
    sr_ = sr; clock_ = 0;
    for (int i = 0; i < kNumMix; i++) { mixMeter_[i].store(0.f); mixTgt_[i] = 1; mixOffDb_[i] = 0; mixPeak_[i] = 0; }
    for (int i = 0; i < 16; i++) { smpHead_[i].store(-1.f); smpHeadAcc_[i] = -1.f; }
    voices_.assign(kMaxVoices, Voice{});
    for (auto& v : voices_) {
        for (auto& g : v.g) g.filt.prepareShared(sr);
        for (auto& o : v.wt) for (auto& w : o.w) w.prepare(sr);
        v.sync.prepare(sr);
        for (auto& f : v.fm) f.fm.prepare(sr);
        for (auto& p : v.pro) { p.proc.prepare(sr, 12345); p.fl = flute::Core(sr, 12345); }
    }
    seedNoise(4242);
    // comb-filter delay lines come from a small shared pool
    combPool_.assign(48, std::vector<float>(8192, 0.f)); combFree_.clear(); for (int i = 47; i >= 0; i--) combFree_.push_back(i);
    fxPool_.assign(256, ColFx{}); fxFree_.clear(); fxFree_.reserve(256); for (int i = 255; i >= 0; i--) fxFree_.push_back(i);
    for (int l = 0; l < kNumLayers; l++) { busGain_[l].set(1); soloMute_[l].set(1); scDuck_[l].set(1); scEnv_[l] = 0; eq_[l].prepare(sr); }
    comp_.prepare(sr); mout_.prepare(sr); fx_.prepare(sr);
    masterGain_.set(masterVolume);
    for (auto& t : tone_) t = ChromiumLowpass{};
    outPos_ = kQuantum; events_.clear(); events_.reserve(2048);
    for (int l = 0; l < kNumLayers; l++) lastPlayed_[l].assign(16, -1.0);
    engineLast_[0] = engineLast_[1] = -1;
    { static const double df[4] = {120, 600, 2500, 8000};
      for (int l = 0; l < kNumLayers; l++) for (int b = 0; b < 4; b++) { eqFreq_[l][b].set(df[b]); eqGain_[l][b].set(0); eqFreq_[l][b].setTarget(df[b], sr, 0.01); eqGain_[l][b].setTarget(0, sr, 0.01); } }
    toneFreq_.set(masterToneToFreq(0)); toneFreq_.setTarget(masterToneToFreq(masterTone), sr, 0.01);
    for (auto& tt : tone_) tt.set(toneFreq_.v, 0.707, sr);
    // initAudio(): applyWtFmMix() issues setTargetAtTime on both bus gains at t=0 (they start at 1)
    busGain_[L_WT].setTarget(std::cos(wtFmMix * 1.5707963267948966), sr, 0.01);
    busGain_[L_FM].setTarget(std::sin(wtFmMix * 1.5707963267948966), sr, 0.01);
}

// Round 22: sub shape / noise colour name lookups, and coloured noise (brown = integrated white, violet = differentiated white, blue = differentiated pink;
// RMS-normalised to 0.3 with a cross-faded loop point, the same algorithm as the web's noiseColourFill()).
static inline int subShapeOf(const std::string& w) { return w == "triangle" ? 1 : w == "square" ? 2 : 0; }
static inline int noiseTypeIndex(const std::string& t) { return t == "pink" ? 1 : (t == "brown" || t == "red") ? 2 : t == "blue" ? 3 : t == "violet" ? 4 : 0; }
static inline void fillColouredNoise(std::vector<float>& out, int type, uint32_t seed) {
    const size_t len = out.size(); if (len < 8) return; const size_t N = std::min<size_t>(4096, len >> 2), tot = len + N;
    std::vector<double> raw(tot); JsRandom r; r.s = seed; double w1 = 0, b = 0, p0 = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0, p6 = 0, pk1 = 0;
    for (size_t i = 0; i < tot; i++) {
        const double w = r.next() * 2 - 1;
        if (type == 2) { b = b * 0.9995 + w * 0.02; raw[i] = b; }
        else if (type == 4) { raw[i] = w - w1; w1 = w; }
        else { p0 = 0.99886 * p0 + w * 0.0555179; p1 = 0.99332 * p1 + w * 0.0750759; p2 = 0.96900 * p2 + w * 0.1538520; p3 = 0.86650 * p3 + w * 0.3104856; p4 = 0.55000 * p4 + w * 0.5329522; p5 = -0.7616 * p5 - w * 0.0168980;
               const double pink = p0 + p1 + p2 + p3 + p4 + p5 + p6 + w * 0.5362; p6 = w * 0.115926; raw[i] = pink - pk1; pk1 = pink; }
    }
    double ss = 0, pk = 0; std::vector<double> d(len);
    for (size_t i = 0; i < len; i++) { d[i] = i < N ? raw[i] * ((double)i / (double)N) + raw[len + i] * (1.0 - (double)i / (double)N) : raw[i]; ss += d[i] * d[i]; pk = std::max(pk, std::fabs(d[i])); }
    const double rms = std::sqrt(ss / (double)len); double g = rms > 0 ? 0.3 / rms : 1.0; if (pk * g > 0.98) g = 0.98 / pk;
    for (size_t i = 0; i < len; i++) out[i] = (float)(d[i] * g);
}
inline void Synth::seedNoise(uint32_t seed) {
    // 2 seconds of white / pink noise per sample rate, like getNoiseBuffer() (looped by every noise source)
    const size_t len = (size_t)(sr_ * 2);
    for (int k = 0; k < 5; k++) noise_[k].assign(len, 0.f);
    { JsRandom r; r.s = seed; for (size_t i = 0; i < len; i++) noise_[0][i] = (float)(r.next() * 2 - 1); }
    { JsRandom r; r.s = seed; double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
      for (size_t i = 0; i < len; i++) { const double white = r.next() * 2 - 1;
        b0 = 0.99886 * b0 + white * 0.0555179; b1 = 0.99332 * b1 + white * 0.0750759; b2 = 0.96900 * b2 + white * 0.1538520;
        b3 = 0.86650 * b3 + white * 0.3104856; b4 = 0.55000 * b4 + white * 0.5329522; b5 = -0.7616 * b5 - white * 0.0168980;
        noise_[1][i] = (float)((b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362) * 0.11); b6 = white * 0.115926; } }
    fillColouredNoise(noise_[2], 2, seed + 11u); fillColouredNoise(noise_[3], 3, seed + 23u); fillColouredNoise(noise_[4], 4, seed + 37u);
}

inline void Synth::setPreset(std::shared_ptr<const Preset> p) {
    dx7ModOn_ = false; dx7LiveValid_ = false;
    for (auto& v : voices_) v.xcfg = nullptr;   // a wholesale preset swap drops the old XFX table: no voice may keep a pointer into it
    xfxVoices_.store(0, std::memory_order_relaxed);
    preset_ = std::move(p);
    for (int l = 0; l < kNumLayers; l++) {
        lastPlayed_[l].assign(16, -1.0);
        const auto& oscs = preset_->layers[l].oscs;
        for (size_t i = 0; i < oscs.size() && i < 16; i++) lastPlayed_[l][i] = oscs[i].lastPlayedFreq;
    }
    engineLast_[0] = engineLast_[1] = -1;
    fx_.configure(preset_->fx, preset_->bpm, preset_->oversample);
    modReset();
    for (int l = 0; l < kNumLayers; l++) { tap_[l].sr = sr_; }
    voc_.prepare(sr_); xmodRebuild(); rebuildVocoder();
    // updateEQ(): setTargetAtTime(..., 0.01) toward the new band settings
    for (int l = 0; l < kNumLayers; l++) for (int b = 0; b < 4; b++) {
        eqFreq_[l][b].setTarget(clampd(preset_->layers[l].eq[b].freq, 20, 18000), sr_, 0.01);
        eqGain_[l][b].setTarget(clampd(preset_->layers[l].eq[b].gain, -15, 15), sr_, 0.01);
    }
}

inline void Synth::noteOn(int mask, int note, double vel, const PLocks* pl, bool legato) {
    events_.push_back(Ev{clock_, 0, mask, note, vel, pl ? *pl : PLocks{}, legato});
}
inline void Synth::noteOff(int mask, int note, bool arpV) { PLocks p; p.arpMode = arpV ? 1 : 0; events_.push_back(Ev{clock_, 1, mask, note, 0, p, false}); }
inline void Synth::stopAllVoices(double relTime) {
    for (auto& v : voices_) if (v.alive && !v.released) stopVoice(v, relTime, now());
}

inline void Synth::applyEvents(uint64_t qStart) {
    size_t w = 0;
    for (size_t i = 0; i < events_.size(); i++) {
        Ev& e = events_[i];
        if (e.at > qStart) { events_[w++] = e; continue; }
        if (e.kind == 0 && e.mask) voc_.noteOn(e.note); else if (e.kind == 1 && e.mask) voc_.noteOff(e.note);
        if (e.kind == 0 && e.mask && preset_) { const Json& ef = preset_->envFollow; envFollowInfo_.valid = true; envFollowInfo_.start = (double)qStart / sr_; envFollowInfo_.a = ef.num("a", 0.01); envFollowInfo_.d = ef.num("d", 0.3); envFollowInfo_.s = ef.num("s", 0.4); envFollowInfo_.peak = e.vel; }
        for (int l = 0; l < kNumLayers; l++) if (e.mask & (1 << l)) { if (e.kind == 0) doNoteOn(l, e.note, e.vel, e.pl, e.legato); else if (e.kind == 2) { Voice* qv = findVoice(l, e.note); if (qv) stopVoice(*qv, e.vel, now()); } else doNoteOff(l, e.note, e.pl.arpMode == 1); }
    }
    events_.resize(w);
}

// New Note Next / Random Sample (web pickAdvanceSlot): one playable slot per new note - it needs a sample, a level > 0, the note inside its zone and
// must survive the arpeggiator split. Next walks the slots in order, skipping the others; Random picks uniformly among the playable ones. -1 = off / none playable.
inline int Synth::pickAdvanceSlot(int note, const PLocks& pl) {
    const LayerCfg& L = effL(L_SMP); if (L.slotAdvance == 0) return -1;
    const int n = (int)L.oscs.size(); int ok[32]; int cnt = 0;
    auto playable = [&](int i) { const OscCfg& o = L.oscs[(size_t)i]; if (note < o.rangeLow || note > o.rangeHigh || o.level <= 0) return false;
        if (pl.arpMode == 1 && !((pl.arpMask >> i) & 1u)) return false; if (pl.arpMode == 2 && ((pl.arpMask >> i) & 1u)) return false;
        auto it = samples_.find(o.name); return it != samples_.end() && it->second; };
    for (int i = 0; i < n && i < 32; i++) if (playable(i)) ok[cnt++] = i;
    if (!cnt) return -1;
    if (L.slotAdvance == 2) return ok[std::min(cnt - 1, (int)(rng_.next() * cnt))];
    for (int k = 0; k < n; k++) { const int i = (nextSlot_ + k) % n; for (int j = 0; j < cnt; j++) if (ok[j] == i) { nextSlot_ = (i + 1) % n; return i; } }
    return -1;
}

// Round Robin: the take (slot index whose sample plays) for this hit of slot `idx`; cents / gain are the per-hit variation. Allocation free (fixed arrays, read-only takes list).
inline int Synth::rrPick(const LayerCfg& L, int idx, int note, double& cents, double& gain) {
    cents = 0; gain = 1; const OscCfg& o = L.oscs[(size_t)idx]; const RrCfg& rr = o.rr; if (!rr.on || idx < 0 || idx >= 32) return idx;
    auto has = [&](int t) { if (t < 0 || t >= (int)L.oscs.size()) return false; auto it = samples_.find(L.oscs[(size_t)t].name); return it != samples_.end() && it->second; };
    int list[32]; int n = 0; if (has(idx)) list[n++] = idx;
    for (int t : rr.takes) { if (t == idx || !has(t) || n >= 32) continue; bool dup = false; for (int k = 0; k < n; k++) if (list[k] == t) dup = true; if (!dup) list[n++] = t; }
    if (n == 0) return idx; const double tnow = now(); int take = list[0];
    if (n > 1) {
        RrState* st = rr.scope == 1 ? &rrKey_[idx & 15][note & 127] : &rrSlot_[idx];
        if (st->epoch != rrEpoch_) { if (rr.scope == 2) { st->pos = 0; st->last = -1; st->dir = 1; } st->epoch = rrEpoch_; }
        if (rr.scope == 3 && tnow - st->t > 1.5) { st->pos = 0; st->last = -1; st->dir = 1; }
        int i;
        if (rr.order == 1) i = std::min(n - 1, (int)(rng_.next() * n));
        else if (rr.order == 2) { i = std::min(n - 2, (int)(rng_.next() * (n - 1))); if (st->last >= 0 && i >= st->last) i++; }
        else if (rr.order == 3) { i = st->pos; if (i >= n) { st->dir = -1; i = n - 2; } if (i < 0) { st->dir = 1; i = 1; } st->pos = i + st->dir; }
        else { i = st->pos % n; st->pos = (i + 1) % n; }
        st->last = i; st->t = tnow; take = list[i];
    }
    if (rr.variation > 0) { cents = (rng_.next() * 2 - 1) * 6 * rr.variation; gain = 1 + (rng_.next() * 2 - 1) * 0.15 * rr.variation; }
    rrLast_[idx].store(take + 1, std::memory_order_relaxed); return take;
}

inline void Synth::doNoteOn(int layer, int note, double vel, const PLocks& pl, bool legato) {
    if (!preset_) return;
    if (findVoice(layer, note, pl.arpMode == 1)) return;   // hasActiveVoice() guard (arp voices and directly played voices are tracked separately)
    Voice* slot = nullptr;
    for (auto& v : voices_) if (!v.alive) { slot = &v; break; }
    if (!slot) { uint64_t oldest = ~0ull; for (auto& v : voices_) if (v.released && v.age < oldest) { oldest = v.age; slot = &v; } if (!slot) for (auto& v : voices_) if (v.age < oldest) { oldest = v.age; slot = &v; } }
    PLocks pickPl; const PLocks* usePl = &pl;
    if (layer == L_SMP && pl.slot < 0 && pl.pitchNote < 0) { const int ps = pickAdvanceSlot(note, pl); if (ps >= 0) { pickPl = pl; pickPl.slot = ps; usePl = &pickPl; } }
    slot->noteVel = vel; startVoice(*slot, layer, note, *usePl, legato, now());
    slot->velocity = vel; slot->arpVoice = (pl.arpMode == 1);
    if (slot->dx.on) {   // DX7 voice: velocity is only known now; the patch's own envelopes shape loudness
        DxEng& d = slot->dx; const LayerCfg& LL = vL(*slot); const int vm = std::max(1, std::min(127, (int)jsRound(vel * 127)));
        d.v.noteOn(dxPatch(LL), d.note, vm, (uint32_t)(rng_.next() * 4294967295.0)); d.blkPos = dx7::N; d.started = true;
        d.releaseSec = dx7::Voice::estimateRelease(dxPatch(LL), d.note, vm, sr_);
    }
    { const LayerCfg& LL = vL(*slot); EnvInfo& ei = envInfo_[layer]; ei.valid = true; ei.start = now(); ei.a = LL.env.a; ei.d = LL.env.d; ei.s = LL.env.s; ei.peak = slot->levelBase; if (layer == L_WT) wtNoteStart_ = ei.start; }
    for (int k = 0; k < kNumLfo; k++) if (preset_->lfo[k].mode == "retrig") { lfo_[k].phase = 0; if (preset_->lfo[k].held) lfo_[k].holdCycle = true; }
    modInitVoice(*slot, now());
    // A layer with nothing to sound for this note (no enabled oscillator) must not hold a pool slot: with five layers every note would
    // otherwise cost five voices and 64-note polyphony would run out at 12 notes.
    { bool any = false; for (int gi = 0; gi < slot->ng; gi++) if (slot->g[gi].active) any = true;
      if (!any) { slot->alive = false; releaseVoiceFx(*slot); for (int gi = 0; gi < slot->ng; gi++) if (slot->g[gi].combSlot >= 0) { combFree_.push_back(slot->g[gi].combSlot); slot->g[gi].filt.detachComb(); slot->g[gi].combSlot = -1; } } }
}

inline void Synth::doNoteOff(int layer, int note, bool arpV) {
    if (!preset_) return;
    Voice* v = findVoice(layer, note, arpV);
    if (!v || v->released) return;
    stopVoice(*v, vL(*v).env.r, now());
}

// Builds one filter group (filter -> volume -> pan -> amp envelope), mirroring the per-group block of buildLayerVoice.
// Base (unmodulated) pan field behind one filter group, as the Mixer PAN dial edits it.
inline double Synth::groupBasePan(const LayerCfg& LP, int gi) {
    if (LP.type == LayerType::Wavetable) return LP.oscs[gi].pan;
    if (LP.type == LayerType::FM) return LP.sixOp ? LP.dx7Pan : LP.oscs[gi * 4].pan + LP.enginePan[gi < 2 ? gi : 1];
    if (LP.type == LayerType::SID) return clampd((gi < (int)LP.oscs.size() ? LP.oscs[gi].pan : 0) + LP.pan, -1, 1);
    return gi < (int)LP.oscs.size() ? LP.oscs[gi].pan : 0;
}
inline void Synth::setupGroup(Voice& v, int gi, const LayerCfg& L, const FilterCfg& gf, const FilterEnvCfg& fe, const EnvCfg& env, int note, const PLocks& pl,
                              bool legato, double t, double panValue, bool applyPan, bool plockCutoff, bool sampler) {
    Group& g = v.g[gi];
    const double baseCutoff = (plockCutoff && pl.hasCutoff) ? pl.cutoff : gf.cutoff;
    const double ckt = clampd(baseCutoff * std::pow(2.0, (note - 60) / 12.0 * (gf.keytrack / 100.0)), 20, 18000);
    g.ckt = F32(ckt); g.ftype = gf.typeIdx; g.reso = F32(gf.reso); g.lpg = gf.lpg; g.lpgDecay = gf.lpgDecay; g.fe = fe; g.fvol = F32(gf.volume);
    g.filt.reset(); g.filt.fr2 = gf.fr2; g.filt.fr3 = gf.fr3;
    g.cutoff.clear(); g.cutoff.setIntrinsic(4000);
    g.cutoff.setValueAtTime(ckt, t);
    if (!g.lpg && fe.amount != 0) {
        if (legato) g.cutoff.setValueAtTime(clampd(ckt + fe.amount * fe.s, 20, 18000), t);
        else {
            g.cutoff.linearRampToValueAtTime(clampd(ckt + fe.amount, 20, 18000), t + fe.a, t);
            g.cutoff.linearRampToValueAtTime(clampd(ckt + fe.amount * fe.s, 20, 18000), t + fe.a + fe.d, t);
        }
    }
    g.panned = applyPan; g.panGL = g.panGR = 1; g.bypass = !gf.on; g.panVal = panValue;   // gf.on false: the filter is skipped (the stack goes straight to the volume / pan stage)
    if (applyPan) monoPanGains(panValue, g.panGL, g.panGR);
    if (g.lpg) {
        const double decay = gf.lpgDecay, total = decay * 6;
        const int points = (int)std::min(400.0, std::max(40.0, jsRound(total * 80)));
        g.lpgPoints = points;
        for (int i = 0; i < points; i++) {
            const double tt = ((double)i / (points - 1)) * total; const double shape = (double)(float)(1.0 / (1.0 + tt / std::max(0.01, decay)));
            g.lpgCurve[i] = (float)(60.0 + shape * (ckt - 60.0));
        }
        g.cutoff.cancelScheduledValues(t); g.cutoff.setValueCurveAtTime(g.lpgCurve, points, t, total);
    }
    // amp envelope
    g.amp.clear(); g.amp.setIntrinsic(0);
    g.envR = env.r;
    if (sampler) g.amp.setValueAtTime(v.levelBase, t);
    else if (legato) g.amp.setValueAtTime(v.levelBase * env.s, t);
    else {
        g.amp.setValueAtTime(0, t);
        g.amp.linearRampToValueAtTime(v.levelBase, t + env.a, t);
        g.amp.linearRampToValueAtTime(v.levelBase * env.s, t + env.a + env.d, t);
        if (env.rep != 0 && !(L.type == LayerType::FM && L.sixOp)) g.rep.arm(env, v.levelBase, t);   // not for the 6-op DX7 voice (its own envelopes)
    }
    g.cutM = g.resoM = g.fvolM = g.levelM = ModSlot{};
    (void)L;
}

inline void Synth::startVoice(Voice& v, int layer, int note, const PLocks& pl, bool legato, double t) {
    // XFX: a key with its own programming plays a per-key resolved copy of the layer (built on the control thread; here it is only a hash lookup)
    v.xcfg = (preset_->xfx && !genOn_[layer]) ? preset_->xfx->find(layer, note) : nullptr;
    if (v.xcfg) xfxVoices_.store(1, std::memory_order_relaxed);
    const LayerCfg& L = v.xcfg ? *v.xcfg : effL(layer);
    for (auto& g : v.g) g.rep.on = false;
    for (auto& r : v.sidRep) r.on = false;
    const int keyNote = note; note = pl.pitchNote >= 0 ? pl.pitchNote : keyNote;   // pads: pitch from pitchNote, identity from the key
    releaseVoiceFx(v);
    v.alive = true; v.released = false; v.layer = layer; v.note = keyNote; v.startT = t; v.age = ++ageCounter_; v.nNoise = 0;
    v.levelBase = pl.hasLevel ? pl.level : 1;
    for (auto& g : v.g) g.active = false;
    for (auto& o : v.wt) o.on = false;
    for (auto& f : v.fm) f.on = false;
    for (auto& pg : v.peg) pg.on = false;   // R11 pitch envelopes are rebuilt per oscillator below
    v.dx.on = false; v.dx.started = false; v.sid.on = false; v.ring.on = false;
    for (auto& p : v.pro) p.on = false;
    killLuaVoices(v);
    for (auto& q : v.smp) q.on = false;
    v.wtSyncPair = nullptr;
    const int numGroups = L.type == LayerType::Wavetable ? 2 : L.type == LayerType::FM ? 2 : L.type == LayerType::Sample ? (int)L.oscs.size() : L.type == LayerType::SID ? 3 : 1;
    v.ng = numGroups;
    auto groupIdxFor = [&](int idx) { return L.type == LayerType::Wavetable ? idx : L.type == LayerType::FM ? idx / 4 : L.type == LayerType::Sample ? idx : L.type == LayerType::SID ? idx : 0; };
    double oscFreqByIndex[16]; bool freqSet[16]; for (int i = 0; i < 16; i++) { freqSet[i] = false; oscFreqByIndex[i] = 0; }
    // glide bookkeeping: (osc index, pointer to the PitchOffset, kind) filled while building
    struct PmRef { PitchOffset* p; int oscIndex; int kind; double built; };   // kind 0 freq(Hz) 1 cents 2 rate
    PmRef pms[64]; int npm = 0;
    auto lp = [&](int i) -> double& { return lastPlayed_[layer][i]; };

    for (int idx = 0; idx < (int)L.oscs.size(); idx++) {
        const OscCfg& o = L.oscs[idx];
        if (pl.pitchNote < 0 && (note < o.rangeLow || note > o.rangeHigh)) continue;
        if (pl.arpMode == 1 && !((pl.arpMask >> idx) & 1u)) continue;
        if (pl.arpMode == 2 && ((pl.arpMask >> idx) & 1u)) continue;
        if (L.type == LayerType::Sample && pl.slot >= 0 && idx != pl.slot) continue;
        if (L.type == LayerType::Wavetable && !o.enabled) continue;
        if (L.type == LayerType::Procedural && !o.enabled) continue;
        if (L.type == LayerType::SID) {
            if (idx != 0) continue;   // all three voices are built together on the first oscillator slot
            bool any = false; for (const OscCfg& so : L.oscs) if (so.enabled) any = true;
            if (!any) continue;
            const double fb = midi(layer, note, o);
            bool sidOverlap = false; for (const Voice& ov : voices_) if (ov.alive && ov.layer == layer && ov.sid.on && !ov.released) sidOverlap = true;   // Tie: another SID note is still held
            SidEng& s = v.sid; s.on = true; s.chip.reset(); s.tieNote = L.sidPerf.tie && (legato || sidOverlap); s.arpPhase = 0; s.lastT = -1; s.arpIdx = -2; s.arpRestore = false; for (double& gh : s.gateHigh) gh = -1;
            for (int k = 0; k < 3 && k < (int)L.oscs.size(); k++) {
                const OscCfg& so = L.oscs[k];
                static const struct { const char* n; int m; } wm[] = {{"triangle", 1}, {"sawtooth", 2}, {"pulse", 4}, {"noise", 8}, {"tri_saw", 3}, {"tri_pulse", 5}, {"saw_pulse", 6}};
                int wv = 1; for (auto& e : wm) if (so.sidWave == e.n) wv = e.m;
                SidVoiceParams& p = s.p[k];
                s.fb = fb; s.freq[k] = F32(fb * std::pow(2.0, so.oct) * std::pow(2.0, (so.semitone * 100 + so.fineTune) / 1200.0));
                p.freq = s.freq[k]; p.wave = wv; p.pw = clampd(so.pulseWidth, 0.01, 0.99); p.ring = so.ringModOn; p.sync = so.syncOn;
                p.ins = so.sidIns; p.ins.vibOn = L.sidPerf.vibrato; p.ins.susHold = L.sidPerf.sustain; p.ins.tie = L.sidPerf.tie && (legato || sidOverlap);
                const EnvCfg& se = so.hasEnv ? so.env : EnvCfg{0.01, 0.15, 0.65, 0.2};
                p.atk = std::max(0.001, se.a); p.dec = std::max(0.001, se.d); p.sus = clampd(se.s, 0, 1); p.rel = std::max(0.001, se.r); p.gate = so.enabled;
                v.sidRep[k].on = false; if (so.enabled && !legato && se.rep != 0) { EnvCfg e1 = se; e1.a = p.atk; e1.d = p.dec; v.sidRep[k].arm(e1, 1.0, t); }
                s.level[k] = F32(so.level); s.levelT[k] = s.level[k]; s.pm[k] = PitchOffset{}; s.pwMod[k] = false;
                pms[npm++] = PmRef{&s.pm[k], k, 0, s.freq[k]};
                oscFreqByIndex[k] = fb; freqSet[k] = true;
                v.g[k].active = true;
            }
            continue;
        }
        if (L.type == LayerType::FM && L.sixOp) { if (idx != 0 || !L.sixOpEnabled || !L.hasDx7Patch) continue; }
        else if (L.type == LayerType::FM && !L.engineEnabled[idx / 4]) continue;
        const double freqBase = midi(layer, note, o);
        oscFreqByIndex[idx] = freqBase; freqSet[idx] = true;
        if (o.noiseOn) {
            if (o.level <= 0) continue;
            if (v.nNoise < 8) { NoiseSrc& n = v.noise[v.nNoise++]; n.on = true; n.group = groupIdxFor(idx); n.gain = F32(o.level); n.type = noiseTypeIndex(o.noiseType); n.pos = 0; v.g[n.group].active = true; }
            continue;
        }
        if (L.type == LayerType::Sample) {
            double rrCents = 0, rrGain = 1; const int rrTake = (o.rr.on && !pl.hasSamplePosition) ? rrPick(L, idx, note, rrCents, rrGain) : idx;   // Round Robin: this hit's take
            const OscCfg& oS = L.oscs[(size_t)rrTake];   // the take supplies the sample, its start / end markers, loop points and root; the playing slot supplies everything else
            auto sit = samples_.find(oS.name);
            if (sit == samples_.end() || !sit->second || o.level <= 0) continue;
            const SampleBuf& B = *sit->second;
            SmpSlot& s = v.smp[idx]; s.on = true; s.group = idx; v.g[idx].active = true;
            s.bleedOn = o.bleedOn; s.bleedM.snap(clampd(o.bleed, 0, 1)); { const double mi[2] = {clampd(o.micMix, 0, 1), clampd(o.spkMix, 0, 1)}; const bool want[2] = {mi[0] > 0 || genRouteFor(L_SMP, GF_MICMIX), mi[1] > 0 || genRouteFor(L_SMP, GF_SPKMIX)};
                ColFx* pp[2] = {nullptr, nullptr}; for (int k = 0; k < 2; k++) if (want[k] && (k == 0 ? o.mic : o.spk) != "none") { s.fxIdx[k] = fxAcquire(); if (s.fxIdx[k] >= 0) pp[k] = &fxPool_[(size_t)s.fxIdx[k]]; }
                s.col.setup(o.mic, o.spk, sr_, mi, want, pp); }
            const double bsr = (double)B.sr, dur = (double)B.len / bsr;
            const double startFrac = pl.hasSamplePosition ? pl.samplePosition : oS.start, endFrac = pl.hasSampleEnd ? pl.sampleEndPosition : oS.end;
            double startSec = startFrac * dur, endSec = endFrac * dur;
            const bool wantReverse = pl.hasSampleReverse ? pl.sampleReverse : o.reverse;
            SamplePlayer::Mode mode = SamplePlayer::kPlain; long sIdx = 0, segLen = 1;
            if (o.pingPong) {
                sIdx = (long)std::floor(startSec * bsr); const long eIdx = (long)std::floor(endSec * bsr); segLen = std::max(1L, eIdx - sIdx);
                mode = SamplePlayer::kPingPong; startSec = 0; endSec = (double)(segLen * 2) / bsr;
            } else if (wantReverse && pl.hasSamplePosition) {
                sIdx = (long)std::floor(startSec * bsr); const long eIdx = (long)std::floor(endSec * bsr); segLen = std::max(1L, eIdx - sIdx);
                mode = SamplePlayer::kReverseSlice; startSec = 0; endSec = (double)segLen / bsr;
            }
            const double noteNumber = 69 + 12 * std::log2(freqBase / 440.0);
            s.baseRate = F32(std::pow(2.0, (noteNumber - oS.root) / 12.0) * std::pow(2.0, (double)o.oct) * std::pow(2.0, (o.semitone * 100 + o.fineTune + rrCents) / 1200.0));
            s.pm = PitchOffset{}; pms[npm++] = PmRef{&s.pm, idx, 2, s.baseRate};
            const bool useGlobal = L.globalAdsrActive; const bool hasEnv = useGlobal || o.hasEnv; const EnvCfg se = useGlobal ? L.globalAdsr : o.env;
            s.env.clear(); s.env.setIntrinsic(1);
            if (hasEnv) {
                if (legato) s.env.setValueAtTime(se.s, t);
                else { s.env.setValueAtTime(0, t); s.env.linearRampToValueAtTime(1, t + se.a, t); s.env.linearRampToValueAtTime(se.s, t + se.a + se.d, t); }
            } else s.env.setValueAtTime(1, t);
            s.rep.on = false;
            if (hasEnv && !legato && se.rep != 0) { EnvCfg e1 = se; s.rep.arm(e1, 1.0, t); }
            s.relTime = hasEnv ? se.r : 0.05;
            s.mix.set(F32(sampleMixGainFor(idx, sampleMixPos)));
            const bool looping = o.pingPong || (o.loop && !wantReverse);
            double ls = startSec, le = endSec;
            if (looping && !o.pingPong && oS.loopStartFrac >= 0 && oS.loopEndFrac >= 0 && mode == SamplePlayer::kPlain && !pl.hasSamplePosition) {
                ls = clampd(oS.loopStartFrac * dur, startSec, endSec - 0.002); le = clampd(oS.loopEndFrac * dur, ls + 0.002, endSec);
            }
            s.pl.start(&B, mode, sIdx, segLen, sr_, looping, ls, le, true, startSec, looping ? -1.0 : std::max(0.001, endSec - startSec), s.baseRate);
            const double played = std::max(0.01, (endSec - startSec) / s.baseRate);
            const double fadeIn = std::min(o.fadeIn, played / 2), fadeOut = looping ? 0.0 : std::min(o.fadeOut, played / 2), lvl = F32(o.level * rrGain);
            s.fade.clear(); s.fade.setIntrinsic(1); s.fade.setValueAtTime(0, t);
            if (fadeIn > 0) s.fade.linearRampToValueAtTime(lvl, t + fadeIn, t); else s.fade.setValueAtTime(lvl, t);
            if (fadeOut > 0) { s.fade.setValueAtTime(lvl, t + std::max(fadeIn, played - fadeOut)); s.fade.linearRampToValueAtTime(0, t + played, t); }
            continue;
        }
        if (o.level <= 0 && !(L.type == LayerType::Wavetable && L.syncEnabled && idx == 0)) continue;

        if (L.type == LayerType::Lua) {   // scripted oscillator slot: enabled flag, velocity window, then one voice of the slot's compiled script
            if (!o.enabled || o.level <= 0 || idx >= LuaHost::kSlots) continue;
            const int vi = (int)std::lround(v.noteVel * 127); if (vi < o.velLow || vi > o.velHigh) continue;
            auto gd = lua_.acquire(idx); if (!gd.e) continue;
            if (o.luaMono) for (auto& ov : voices_) if (&ov != &v && ov.alive && ov.layer == v.layer) { LuaSrc& os = ov.lua[idx]; if (os.on && os.gen == gd.e->gen) { gd.e->e.voiceKill(os.slot); os.on = false; } }
            const uint32_t seed = o.luaSeedLock ? (uint32_t)(((uint64_t)o.luaSeed * 2654435761ull) ^ ((uint64_t)note * 40503ull) ^ ((uint64_t)idx * 7919ull)) : (uint32_t)(rng_.next() * 4294967295.0);
            const int sl = gd.e->e.voiceOn(note, (float)std::max(0.01, v.noteVel), seed); if (sl < 0) continue;
            LuaSrc& ls = v.lua[idx]; ls = LuaSrc{}; ls.on = true; ls.slot = sl; ls.gen = gd.e->gen;
            ls.freq = freqBase * std::pow(2.0, o.oct) * std::pow(2.0, (o.semitone * 100 + o.fineTune) / 1200.0);
            { const double s_ = o.velSens, c_ = o.velCurve; double x = clampd(v.noteVel, 0, 1); if (c_ > 0) x = std::pow(x, 1 + c_); else if (c_ < 0) x = std::pow(x, 1 / (1 - c_)); ls.level = o.level * ((1 - s_) + s_ * x); }
            ls.pan = o.pan; ls.width = o.luaWidth; ls.pol = o.luaPolarity; ls.pc = PitchOffset{}; pms[npm++] = PmRef{&ls.pc, idx, 1, 0};
            v.g[0].active = true; continue;
        }
        if (L.type == LayerType::Procedural) {
            ProGen& pg = v.pro[idx]; pg.on = true; pg.idx = idx;
            pg.proc.start((uint32_t)((uint64_t)(rng_.next() * 4294967295.0) & 0xFFFFFFFFu));
            static const char* names[5] = {"pluck", "modal", "breath", "chaos", "bow"};
            int model = 0; for (int m = 0; m < 5; m++) if (o.model == names[m]) model = m;
            // key tracking: Tone and Damp follow how far the note sits from middle C (48 semitones = +-4 octaves)
            const double ktSemis = note - 60, ktBright = clampd(o.brightKeyTrack / 100 * ktSemis / 48, -0.4, 0.4), ktDamp = clampd(o.dampKeyTrack / 100 * ktSemis / 48, -0.4, 0.4);
            pg.pr.model = model; pg.pr.bright = F32(clampd(o.bright + ktBright, 0, 1)); pg.pr.damp = F32(clampd(o.damp + ktDamp, 0, 1)); pg.pr.body = F32(o.body); pg.pr.chaos = F32(o.chaos);
            pg.pr.stiffness = F32(o.stiffness); pg.pr.excite = F32(o.excite); pg.pr.bore = F32(o.bore);
            pg.ringN = 0; pg.rw = 0; for (int k = 0; k < kQuantum; k++) pg.xin[k] = 0; std::fill(pg.ring, pg.ring + 1024, 0.f);
            pg.isFlute = o.model == "flute";
            if (pg.isFlute) {   // breath-driven waveguide flute: configure the voice's core from the preset, then start the breath
                pg.fl.setInstrument(o.fluteInst); for (int k = 0; k < flute::kNumParams; k++) pg.fl.setParam(k, o.flute[k]);
                pg.fl.snapParams(); pg.fl.reset(); pg.fl.setSeed((uint32_t)((uint64_t)(rng_.next() * 4294967295.0) & 0xFFFFFFFFu)); pg.fl.noteOn(v.noteVel);
            }
            pg.freq = F32(freqBase * std::pow(2.0, o.oct) * std::pow(2.0, (o.semitone * 100 + o.fineTune) / 1200.0));
            pg.bOct = o.oct; pg.bSemi = o.semitone; pg.bFine = o.fineTune;
            pg.level = F32(o.level); double gl, gr; monoPanGains(o.pan, gl, gr); pg.mono = 0.5 * (gl + gr); pg.lvTgt = pg.level; pg.monoTgt = pg.mono; pg.mixCur = mixTgt_[21 + (idx ? 1 : 0)];
            pg.pm = PitchOffset{}; pms[npm++] = PmRef{&pg.pm, idx, 0, pg.freq};
            v.peg[4 + (idx ? 1 : 0)].build(o.peg, t, legato);
            v.g[0].active = true;
            continue;
        }
        if (L.type == LayerType::Wavetable && L.ringModEnabled) {
            if (idx == 1) continue;   // OSC2 only exists as the modulator
            const OscCfg& o0 = L.oscs[0]; RingEng& r = v.ring;
            static const char* names[4] = {"sine", "triangle", "sawtooth", "square"}; r.wave = 0; for (int k = 0; k < 4; k++) if (L.rmWave == names[k]) r.wave = k;
            double ratio = L.rmInterval == "octave" ? 2.0 : L.rmInterval == "m3" ? std::pow(2.0, 3.0 / 12) : L.rmInterval == "tritone" ? std::pow(2.0, 6.0 / 12) : L.rmInterval == "micro" ? 1.0 : 1.5;
            ratio *= std::pow(2.0, L.rmDetuneCents / 1200.0);
            r.fc = freqBase * std::pow(2.0, o0.oct) * std::pow(2.0, (o0.semitone * 100 + o0.fineTune) / 1200.0); r.fm = r.fc * ratio;
            r.phC = r.phM = 0; r.t0 = t; r.envCents = L.rmPitchEnvAmt * 100; r.envTc = std::max(0.005, L.rmPitchEnvDecay);
            r.level = F32(o0.level); r.bleed = clampd(L.rmBleed, 0, 1); r.velDepth = clampd(L.rmVelDepth, 0, 1);
            const double keytrack = r.fc * std::pow(2.0, L.rmLpgKeytrack * 3); const double cut = std::max(80.0, L.rmLpgCutoff * 0.5 + keytrack * 0.5);
            const double w0 = 2 * 3.141592653589793 * std::min(cut, sr_ * 0.5 - 1) / sr_, cs = std::cos(w0), al = std::sin(w0) / (2 * std::pow(10.0, 0.7 / 20));   // WebAudio lowpass: Q is in dB
            r.lpg.reset(); r.lpg.norm((1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + al, -2 * cs, 1 - al);
            r.on = true; v.g[0].active = true; continue;
        }
        if (L.type == LayerType::Wavetable) {
            if (L.syncEnabled && idx == 1) continue;
            if (L.syncEnabled && idx == 0) {
                const OscCfg& o0 = L.oscs[0]; const OscCfg& o1 = L.oscs[1];
                WtOsc& a = v.wt[0]; WtOsc& b = v.wt[1];
                a.on = b.on = true; a.sync = b.sync = true; a.idx = 0; b.idx = 1; a.group = 0; b.group = 1; a.n = b.n = 1;
                v.peg[0].build(o0.peg, t, legato); v.peg[1].build(o1.peg, t, legato);
                a.table = table(o0.table); b.table = table(o1.table); v.sync.reset(); v.sync.setTables(a.table, b.table);
                a.freq[0] = F32(freqBase * std::pow(2.0, o0.oct) * std::pow(2.0, (o0.semitone * 100 + o0.fineTune) / 1200.0)); b.freq[0] = F32(freqBase * std::pow(2.0, o1.oct) * std::pow(2.0, (o1.semitone * 100 + o1.fineTune + o1.detune) / 1200.0));   // Round 22: Semitone / Fine tune apply in hard sync too (slave pitch vs master makes the sweep)
                a.pos = F32(pl.hasPosition ? pl.position : o0.position); b.pos = F32(o1.position);
                a.pwm = F32(o0.pwm); b.pwm = F32(o1.pwm); a.pwmPh = b.pwmPh = 0; a.phase = F32(o0.phase); b.phase = F32(o1.phase); a.startEnd = F32(o0.startEnd); b.startEnd = F32(o1.startEnd);
                a.segment = F32(o0.segment); b.segment = F32(o1.segment); a.pingPong = o0.pingPong; b.pingPong = o1.pingPong;
                a.level = F32(o0.level); b.level = F32(o1.level);
                double gl, gr; monoPanGains(o0.pan, gl, gr); a.panL[0] = gl; a.panR[0] = gr; monoPanGains(o1.pan, gl, gr); b.panL[0] = gl; b.panR[0] = gr; a.monoPan[0] = b.monoPan[0] = 1;
                a.oscGain = b.oscGain = 1;
                a.pm[0] = PitchOffset{}; b.pm[0] = PitchOffset{};
                pms[npm++] = PmRef{&a.pm[0], 0, 0, a.freq[0]}; pms[npm++] = PmRef{&b.pm[0], 1, 0, b.freq[0]};
                a.sub = (o0.sub > 0 && o0.level > 0); if (a.sub) { a.subShape = subShapeOf(o0.subWave); a.subDiv = std::pow(2.0, o0.sub); a.subLevel = o0.level * 0.7; a.subFreq = a.freq[0]; a.subOsc = SineOsc{}; }
                b.sub = (o1.sub > 0 && o1.level > 0); if (b.sub) { b.subShape = subShapeOf(o1.subWave); b.subDiv = std::pow(2.0, o1.sub); b.subLevel = o1.level * 0.7; b.subFreq = b.freq[0]; b.subOsc = SineOsc{}; }
                v.wtSyncPair = &a; v.g[0].active = v.g[1].active = true;
                continue;
            }
            WtOsc& w = v.wt[idx]; w.on = true; w.sync = false; w.idx = idx; w.group = idx;
            const int n = std::max(1, std::min(kMaxUnison, o.unison)); w.n = n; w.uLive = false;
            w.table = table(o.table); w.level = F32(o.level);
            // Serum-style Stack & Spread: Blend tapers each voice by its distance from the stack centre, Phase spreads the start phases; oscGain = level / sqrt(sum w^2)
            const double blendAmt = clampd(o.unisonBlend, 0, 1), phaseSpread = clampd(o.unisonPhase, 0, 1), centreIdx = (n - 1) / 2.0;
            double sumWeightSq = 0; for (int u = 0; u < n; u++) { const double d = n == 1 ? 0 : std::fabs(u - centreIdx) / centreIdx; w.weights[u] = std::max(0.0, 1 - blendAmt * d); sumWeightSq += w.weights[u] * w.weights[u]; }
            if (sumWeightSq == 0) sumWeightSq = 1;
            w.oscGain = F32(o.level / std::sqrt(sumWeightSq));
            w.pos = F32((idx == 0 && pl.hasPosition) ? pl.position : o.position);
            w.pwm = F32(o.pwm); w.pwmPh = 0; w.phase = F32(o.phase); w.startEnd = F32(o.startEnd); w.segment = F32(o.segment); w.pingPong = o.pingPong;
            w.bOct = o.oct; w.bSemi = o.semitone; w.bFine = o.fineTune; w.bDetune = o.detune;
            v.peg[idx & 1].build(o.peg, t, legato);
            for (int u = 0; u < n; u++) {
                const double cents = n == 1 ? 0 : (-o.detune / 2) + (o.detune * u / (n - 1));
                w.cents[u] = cents;
                w.freq[u] = F32(freqBase * std::pow(2.0, o.oct) * std::pow(2.0, (o.semitone * 100 + o.fineTune + cents) / 1200.0));
                w.w[u].reset(); w.w[u].setTable(w.table);
                // every voice has its own StereoPanner in the web; with the group filter ON that stack is down-mixed 0.5*(L+R) into the (one-channel)
                // filter, with the filter bypassed it stays stereo through the group pan
                double gl, gr; monoPanGains(n > 1 ? clampd((-1 + 2.0 * u / (n - 1)) * o.width, -1, 1) : 0.0, gl, gr);
                w.monoPan[u] = w.weights[u]; w.panL[u] = gl; w.panR[u] = gr;
                w.phaseV[u] = n == 1 ? 0 : (-phaseSpread / 2) + (phaseSpread * u / (n - 1));
                w.pm[u] = PitchOffset{};
                if (npm < 64) pms[npm++] = PmRef{&w.pm[u], idx, 0, w.freq[u]};
            }
            w.sub = o.sub > 0;
            if (w.sub) { w.subShape = subShapeOf(o.subWave); w.subDiv = std::pow(2.0, o.sub); w.subLevel = F32(o.level * 0.7); w.subFreq = freqBase * std::pow(2.0, o.oct) * std::pow(2.0, (o.semitone * 100 + o.fineTune) / 1200.0); w.subOsc = SineOsc{}; }
            v.g[idx].active = true;
            continue;
        }
        if (L.type == LayerType::FM && L.sixOp) {
            DxEng& d = v.dx; d.on = true; d.started = false; d.v.setSampleRate(sr_);
            d.note = std::max(0, std::min(127, (int)jsRound(note))); d.freq = freqBase * std::pow(2.0, L.dx7Transpose); d.trim = L.dx7OutputTrim;
            d.pc = PitchOffset{}; pms[npm++] = PmRef{&d.pc, idx, 1, 0};
            v.g[0].active = true; continue;
        }
        if (L.type == LayerType::FM) {
            if (idx != 0 && idx != 4) continue;
            const int base = idx, ei = idx == 0 ? 0 : 1;
            FmEng& f = v.fm[ei]; f.on = true; f.base = base; f.group = ei; f.fm.reset();
            const double tuned = freqBase * std::pow(2.0, L.engineOctave[ei]) * std::pow(2.0, (L.engineSemitone[ei] * 100 + L.engineFine[ei]) / 1200.0);
            const double glide = L.engineGlide[ei]; const double prev = engineLast_[ei];
            f.baseFreq.clear(); f.baseFreq.setIntrinsic(440);
            if (glide > 0 && prev >= 0) { f.baseFreq.setValueAtTime(F32(prev), t); f.baseFreq.linearRampToValueAtTime(F32(tuned), t + glide, t); }
            else f.baseFreq.setValueAtTime(F32(tuned), t);
            v.peg[2 + ei].build(o.peg, t, legato);
            engineLast_[ei] = tuned; f.tunedFreq = tuned; f.freqBase = freqBase; f.modded = false;
            f.fb0 = F32(clampd(L.oscs[base].feedback, 0, 1));
            f.algo = L.fmAlgorithms[ei];
            for (int i = 0; i < 4; i++) { const OscCfg& op = L.oscs[base + i]; f.ratio[i] = F32(clampd(op.ratio, 0.01, 64)); f.level[i] = F32(clampd(op.level, 0, 1)); f.wave[i] = op.wave; f.pc[i] = PitchOffset{}; pms[npm++] = PmRef{&f.pc[i], base + i, 1, 0}; }
            f.sub = false;
            const int subN = L.fmSub[ei];
            if (subN > 0) {
                double maxLvl = 0; for (int i = 0; i < 4; i++) maxLvl = std::max(maxLvl, L.oscs[base + i].level);
                f.sub = true; f.subDiv = std::pow(2.0, subN); f.subLevel = 0.5 * maxLvl; f.subFreq = tuned; f.subOsc = SineOsc{};
                f.subGlide = (glide > 0 && prev >= 0); f.subGlideFrom = prev; f.subGlideT0 = t; f.subGlideDur = glide; f.subTo = tuned;
            }
            v.g[ei].active = true;
            continue;
        }
    }
    // Glide: ramp each oscillator's pitch offset from its previous note down to 0.
    for (int i = 0; i < npm; i++) {
        const PmRef& r = pms[i]; const OscCfg& o = L.oscs[r.oscIndex];
        r.p->kind = r.kind; r.p->osc = r.oscIndex; r.p->drift = o.drift; r.p->built = r.built;
        if (!freqSet[r.oscIndex] || !(o.glideTime > 0) || lp(r.oscIndex) < 0) continue;
        const double from = lp(r.oscIndex), to = oscFreqByIndex[r.oscIndex];
        double start = 0;
        if (r.kind == 0) start = from - to; else if (r.kind == 1) start = 1200.0 * std::log2(from / to); else start = from - to;
        r.p->gStart = start; r.p->gT0 = t; r.p->gDur = o.glideTime;
    }
    for (int i = 0; i < 16 && i < (int)L.oscs.size(); i++) if (freqSet[i]) lp(i) = oscFreqByIndex[i];
    // Cross-mod FM: another layer's (3 ms delayed) pre-bus audio drives every pitch input of this voice at audio rate. Depth uses the slot's base amount.
    for (int xi = 0; xi < 4; xi++) {
        const XmodCfg& sx = preset_->xmod[xi]; if (!sx.on || sx.type != "FM") continue;
        const int sl = layerIndex(sx.src), dl = layerIndex(sx.dst); if (dl != layer || sl < 0 || sl == dl) continue;
        for (int i = 0; i < npm; i++) {
            PitchOffset& po = *pms[i].p; if (po.nx >= 4) continue; const double a = sx.amount;
            po.xr[po.nx] = &tap_[sl]; po.xd[po.nx] = pms[i].kind == 0 ? a * a * 1500 : pms[i].kind == 1 ? a * a * 2400 : a * a * 0.5; po.nx++;
        }
    }

    // Filter groups
    for (int gi = 0; gi < numGroups; gi++) {
        FilterCfg gf; FilterEnvCfg fe; double panValue = 0; bool applyPan = false;
        if (L.type == LayerType::Wavetable) { const OscCfg& o = L.oscs[gi]; gf = o.hasFilter ? o.filter : FilterCfg{}; fe = o.filterEnv; panValue = o.pan; applyPan = true; }
        else if (L.type == LayerType::FM) { const OscCfg& o = L.oscs[gi * 4]; gf = o.hasFilter ? o.filter : FilterCfg{}; fe = o.filterEnv; panValue = L.sixOp ? L.dx7Pan : o.pan; applyPan = true; }
        else if (L.type == LayerType::Sample) { const OscCfg& o = L.oscs[gi]; gf = o.filter; fe = o.filterEnv; panValue = pl.hasSamplePan ? pl.samplePan : o.pan; applyPan = true; }
        else if (L.type == LayerType::SID) { gf = L.filter; fe = L.filterEnv; panValue = clampd((gi < (int)L.oscs.size() ? L.oscs[gi].pan : 0) + L.pan, -1, 1); applyPan = true; }
        else { gf = L.filter; fe = L.filterEnv; }
        if (L.type == LayerType::FM && !L.sixOp) panValue += L.enginePan[gi < 2 ? gi : 1];
        setupGroup(v, gi, L, gf, fe, L.envForGroup(gi), note, pl, legato, t, panValue, applyPan, gi == 0, L.type == LayerType::Sample || (L.type == LayerType::FM && L.sixOp));
        { Group& gg = v.g[gi];
          gg.mixKey = L.type == LayerType::Wavetable ? gi : L.type == LayerType::FM ? (L.sixOp ? 4 : (gi == 0 ? 2 : 3)) : L.type == LayerType::Sample ? 5 + gi : L.type == LayerType::SID ? 23 : -1;
          if (gg.mixKey >= kNumMix || gg.mixKey < -1) gg.mixKey = -1;
          gg.mixCur = gg.mixKey >= 0 ? mixTgt_[gg.mixKey] : 1; gg.panLive = applyPan; gg.panCur = gg.panStart = gg.panTgt = clampd(panValue, -1, 1); gg.panRef = groupBasePan(effL(v.layer), gi); }
        if (v.g[gi].combSlot >= 0) { combFree_.push_back(v.g[gi].combSlot); v.g[gi].filt.detachComb(); v.g[gi].combSlot = -1; }
        if (v.g[gi].ftype == 5 && v.g[gi].active && !combFree_.empty()) {   // comb filter needs its delay line (shared pool)
            const int s = combFree_.back(); combFree_.pop_back(); v.g[gi].combSlot = s; v.g[gi].filt.attachComb(combPool_[s].data(), 8192);
        }
    }
    // record the amp-envelope shape of the voice for stop()
}

inline void Synth::stopVoice(Voice& v, double relTime, double t) {
    const LayerCfg& L = vL(v);
    for (auto& g : v.g) g.rep.on = false;            // envelope repeat ends at note-off; the release starts from wherever the cycle is
    for (auto& q : v.smp) q.rep.on = false;
    for (auto& r : v.sidRep) r.on = false;
    double maxRel = relTime, maxLpg = 0, maxSample = 0;
    if (v.dx.on) v.dx.v.noteOff();
    if (v.sid.on) for (int k = 0; k < 3; k++) v.sid.p[k].gate = false;
    if (L.type == LayerType::Sample)
        for (auto& q : v.smp) if (q.on) {
            const double cur = q.env.valueAt(t); q.env.cancelScheduledValues(t); q.env.setValueAtTime(cur, t); q.env.linearRampToValueAtTime(0, t + q.relTime, t);
            maxSample = std::max(maxSample, q.relTime);
        }
    for (int gi = 0; gi < v.ng; gi++) {
        Group& g = v.g[gi];
        if (L.type != LayerType::Sample) {
            const double r = (L.type == LayerType::FM && L.sixOp) ? v.dx.releaseSec : L.envForGroup(gi).r;
            const double cur = g.amp.valueAt(t);
            g.amp.cancelScheduledValues(t); g.amp.setValueAtTime(cur, t); g.amp.linearRampToValueAtTime(0, t + r, t);
            maxRel = std::max(maxRel, r);
        }
        if (g.lpg) maxLpg = std::max(maxLpg, g.lpgDecay * 6);
        else {
            const double cur = g.cutoff.valueAt(t);
            g.cutoff.cancelScheduledValues(t); g.cutoff.setValueAtTime(cur, t); g.cutoff.linearRampToValueAtTime(g.ckt, t + g.fe.r, t);
        }
    }
    for (auto& p : v.pro) if (p.on && p.isFlute) p.fl.noteOff();   // breath release starts at note-off (the layer's amp release sets how long it can ring)
    for (int i = 0; i < LuaHost::kSlots; i++) if (v.lua[i].on) { auto gd = lua_.acquire(i); if (gd.e && gd.e->gen == v.lua[i].gen) gd.e->e.voiceOff(v.lua[i].slot); }
    v.released = true; v.stopAt = t + std::max(maxRel, std::max(maxLpg, maxSample)) + 0.05;
}

inline void Synth::process(float* outL, float* outR, int n) {
    const PowerCfg pw = PowerCfg::unpack(power_.load(std::memory_order_relaxed)); ScopedFlushDenormals ftz(pw.profile > 0 && (pw.flags & PW_DSP));   // DSP optimisation: no denormal stalls in decaying states
    int done = 0;
    while (done < n) {
        if (outPos_ >= kQuantum) { applyEvents(clock_); renderQuantum(outBuf_[0], outBuf_[1]); outPos_ = 0; clock_ += kQuantum; }
        const int c = std::min(n - done, kQuantum - outPos_);
        std::memcpy(outL + done, outBuf_[0] + outPos_, c * sizeof(float));
        std::memcpy(outR + done, outBuf_[1] + outPos_, c * sizeof(float));
        done += c; outPos_ += c;
    }
}

inline void Synth::killLuaVoices(Voice& v) {
    for (int i = 0; i < LuaHost::kSlots; i++) { LuaSrc& ls = v.lua[i]; if (!ls.on) continue; auto gd = lua_.acquire(i); if (gd.e && gd.e->gen == ls.gen) gd.e->e.voiceKill(ls.slot); ls.on = false; }
}
inline void Synth::loadLuaFromPreset(const Preset& p) {
    const LayerCfg& L = p.layers[L_LUA];
    for (int i = 0; i < LuaHost::kSlots && i < (int)L.oscs.size(); i++) {
        const OscCfg& o = L.oscs[i]; if (o.luaSrc.empty()) { lua_.clear(i); continue; }
        lua_.load(i, o.luaSrc, o.luaSeed, o.luaParams);
    }
}

inline void Synth::renderVoice(Voice& v, double t0, float* busL, float* busR) {
    const LayerCfg& L = v.xcfg ? *v.xcfg : preset_->layers[v.layer];
    static thread_local double gin[kMaxGroups][kQuantum], ginR[kMaxGroups][kQuantum];
    bool gstereo[kMaxGroups] = {};   // true when a group received a genuinely stereo signal (WT unison stacks with the filter bypassed)
    for (int gi = 0; gi < v.ng; gi++) if (v.g[gi].active) { std::fill(gin[gi], gin[gi] + kQuantum, 0.0); std::fill(ginR[gi], ginR[gi] + kQuantum, 0.0); }
    const double dt = 1.0 / sr_; v.outPk = 1.f; float outPkAcc = 0.f;
    // sources stop sample-accurately at stopAt (AudioScheduledSourceNode.stop); matters for sources with no amp envelope (sampler slots, noise)
    int cut = kQuantum; if (v.released) cut = (int)clampd(std::ceil((v.stopAt - t0) * sr_ - 1e-6), 0, kQuantum);
    // ---- wavetable
    if (L.type == LayerType::Wavetable && v.ring.on) {
        RingEng& r = v.ring;
        auto osc = [&](double& ph, double f) {   // naive sine / triangle, PolyBLEP saw / square
            const double dtp = std::min(0.5, std::abs(f) / sr_); double o;
            auto blep = [&](double x) { if (x < dtp) { x /= dtp; return x + x - x * x - 1.0; } if (x > 1 - dtp) { x = (x - 1) / dtp; return x * x + x + x + 1.0; } return 0.0; };
            if (r.wave == 1) o = ph < 0.5 ? 4 * ph - 1 : 3 - 4 * ph;
            else if (r.wave == 2) o = 2 * ph - 1 - blep(ph);
            else if (r.wave == 3) { o = ph < 0.5 ? 1.0 : -1.0; o += blep(ph); o -= blep(std::fmod(ph + 0.5, 1.0)); }
            else o = std::sin(2 * 3.141592653589793 * ph);
            ph += f / sr_; ph -= std::floor(ph); return o; };
        const double depth = (1 - r.velDepth) + r.velDepth * clampd(v.velocity, 0, 1);
        for (int i = 0; i < cut; i++) {
            const double t = t0 + i * dt;
            const double fmod = clampd(r.fm * std::pow(2.0, (r.envCents * std::exp(-(t - r.t0) / r.envTc)) / 1200.0), 0, sr_ * 0.5);
            const double c = osc(r.phC, clampd(r.fc, 0, sr_ * 0.5)), m = osc(r.phM, fmod);
            gin[0][i] += r.lpg.process((c * m * depth + c * r.bleed) * r.level);
        }
    } else if (L.type == LayerType::Wavetable) {
        // PWM speed (Hz dial): a sine sweeps the pulse width +-0.4 around the (modulated) PWM value, per sample, no allocation.
        auto pwSweep = [&](WtOsc& w) -> double {
            const double hz = wtLive_[w.idx & 1].hz; if (hz <= 0) return w.pwm;
            w.pwmPh += hz / sr_; if (w.pwmPh >= 1) w.pwmPh -= 1;
            return clampd(w.pwm + 0.4 * std::sin(6.283185307179586 * w.pwmPh), 0.02, 0.98);
        };
        if (v.wtSyncPair) {
            WtOsc& a = *v.wtSyncPair; WtOsc& b = v.wt[1];
            for (int i = 0; i < kQuantum; i++) {
                const double t = t0 + i * dt;
                if (a.modded) { a.pos = a.ms[0].at(t); a.pwm = a.ms[1].at(t); a.phase = a.ms[2].at(t); a.startEnd = a.ms[3].at(t); a.segment = a.ms[4].at(t); }   // a-rate params
                if (b.modded) { b.pos = b.ms[0].at(t); b.pwm = b.ms[1].at(t); b.phase = b.ms[2].at(t); b.startEnd = b.ms[3].at(t); b.segment = b.ms[4].at(t); }
                WavetableSync::Osc oa, ob;
                oa.freq = clampd((a.freq[0] + a.pm[0].at(t)) * v.peg[0].mul(t), 0.01, 20000); ob.freq = clampd((b.freq[0] + b.pm[0].at(t)) * v.peg[1].mul(t), 0.01, 20000);
                oa.pos = a.pos; oa.pw = pwSweep(a); oa.phOff = a.phase; oa.se = a.startEnd; oa.seg = a.segment; oa.pingPong = a.pingPong;
                ob.pos = b.pos; ob.pw = pwSweep(b); ob.phOff = b.phase; ob.se = b.startEnd; ob.seg = b.segment; ob.pingPong = b.pingPong;
                double o0, o1; v.sync.tick(oa, ob, o0, o1);
                const double ya = o0 * a.level, yb = o1 * b.level;   // each side has its own StereoPanner (stereo when its filter is bypassed, down-mixed 0.5*(L+R) otherwise)
                if (v.g[0].bypass) { gin[0][i] += ya * a.panL[0]; ginR[0][i] += ya * a.panR[0]; gstereo[0] = true; } else gin[0][i] += ya * 0.5 * (a.panL[0] + a.panR[0]);
                if (v.g[1].bypass) { gin[1][i] += yb * b.panL[0]; ginR[1][i] += yb * b.panR[0]; gstereo[1] = true; } else gin[1][i] += yb * 0.5 * (b.panL[0] + b.panR[0]);
                if (a.sub) { const double sv = subShapedTick(a.subOsc, a.subShape, (a.subFreq + a.pm[0].at(t)) / a.subDiv, sr_) * a.subLevel; gin[0][i] += sv; if (v.g[0].bypass) ginR[0][i] += sv; }
                if (b.sub) { const double sv = subShapedTick(b.subOsc, b.subShape, (b.subFreq + b.pm[0].at(t)) / b.subDiv, sr_) * b.subLevel; gin[1][i] += sv; if (v.g[1].bypass) ginR[1][i] += sv; }
            }
        } else {
            for (int oi = 0; oi < 2; oi++) {
                WtOsc& w = v.wt[oi]; if (!w.on) continue;
                if (w.uLive) {   // held unison stack follows the oscillator's current Width / Blend / Phase (web: setTargetAtTime tau 20 ms voice gain + pan, 10 ms phase)
                    const double k20 = 1.0 - std::exp(-(double)kQuantum / (0.02 * sr_)), k10 = 1.0 - std::exp(-(double)kQuantum / (0.01 * sr_));
                    for (int u = 0; u < w.n; u++) { w.monoPan[u] += (w.tW[u] - w.monoPan[u]) * k20; w.panL[u] += (w.tPL[u] - w.panL[u]) * k20; w.panR[u] += (w.tPR[u] - w.panR[u]) * k20; w.phaseV[u] += (w.tPh[u] - w.phaseV[u]) * k10; }
                }
                for (int i = 0; i < kQuantum; i++) {
                    const double t = t0 + i * dt; double acc = 0;
                    if (w.modded) { w.pos = w.ms[0].at(t); w.pwm = w.ms[1].at(t); w.phase = w.ms[2].at(t); w.startEnd = w.ms[3].at(t); w.segment = w.ms[4].at(t); }   // a-rate params
                    const double pwNow = pwSweep(w);
                    double accR = 0;
                    const double pegM = v.peg[oi].mul(t);   // R11 pitch envelope, once per sample for the whole unison stack
                    for (int u = 0; u < w.n; u++) {
                        const double f = clampd((w.freq[u] + w.pm[u].at(t)) * pegM, 0.01, 20000);
                        const double ph = w.n == 1 ? w.phase : std::fmod(std::fmod(w.phase + w.phaseV[u], 1.0) + 1.0, 1.0);
                        const double sv = w.w[u].tick(f, w.pos, pwNow, ph, w.startEnd, w.segment, w.pingPong) * w.monoPan[u];
                        acc += sv * w.panL[u]; accR += sv * w.panR[u];
                    }
                    const bool byp = v.g[w.group].bypass;
                    if (byp) { gin[w.group][i] += acc * w.oscGain; ginR[w.group][i] += accR * w.oscGain; gstereo[w.group] = true; }
                    else gin[w.group][i] += 0.5 * (acc + accR) * w.oscGain;
                    if (w.sub) { const double sv = subShapedTick(w.subOsc, w.subShape, (w.subFreq + w.pm[0].at(t)) / w.subDiv, sr_) * w.subLevel; gin[w.group][i] += sv; if (byp) ginR[w.group][i] += sv; }
                }
            }
        }
    } else if (L.type == LayerType::SID) {
        SidEng& s = v.sid;
        if (s.on) {
            float* outs[3] = { s.out[0], s.out[1], s.out[2] };
            { const double k20 = 1.0 - std::exp(-(double)kQuantum / (0.02 * sr_)); for (int k = 0; k < 3; k++) s.level[k] += (s.levelT[k] - s.level[k]) * k20; }   // live level dial (web: setTargetAtTime)
            for (int k = 0; k < 3 && k < (int)L.oscs.size(); k++) { const bool act = s.p[k].ins.active; s.p[k].ins = L.oscs[k].sidIns; s.p[k].ins.vibOn = L.sidPerf.vibrato; s.p[k].ins.susHold = L.sidPerf.sustain; s.p[k].ins.tie = s.tieNote; (void)act; }   // live edits reach held/latched notes every block
            for (int k = 0; k < 3; k++) { s.p[k].freq = clampd(s.freq[k] + s.pm[k].at(t0), 0.01, 20000); if (s.pwMod[k]) s.p[k].pw = clampd(s.pwM[k].at(t0), 0.01, 0.99); }
            s.chip.process(s.p, sr_, outs, kQuantum);
            for (int k = 0; k < 3; k++) { const double lv = s.level[k]; for (int i = 0; i < kQuantum; i++) gin[k][i] += (double)s.out[k][i] * lv; }
        }
    } else if (L.type == LayerType::FM && L.sixOp) {
        DxEng& d = v.dx;
        if (d.on && d.started) for (int i = 0; i < kQuantum; i++) {
            if (d.blkPos >= dx7::N) { d.v.renderBlock(d.blk, d.freq, clampd(d.pc.at(t0 + i * dt), -4800, 4800)); d.blkPos = 0; }
            gin[0][i] += (double)d.blk[d.blkPos++] * d.trim;
        }
    } else if (L.type == LayerType::FM) {
        for (int ei = 0; ei < 2; ei++) {
            FmEng& f = v.fm[ei]; if (!f.on) continue;
            if (f.modded) { for (int k = 0; k < 4; k++) f.ratio[k] = f.ratioM[k].at(t0); f.fb0 = f.fbM.at(t0); }
            for (int i = 0; i < kQuantum; i++) {
                const double t = t0 + i * dt;
                const double pegM = v.peg[2 + ei].mul(t);
                const double bf = clampd(f.baseFreq.valueAt(t) * pegM, 0.01, 20000);
                double pc[4]; for (int k = 0; k < 4; k++) pc[k] = clampd(f.pc[k].at(t), -4800, 4800);
                gin[f.group][i] += f.fm.tick(bf, f.ratio, f.level, f.wave, f.fb0, pc, f.algo);
                if (f.sub) {
                    double sf = (f.subMod ? f.subM.at(t) : f.subFreq) / f.subDiv;
                    if (f.subGlide) { const double a = f.subGlideT0, d = f.subGlideDur; const double fr = f.subGlideFrom / f.subDiv, to = f.subTo / f.subDiv; sf = t >= a + d ? to : (t <= a ? fr : fr + (to - fr) * (t - a) / d); }
                    sf *= std::pow(2.0, f.pc[0].at(t) / 1200.0);
                    sf *= pegM;
                    gin[f.group][i] += f.subOsc.tick(sf, sr_) * f.subLevel;
                }
            }
        }
    } else if (L.type == LayerType::Lua) {
        const float hb = luaHostBpm_.load(std::memory_order_relaxed); const double beat0 = luaHostBeat_.load(std::memory_order_relaxed); const float playing = luaHostPlaying_.load(std::memory_order_relaxed) ? 1.f : 0.f;
        for (int si = 0; si < LuaHost::kSlots; si++) {
            LuaSrc& ls = v.lua[si]; if (!ls.on) continue;
            auto gd = lua_.acquire(si); if (!gd.e || gd.e->gen != ls.gen) { ls.on = false; continue; }   // script replaced (or unloaded) while this note sounded: drop the voice
            lx::eng::Engine& E = gd.e->e; lx::eng::HostIn& H = E.host[ls.slot];
            H.freq = (float)ls.freq; H.bpm = hb > 1 ? hb : 120.f; H.playing = playing; H.gate = v.released ? 0.f : 1.f; H.beat = beat0 + (t0 - v.startT) * (H.bpm / 60.0); H.pitchCents = (float)ls.pc.at(t0);
            lua_.copyParams(si, H.params);
            E.lastBlockInstr = 0; E.render(ls.slot, gd.e->outL, gd.e->outR, kQuantum);
            const double pa = (clampd(ls.pan, -1, 1) + 1) * 0.7853981633974483, pgL = std::cos(pa) * 1.4142135623730951, pgR = std::sin(pa) * 1.4142135623730951, sgn = ls.pol ? -1.0 : 1.0;
            float pk = 0;
            for (int i = 0; i < kQuantum; i++) {
                double l = gd.e->outL[i], r = gd.e->outR[i]; const double m = 0.5 * (l + r), sd = 0.5 * (l - r) * ls.width; l = m + sd; r = m - sd;
                l *= ls.level * pgL * sgn; r *= ls.level * pgR * sgn;
                if (v.g[0].bypass) { gin[0][i] += l; ginR[0][i] += r; gstereo[0] = true; } else gin[0][i] += 0.5 * (l + r);
                const float a = (float)std::fabs(0.5 * (l + r)); if (a > pk) pk = a;
            }
            if (pk > v.outPk || true) { /* level metering is handled by the group stage */ }
        }
    } else if (L.type == LayerType::Procedural) {
        float fr[kQuantum], out[kQuantum];
        for (int pi = 0; pi < 2; pi++) {
            ProGen& p = v.pro[pi]; if (!p.on) continue;
            for (int i = 0; i < kQuantum; i++) fr[i] = (float)clampd((p.freq + p.pm.at(t0 + i * dt)) * v.peg[4 + pi].mul(t0 + i * dt), 0.01, 20000);
            if (p.modded) { p.pr.bright = p.gm[0].at(t0); p.pr.damp = p.gm[1].at(t0); p.pr.body = p.gm[2].at(t0); p.pr.chaos = p.gm[3].at(t0); }
            p.pr.frequency = fr[0];
            // coupled waveguide junction: the other generator's raw output, delayed ~3 ms (never less than one render quantum, as a Web Audio
            // DelayNode inside a cycle), x amount*0.8, excites this generator's own excitation point
            const float* inpP = nullptr; const double amt = clampd(L.proCoupling, 0, 1);
            ProGen& oth = v.pro[1 - pi];
            if (amt > 0.001 && v.pro[0].on && v.pro[1].on) {
                const int Dly = std::min(880, std::max(kQuantum, (int)std::lround(0.003 * sr_))), adj = pi == 1 ? kQuantum : 0, D = Dly + adj;
                for (int i = 0; i < kQuantum; i++) { const int64_t k = p.ringN + i - D; p.xin[i] = (k >= 0 && k > oth.ringN - 1024) ? (float)(oth.ring[k & 1023] * (amt * 0.8)) : 0.f; }
                inpP = p.xin;
            }
            if (p.isFlute) p.fl.render(out, kQuantum, fr, fr[0]); else p.proc.process(out, kQuantum, p.pr, fr, inpP);
            for (int i = 0; i < kQuantum; i++) p.ring[(p.ringN + i) & 1023] = out[i];
            p.ringN += kQuantum;
            const double mt = mixTgt_[21 + pi], mk = 1.0 - std::exp(-1.0 / (0.015 * sr_)); float pk = 0;
            { const double k20 = 1.0 - std::exp(-(double)kQuantum / (0.02 * sr_)), k15 = 1.0 - std::exp(-(double)kQuantum / (0.015 * sr_)); p.level += (p.lvTgt - p.level) * k20; p.mono += (p.monoTgt - p.mono) * k15; }   // live Level / Pan of the generator (held-note retarget)
            for (int i = 0; i < kQuantum; i++) { p.mixCur += (mt - p.mixCur) * mk; const double y = (double)out[i] * p.level * p.mono * p.mixCur; gin[0][i] += y; const float ay = (float)std::fabs(y); if (ay > pk) pk = ay; }
            if (pk > mixPeak_[21 + pi]) mixPeak_[21 + pi] = pk;
        }
    } else if (L.type == LayerType::Sample) {
        for (int si = 0; si < 16; si++) {
            SmpSlot& q = v.smp[si]; if (!q.on || q.pl.done()) continue;
            if (!v.released) smpHeadAcc_[si] = (float)q.pl.headFrac();
            const double rate = F32(q.baseRate + q.pm.at(t0));   // may go negative (see SamplePlayer::tick)
            float* dst = nullptr; (void)dst;
            for (int i = 0; i < cut; i++) {
                const double t = t0 + i * dt; double l, r; q.pl.tick(rate, l, r);
                const double x = q.col.tick(q.pl.channels() > 1 ? 0.5 * (l + r) : l);
                const double fe = q.fade.valueAt(t) * q.env.valueAt(t), mx = q.mix.tick();
                gin[q.group][i] += x * fe * mx;
                if (q.bleedOn) {
                    const double sendv = x * fe * mx * q.bleedM.at(t);
                    if (sendv != 0) for (int oj = 0; oj < 16; oj++) if (oj != si && v.smp[oj].on && v.smp[oj].bleedOn) gin[v.smp[oj].group][i] += sendv;
                }
                if (carOn_ && voc_.P.slots[si]) { carBus_[0][i] += (float)(l * fe * mx); carBus_[1][i] += (float)((q.pl.channels() > 1 ? r : l) * fe * mx); }
            }
        }
    }
    // ---- noise sources
    for (int ni = 0; ni < v.nNoise; ni++) {
        NoiseSrc& n = v.noise[ni]; const std::vector<float>& buf = noise_[n.type]; const uint32_t len = (uint32_t)buf.size();
        for (int i = 0; i < cut; i++) { gin[n.group][i] += buf[n.pos] * n.gain; if (++n.pos >= len) n.pos = 0; }
    }
    // ---- filter groups -> bus
    for (int gi = 0; gi < v.ng; gi++) {
        Group& g = v.g[gi]; if (!g.active) continue;
        const double mt = g.mixKey >= 0 ? mixTgt_[g.mixKey] : 1.0, mk = 1.0 - std::exp(-1.0 / (0.015 * sr_)); float pk = 0;
        if (g.panLive) {   // Mixer PAN / layer pan fields follow live on a held note (same pan fields the web re-reads every tick)
            const double pt = g.panStart + (groupBasePan(vL(v), gi) - g.panRef);   // note-on value (incl. modulation / p-locks) + later edits of the pan field
            if (!v.released) g.panTgt = clampd(pt, -1, 1);   // a released note is off the active list: its last pushed target stays
            g.panCur += (g.panTgt - g.panCur) * (1.0 - std::exp(-(double)kQuantum / (0.015 * sr_)));
            monoPanGains(g.panCur, g.panGL, g.panGR);
        }
        if (gi < 16 && (tapMask_[v.layer].load(std::memory_order_relaxed) & (1u << gi))) { float* ta = tapAcc_[v.layer][gi]; for (int i = 0; i < kQuantum; i++) ta[i] += (float)gin[gi][i]; }   // scope lane tap (pre-filter)
        for (int i = 0; i < kQuantum; i++) {
            const double t = t0 + i * dt;
            const double cutoff = clampd(g.cutoff.valueAt(t) + g.cutM.at(t), 20, 18000), reso = clampd(g.reso + g.resoM.at(t), 0, 130);
            double y = g.bypass ? (double)(float)gin[gi][i] : (double)(float)g.filt.tick(gin[gi][i], cutoff, reso, g.ftype);
            y *= (g.fvol + g.fvolM.at(t));
            g.mixCur += (mt - g.mixCur) * mk; y *= g.mixCur;
            { const float ay = (float)std::fabs(y); if (ay > pk) pk = ay; }
            const double amp = g.amp.valueAt(t) + g.levelM.at(t);
            y *= amp;
            if (gstereo[gi]) {   // stereo input: the StereoPanner's stereo law
                double yr = (double)(float)ginR[gi][i]; yr *= (g.fvol + g.fvolM.at(t)); yr *= g.mixCur; yr *= amp;
                double yl = y; const double pv = g.panned ? clampd(g.panLive ? g.panCur : g.panVal, -1, 1) : 0.0;
                if (pv <= 0) { const double x = (pv + 1) * 1.5707963267948966; yl = y + yr * std::cos(x); yr = yr * std::sin(x); }
                else { const double x = pv * 1.5707963267948966; yr = yr + y * std::sin(x); yl = y * std::cos(x); }
                busL[i] += (float)yl; busR[i] += (float)yr; { const float a = (float)std::max(std::fabs(yl), std::fabs(yr)); if (a > outPkAcc) outPkAcc = a; }
            } else { busL[i] += (float)(y * g.panGL); busR[i] += (float)(y * g.panGR); { const float a = (float)std::fabs(y); if (a > outPkAcc) outPkAcc = a; } }
        }
        if (g.mixKey >= 0 && pk > mixPeak_[g.mixKey]) mixPeak_[g.mixKey] = pk;
    }
    v.outPk = outPkAcc;
}

inline void Synth::renderQuantum(float* outL, float* outR) {
    const double t0 = now();
    { int c = 0; for (const auto& v : voices_) if (v.alive && v.xcfg) { c = 1; break; } xfxVoices_.store(c, std::memory_order_relaxed); }   // the control thread reads this before freeing retired presets
    { const uint32_t w = power_.load(std::memory_order_relaxed);   // Battery Saving Mode: adopt a changed setting at the quantum boundary
      if (w != powerApplied_) { powerApplied_ = w; policy_ = PowerPolicy::forConfig(PowerCfg::unpack(w), sr_); modSettleTaus().store(policy_.modSettleTaus, std::memory_order_relaxed);
          fx_.setIdleSleep(policy_.fxBlocks, policy_.fxThresh, &fxSkipLocal_); for (int l = 0; l < kNumLayers; l++) { eqQuiet_[l] = 0; eqAsleep_[l] = false; } }
      powerStats_.quanta.fetch_add(1, std::memory_order_relaxed); }
    if (preset_) { bool anySolo = false; for (int i = 0; i < kNumMix; i++) anySolo = anySolo || preset_->mixSolo[i];
        for (int i = 0; i < kNumMix; i++) mixTgt_[i] = (preset_->mixMute[i] || (anySolo && !preset_->mixSolo[i])) ? 0.0 : std::pow(10.0, (preset_->mixerGainDb[i] + mixOffDb_[i]) / 20.0); }
    if (preset_ && fxPending_ && clock_ >= fxDue_) { fxPending_ = false; modFx(fxAccPending_); }
    if (preset_) {
        if (tickSeconds <= 0) modTick(t0);
        else { while (true) { const uint64_t f = tickOrigin_ + (uint64_t)(std::floor(tickIndex_ * tickSeconds * sr_ / 128.0 + 0.5)) * 128; if (clock_ < f) break; modTick(t0); tickIndex_++; } }
    }
    std::memset(bus_, 0, sizeof(bus_)); std::memset(xfxBus_, 0, sizeof(xfxBus_)); std::memset(carBus_, 0, sizeof(carBus_)); carOn_ = voc_.P.on;
    uint64_t nm[2] = {0, 0}; for (int i = 0; i < 16; i++) smpHeadAcc_[i] = -1.f;
    for (auto& v : voices_) {
        if (!v.alive) continue;
        if (!v.released && !v.arpVoice) nm[(v.note >> 6) & 1] |= 1ull << (v.note & 63);
        serviceEnvRepeat(v, t0);
        if (preset_ && preset_->xfx && preset_->xfx->inZone(v.note)) {   // XFX tap: the voice is rendered once and added to its own layer bus and to the XFX send bus
            float tl_[kQuantum], tr_[kQuantum]; std::memset(tl_, 0, sizeof(tl_)); std::memset(tr_, 0, sizeof(tr_)); renderVoice(v, t0, tl_, tr_);
            for (int i = 0; i < kQuantum; i++) { bus_[v.layer][0][i] += tl_[i]; bus_[v.layer][1][i] += tr_[i]; xfxBus_[0][i] += tl_[i]; xfxBus_[1][i] += tr_[i]; }
        } else
        renderVoice(v, t0, bus_[v.layer][0], bus_[v.layer][1]);
        // Intelligent voice management: a RELEASED voice that has been inaudible for the guard time is freed early (held notes are never touched; audible tails never cut).
        bool reap = false;
        if (policy_.voiceBlocks > 0 && v.released) { if (v.outPk <= policy_.voiceThresh) { if (++v.quietBlocks >= policy_.voiceBlocks) reap = true; } else v.quietBlocks = 0; }
        if (reap) powerStats_.voicesReaped.fetch_add(1, std::memory_order_relaxed);
        if (reap || (v.released && t0 >= v.stopAt)) { v.alive = false; killLuaVoices(v); releaseVoiceFx(v); for (int gi = 0; gi < v.ng; gi++) if (v.g[gi].combSlot >= 0) { combFree_.push_back(v.g[gi].combSlot); v.g[gi].filt.detachComb(); v.g[gi].combSlot = -1; } }
    }
    for (int l = 0; l < kNumLayers; l++) { const uint32_t m = tapMask_[l].load(std::memory_order_relaxed);
        for (int g = 0; g < 16; g++) { float* acc = tapAcc_[l][g];
            if (m & (1u << g)) { float* r = &tapRing_[(size_t)(l * 16 + g) * kTapN]; int w = tapW_[l][g].load(std::memory_order_relaxed); for (int i = 0; i < kQuantum; i++) { r[w] = acc[i]; w = (w + 1) & (kTapN - 1); } tapW_[l][g].store(w, std::memory_order_release); }
            std::memset(acc, 0, sizeof(float) * kQuantum); } }
    for (int i = 0; i < 16; i++) smpHead_[i].store(smpHeadAcc_[i], std::memory_order_relaxed);
    noteMask_[0].store(nm[0], std::memory_order_relaxed); noteMask_[1].store(nm[1], std::memory_order_relaxed);
    // pre-bus taps (cross-mod sources, read 3 ms later)
    for (int l = 0; l < kNumLayers; l++) for (int i = 0; i < kQuantum; i++) tap_[l].push((float)(0.5 * ((double)bus_[l][0][i] + bus_[l][1][i])));
    // Mixer meters: per-track peak of the post-fader signal (max over voices), ~300 ms fall-off
    { const double fall = std::exp(-(kQuantum / sr_) / 0.3);
      for (int i = 0; i < kNumMix; i++) { const float cur = mixMeter_[i].load(std::memory_order_relaxed); const float nv = std::max(mixPeak_[i], (float)(cur * fall)); mixMeter_[i].store(nv < 1e-5f ? 0.f : nv, std::memory_order_relaxed); mixPeak_[i] = 0; } }
    // sidechain: peak-follow every layer's raw bus (instant attack, ~120 ms release), then duck listeners while above threshold
    {
        const double dtq = kQuantum / sr_;
        for (int l = 0; l < kNumLayers; l++) {
            double pk = 0; for (int i = 0; i < kQuantum; i++) pk = std::max(pk, std::fabs(0.5 * ((double)bus_[l][0][i] + bus_[l][1][i])));
            scEnv_[l] = pk > scEnv_[l] ? pk : scEnv_[l] * std::exp(-dtq / 0.12);
        }
        for (int l = 0; l < kNumLayers; l++) {
            const LayerCfg& lc = effL(l); double target = 1;
            if (lc.scSrc >= 0 && lc.scSrc < kNumLayers && scEnv_[lc.scSrc] > lc.scThresh) target = 1 - lc.scAmount;
            const double cur = scDuck_[l].target;
            if (std::fabs(cur - target) > 0.001) scDuck_[l].setTarget(target, sr_, target < cur ? 0.006 : 0.12);
        }
    }
    { const int so = soloMask_.load(std::memory_order_relaxed), mu = muteMask_.load(std::memory_order_relaxed);   // web updateSoloMuteGains: setTargetAtTime(audible, tau 0.01)
      for (int l = 0; l < kNumLayers; l++) { const bool audible = !((mu >> l) & 1) && ((so & (kXfxBit - 1)) == 0 || ((so >> l) & 1)); /* bit 6 (XFX) never counts as a layer solo */ soloMute_[l].setTarget(audible ? 1.0 : 0.0, sr_, 0.01); } }
    // layer buses -> solo/mute -> (EQ -> master) and/or FX sends; the chain's output joins the master bus
    float masterL[kQuantum], masterR[kQuantum]; std::fill(masterL, masterL + kQuantum, 0.f); std::fill(masterR, masterR + kQuantum, 0.f);
    float sendL[kNumLayers][kQuantum], sendR[kNumLayers][kQuantum]; const float* spL[kFxLayers]; const float* spR[kFxLayers]; spL[kXfxSend] = xfxBus_[0]; spR[kXfxSend] = xfxBus_[1];
    for (int l = 0; l < kNumLayers; l++) {
        float mono[kQuantum], eqOut[kQuantum];
        if (xmChainN_[l] > 0) {   // cross-mod insert between the bus gain and solo/mute
            float xl[kQuantum], xr[kQuantum], ol[kQuantum], orr[kQuantum];
            for (int i = 0; i < kQuantum; i++) { const double gb = (l == L_WT || l == L_FM) ? busGain_[l].tick() : 1.0; xl[i] = (float)(bus_[l][0][i] * gb); xr[i] = (float)(bus_[l][1][i] * gb); }
            xmodChainProcess(l, xl, xr, ol, orr, kQuantum);
            for (int i = 0; i < kQuantum; i++) { const double sm = soloMute_[l].tick() * scDuck_[l].tick(); sendL[l][i] = (float)(ol[i] * sm); sendR[l][i] = (float)(orr[i] * sm); mono[i] = (float)(0.5 * ((double)ol[i] + orr[i]) * sm); }
        } else
        for (int i = 0; i < kQuantum; i++) {
            const double g = ((l == L_WT || l == L_FM) ? busGain_[l].tick() : 1.0) * soloMute_[l].tick() * scDuck_[l].tick();
            sendL[l][i] = (float)(bus_[l][0][i] * g); sendR[l][i] = (float)(bus_[l][1][i] * g);
            mono[i] = (float)(0.5 * ((double)bus_[l][0][i] + bus_[l][1][i]) * g);
        }
        spL[l] = sendL[l]; spR[l] = sendR[l];
        if (fx_.layerHasActiveSend(l)) continue;   // insert behaviour: the layer only reaches the master through the chain (its EQ node is unplugged and idles)
        EQ::Params ep; for (int b = 0; b < 4; b++) { ep.freq[b] = eqFreq_[l][b].advance(kQuantum); ep.gain[b] = eqGain_[l][b].advance(kQuantum); }
        if (policy_.eqBlocks > 0) {   // DSP optimisation: a layer EQ with silent input and a decayed output is skipped (parameters keep gliding above)
            float inPk = 0; for (int i = 0; i < kQuantum; i++) inPk = std::max(inPk, std::fabs(mono[i]));
            if (eqAsleep_[l] && inPk <= policy_.fxThresh) { powerStats_.eqBlocksSkipped.fetch_add(1, std::memory_order_relaxed); continue; }
            eqAsleep_[l] = false; eq_[l].process(mono, eqOut, kQuantum, ep);
            float outPk = 0; for (int i = 0; i < kQuantum; i++) outPk = std::max(outPk, std::fabs(eqOut[i]));
            if (inPk <= policy_.fxThresh && outPk <= policy_.fxThresh) { if (++eqQuiet_[l] >= policy_.eqBlocks) eqAsleep_[l] = true; } else eqQuiet_[l] = 0;
        } else eq_[l].process(mono, eqOut, kQuantum, ep);
        for (int i = 0; i < kQuantum; i++) { masterL[i] += eqOut[i]; masterR[i] += eqOut[i]; }
    }
    if (fx_.stageCount() > 0) {
        float fL[kQuantum], fR[kQuantum]; fx_.process(spL, spR, fL, fR, kQuantum); powerStats_.fxBlocksSkipped.store(fxSkipLocal_, std::memory_order_relaxed);
        for (int i = 0; i < kQuantum; i++) { masterL[i] += fL[i]; masterR[i] += fR[i]; }
    }
    if (voc_.P.on) {
        float ext[kQuantum], modm[kQuantum], cl[kQuantum], cr[kQuantum];
        const uint32_t avail = extW_ - extR_; if (avail > 8000) extR_ = extW_ - kQuantum;   // overrun: keep only the newest block
        for (int i = 0; i < kQuantum; i++) { ext[i] = (extW_ - extR_ > 0) ? extIn_[extR_ & 8191] : 0.f; if (extW_ - extR_ > 0) extR_++; }
        const int ms = voc_.P.modSrc; const int ml = ms == 1 ? L_WT : ms == 2 ? L_FM : ms == 3 ? L_SMP : L_PRO;
        for (int i = 0; i < kQuantum; i++) modm[i] = ms == 0 ? ext[i] : 0.5f * (bus_[ml][0][i] + bus_[ml][1][i]);
        const int cs = voc_.P.carSrc;
        if (cs >= 2 && cs <= 4) { const int cl_ = cs == 2 ? L_WT : cs == 3 ? L_FM : L_PRO; for (int i = 0; i < kQuantum; i++) { cl[i] = bus_[cl_][0][i]; cr[i] = bus_[cl_][1][i]; } }
        else { std::memcpy(cl, carBus_[0], sizeof(cl)); std::memcpy(cr, carBus_[1], sizeof(cr)); }
        voc_.process(modm, cl, cr, ext, masterL, masterR, kQuantum);
    }
    float tl[kQuantum], tr[kQuantum];
    const double mvol = (preset_ && preset_->mixMute[kNumMix]) ? 0.0 : masterVolume;   // Master M button
    toneFreq_.target = masterToneToFreq(clampd(masterTone + toneOff_, 0, 1));
    for (int i = 0; i < kQuantum; i++) {
        const double before = toneFreq_.v; const double f = toneFreq_.tick();
        if (std::fabs(f - toneFreq_.target) > 1e-4 * toneFreq_.target || before != f) { tone_[0].set(f, 0.707, sr_); tone_[1] = tone_[0].coeffsOnly(tone_[1]); }
        tl[i] = (float)tone_[0].tick((double)masterL[i] * mvol); tr[i] = (float)tone_[1].tick((double)masterR[i] * mvol);
    }
    Compressor::Params cp; cp.thresholdDb = -8; cp.kneeDb = 3; cp.ratio = 20; cp.attack = 0.003f; cp.release = 0.15f;
    comp_.process(tl, tr, outL, outR, kQuantum, cp);
    mout_.process(outL, outR, kQuantum);   // optional master limiter + loudness meter (bit-exact pass-through while off)
    // Always-on final safety ceiling (port of the web app's safetyShaper + clamp): identity up to 0.89, then a C1-continuous tanh knee into 0.95,
    // and a last +/-0.995 clamp. The compressor alone cannot stop a dense chord of 5+ notes overshooting; this guarantees no digital clipping even
    // with the optional limiter off. Bit-exact below 0.89 so quiet material and the golden tests are untouched.
    if (safetyCeiling_) for (int i = 0; i < kQuantum; i++) {
        for (float* ch : { outL, outR }) {
            const float a = std::fabs(ch[i]);
            if (a > 0.89f) { float y = 0.89f + 0.06f * std::tanh((a - 0.89f) / 0.06f); y = std::min(y, 0.995f); ch[i] = std::copysign(y, ch[i]); }
        }
    }
}

}  // namespace vv

namespace vv {
inline void Synth::setSampleMix(bool on, double pos) {
    sampleMixOn = on; sampleMixPos = pos;
    for (auto& v : voices_) if (v.alive && preset_ && preset_->layers[v.layer].type == LayerType::Sample)
        for (int i = 0; i < 16; i++) if (v.smp[i].on) v.smp[i].mix.setTarget(F32(sampleMixGainFor(i, pos)), sr_, 0.02);
}
}  // namespace vv
#include "modulation.hpp"

namespace vv {
inline void Synth::xmodApply(int i, double a) {
    XmodRt& x = xm_[i]; if (!x.active) return;
    if (x.type == 0) { x.pg.setTarget(1 - a, sr_, 0.01); x.sg.setTarget(a, sr_, 0.01); }
    else if (x.type == 1) { x.pg.setTarget(1 - a * 0.5, sr_, 0.01); x.sg.setTarget(a * 0.5, sr_, 0.01); }
    else x.sg.setTarget(a * 0.003, sr_, 0.01);
}
// rebuildXmod(): one insert chain per destination layer, slots in index order. Fresh gain nodes start at 1 and glide to the amount (tau 10 ms).
inline void Synth::xmodRebuild() {
    for (int l = 0; l < kNumLayers; l++) xmChainN_[l] = 0;
    for (int i = 0; i < 4; i++) xm_[i].active = false;
    if (!preset_) return;
    for (int k = 0; k < kNumLayers; k++) for (int i = 0; i < 4; i++) {
        const XmodCfg& c = preset_->xmod[i]; XmodRt& x = xm_[i];
        const int sl = layerIndex(c.src), dl = layerIndex(c.dst);
        if (!c.on || sl < 0 || dl != k || sl == dl || c.type == "FM") continue;
        x.active = true; x.type = c.type == "RM" ? 0 : c.type == "AM" ? 1 : 2; x.src = sl; x.dst = dl; x.offOn = false;
        x.pg.set(1); x.sg.set(1); std::memset(x.dl, 0, sizeof(x.dl)); x.w = 0;
        xmodApply(i, clampd(c.amount, 0, 1));
        xmChain_[k][xmChainN_[k]++] = i;
    }
}
inline void Synth::xmodChainProcess(int layer, const float* inL, const float* inR, float* outL, float* outR, int n) {
    std::memcpy(outL, inL, n * sizeof(float)); std::memcpy(outR, inR, n * sizeof(float));
    const uint64_t base = clock_;
    for (int ci = 0; ci < xmChainN_[layer]; ci++) {
        XmodRt& x = xm_[xmChain_[layer][ci]]; const TapRing& src = tap_[x.src];
        for (int i = 0; i < n; i++) {
            const double s = src.sampleAt((double)(base + i) - 0.003 * sr_);
            if (x.type < 2) {
                const double g = x.pg.tick() + x.sg.tick() * s;
                outL[i] = (float)(outL[i] * g); outR[i] = (float)(outR[i] * g);
            } else {
                const double dt = std::max(0.0, std::min(0.02, 0.004 + x.sg.tick() * s)), dsamp = dt * sr_;
                // Chromium DelayNode (a-rate): write the input first, then read at (write - delay) with linear interpolation
                x.dl[0][x.w & 4095] = outL[i]; x.dl[1][x.w & 4095] = outR[i];
                const double rp = (double)x.w - dsamp; const double fl = std::floor(rp); const double f = rp - fl;
                const uint32_t i0 = (uint32_t)((int64_t)fl & 4095), i1 = (i0 + 1) & 4095;
                outL[i] = (float)(x.dl[0][i0] * (1 - f) + x.dl[0][i1] * f); outR[i] = (float)(x.dl[1][i0] * (1 - f) + x.dl[1][i1] * f);
                x.w++;
            }
        }
    }
}
inline void Synth::rebuildVocoder() {
    HvParams p = voc_.P; p.on = false;
    if (!preset_) { voc_.setParams(p); return; }
    const Json& v = preset_->vocoder; p = HvParams();
    p.on = v.boolean("on", false); p.bands = (int)v.num("bands", 8); if (p.bands != 8 && p.bands != 12 && p.bands != 16 && p.bands != 24) p.bands = p.bands >= 20 ? 24 : p.bands >= 14 ? 16 : p.bands >= 10 ? 12 : 8;
    p.logSpacing = v.str("spacing", "log") == "log";   // the web app: log, anything else ("lin") is linear
    p.poles4 = v.boolean("poles4", false); p.mix = v.num("mix", 0.8);
    const std::string mode = v.str("mode", "classic"); p.mode = mode == "classic" ? 0 : mode == "pitch" ? 2 : mode == "spectral" ? 3 : 1;   // anything else (the retired "formant"/LPC mode included) behaves as the web does: the worklet runs in mode 0 and outputs silence
    const std::string ms = v.str("modSrc", "WT"); p.modSrc = ms == "mic" ? 0 : ms == "FM" ? 2 : ms == "SMP" ? 3 : ms == "PRO" ? 4 : 1;
    const std::string cs = v.str("carSrc", "SMP"); p.carSrc = cs == "internal" ? 0 : cs == "WT" ? 2 : cs == "FM" ? 3 : cs == "PRO" ? 4 : cs == "external" ? 5 : 1;
    p.micGainDb = v.num("micGainDb", 0); p.hpf = clampd(v.num("hpf", 100), 80, 150); p.gateDb = v.num("gateDb", -80); p.comp = v.boolean("comp", true);
    p.sibSens = clampd(v.num("sibSens", 0.5), 0, 1); p.noiseDb = v.num("noiseDb", -18); p.formantSt = clampd(v.num("formantSt", 0), -12, 12);
    p.releaseMs = clampd(v.num("releaseMs", 18), 2, 500); p.q = clampd(v.num("q", 4), 0.5, 20); p.drive = clampd(v.num("drive", 0), 0, 100);
    p.chorusDepth = clampd(v.num("chorusDepth", 0), 0, 100); p.chorusRate = clampd(v.num("chorusRate", 0.8), 0.1, 5);
    p.pitchSpeedMs = clampd(v.num("pitchSpeed", 20), 0, 400); hvScaleMask(v.str("pitchScale", "chromatic"), p.mask); p.root = (int)v.num("pitchRoot", 0); p.held = v.boolean("pitchHeld", false); p.drone = v.boolean("drone", false);
    if (const Json* sl = v.get("slots")) for (size_t i = 0; i < sl->a.size() && i < 16; i++) p.slots[i] = (sl->a[i].asNum(1) != 0);
    voc_.setParams(p);
}
}  // namespace vv

namespace vv {
inline void Synth::updatePreset(std::shared_ptr<const Preset> p) {
    if (!preset_ || !p) { if (p) setPreset(std::move(p)); return; }
    const std::shared_ptr<const Preset> oldP = preset_;   // (the caller keeps the old one alive past this call)
    const Preset& O = *oldP; const Preset& N = *p;
    bool sameFx = O.fx.size() == N.fx.size() && O.oversample == N.oversample;
    for (size_t i = 0; sameFx && i < O.fx.size(); i++) if (O.fx[i].type != N.fx[i].type) sameFx = false;
    preset_ = std::move(p);
    if (!sameFx) { fx_.configure(N.fx, N.bpm, N.oversample); }
    else {
        fx_.setBpm(N.bpm);
        for (size_t i = 0; i < N.fx.size(); i++) {
            if (!jsonEqual(O.fx[i].params, N.fx[i].params)) fx_.setStageParams((int)i, N.fx[i].params);
            if (O.fx[i].enabled != N.fx[i].enabled) fx_.setStageEnabled((int)i, N.fx[i].enabled);
            for (int l = 0; l < kNumLayers; l++) if (O.fx[i].sends[l] != N.fx[i].sends[l]) fx_.setStageSend((int)i, l, N.fx[i].sends[l]);
        }
    }
    double ph[kNumLfo], mb[4]; for (int k = 0; k < kNumLfo; k++) ph[k] = lfo_[k].phase; for (int k = 0; k < 4; k++) mb[k] = macroBase_[k];
    modReset();
    for (int k = 0; k < kNumLfo; k++) lfo_[k].phase = ph[k]; for (int k = 0; k < 4; k++) if (O.macros[k] == N.macros[k]) macroBase_[k] = mb[k];
    bool xmodChanged = false; for (int i = 0; i < 4; i++) { const XmodCfg &a = O.xmod[i], &b = N.xmod[i]; if (a.on != b.on || a.src != b.src || a.dst != b.dst || a.type != b.type || a.amount != b.amount) xmodChanged = true; }
    if (xmodChanged) xmodRebuild();
    if (!jsonEqual(O.vocoder, N.vocoder)) rebuildVocoder();
    for (int l = 0; l < kNumLayers; l++) for (int b = 0; b < 4; b++) {
        eqFreq_[l][b].setTarget(clampd(N.layers[l].eq[b].freq, 20, 18000), sr_, 0.01);
        eqGain_[l][b].setTarget(clampd(N.layers[l].eq[b].gain, -15, 15), sr_, 0.01);
    }
}

// Envelope repeat: schedules the next A+D cycles of the amp envelopes just in time (a 2-quantum horizon keeps the fixed-capacity Param timelines small).
inline void Synth::serviceEnvRepeat(Voice& v, double t0) {
    const double h = 2.0 * kQuantum / sr_ + 0.0005;
    for (int gi = 0; gi < v.ng; gi++) { Group& g = v.g[gi]; if (g.rep.on && g.active && !v.released) g.rep.scheduleOn(g.amp, t0, h); }
    for (auto& q : v.smp) if (q.rep.on && q.on && !v.released) q.rep.scheduleOn(q.env, t0, h);
    if (v.sid.on && !v.released) for (int k = 0; k < 3; k++) if (v.sidRep[k].on) v.sidRep[k].service(t0, 0.0, [&](double) { v.sid.p[k].rt++; });
}
}  // namespace vv
