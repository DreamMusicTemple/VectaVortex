// Wavetable authoring tools (harmonics / formula / draw / slice) and offline sample tools (stretch / warp / spectral), through the C ABI.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "vv_engine.h"
#include "vv/voice_dsp.hpp"
#include "vv/daw_recorder.hpp"
static double gz(const std::vector<float>& a, double f, int sr) { double re = 0, im = 0; for (size_t i = 0; i < a.size(); i++) { re += a[i] * std::cos(6.283185307 * f * i / sr); im += a[i] * std::sin(6.283185307 * f * i / sr); } return std::sqrt(re * re + im * im) / a.size(); }
static double frameH(const float* f, int h) { double re = 0, im = 0; for (int i = 0; i < 1024; i++) { re += f[i] * std::cos(6.283185307 * h * i / 1024); im += f[i] * std::sin(6.283185307 * h * i / 1024); } return std::sqrt(re * re + im * im) / 1024; }
int main() {
    int fails = 0; auto check = [&](const char* n, bool ok) { std::printf("%-62s %s\n", n, ok ? "OK" : "FAIL"); if (!ok) fails++; };
    std::vector<float> f(1024 * 256); char err[160];
    {   // harmonics: amps {1, 0, 0.5} = fundamental + 3rd harmonic at ratio 0.5, normalised to peak 1
        const float amps[3] = {1.f, 0.f, 0.5f}; vv_wt_harmonics(amps, 3, f.data()); float mx = 0; for (int i = 0; i < 1024; i++) mx = std::fmax(mx, std::fabs(f[i]));
        check("harmonics: peak normalised to 1", std::fabs(mx - 1.f) < 1e-5); check("harmonics: 3rd/1st = 0.5, no 2nd", std::fabs(frameH(f.data(), 3) / frameH(f.data(), 1) - 0.5) < 0.01 && frameH(f.data(), 2) < 1e-4);
    }
    {   // formula
        int n = vv_wt_formula("Math.sin(2*Math.PI*x)", 32, f.data(), 256, err, 160); check("formula: plain sine = 1 frame", n == 1 && frameH(f.data(), 1) > 0.49);
        n = vv_wt_formula("Math.sin(2*Math.PI*x*(1+6*n))", 32, f.data(), 256, err, 160); check("formula: uses n -> 32 frames, last frame is the 7th harmonic", n == 32 && frameH(f.data() + 31 * 1024, 7) > 0.49 && frameH(f.data(), 1) > 0.49);
        n = vv_wt_formula("x < 0.5 ? 1 : -1", 8, f.data(), 256, err, 160); check("formula: ternary square", n == 1 && f[100] == 1.f && f[900] == -1.f);
        n = vv_wt_formula("(2 ** 3 ** 2 + (7 % 4) * -2 == 506) ? 1 : -1", 8, f.data(), 256, err, 160); check("formula: precedence and right-associative ** (= 506)", n == 1 && f[0] == 1.f);
        n = vv_wt_formula("Math.sin(", 8, f.data(), 256, err, 160); check("formula: syntax error reported", n == 0 && std::string(err).find("Formula error") == 0);
        n = vv_wt_formula("foo(x)", 8, f.data(), 256, err, 160); check("formula: unknown identifiers rejected", n == 0);
        n = vv_wt_formula("x > 0.5", 8, f.data(), 256, err, 160); check("formula: boolean result rejected like the web (must return a number)", n == 0 && std::string(err).find("number") != std::string::npos);
        n = vv_wt_formula("Math.pow(x, 2) + Math.max(0, Math.min(1, x*3)) - Math.abs(-0.2) + Math.floor(x*4)/4", 8, f.data(), 256, err, 160); check("formula: Math functions evaluate", n == 1);
        n = vv_wt_formula("   ", 8, f.data(), 256, err, 160); check("formula: empty rejected", n == 0);
    }
    {   // draw: straight ramp from (0,0) to (1,1) -> -1..1 saw
        const float pts[8] = {0, 0, 0.5f, 0.5f, 1, 1, 1, 1}; vv_wt_curve(pts, 2, f.data()); check("draw: ramp curve rises monotonically from -1 to ~+1", f[0] < -0.99f && f[1000] > 0.9f && f[512] > f[256]);
    }
    {   // slice: 4 chunks of audio -> 4 frames
        std::vector<float> a(8192); for (int i = 0; i < 8192; i++) a[i] = (i / 2048 % 2) ? 0.5f : -0.25f; int n = vv_wt_slice(a.data(), 8192, 4, f.data(), 256); check("slice: audio -> requested frame count", n == 4);
        check("slice: frames normalised", std::fabs(f[10] ) == 1.f);
    }
    // sample tools
    const int sr = 48000; std::vector<float> sine(sr); for (int i = 0; i < sr; i++) sine[i] = 0.6f * (float)std::sin(6.283185307 * 440 * i / sr);
    VVAudio* src = vv_audio_create(sr, 1, sr, sine.data(), nullptr);
    {
        VVAudio* s2 = vv_audio_stretch(src, 0, 2.0, 0, 4, 120, 1); const int len = vv_audio_length(s2); std::vector<float> o(len); vv_audio_read(s2, 0, o.data());
        check("stretch x2: length doubles", std::abs(len - 2 * sr) < 8); std::vector<float> mid(o.begin() + sr / 2, o.begin() + sr);
        check("stretch x2: pitch kept (440 Hz dominant)", gz(mid, 440, sr) > 4 * gz(mid, 330, sr) && gz(mid, 440, sr) > 0.15); vv_audio_free(s2);
        VVAudio* sp = vv_audio_stretch(src, 1, 1, 12, 4, 120, 1); const int l2 = vv_audio_length(sp); std::vector<float> o2(l2); vv_audio_read(sp, 0, o2.data());
        check("pitch +12 st keeps length", std::abs(l2 - sr) < 8); std::vector<float> m2(o2.begin() + sr / 4, o2.begin() + sr * 3 / 4); check("pitch +12 st: 880 Hz dominant", gz(m2, 880, sr) > 4 * gz(m2, 440, sr)); vv_audio_free(sp);
        VVAudio* fit = vv_audio_stretch(src, 2, 1, 0, 1, 120, 1); check("fit to 1 beat at 120 bpm = 0.5 s", std::abs(vv_audio_length(fit) - sr / 2) < 8); vv_audio_free(fit);
        const double mk[2] = {0.5, 0.75}; VVAudio* w = vv_audio_warp(src, mk, 1); check("warp: marker (0.5 -> 0.75) keeps total length", std::abs(vv_audio_length(w) - sr) < 8); vv_audio_free(w);
    }
    {   // spectral: zeroing all but the lowest band removes a 6 kHz tone, keeping a 200 Hz one
        std::vector<float> two(sr); for (int i = 0; i < sr; i++) two[i] = 0.3f * (float)(std::sin(6.283185307 * 200 * i / sr) + std::sin(6.283185307 * 6000 * i / sr));
        VVAudio* t = vv_audio_create(sr, 1, sr, two.data(), nullptr); const double bands[8] = {1, 0, 0, 0, 0, 0, 0, 0}; VVAudio* o = vv_audio_spectral(t, 0, 0, 0, bands, 8);
        std::vector<float> r(sr); vv_audio_read(o, 0, r.data()); std::vector<float> m(r.begin() + 8000, r.begin() + 40000);
        check("spectral: low band kept, 6 kHz removed", gz(m, 200, sr) > 0.1 && gz(m, 6000, sr) < 0.02); vv_audio_free(o); vv_audio_free(t);
    }
    {   // wav round trip
        const size_t n = vv_audio_to_wav16(src, nullptr, 0); std::vector<uint8_t> w(n); vv_audio_to_wav16(src, w.data(), n); VVAudio* back = vv_audio_from_wav(w.data(), n);
        check("wav16 round trip keeps length and rate", back && vv_audio_length(back) == sr && vv_audio_sample_rate(back) == sr); if (back) { std::vector<float> r(sr); vv_audio_read(back, 0, r.data()); check("wav16 round trip keeps the waveform", std::fabs(r[100] - sine[100]) < 2e-4); vv_audio_free(back); }
    }
    {   // glitch companding: 0 = plain linear crush; >0 follows the web worklet's mu-law blend
        vv::Glitch g; g.prepare(48000, 1); vv::Glitch::Params q; q.bitDepth = 4; float in[4] = {0.03f, 0.3f, -0.07f, 0.9f}, a[4], b[4], c[4];
        g.process(in, a, 4, q); g.reset(); q.companding = 1; g.process(in, b, 4, q); g.reset(); q.companding = 0; g.process(in, c, 4, q);
        bool lin = true, diff = false; double want = 0;
        for (int i = 0; i < 4; i++) { lin = lin && a[i] == c[i] && std::fabs(a[i] - std::round(in[i] * 16.0) / 16.0) < 1e-6; diff = diff || a[i] != b[i]; }
        { const double mu = 255, x = 0.03, comp = std::log(1 + mu * x) / std::log(1 + mu), qc = std::round(comp * 8) / 8; want = (std::pow(1 + mu, qc) - 1) / mu; }
        check("glitch companding=0 is plain linear quantisation", lin);
        check("glitch companding=1 differs and matches the mu-law reference", diff && std::fabs(b[0] - want) < 1e-6);
    }
    {   // image -> audio: a single bright row = a single harmonic of the base note
        const int W = 64, H = 32; std::vector<uint8_t> img(W * H * 4, 0); for (int i = 0; i < W * H; i++) img[i * 4 + 3] = 255;
        const int n = 3, row = H - n; for (int x = 0; x < W; x++) { uint8_t* q = &img[((size_t)row * W + x) * 4]; q[0] = q[1] = q[2] = 255; }
        VVAudio* a = vv_audio_from_image(img.data(), W, H, sr, 1.0, 57 /*A3 = 220 Hz*/, H, 0.0, 1.0, 0, 1, 0); check("image->audio renders", a && vv_audio_length(a) > 0);
        if (a) { std::vector<float> r(vv_audio_length(a)); vv_audio_read(a, 0, r.data()); const double g3 = gz(r, 660, sr), g1 = gz(r, 220, sr); check("image->audio: lit row 3 = 3rd harmonic only", g3 > 0.2 && g1 < 0.02); vv_audio_free(a); }
        check("image->audio rejects empty input", vv_audio_from_image(nullptr, 0, 0, sr, 1, 57, 32, 0, 1, 0, 1, 0) == nullptr);
    }
    {   // recorder: master-output capture, armed start, external-input source
        VVEngine* e = vv_engine_create(48000); std::vector<float> L(128, 0.f), R(128, 0.f);
        vv_engine_rec_prepare(e, 48000, 1.0, 1, 1, 0.05); vv_engine_rec_start(e);
        std::vector<float> quiet(128, 0.001f), loud(128, 0.5f);
        vv_engine_feed_input(e, quiet.data(), nullptr, 128); vv_engine_render(e, L.data(), R.data(), 128); check("recorder armed: ignores sound below the threshold", vv_engine_rec_state(e) == 1 && vv_engine_rec_frames(e) == 0);
        for (int i = 0; i < 3; i++) { vv_engine_feed_input(e, loud.data(), nullptr, 128); vv_engine_render(e, L.data(), R.data(), 128); }
        check("recorder starts on sound and counts frames", vv_engine_rec_state(e) == 2 && vv_engine_rec_frames(e) == 384);
        vv_engine_rec_stop(e); VVAudio* t = vv_engine_rec_take(e, 48000); check("recorder take returns the external input", t && vv_audio_length(t) == 384);
        if (t) { std::vector<float> r(384); vv_audio_read(t, 0, r.data()); check("recorded samples match the input", std::fabs(r[0] - 0.5f) < 1e-6f); vv_audio_free(t); }
        vv_engine_rec_prepare(e, 48000, 128.0 / 48000, 0, 0, 0); vv_engine_rec_start(e); vv_engine_render(e, L.data(), R.data(), 128);
        check("recorder stops by itself when the buffer is full", vv_engine_rec_state(e) == 3); vv_engine_destroy(e);
    }
    {   // DAW recorder + click
        VVEngine* e = vv_engine_create(48000); vv_engine_set_transport(e, 120, 0); std::vector<float> L(480), R(480);
        vv_daw_set_click(e, 1); vv_daw_start(e); int onsets = 0, sinceLoud = 100000; double clickE = 0;
        for (int b = 0; b < 200; b++) {   // 2 s in 10 ms blocks, drained like the UI timer would
            vv_engine_render(e, L.data(), R.data(), 480); if (b % 10 == 0) vv_daw_drain(e);
            for (int i = 0; i < 480; i++) { const float a = std::fabs(L[i]); if (a > 0.05f && sinceLoud > 1000) onsets++; if (a > 0.01f) sinceLoud = 0; else sinceLoud++; clickE += L[i] * L[i]; }
        }
        vv_daw_stop(e); const double secs = vv_daw_drain(e);
        check("click: audible in the output", clickE > 0.1);
        check("click: 120 bpm gives a beat every 0.5 s (4 in 2 s)", onsets == 4);
        check("recorder: take length matches", std::fabs(secs - 2.0) < 0.02 && vv_daw_state(e) == 2);
        const size_t n16 = vv_daw_take_wav(e, 16, 48000, nullptr, 0), n24 = vv_daw_take_wav(e, 24, 44100, nullptr, 0);
        check("recorder: 16-bit wav size", n16 == 44 + (size_t)(secs * 48000 + 0.5) * 4);
        check("recorder: 24-bit 44.1 kHz wav size", std::llabs((long long) n24 - (long long) (44 + 2.0 * 44100 * 6)) < 64);
        std::vector<uint8_t> w(n24); vv_daw_take_wav(e, 24, 44100, w.data(), w.size());
        uint32_t rate = w[24] | (w[25] << 8) | (w[26] << 16) | ((uint32_t) w[27] << 24); check("recorder: wav header carries 44100 Hz / 24 bit", rate == 44100 && w[34] == 24);
        double takeE = 0; for (size_t i = 44; i + 2 < w.size(); i += 3) { const int v = (w[i] | (w[i + 1] << 8) | ((int8_t) w[i + 2] << 16)); takeE += std::fabs(v / 8388608.0); }
        check("recorder: the click is not in the recording", takeE < 1e-6);
        // resampling keeps a tone's pitch: record a held note at 48 k, convert to 96 k, length doubles
        vv_daw_clear_take(e); vv_engine_destroy(e);
    }
    {   // resample accuracy: a 1 kHz tone survives 48k -> 96k -> 48k
        std::vector<float> t(48000); for (size_t i = 0; i < t.size(); i++) t[i] = 0.5f * (float) std::sin(6.283185307 * 1000 * i / 48000.0);
        auto up = vv::DawRecorder::resample(t, 48000, 96000), dn = vv::DawRecorder::resample(up, 96000, 48000); double err = 0; for (size_t i = 100; i < 47900; i++) err = std::max(err, (double) std::fabs(dn[i] - t[i]));
        check("resampler: 48k->96k->48k round trip within 1e-3", up.size() == 96000 && dn.size() == 48000 && err < 1e-3);
    }
    {   // PWM speed (Hz) dial: static vs swept pulse width, and the Hz dial as a matrix destination
        std::vector<unsigned char> wt(8 + 256 * 4); { uint32_t fc = 1, fs = 256; std::memcpy(wt.data(), &fc, 4); std::memcpy(wt.data() + 4, &fs, 4); for (int i = 0; i < 256; i++) { float x = (float)(i / 128.0 - 1.0); std::memcpy(wt.data() + 8 + i * 4, &x, 4); } }
        auto run = [&](const char* json) {
            VVEngine* e = vv_engine_create(48000); vv_engine_load_wavetable(e, "analogMorph", wt.data(), wt.size());
            vv_engine_load_preset_json(e, json, std::strlen(json)); std::vector<float> L(480), R(480); vv_engine_render(e, L.data(), R.data(), 480);
            vv_engine_note_on(e, 57, 0.8f); std::vector<float> out; for (int b = 0; b < 200; b++) { vv_engine_render(e, L.data(), R.data(), 480); out.insert(out.end(), L.begin(), L.end()); }
            vv_engine_destroy(e); return out; };
        auto diff = [](const std::vector<float>& a, const std::vector<float>& b) { double d = 0; for (size_t i = 0; i < a.size(); i++) d += std::fabs(a[i] - b[i]); return d / a.size(); };
        const auto s0 = run("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8,\"pwm\":0.5,\"pwmRate\":0},{\"level\":0}]}}}");
        const auto s1 = run("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8,\"pwm\":0.5,\"pwmRate\":5},{\"level\":0}]}}}");
        const auto s2 = run("{\"layers\":{\"WT\":{\"type\":\"wavetable\",\"oscs\":[{\"table\":\"analogMorph\",\"level\":0.8,\"pwm\":0.5,\"pwmRate\":0},{\"level\":0}]}},\"connections\":[{\"source\":\"velocity\",\"dest\":\"WT:osc1PwmRate\",\"amount\":60}]}");
        bool fin = true; for (float v : s1) if (!std::isfinite(v)) fin = false;
        check("pwm Hz: finite output", fin);
        check("pwm Hz > 0 changes the sound vs static pulse width", diff(s0, s1) > 0.01);
        check("pwm Hz is a modulation destination (osc1PwmRate)", diff(s0, s2) > 0.01);
    }
    {   // sample-editor audition + playhead
        std::vector<float> tone(48000); for (size_t i = 0; i < tone.size(); i++) tone[i] = 0.5f * (float) std::sin(6.283185307 * 220 * i / 48000.0);
        VVAudio* a = vv_audio_create(48000, 1, 48000, tone.data(), nullptr); std::vector<uint8_t> wav(vv_audio_to_wav16(a, nullptr, 0)); vv_audio_to_wav16(a, wav.data(), wav.size()); vv_audio_free(a);
        VVEngine* e = vv_engine_create(48000); vv_engine_load_sample_wav(e, "tt", wav.data(), wav.size());
        const char* pj = "{\"layers\":{\"SMP\":{\"type\":\"sampler\",\"oscs\":[{\"name\":\"tt\",\"level\":0.8,\"start\":0,\"end\":1,\"loop\":false,\"transients\":[{\"pos\":0.25,\"reverse\":false,\"pan\":0},{\"pos\":0.5,\"reverse\":false,\"pan\":0}]}]}}}";
        check("audition preset loads", vv_engine_load_preset_json(e, pj, std::strlen(pj)) == 1);
        std::vector<float> L(480), R(480); vv_engine_render(e, L.data(), R.data(), 480);
        check("playhead idle before audition", vv_engine_sample_head(e, 0) < 0);
        vv_engine_audition_slice(e, 0, 2, 0.9f); double e2 = 0; for (int b = 0; b < 5; b++) { vv_engine_render(e, L.data(), R.data(), 480); for (float v : L) e2 += v * v; }
        const float h = vv_engine_sample_head(e, 0);
        check("audition slice 2 sounds", e2 > 0.1);
        check("playhead sits inside slice 2 (0.5..1)", h >= 0.5f && h < 0.6f);
        vv_engine_audition_off(e); for (int b = 0; b < 400; b++) vv_engine_render(e, L.data(), R.data(), 480);
        check("playhead idle after release", vv_engine_sample_head(e, 0) < 0);
        vv_engine_destroy(e);
    }
    vv_audio_free(src);
    std::printf(fails ? "AUTHORING TESTS FAILED\n" : "authoring tests passed\n"); return fails ? 1 : 0;
}
