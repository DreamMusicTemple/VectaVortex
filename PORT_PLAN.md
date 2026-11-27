# Port plan and status

## Architecture
```
 SwiftUI (App + AUv3 view)  --AUParameterTree / MIDI-->  VectaAudioUnit (Swift, AUAudioUnit)
                                                              |  C ABI (Bridge/vv_engine.h)
                                                              v
                                                   vv::Engine (C++17, header-only, no allocation in render)
                                                   voices -> layer filters/EQ -> FX rack -> master
```
* **Single source of truth for parameters:** the web app's `VectaAUv3.getParameterTree()` -> `Core/parameters.json` -> generated C++ and Swift tables. Ids never change once assigned (`Core/param_ids.json`).
* **Parity rule:** every DSP block is ported with a golden test - `tools/make_golden.js` runs the *real* JS worklet from `full-app.html` in Node and the C++ must match. This is how the two reverbs and the phaser were done; it is mechanical for the rest.
* **No JUCE** in this plan (you asked for Swift UI + C++ DSP). The earlier idea was a JUCE wrapper; the C++ here has no framework dependency, so JUCE could still host it later if you change your mind.
* iOS minimum is set to **16.0** (Apple requires apps to target iOS 13+ from 9 Sep 2026; 16 keeps modern SwiftUI/NavigationStack). Tell me if you need older devices.

## Status of the DSP worklets (21 in the web app)
| Processor | JS lines | Status |
|---|---|---|
| phaser-flanger | 55 | **ported, verified** |
| adv-reverb (hall, plate ambient, kristal, flow, mothership, formant) | 188 | **ported, verified** |
| delay | 50 | **ported, verified** (golden2, bit-exact) |
| chorus | 21 | **ported, verified** (golden2, bit-exact) |
| eq | 23 | **ported, verified** (golden2, bit-exact) |
| resonator | 19 | **ported, verified** (golden2, bit-exact) |
| saturation | 27 | **ported, verified** (golden2, bit-exact) |
| panning / band-pan | 52 / 30 | **ported, verified** (golden2, bit-exact) |
| wavefolder | 47 | **ported, verified** (golden2, bit-exact) |
| tape | 49 | **ported, verified** (golden2, bit-exact) |
| fm | 29 | **ported, verified** (golden2, bit-exact) |
| wavetable / wavetable-sync | 36 / 62 | **ported, verified** (golden2, bit-exact) |
| multi-filter (the voice filter) | 85 | **ported, verified** (golden2, bit-exact) |
| procedural (pluck, modal, breath, chaos) | 118 | **ported, verified** (golden2, bit-exact) |
| glitch | 90 | **ported, verified** (golden2, bit-exact) |
| granular | 84 | **ported, verified** (golden2, bit-exact) |
| spectralfx | 118 | **ported, verified** (golden2, bit-exact) |
| reverb (room, spring, plate, shimmer, reverse) | 333 | **ported, verified** (golden2, bit-exact) |
| recorder | 19 | not ported: replaced by AVAudioEngine/AU input taps in Swift |

## Parts of the web app that are NOT worklets (the bigger half of the work)
The web app builds each voice from native Web Audio nodes on the main thread (about 56 gain nodes, 9 biquad filters, buffer sources, constant sources, a compressor) and runs modulation from a ~60 Hz timer. None of that exists on iOS, so it must be re-implemented in C++:
1. **Voice engine** - per-layer voice build (WT/FM/SMP/PROCEDURAL), amp and filter ADSR as per-sample envelopes, unison/detune/drift, glide, zones, legato, sub/noise, pan.
2. **Filters** - the biquad-based paths and the multi-filter processor.
3. **Modulation** - 4 LFOs, macros, envelope follower, cross-mod, mod matrix with all destinations, MPE.
4. **Sequencer / arp / note repeat** - patterns, p-locks, ratchets, probability, chains, swing; must sync to host transport (`setTransport` already in the ABI).
5. **Sampler** - slot playback, loop/ping-pong/reverse, start/end, fades, transient slicing, sample mix, WSOLA time-stretch, importers (WAV smpl, AIFF, IFF 8SVX, SF2, SFZ, raw), recording, image-to-audio. Importers and stretch are plain algorithms - port to C++/Swift; decoding MP3/FLAC/M4A uses AVFoundation instead of the browser.
6. **Presets and state** - the web preset JSON maps onto the parameter table plus non-parameter state (wavetables, samples, matrix, sequences). Keep the JSON format so web and iOS presets interchange.
7. **Master chain** - limiter/compressor, master tone, oversampling option.
8. **Built-in samples** - already exported to `Assets/Samples` (56 WAVs, 13 MB).

