// Vektur Vortex native engine: lock-free parameter store, MIDI in, host tempo/transport in, and the full instrument
// (Synth + Performance + FX chain + vocoder) behind a stable C ABI (see Bridge/vv_engine.h).
// Thread model: setParam()/getParam() are safe from any thread (atomics). noteOn/noteOff/midi/render must be called from the audio thread.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <string_view>
#include <vector>
#include "params_generated.hpp"
#include "full_engine.hpp"
#include "param_bridge.hpp"

namespace vv {

class Engine {
public:
    double sampleRate() const { return sr_; }
    void prepare(double sampleRate) {
        sr_ = sampleRate;
        for (int i = 0; i < kParamCount; i++) params_[kParams[i].id].store((float)kParams[i].defaultValue, std::memory_order_relaxed);
        full_.prepare(sr_);
    }
    void reset() { full_.allNotesOff(); }

    // ---- parameters (any thread)
    void setParam(uint32_t id, float value) {
        if (id >= kParamIdLimit) return;
        const ParamInfo* pi = info(id); if (!pi) return;
        const float v = (float)std::min(pi->maxValue, std::max(pi->minValue, (double)value));
        params_[id].store(v, std::memory_order_relaxed);
        if (id == kP_master_volume) full_.setMasterVolume(v);   // lock-free command ring: safe on the render thread
        dirty_[id].store(1, std::memory_order_release); anyDirty_.store(true, std::memory_order_release);
    }
    // Non-realtime: applies host parameter changes to the preset (call ~30 Hz from a timer; cheap when nothing changed).
    int pumpParams() {
        if (!full_.hasPreset()) return 0;   // flags stay set until a preset exists, so restored host values are not lost
        if (!anyDirty_.exchange(false, std::memory_order_acq_rel)) return 0;
        std::vector<uint32_t> ids; for (uint32_t i = 0; i < kParamIdLimit; i++) if (dirty_[i].exchange(0, std::memory_order_acq_rel)) ids.push_back(i);
        ParamBridge::apply(full_, ids, [&](uint32_t id) { return (double)params_[id].load(std::memory_order_relaxed); });
        return (int)ids.size();
    }
    // Non-realtime: after a preset load, move preset-backed host parameters to the preset's values (without marking them dirty).
    // Returns how many changed; the caller then refreshes its UI/AUParameterTree from getParam().
    int syncParamsFromPreset() {
        int n = 0;
        for (int i = 0; i < kParamCount; i++) { double v; const uint32_t id = kParams[i].id;
            if (id != kP_master_volume && ParamBridge::read(full_, id, v)) { v = std::min(kParams[i].maxValue, std::max(kParams[i].minValue, v)); params_[id].store((float)v, std::memory_order_relaxed); n++; } }
        return n;
    }
    void setSoloMute(int s, int m) { full_.setSoloMute(s, m); }   // layer bitmasks, any thread
    void activeNoteMask(uint64_t o[2]) const { full_.activeNoteMask(o); }   // sounding non-arp notes (chord analyzer), any thread
    float mixerLevel(int i) const { return full_.mixerLevel(i); }
    float lfoPhaseSnap(int k) const { return full_.lfoPhaseSnap(k); } float lfoRateHz(int k) const { return full_.lfoRateHz(k); }
    void setDisplayLatency(int samples) { full_.setDisplayLatency(samples); }   // output latency in samples: playheads follow what is audible
    void setScopeTapMask(int layer, uint32_t groups) { full_.setScopeTapMask(layer, groups); }
    bool scopeTapRead(int layer, int group, float* out, int n) const { return full_.scopeTapRead(layer, group, out, n); }   // Mixer meter peak for track i (0..23), any thread
    float getParam(uint32_t id) const { return id < kParamIdLimit ? params_[id].load(std::memory_order_relaxed) : 0.f; }
    static const ParamInfo* info(uint32_t id) { for (int i = 0; i < kParamCount; i++) if (kParams[i].id == id) return &kParams[i]; return nullptr; }

    // ---- audio thread
    void noteOn(int note, float velocity) { full_.noteOn(note, velocity); }
    void noteOff(int note) { full_.noteOff(note); }
    void allNotesOff() { full_.allNotesOff(); }
    void midi(const uint8_t* b, int len) { full_.midi(b, len); }
    void setTransport(double bpm, bool playing) { bpm_ = bpm; playing_ = playing; full_.setBpm(bpm); }
    double tempo() const { return bpm_; }
    bool playing() const { return playing_; }

    // The full instrument (Synth + Performance). Silent until a preset has been loaded.
    FullEngine& full() { return full_; }
    void feedInput(const float* l, const float* r, int n) { full_.feedInput(l, r, n); }   // audio thread: host/mic audio for the vocoder's external modulator/carrier
    // Silent until a preset has been loaded (a preset staged from another thread is picked up at the start of the block).
    void render(float* left, float* right, int frames) {
        if (!full_.hasPreset()) {
            full_.render(left, right, 0);
            if (!full_.hasPreset()) { std::fill(left, left + frames, 0.f); std::fill(right, right + frames, 0.f); return; }
        }
        full_.render(left, right, frames);
    }

private:
    double sr_ = 48000, bpm_ = 120; bool playing_ = false;
    std::atomic<float> params_[kParamIdLimit];
    std::atomic<uint8_t> dirty_[kParamIdLimit] = {};
    std::atomic<bool> anyDirty_{false};
    FullEngine full_;
};

} // namespace vv
