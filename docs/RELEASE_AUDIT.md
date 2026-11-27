# Release audit (App Store / AUv3 review readiness)

Written on Linux. "Verified" means a test in this repository ran and passed here. "Unverified" means the code is written but has never been
compiled or run on Apple platforms (no Mac / Xcode was available).

## 1. Interruptions: backgrounding, phone calls, route changes

| Check | Status |
|---|---|
| Engine survives "all notes off" mid-stream, zero-frame renders, resume, two `prepare()` calls at different sample rates, 200 rapid on/panic cycles, output stays finite | **Verified** (`engine_tests`, "interruption:" lines) |
| Standalone app: `AVAudioSession` interruption (began: panic; ended: reactivate session + restart engine, even without `.shouldResume` in the foreground), route change, media-services reset (engine + audio unit rebuilt), `AVAudioEngineConfigurationChange`, background/foreground restart | **Written, unverified** (`Apple/App/VectaVortexApp.swift`) |
| AUv3: interruption notification releases all notes; `deallocateRenderResources` releases all notes | **Written, unverified** (`VectaAudioUnit.swift`) |
| JUCE standalone iOS: JUCE's own iOS audio device restarts after interruptions | JUCE behaviour, not tested here |
| Background audio: `UIBackgroundModes = audio` is set in `project.yml` | configured, unverified |

A device test is still required: start a note, lock the phone, take a call, unplug headphones, background and foreground the app.

## 2. Render thread: allocation / locking audit (items 43 and 46)

* **Verified:** `Core/tests/rt_alloc_tests.cpp` hooks `operator new`, then renders every shipped preset (19) for about 2 s with a held chord,
  with 20 note-on/note-off cycles, and with the arpeggiator + sequencer running. Result: **0 allocations** in steady state, during note
  churn and with the arp/sequencer on. The hook itself is self-tested. (It sees `operator new`, not a direct `malloc` call; a grep of the render path found none.)
* **Fixed during this audit:** the arpeggiator built several `std::vector`s on every step; it now uses fixed scratch arrays. `held_`,
  `latched_`, `poly_` are pre-reserved. The new arp-target check no longer builds strings.
* **Known, not fixed:** the first render after a preset load adopts the staged preset *on the audio thread* (measured: up to ~166
  allocations). It happens on a user action, not continuously, but it can cause one short glitch when switching presets. Fixing it
  properly means building the voice/performance state off-thread and swapping a pointer.
* **Known:** the render entry point uses `std::mutex::try_lock` (never blocks) to pick up staged work.
* **Swift render block** (`internalRenderBlock`): reviewed by reading; it only uses stack values, pre-allocated scratch buffers and C calls, no
  Swift collections or string work. Swift/ARC behaviour of the closure is unverified until it is built and profiled with Instruments.

## 3. AUv3 sandboxing and thread constraints (items 44, 45)

* `sandboxSafe: true`, `APPLICATION_EXTENSION_API_ONLY: YES`, `ENABLE_USER_SCRIPT_SANDBOXING: YES` are set in `project.yml`.
* The extension reads only its own bundle (wavetables, samples, presets) and does not touch the network or the microphone. The app (not the extension) carries `NSMicrophoneUsageDescription`.
* Host parameter events only store + flag on the render thread; a 30 Hz background timer applies them to the preset (see `vv_engine.h`).
* `createAudioUnit` returns quickly: asset loading runs on a background queue (`InstrumentLoader`). `requiresAsyncInstantiation` is a flag
  the system sets for extensions with a UI (hosts then call `AudioComponentInstantiate`); there is nothing to set in our plist, and the standalone app already
  instantiates with the asynchronous `AVAudioUnit.instantiate`.
* **Unverified:** actual validation with `auval -v aumu Vvtx DMTm` and loading in AUM / Logic.

## 4. State, presets, resizing (item 47)