## UI (SwiftUI)
All screens exist as source (uncompiled): Play (keyboard, macros, wheels) · Sound (WT / FM / Sampler / Procedural oscillators, filters, envelopes, EQ) · Mod (4 LFOs, macros, envelope follower, matrix, cross-mod, vocoder) · Seq (transport, swing, arpeggiator, pattern grid with playhead, chains, step editor, p-locks) · FX rack (14 effect types, sends, reorder, multiband panner, glitch sequencer) · Presets (18 factory presets, user library, import/export, save-as) · Settings (polyphony, legato, tuning, MPE, bend range, WT/FM mix, master tone, sample mix, oversampling) · Host params (all 200 automatable AU parameters).
Not built as bespoke screens: the oscilloscope / cymatics visualisers, XY pads, reverb visualiser, sample editing tools (record, WSOLA, importers beyond WAV). The look reference is the web app (cyan on near-black, monospaced).

## Phases
| Phase | Content | Exit test |
|---|---|---|
| P0 (done) | tooling, golden harness, phaser + reverbs, engine shell, parameter tables, Xcode/AUv3 skeleton, docs | `Core/run_tests.sh` green; Swift compiles on the Mac |
| P1 (effects DONE; FX rack/sends wiring TODO) | all 20 worklet processors ported with golden parity (Core/tests/golden + golden2: 111 cases, 0.00 error) | `Core/run_tests.sh` green |
| P2 | voices: WT, then FM, Sampler, PROCEDURAL; layer filter/EQ/envelopes | offline-render a held note in web and native, compare spectra/levels |
| P3 | LFOs, macros, matrix, cross-mod, arp, sequencer, transport sync | scripted scenarios match the web app |
| P4 | SwiftUI screens, presets, sample tools | feature checklist vs web |
| P5 | performance on device, interruptions, buffer sizes, memory limits of AUv3 extensions, accessibility | Instruments clean, no dropouts at 64-frame buffers |
| P6 | TestFlight internal then external | see APPSTORE_CHECKLIST.md |
| P7 | App Store submission | approved |

## Risks and decisions to confirm
* **Name / bundle ids / AU codes** are placeholders: `com.dreammusictemple.vectavortex`, manufacturer `DMTm`, subtype `Vvtx`. Choose final values before the first TestFlight build; changing AU codes later breaks saved DAW sessions. (The web bridge currently reports `vvtx` / `DMT `; align it when the codes are final.)
* **Extension memory limits** - AUv3 extensions run inside the host's process and have tighter memory budgets than apps. The 13 MB built-in sample set must be loaded lazily/memory-mapped, not all at once.
* **Real-time safety** - no allocation, locks, or Swift/ObjC calls in the render path (the engine is written to this rule; keep it).
* **Scope of the first public release** - recommend shipping a smaller, solid first version (core layers + FX + presets) rather than waiting for 100% parity.


