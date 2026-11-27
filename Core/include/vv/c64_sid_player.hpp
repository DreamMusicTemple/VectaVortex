// C64 SID player for the native app: a thin, real-time-safe wrapper around libsidplayfp (full 6502/CIA/VIC + reSID-family SID emulation).
// LICENSE NOTE: libsidplayfp is GPL-2.0-or-later. Linking it makes the combined app GPL, which conflicts with App Store distribution terms.
// It is therefore an OPTIONAL build (CMake -DVV_WITH_SIDPLAYFP=ON); without it this header compiles to a stub that reports "not available".
// Threading: load()/selectSubtune()/configure() run on a non-audio thread and publish a prepared engine through an atomic pointer;
// render() (audio thread) never allocates, locks or touches the disk.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#ifdef VV_WITH_SIDPLAYFP
#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/SidTune.h>
#include <sidplayfp/SidTuneInfo.h>
#include <sidplayfp/SidConfig.h>
#include <sidplayfp/SidInfo.h>
#include <sidplayfp/builders/sidlite.h>
#endif
namespace vv {
struct C64TuneInfo { std::string title, author, released, format, model, clock; unsigned songs = 0, startSong = 0, current = 0; uint32_t fileSize = 0; bool ok = false; std::string error; };
class C64SidPlayer {
public:
    static constexpr bool available() {
#ifdef VV_WITH_SIDPLAYFP
        return true;
#else
        return false;
#endif
    }
#ifdef VV_WITH_SIDPLAYFP
    // Non-audio thread. Accepts .sid (PSID/RSID), .mus (+ optional .str side file via its path), .prg handled by libsidplayfp's own loaders.
    C64TuneInfo load(const std::string& path, double sampleRate) {
        C64TuneInfo r; Engine* e = new Engine(); e->sr = sampleRate; e->path = path;
        e->tune.reset(new SidTune(path.c_str()));
        if (!e->tune->getStatus()) { r.error = e->tune->statusString(); delete e; return r; }
        const SidTuneInfo* ti = e->tune->getInfo();
        r.title = ti->numberOfInfoStrings() > 0 ? ti->infoString(0) : ""; r.author = ti->numberOfInfoStrings() > 1 ? ti->infoString(1) : ""; r.released = ti->numberOfInfoStrings() > 2 ? ti->infoString(2) : "";
        r.format = ti->formatString(); r.songs = ti->songs(); r.startSong = ti->startSong(); r.current = r.startSong; r.fileSize = ti->dataFileLen();
        r.model = ti->sidModel(0) == SidTuneInfo::SIDMODEL_8580 ? "8580" : ti->sidModel(0) == SidTuneInfo::SIDMODEL_6581 ? "6581" : "unknown";
        r.clock = ti->clockSpeed() == SidTuneInfo::CLOCK_NTSC ? "NTSC" : ti->clockSpeed() == SidTuneInfo::CLOCK_PAL ? "PAL" : "unknown";
        if (!e->start(r.startSong)) { r.error = e->error; delete e; return r; }
        r.ok = true; publish(e); return r;
    }
    bool selectSubtune(unsigned n) { Engine* cur = cur_.load(); if (!cur) return false; Engine* e = new Engine(); e->sr = cur->sr; e->path = cur->path; e->tune.reset(new SidTune(e->path.c_str())); if (!e->tune->getStatus() || !e->start(n)) { delete e; return false; } publish(e); return true; }
    void setVoiceMute(unsigned voice, bool m) { Engine* e = cur_.load(); if (e && voice < 3) e->engine.mute(0, voice, m); }
    // Audio thread: fills n mono samples (preallocated scratch inside the engine). Returns false when nothing is loaded.
    bool render(float* out, unsigned n) {
        Engine* e = cur_.load(std::memory_order_acquire); if (!e) { for (unsigned i = 0; i < n; ++i) out[i] = 0; return false; }
        unsigned done = 0, spin = 0;
        while (done < n) { if (e->avail == 0) { int s = e->engine.play(2000); if (s < 0) break; e->avail = e->engine.mix(e->buf.data(), (unsigned)s); e->pos = 0; if (e->avail == 0) continue; }
            unsigned k = e->avail < n - done ? e->avail : n - done; for (unsigned i = 0; i < k; ++i) out[done + i] = e->buf[e->pos + i] / 32768.0f; e->pos += k; e->avail -= k; done += k; }
        for (unsigned i = done; i < n; ++i) out[i] = 0; return true;
    }
    ~C64SidPlayer() { delete cur_.load(); delete retired_; }
private:
    struct Engine {
        sidplayfp engine; std::unique_ptr<SidTune> tune; SIDLiteBuilder builder{"vv"}; std::vector<short> buf = std::vector<short>(65536); unsigned avail = 0, pos = 0; double sr = 48000; std::string error; std::string path;
        bool start(unsigned sub) {
            SidConfig c; c.frequency = (uint_least32_t)sr; c.samplingMethod = SidConfig::INTERPOLATE; c.sidEmulation = &builder; c.defaultC64Model = SidConfig::PAL; c.defaultSidModel = SidConfig::MOS6581;
            if (!engine.config(c)) { error = engine.error(); return false; }
            tune->selectSong(sub); if (!engine.load(tune.get())) { error = engine.error(); return false; } engine.initMixer(false); avail = 0; pos = 0; return true;   // initMixer needs the loaded tune's SID count
        }
    };
    void publish(Engine* e) { Engine* old = cur_.exchange(e, std::memory_order_acq_rel); delete retired_; retired_ = old; }   // the previous engine is freed one swap later, never on the audio thread
    std::atomic<Engine*> cur_{nullptr}; Engine* retired_ = nullptr;
#else
    C64TuneInfo load(const std::string&, double) { C64TuneInfo r; r.error = "libsidplayfp support not compiled in (build with -DVV_WITH_SIDPLAYFP=ON)"; return r; }
    bool selectSubtune(unsigned) { return false; }
    void setVoiceMute(unsigned, bool) {}
    bool render(float* out, unsigned n) { for (unsigned i = 0; i < n; ++i) out[i] = 0; return false; }
#endif
};
} // namespace vv
