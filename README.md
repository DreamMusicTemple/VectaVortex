# Vektur Vortex - native port (SwiftUI + C++ DSP + AUv3)

The web app (`Web/full-app.html`, live on GitHub Pages) stays the reference implementation and design source while this port grows.

```
Core/        C++17 DSP, header-only, no allocation in process(). Tested here against the web app's own output.
Bridge/      C ABI (vv_engine.h) - the only thing Swift sees of the C++.
Apple/       Xcode project spec (XcodeGen), AUv3 extension, standalone app, SwiftUI interface.
Assets/      Built-in samples rendered from the web app (WAV) + manifest. Filed under "Misc".
tools/       golden-vector generator (runs the real JS worklets in Node), parameter-table generator, sample exporter.
Web/         copy of the current web app (the reference).
docs/        PORT_PLAN.md (status + roadmap), MAC_DAY_ONE.md, APPSTORE_CHECKLIST.md, PARAMETER_MAP.md.
```

## What works today (verified on Linux)
* **Phaser/flanger and the six advanced reverbs** are ported to C++ and match the JavaScript output (bit-exact or about 1e-7), are stable at extreme settings, and are independent of host buffer size.
* **Engine + C ABI**: the full instrument (Wavetable, FM, Sampler and Procedural layers, FX chain including Interstellar, vocoder, sequencer, arpeggiator, chord maker, MPC pads, XY-pad modulation sources), a 202-entry host parameter table, MIDI (including MIDI learn and the CC map), and transport sync.
* **Parameter table** generated from the web app's AUv3 bridge, with persistent ids.
* Run everything: `Core/run_tests.sh`

## What is written but NOT yet compiled (needs your Mac)
* All Swift (AUv3 audio unit, extension view controller, standalone host app, SwiftUI screens) and `Apple/project.yml`.
  They were written carefully but there is no Swift compiler here, so expect a few small compile fixes on the first build. See `docs/MAC_DAY_ONE.md`.

## Next
See `docs/PORT_PLAN.md`. Short version: port the remaining effects, then the voice layers one at a time (each with an audio-parity test against the web app), then modulation/sequencer, then the bespoke SwiftUI screens, then TestFlight.


## Release batch 2026-10-16 (47-item list)
Ported to the C++ engine and JUCE UI: Notch + iSEM filters, 6 LFOs (LFO 1 = 0.25 Hz), 10 FX stages, arp layer targeting (WT 1/2, FM A/B, Procedural, Sample 1-16)
with per-sample Arp On/Off, Hold latches the arp per layer (no separate Latch), per-sample Bleed (+ Bleed Amount, modulation destination), per-sample filter destinations, mic/speaker colouring
(4 classic + 4 unusual each), Root Key removed, matrix Edit button, piano-roll tap-any-pitch and long-press P-Locks, PANIC bottom-right.
Engine behaviour is covered by `Core/run_tests.sh` (9 suites incl. golden parity for the new filters and a render-thread allocation test).
See `docs/RELEASE_AUDIT.md` for the interruption / real-time / AUv3 audit and what is still unverified (everything Swift/Xcode).
Web-only visuals (dropdown styles, logo-style headings, collapsible manual, dial sizing) exist in `Web/full-app.html`; the JUCE UI has its own panel layout and is not a pixel clone.

## Hybrid vocoder, gate / sidechain, formants, banks (2026-10-16 b)
Native engine, JUCE and SwiftUI now include: the hybrid vocoder (classic / formant LPC / pitch / spectral, external or internal carrier), mic and speaker colouring, `note` modulation source plus FX-parameter and generic layer destinations,
per-layer sequencer Gate and sidechain ducking, male / female vowel tables for the formant filter, the DMT CYMATICS factory bank, and `.vvpreset.json` single-preset import / export.
Reference parity: 156 scenarios vs the real web app, 0 failed. Details and limitations: `docs/RELEASE_AUDIT.md`.

## Generative chord maker
Sequencer > Chords (JUCE) / top of the Sequencer page (web): key, 36 western + world modes, naturalness, density, sparseness, randomness, probability, rate multiplier / divider, Euclidean mode, mutate / lock / re-voice. See `docs/RELEASE_AUDIT.md`.