* Swift AUv3 `fullState` now also stores `vv.ui` (selected tab; extensible for view modes) and restores it; version bumped to 2. **Unverified** (not compiled).
* JUCE `getStateInformation` now stores the oscilloscope view mode (wave / X-Y), selected tab and window size, next to the preset and the 200 host parameters;
  restore is covered by `ui_snapshot` ("state restores ..."). **Verified** on Linux.
* The preset JSON carries every new control (per-sample Bleed, Bleed amount, Arp, mic, speaker, arp layer, 6 LFOs, 10 FX stages).
* Resizing: the JUCE editor is resizable (420 x 480 up to 2000 x 1400) with a narrow layout below 760 px. The SwiftUI view autoresizes inside the AUv3 view controller.

## 5. Configuration clean-up (item 44)

Searched `project.yml`, plists, entitlements, privacy manifest, CMake files, Swift and C++ for placeholder strings, dummy URLs and test assets:
none found. The one deliberate blank is `DEVELOPMENT_TEAM: ""` (your Apple Team ID). `MARKETING_VERSION` is `0.1.0`: set the real version before submission.


## Release batch 2026-10-16 (b) — hybrid vocoder, mic/speaker FX, generic modulation, gate/sidechain, formants, banks

**Ported to the C++ engine, JUCE and (uncompiled) SwiftUI:**
* Hybrid Vocoder & Vocal Resynthesis engine (`Core/include/vv/hybrid_vocoder.hpp`): modulator gain / 80–150 Hz HPF / gate / compressor, internal PolyBLEP saw+pulse carrier or external / sidechain input, >5 kHz sibilance noise injection, four modes (classic 8–24 band + BBD ensemble, LPC formant/talkbox, YIN pitch corrector with scale / MIDI quantise, STFT resynthesis), carrier drive, formant shift, dry/wet.
* Mic / speaker colouring FX bank (17 + 17 types) with live mix modulation.
* Modulation: `note` source, `FXP:<type>:<key>` (30) and `GEN:<LAYER>.<name>` (33) destinations; they appear in the JUCE and Swift matrix pickers.
* Sequencer gate length (per layer) and sidechain ducking (source layer, threshold, amount); Latch removed in favour of per-layer Hold latching.
* Formant filter vowel tables (classic / male / female, 14 vowels) for voice filters and the FX filter.
* The 18 DMT CYMATICS presets are shipped as factory presets (`Assets/Presets/DMT CYMATICS - <category> - <name>.json`, regenerate with `tools/export_cymatics_bank.js`).
* Single-preset files (`{vectaVortexPreset:1,name,category,data}`, `.vvpreset.json`) import and export in JUCE and Swift; the core loader also accepts them.

**Verified (Linux):** 156 reference scenarios against the real web app rendered in Chromium, 0 failed (vocoder classic/LPC/pitch/spectral/drive, 8 mic/speaker FX, GEN, FXP, note source, formant tables all included); 10 Core test suites incl. `vv_hybrid_vocoder_tests` and the render-thread allocation test (steady state 0); JUCE Linux build + headless UI test (`vv_ui_snapshot`) all passing.

**Unverified / known limitations**
* Swift / Xcode / AUv3 have never been compiled or run (no Mac). The AUv3 wrapper has no external input bus wired (JUCE has an optional "Vocoder / Sidechain In" bus).
* Not browser-comparable: the vocoder noise gate (analyser + timer driven), sidechain ducking and sequencer gate timing (timer driven; unit-tested natively instead), granular and tape (random streams; the `mod_fxp_tape` scenario was removed because its baseline is not deterministic), FX delay-time modulation (~1e-2 abs residual, tolerance 2e-2).
* Vocoder internal carrier is PolyBLEP, not bit-identical to Web Audio oscillators. The pitch corrector is monophonic. Vocoder and mic/speaker drive stages use no oversampling (the web app was changed to match). Vowel table values beyond the Peterson & Barney set are textbook approximations.
* Layer envelope and GEN changes apply to new notes only (PRO tune and mic/speaker mix are live).
* Not ported to JUCE/Swift: collapsible Wire Overview tree, tap-to-type readouts beyond the existing number boxes, Nebula and "Vektur Vortex" background images.

