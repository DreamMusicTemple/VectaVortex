#include "vv_engine.h"
#include <cstdio>
#include "vv/engine.hpp"
#include "vv/vektur_void.hpp"
#include "vv/chord_analyzer.hpp"
#include "vv/wt_tools.hpp"
#include "vv/sample_tools.hpp"
#include "vv/daw_recorder.hpp"
#include "vv/roll_rec.hpp"
#include <string>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <atomic>

// Sample recorder (web 'RECORD' tool). The buffers are allocated on a non-audio thread by vv_engine_rec_prepare; the audio thread only copies
// into them (no allocation, locks or logging) and publishes progress through atomics.
struct VVRec {
    std::vector<float> L, R; std::atomic<int> state{0} /*0 idle 1 armed 2 recording 3 finished*/, pos{0}; std::atomic<float> peak{0.f};
    std::atomic<int> source{0} /*0 master out, 1 external input*/, arm{0}; std::atomic<float> threshold{0.02f};
    std::vector<float> inL, inR; int inFrames = 0;   // last block handed to vv_engine_feed_input (audio thread only)
};
struct VVEngine { vv::Engine impl; vv::ChordAnalyzer chord; VVRec rec; vv::DawRecorder daw; vv::Metronome click; vv::rollrec::Recorder rollRec; };
struct VVAudio { vv::stools::Audio a; };
struct VVVoid { vv::vvoid::Game g; vv::vvoid::Lcg rnd; explicit VVVoid(uint32_t seed) : rnd(seed) {} };