## Status after this pass (honest)
**Verified in C++ against the real web app rendered offline in Chromium (131 scenarios, all pass; `ctest` 8/8 on Linux):**
* every worklet DSP block (bit-exact), the voice engine (WT, FM, Sampler, PROCEDURAL, unison/glide/zones/legato, layer filters + EQ, envelopes), FX chain with sends, master tone + compressor, preset JSON loader;
* the modulation matrix (4 LFOs, macros, envelope follower, mod wheel, velocity, random, MPE timbre/pressure, WT frame-brightness source, all global and per-voice destinations);
* cross-modulation RM / AM / PM and FM-xmod between layers (including chains and a feedback loop) and the vocoder (log and linear spacing, 8 / 16 bands, band slots).
Most scenarios match at ~1e-5 relative RMS; FM / master-tone / filter-FX ones at 1e-4..6e-4; PM cross-mod at ~2.5e-3 because of an unexplained start transient (see `Core/tests/tolerances.txt`, WEB_QUIRKS.md).

**Ported and unit-tested only (NOT compared with the browser - the browser versions are timer based):** held notes, polyphony limit, mono/legato, per-layer Hold latching, layer sidechain ducking and sequencer gate length, arpeggiator (all orders), step sequencer (patterns, chains, ratchets, probability, tempo ratio, swing, sample-transient p-locks), MPE per-note pitch bend, live preset edits (`Synth::updatePreset`), host-parameter automation mapping. Tests: `performance_tests`, `preset_bank_tests` (all 18 factory presets + default render finite, audible, peak <= 1, survive live edits), `engine_tests` (C ABI end to end including automation).

**Real-time design:** every control (notes, MIDI, macros, host parameters) enters the audio thread through a lock-free command ring; preset swaps are staged under a mutex the audio thread only `try_lock`s. Host parameter events only store a value and set a flag; a 30 Hz timer (Swift) calls `vv_engine_pump_params` to apply them to the preset. Not yet measured on a device (no Mac).

**Wired:** `FullEngine` (Synth + Performance) behind the C ABI (`Bridge/vv_engine.h`). The AU parameter tree (200 parameters) maps onto preset paths (`param_bridge.hpp`, scaling identical to the web bridge) and follows presets loaded or edited in the UI. Full state save/restore stores the live preset JSON.

**NOT ported:**
* Randomised FX (tape / granular / glitch) are not bit-comparable with the browser (different random streams); they are tested for sanity only.
* FX-parameter modulation is approximate (about 3e-4 absolute vs the browser; extreme resonance settings can diverge). Reverb / saturation / delay "cumulative drift" modulation in the browser is replaced by a direct-offset model (see WEB_QUIRKS.md); not browser-verified.
* Recording, sample importers beyond WAV (AIFF, SF2, SFZ, 8SVX), image-to-audio, WSOLA stretch UI, oscilloscope / cymatics / XY-pad screens.
* Held notes do not live-update non-modulated dials (a changed dial applies from the next note).
* **Swift has never been compiled** (no Mac). Expect compile errors on first build; see MAC_DAY_ONE.md.
* **Web app:** `Web/full-app.html` is unchanged; GitHub Pages testing is unaffected.

## Update: UI path
JUCE (`Juce/`, see JUCE.md) is now the recommended UI + AUv3 wrapper. The SwiftUI code in `Apple/` remains as a fallback.

## Status addendum
Done in JUCE: piano roll, Send to Vektur Sonic + mono warning, (the FX space-shooter games, since replaced by Vektur Void), 53 wavetables, widened controls. Remaining: gradient-ring control skin, Ocean background, recording/WSOLA/cymatics UI.

## Status addendum 2026-10-16
Engine + JUCE ports of the 47-item batch are done (see README "Release batch" and RELEASE_AUDIT.md). Not ported to JUCE: matrix overview grid (JUCE has the wire list only), the three dropdown styles,
logo-style section headings, the collapsible manual, Invert Keys, long-press-on-dial modulation menu, LFO 5/6 MIDI CC and host-parameter exposure. Swift UI (never compiled) got: fullState tab restore, interruption handling, Notch/iSEM, 6 LFOs, 10 FX, arp layer, sampler Bleed/Arp/mic/speaker (Root key removed).