## Generative chord maker (2026-10-16 c)
New in the sequencer (web: top of the Sequencer page; JUCE: SEQ > Chords; SwiftUI: Sequencer > Chord maker, unbuilt):
* Key, 36 western and world modes (Ionian ... Hijaz, Hirajoshi, ragas, maqams, pelog/slendro approximations, 12-TET), Naturalness, Density, Sparseness, Randomness, Probability, Gate, Strum, Velocity, tempo multiplier / divider (÷4 ... ×4 on a 2-beat base),
  voicing (close / open / drop 2), target layers, chord count, and Euclidean mode (one chord per pulse, held to the next onset). Generate / Mutate / Re-voice / Rotate / Reverse, per-chord lock, re-roll and audition.
* Algorithm (`tools/chordmaker_core.js`, `Core/include/vv/chordmaker.hpp`): root motion weighted by functional stability and root-motion strength, blended with a uniform choice by Naturalness; tonic start and dominant-to-tonic cadence at high Naturalness;
  chords are stacked scale degrees (always in key / mode); voice-leading picks the inversion nearest the previous chord; a rolling history of the last 16 progressions makes every Generate original. Randomness moves notes to neighbouring degrees of the mode at each trigger.
* Chords are stored as scale-degree indices in the preset (`chordMaker` block), so changing Key or Mode re-maps the same progression instantly.
**Verified:** the C++ generator, mutator, play-note randomiser and chord names are bit-identical to the JavaScript for six golden cases (`Core/tests/chordmaker_tests.cpp`, golden written by `tools/test_chordmaker.js`);
all generated notes stay in the mode (21,780 notes, every scale / 3 keys); timing on the transport incl. Euclidean spacing and probability (`performance_tests`); C-API actions (generate, mutate, lock survives mutate, fit, Euclid sizing); JUCE headless UI test.
**Unverified:** SwiftUI page (never compiled); musical taste of the output (properties are tested, not "does it sound good"); world scales are 12-TET approximations. Playback uses the sequencer Run switch / PLAY transport as its clock; with Run off nothing plays (press a card's play button on the web to audition).
Also: choosing a background image on the web now switches the colour scheme to Future (the native apps have no background picker).

## Random Sampler FX (2026-10-16 d)
Web: RANDOM SAMPLER FX button right after RANDOM POPULATE in the Sampler: each of the 16 slots gets a different microphone and a different speaker (two independent shuffles of the 16 real types, mix set to 0.5 if it was 0). Verified in Chromium (16 unique each, none = "none").
JUCE and SwiftUI: both RANDOM POPULATE and RANDOM SAMPLER FX buttons added (native had neither); the native mic / speaker pickers now list all 16 types (previously only 8). JUCE verified in the headless UI test; SwiftUI unbuilt.

## 2026-10-16 — Native parity audit
See `docs/NATIVE_PARITY_AUDIT.md`. Core 12/12 tests and 156/156 reference scenarios pass; JUCE headless UI tests pass; Swift is uncompiled. Web: GENERATE fix for FM categories, launch splash added, Brushed metal background added. Native: MPC pads, Zones, appearance + backgrounds, preset artists/categories/bank zip, Wire Overview, MIDI CC map + MIDI LEARN. CC macros moved to 102-105 to match the web. **Swift could not be compiled in this environment (swift.org / package hosts blocked); it has only had a brace-balance check.**

Brushed metal is now the default background (web, JUCE, Swift); `-lc++` added to the Xcode targets.


## Interstellar
New FX stage; tests: `interstellar`, `interstellar_engine`, `interstellar_golden`, JUCE UI test block, 24 patches in preset_bank. Swift sources for it are uncompiled.