extern "C" {

// ---- Vektur Void
VVVoid* vv_void_create(uint32_t seed) { return new VVVoid(seed ? seed : 1u); }
void vv_void_destroy(VVVoid* g) { delete g; }
void vv_void_set_god(VVVoid* g, int on) { if (g) g->g.god = on != 0; }
void vv_void_step(VVVoid* g, double dt, double dx, double dy, int plasma, int bomb, int start) {
    if (!g) return; vv::vvoid::Input in; in.dx = dx; in.dy = dy; in.plasma = plasma != 0; in.bomb = bomb != 0; in.start = start != 0;
    vv::vvoid::step(g->g, in, dt, [g] { return g->rnd(); });
}
int vv_void_banner(VVVoid* g, char* out, int cap) {
    if (!g || !out || cap <= 0) return 0; const std::string& b = g->g.banner; const int n = (int)std::min<size_t>(b.size(), (size_t)cap - 1);
    std::memcpy(out, b.data(), (size_t)n); out[n] = 0; return n;
}
int vv_void_snapshot(VVVoid* gp, float* out, int cap) {
    using namespace vv::vvoid; if (!gp || !out) return -1; const Game& G = gp->g;
    const size_t need = 30 + 9 + G.bullets.size() * 4 + G.bombs.size() * 3 + G.blasts.size() * 4 + G.enemies.size() * 6 + G.ebul.size() * 4 + G.pick.size() * 3 + G.parts.size() * 4 + G.beams.size() * 2 + G.obstacles.size() * 16;
    if ((long)need > cap) return -1; int n = 0; auto put = [&](double v) { out[n++] = (float)v; };
    const Boss& b = G.boss; const bool hb = G.hasBoss;
    put(G.mode); put(G.t); put(G.modeT); put((double)G.score); put(G.level); put(G.spares); put(G.continues); put(G.wLvl); put(G.ship.x); put(G.ship.y); put(G.ship.alive ? 1 : 0);
    put(G.ship.inv); put(G.ship.shield); put(G.shake); put(G.bannerT); put(G.contT); put(G.typed);
    put(hb ? 1 : 0); put(hb ? b.x : 0); put(hb ? b.y : 0); put(hb ? b.w : 0); put(hb ? b.h : 0); put(hb ? b.hits : 0); put(hb ? b.maxHits : 1); put(hb ? b.flash : 0); put(hb ? b.dying : 0); put(hb ? b.t : 0);
    put(hb && b.final_ ? 1 : 0); put(hb ? b.core : 0); put(hb ? bossTint(b) : 0);
    put((double)G.bullets.size()); put((double)G.bombs.size()); put((double)G.blasts.size()); put((double)G.enemies.size()); put((double)G.ebul.size()); put((double)G.pick.size());
    put((double)G.parts.size()); put((double)G.beams.size()); put((double)G.obstacles.size());
    for (auto& e : G.bullets) { put(e.x); put(e.y); put(e.w); put(e.h); }
    for (auto& e : G.bombs) { put(e.x); put(e.y); put(e.r); }
    for (auto& e : G.blasts) { put(e.x); put(e.y); put(e.r); put(e.t); }
    for (auto& e : G.enemies) { put(e.type); put(e.x); put(e.y); put(e.r); put(e.t); put(e.flash); }
    for (auto& e : G.ebul) { put(e.x); put(e.y); put(e.r); put(e.col); }
    for (auto& e : G.pick) { put(e.kind == 'W' ? 0 : 1); put(e.x); put(e.y); }
    for (auto& e : G.parts) { put(e.x); put(e.y); put(e.life); put(e.t); }
    for (auto& e : G.beams) { put(e.y); put(e.t); }
    for (auto& o : G.obstacles) { put(o.kind); put(o.x); put(o.width); put(o.nparts); for (int i = 0; i < o.nparts; i++) { put(o.parts[i].x); put(o.parts[i].y); put(o.parts[i].w); put(o.parts[i].h); } }
    return n;
}
VVEngine* vv_engine_create(double sr) { VVEngine* e = new VVEngine(); e->impl.prepare(sr); e->daw.prepare(sr); e->click.prepare(sr); return e; }
void vv_engine_destroy(VVEngine* e) { delete e; }
double vv_engine_sample_rate(VVEngine* e) { return e ? e->impl.sampleRate() : 48000.0; }
void vv_engine_prepare(VVEngine* e, double sr) { if (e) { e->impl.prepare(sr); if (e->daw.sampleRate() != sr || e->daw.state() == 0) e->daw.prepare(sr); e->click.prepare(sr); } }
void vv_engine_reset(VVEngine* e) { if (e) e->impl.reset(); }
uint32_t vv_param_count(void) { return (uint32_t)vv::kParamCount; }
uint32_t vv_param_id_at(uint32_t i) { return i < (uint32_t)vv::kParamCount ? vv::kParams[i].id : 0; }
const char* vv_param_address(uint32_t id) { const vv::ParamInfo* p = vv::Engine::info(id); return p ? p->address : ""; }
void vv_engine_set_param(VVEngine* e, uint32_t id, float v) { if (e) e->impl.setParam(id, v); }
int vv_engine_pump_params(VVEngine* e) { return e ? e->impl.pumpParams() : 0; }
int vv_engine_sync_params_from_preset(VVEngine* e) { return e ? e->impl.syncParamsFromPreset() : 0; }
int vv_wt_harmonics(const float* amps, int n, float* out) { if (!amps || !out || n <= 0) return 0; std::vector<double> a(amps, amps + n); auto f = vv::wttools::harmonicFrame(a); memcpy(out, f.data(), f.size() * 4); return 1; }
int vv_wt_formula(const char* expr, int frames, float* out, int maxFrames, char* err, int errCap) {
    std::vector<std::vector<float>> fr; std::string e; if (!expr || !out) return 0;
    if (!vv::wttools::formulaFrames(expr, frames, fr, e)) { if (err && errCap > 0) { const int n = (int)std::min<size_t>(e.size(), (size_t)errCap - 1); memcpy(err, e.data(), n); err[n] = 0; } return 0; }
    const int n = std::min((int)fr.size(), maxFrames); for (int i = 0; i < n; i++) memcpy(out + (size_t)i * vv::wttools::kFrame, fr[i].data(), vv::wttools::kFrame * 4); if (err && errCap > 0) err[0] = 0; return n;
}
int vv_wt_curve(const float* p, int n, float* out) {
    if (!p || n < 2 || !out) return 0; std::vector<vv::CurvePt> pts(n); for (int i = 0; i < n; i++) { pts[i].x = p[i * 4]; pts[i].y = p[i * 4 + 1]; pts[i].cx = p[i * 4 + 2]; pts[i].cy = p[i * 4 + 3]; }
    std::vector<float> f(vv::wttools::kFrame); for (int i = 0; i < vv::wttools::kFrame; i++) f[i] = (float)(vv::evalCurveY(pts, (double)i / vv::wttools::kFrame) * 2 - 1);
    vv::wttools::normalize(f); memcpy(out, f.data(), f.size() * 4); return 1;
}
int vv_wt_slice(const float* mono, int len, int frames, float* out, int maxFrames) { auto fr = vv::wttools::sliceAudio(mono, len, frames); const int n = std::min((int)fr.size(), maxFrames); for (int i = 0; i < n; i++) memcpy(out + (size_t)i * vv::wttools::kFrame, fr[i].data(), vv::wttools::kFrame * 4); return n; }
VVAudio* vv_audio_create(int sr, int ch, int len, const float* l, const float* r) {
    if (!l || len <= 0) return nullptr; auto* a = new VVAudio; a->a.sr = sr; a->a.ch.emplace_back(l, l + len); if (ch > 1 && r) a->a.ch.emplace_back(r, r + len); return a;
}
VVAudio* vv_audio_from_wav(const uint8_t* wav, size_t n) {
    auto b = vv::parseWav(wav, n); if (!b || b->len <= 0) return nullptr; auto* a = new VVAudio; a->a.sr = b->sr; for (int c = 0; c < b->ch && c < 2; c++) a->a.ch.emplace_back(b->d[c].begin(), b->d[c].begin() + b->len); return a;
}
void vv_audio_free(VVAudio* a) { delete a; }
int vv_audio_length(const VVAudio* a) { return a ? a->a.length() : 0; }
int vv_audio_channels(const VVAudio* a) { return a ? (int)a->a.ch.size() : 0; }
int vv_audio_sample_rate(const VVAudio* a) { return a ? a->a.sr : 0; }
void vv_audio_read(const VVAudio* a, int c, float* dst) { if (a && dst && c >= 0 && c < (int)a->a.ch.size()) memcpy(dst, a->a.ch[c].data(), a->a.ch[c].size() * 4); }
size_t vv_audio_to_wav16(const VVAudio* a, uint8_t* buf, size_t cap) { if (!a) return 0; const auto w = vv::stools::toWav16(a->a); if (buf && cap >= w.size()) memcpy(buf, w.data(), w.size()); return w.size(); }
VVAudio* vv_audio_stretch(const VVAudio* a, int mode, double ratio, double semi, double beats, double bpm, int quality) {
    if (!a) return nullptr; vv::stools::StretchCfg c; c.mode = mode == 1 ? vv::stools::StretchCfg::Pitch : mode == 2 ? vv::stools::StretchCfg::Fit : vv::stools::StretchCfg::Ratio; c.ratio = ratio; c.semi = semi; c.beats = beats; c.bpm = bpm; c.quality = quality;
    auto* o = new VVAudio; o->a = vv::stools::applyStretch(a->a, c); return o;
}
VVAudio* vv_audio_warp(const VVAudio* a, const double* m, int n) {
    if (!a) return nullptr; std::vector<vv::stools::WarpMarker> mk; for (int i = 0; i < n; i++) mk.push_back({m[i * 2], m[i * 2 + 1]}); auto* o = new VVAudio; o->a = vv::stools::applyWarp(a->a, mk); return o;
}
VVAudio* vv_audio_from_image(const uint8_t* rgba, int w, int h, int sr, double duration, int note, int harmonics, double tilt, double gamma, int invert, int loop, int rgbStereo) {
    vv::stools::ImageAudioCfg c; c.sr = sr > 0 ? sr : 48000; c.duration = duration; c.note = note; c.harmonics = harmonics; c.tilt = tilt; c.gamma = gamma; c.invert = invert != 0; c.loop = loop != 0; c.rgbStereo = rgbStereo != 0;
    auto* o = new VVAudio; o->a = vv::stools::imageToAudio(rgba, w, h, c); if (o->a.length() == 0) { delete o; return nullptr; } return o;
}
VVAudio* vv_audio_spectral(const VVAudio* a, int granular, int logScale, int window, const double* bands, int nBands) {
    if (!a) return nullptr; vv::stools::SpectralCfg c; c.granular = granular != 0; c.logScale = logScale != 0; c.window = window; if (bands && nBands > 0) c.bands.assign(bands, bands + nBands); auto* o = new VVAudio; o->a = vv::stools::spectralResynth(a->a, c); return o;
}
int vv_engine_chord_label(VVEngine* e, char* buf, int cap) {
    if (!e || !buf || cap <= 0) return 0;
    uint64_t m[2]; e->impl.activeNoteMask(m);
    const std::string& l = e->chord.update(m);
    const int n = (int)std::min<size_t>(l.size(), (size_t)cap - 1); std::memcpy(buf, l.data(), n); buf[n] = 0; return n;
}
void  vv_engine_set_solo_mute(VVEngine* e, int solo_mask, int mute_mask) { if (e) e->impl.setSoloMute(solo_mask, mute_mask); }
float vv_engine_sample_head(VVEngine* e, int slot) { return e ? e->impl.full().sampleHead(slot) : -1.f; }
float vv_engine_mixer_level(VVEngine* e, int index) { return e ? e->impl.mixerLevel(index) : 0.f; }
float vv_engine_lfo_phase(VVEngine* e, int index) { return e ? e->impl.lfoPhaseSnap(index) : 0.f; }
float vv_engine_lfo_rate_hz(VVEngine* e, int index) { return e ? e->impl.lfoRateHz(index) : 0.f; }
void vv_engine_scope_tap_mask(VVEngine* e, int layer, unsigned groups) { if (e) e->impl.setScopeTapMask(layer, groups); }
void vv_engine_set_display_latency(VVEngine* e, int samples) { if (e) e->impl.setDisplayLatency(samples); }
int vv_engine_scope_tap_read(VVEngine* e, int layer, int group, float* out, int n) { return (e && out) ? (e->impl.scopeTapRead(layer, group, out, n) ? 1 : 0) : 0; }
float vv_engine_get_param(VVEngine* e, uint32_t id) { return e ? e->impl.getParam(id) : 0.f; }
void vv_engine_note_on(VVEngine* e, int n, float v) { if (e) e->impl.noteOn(n, v); }
void vv_engine_note_off(VVEngine* e, int n) { if (e) e->impl.noteOff(n); }
void vv_engine_set_power(VVEngine* e, int profile, int flags) { if (!e) return; vv::PowerCfg c; c.profile = profile < 0 ? 0 : profile > 3 ? 3 : profile; c.flags = (uint32_t)flags & 31u; e->impl.full().synth().setPower(c); }
void vv_engine_get_power_stats(VVEngine* e, double out[5]) {
    for (int i = 0; i < 5; i++) out[i] = 0; out[4] = 1; if (!e) return; auto& s = e->impl.full().synth(); const auto& st = s.powerStats();
    out[0] = (double)st.voicesReaped.load(std::memory_order_relaxed); out[1] = (double)st.fxBlocksSkipped.load(std::memory_order_relaxed); out[2] = (double)st.eqBlocksSkipped.load(std::memory_order_relaxed); out[3] = (double)st.quanta.load(std::memory_order_relaxed);
    out[4] = (double)vv::PowerPolicy::forConfig(s.power(), s.sampleRate() > 0 ? s.sampleRate() : 48000.0).uiDivider;
}
void vv_engine_pad_slice_on(VVEngine* e, int slot, int slice, int pad, float v) { if (e) e->impl.full().padSliceOn(slot, slice, pad & 15, v); }
void vv_engine_pad_on(VVEngine* e, int layer, int slot, int pad, int pitchNote, float v) { if (e) e->impl.full().padOn(layer, slot, pad & 15, pitchNote & 127, v); }
void vv_engine_pad_off(VVEngine* e, int layer, int pad) { if (e) e->impl.full().padOff(layer, pad & 15); }
void vv_engine_audition_slice(VVEngine* e, int slot, int slice, float vel) { if (e) e->impl.full().auditionSlice(slot & 15, slice < 0 ? 0 : slice, vel); }
void vv_engine_audition_off(VVEngine* e) { if (e) e->impl.full().auditionOff(); }
int vv_engine_poll_cc(VVEngine* e) { return e ? e->impl.full().pollCc() : 0; }
void vv_engine_cc_learn_arm(VVEngine* e, const char* path, double lo, double hi) { if (e && path) e->impl.full().armCcLearn(path, lo, hi); }
void vv_engine_cc_learn_cancel(VVEngine* e) { if (e) e->impl.full().cancelCcLearn(); }
int vv_engine_cc_learned(VVEngine* e) { return e ? e->impl.full().learnedCc() : -1; }
int vv_engine_cc_last(VVEngine* e) { return e ? e->impl.full().lastCc() : -1; }
void vv_engine_cc_learn_clear(VVEngine* e) { if (e) e->impl.full().clearCcLearn(); }
size_t vv_engine_cc_learn_json(VVEngine* e, char* buf, size_t cap) { if (!e) return 0; const auto s = e->impl.full().ccLearnJson(); if (buf && cap) { const size_t n = s.size() < cap - 1 ? s.size() : cap - 1; memcpy(buf, s.data(), n); buf[n] = 0; } return s.size(); }
int vv_engine_cc_learn_set_json(VVEngine* e, const char* json) { return e && json && e->impl.full().setCcLearnJson(json) ? 1 : 0; }
void vv_engine_all_notes_off(VVEngine* e) { if (e) e->impl.allNotesOff(); }
void vv_engine_midi(VVEngine* e, const uint8_t* b, int len) { if (e) e->impl.midi(b, len); }
void vv_engine_set_transport(VVEngine* e, double bpm, int playing) { if (e) e->impl.setTransport(bpm, playing != 0); }
double vv_engine_transport_beat(VVEngine* e) { return e ? e->impl.full().transportBeat() : 0.0; }
void vv_engine_set_host_transport(VVEngine* e, int valid, int playing, double beat, double bpm) { if (e) e->impl.full().setHostTransport(valid != 0, playing != 0, beat, bpm); }
int vv_engine_load_preset_json(VVEngine* e, const char* j, size_t n) { return e && j && e->impl.full().loadPresetJson(std::string(j, n)) ? 1 : 0; }
int vv_engine_load_wavetable(VVEngine* e, const char* name, const uint8_t* d, size_t n) { if (!e || !name || !d) return 0; auto t = vv::parseWavetable(d, n); if (!t) return 0; e->impl.full().addWavetable(name, t); return 1; }
int vv_engine_load_sample_wav(VVEngine* e, const char* name, const uint8_t* d, size_t n) { if (!e || !name || !d) return 0; auto b = vv::parseWav(d, n); if (!b) return 0; e->impl.full().addSample(name, b); return 1; }
int vv_engine_set_value(VVEngine* e, const char* path, const char* v) { return e && path && v && e->impl.full().setValue(path, v) ? 1 : 0; }
size_t vv_engine_get_value(VVEngine* e, const char* path, char* buf, size_t n) {
    if (!e) return 0; const std::string s = (path && *path) ? e->impl.full().getValue(path) : e->impl.full().exportPreset();
    if (buf && n) { const size_t c = s.size() < n - 1 ? s.size() : n - 1; memcpy(buf, s.data(), c); buf[c] = 0; } return s.size();
}
int vv_engine_set_values(VVEngine* e, const char* object_json) {
    if (!e || !object_json) return 0;
    vv::Json j; try { j = vv::JsonParser::parse(object_json); } catch (...) { return 0; }
    if (j.type != vv::Json::Obj) return 0;
    std::vector<std::pair<std::string, std::string>> kv; for (auto& p : j.o) kv.emplace_back(p.first, vv::jsonDump(p.second));
    return e->impl.full().setValues(kv) ? 1 : 0;
}
int vv_engine_xfx_action(VVEngine* e, const char* a, const char* args) { return e && a && e->impl.full().xfxAction(a, args ? args : "{}") ? 1 : 0; }

size_t vv_engine_export_preset(VVEngine* e, char* buf, size_t cap) { if (!e) return 0; const auto s = e->impl.full().exportPreset(); if (buf && cap) { const size_t n = s.size() < cap - 1 ? s.size() : cap - 1; memcpy(buf, s.data(), n); buf[n] = 0; } return s.size(); }
int vv_engine_smp_rr_last(VVEngine* e, int slot) { return e ? e->impl.full().smpRrLast(slot) : -1; }
void vv_engine_smp_rr_reset(VVEngine* e) { if (e) e->impl.full().smpRrReset(); }
/* ---- clip launcher */
int vv_clip_op(VVEngine* e, const char* op, const char* args, char* out, size_t cap) { if (!e || !op) return 0; std::string o; const int r = e->impl.full().clipOp(op, args ? args : "{}", &o); if (out && cap) { const size_t n = o.size() < cap - 1 ? o.size() : cap - 1; memcpy(out, o.data(), n); out[n] = 0; } return r; }
size_t vv_clip_state_json(VVEngine* e, char* buf, size_t cap) { if (!e) return 0; const auto s = e->impl.full().clipsJson(); if (buf && cap) { const size_t n = s.size() < cap - 1 ? s.size() : cap - 1; memcpy(buf, s.data(), n); buf[n] = 0; } return s.size(); }
void vv_clip_launch(VVEngine* e, int slot) { if (e) e->impl.full().clipLaunch(slot); }
void vv_clip_stop(VVEngine* e, int slot, int immediate) { if (e) e->impl.full().clipStop(slot, immediate != 0); }
void vv_clip_stop_all(VVEngine* e) { if (e) e->impl.full().clipStopAll(); }
uint64_t vv_clip_playing_mask(VVEngine* e) { return e ? e->impl.full().clipPlayingMask() : 0; }
uint64_t vv_clip_queued_start_mask(VVEngine* e) { return e ? e->impl.full().clipQueuedStartMask() : 0; }
uint64_t vv_clip_queued_stop_mask(VVEngine* e) { return e ? e->impl.full().clipQueuedStopMask() : 0; }
int vv_clip_recording_slot(VVEngine* e) { return e ? e->impl.full().clipRecordingSlot() : -1; }
float vv_clip_phase(VVEngine* e, int slot) { return e ? e->impl.full().clipPhase(slot) : -1.f; }
int vv_clip_drain_recording(VVEngine* e) { return e ? e->impl.full().clipDrainRecording() : 0; }
size_t vv_engine_xfx_export(VVEngine* e, char* buf, size_t n) {
    if (!e) return 0; const std::string s = e->impl.full().xfxExport();
    if (buf && n) { const size_t k = s.size() < n - 1 ? s.size() : n - 1; memcpy(buf, s.data(), k); buf[k] = 0; } return s.size();
}
int vv_engine_set_global(VVEngine* e, const char* n, double v) { return e && n && e->impl.full().setGlobal(n, v) ? 1 : 0; }
int vv_engine_preset_action(VVEngine* e, const char* a, const char* c, uint32_t seed) { return e && a && e->impl.full().presetAction(a, c ? c : "", seed) ? 1 : 0; }
int vv_engine_preset_generate(VVEngine* e, const char* c, uint32_t seed, int scale_on, int scale_root, uint32_t scale_mask, const char* scale_label, char* out_name, int name_cap, char* out_view, int view_cap) {
    if (!e || !c) return 0; std::string nm, vw;
    if (!e->impl.full().presetGenerate(c, seed, scale_on != 0, scale_root, scale_mask, &nm, &vw, scale_label ? scale_label : "")) return 0;
    if (out_name && name_cap > 0) { std::snprintf(out_name, (size_t)name_cap, "%s", nm.c_str()); }
    if (out_view && view_cap > 0) { std::snprintf(out_view, (size_t)view_cap, "%s", vw.c_str()); }
    return 1;
}
int vv_engine_chord_action(VVEngine* e, const char* a, int i, uint32_t seed) { return e && a && e->impl.full().chordAction(a, i, seed) ? 1 : 0; }
size_t vv_engine_chord_info(VVEngine* e, char* buf, size_t n) {
    if (!e) return 0; const std::string s = e->impl.full().chordInfo();
    if (buf && n) { const size_t c = s.size() < n - 1 ? s.size() : n - 1; memcpy(buf, s.data(), c); buf[c] = 0; } return s.size();
}
int vv_engine_chord_playing(VVEngine* e) { return e ? e->impl.full().chordPlaying() : -1; }
int vv_engine_seq_step(VVEngine* e, int l) { return e ? e->impl.full().seqStep(l) : 0; }
int vv_engine_seq_slot(VVEngine* e, int l) { return e ? e->impl.full().seqSlot(l) : 0; }
int vv_engine_drum_step(VVEngine* e, int lane) { return e ? e->impl.full().drumStep(lane) : -1; }
int vv_engine_seq_running(VVEngine* e) { return e && e->impl.full().seqRunning() ? 1 : 0; }
void vv_engine_set_macro(VVEngine* e, int i, float v) { if (e) e->impl.full().setMacro(i, v); }
void vv_engine_set_xy(VVEngine* e, int i, float v) { if (e) e->impl.full().setXy(i, v); }
void vv_engine_set_modwheel(VVEngine* e, float v) { if (e) e->impl.full().setModWheel(v); }
void vv_engine_set_pitchbend(VVEngine* e, float v) { if (e) e->impl.full().setPitchBend(v); }
void vv_engine_set_master_limiter(VVEngine* e, int on, double thr, double ceil, double rel, double knee, double softClip) { if (!e) return; vv::MasterOut::Params p; p.on = on != 0; p.threshold = thr; p.ceiling = ceil; p.release = rel; p.knee = knee; p.softClip = softClip; e->impl.full().setMasterLimiter(p); }
// out[0..5] = momentary, short-term, integrated LUFS, peak L dBFS, peak R dBFS, max gain reduction dB (-1000 = nothing measured yet)
void vv_engine_get_loudness(VVEngine* e, double* out) { if (!e || !out) return; const auto& m = e->impl.full().masterMeter(); const double v[6] = { m.momentary(), m.shortTerm(), m.integrated(), m.peakLdb(), m.peakRdb(), m.gainReductionDb() }; for (int i = 0; i < 6; i++) out[i] = std::isfinite(v[i]) ? v[i] : -1000.0; }
void vv_engine_reset_loudness(VVEngine* e) { if (e) e->impl.full().resetLoudness(); }
void vv_engine_set_midi_channel(VVEngine* e, int ch) { if (e) e->impl.full().setMidiChannel(ch); }
void vv_engine_mpe_glide(VVEngine* e, int note, double semis) { if (e) e->impl.full().mpeGlide(note, semis); }
void vv_engine_set_mpe(VVEngine* e, int on) { if (e) e->impl.full().setMpe(on != 0); }
int vv_engine_arp_fit(VVEngine* e, int count, const uint8_t* strong, int kbMask, uint32_t seed, int lo, int hi, int* out, char* name, int nameCap) {
    if (!e || !out || count <= 0) return 0; vv::harmony::Palette p;
    if (!e->impl.full().arpFitPitches(count, strong, kbMask, seed, lo, hi, out, &p)) return 0;
    if (name && nameCap > 0) std::snprintf(name, (size_t)nameCap, "%s", p.name);
    return 1;
}
// Piano-roll RECORD. set(on): arm / disarm (disarm flushes held notes through the next drain with final = 1). drain: UI thread only (single consumer), ~30 Hz.
void vv_engine_roll_rec_set(VVEngine* e, int on) { if (!e) return; if (on) e->rollRec.begin(); e->impl.full().rollRecArm(on != 0); }
int vv_engine_roll_rec_armed(VVEngine* e) { return e && e->impl.full().rollRecArmed() ? 1 : 0; }
size_t vv_engine_roll_rec_drain_json(VVEngine* e, double loopStartBeats, double loopEndBeats, double rollEndBeats, double gridBeats, int final_, char* buf, size_t cap) {
    if (!e || !buf || cap < 32) return 0;
    vv::rollrec::Cfg c; c.ls = loopStartBeats; c.le = std::max(loopStartBeats + 1e-3, loopEndBeats); c.roomEnd = rollEndBeats; c.grid = gridBeats > 0 ? gridBeats : 0;
    vv::rollrec::Out out; vv::rollrec::Ev ev; int guard = 0; while (guard++ < 8192 && e->impl.full().rollRecPop(ev)) e->rollRec.feed(ev, c, out);
    e->rollRec.settle(e->impl.full().transportBeat(), c, out, final_ != 0);
    std::string s = "{\"notes\":["; char t[160];
    for (size_t i = 0; i < out.notes.size(); i++) { std::snprintf(t, sizeof t, "%s{\"t\":%.3f,\"l\":%.3f,\"p\":%d,\"v\":%.3f}", i ? "," : "", out.notes[i].t, out.notes[i].l, out.notes[i].p, out.notes[i].v); s += t; }
    auto pts = [&](const char* nm, const std::vector<vv::rollrec::Pt>& v) { s += ",\""; s += nm; s += "\":["; for (size_t i = 0; i < v.size(); i++) { std::snprintf(t, sizeof t, "%s[%.3f,%.3f]", i ? "," : "", v[i].t, v[i].v); s += t; } s += "]"; };
    s += "]"; pts("bend", out.bend); pts("bendCut", out.bendCut); pts("mod", out.mod); pts("modCut", out.modCut);
    std::snprintf(t, sizeof t, ",\"count\":%d,\"armed\":%d}", e->rollRec.notesRecorded(), e->impl.full().rollRecArmed() ? 1 : 0); s += t;
    if (s.size() + 1 > cap) return 0;   // caller's buffer too small: events are lost, so size it generously (64 KB is ample at 30 Hz)
    std::memcpy(buf, s.c_str(), s.size() + 1); return s.size();
}
void vv_engine_set_arp(VVEngine* e, int on, int latch) { if (e) { e->impl.full().setLatch(latch != 0); e->impl.full().setArpOn(on != 0); } }
void vv_engine_start_sequencer(VVEngine* e, int running) { if (e) e->impl.full().setTransport(running != 0); }
void vv_engine_set_polyphony(VVEngine* e, int v, int l) { if (e) e->impl.full().setPolyphony(v, l != 0); }
void vv_engine_release_layer_hold(VVEngine* e, int layer) { if (e) e->impl.full().releaseLayerHold(layer); }
int vv_engine_has_preset(VVEngine* e) { return e && e->impl.full().hasPreset() ? 1 : 0; }
void vv_engine_feed_input(VVEngine* e, const float* l, const float* r, int frames) {
    if (!e || !l || frames <= 0) return; e->impl.feedInput(l, r, frames);
    if (e->rec.state.load(std::memory_order_relaxed) && frames <= (int)e->rec.inL.size()) { std::memcpy(e->rec.inL.data(), l, sizeof(float) * frames); std::memcpy(e->rec.inR.data(), r ? r : l, sizeof(float) * frames); e->rec.inFrames = frames; }
}
void vv_engine_render(VVEngine* e, float* l, float* r, int frames) {
    if (!e) return; e->impl.render(l, r, frames);
    e->daw.push(l, r, frames);                                    // recorder taps the master BEFORE the click is added
    e->click.process(l, r, frames, e->impl.tempo());              // click: heard, never recorded
    VVRec& rc = e->rec; const int st = rc.state.load(std::memory_order_acquire); if (st != 1 && st != 2) return;
    const bool ext = rc.source.load() == 1; const float* sl = ext ? (e->rec.inFrames >= frames ? e->rec.inL.data() : nullptr) : l; const float* sr = ext ? (e->rec.inFrames >= frames ? e->rec.inR.data() : nullptr) : r;
    float pk = 0; int start = 0; int cur = rc.state.load();
    if (sl) for (int i = 0; i < frames; i++) pk = std::max(pk, std::max(std::fabs(sl[i]), std::fabs(sr ? sr[i] : sl[i])));
    rc.peak.store(pk, std::memory_order_relaxed);
    if (cur == 1) { if (!sl || pk < rc.threshold.load()) return; rc.state.store(2); cur = 2; }   // armed: wait for sound
    const int cap = (int)rc.L.size(); int pos = rc.pos.load(); const int n = std::min(frames - start, cap - pos);
    if (sl) for (int i = 0; i < n; i++) { rc.L[(size_t)pos + i] = sl[i]; rc.R[(size_t)pos + i] = sr ? sr[i] : sl[i]; }
    pos += n; rc.pos.store(pos, std::memory_order_release); if (pos >= cap) rc.state.store(3);
}
int vv_engine_rec_prepare(VVEngine* e, int sample_rate, double max_seconds, int source, int arm, double threshold) {
    if (!e) return 0; VVRec& rc = e->rec; rc.state.store(0); const size_t cap = (size_t)std::max(1.0, max_seconds * std::max(8000, sample_rate));
    rc.L.assign(cap, 0.f); rc.R.assign(cap, 0.f); rc.pos.store(0); rc.peak.store(0.f); rc.source.store(source); rc.arm.store(arm); rc.threshold.store((float)threshold);
    rc.inL.assign(8192, 0.f); rc.inR.assign(8192, 0.f); rc.inFrames = 0; return 1;
}
void vv_engine_rec_start(VVEngine* e) { if (e && !e->rec.L.empty()) e->rec.state.store(e->rec.arm.load() ? 1 : 2, std::memory_order_release); }
void vv_engine_rec_stop(VVEngine* e) { if (e && (e->rec.state.load() == 1 || e->rec.state.load() == 2)) e->rec.state.store(3, std::memory_order_release); }
int vv_engine_rec_state(VVEngine* e) { return e ? e->rec.state.load() : 0; }
int vv_engine_rec_frames(VVEngine* e) { return e ? e->rec.pos.load(std::memory_order_acquire) : 0; }
float vv_engine_rec_peak(VVEngine* e) { return e ? e->rec.peak.load() : 0.f; }
VVAudio* vv_engine_rec_take(VVEngine* e, int sample_rate) {
    if (!e || e->rec.state.load() == 1 || e->rec.state.load() == 2) return nullptr; const int n = e->rec.pos.load(); if (n <= 0) return nullptr;
    auto* o = new VVAudio; o->a.sr = sample_rate; o->a.ch = { std::vector<float>(e->rec.L.begin(), e->rec.L.begin() + n), std::vector<float>(e->rec.R.begin(), e->rec.R.begin() + n) };
    e->rec.state.store(0); e->rec.pos.store(0); return o;
}
}

