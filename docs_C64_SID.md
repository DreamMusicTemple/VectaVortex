# C64 SID Player — formats, build, limitations

## Web app (single HTML file)
- Engine: built-in 6510 CPU + register-level SID emulator running in an AudioWorklet (`c64-player`). Klaus Dormann 6502 functional test passes; PAL/NTSC play-call timing verified.
- Supported import: **.sid (PSID v1-4, RSID)**. Multi-file playlist, subtune prev/next/selector, loop, 6581/8580 and PAL/NTSC override, voice mute/solo, live register read-out, scope.
- **Not supported on web: .mus, .str, .prg, .sng.** They are rejected with an explicit reason, never reported as imported. Corrupt/truncated/empty .sid files are rejected.
- Routing: through the SID layer bus (existing FX/mixer/master/recording) or straight to master; independent volume.
- WAV export: 16/24/32f at 44.1/48/88.2/96 kHz, user-set length (default 180 s), mono, offline render in chunks (main thread, not audio thread).
- Limitations: combined waveforms use an AND approximation (no measured wave tables); 6581 filter curve approximated; CIA/VIC are timing stubs (no sprites/badlines/ROMs); no Songlengths DB so duration is "unknown"; panel registers are read-only (the tune drives them).
- Validation vs libsidplayfp 3.1.2: pitch parity, levels within ~±2.3 dB, band-shape correlation 0.66-0.95 (noise/ring/sync stress tune 0.34).

## Native (C++)
- `Core/include/vv/c64_sid_player.hpp`: libsidplayfp wrapper (build flag `VV_WITH_SIDPLAYFP`; stub otherwise). Loading happens off the audio thread; engines swapped atomically, retired one swap later; render() allocation-free.
- `Core/tools/sid_render.cpp`: offline WAV renderer (`sid_render in.sid out.wav subtune seconds rate bits`). Tested: RoboCop 3 sub 5 24-bit/96k, RSID 32f/44.1k, bad file → exit 3.
- Build libsidplayfp: `Core/tools/build_libsidplayfp.sh` (uses 3.1.2 release tarball).
- **Licensing:** libsidplayfp is GPL-2.0-or-later. Linking it makes the app GPL, which conflicts with App Store distribution. Hence optional, off by default; web build uses its own emulator.
- Not done natively: JUCE/SwiftUI player UI, routing through the native FX chain, .mus/.str/.prg/.sng.
