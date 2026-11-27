#include <cstring>
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>
#include "vv_engine.h"
#include "vv/params_generated.hpp"
static double rms(const std::vector<float>& v, int a, int b) { double s = 0; for (int i = a; i < b; i++) s += v[i] * v[i]; return std::sqrt(s / (b - a)); }
int main() {
    int fails = 0; const int N = 48000; std::vector<float> L(N), R(N);
    VVEngine* e = vv_engine_create(48000);
    auto check = [&](const char* name, bool ok) { std::printf("%-46s %s\n", name, ok ? "OK" : "FAIL"); if (!ok) fails++; };
    check("param count matches table", vv_param_count() == (uint32_t)vv::kParamCount && vv_param_count() >= 200);
    vv_engine_render(e, L.data(), R.data(), 4800); check("silent with no preset", rms(L, 0, 4800) == 0);
    uint8_t on[3] = { 0x90, 60, 100 }; vv_engine_midi(e, on, 3); vv_engine_render(e, L.data(), R.data(), 4800); check("silent with no preset even when a note is sent", rms(L, 0, 4800) == 0);
    check("param clamping", [&] { vv_engine_set_param(e, vv::kP_reverb_decay, 5.0f); return vv_engine_get_param(e, vv::kP_reverb_decay) <= 0.97f + 1e-6f; }());
    vv_engine_destroy(e);
    {   // chaos sources + new LFO destinations load and keep the engine finite
        VVEngine* c = vv_engine_create(48000); std::vector<float> a(48000), b(48000);
        const char* pj = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"level\":0.8},{\"level\":0}]}},\"connections\":[{\"source\":\"lorenz\",\"dest\":\"LFO1:rate\",\"amount\":50},{\"source\":\"rossler\",\"dest\":\"CHAOS:lorenzRate\",\"amount\":30},{\"source\":\"lfo2\",\"dest\":\"LFO1:morph\",\"amount\":40}],\"lfo\":{\"lfo1\":{\"waveform\":\"morph\",\"morph\":0.4,\"smoothing\":0.3}},\"chaos\":{\"lorenz\":{\"rate\":1.2},\"rossler\":{\"rate\":0.8}}}";
        { std::vector<unsigned char> wt0(8 + 256 * 4); uint32_t fc = 1, fs = 256; memcpy(wt0.data(), &fc, 4); memcpy(wt0.data() + 4, &fs, 4); for (int i = 0; i < 256; i++) { float x = (float)std::sin(6.2831853 * i / 256); memcpy(wt0.data() + 8 + i * 4, &x, 4); } vv_engine_load_wavetable(c, "analogMorph", wt0.data(), wt0.size()); }
        check("chaos/morph preset loads", vv_engine_load_preset_json(c, pj, strlen(pj)) == 1);
        vv_engine_note_on(c, 60, 0.8f); bool finite = true; for (int i = 0; i < 48000; i += 480) { vv_engine_render(c, &a[i], &b[i], 480); } for (int i = 0; i < 48000; i++) if (!std::isfinite(a[i])) finite = false;
        check("chaos-modulated engine stays finite and audible", finite && rms(a, 24000, 48000) > 0.005);
        vv_engine_destroy(c);
    }
    {   // full instrument through the C ABI: preset JSON + wavetable bytes + arp + sequencer
        VVEngine* f = vv_engine_create(48000);
        std::vector<unsigned char> wt = [] { std::vector<unsigned char> b(8 + 256 * 4); uint32_t fc = 1, fs = 256; memcpy(b.data(), &fc, 4); memcpy(b.data() + 4, &fs, 4); for (int i = 0; i < 256; i++) { float x = (float)std::sin(6.2831853 * i / 256); memcpy(b.data() + 8 + i * 4, &x, 4); } return b; }();
        check("wavetable loads", vv_engine_load_wavetable(f, "analogMorph", wt.data(), wt.size()) == 1);
        const char* pj = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8}]}},\"bpm\":120}";

        { const std::string wrapped = std::string("{\"vectaVortexPreset\":1,\"name\":\"x\",\"category\":\"Pad\",\"data\":") + pj + "}";
          check("single-preset wrapper (.vvpreset.json) loads", vv_engine_load_preset_json(f, wrapped.c_str(), wrapped.size()) == 1); }        check("preset json loads", vv_engine_load_preset_json(f, pj, strlen(pj)) == 1);
        check("bad json rejected", vv_engine_load_preset_json(f, "{nope", 5) == 0);
        std::vector<float> a(48000), b(48000); vv_engine_render(f, a.data(), b.data(), 128);
        check("preset picked up by render", vv_engine_has_preset(f) == 1);
        vv_engine_note_on(f, 60, 0.8f); for (int i = 0; i < 48000; i += 480) vv_engine_render(f, &a[i], &b[i], 480);
        check("full engine plays a note (odd block size)", rms(a, 24000, 48000) > 0.01);
        vv_engine_note_off(f, 60); for (int k = 0; k < 40; k++) vv_engine_render(f, a.data(), b.data(), 4800);
        check("full engine releases to silence", rms(a, 0, 4800) < 1e-4);
        vv_engine_set_arp(f, 1, 0); vv_engine_note_on(f, 60, 0.8f); vv_engine_note_on(f, 64, 0.8f); for (int i = 0; i < 48000; i += 480) vv_engine_render(f, &a[i], &b[i], 480);
        check("arp through the ABI sounds", rms(a, 24000, 48000) > 0.005);
        // live edits by path: level 0 silences, level back up restores; exported JSON reflects the edit
        check("set_value level 0 -> silent", [&] { vv_engine_set_value(f, "layers.WT.oscs.0.level", "0"); vv_engine_note_off(f, 60); vv_engine_note_off(f, 64); vv_engine_set_arp(f, 0, 0); for (int k = 0; k < 30; k++) vv_engine_render(f, a.data(), b.data(), 4800);
            vv_engine_note_on(f, 62, 0.8f); for (int k = 0; k < 8; k++) vv_engine_render(f, a.data(), b.data(), 4800); return rms(a, 0, 4800) < 1e-4; }());
        check("set_value level 0.8 -> sound", [&] { vv_engine_set_value(f, "layers.WT.oscs.0.level", "0.8"); vv_engine_note_off(f, 62); for (int k = 0; k < 20; k++) vv_engine_render(f, a.data(), b.data(), 4800);
            vv_engine_note_on(f, 62, 0.8f); for (int k = 0; k < 4; k++) vv_engine_render(f, a.data(), b.data(), 4800); return rms(a, 0, 4800) > 0.01; }());
        check("get_value returns the edit", [&] { char buf[64]; size_t n = vv_engine_get_value(f, "layers.WT.oscs.0.level", buf, sizeof buf); return n > 0 && atof(buf) == 0.8; }());
        check("export is valid preset json", [&] { size_t n = vv_engine_get_value(f, "", nullptr, 0); std::vector<char> big(n + 1); vv_engine_get_value(f, "", big.data(), big.size()); return n > 20 && vv_engine_load_preset_json(f, big.data(), n) == 1; }());
        // host automation: set_param -> pump -> preset paths; sync back from preset
        auto gv = [&](const char* path) { char buf[128] = {0}; vv_engine_get_value(f, path, buf, sizeof buf); return atof(buf); };
        check("automation: osc level", [&] { vv_engine_set_param(f, vv::kP_wt_osc1_level, 0.25f); return vv_engine_pump_params(f) >= 1 && std::fabs(gv("layers.WT.oscs.0.level") - 0.25) < 1e-6; }());
        check("automation: filter cutoff scaling", [&] { vv_engine_set_param(f, vv::kP_wavetable_filter_cutoff, 0.5f); vv_engine_pump_params(f); return std::fabs(gv("layers.WT.filter.cutoff") - (20 + 0.5 * 17980)) < 1; }());
        check("automation: amp release scaling", [&] { vv_engine_set_param(f, vv::kP_wavetable_amp_release, 0.5f); vv_engine_pump_params(f); return std::fabs(gv("layers.WT.env.r") - (0.001 + 0.5 * 3.0)) < 1e-3; }());
        check("automation: lfo rate scaling", [&] { vv_engine_set_param(f, vv::kP_lfo_1_rate_hz, 1.0f); vv_engine_pump_params(f); return std::fabs(gv("lfo.lfo1.hz") - 20.0) < 1e-3; }());
        check("automation: tempo", [&] { vv_engine_set_param(f, vv::kP_tempo, 90.f); vv_engine_pump_params(f); return std::fabs(gv("bpm") - 90) < 1e-6; }());
        check("automation: reverb algorithm creates stage", [&] { vv_engine_set_param(f, vv::kP_reverb_algorithm, 3); vv_engine_pump_params(f); vv_engine_set_param(f, vv::kP_reverb_mix, 0.6f); vv_engine_pump_params(f);
            char buf[64] = {0}; vv_engine_get_value(f, "fxStages.0.params.algo", buf, sizeof buf); return std::string(buf) == "\"kristal\"" && std::fabs(gv("fxStages.0.params.mix") - 0.6) < 1e-6; }());
        check("automation: reverb off disables", [&] { vv_engine_set_param(f, vv::kP_reverb_algorithm, 0); vv_engine_pump_params(f); char buf[16] = {0}; vv_engine_get_value(f, "fxStages.0.enabled", buf, sizeof buf); return std::string(buf) == "false"; }());
        check("automation: sampler start/end stay ordered", [&] { vv_engine_set_param(f, vv::kP_smp_slot1_end, 0.4f); vv_engine_pump_params(f); vv_engine_set_param(f, vv::kP_smp_slot1_start, 0.9f); vv_engine_pump_params(f); return gv("layers.SMP.oscs.0.start") < gv("layers.SMP.oscs.0.end"); }());
        check("automation: audio still sane after edits", [&] { vv_engine_set_param(f, vv::kP_wt_osc1_level, 0.8f); vv_engine_pump_params(f); vv_engine_note_on(f, 64, 0.8f); for (int k = 0; k < 6; k++) vv_engine_render(f, a.data(), b.data(), 4800); for (int i = 0; i < 4800; i++) if (!std::isfinite(a[i])) return false; return true; }());
        check("pump with nothing dirty is a no-op", vv_engine_pump_params(f) == 0);
        check("sync from preset moves params", [&] { vv_engine_set_value(f, "layers.WT.oscs.0.level", "0.33"); vv_engine_sync_params_from_preset(f); return std::fabs(vv_engine_get_param(f, vv::kP_wt_osc1_level) - 0.33f) < 1e-5 && vv_engine_pump_params(f) == 0; }());
        vv_engine_destroy(f);
    }
    // ---- abrupt interruption behaviour (what an iOS call / route change / backgrounding does to the engine)
    {
        VVEngine* g = vv_engine_create(48000);
        {   // a sine wavetable preset (the engine is silent until a preset is loaded)
            std::vector<unsigned char> wt(8 + 256 * 4); uint32_t fc = 1, fs = 256; memcpy(wt.data(), &fc, 4); memcpy(wt.data() + 4, &fs, 4);
            for (int i = 0; i < 256; i++) { float x = (float)std::sin(6.2831853 * i / 256); memcpy(wt.data() + 8 + i * 4, &x, 4); }
            vv_engine_load_wavetable(g, "analogMorph", wt.data(), wt.size());
            const char* pj2 = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8}]}},\"bpm\":120}";
            vv_engine_load_preset_json(g, pj2, strlen(pj2)); { std::vector<float> w(512), w2(512); vv_engine_render(g, w.data(), w2.data(), 512); }
        }
        auto finite = [](const std::vector<float>& v, int n) { for (int i = 0; i < n; i++) if (!std::isfinite(v[i]) || std::fabs(v[i]) > 8.f) return false; return true; };
        std::vector<float> a(48000), b(48000); uint8_t n1[3] = {0x90, 60, 100}, n2[3] = {0x90, 67, 100};
        vv_engine_midi(g, n1, 3); vv_engine_midi(g, n2, 3);
        for (int i = 0; i < 24000; i += 480) vv_engine_render(g, &a[i], &b[i], 480);
        check("interruption: playing before the cut", rms(a, 12000, 24000) > 0.01);
        vv_engine_all_notes_off(g);                                   // interruption began: AU/app releases every note
        for (int i = 0; i < 48000; i += 480) vv_engine_render(g, &a[i], &b[i], 480);
        check("interruption: all notes off decays to silence", rms(a, 43200, 48000) < 1e-4 && finite(a, 48000));
        vv_engine_render(g, a.data(), b.data(), 0);                   // hosts may pull zero frames
        check("interruption: zero-frame render is harmless", true);
        vv_engine_midi(g, n1, 3);
        for (int i = 0; i < 24000; i += 128) vv_engine_render(g, &a[i], &b[i], 128);
        check("interruption: engine plays again after resume", rms(a, 12000, 24000) > 0.01 && finite(a, 24000));
        vv_engine_prepare(g, 44100); vv_engine_prepare(g, 48000);     // route change: host re-allocates render resources at another rate, twice
        vv_engine_midi(g, n2, 3);
        for (int i = 0; i < 24000; i += 512) vv_engine_render(g, &a[i], &b[i], 512);
        check("interruption: sample-rate change keeps the engine alive", finite(a, 24000) && rms(a, 12000, 24000) > 0.01);
        for (int k = 0; k < 200; k++) { vv_engine_midi(g, n1, 3); vv_engine_render(g, a.data(), b.data(), 64); vv_engine_all_notes_off(g); }
        vv_engine_render(g, a.data(), b.data(), 512);
        check("interruption: 200 rapid on/panic cycles stay finite", finite(a, 512));
        vv_engine_destroy(g);
    }
    {   // XY pad positions are matrix sources: xy1x (+100%) drives the WT oscillator level
        VVEngine* h = vv_engine_create(48000);
        std::vector<unsigned char> wt(8 + 256 * 4); uint32_t fc = 1, fs = 256; memcpy(wt.data(), &fc, 4); memcpy(wt.data() + 4, &fs, 4);
        for (int i = 0; i < 256; i++) { float x = (float)std::sin(6.2831853 * i / 256); memcpy(wt.data() + 8 + i * 4, &x, 4); }
        vv_engine_load_wavetable(h, "analogMorph", wt.data(), wt.size());
        const char* pj3 = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.5}]}},\"bpm\":120,\"connections\":[{\"source\":\"xy1x\",\"dest\":\"WT:level\",\"amount\":100}]}";
        check("xy source preset loads", vv_engine_load_preset_json(h, pj3, strlen(pj3)) == 1);
        std::vector<float> a(24000), b(24000);
        auto level = [&](float x) { vv_engine_all_notes_off(h); for (int k = 0; k < 10; k++) vv_engine_render(h, a.data(), b.data(), 4800); vv_engine_set_xy(h, 0, x); uint8_t on[3] = { 0x90, 60, 100 }; vv_engine_midi(h, on, 3); for (int k = 0; k < 5; k++) vv_engine_render(h, &a[k * 4800], &b[k * 4800], 4800); return rms(a, 12000, 24000); };
        const double low = level(0.0f), high = level(1.0f);
        check("xy source modulates the level (x = 1 louder than x = 0)", high > low * 2 && high > 0.01);
        {   // App MIDI channel: channel 3 selected -> a note on channel 1 is ignored, on channel 3 it sounds; Omni accepts both
            auto sound = [&](uint8_t status) { vv_engine_all_notes_off(h); for (int k = 0; k < 10; k++) vv_engine_render(h, a.data(), b.data(), 4800); uint8_t on[3] = { status, 60, 100 }; vv_engine_midi(h, on, 3); for (int k = 0; k < 5; k++) vv_engine_render(h, &a[k * 4800], &b[k * 4800], 4800); return rms(a, 12000, 24000); };
            vv_engine_set_midi_channel(h, 3);
            const double wrong = sound(0x90), right = sound(0x92);
            vv_engine_set_midi_channel(h, 0); const double omni = sound(0x90);
            check("MIDI channel filter: other channel silent, selected channel sounds, Omni accepts all", wrong < 1e-6 && right > 0.01 && omni > 0.01);
        }
        vv_engine_destroy(h);
    }
    {   // native-only parameters: SID voice 1 waveform round-trips through the param bridge
        VVEngine* h = vv_engine_create(48000);
        const char* sj = "{\"layers\":{\"SID\":{\"type\":\"sid\",\"oscs\":[{\"enabled\":true,\"wave\":\"triangle\"},{},{}]}},\"bpm\":120}";
        check("sid preset loads", vv_engine_load_preset_json(h, sj, strlen(sj)) == 1);
        { std::vector<float> ra(512), rb(512); vv_engine_render(h, ra.data(), rb.data(), 512); }   // the render thread adopts the pending preset
        vv_engine_set_param(h, vv::kP_sid_voice1_wave, 2.0f); vv_engine_pump_params(h);
        char buf[64] = {0}; vv_engine_get_value(h, "layers.SID.oscs.0.wave", buf, sizeof buf);
        check("sid_voice1_wave param writes the preset (pulse)", std::string(buf) == "\"pulse\"");
        std::printf("   sid wave value read back: %s\n", buf);
        vv_engine_sync_params_from_preset(h);
        check("sid_voice1_wave reads back as 2", std::fabs(vv_engine_get_param(h, vv::kP_sid_voice1_wave) - 2.0f) < 1e-6f);
        vv_engine_destroy(h);
    }
    {   // Stereo Spread FX: L untouched, R Haas-delayed; mix 0 = bypass (mono stays mono)
        std::vector<unsigned char> wt(8 + 256 * 4); uint32_t fc = 1, fs = 256; memcpy(wt.data(), &fc, 4); memcpy(wt.data() + 4, &fs, 4);
        for (int i = 0; i < 256; i++) { float x = (float)std::sin(6.2831853 * i / 256); memcpy(wt.data() + 8 + i * 4, &x, 4); }
        auto diff = [&](double spread, double mix) {
            VVEngine* h = vv_engine_create(48000); vv_engine_load_wavetable(h, "analogMorph", wt.data(), wt.size());
            char pj[600]; snprintf(pj, sizeof pj, "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.5}]}},\"bpm\":120,\"fxStages\":[{\"type\":\"spread\",\"enabled\":true,\"params\":{\"spread\":%g,\"mix\":%g},\"sends\":{\"WT\":true}}]}", spread, mix);
            vv_engine_load_preset_json(h, pj, strlen(pj));
            std::vector<float> a(24000), b(24000); vv_engine_note_on(h, 57, 0.9f);
            for (int k = 0; k < 5; k++) vv_engine_render(h, &a[k * 4800], &b[k * 4800], 4800);
            double d = 0, e = 0; for (int i = 12000; i < 24000; i++) { d += std::fabs(a[i] - b[i]); e += std::fabs(a[i]); }
            vv_engine_destroy(h); return std::make_pair(d, e);
        };
        const auto wide = diff(2.0, 1.0), bypass = diff(2.0, 0.0), none = diff(0.0, 1.0);
        check("spread: wide stage makes L and R differ", wide.second > 1 && wide.first > 0.2 * wide.second);
        check("spread: mix 0 keeps the signal mono", bypass.second > 1 && bypass.first < 1e-3 * bypass.second);
        check("spread: zero spread keeps the signal mono", none.first < 1e-3 * none.second);
    }
    {   // WT ring mod: carrier 220 Hz x modulator 1.5x -> sum / difference tones (110 Hz, 550 Hz), carrier and modulator themselves cancelled
        std::vector<unsigned char> wt(8 + 256 * 4); uint32_t fc = 1, fs = 256; memcpy(wt.data(), &fc, 4); memcpy(wt.data() + 4, &fs, 4);
        for (int i = 0; i < 256; i++) { float x = (float)std::sin(6.2831853 * i / 256); memcpy(wt.data() + 8 + i * 4, &x, 4); }
        VVEngine* h = vv_engine_create(48000); vv_engine_load_wavetable(h, "analogMorph", wt.data(), wt.size());
        const char* pj = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"ringModEnabled\":true,\"ringMod\":{\"interval\":\"p5\",\"wave\":\"sine\",\"bleed\":0,\"velDepth\":0,\"lpgCutoff\":16000,\"lpgKeytrack\":1},\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.5},{\"level\":0.5}]}},\"bpm\":120}";
        vv_engine_load_preset_json(h, pj, strlen(pj)); vv_engine_note_on(h, 57, 1.0f);
        std::vector<float> a(48000), b(48000); for (int k2 = 0; k2 < 10; k2++) vv_engine_render(h, &a[k2 * 4800], &b[k2 * 4800], 4800);
        auto gz = [&](double f) { double re = 0, im = 0; for (int i = 24000; i < 48000; i++) { re += a[i] * std::cos(6.2831853 * f * i / 48000); im += a[i] * std::sin(6.2831853 * f * i / 48000); } return std::sqrt(re * re + im * im) / 12000; };
        const double d110 = gz(110), s550 = gz(550), c220 = gz(220), m330 = gz(330);
        check("ring mod: difference tone (110 Hz) present", d110 > 0.01);
        check("ring mod: sum tone (550 Hz) present", s550 > 0.005);
        check("ring mod: carrier and modulator suppressed", c220 < d110 * 0.1 && m330 < d110 * 0.1);
    }
    {   // SID live PWM: an LFO routed to SID:voice1Pwm moves the pulse width of a held note (static pulse is a fixed waveform; PWM makes it drift)
        auto run = [&](const char* conns) { VVEngine* h = vv_engine_create(48000);
            std::string pj = std::string("{\"layers\":{\"SID\":{\"type\":\"sid\",\"oscs\":[{\"enabled\":true,\"wave\":\"pulse\",\"pulseWidth\":0.5,\"level\":0.8},{\"enabled\":false},{\"enabled\":false}]}},\"bpm\":120,\"lfos\":{\"lfo1\":{\"hz\":3,\"amount\":1,\"waveform\":\"sine\"}}") + conns + "}";
            vv_engine_load_preset_json(h, pj.c_str(), pj.size()); vv_engine_note_on(h, 57, 0.9f); std::vector<float> a(4800), b(4800); std::vector<double> rms;
            for (int k2 = 0; k2 < 40; k2++) { vv_engine_render(h, a.data(), b.data(), 4800); double s2 = 0; for (float x : a) s2 += x; rms.push_back(s2 / 4800); }   // block mean = DC of the pulse = 2*pw-1
            vv_engine_destroy(h); return rms; };
        const auto st = run(""), pw = run(",\"connections\":[{\"source\":\"lfo1\",\"dest\":\"SID:voice1Pwm\",\"amount\":45}]");
        auto spread = [](const std::vector<double>& v) { double lo = 1e9, hi = -1e9; for (size_t i = 10; i < v.size(); i++) { lo = std::min(lo, v[i]); hi = std::max(hi, v[i]); } return hi - lo; };
        check("SID PWM: static pulse width keeps a steady level", spread(st) < 0.01);
        check("SID PWM: modulated pulse width changes the waveform over time", spread(pw) > 0.02 + 3 * spread(st));
    }
    {   // SID WT arp: steps alternate +0 / +12 semitones, so a triangle at A3 (220 Hz) gains energy at 440 Hz; arp off (or voice arpOn off) does not
        auto run = [&](bool on, bool arpOn) { VVEngine* h = vv_engine_create(48000);
            std::string pj = std::string("{\"layers\":{\"SID\":{\"type\":\"sid\",\"wtArp\":{\"on\":") + (on ? "true" : "false") + ",\"speedHz\":25,\"hardRestart\":false,\"steps\":[{\"wave\":\"triangle\",\"mode\":\"relative\",\"value\":0,\"gate\":true},{\"wave\":\"triangle\",\"mode\":\"relative\",\"value\":12,\"gate\":true}]},\"oscs\":[{\"enabled\":true,\"wave\":\"triangle\",\"level\":0.8,\"arpOn\":" + (arpOn ? "true" : "false") + "},{\"enabled\":false},{\"enabled\":false}]}},\"bpm\":120}";
            vv_engine_load_preset_json(h, pj.c_str(), pj.size()); vv_engine_note_on(h, 57, 0.9f); std::vector<float> a(96000), b(96000); for (int k2 = 0; k2 < 20; k2++) vv_engine_render(h, &a[k2 * 4800], &b[k2 * 4800], 4800);
            auto gz = [&](double f) { double re = 0, im = 0; for (int i = 24000; i < 96000; i++) { re += a[i] * std::cos(6.2831853 * f * i / 48000); im += a[i] * std::sin(6.2831853 * f * i / 48000); } return std::sqrt(re * re + im * im) / 36000; };
            const double r = gz(440) / (gz(220) + 1e-9); vv_engine_destroy(h); return r; };
        const double off = run(false, true), on = run(true, true), noVoice = run(true, false);
        check("SID arp: off keeps a plain note (little 440 Hz)", off < 0.2);
        check("SID arp: on adds the +12 step (strong 440 Hz)", on > 0.5);
        check("SID arp: voice with arpOn=false ignores the arp", noVoice < 0.2);
    }
    {   // Chord analyzer: names the sounding chord, keeps the last label when notes are released, ignores single notes
        VVEngine* h = vv_engine_create(48000);
        std::string pj = "{\"layers\":{\"SID\":{\"type\":\"sid\",\"oscs\":[{\"enabled\":true,\"wave\":\"triangle\",\"level\":0.8},{\"enabled\":false},{\"enabled\":false}]}},\"bpm\":120}";
        vv_engine_load_preset_json(h, pj.c_str(), pj.size());
        std::vector<float> a(4800), b(4800); auto lbl = [&]() { char buf[64]; vv_engine_chord_label(h, buf, 64); return std::string(buf); };
        vv_engine_render(h, a.data(), b.data(), 4800);
        vv_engine_note_on(h, 60, 0.9f); vv_engine_render(h, a.data(), b.data(), 4800);
        check("chord: a single note names nothing", lbl() == "");
        vv_engine_note_on(h, 64, 0.9f); vv_engine_note_on(h, 67, 0.9f); vv_engine_render(h, a.data(), b.data(), 4800);
        check("chord: C-E-G = C Major", lbl() == "C Major");
        vv_engine_note_on(h, 70, 0.9f); vv_engine_render(h, a.data(), b.data(), 4800);
        check("chord: + Bb = C Dominant 7", lbl() == "C Dominant 7");
        vv_engine_note_off(h, 70); vv_engine_note_off(h, 67); vv_engine_note_off(h, 64); vv_engine_note_off(h, 60);
        for (int k2 = 0; k2 < 20; k2++) vv_engine_render(h, a.data(), b.data(), 4800);
        check("chord: label stays after release", lbl() == "C Dominant 7");
        vv_engine_note_on(h, 69, 0.9f); vv_engine_note_on(h, 72, 0.9f); vv_engine_render(h, a.data(), b.data(), 4800);
        check("chord: new dyad replaces the label (A minor 3rd)", lbl() == "A (minor 3rd)");
        vv_engine_destroy(h);
    }
    {   // Preset-embedded content (web snap.customWavetables / snap.sampleLibrary) is registered with the engine
        auto b64 = [](const std::vector<unsigned char>& d) { static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string o; for (size_t i = 0; i < d.size(); i += 3) { unsigned v = d[i] << 16 | (i + 1 < d.size() ? d[i + 1] << 8 : 0) | (i + 2 < d.size() ? d[i + 2] : 0); o += A[v >> 18 & 63]; o += A[v >> 12 & 63]; o += i + 1 < d.size() ? A[v >> 6 & 63] : '='; o += i + 2 < d.size() ? A[v & 63] : '='; } return o; };
        auto gz = [](const std::vector<float>& a, double f, int from) { double re = 0, im = 0; for (int i = from; i < (int)a.size(); i++) { re += a[i] * std::cos(6.2831853 * f * i / 48000); im += a[i] * std::sin(6.2831853 * f * i / 48000); } return std::sqrt(re * re + im * im) / (a.size() - from); };
        {   // custom wavetable: a square frame carries a strong 3rd harmonic, the sine default does not
            auto run = [&](bool square) { VVEngine* h = vv_engine_create(48000); std::string fr = "["; for (int i = 0; i < 1024; i++) { fr += (i ? "," : ""); fr += square ? (i < 512 ? "1" : "-1") : std::to_string(std::sin(6.2831853 * i / 1024)); } fr += "]";
                std::string pj = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"custom:t1\",\"level\":0.8}]}},\"customWavetables\":[{\"id\":\"custom:t1\",\"name\":\"T\",\"frames\":[" + fr + "]}],\"bpm\":120}";
                vv_engine_load_preset_json(h, pj.c_str(), pj.size()); std::vector<float> a(48000), b(48000); vv_engine_note_on(h, 57, 0.9f); for (int i = 0; i < 48000; i += 480) vv_engine_render(h, &a[i], &b[i], 480);
                const double r = gz(a, 660, 24000) / (gz(a, 220, 24000) + 1e-9); vv_engine_destroy(h); return r; };
            const double sq = run(true), sn = run(false);
            check("custom wavetable in preset: square frame has a 3rd harmonic (~1/3)", sq > 0.2 && sq < 0.5);
            check("custom wavetable in preset: sine frame has none", sn < 0.05);
        }
        {   // embedded sample: 1 kHz sine WAV (16-bit mono) used by a sampler slot by name
            std::vector<unsigned char> w(44 + 2 * 12000); auto u32 = [&](int o, uint32_t v) { memcpy(&w[o], &v, 4); }; auto u16 = [&](int o, uint16_t v) { memcpy(&w[o], &v, 2); };
            memcpy(&w[0], "RIFF", 4); u32(4, 36 + 24000); memcpy(&w[8], "WAVEfmt ", 8); u32(16, 16); u16(20, 1); u16(22, 1); u32(24, 48000); u32(28, 96000); u16(32, 2); u16(34, 16); memcpy(&w[36], "data", 4); u32(40, 24000);
            for (int i = 0; i < 12000; i++) { int16_t x = (int16_t)(20000 * std::sin(6.2831853 * 1000 * i / 48000)); memcpy(&w[44 + 2 * i], &x, 2); }
            VVEngine* h = vv_engine_create(48000);
            std::string pj = "{\"layers\":{\"SMP\":{\"type\":\"sample\",\"oscs\":[{\"name\":\"mine\",\"enabled\":true,\"level\":0.8}]}},\"sampleLibrary\":[{\"name\":\"mine\",\"base64\":\"" + b64(w) + "\"}],\"bpm\":120}";
            vv_engine_load_preset_json(h, pj.c_str(), pj.size()); std::vector<float> a(12000), b(12000); vv_engine_note_on(h, 60, 0.9f); for (int i = 0; i < 12000; i += 480) vv_engine_render(h, &a[i], &b[i], 480);
            double e = 0; for (float x : a) e += x * x; check("embedded sampleLibrary entry plays in a sampler slot", e > 1.0);
            vv_engine_destroy(h);
        }
    }
    {   // Layer SOLO / MUTE (web buttons): mute silences, solo of another layer silences this one, solo of itself keeps it, release restores
        std::vector<unsigned char> wt(8 + 256 * 4); uint32_t fc = 1, fs = 256; memcpy(wt.data(), &fc, 4); memcpy(wt.data() + 4, &fs, 4);
        for (int i = 0; i < 256; i++) { float x = (float)std::sin(6.2831853 * i / 256); memcpy(wt.data() + 8 + i * 4, &x, 4); }
        VVEngine* h = vv_engine_create(48000); vv_engine_load_wavetable(h, "analogMorph", wt.data(), wt.size());
        const char* pj = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.5},{\"level\":0}]}},\"bpm\":120}";
        vv_engine_load_preset_json(h, pj, strlen(pj)); vv_engine_note_on(h, 57, 0.9f);
        auto rms = [&](int blocks) { std::vector<float> a(4800), b(4800); double s = 0; for (int k = 0; k < blocks; k++) { vv_engine_render(h, a.data(), b.data(), 4800); if (k == blocks - 1) for (int i = 0; i < 4800; i++) s += a[i] * a[i]; } return std::sqrt(s / 4800); };
        const double base = rms(4); check("solo/mute: baseline plays", base > 0.01);
        vv_engine_set_solo_mute(h, 0, 1 << 0); check("solo/mute: muted WT is silent", rms(3) < base * 0.001);
        vv_engine_set_solo_mute(h, 1 << 1, 0); check("solo/mute: solo on FM silences WT", rms(3) < base * 0.001);
        vv_engine_set_solo_mute(h, 1 << 0, 0); check("solo/mute: solo on WT keeps WT", std::fabs(rms(3) / base - 1) < 0.02);
        vv_engine_set_solo_mute(h, 0, 0); check("solo/mute: release restores", std::fabs(rms(3) / base - 1) < 0.02);
    }
    {   // Mixer window: fader gain, MIX:<track> matrix destination, live pan, meters, host parameter
        std::vector<unsigned char> wt(8 + 256 * 4); uint32_t fc = 1, fs = 256; memcpy(wt.data(), &fc, 4); memcpy(wt.data() + 4, &fs, 4);
        for (int i = 0; i < 256; i++) { float x = (float)std::sin(6.2831853 * i / 256); memcpy(wt.data() + 8 + i * 4, &x, 4); }
        auto mk = [&](const char* extra, const char* conns) { VVEngine* h = vv_engine_create(48000); vv_engine_load_wavetable(h, "analogMorph", wt.data(), wt.size());
            char pj[900]; snprintf(pj, sizeof pj, "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.5%s},{\"level\":0}]}},\"bpm\":120%s}", extra, conns);
            vv_engine_load_preset_json(h, pj, strlen(pj)); return h; };
        auto run = [&](VVEngine* h, double& rl, double& rr) { std::vector<float> a(24000), b(24000); vv_engine_note_on(h, 57, 0.9f);
            for (int k = 0; k < 5; k++) vv_engine_render(h, &a[k * 4800], &b[k * 4800], 4800);
            double sl = 0, sr = 0; for (int i = 12000; i < 24000; i++) { sl += a[i] * a[i]; sr += b[i] * b[i]; } rl = std::sqrt(sl / 12000); rr = std::sqrt(sr / 12000); };
        double l0, r0, l1, r1;
        VVEngine* h0 = mk("", ""); run(h0, l0, r0);
        check("mixer: baseline plays", l0 > 0.01);
        { char b[64]; vv_engine_set_value(h0, "mixerGains.WT1", "-6"); }
        VVEngine* h1 = [&] { VVEngine* h = vv_engine_create(48000); vv_engine_load_wavetable(h, "analogMorph", wt.data(), wt.size());
            const char* pj = "{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.5},{\"level\":0}]}},\"bpm\":120,\"mixerGains\":{\"WT1\":-6.0206}}";
            vv_engine_load_preset_json(h, pj, strlen(pj)); return h; }();
        run(h1, l1, r1);
        check("mixer: -6.02 dB fader halves the level", std::fabs(l1 / l0 - 0.5) < 0.01);
        const float meter = vv_engine_mixer_level(h1, 0);
        check("mixer: meter for WT1 reports signal, other tracks silent", meter > 0.01f && vv_engine_mixer_level(h1, 1) == 0.f && vv_engine_mixer_level(h1, 23) == 0.f);
        VVEngine* h2 = mk("", ",\"mixerGains\":{\"WT1\":-60}"); run(h2, l1, r1);
        check("mixer: -60 dB fader is (nearly) silent", l1 < l0 * 0.002);
        VVEngine* h3 = mk("", ",\"connections\":[{\"source\":\"velocity\",\"dest\":\"MIX:WT1\",\"amount\":-100}]");
        { std::vector<float> a(4800), b(4800); for (int k = 0; k < 10; k++) vv_engine_render(h3, a.data(), b.data(), 4800); }
        run(h3, l1, r1);
        check("mixer: MIX:WT1 modulation destination parses and moves the level", std::fabs(l1 - l0) > 0.02 * l0);
        const char* fxPass = ",\"fxStages\":[{\"type\":\"spread\",\"enabled\":true,\"params\":{\"spread\":0,\"mix\":0},\"sends\":{\"WT\":true}}]";   // the layer EQ is mono (as on the web), so pan is audible through an FX send
        VVEngine* h4 = mk(",\"pan\":-1", fxPass); run(h4, l1, r1);
        check("mixer: osc pan -1 puts the sound on the left", l1 > 0.01 && r1 < l1 * 0.05);
        { vv_engine_set_value(h4, "layers.WT.oscs.0.pan", "1"); vv_engine_sync_params_from_preset(h4); std::vector<float> a(24000), b(24000);
          for (int k = 0; k < 5; k++) vv_engine_render(h4, &a[k * 4800], &b[k * 4800], 4800);
          double sl = 0, sr = 0; for (int i = 12000; i < 24000; i++) { sl += a[i] * a[i]; sr += b[i] * b[i]; }
          check("mixer: pan edit moves a held note live", sr > 4 * sl && sr > 1); }
        VVEngine* h5 = mk("", ""); { std::vector<float> a(4800), b(4800); vv_engine_render(h5, a.data(), b.data(), 4800); vv_engine_set_param(h5, vv::kP_mixer_wt1_gain, -60.f); vv_engine_pump_params(h5); }
        run(h5, l1, r1);
        check("mixer: host parameter mixer_wt1_gain drives the fader", l1 < l0 * 0.002);
        for (VVEngine* h : { h0, h1, h2, h3, h4, h5 }) vv_engine_destroy(h);
    }
    {   // 16-lane drum sequencer: lane i one-shots sampler slot i on straight sixteenths over its own length
        std::vector<unsigned char> w(44 + 2 * 4800); auto u32 = [&](int o, uint32_t v) { memcpy(&w[o], &v, 4); }; auto u16 = [&](int o, uint16_t v) { memcpy(&w[o], &v, 2); };
        memcpy(&w[0], "RIFF", 4); u32(4, 36 + 9600); memcpy(&w[8], "WAVEfmt ", 8); u32(16, 16); u16(20, 1); u16(22, 1); u32(24, 48000); u32(28, 96000); u16(32, 2); u16(34, 16); memcpy(&w[36], "data", 4); u32(40, 9600);
        for (int i = 0; i < 4800; i++) { int16_t x = (int16_t)(12000 * std::sin(6.2831853 * 440 * i / 48000.0)); memcpy(&w[44 + 2 * i], &x, 2); }
        auto run = [&](bool on, bool playing, double& first, double& total) {
            VVEngine* h = vv_engine_create(48000); check("drum: wav loads", vv_engine_load_sample_wav(h, "blip", w.data(), w.size()) >= 0);
            char pj[800]; snprintf(pj, sizeof pj, "{\"layers\":{\"SMP\":{\"type\":\"sample\",\"oscs\":[{\"name\":\"blip\",\"level\":0.8,\"loop\":false,\"root\":60}]}},\"bpm\":120,\"drumSeq\":{\"on\":%s,\"lanes\":[{\"length\":4,\"steps\":[false,false,true,false]}]}}", on ? "true" : "false");
            vv_engine_load_preset_json(h, pj, strlen(pj)); std::vector<float> a(512), b(512); vv_engine_render(h, a.data(), b.data(), 512);
            vv_engine_start_sequencer(h, playing ? 1 : 0);
            first = 0; total = 0; for (int blk = 0; blk < 375; blk++) { vv_engine_render(h, a.data(), b.data(), 512); double e = 0; for (int i = 0; i < 512; i++) e += std::fabs(a[i]); total += e; if (blk < 14) first += e; }
            vv_engine_destroy(h); };
        double f1, t1, f0, t0, f2, t2; run(true, true, f1, t1); run(false, true, f0, t0); run(true, false, f2, t2);
        check("drum: lane step plays the slot while the transport runs", t1 > 50);
        check("drum: silent when the sequencer is off or the transport stopped", t0 < 1 && t2 < 1);
        check("drum: step 3 of a 4-step lane lands after ~2 sixteenths (nothing in the first 0.25 s)", f1 < 1);
    }
    {   // Vektur Void C ABI: snapshot layout and a short scripted game
        VVVoid* v = vv_void_create(99); std::vector<float> snap(16384);
        int n = vv_void_snapshot(v, snap.data(), (int) snap.size()); check("void: title snapshot has header + counts", n == 39 && snap[0] == 0);
        vv_void_step(v, 1.0 / 60, 0, 0, 0, 0, 1);
        for (int i = 0; i < 600; i++) vv_void_step(v, 1.0 / 60, 0, 0, 1, 0, 0);
        n = vv_void_snapshot(v, snap.data(), (int) snap.size());
        check("void: playing after START, level 1", n >= 39 && snap[0] == 1 && snap[4] == 1);
        const int nb = (int) snap[30], ne = (int) snap[33]; check("void: plasma bullets exist and enemies spawned", nb > 0 && ne >= 0 && snap[3] >= 0);
        char bn[64]; check("void: banner text", vv_void_banner(v, bn, sizeof bn) > 0 && std::string(bn).find("LEVEL") == 0);
        check("void: too-small buffer is refused", vv_void_snapshot(v, snap.data(), 10) == -1);
        vv_void_destroy(v);
    }
    std::printf("%s\n", fails ? "ENGINE TESTS FAILED" : "all engine tests passed"); return fails ? 1 : 0;
}