// ---- DAW recorder + click (standalone use). start/stop/state/frames are lock-free; drain must be called regularly from ONE non-audio thread.
void vv_daw_start(VVEngine* e) { if (e) { e->daw.start(); if (e->click.mode() != 0) e->click.start(); } }
void vv_daw_stop(VVEngine* e) { if (e) e->daw.stop(); }
int  vv_daw_state(VVEngine* e) { return e ? e->daw.state() : 0; }
double vv_daw_drain(VVEngine* e) { if (!e) return 0; const int64_t n = e->daw.drain(); return (double)n / e->daw.sampleRate(); }   /* take length in seconds */
int  vv_daw_dropped(VVEngine* e) { return e ? (int)std::min<int64_t>(e->daw.dropped(), 2147483647) : 0; }
void vv_daw_set_click(VVEngine* e, int mode) { if (e) { const bool was = e->click.mode() != 0; e->click.setMode(mode); if (!was && mode != 0) e->click.start(); } }
size_t vv_daw_take_wav(VVEngine* e, int bit_depth, int sample_rate, uint8_t* buf, size_t cap) {
    if (!e || e->daw.frames() <= 0) return 0; const double sr = e->daw.sampleRate(); const int dst = sample_rate > 0 ? sample_rate : (int)sr;
    const int bps = bit_depth == 24 ? 3 : 2;
    if (!buf) { const int64_t n = std::fabs(sr - dst) < 0.5 ? e->daw.frames() : (int64_t)std::ceil((double)e->daw.frames() * dst / sr); return (size_t)(44 + n * 2 * bps); }   /* size query: no resampling */
    auto L = vv::DawRecorder::resample(e->daw.left(), sr, dst), R = vv::DawRecorder::resample(e->daw.right(), sr, dst);
    auto w = vv::DawRecorder::toWav(L, R, dst, bit_depth == 24 ? 24 : 16); if (buf && cap >= w.size()) std::memcpy(buf, w.data(), w.size()); return w.size();
}
void vv_daw_clear_take(VVEngine* e) { if (e) e->daw.clearTake(); }

