// Modulation engine: a C++ port of the web app's 100 ms scheduler tick (LFOs, sources, matrix, macros, per-voice mod inputs, live oscillator /
// FM / FX parameter pushes). Included at the end of synth_impl.hpp. Formulas and scale factors are the web app's; see docs/WEB_QUIRKS.md for the
// handful of places where the native engine deliberately differs (direct-offset FX modulation, musically correct pitch bend, no p-lock overwrite).
#pragma once

namespace vv {


inline double lfoHashNoise(double seed) { const double x = std::sin(seed * 12.9898) * 43758.5453; return (x - std::floor(x)) * 2 - 1; }

inline double evalCurveY(const std::vector<CurvePt>& pts, double p) {
    size_t i = 0; while (i + 2 < pts.size() && pts[i + 1].x < p) i++;
    const CurvePt& a = pts[i]; if (i + 1 >= pts.size()) return a.y;
    const CurvePt& b = pts[i + 1];
    if (a.lin) { const double dx = b.x - a.x; return dx < 1e-9 ? b.y : a.y + (b.y - a.y) * clampd((p - a.x) / dx, 0, 1); }   // straight segment (R11)
    const double cxv = a.cx, cyv = a.cy;
    const double a0 = a.x - 2 * cxv + b.x, b0 = 2 * (cxv - a.x), c0 = a.x - p;
    double t;
    if (std::fabs(a0) < 1e-6) { t = std::fabs(b0) < 1e-6 ? 0 : clampd(-c0 / b0, 0, 1); }
    else {
        const double disc = std::max(0.0, b0 * b0 - 4 * a0 * c0), sq = std::sqrt(disc);
        const double t1 = (-b0 + sq) / (2 * a0), t2 = (-b0 - sq) / (2 * a0);
        t = (t1 >= -0.001 && t1 <= 1.001) ? clampd(t1, 0, 1) : clampd(t2, 0, 1);
    }
    const double mt = 1 - t;
    return mt * mt * a.y + 2 * mt * t * cyv + t * t * b.y;
}

// Basic (non-custom, non-morph) shapes as pure functions of phase - shared by the plain LFO, Morph / Morph 2 and the smoothing window.
inline double lfoStrollShapeAt(double p) {
    const int N = 9; const double x = p * N; const int i0 = (int)std::floor(x) % N, i1 = (i0 + 1) % N; const double f = x - std::floor(x);
    const double v0 = lfoHashNoise(i0 * 5.37 + 0.61), v1 = lfoHashNoise(i1 * 5.37 + 0.61), sf = f * f * (3 - 2 * f);
    return v0 + (v1 - v0) * sf;
}
inline double lfoOrganicShapeAt(double p) {
    const double T = 2 * 3.141592653589793;
    const double v = std::sin(T * p) + 0.5 * std::sin(T * p * 3 + 1.3) + 0.28 * std::sin(T * p * 5 + 2.6) + 0.15 * std::sin(T * p * 2 + 4.0);
    return clampd(v / 1.8, -1, 1);
}
inline double lfoBaseShapeAt(const std::string& w, double p) {
    if (w == "triangle") return p < 0.5 ? (4 * p - 1) : (3 - 4 * p);
    if (w == "pulse") return p < 0.5 ? 1 : -1;
    if (w == "sawUp") return 2 * p - 1;
    if (w == "sawDown") return 1 - 2 * p;
    if (w == "sh") return lfoHashNoise(std::floor(p * 8) * 7.31 + 1.7);
    if (w == "noise") return lfoHashNoise(std::floor(p * 32) * 3.17 + 9.2);
    if (w == "staircase") return (std::floor(p * 8) / 7.0) * 2 - 1;
    if (w == "stairsDown") return 1 - (std::floor(p * 8) / 7.0) * 2;
    if (w == "stroll") return lfoStrollShapeAt(p);
    if (w == "organic") return lfoOrganicShapeAt(p);
    return std::sin(2 * 3.141592653589793 * p);
}
inline double lfoMorphAt(const char* const* set, double p, double m) {
    const int n = 5; const double x = clampd(m, 0, 1) * (n - 1); const int i0 = (int)std::floor(x), i1 = std::min(i0 + 1, n - 1); const double f = x - i0;
    const double a = lfoBaseShapeAt(set[i0], p), b = lfoBaseShapeAt(set[i1], p); return a + (b - a) * f;
}
inline double lfoEval(const LfoCfg& l, double phase, double smOff, double morphOff) {
    static const char* const kMorph1[5] = {"triangle", "pulse", "sine", "sawUp", "staircase"};
    static const char* const kMorph2[5] = {"sawDown", "stairsDown", "sh", "noise", "stroll"};
    double ph = std::fmod(phase + l.phaseOffset, 1.0); ph = std::fmod(ph + 1.0, 1.0);
    const double p = ph; const std::string& w = l.waveform; const double morph = clampd(l.morph + morphOff, 0, 1);
    auto rawAt = [&](double q) -> double {
        if (w == "custom") return l.curve.size() < 2 ? std::sin(2 * 3.141592653589793 * q) : evalCurveY(l.curve, q) * 2 - 1;
        if (w == "morph") return lfoMorphAt(kMorph1, q, morph);
        if (w == "morph2") return lfoMorphAt(kMorph2, q, morph);
        return lfoBaseShapeAt(w, q);
    };
    double raw = rawAt(p);
    const double sm = clampd(l.smoothing + smOff, 0, 1);
    if (sm > 0.001) {   // raised-cosine average of neighbouring phase samples rounds off steps and corners
        const double win = sm * 0.12; const int K = 4; double sum = 0, wsum = 0;
        for (int i = -K; i <= K; i++) { const double wt = 0.5 + 0.5 * std::cos(3.141592653589793 * i / K); double pp = std::fmod(std::fmod(p + ((double)i / K) * win, 1.0) + 1.0, 1.0); sum += rawAt(pp) * wt; wsum += wt; }
        raw = sum / wsum;
    }
    if (!l.bipolar) return (raw + 1) / 2;
    return raw;
}
inline double Synth::lfoValue(int k, double phase, double smOff, double morphOff) const { return lfoEval(preset_->lfo[k], phase, smOff, morphOff); }

inline double Synth::lfoHz(int k) const {
    const LfoCfg& l = preset_->lfo[k];
    const double baseHz = l.rateMode == "hz" ? l.hz : 1.0 / ((60.0 / preset_->bpm) * l.division);
    return clampd(baseHz + lfoRateOff_[k] * 10, 0.001, 40);
}

// wavetableShapeValue(): brightness descriptor of the frame(s) the oscillator's position currently lands on.
inline double Synth::wtShapeValue(int oi) const {
    const auto& oscs = preset_->layers[L_WT].oscs; if (oi >= (int)oscs.size()) return 0;
    const WtTable* t = table(oscs[oi].table); if (!t || t->frames.empty()) return 0;
    const int nF = (int)t->frames.size(); const double seg = wtLive_[oi].seg;
    const int eff = std::max(1, (int)jsRound(nF * clampd(seg, 0.05, 1)));
    const double pos = clampd(wtLive_[oi].pos, 0, 1), ps = pos * (eff - 1); const int fi = (int)std::floor(ps); const double ff = ps - fi;
    auto desc = [&](int idx) { const std::vector<float>& f = t->frames[std::min(idx, nF - 1)]; double tv = 0; for (size_t i = 1; i < f.size(); i++) tv += std::fabs((double)f[i] - (double)f[i - 1]); return clampd(tv / (double)f.size() * 8 - 1, -1, 1); };
    const double a = desc(fi), b = desc(std::min(fi + 1, eff - 1)); return a + (b - a) * ff;
}
inline double Synth::autoLaneAt(const AutoLane& l) const {
    const size_t n = l.v.size(); if (n == 0) return 0.5;
    const double loop = (double)n / kAutoR; double bp = std::fmod(autoBeat_, loop); if (bp < 0) bp += loop;
    const double p = bp * kAutoR; const size_t i = (size_t)p % n; const double f = p - std::floor(p);
    const double a = l.v[i], b = l.v[(i + 1) % n]; return a + (b - a) * f;
}
inline void Synth::setAutoClock(double beat, bool running) {
    autoBeat_ = beat; autoRun_ = running; autoSrcAny_ = false;
    for (double& o : autoSrcOff_) o = 0;
    if (!running || !preset_) return;
    for (const AutoLane& l : preset_->automation.lanes) if (l.isSource && l.on) { const int s = parseModSource(l.key.substr(2)); if (s >= 0 && s < 40) { autoSrcOff_[s] = (autoLaneAt(l) - 0.5) * 2; autoSrcAny_ = true; } }
}
inline double Synth::modSource(int src, const Voice* v, double T) {
    if (src >= kClipSrcBase) { const int i = src - kClipSrcBase; return i < clips::kMaxLaneRefs ? clipLane_[i] : 0.0; }
    if (src >= kAutoSrcBase) { if (!autoRun_ || !preset_) return 0; const size_t i = (size_t)(src - kAutoSrcBase); return i < preset_->automation.lanes.size() ? (autoLaneAt(preset_->automation.lanes[i]) - 0.5) * 2 : 0; }
    const double x = modSourceRaw(src, v, T);
    return (autoSrcAny_ && src >= 0 && src < 40 && autoSrcOff_[src] != 0) ? clampd(x + autoSrcOff_[src], -1, 1) : x;
}
// Position auto-scan (web wtAutoScanValue). ampEnv = the WT amp envelope, filterEnv = this oscillator's filter envelope (normalised by its amount),
// pitchEnv = its drawn pitch envelope curve; any other id is a matrix source (bipolar -1..1 mapped to 0..1, envelope sources already 0..1).
inline bool Synth::wtAutoScan(const OscCfg& o, double T, double& out) {
    const std::string& s = o.positionLfoSource; if (s.empty()) return false;
    const double dt = T - wtNoteStart_;
    if (s == "ampEnv") { out = clampd(modSource(S_ENV_WT, nullptr, T), 0, 1); return true; }
    if (s == "filterEnv") {
        const FilterEnvCfg& f = o.filterEnv; const double a = std::max(0.001, f.a), d = std::max(0.001, f.d); double y;
        if (dt < 0) y = 0; else if (dt < a) y = dt / a; else if (dt < a + d) y = 1 - (1 - f.s) * ((dt - a) / d); else y = f.s;
        out = clampd(y, 0, 1); return true;
    }
    if (s == "pitchEnv") { out = (o.peg.pts.size() < 2 || dt < 0) ? 0 : clampd(evalCurveY(o.peg.pts, clampd(dt / std::max(0.001, o.peg.time), 0, 1)), 0, 1); return true; }
    const int src = parseModSource(s); if (src < 0) return false;
    const double v = modSource(src, nullptr, T);
    out = (src == S_ENV_WT || src == S_ENV_FM || src == S_ENV_SMP) ? clampd(v, 0, 1) : clampd(v * 0.5 + 0.5, 0, 1); return true;
}
inline double Synth::modSourceRaw(int src, const Voice* v, double T) {
    auto env = [&](const EnvInfo& e) -> double {
        if (!e.valid) return 0; const double dt = T - e.start;
        if (dt < e.a) return e.peak * (dt / e.a);
        if (dt < e.a + e.d) return e.peak - (e.peak - e.peak * e.s) * ((dt - e.a) / e.d);
        return e.peak * e.s;
    };
    switch (src) {
        case S_LFO1: case S_LFO2: case S_LFO3: case S_LFO4: case S_LFO5: case S_LFO6: {
            const int k = src - S_LFO1; const double amt = clampd(preset_->lfo[k].amount + lfoAmtOff_[k], 0, 2);
            return lfoValue(k, lfo_[k].phase + lfoPhaseOff_[k], lfoSmOff_[k], lfoMorphOff_[k]) * amt;
        }
        case S_LORENZ: return clampd(chaos_[0][0] / 20.0, -1, 1);
        case S_ROSSLER: return clampd(chaos_[1][0] / 12.0, -1, 1);
        case S_NOTE: return v ? clampd((v->note - 60) / 36.0, -1, 1) : 0;   // C4 = 0, +-3 octaves = +-1
        case S_VELOCITY: return ((v && v->velocity != 0) ? v->velocity : 0.8) * 2 - 1;
        case S_MODWHEEL: return modwheel * 2 - 1;
        case S_RANDOM: { if (!v) return 0; Voice* mv = const_cast<Voice*>(v); if (!mv->rndSet) { mv->rnd = rng_.next() * 2 - 1; mv->rndSet = true; } return mv->rnd; }
        case S_ENV_WT: return env(envInfo_[L_WT]);
        case S_ENV_FM: return env(envInfo_[L_FM]);
        case S_ENV_SMP: return env(envInfo_[L_SMP]);
        case S_ENV_FOLLOW: return envFollowInfo_.valid ? env(envFollowInfo_) * 2 - 1 : 0;
        case S_MACRO1: case S_MACRO2: case S_MACRO3: case S_MACRO4: return macroValue(src - S_MACRO1) * 2 - 1;
        case S_WT1POS: return clampd(wtLive_[0].pos, 0, 1) * 2 - 1;
        case S_WT2POS: return clampd(wtLive_[1].pos, 0, 1) * 2 - 1;
        case S_MPE_TIMBRE: { if (!v || !mpeEnabled) return 0; const MpeExpr& e = mpeExpr_[v->note & 127]; return e.has ? e.timbre * 2 - 1 : 0; }
        case S_MPE_PRESSURE: { if (!v || !mpeEnabled) return 0; const MpeExpr& e = mpeExpr_[v->note & 127]; return e.has ? e.pressure * 2 - 1 : 0; }
        case S_WT1SHAPE: case S_WT2SHAPE: return wtShapeValue(src == S_WT1SHAPE ? 0 : 1);
        case S_XY1X: case S_XY1Y: case S_XY2X: case S_XY2Y: case S_XY3X: case S_XY3Y: case S_XY4X: case S_XY4Y: return xy_[src - S_XY1X] * 2 - 1;
        default: return 0;
    }
}

inline void Synth::rebuildClipRoutes() {
    for (size_t i = 0; i < routes_.size();) { if (routes_[i].src >= kClipSrcBase) routes_.erase(routes_.begin() + (long)i); else i++; }
    for (double& x : clipLane_) x = 0;
    if (!clipSet_) return;
    for (size_t i = 0; i < clipSet_->laneRefs.size() && i < (size_t)clips::kMaxLaneRefs; i++) {
        const clips::LaneRef& lr = clipSet_->laneRefs[i]; const clips::Clip& c = clipSet_->at(lr.slot); if (lr.lane < 0 || lr.lane >= (int)c.lanes.size()) continue;
        ModRoute r; r.src = kClipSrcBase + (int)i; r.amt = 1.0; if (!parseModDest(c.lanes[(size_t)lr.lane].key, r.layer, r.dest, r.g, r.scale)) continue;
        if (r.dest >= D_V_CUTOFF) hasVoiceRoutes_ = true; else r.layer = -1;
        routes_.push_back(r);
    }
}
inline void Synth::modReset() {
    routes_.clear(); hasVoiceRoutes_ = false;
    chaos_[0][0] = chaos_[1][0] = 0.1; chaos_[0][1] = chaos_[0][2] = chaos_[1][1] = chaos_[1][2] = 0; chaosRateOff_[0] = chaosRateOff_[1] = 0;
    for (size_t li = 0; li < preset_->automation.lanes.size(); li++) {   // destination automation lanes ride the same engine as hidden matrix routes
        const AutoLane& ln = preset_->automation.lanes[li]; if (ln.isSource || !ln.on) continue;
        ModRoute r; r.src = kAutoSrcBase + (int)li; r.amt = 1.0;
        if (!parseModDest(ln.key, r.layer, r.dest, r.g, r.scale)) continue;
        if (r.dest >= D_V_CUTOFF) hasVoiceRoutes_ = true; else r.layer = -1;
        routes_.push_back(r);
    }
    rebuildClipRoutes();
    for (auto& c : preset_->connections) {
        if (c.source.rfind("auto:", 0) == 0) continue;   // web-side hidden connections: rebuilt from the lanes above
        ModRoute r; r.src = parseModSource(c.source); r.amt = c.amount / 100.0;
        if (r.src == S_NONE || !parseModDest(c.dest, r.layer, r.dest, r.g, r.scale)) continue;
        if (r.dest >= D_V_CUTOFF) hasVoiceRoutes_ = true; else r.layer = -1;
        routes_.push_back(r);
    }
    for (int i = 0; i < kNumGen; i++) genHas_[i] = false;
    for (int l = 0; l < kNumLayers; l++) genOn_[l] = false;
    for (int i = 0; i < kNumVoc; i++) vocHas_[i] = false;
    for (auto& r : routes_) if (r.dest >= D_VOC0 && r.dest <= D_VOC_END) vocHas_[r.dest - D_VOC0] = true;
    for (auto& r : routes_) if (r.dest >= D_GEN0 && r.dest <= D_GEN_END) { genHas_[r.dest - D_GEN0] = true; genOn_[kGenDefs[r.dest - D_GEN0].layer] = true; }
    for (int l = 0; l < kNumLayers; l++) if (genOn_[l]) genL_[l] = preset_->layers[l];
    if (preset_) for (int l = 0; l < kNumLayers; l++) for (int b = 0; b < 4; b++) { eqFreq_[l][b].setTarget(clampd(preset_->layers[l].eq[b].freq, 20, 18000), sr_, 0.01); eqGain_[l][b].setTarget(clampd(preset_->layers[l].eq[b].gain, -15, 15), sr_, 0.01); }
    for (int k = 0; k < kNumLfo; k++) { lfo_[k].phase = preset_->lfo[k].phase; lfo_[k].holdCycle = false; lfoRateOff_[k] = lfoAmtOff_[k] = lfoPhaseOff_[k] = lfoSmOff_[k] = lfoMorphOff_[k] = 0; }
    for (int k = 0; k < 4; k++) { macroBase_[k] = preset_->macros[k]; macroOff_[k] = 0; xmodOff_[k] = 0; xmodHas_[k] = false; }
    arpRateOff_ = 0; toneOff_ = 0; wtFmModOn_ = toneModOn_ = smpMixModOn_ = false; wtTuneMod_ = fmTuneMod_ = genRoute_ = false;
    for (int i = 0; i < 8; i++) { fmRatioRoute_[i] = false; fmRatioLive_[i] = preset_->layers[L_FM].oscs.size() > (size_t)i ? preset_->layers[L_FM].oscs[i].ratio : 1; }
    fmFbRoute_[0] = fmFbRoute_[1] = false;
    for (auto& r : routes_) {
        if (r.dest >= D_FM_OPR1 && r.dest <= D_FM_OPR8) fmRatioRoute_[r.dest - D_FM_OPR1] = true;
        if (r.dest == D_FM_FB_A) fmFbRoute_[0] = true; if (r.dest == D_FM_FB_B) fmFbRoute_[1] = true;
        if (r.dest == D_V_GEN) genRoute_ = true;
    }
    for (int i = 0; i < 2; i++) {
        const OscCfg& o = preset_->layers[L_WT].oscs[i];
        wtLive_[i].pos = o.position; wtLive_[i].pwm = o.pwm; wtLive_[i].phase = o.phase; wtLive_[i].se = o.startEnd; wtLive_[i].seg = o.segment; wtLive_[i].hz = o.pwmRate; wtLive_[i].mod = false;
        wtOct_[i] = o.oct; wtSemi_[i] = o.semitone; wtFine_[i] = o.fineTune;
        fmOct_[i] = (int)preset_->layers[L_FM].engineOctave[i]; fmSemi_[i] = (int)preset_->layers[L_FM].engineSemitone[i]; fmFine_[i] = preset_->layers[L_FM].engineFine[i];
    }
    for (auto& e : envInfo_) e = EnvInfo{}; envFollowInfo_ = EnvInfo{};
    fxScratch_.clear(); fxModded_.assign(preset_->fx.size(), 0);
    for (auto& f : preset_->fx) fxScratch_.push_back(f.params);
    tickOrigin_ = clock_; tickIndex_ = 1; lastTickT_ = now();
}

// One matrix evaluation. T = engine time of this tick.
inline void Synth::modTick(double T) {
    const Preset& P = *preset_;
    double dt = T - lastTickT_; lastTickT_ = T; if (dt < 0) dt = 0;
    // 1. LFO phases (rate uses the previous tick's rate offsets, like the web)
    for (int k = 0; k < kNumLfo; k++) {
        LfoRt& r = lfo_[k]; const LfoCfg& c = P.lfo[k];
        if (c.held && !r.holdCycle) continue;
        const double np = r.phase + lfoHz(k) * dt;
        if (c.held && r.holdCycle && np >= 1) { r.phase = 0; r.holdCycle = false; } else r.phase = std::fmod(np, 1.0);
    }
    for (int k = 0; k < kNumLfo; k++) { lfoSnapPhase_[k].store((float)lfo_[k].phase, std::memory_order_relaxed); lfoSnapHz_[k].store((P.lfo[k].held && !lfo_[k].holdCycle) ? 0.f : (float)lfoHz(k), std::memory_order_relaxed); }   // R11 lock-free UI snapshot (no allocation)
    // 1b. chaos attractors: free-running Euler integration (step = dt * rate * k), re-seeded on blow-up
    for (int a = 0; a < 2; a++) {
        const double rate = clampd(P.chaosRate[a] + chaosRateOff_[a], 0, 2); double* c = chaos_[a];
        const double h = dt * rate * (a == 0 ? 0.02 : 0.06); double dx, dy, dz;
        if (a == 0) { const double sg = 10, rho = 28, beta = 8.0 / 3; dx = sg * (c[1] - c[0]); dy = c[0] * (rho - c[2]) - c[1]; dz = c[0] * c[1] - beta * c[2]; }
        else { const double aa = 0.2, bb = 0.2, cc = 5.7; dx = -c[1] - c[2]; dy = c[0] + aa * c[1]; dz = bb + c[2] * (c[0] - cc); }
        c[0] += dx * h; c[1] += dy * h; c[2] += dz * h;
        if (!std::isfinite(c[0]) || !std::isfinite(c[1]) || !std::isfinite(c[2]) || std::fabs(c[0]) > 200) { c[0] = 0.1; c[1] = 0; c[2] = 0; }
    }
    // 2. global accumulation
    double acc[D_COUNT]; for (double& a : acc) a = 0;
    bool anyLive = false;
    for (auto& r : routes_) { if (r.dest >= D_V_CUTOFF) continue; acc[r.dest] += modSource(r.src, nullptr, T) * r.amt * r.scale; anyLive = true; }
    (void)anyLive;
    for (int i = 0; i < 4; i++) macroOff_[i] = acc[D_MACRO1 + i];
    for (int i = 0; i < kNumLfo; i++) { lfoRateOff_[i] = acc[D_LFO_RATE1 + i]; lfoAmtOff_[i] = acc[D_LFO_AMT1 + i]; lfoPhaseOff_[i] = acc[D_LFO_PH1 + i]; lfoSmOff_[i] = acc[D_LFO_SM1 + i]; lfoMorphOff_[i] = acc[D_LFO_MORPH1 + i]; }
    for (int i = 0; i < kNumMix; i++) mixOffDb_[i] = acc[D_MIX0 + i];
    arpRateOff_ = acc[D_ARP_RATE]; chaosRateOff_[0] = acc[D_CHAOS_LORENZ_RATE]; chaosRateOff_[1] = acc[D_CHAOS_ROSSLER_RATE];
    for (int i = 0; i < 4; i++) { xmodOff_[i] = acc[D_XMOD1 + i]; xmodHas_[i] = false; }
    for (auto& r : routes_) if (r.dest >= D_XMOD1 && r.dest <= D_XMOD4) xmodHas_[r.dest - D_XMOD1] = true;
    for (int i = 0; i < 4; i++) {   // slot amount: base + this tick's offset (setTargetAtTime tau 10 ms), back to the base once the route is gone
        if (xmodHas_[i]) { xm_[i].offOn = true; xmodApply(i, clampd(P.xmod[i].amount + xmodOff_[i], 0, 1)); }
        else if (xm_[i].offOn) { xm_[i].offOn = false; xmodApply(i, clampd(P.xmod[i].amount, 0, 1)); }
    }
    // 3. wavetable oscillators (direct offset around the preset's own values, recomputed each tick)
    {
        const LayerCfg& WL = P.layers[L_WT];
        const double nAny[2] = {acc[D_WT_START] + acc[D_WT_SCAN] + acc[D_WT_O1SCAN], acc[D_WT_START] + acc[D_WT_SCAN] + acc[D_WT_O2SCAN]};
        const double pwmO[2] = {acc[D_WT_PWM] + acc[D_WT_O1PWM], acc[D_WT_PWM] + acc[D_WT_O2PWM]};
        const double hzO[2] = {acc[D_WT_O1PWMHZ], acc[D_WT_O2PWMHZ]};   // PWM-speed offset in Hz (route scale 10 Hz per unit)
        const double phO[2] = {acc[D_WT_PHASE] + acc[D_WT_O1PHASE], acc[D_WT_PHASE] + acc[D_WT_O2PHASE]};
        const double seO[2] = {acc[D_WT_STARTEND] + acc[D_WT_O1SE], acc[D_WT_STARTEND] + acc[D_WT_O2SE]};
        const double segO[2] = {acc[D_WT_O1SEG], acc[D_WT_O2SEG]};
        const double octO[2] = {acc[D_WT_O1OCT], acc[D_WT_O2OCT]}, semiO[2] = {acc[D_WT_O1SEMI], acc[D_WT_O2SEMI]}, fineO[2] = {acc[D_WT_O1FINE], acc[D_WT_O2FINE]};
        bool routeAny[2] = {false, false}, tuneRoute = false;
        for (auto& r : routes_) {
            const int d = r.dest;
            if (d == D_WT_START || d == D_WT_SCAN || d == D_WT_PWM || d == D_WT_PHASE || d == D_WT_STARTEND) routeAny[0] = routeAny[1] = true;
            if (d == D_WT_O1SCAN || d == D_WT_O1PWM || d == D_WT_O1PHASE || d == D_WT_O1SE || d == D_WT_O1SEG) routeAny[0] = true;
            if (d == D_WT_O2SCAN || d == D_WT_O2PWM || d == D_WT_O2PHASE || d == D_WT_O2SE || d == D_WT_O2SEG) routeAny[1] = true;
            if (d >= D_WT_O1OCT && d <= D_WT_O2FINE) tuneRoute = true;
        }
        const double posNow[2] = {wtLive_[0].pos, wtLive_[1].pos}; (void)posNow;
        for (int i = 0; i < 2; i++) {
            const OscCfg& o = WL.oscs[i]; WtLive& w = wtLive_[i];
            double asv = 0; const bool as = wtAutoScan(o, T, asv);
            w.hz = clampd(o.pwmRate + hzO[i], 0, 20);   // PWM speed: applied per sample in the voice render
            if (!(routeAny[i] || as || w.mod)) continue;
            w.mod = true;
            double basePos = o.position;
            if (as) basePos = asv;
            w.pos = clampd(basePos + nAny[i], 0, 1);
            w.pwm = clampd(o.pwm + pwmO[i], 0.02, 0.98);
            w.phase = std::fmod(std::fmod(o.phase + phO[i], 1.0) + 1.0, 1.0);
            w.se = clampd(o.startEnd + seO[i], 0.02, 1);
            w.seg = segO[i] != 0 ? clampd(o.segment + segO[i], 0.05, 1) : o.segment;
        }
        if (tuneRoute || wtTuneMod_) {
            wtTuneMod_ = true;
            for (int i = 0; i < 2; i++) {
                const OscCfg& o = WL.oscs[i];
                wtOct_[i] = (int)clampd(jsRound(o.oct + octO[i]), -2, 2); wtSemi_[i] = (int)clampd(jsRound(o.semitone + semiO[i]), -12, 12); wtFine_[i] = clampd(o.fineTune + fineO[i], -100, 100);
            }
        }
    }
    // 4. FM engine tuning + operator ratios
    {
        const LayerCfg& FL = P.layers[L_FM];
        bool tuneRoute = false; for (auto& r : routes_) if (r.dest >= D_FM_AOCT && r.dest <= D_FM_BFINE) tuneRoute = true;
        if (tuneRoute || fmTuneMod_) {
            fmTuneMod_ = true;
            const double oct[2] = {acc[D_FM_AOCT], acc[D_FM_BOCT]}, semi[2] = {acc[D_FM_ASEMI], acc[D_FM_BSEMI]}, fine[2] = {acc[D_FM_AFINE], acc[D_FM_BFINE]};
            for (int e = 0; e < 2; e++) {
                fmOct_[e] = (int)clampd(jsRound(FL.engineOctave[e] + oct[e]), -4, 4); fmSemi_[e] = (int)clampd(jsRound(FL.engineSemitone[e] + semi[e]), -12, 12); fmFine_[e] = clampd(FL.engineFine[e] + fine[e], -100, 100);
            }
        }
        for (int i = 0; i < 8; i++) if (fmRatioRoute_[i] && (size_t)i < FL.oscs.size()) fmRatioLive_[i] = clampd(FL.oscs[i].ratio + acc[D_FM_OPR1 + i], 0.5, 12);
    }
    // 4b. FM 6-OP (DX7) matrix destinations: the same offset goes onto every operator's patch byte (web: base = the patch value when modulation first
    // reaches it; offset 0 restores it), then the changed patch is pushed to every sounding DX7 voice and used by new notes
    {
        const LayerCfg& FL = P.layers[L_FM];
        bool route = false; for (auto& r : routes_) if (r.dest >= D_DX_FINE && r.dest <= D_DX_L2) route = true;
        if (FL.sixOp && FL.hasDx7Patch && (route || dx7ModOn_)) {
            uint8_t np[dx7::PATCH_SIZE]; std::memcpy(np, FL.dx7Patch, sizeof(np));
            struct F { int dest, byte, lo, hi; };
            static const F fields[] = { {D_DX_FINE, 19, 0, 99}, {D_DX_DETUNE, 20, 0, 14}, {D_DX_LEVEL, 16, 0, 99}, {D_DX_R2, 1, 0, 99}, {D_DX_R3, 2, 0, 99}, {D_DX_R4, 3, 0, 99}, {D_DX_L1, 4, 0, 99}, {D_DX_L2, 5, 0, 99} };
            for (const F& f : fields) for (int op = 1; op <= 6; op++) { const int idx = (6 - op) * 21 + f.byte; np[idx] = (uint8_t)clampd(jsRound((double)FL.dx7Patch[idx] + acc[f.dest]), f.lo, f.hi); }
            np[140] = (uint8_t)clampd(jsRound((double)FL.dx7Patch[140] + acc[D_DX_AMD]), 0, 99);
            dx7ModOn_ = true;
            if (std::memcmp(np, dx7Live_, sizeof(np)) != 0 || !dx7LiveValid_) {
                std::memcpy(dx7Live_, np, sizeof(np)); dx7LiveValid_ = true;
                for (auto& v : voices_) if (v.alive && v.dx.on && v.dx.started) v.dx.v.updatePatch(dx7Live_);
            }
        } else if (!(FL.sixOp && FL.hasDx7Patch)) { dx7ModOn_ = false; dx7LiveValid_ = false; }
    }
    // 5. master-level scalars
    if (acc[D_WTFM] != 0 || wtFmModOn_) { wtFmModOn_ = acc[D_WTFM] != 0; applyWtFmMix(clampd(wtFmMix + acc[D_WTFM], 0, 1)); }
    toneOff_ = acc[D_TONE];
    if (acc[D_SMPMIX] != 0 || smpMixModOn_) { smpMixModOn_ = acc[D_SMPMIX] != 0; setSampleMix(sampleMixOn, clampd(sampleMixPos + acc[D_SMPMIX] * 15, 0, 15)); sampleMixPos = sampleMixPos; }
    genApply(acc); vocApply(acc);
    // 6. FX (the browser's FX worklets pick the new values up a couple of render quanta late - see fxParamLatency)
    if (fxParamLatency <= 0) modFx(acc); else { for (int i = 0; i < D_COUNT; i++) fxAccPending_[i] = acc[i]; fxPending_ = true; fxDue_ = clock_ + (uint64_t)fxParamLatency; }
    // 7. voices
    for (auto& v : voices_) if (v.alive && !v.released) modVoice(v, T);   // noteOff removes the voice from the active list: its mod inputs freeze
}

// ---- generic layer-parameter modulation (GEN:*): base value from the preset, offset from the matrix, written into the working copy genL_ (used by new notes)
inline double genValue(double mn, double mx, double step, bool ex, double base, double off) {
    double v = ex ? base * std::pow(2.0, off * 3) : base + off * (mx - mn) * (mn < 0 ? 0.5 : 1);
    v = clampd(v, mn, mx); if (step >= 1) v = jsRound(v / step) * step; return v;
}
inline void Synth::vocApply(const double* acc) {
    bool any = false; for (int i = 0; i < kNumVoc; i++) any = any || vocHas_[i];
    if (!any) { if (vocOn_) { vocOn_ = false; rebuildVocoder(); } return; }
    vocOn_ = true; if (!preset_) return;
    HvParams p = voc_.P; const Json& v = preset_->vocoder;
    for (int i = 0; i < kNumVoc; i++) {
        if (!vocHas_[i]) continue; const VocDef& d = kVocDefs[i];
        const double val = genValue(d.mn, d.mx, d.step, d.exp, v.num(d.key, d.dflt), acc[D_VOC0 + i]);
        switch (i) { case 0: p.mix = val; break; case 1: p.formantSt = val; break; case 2: p.q = val; break; case 3: p.releaseMs = val; break; case 4: p.hpf = val; break;
                     case 5: p.micGainDb = val; break; case 6: p.noiseDb = val; break; case 7: p.sibSens = val; break; case 8: p.pitchSpeedMs = val; break; case 9: p.chorusRate = val; break; }
    }
    voc_.setParams(p);
}
inline void Synth::genApply(const double* acc) {
    for (int i = 0; i < kNumGen; i++) {
        if (!genHas_[i]) continue; const GenDef& d = kGenDefs[i]; const double off = acc[D_GEN0 + i];
        const LayerCfg& B = preset_->layers[d.layer]; LayerCfg& T = genL_[d.layer];
        auto gv = [&](double base) { return genValue(d.mn, d.mx, d.step, d.exp, base, off); };
        if (d.field == GF_EQFREQ || d.field == GF_EQGAIN) {   // the layer EQ is a live node, not read at note-on: push the modulated value to its smoothed target
            const double v = gv(d.field == GF_EQFREQ ? B.eq[d.band].freq : B.eq[d.band].gain);
            if (d.field == GF_EQFREQ) eqFreq_[d.layer][d.band].setTarget(clampd(v, 20, 18000), sr_, 0.01); else eqGain_[d.layer][d.band].setTarget(clampd(v, -15, 15), sr_, 0.01);
            continue;
        }
        if (d.field >= GF_ATTACK && d.field <= GF_RELEASE) {
            auto setE = [&](const EnvCfg& b, EnvCfg& t) { const double v = gv(d.field == GF_ATTACK ? b.a : d.field == GF_DECAY ? b.d : d.field == GF_SUSTAIN ? b.s : b.r);
                (d.field == GF_ATTACK ? t.a : d.field == GF_DECAY ? t.d : d.field == GF_SUSTAIN ? t.s : t.r) = v; };
            if (d.sub == 1) { setE(B.env2, T.env2); T.hasEnv2 = true; continue; }   // OSC2 / Engine B envelope (copy of env when the preset has none, as the web does)
            if (d.layer != L_SID) setE(B.env, T.env);
            if (d.layer == L_SMP) { setE(B.globalAdsr, T.globalAdsr); for (size_t o = 0; o < B.oscs.size() && o < T.oscs.size(); o++) if (B.oscs[o].hasEnv) setE(B.oscs[o].env, T.oscs[o].env); }
            if (d.layer == L_SID) for (size_t o = 0; o < B.oscs.size() && o < T.oscs.size(); o++) if (B.oscs[o].hasEnv) setE(B.oscs[o].env, T.oscs[o].env);
            continue;
        }
        for (size_t o = 0; o < B.oscs.size() && o < T.oscs.size(); o++) {
            const OscCfg& b = B.oscs[o]; OscCfg& t = T.oscs[o];
            switch (d.field) {
                case GF_PAN: t.pan = gv(b.pan); break; case GF_WIDTH: t.width = gv(b.width); break; case GF_DETUNE: t.detune = gv(b.detune); break;
                case GF_DRIFT: t.drift = gv(b.drift); break; case GF_LEVEL: t.level = gv(b.level); break;
                case GF_OCT: t.oct = (int)gv(b.oct); break; case GF_SEMI: t.semitone = (int)gv(b.semitone); break; case GF_FINE: t.fineTune = gv(b.fineTune); break;
                case GF_MICMIX: t.micMix = gv(b.micMix); break; case GF_SPKMIX: t.spkMix = gv(b.spkMix); break;
                case GF_UBLEND: t.unisonBlend = gv(b.unisonBlend); break; case GF_UPHASE: t.unisonPhase = gv(b.unisonPhase); break;
                case GF_VIB: t.vibratoDepth = gv(b.vibratoDepth); break;
                case GF_FLUTE: t.flute[d.band] = gv(b.flute[d.band]); break;
            }
        }
    }
}

// ---- FX parameter modulation (direct-offset model: base value from the preset + offset, recomputed every tick)
inline void Synth::modFx(const double* acc) {
    struct M { int dest; const char* type; const char* key; double mul, lo, hi; };
    static const M tab[] = {
        {D_FX_REVMIX, "reverb", "mix", 0.08, 0, 1}, {D_FX_SATDRIVE, "saturation", "drive", 0.08, 0, 1}, {D_FX_SATMIX, "saturation", "mix", 0.08, 0, 1},
        {D_FX_FLGRATE, "flanger", "rate", 0.4, 0.05, 5}, {D_FX_FLGMIX, "flanger", "mix", 0.08, 0, 1}, {D_FX_FLGFB, "flanger", "feedback", 0.08, 0, 0.95},
        {D_FX_PHRATE, "phaser", "rate", 0.4, 0.05, 5}, {D_FX_PHMIX, "phaser", "mix", 0.08, 0, 1}, {D_FX_PHFB, "phaser", "feedback", 0.08, 0, 0.95},
        {D_FX_FOLDDRIVE, "wavefolder", "drive", 9.5, 1, 20}, {D_FX_FOLDSYM, "wavefolder", "symmetry", 1, -1, 1}, {D_FX_FOLDMIX, "wavefolder", "mix", 0.5, 0, 1}, {D_FX_FOLDPWM, "wavefolder", "pwm", 0.48, 0.02, 0.98},
        {D_FX_SPECBLUR, "spectral", "blur", 0.08, 0, 1}, {D_FX_SPECSHIFT, "spectral", "shift", 3, -200, 200}, {D_FX_SPECTHR, "spectral", "threshold", 0.08, 0, 1}, {D_FX_SPECSPREAD, "spectral", "spread", 0.08, 0, 1}, {D_FX_SPECMIX, "spectral", "mix", 0.08, 0, 1},
        {D_FX_FILTCUT, "filter", "cutoff", 1, 20, 18000}, {D_FX_FILTRESO, "filter", "resonance", 1, 0, 130},
        {D_FX_GRSIZE, "granular", "grainSize", 1, 5, 500}, {D_FX_GRPITCH, "granular", "pitch", 1, -24, 24}, {D_FX_GRPAN, "granular", "panCenter", 1, -1, 1}, {D_FX_GRDENS, "granular", "density", 1, 0.5, 100},
        {D_FX_DELFB, "delay", "feedback", 0.08, 0, 0.9}, {D_FX_DELMIX, "delay", "mix", 0.08, 0, 1},
        {D_FX_RESFREQ, "resonator", "freq", 40, 40, 2000}, {D_FX_RESRES, "resonator", "resonance", 2, 1, 40}, {D_FX_RESMIX, "resonator", "mix", 0.08, 0, 1},
    };
    auto numPtr = [](Json& j, const char* k) -> Json* { for (auto& kv : j.o) if (kv.first == k) return kv.second.type == Json::Num ? &kv.second : nullptr; return nullptr; };
    for (size_t s = 0; s < preset_->fx.size() && s < fxScratch_.size(); s++) {
        const FxStageCfg& st = preset_->fx[s]; Json& sc = fxScratch_[s]; bool touched = false;
        for (const M& m : tab) {
            if (st.type != m.type) continue;
            const double off = acc[m.dest]; if (off == 0) continue;
            Json* dst = numPtr(sc, m.key); const double base = st.params.num(m.key, NAN); if (!dst || std::isnan(base)) continue;
            // granular / filter / wavefolder apply all of their dials as a group in the web app; the rest only the destination that moved
            dst->n = clampd(base + off * m.mul, m.lo, m.hi); touched = true;
        }
        for (int i = 0; i < kNumFxp; i++) {   // generic FXP:<type>:<key> destinations (every stage of that type)
            const FxpDef& d = kFxpDefs[i]; if (st.type != d.type) continue;
            const double off = acc[D_FXP0 + i]; if (off == 0) continue;
            Json* dst = numPtr(sc, d.key); const double base = st.params.num(d.key, NAN); if (!dst || std::isnan(base)) continue;
            dst->n = genValue(d.mn, d.mx, d.step, d.exp, base, off); touched = true;
        }
        if (st.type == "glitch") {
            if (acc[D_FX_STUTDIV] != 0) { static const double opts[4] = {1, 0.5, 0.25, 0.125}; const int idx = (int)clampd(jsRound((acc[D_FX_STUTDIV] * 2 + 0.5) * 3), 0, 3); if (Json* d = numPtr(sc, "stutterDivision")) { d->n = opts[idx]; touched = true; } }
            if (acc[D_FX_BITDEPTH] != 0) { const double base = st.params.num("bitDepth", NAN); if (Json* d = numPtr(sc, "bitDepth")) if (!std::isnan(base)) { d->n = clampd(jsRound(base + acc[D_FX_BITDEPTH] * 4), 1, 16); touched = true; } }
        }
        if (touched || fxModded_[s]) {
            if (!touched) sc = st.params;   // all offsets back to zero: restore the dials' own values once
            fx_.setStageParams((int)s, sc);   // DELAYHOOK
            fxModded_[s] = touched ? 1 : 0;
        }
    }
}

// ---- per-voice modulation inputs (cutoff / reso / level / filterVolume / pitch / procedural dials) and live parameter pushes
inline void Synth::modVoice(Voice& v, double T) {
    const LayerCfg& L = preset_->layers[v.layer];
    double cut[kMaxGroups], res[kMaxGroups]; for (int i = 0; i < kMaxGroups; i++) cut[i] = res[i] = 0;
    double levelOff = 0, pitchAll = 0, fvolOff = 0, gen[8] = {0, 0, 0, 0, 0, 0, 0, 0}; double bleedOff[16] = {0}; double pwOff[3] = {0, 0, 0}; bool pwRoute[3] = {false, false, false};
    for (auto& r : routes_) {
        if (r.dest < D_V_CUTOFF || r.layer != v.layer) continue;
        const double x = modSource(r.src, &v, T) * r.amt * r.scale;
        switch (r.dest) {
            case D_V_CUTOFF: if (r.g < kMaxGroups) cut[r.g] += x; break;
            case D_V_RESO: if (r.g < kMaxGroups) res[r.g] += x; break;
            case D_V_LEVEL: levelOff += x; break; case D_V_PITCH: pitchAll += x; break; case D_V_FVOL: fvolOff += x; break;
            case D_V_GEN: gen[r.g] += x / r.scale; break;
            case D_V_BLEED: if (r.g < 16) bleedOff[r.g] += x; break;
            case D_V_PWM: if (r.g < 3) { pwOff[r.g] += x; pwRoute[r.g] = true; } break;
        }
    }
    if (L.type == LayerType::SID && v.sid.on) for (int k = 0; k < 3 && k < (int)L.oscs.size(); k++) if (pwRoute[k]) {   // web: pulseWidth = base + offset, pushed with setTargetAtTime(tau 10 ms) every tick
        if (!v.sid.pwMod[k]) { v.sid.pwMod[k] = true; v.sid.pwM[k].snap(v.sid.p[k].pw); }
        v.sid.pwM[k].set(clampd(L.oscs[k].pulseWidth + pwOff[k], 0.01, 0.99), T, 0.01); }
    if (L.type == LayerType::SID && v.sid.on) {   // web tick: every SID voice dial follows live on a held note (level, pitch, waveform, ring/sync, ADSR)
        SidEng& s = v.sid;
        static const struct { const char* n; int m; } wm[] = {{"triangle", 1}, {"sawtooth", 2}, {"pulse", 4}, {"noise", 8}, {"tri_saw", 3}, {"tri_pulse", 5}, {"saw_pulse", 6}};
        for (int k = 0; k < 3 && k < (int)L.oscs.size(); k++) {
            const OscCfg& so = L.oscs[k]; SidVoiceParams& p = s.p[k];
            s.levelT[k] = F32(so.level);
            s.freq[k] = F32(s.fb * std::pow(2.0, so.oct) * std::pow(2.0, (so.semitone * 100 + so.fineTune) / 1200.0));
            int wv = 1; for (auto& e : wm) if (so.sidWave == e.n) wv = e.m;
            p.wave = wv; p.ring = so.ringModOn; p.sync = so.syncOn;
            const EnvCfg& se = so.hasEnv ? so.env : EnvCfg{0.01, 0.15, 0.65, 0.2};
            p.atk = std::max(0.001, se.a); p.dec = std::max(0.001, se.d); p.sus = clampd(se.s, 0, 1); p.rel = std::max(0.001, se.r);
            if (!s.pwMod[k]) p.pw = clampd(so.pulseWidth, 0.01, 0.99);
        }
    }
    if (L.type == LayerType::SID && v.sid.on) {   // SID "wavetable" arp: web stepWtArpVoice / applyWtArpStep / restoreFromWtArp
        SidEng& s = v.sid; const WtArpCfg& arp = L.wtArp; const double dtA = s.lastT < 0 ? 0 : std::max(0.0, T - s.lastT); s.lastT = T;
        static const struct { const char* n; int m; } wm[] = {{"triangle", 1}, {"sawtooth", 2}, {"pulse", 4}, {"noise", 8}, {"tri_saw", 3}, {"tri_pulse", 5}, {"saw_pulse", 6}};
        auto waveMask = [&](const std::string& w, int def) { for (auto& e : wm) if (w == e.n) return e.m; return def; };
        auto voiceFreq = [&](int k, double noteNum) { const OscCfg& o = L.oscs[k]; return F32(midiToFreq(noteNum, preset_->a4) * std::pow(2.0, o.oct) * std::pow(2.0, (o.semitone * 100 + o.fineTune) / 1200.0)); };
        for (int k = 0; k < 3; k++) if (s.gateHigh[k] >= 0 && T >= s.gateHigh[k]) { s.p[k].gate = true; s.gateHigh[k] = -1; }   // hard-restart: gate back high 1 ms after the low
        if (arp.on && !arp.steps.empty()) {
            const double stepDur = 1.0 / clampd(arp.speedHz, 25, 240);
            auto lenOf = [&](const WtArpStep& st) { if (st.tempoSync) return 1.0 / clampd(1.0 / ((60.0 / preset_->bpm) * st.syncDivision), 0.01, 1000); return stepDur * std::max(1, st.lengthTicks); };
            auto apply = [&](const WtArpStep& st) {
                for (int k = 0; k < 3 && k < (int)L.oscs.size(); k++) {
                    const OscCfg& o = L.oscs[k]; if (!o.arpOn) continue;
                    const double tn = st.mode == "fixed" ? st.value : (v.note + st.value);
                    s.freq[k] = voiceFreq(k, tn); s.p[k].wave = waveMask(st.wave, waveMask(o.sidWave, 1)); s.p[k].ring = st.ringMod; s.p[k].sync = st.sync;
                    if (!st.gate) { s.p[k].gate = false; s.gateHigh[k] = -1; }
                    else if (arp.hardRestart) { s.p[k].gate = false; s.gateHigh[k] = T + 0.001; }
                    else { s.p[k].gate = true; s.gateHigh[k] = -1; }
                }
            };
            if (s.arpIdx == -2) s.arpIdx = -1;   // first tick lands on step 0
            s.arpPhase += dtA; s.arpRestore = true;
            for (int guard = 0; guard < 64; guard++) {
                const WtArpStep& cur = s.arpIdx >= 0 ? arp.steps[(size_t)s.arpIdx % arp.steps.size()] : arp.steps[0];
                const double curDur = lenOf(cur); if (s.arpPhase < curDur) break;
                s.arpPhase -= curDur; s.arpIdx = (int)(((size_t)(s.arpIdx + 1)) % arp.steps.size()); apply(arp.steps[(size_t)s.arpIdx]);
            }
        } else if (s.arpRestore) {   // arp switched off while held: put every arp voice back on its own note-on values
            for (int k = 0; k < 3 && k < (int)L.oscs.size(); k++) { const OscCfg& o = L.oscs[k]; s.freq[k] = voiceFreq(k, v.note); s.p[k].wave = waveMask(o.sidWave, 1); s.p[k].ring = o.ringModOn; s.p[k].sync = o.syncOn; s.p[k].gate = o.enabled; s.gateHigh[k] = -1; }
            s.arpRestore = false; s.arpIdx = -2; s.arpPhase = 0;
        }
    }
    if (L.type == LayerType::Sample) for (int si = 0; si < 16; si++) { SmpSlot& q = v.smp[si]; if (q.on && q.bleedOn) q.bleedM.set(clampd(L.oscs[si].bleed + bleedOff[si], 0, 1), T, 0.02); }
    for (int gi = 0; gi < v.ng; gi++) {
        Group& g = v.g[gi]; if (!g.active) continue;
        g.cutM.set(cut[gi], T, 0.02); g.resoM.set(res[gi], T, 0.02); g.levelM.set(levelOff, T, 0.02); g.fvolM.set(fvolOff, T, 0.02);
    }
    // pitch: matrix offset + drift + bend + live retune of the oscillator's Octave/Semitone/Fine
    const double bendNorm = (mpeEnabled && mpeExpr_[v.note & 127].has) ? mpeExpr_[v.note & 127].pitchBend : pitchBend;
    auto pitchTarget = [&](PitchOffset& p, int bendIdx, bool wtRetune, double extraRetune = 0) {
        if (p.drift > 0) { p.driftVal += (rng_.next() - 0.5) * p.drift * 0.3; p.driftVal = clampd(p.driftVal, -30, 30); }
        double bend = 0;
        const double glideSemis = mpeEnabled ? mpeExpr_[v.note & 127].glide : 0.0;
        if ((bendNorm != 0 && (bendIdx < 0 || bendIdx >= 16 || preset_->bendTargets[v.layer][bendIdx])) || glideSemis != 0) { const double c = ((bendIdx < 0 || bendIdx >= 16 || preset_->bendTargets[v.layer][bendIdx]) ? bendNorm * bendRange * 100.0 : 0.0) + glideSemis * 100.0; /* the drag glide moves every oscillator, bend targets only gate the wheel */ bend = p.kind == 1 ? c : p.built * (std::pow(2.0, c / 1200.0) - 1.0); }
        double retune = 0;
        if (wtRetune && wtTuneMod_ && bendIdx >= 0 && bendIdx < 2) {
            const OscCfg& o = L.oscs[bendIdx];
            const double f = std::pow(2.0, wtOct_[bendIdx] - o.oct) * std::pow(2.0, ((wtSemi_[bendIdx] - o.semitone) * 100 + (wtFine_[bendIdx] - o.fineTune)) / 1200.0);
            retune = p.built * (f - 1);
        }
        const double total = pitchAll + p.driftVal + bend + retune + extraRetune;
        p.lm.set(p.kind == 2 ? total / 1000.0 : total, T, 0.02);
    };
    if (L.type == LayerType::Wavetable) {
        for (int oi = 0; oi < 2; oi++) {
            WtOsc& w = v.wt[oi]; if (!w.on) continue;
            for (int u = 0; u < w.n; u++) {
                double gr = 0;
                if (genOn_[L_WT] && (size_t)oi < genL_[L_WT].oscs.size()) {   // GEN:WT.octave/semi/fine/detune: the web re-derives a held unison voice's pitch from the oscillator's current values
                    const OscCfg& eo = genL_[L_WT].oscs[oi]; const int n = w.n;
                    auto sp = [&](double d) { return n == 1 ? 0.0 : (-d / 2) + (d * u / (n - 1)); };
                    const double f = std::pow(2.0, eo.oct - w.bOct) * std::pow(2.0, ((eo.semitone - w.bSemi) * 100 + (eo.fineTune - w.bFine) + (w.sync ? 0.0 : sp(eo.detune) - sp(w.bDetune))) / 1200.0);
                    gr = w.pm[u].built * (f - 1);
                }
                pitchTarget(w.pm[u], oi, true, gr);
            }
            if (w.n > 1 && !w.sync && (size_t)oi < effL(L_WT).oscs.size()) {   // Stack & Spread follows the live Width / Blend / Phase (web re-applies them to every held unison voice each tick)
                const OscCfg& eo = effL(L_WT).oscs[oi]; const int n = w.n;
                const double bl = clampd(eo.unisonBlend, 0, 1), ps = clampd(eo.unisonPhase, 0, 1), ci = (n - 1) / 2.0;
                w.uLive = true;
                for (int u = 0; u < n; u++) {
                    const double d = std::fabs(u - ci) / ci; w.tW[u] = std::max(0.0, 1 - bl * d);
                    double gl, gr; monoPanGains(clampd((-1 + 2.0 * u / (n - 1)) * eo.width, -1, 1), gl, gr); w.tPL[u] = gl; w.tPR[u] = gr;
                    w.tPh[u] = (-ps / 2) + (ps * u / (n - 1));
                }
            }
            if (wtLive_[oi].mod) { const WtLive& lv = wtLive_[oi]; if (!w.modded) { w.modded = true; w.ms[0].snap(w.pos); w.ms[1].snap(w.pwm); w.ms[2].snap(w.phase); w.ms[3].snap(w.startEnd); w.ms[4].snap(w.segment); } w.ms[0].set(lv.pos, T, 0.01); w.ms[1].set(lv.pwm, T, 0.01); w.ms[2].set(lv.phase, T, 0.01); w.ms[3].set(lv.se, T, 0.01); w.ms[4].set(lv.seg, T, 0.01); }
        }
    } else if (L.type == LayerType::FM) {
        for (int ei = 0; ei < 2; ei++) {
            FmEng& f = v.fm[ei]; if (!f.on) continue;
            for (int k = 0; k < 4; k++) pitchTarget(f.pc[k], ei, false);
            const int base = f.base;
            if (fmFbRoute_[ei] || fmRatioRoute_[base] || fmRatioRoute_[base + 1] || fmRatioRoute_[base + 2] || fmRatioRoute_[base + 3]) {
                if (!f.modded) { f.modded = true; for (int k = 0; k < 4; k++) f.ratioM[k].snap(f.ratio[k]); f.fbM.snap(f.fb0); }
                double fbOff = 0; for (auto& r : routes_) if (r.dest == (ei == 0 ? D_FM_FB_A : D_FM_FB_B)) fbOff += modSource(r.src, &v, T) * r.amt;
                f.fbM.set(clampd(L.oscs[base].feedback + fbOff, 0, 1), T, 0.02);
                for (int k = 0; k < 4; k++) f.ratioM[k].set(fmRatioRoute_[base + k] ? fmRatioLive_[base + k] : L.oscs[base + k].ratio, T, 0.02);
            }
            if (fmTuneMod_) {
                const double tuned = f.freqBase * std::pow(2.0, fmOct_[ei]) * std::pow(2.0, (fmSemi_[ei] * 100 + fmFine_[ei]) / 1200.0);
                f.baseFreq.setTargetAtTime(tuned, T, 0.02);
                if (f.sub) { if (!f.subMod) { f.subMod = true; f.subM.snap(f.subFreq); } f.subM.set(tuned, T, 0.02); }
            }
        }
    } else if (L.type == LayerType::Procedural) {
        for (int pi = 0; pi < 2; pi++) {
            ProGen& p = v.pro[pi]; if (!p.on) continue;
            double proRetune = 0;   // GEN:PRO.octave/semi/fine: the web retunes held procedural notes live (relative to the tuning they were built with)
            if (genOn_[L_PRO] && (size_t)pi < effL(L_PRO).oscs.size()) { const OscCfg& eo = effL(L_PRO).oscs[pi]; const double f = std::pow(2.0, eo.oct - p.bOct) * std::pow(2.0, ((eo.semitone - p.bSemi) * 100 + (eo.fineTune - p.bFine)) / 1200.0); proRetune = p.pm.built * (f - 1); }
            pitchTarget(p.pm, pi, false, proRetune);
            if (p.isFlute && (size_t)pi < effL(L_PRO).oscs.size()) { const OscCfg& eo = effL(L_PRO).oscs[pi]; p.fl.setInstrument(eo.fluteInst); for (int k = 0; k < flute::kNumParams; k++) p.fl.setParam(k, eo.flute[k]); }   // flute: UI edits and GEN:PRO.fl_* modulation are live on held / latched notes (the core smooths targets)
            if ((size_t)pi < effL(L_PRO).oscs.size()) { const OscCfg& eo = effL(L_PRO).oscs[pi]; p.lvTgt = F32(eo.level); double gl, gr; monoPanGains(clampd(eo.pan, -1, 1), gl, gr); p.monoTgt = 0.5 * (gl + gr); }
            if (genRoute_) {
                const OscCfg& o = L.oscs[pi]; const int b = pi * 4;
                if (!p.modded) { p.modded = true; p.gm[0].snap(p.pr.bright); p.gm[1].snap(p.pr.damp); p.gm[2].snap(p.pr.body); p.gm[3].snap(p.pr.chaos); }
                p.gm[0].set(clampd(o.bright + gen[b] * 0.5, 0, 1), T, 0.02); p.gm[1].set(clampd(o.damp + gen[b + 1] * 0.5, 0, 1), T, 0.02);
                p.gm[2].set(clampd(o.body + gen[b + 2] * 0.5, 0, 1), T, 0.02); p.gm[3].set(clampd(o.chaos + gen[b + 3] * 0.5, 0, 1), T, 0.02);
            }
        }
    } else if (L.type == LayerType::Sample) {
        const LayerCfg& EL = effL(v.layer);
        for (int si = 0; si < 16; si++) { SmpSlot& q = v.smp[si]; if (!q.on) continue; pitchTarget(q.pm, si, false);
            if (genOn_[v.layer] && (size_t)si < EL.oscs.size()) { if (q.col.hasFx(0)) q.col.setMix(0, EL.oscs[si].micMix); if (q.col.hasFx(1)) q.col.setMix(1, EL.oscs[si].spkMix); } }
    }
}

// A voice built mid-modulation starts from the *current* live oscillator values (the web app builds notes from the mutated state objects).
inline void Synth::modInitVoice(Voice& v, double t) {
    const LayerCfg& L = preset_->layers[v.layer];
    v.rndSet = false; v.fmRetuned = false;
    if (L.type == LayerType::Wavetable) {
        for (int oi = 0; oi < 2; oi++) {
            WtOsc& w = v.wt[oi]; w.modded = false; if (!w.on) continue;
            if (wtLive_[oi].mod) {
                const WtLive& lv = wtLive_[oi]; w.modded = true;
                w.ms[0].snap(oi == 0 && w.pos != L.oscs[0].position ? w.pos : lv.pos);   // a p-locked position survives until the next tick
                w.ms[1].snap(lv.pwm); w.ms[2].snap(lv.phase); w.ms[3].snap(lv.se); w.ms[4].snap(lv.seg);
            }
            if (wtTuneMod_) {   // the note is built at the live (modulated) tuning
                const OscCfg& o = L.oscs[oi];
                const double f = std::pow(2.0, wtOct_[oi] - o.oct) * std::pow(2.0, ((wtSemi_[oi] - o.semitone) * 100 + (wtFine_[oi] - o.fineTune)) / 1200.0);
                for (int u = 0; u < w.n; u++) w.pm[u].lm.snap(w.pm[u].built * (f - 1));
            }
        }
    } else if (L.type == LayerType::FM) {
        for (int ei = 0; ei < 2; ei++) {
            FmEng& f = v.fm[ei]; f.modded = false; f.subMod = false; if (!f.on) continue;
            if (fmFbRoute_[ei]) {   // computeModulatedFmFeedback() at note build time
                double fbOff = 0; for (auto& r : routes_) if (r.dest == (ei == 0 ? D_FM_FB_A : D_FM_FB_B)) fbOff += modSource(r.src, &v, t) * r.amt;
                f.fb0 = (double)(float)clampd(L.oscs[f.base].feedback + fbOff, 0, 1);
            }
            if (fmTuneMod_ && !(L.engineGlide[ei] > 0)) {
                const double tuned = f.freqBase * std::pow(2.0, fmOct_[ei]) * std::pow(2.0, (fmSemi_[ei] * 100 + fmFine_[ei]) / 1200.0);
                f.baseFreq.clear(); f.baseFreq.setValueAtTime(tuned, t); f.tunedFreq = tuned; f.subFreq = f.sub ? tuned : f.subFreq;
            }
        }
    } else if (L.type == LayerType::Procedural) {
        for (int pi = 0; pi < 2; pi++) v.pro[pi].modded = false;
    }
}

}  // namespace vv