// ---------------------------------------------------------------- LUA layer
#include "vv/lua_factory.hpp"
extern "C" {
static std::string luaJsonEsc(const std::string& s) { std::string o; for (char ch : s) { if (ch == '"' || ch == '\\') { o += '\\'; o += ch; } else if (ch == '\n') o += "\\n"; else if ((unsigned char)ch < 32) o += ' '; else o += ch; } return o; }
static std::string luaInfoJson(const vv::LuaSlotInfo& i) {
    std::string o = "{\"ok\":" + std::string(i.ok ? "true" : "false") + ",\"error\":\"" + luaJsonEsc(i.error) + "\",\"line\":" + std::to_string(i.line) + ",\"col\":" + std::to_string(i.col) + ",\"name\":\"" + luaJsonEsc(i.name) + "\",\"category\":\"" + luaJsonEsc(i.category) + "\",\"author\":\"" + luaJsonEsc(i.author) + "\",\"desc\":\"" + luaJsonEsc(i.desc) + "\",\"setupInstr\":" + std::to_string(i.setupInstr) + ",\"tables\":" + std::to_string(i.tables) + ",\"params\":[";
    for (size_t k = 0; k < i.params.size(); k++) { const auto& p = i.params[k]; if (k) o += ","; char nb[160]; std::snprintf(nb, sizeof nb, "\"min\":%.9g,\"max\":%.9g,\"def\":%.9g,\"step\":%.9g", p.mn, p.mx, p.def, p.step);
        o += "{\"id\":\"" + luaJsonEsc(p.id) + "\",\"label\":\"" + luaJsonEsc(p.label) + "\"," + nb + ",\"unit\":\"" + luaJsonEsc(p.unit) + "\",\"group\":\"" + luaJsonEsc(p.group) + "\",\"kind\":\"" + p.kind + "\",\"options\":["; for (size_t q = 0; q < p.options.size(); q++) { if (q) o += ","; o += "\"" + luaJsonEsc(p.options[q]) + "\""; } o += "]}"; }
    return o + "]}";
}
static int luaCopyOut(const std::string& s, char* buf, int cap) { if (!buf || cap <= 0) return (int)s.size(); const int n = std::min((int)s.size(), cap - 1); std::memcpy(buf, s.data(), n); buf[n] = 0; return (int)s.size(); }
int vv_engine_lua_info(VVEngine* e, int slot, char* buf, int cap) { if (!e) return -1; return luaCopyOut(luaInfoJson(e->impl.full().synth().luaHost().info(slot)), buf, cap); }
int vv_engine_lua_validate(VVEngine* e, const char* src, int seed, char* buf, int cap) { if (!e || !src) return -1; return luaCopyOut(luaInfoJson(e->impl.full().synth().luaHost().validate(src, (uint32_t)seed)), buf, cap); }
int vv_engine_lua_stats(VVEngine* e, int slot, char* buf, int cap) { if (!e) return -1; auto& h = e->impl.full().synth().luaHost();
    std::string o = "{\"voices\":" + std::to_string(h.activeVoices(slot)) + ",\"instr\":" + std::to_string(h.lastBlockInstr(slot)) + ",\"faults\":" + std::to_string(h.faults(slot)) + ",\"error\":\"" + luaJsonEsc(h.lastError(slot)) + "\",\"log\":\"" + luaJsonEsc(h.log(slot)) + "\"}"; return luaCopyOut(o, buf, cap); }
int vv_engine_lua_set_param(VVEngine* e, int slot, int index, float value) { if (!e) return 0; e->impl.full().synth().luaHost().setParam(slot, index, value); return 1; }
int vv_engine_lua_factory_count(void) { return (int)vv::kLuaFactoryCount; }
const char* vv_engine_lua_factory_id(int i) { return i >= 0 && i < (int)vv::kLuaFactoryCount ? vv::kLuaFactory[i].id : ""; }
const char* vv_engine_lua_factory_src(int i) { return i >= 0 && i < (int)vv::kLuaFactoryCount ? vv::kLuaFactory[i].src : ""; }
}  // extern "C"

// ---------------------------------------------------------------- IR convolution reverb library
#include "vv/ir.hpp"
#include "vv/json.hpp"
extern "C" {
static void irCopy(const std::string& s, char* buf, int cap) { if (!buf || cap <= 0) return; const int n = std::min((int)s.size(), cap - 1); std::memcpy(buf, s.data(), (size_t)n); buf[n] = 0; }
int vv_ir_category_count(void) { return vv::ir::kCategoryCount; }
const char* vv_ir_category_name(int i) { return i >= 0 && i < vv::ir::kCategoryCount ? vv::ir::kCategories[i] : ""; }
int vv_ir_factory_count(void) { return vv::ir::kFactoryCount; }
const char* vv_ir_factory_id(int i) { static thread_local std::string s; if (i < 0 || i >= vv::ir::kFactoryCount) return ""; s = std::string("f:") + vv::ir::kFactory[i].id; return s.c_str(); }
const char* vv_ir_factory_name(int i) { return i >= 0 && i < vv::ir::kFactoryCount ? vv::ir::kFactory[i].name : ""; }
const char* vv_ir_factory_category(int i) { return i >= 0 && i < vv::ir::kFactoryCount ? vv::ir::kFactory[i].cat : ""; }
static int irReport(bool ok, const std::string& id, const std::string& err, const std::vector<std::string>& warn, char* out_id, int id_cap, char* out_msg, int msg_cap) {
    if (ok) { irCopy(id, out_id, id_cap); std::string w; for (auto& x : warn) w += (w.empty() ? "" : " ") + x; irCopy(w, out_msg, msg_cap); return 1; }
    irCopy("", out_id, id_cap); irCopy(err, out_msg, msg_cap); return 0;
}
int vv_ir_import_file(const char* path, const char* name, const char* replace_id, char* out_id, int id_cap, char* out_msg, int msg_cap) {
    if (!path) return irReport(false, "", "No file.", {}, out_id, id_cap, out_msg, msg_cap);
    std::string nm = name && *name ? name : path; if (!(name && *name)) { const size_t sl = nm.rfind('/'); if (sl != std::string::npos) nm = nm.substr(sl + 1); const size_t dt = nm.rfind('.'); if (dt != std::string::npos) nm = nm.substr(0, dt); }
    std::string id, err; std::vector<std::string> w; const bool ok = vv::ir::Library::get().addFile(path, nm, id, err, w, replace_id ? replace_id : ""); return irReport(ok, id, err, w, out_id, id_cap, out_msg, msg_cap);
}
int vv_ir_import_bytes(const uint8_t* data, size_t n, const char* name, const char* replace_id, char* out_id, int id_cap, char* out_msg, int msg_cap) {
    std::string id, err; std::vector<std::string> w; const bool ok = vv::ir::Library::get().add(data, n, name ? name : "Imported IR", id, err, w, replace_id ? replace_id : ""); return irReport(ok, id, err, w, out_id, id_cap, out_msg, msg_cap);
}
int vv_ir_remove(const char* id) { return id && vv::ir::Library::get().remove(id) ? 1 : 0; }
int vv_ir_scan_dir(const char* dir) { return dir ? vv::ir::Library::get().scanDir(dir) : 0; }
size_t vv_ir_user_json(char* buf, size_t cap) { const std::string s = vv::ir::Library::get().userJson(); if (buf && cap) irCopy(s, buf, (int)std::min<size_t>(cap, 1u << 30)); return s.size(); }
size_t vv_ir_missing_json(char* buf, size_t cap) { const std::string s = vv::ir::Library::get().missingJson(); if (buf && cap) irCopy(s, buf, (int)std::min<size_t>(cap, 1u << 30)); return s.size(); }
int vv_ir_wait_ready(int timeout_ms) { return vv::ir::Worker::get().waitIdle(timeout_ms) ? 1 : 0; }
static vv::ir::Shape irShapeOf(const vv::Json& p, std::string& id) {
    id = p.str("irId", vv::ir::kDefaultId); vv::ir::Shape s; s.start = p.num("start", 0); s.end = p.num("end", 1); s.lenPct = p.num("lenPct", 100); s.earlyLate = p.num("earlyLate", 0); s.damping = p.num("damping", 0); s.irGain = p.num("irGain", 0);
    if (auto v = p.get("reverse")) s.reverse = v->type == vv::Json::Bool ? v->b : v->n != 0; if (auto v = p.get("normalise")) s.normalise = v->type == vv::Json::Bool ? v->b : v->n != 0; return s;
}
int vv_ir_audition(const char* params_json, float* L, float* R, int max_frames, double sr) {
    if (!L || !R || max_frames <= 0 || sr < 8000) return 0; try {
        vv::Json p = vv::JsonParser::parse(params_json ? params_json : "{}"); std::string id; const vv::ir::Shape sh = irShapeOf(p, id); auto src = vv::ir::Library::get().source(id, sr); vv::ir::Buf o = vv::ir::audition(vv::ir::process(*src, sh));
        const int n = std::min<int>(max_frames, (int)o.L.size()); std::memcpy(L, o.L.data(), sizeof(float) * (size_t)n); std::memcpy(R, o.R.data(), sizeof(float) * (size_t)n); return n; } catch (...) { return 0; }
}
double vv_ir_envelope(const char* ir_id, float* out, int bins, double sr) {
    if (!out || bins <= 0 || sr < 8000) return 0; auto src = vv::ir::Library::get().source(ir_id ? ir_id : vv::ir::kDefaultId, sr); vv::ir::envelope(*src, out, bins); return (double)src->L.size() / sr;
}
double vv_ir_auto_trim(const char* ir_id, double sr, double* start, double* end) {
    auto src = vv::ir::Library::get().source(ir_id ? ir_id : vv::ir::kDefaultId, sr); double a = 0, b = 1; vv::ir::autoTrim(*src, a, b); if (start) *start = a; if (end) *end = b; return (double)src->L.size() / sr;
}
}  // extern "C"
extern "C" size_t vv_engine_load_warnings(VVEngine* e, char* buf, size_t cap) {
    if (!e) return 0; std::string s; for (auto& w : e->impl.full().loadWarnings()) s += (s.empty() ? "" : "\n") + w;
    if (buf && cap) { const size_t n = std::min(cap - 1, s.size()); memcpy(buf, s.data(), n); buf[n] = 0; } return s.size();
}
