# Native parity audit — 2026-10-16

Method: `tools/audit_web_ui.js` enumerates every control in the live web DOM (Chromium); a fuzzy matcher compares the labels with the JUCE and Swift sources. Gaps were ported C++ first, then JUCE, then Swift.

## Verification status
| Layer | Status |
|---|---|
| C++ Core | `Core/run_tests.sh` 12/12 pass (incl. new `presetgen`, bend-target test). 156-scenario web-vs-C++ reference suite: 156 run, 0 failed. |
| JUCE | Builds on Linux; headless UI test (`Juce/Tests/ui_snapshot.cpp`) passes — new checks for pattern tools, undo/redo, scope modes, GENERATE/MODIFY, custom LFO shape, scale lock. Run with `VV_NO_SPLASH=1` so the splash does not cover snapshots. |
| Swift / AUv3 | **Written but never compiled** (no Xcode here). Only a brace/paren balance check was run. Expect to fix compile errors on first build. |

## Ported in this audit
| Web feature | C++ | JUCE | Swift |
|---|---|---|---|
| Per-oscillator pitch-bend targets | `Preset::bendTargets`, gated in `modulation.hpp` | BEND TARGETS panel | BendTargetsSheet |
| GENERATE (recipe-driven: Arp Bass Key Lead Pad Drum FX XFX, scale-lock aware) / MODIFY | `presetgen.hpp` (+ generated `presetgen_recipes.hpp`, `presetgen_init.hpp`), `vv_engine_preset_generate`, `vv_engine_preset_action` | top bar + Presets screen | PresetBar + browser |
| Top preset bar (◀ ▶ quick save, save as, load, init, undo, redo) | — | yes | yes |
| Undo / redo (60 steps) | — | Store | PresetStore |
| Pattern COPY / PASTE / RANDOMISE / CLEAR | — | yes | yes |
| Keyboard scale highlight / lock / root / invert / labels / octaves / hide | — | yes | yes |
| Scope: Wave, XY, Radial, Spectrogram, Spectrogram 3D, style, fullscreen | — | ScopeView | ScopeView + audio tap |
| Curve editor (custom LFO, env follower, filter env shape) | — (LFO custom shape already honoured) | CurveEditor.h | CurveEditorView |
| Global sampler ADSR, global swing toggles | — | yes | yes |
| MOD TARGETS shortcut | — | yes | yes |
| Wire Overview: colour-coded groups, large fonts, Expand/Collapse all, connector lines, click to wire | — | WireOverview.h | WireOverviewView |
| Preset browser: artist folders (new/rename/delete), category filter, export bank .zip, import destination | — | Presets screen | PresetBrowserView + PresetLibrary |
| Settings: colour scheme, pot / dropdown styles, background image picker (incl. new **Brushed metal**) | — | Appearance.h | Appearance.swift (pot/dropdown styles n/a) |
| MPC pads (pitched pad voices, modes, lock) | `padOn/padOff`, `PLocks.pitchNote` | MpcOverlay | MpcPadsView |
| Zones panel | — | ZonesPanel | ZonesView |
| MIDI CC map (full web table) + MIDI LEARN, 4 profiles | `pollCc`, `vv_engine_cc_learn_*` | MIDI LEARN button + Settings panel | MidiLearnSection (Settings) + pump-timer poll |
| Launch splash | — | removed (not wanted) | removed (not wanted) |

## Default background
Brushed metal is the default background on web, JUCE and Swift: "Scheme default" resolves to it for Gold / Cyan / Magenta / Cream; Future and Rustic keep their own pictures; any explicit choice (including None and a custom image) wins. JUCE UI test checks both cases; web checked in Chromium.

## Xcode build notes
`project.yml` links `-lc++` into both app targets (the VVEngine static library is C++), bundles `Assets/Backgrounds` and `splash.jpg` in both targets, and the Core is header-only so no extra sources are needed. Samples (30 MB) are bundled in both the app and the AUv3 extension; if app size matters, drop them from the host app once you confirm it does not run its own engine.

## Web fix found during the audit
`generateRandomPreset` produced silent FM-category presets (both FM engines off, WT osc level untouched) and ignored the oscillator-level filter. Fixed: FM engine A on, WT osc 0 muted, filter mirrored to the oscillator filter. Native generate does the same by design; it also does not wipe patterns.

## Known gaps (not ported)
- Audio latency apply (Settings); tap-to-type readouts; long-press modulation menu; LFO 5/6 MIDI CC; vocoder audio-input device select in Swift; MPC drag-swap of samples (a dropdown replaces it); zones painted on the keyboard; SEND TO VEKTUR SONIC (not possible in AUv3); chord maker "print to layer pattern" (not built on any platform).
- Hardware-synth skin: the reference image is used as the splash only; the UI is not yet restyled to match it.

## Documented deviations
- Pad voices bypass the arp and zones.
- MIDI CC / learn only apply while the host polls: JUCE `Processor::timerCallback`, Swift `VectaAudioUnit.startPump`.
- **CC macro mapping changed**: macros are now CC 102-105 (as on the web); the old native CC 20-23 -> macros mapping is removed (CC 20-23 now drive the FM / sampler filters, as on the web).
- Native generate differs from the web (FM engine A on, WT osc 0 muted, filter mirrored, patterns not wiped).
- Swift MIDI learn is a Settings form (pick a parameter, press Learn) because the Swift UI has no dials to click.

## Limitations (unchanged)
Vocoder gate/sidechain timing, granular, tape and delay-time modulation are not parity-tested; the vocoder internal carrier is PolyBLEP; the pitch corrector is mono; layer envelope/GEN edits affect new notes only; world scales are 12-TET approximations; AUv3 has no external input bus wired.


## Interstellar (FX stage)
Ported to C++ core (reference), web worklet (golden-tested against C++), JUCE UI and Swift UI, with 24 presets and `FXP:interstellar:*` destinations on all layers. Native has no key readout or visualiser (web only). Swift unverified (not compiled). See `docs/INTERSTELLAR.md`.


## Dials (Vektur Interstellar design)
All standard dials (small and large, incl. macros, master, Vocoder and Procedural) use the Vektur Interstellar dial: dot ring that grows clockwise, knurled top-lit skirt, specular cap, white pointer; modulation shows as a satellite on the ring (web). Rustic dials are unchanged. Web: `enhanceToKnob` draw routine; JUCE: `drawInterstellarDial` (Widgets.cpp, all pot styles except Rustic); Swift: `Apple/Shared/DialFace.swift` + `SliderRow` (Swift's FX/sound rows were sliders, now dials). Swift unverified (not compiled); JUCE verified by the headless UI test screenshots; web verified in Chromium.

## Wavefolder animation, bottom row, manual
- Web: the Wavefolder FX offers only the Warp Chronology Convergence animation (the Folding Foundry game and its VIEW button are removed). JUCE ports it as `WarpView` (ANIMATION button on the Wavefolder stage). Swift has no FX animations.
- Web: the tempo (BPM) field sits to the right of INVERT KEYS on the bottom row. Web-only; the native apps have no equivalent transport row.
- Web: the user manual is restructured into 13 sections with an entry for every FX (including Interstellar), detailed MPC pads, and sequencer probability.

## Layout and text pass
- XY pads are matrix sources (`xy1x` ... `xy4y`, eight in total). Web: Matrix source menus and the overview grid. Core: `S_XY1X..S_XY4Y`, `vv_engine_set_xy(engine, pad * 2 + axis, value01)`; the JUCE XY pads call it on every move; the SwiftUI app has an XY page under Mod (four pads, snap-back switch). Covered by `Core/tests/engine_tests.cpp` ("xy source modulates the level").
- MIDI CC map: LFO 5 / 6 rate (CC 113 / 114), Procedural filter cutoff / resonance / keytrack (CC 115 / 116 / 117), Procedural amp A / D / S / R (CC 118 / 119 / 85 / 86), Master tone (CC 87), WT / FM mix (CC 88). Web table, Core (`FullEngine::midi` / `pollCc`), host parameter table (`Core/parameters.json` regenerated, 202 parameters) and the JUCE / SwiftUI MIDI panels all list them.
- Oscillator dials are 10 % smaller: web (`enhanceToKnob` size 81 on the oscillator pages) and JUCE (`Panel::setKnobScale(0.9f)` on the oscillator, FM engine / operator, sampler slot and procedural generator panels). The SwiftUI dials are a single standard size, so there is no separate large dial to shrink.
- Vocoder rows: web and JUCE start a new row for `4 Pole Bands` (with Formant Shift, Envelope Release, Band Resonance) and for the drone switch (with Pitch Scale, Root, Correction Speed, Snap to held keys). SwiftUI forms already list one control per row in that order.
- Removed web help lines that the manual covers (sequencer gate / sidechain, chord maker, global envelope, Bluetooth MIDI, in-app purchase panel). Appearance options each sit on their own Settings row; APPLY (restarts engine) has its own row. These Settings rows are web-only (JUCE and SwiftUI have no engine restart button).
- Cleanup: the engine no longer contains the stage-1 placeholder voices (it is silent until a preset is loaded); the unused Wavefolder game theme and the Power Panning unlock gate are gone from the web app.


## Rename, Mixer, Void, drum sequencer, EQ graph (2026-11 pass)
- Visible name is Vektur Vortex (JUCE title, SwiftUI title, docs). Internals deliberately keep "vecta" (preset key `vectaVortexPreset`, AU codes, `VectaAUv3`, `vectaVortexMidiProfiles`, class and target names). Preset state version is 2.
- Mixer: 24 tracks, fader -60..+12 dB, `MIX:<track>` matrix destination (+-24 dB), per-track pan, meters via `vv_engine_mixer_level`. Engine + JUCE `ScreensMixer.*` (headless snapshot checked) + SwiftUI `MixerView` (uncompiled). The layer EQ is mono (as on the web), so pan is only audible through FX sends.
- Drum sequencer: engine + `vv_engine_drum_step`; no dedicated native UI yet.
- Stereo Spread FX: engine only, no native stage UI yet.
- Vektur Void: Core logic golden-tested; JUCE view snapshot checked; SwiftUI view uncompiled. Levels 5-6: the web softlock is FIXED in both web and native (your request): scenery stops spawning once the enemy quota is met, and the three slabs of the Z-wall are now gates that slide shut and open (2.2 s cycle, staggered by a third, retracted slabs are parked off-screen so they cannot hit the ship). Bridges are unchanged. Web and C++ stay frame-for-frame identical (golden regenerated: `tools/make_golden_void.js`).
- EQ: draggable-band graph (LOW / BELL / BELL / HIGH, 40-16000 Hz, +-15 dB, 0.5 dB steps) replaces the dials in JUCE (`EqGraph.*`, curve unit-checked against the engine biquads) and SwiftUI (`EQGraphView`, uncompiled).
- Tabs (JUCE): OSCILLATORS, XY PADS, MATRIX, FX, SEQUENCER, LFO, CROSS MOD, VOCODER, SCOPE, MIXER, PRESETS, SETTINGS, HOST in web order. SwiftUI keeps its older 8-tab layout plus Mixer.

## NOT done (inventory, nothing hidden)
- JUCE now has the persistent bottom area under every tab (`PlayFooter`: macros, master, WT/FM and sample mixers, keyboard, wheels, scale/arp options, MPC pads, zones, MIDI learn, panic); the SCOPE tab holds the scope only. The footer also has the web's transport row: VIEW <layer> (opens OSCILLATORS + layer sub-tab), PLAY, HOLD and SOLO per layer (modifier-click = MUTE, the web's double-tap), BPM. HOLD off releases all voices (the web releases only that layer's). SOLO/MUTE needed a new engine feature (`vv_engine_set_solo_mute`, tested); it was missing from the engine before. CHORD analyzer, swing menu, MPE, OS and GENERATE buttons of the web transport are not in the footer. SwiftUI has no footer; its Sound tab has a per-layer Hold / Solo / Mute section instead. Tab labels are tight at 1100 px wide. Vektur Vocal key feel (single surface glissando, strike-height velocity) and token/LED restyle not applied. Native UI for SID, DX7 6-op, Spread, drum sequencer, LFO morph/smoothing is missing.
- SwiftUI was never restructured to the web tab layout; none of the Swift code has been compiled (Mac pass skipped by instruction).
- (Superseded by the 'Gap-closing pass' section at the end of this file.) Engine ports listed here as missing were closed there, except the items named in that section. (Done since: SID live PWM, SID WT arp engine+JUCE+SwiftUI editor, solo/mute, WT ring mod, Void gates.) Other web FX animations were not audited.

## Update (later same pass)
- SID voices tab and DX7 6-op panel added to the native Sound screens (JUCE tested headlessly; SwiftUI uncompiled).
- Added later: Stereo Spread FX stage, Drums sub-tab of SEQUENCER (16 lanes, per-lane length), LFO smoothing / morph knobs and the full web LFO waveform list (staircase, stairsDown, stroll, organic, morph, morph2), SPECTROGRAM tab (the scope in spectrogram mode), WT ring mod (engine + JUCE + SwiftUI controls).
- WT ring mod deviation: carrier / modulator saw and square are PolyBLEP-corrected, not WebAudio's wavetable band-limiting; no golden exists (WebAudio native oscillators), the test checks the sum / difference tones analytically. Pitch-mod routing into the ring pair is not implemented.
- (Superseded by the 'Gap-closing pass' section below.) (Done since: SID live PWM, SID WT arp engine+JUCE+SwiftUI editor, solo/mute, WT ring mod, Void gates.)


## Gap-closing pass (2026-11)
Done, with tests where stated:
- Chord analyzer (footer CHORD label), DX7 per-operator mod destinations (`D_DX_*`), swing / MPE / oversampling / quick-generate footer row.
- Presets now carry their own content: `sampleLibrary` (base64 16-bit WAV) and `customWavetables` are registered into the engine on load (hash-cached). Core tests cover both.
- Wavetable authoring: harmonics editor, formula (safe JS-expression subset), draw, audio import (`wt_tools.hpp`, `vv_wt_*`, JUCE `AuthoringTools.*`, SwiftUI `AuthoringTools.swift`).
- Sample tools: time-stretch (WSOLA), warp markers, spectral resynthesis, import (`sample_tools.hpp`, `vv_audio_*`). Authoring test target `vv_authoring_tests` (35 checks).
- Glitch companding (mu-law blend, matches the web worklet formula; unit-checked) and the Retro DSP model menu (SP-1200 / Mirage / Fairlight) in JUCE and SwiftUI. The menu assumes a 48 kHz rate when it computes SR reduction (web uses the live context rate; at 44.1 kHz the Mirage model would round to 1 instead of 2; SP-1200 and Fairlight match).
- 16 ambient / aggressive-bass wavetables exported from the web generators (`tools/export_wavetables.js`, 72 tables total; the 56 earlier tables re-export byte-identical). They appear in the table pickers under their ids (no pretty labels natively).

Documented deviations: formula evaluator is a subset (web uses `new Function`); sample tools run synchronously on the UI thread; no zoom in the warp editor; Swift code is uncompiled.

Added after that (same pass):
- IMAGE -> AUDIO (`imageToAudio`, `vv_audio_from_image`; JUCE `ImageAudioDialog`, SwiftUI `ImageAudioSheet`). Deviation: the web downsamples the image with browser smoothing; native uses an area average, so results are close, not bit-identical. Test: a lit row renders only its harmonic.
- Sample RECORD (`vv_engine_rec_*`; JUCE `RecordDialog`, SwiftUI `RecordSheet`). The audio thread only copies into a buffer allocated beforehand; arm-on-sound, 2 min limit, save into the preset's sample library and load into the slot. Tested in Core (armed start, external input, full buffer) and in the JUCE UI test (master capture, save). JUCE records master output or external input. SwiftUI records master output only: the AUv3 is an instrument with no input bus, so `vv_engine_feed_input` is never called there (this also means the Hybrid Vocoder's external input has no source in the AUv3 build). Adding an input bus needs testing in a real host.
- `vv_engine_sample_rate` added so the tools write samples at the engine rate.

DAW recorder + click (added because the app also runs standalone): `Core/include/vv/daw_recorder.hpp`, C ABI `vv_daw_*`.
- REC / STOP / elapsed time / settings in the footer (JUCE `DawRecorderUi.*`, SwiftUI `DawRecorderView.swift`). Settings: bit depth 16 | 24, sample rate 44.1 | 48 | 96 kHz, click Off | Beep | Rimshot, takes list (rename, save as / share, delete).
- The audio thread only pushes into a preallocated ring; a UI timer drains it. The click (same envelopes as the web) is added after the recorder tap, so it is heard but never recorded. Beats come every 60/bpm from the start of the recording (the engine tempo, so host tempo in AUv3).
- A stopped take is resampled (windowed sinc, not the browser's resampler) and written as a stereo PCM WAV to `Documents/Vektur Vortex Recordings`; takes up to 60 s are also added to the preset's sample library, as the web does (longer ones are file-only so presets do not balloon).
- Deviations: takes are limited to 10 minutes (memory); the takes list lasts for the session (the files stay on disk); no download button (file + share sheet instead). Tests: Core (beat spacing, click not recorded, WAV sizes and header, resampler round trip) and a JUCE UI test (REC, render, STOP, a 24-bit 44.1 kHz file appears, library copy). SwiftUI side is uncompiled. In an AUv3 the file lands in the extension's container.

Still NOT ported / not done (honest list):
- Vektur Vocal visual restyle (JUCE): done from the Vocal web app you uploaded (same tokens as the web app's `vk-skin`: well #0A0B0F, panel #12141B, control #1B1F2A, border #232733, silk #B3BAC9; edged gradient panels with corner screws and an accent title bar; push buttons with drop shadow and lit LED when on; dark dropdown wells with accent text; Vocal key colours). Layout, tabs and windows are unchanged. Not done: the Vocal app's single-surface key glissando was not compared (JUCE keyboard default drag-to-play and strike-height velocity are used); SwiftUI is not restyled. (An earlier note here said the Vocal spec was missing; that was wrong, the file was in your uploads.)
- No Mac compile or on-device test of any Swift / AUv3 code.

Test status at this point: Core ctest 22/22 (including rt_alloc), JUCE `vv_ui_snapshot` "all UI tests passed" (headless Linux, not a device).

- Launch splash image removed from both native UIs (your request); `Assets/splash.jpg` and `SplashView.swift` deleted. The web app never had one.

## Footer fixes (2026-11-07)
- Master tone dial added next to Master volume (host parameter `master_tone`, CC 87) in the JUCE footer and as a slider in the SwiftUI root view. It existed only on the FX / Settings page before.
- PLAY is green; HOLD buttons are teal-green shades and SOLO buttons blue shades (JUCE and SwiftUI).
- (Closed in the next section.) The native sampler slot page had no waveform / transient-slice view like the web sampler's.

## Sampler waveform editor, 16 slices, 64 voices, PWM-speed dial (2026-11-07)
**Web (`Web/full-app.html`)**
- Auto slices 32 -> 16 (both `detectTransients` call sites and the manual text). The sequencer's slice column simply addresses up to 16 slices.
- Polyphony list now goes 1, 1 Legato, 2..16, 24, 32, 48, 64. Default **64** (`state.maxPolyphony`, `enforcePolyphony` fallback). Old presets keep the value they saved.
- New **Hz** dial beside PWM on each wavetable oscillator (`osc.pwmRate`, 0..20 Hz, 0 = static pulse width). Long-press modulation works through `modKeyFor` (`WT:osc1PwmRate` / `WT:osc2PwmRate`, +-10 Hz at full amount) and the two destinations are in the matrix list. The sweep is a per-sample sine inside both wavetable worklets (`pwmRate`, `pwmRate0/1` parameters).
- **Design choices that are mine, not from any earlier spec:** the sweep is a sine, its depth is fixed at +-0.4 around the (modulated) PWM value and clamped 0.02..0.98, phase restarts per note. Tell me if you want a depth dial, another shape, or tempo sync.

**Native engine**
- `maxPolyphony` default 64 (Core model, performance layer; the voice pool was already 64). JUCE Settings stepper and SwiftUI Settings stepper now go to 64. Tested: 64 simultaneous notes sound, the 65th retires the oldest. **Found while testing:** the pool was shared by all five layers and every note took a slot on *every* layer even when that layer had nothing to play, so a default patch ran out at 12 notes. Fixes: a voice with no active source is now freed immediately, and the pool grew 64 -> 128 voices (about 119 KB each, ~15 MB total). The 64 limit counts held notes; a patch that sounds on several layers still needs one voice per layer, so 64 notes on 3 layers can exceed 128 and the oldest voice is stolen.
- `osc.pwmRate` + destinations `osc1PwmRate` / `osc2PwmRate` (global destinations, route scale 10 Hz per unit). The sweep runs per sample in the voice render (the 100 ms matrix tick is far too slow for it - the first attempt at tick rate aliased and was replaced). No allocation, one `sin` per oscillator per sample.
- New engine/C-ABI pieces for the editor: `vv_engine_audition_slice(slot, slice)` / `vv_engine_audition_off` (plays the whole region or one transient slice with its own reverse + pan, using the same p-locks as the sequencer's slice column), and `vv_engine_sample_head(slot)` (lock-free playhead as a fraction of the source buffer). Tested in `authoring_tests`.

**JUCE (`Juce/Source/SampleEditor.*`)** - responsive waveform panel at the top of the Sampler page (fills the width, wraps its buttons) and a FULL SCREEN EDIT overlay:
- start / end handles, fade in/out shading, loop markers (draggable when Loop is on and the points exist), up to 16 numbered slice markers (reverse = red, pan tick), live playhead with the slice it is in lit;
- drag markers (handles win over a slice sitting under them), wheel / pinch zoom, shift-drag / horizontal wheel pan, double-click reset, ZOOM -/+/RESET VIEW, snap-to-zero-crossing, alt-click adds a slice, right-click a slice for reverse / centre pan / delete;
- CROP TO SELECTION, CUT (SILENCE) (3 ms edge fades like the web), NORMALIZE, DETECT TRANSIENTS (the web's energy-onset algorithm, max 16), WARP MARKERS... (opens the existing sample-tools warp editor);
- full screen adds the slice list: position, reverse, pan, press-and-hold PLAY audition, delete, plus AUDITION of the whole region.
- Audio edits write a processed copy into the preset's sample library (undoable) instead of editing a buffer in place; the original is not kept after crop / cut / normalize, same as the web dropping its `origBuffer`.
- **Long-press modulation** (`Knob::setModDest`): hold a dial 550 ms (without dragging) to pick a source; a cyan dot marks dials that have routes. Wired to the wavetable oscillator dials (Position, Level, Pulse width, Hz, Phase, Start/end, Segment) with the web's `modKeyFor` keys. **Other dials are not wired yet** - the web wires many more; that part of the "long-press modulation menu" gap stays open.

**SwiftUI (not compiled)** - `SampleWaveformEditor.swift` (same features; pinch zoom + view slider instead of wheel / shift-drag, `+ SLICE` instead of alt-click), `SliderRow.modDest` long-press (context menu) on the same wavetable dials, Hz slider, 16-slice / 64-voice limits.

**Known gaps in this pass**
- No on-device or compiled check of any Swift; the JUCE UI is tested headless (mouse-event simulation, snapshots), not on a touch device.
- Warp markers are not drawn on the waveform (they live in the warp editor of SAMPLE TOOLS).
- The old SwiftUI sampler still has no Vocal restyle.

Test status: see the final report in the conversation (numbers are recorded there after the last full run).

## Sync to the 2026-11-08 web app (index.html, 20,204 lines = Web/full-app.html)

Engine (Core) now follows the web for: procedural models (pluck/modal/breath/chaos/bow, Stiffness, Strike material, Bore, key-tracking, Coupling), WT unison Blend/Phase + stereo path, filter bypass (`on`), multiband saturation, transparent send-less FX stages, all `GEN:` destinations (SID voices, WT2/FM2 envelopes, unison Blend/Phase, vibrato, per-layer EQ bands), and live updates of held notes (GEN detune/octave/semi/fine retune, unison width/blend/phase, pan, procedural level/pan; a released note keeps its last target as in the web). Samples with a negative glide rate now end exactly like the web's AudioBufferSourceNode. The retired LPC "formant" vocoder mode is gone from the web, so a preset still naming it is silent in both.

Reference suite: 163 scenarios rendered through the real web app; all pass with tolerances.txt. Open items (tolerance > 1e-3, not yet explained): mod_fm_ratio (late-note live ratio), mod_fxp_reverb_delay, smp_plock_slice, mod_gen_pro_* (first-tick slew timing), xmod_loop/xmod_pm. Web bug fixed 2026-11-08: the three multiband-saturation band processors are now initialised at creation (`refreshAll(p, true)`); native matches the fixed web (scenario fx_saturation_multiband).

EQ: the JUCE EqGraph now mirrors the web equaliser (magenta curve, numbered nodes, readout, arrow keys, 30 Hz-20 kHz); the live spectrum behind the curve is not ported (no analyser tap in the plugin) and the Swift EQ is unchanged. UI still to bring over (JUCE/SwiftUI, uncompiled in this session): long-press modulation on every dial, warp markers on the waveform, DX7 SysEx import/export.

## Web look (2026-11-08)
The web app now ships a "Plugin (JUCE) look" skin (`<style id="vv-juce-look">`, on by default, Settings > Appearance toggle, remembered in localStorage) that restyles the existing web layout like the plugin: navy panels, a row of bordered tab buttons, small dim mono labels, white value boxes under the dials, pill switches, cyan accents. It is a visual layer only; the web's layout and functions are unchanged, so the screens are not rearranged to match the plugin (for example the plugin scrolls one page above a fixed footer, the web keeps its own keyboard/controls strip).

## Sync: Settings additions (web)
Web: Dropdown style option removed (charcoal fixed); added Background audio, Do not sleep (Screen Wake Lock) and App MIDI channel (Omni/1–16, ignored under MPE); vocoder labels 11px. Native (Core/JUCE/Swift) mirror of these three settings NOT yet done.

## Sync: settings mirrored + live dials (2026-11-08)
- Settings (web + JUCE + SwiftUI): Dropdown style removed; Background audio, Do not sleep, App MIDI channel (Omni/1-16) added. Core: `FullEngine::setMidiChannel` filters channel messages in `midi()` (ignored while MPE is on); bridge `vv_engine_set_midi_channel`; test in engine_tests. JUCE: Do not sleep = `Desktop::setScreenSaverEnabled`; Background audio is a stored flag only (plugin/standalone host decides). iOS: `isIdleTimerDisabled`, background pause when Background audio is off. UNTESTED on device / JUCE / Swift (never compiled here).
- Web: wavetable-page labels fixed at 11px; vocoder labels 11px.
- Held notes: SID voice level / pitch / waveform / ring / sync / ADSR / PW now follow live in web and native (A/D/R only matter for later phases). Web drift dial live. FM per-op ratio/feedback/pan and WT segment / glide / FM glide remain note-on-only in the audit; glide is note-on by definition.
- FX per-layer serial routing: see earlier sync note.

## Sync: Recorder moved beside PANIC (2026-11-08)
Web: DAW recorder block moved from the macros row to the transport bar, immediately left of PANIC!. JUCE: `daw` laid out immediately left of `panic` in the bottom row (PluginEditor.cpp). SwiftUI: `DawRecorderBlock` moved from PlayFooterView into KeyboardOptions left of PANIC!. Untested (JUCE not buildable here; Swift never compiled).

## Sync: Poly dropdown beside the recorder (2026-11-08)
Web: Poly select moved into the transport bar, left of the recorder. JUCE: new Poly combo (maps to preset maxPolyphony + legatoMode) left of the recorder; SwiftUI: Poly picker in KeyboardOptions left of the recorder. Untested (JUCE/Swift not built here).

## Sync: multi-select Arp layers (2026-11-08)
Web: the Arp layer dropdown is now a tick-list (state.arp.targets; state.arp.target kept as first entry, old presets migrate). Core: ArpCfg.targets + targetMatch loops over them (compiles; no web reference scenario because the web arp runs on a real-time timer). JUCE: popup tick-list that re-opens after each pick; SwiftUI: Menu with checkmarks. JUCE/Swift untested.

## Sync: analyzer / multiband compressor / mixer meters / master limiter (2026-11-08)
- Spectral FX: `tilt` (dB/oct about 1 kHz) and `disperse` added to Core (fx_spectral_granular.hpp), JUCE and SwiftUI. Reference scenario `fx_spectral_tilt_disperse` matches the web render (maxAbsErr 1.4e-4).
- Multi-band compressor `mbcomp` (3-band LR4, per-band down/up dynamics, OTT mode): Core `fx_mbcomp.hpp`, JUCE (ScreensFx.cpp) and SwiftUI (FXRackView.swift). Reference scenarios `fx_mbcomp_normal` / `fx_mbcomp_ott` match web (maxAbsErr 1e-4). The draggable-crossover graph is web-only; native uses crossover sliders.
- Master limiter + BS.1770-4 loudness (M / S / integrated LUFS, peaks, GR): Core `master_out.hpp`, bit-exact bypass when off, bridge `vv_engine_set_master_limiter / get_loudness / reset_loudness`, JUCE + SwiftUI Settings. Unit test `master_out` (1 kHz level, ceiling, transient clamp). No web reference scenario (the offline harness has no hook for it).
- Mixer meters: calibrated -60..+12 dB scale with ticks, peak hold and average bar in JUCE and SwiftUI. NOTE: the engine reports per-voice block peaks, so the second bar is an averaged level, not true RMS (web measures true RMS).
- NOT ported: the Spectrogram/Phase analyzer (goniometer, correlation meter, phase overlay, palettes, FFT/window/scale options, freeze/snap). Native has no spectrum analyzer view yet (same gap as the EQ spectrum), so this needs a Core analysis module + JUCE/Swift views.
- JUCE and Swift changes are untested (not buildable here); Core is built and all 23 ctest suites pass.

## Sync: new sound content (2026-11-08)
- Wavetables (6): chipNesVoices, chipSidCombined (chiptune); dnbDarkChoir, dnbHollowChoir, dnbCathedralChoir, dnbDemonChoir (dark DnB choirs). Exported to Assets/Wavetables/*.vvwt + manifest.json via tools/export_wavetables.js.
- Sampler (8): Synth Choir Ahh Wide / Ooh Soft / Minor Dark / Hollow Fifths / Cathedral / Stab / Breathy Air / Rising Swell, rendered to Assets/Samples via `tools/export_builtin_samples.py "Synth Choir"` and appended to builtin_samples.json (Misc folder). Native lists read the manifests, so no UI code changed.

## Sync: piano roll, automation, MPC record, mixer M/S/A/E, header logo (2026-11-09)
Ported + tested (Core, ctest `vv_roll_automation_tests`): roll data model (`pat.roll`) and sample-clocked playback (loop region, probability, ratchet, replaces step grid when present), automation lanes (8 samples/beat, destination lanes via the modulation engine, source lanes), mixer mute/solo/master mute gating, host-transport sync (`vv_engine_set_host_transport`, `vv_engine_transport_beat`), preset round-trip of roll/automation/mixer M-S-A.
Ported, UNTESTED (JUCE/Swift cannot be built here): JUCE Automation screen (SID-adjacent 6th Sound tab), mixer M/S/A/E + Master strip + vertical limiter/LUFS column (removed from Settings), host-transport hook in the processor, header logo (JUCE + SwiftUI), JUCE piano roll page (`ScreensRoll.cpp`, SEQ "Piano Roll" tab): tap-add, double-tap delete, drag/resize, marquee, copy (Alt-drag), semitone/octave arrows, Quantise, grid 1/4..1/32 + triplets + off, loop bars, velocity lane, underlay, scale lock, mini keyboard, scroll/zoom, probability/ratchet via right-click.
NOT ported natively yet: chord-sequencer reflection onto the roll display, per-note P-lock inspector, MPC "Record To Piano Roll" + Pan/Vol/Pitch/Stutter side controls (JUCE and Swift), all SwiftUI screens for roll/automation/mixer M-S-A-E/Master/limiter column (engine side done).

## Sync: New Note Next / Random Sample + Send Transients to MPC (2026-11-09)
Web: Samples layer global button "NEW NOTE NEXT SAMPLE" (tap = on/off, quick double tap = "NEW NOTE RANDOM SAMPLE"), stored as layers.SMP.slotAdvance = off|next|random. Each new note (keyboard, sequencer, roll, arp) plays ONE slot: Next cycles through the playable slots in order, Random picks any playable slot. Slots with no sample, zero level, outside the note's key zone, or excluded by the arp split are skipped (so 16 notes cycle all 16 only when all 16 slots are loaded). A roll note recorded from an MPC pad keeps its own slot. "SEND TRANSIENTS TO MPC" (after DETECT TRANSIENTS): transient k becomes the one-shot of pad k (reverse/pan per transient honoured); pads beyond the last transient revert to their whole sample; stored in the preset as extra.mpcPadSlice.
Core (tested, ctest vv_roll_automation_tests `testSlotAdvance`): LayerCfg.slotAdvance, PLocks.slot, Synth::pickAdvanceSlot, startVoice slot filter, C_PadSlice now carries the pad number; Bridge vv_engine_pad_slice_on.
JUCE/Swift (UNTESTED, not buildable here): toggle with double-tap (Screens.cpp SlotAdvanceButton, Controls.swift SlotAdvanceRow), SEND TRANSIENTS TO MPC in the sample editor (SampleEditor.cpp, SampleWaveformEditor.swift), MPC pads play slices (PluginEditor.cpp, MpcPadsView.swift). Native MPC pad hits do not honour a slice's loop settings (one-shot), same as web.

## Battery Saving Mode (2026-11-10)
Device setting, never stored in presets (web: localStorage `vv-battery`; JUCE: `battery_saving.json` in the app-data folder; Swift: UserDefaults / App Group suite). Master toggle, profile (Light / Balanced (recommended) / Maximum Efficiency) and five switches.
Core (power.hpp, tested by `vv_power_tests`, 25/25 ctest pass): one atomic word from UI to audio thread, adopted at the next 128-frame quantum; no locks/allocation. Off = bit-identical rendering.
- Effects: a stage whose input AND output stay below a threshold for a guard time is skipped (outputs exact zero) until signal returns. Light 4 s / -140 dB, Balanced 1.5 s / -130 dB, Maximum 0.5 s / -120 dB.
- DSP: layer-EQ idle skip (same guards) + flush-to-zero/denormals-are-zero for the render call (x86 MXCSR, ARM64 FPCR). This was the largest single saving on x86.
- Voices: a RELEASED voice silent for 0.5 / 0.25 / 0.1 s is freed (held notes never touched). Measured benefit in the tests: only when a voice is muted during a long release; normal voices already end at their own stop time (0 reaped in the tail tests).
- Modulation: a glide older than 30 / 16 / 10 time constants is treated as arrived (skips an exp() per sample). The 100 ms matrix tick is NOT slowed: it is already control-rate and slowing it would audibly step modulation.
- Visualisation: refresh divided by 2 / 3 / 6 (web rAF gate, JUCE VV_UI_THROTTLE, SwiftUI TimelineView intervals), twice as fast while dragging.
- Low Power Mode / thermal state (Swift only, optional): raises the effective profile to at least Balanced (Light if the mode is off); observed by notification, user settings never rewritten; status text shows why.
Not done / limits: not measured on iPhone/iPad hardware or Instruments (not available here); no energy measurement; spectrum analyser/oscillator-level and filter-coefficient caching were not changed (the engine already caches static parameters and band-limits oscillators; no safe gain found); JUCE and Swift UI are untested; the browser build only gets the visualisation throttle (engine switches are stored but have no effect on WebAudio).

## XFX MODE + amp-envelope vertical faders and Repeat (2026-11-10)
Web (authoritative, `new-web.html` / `Web/full-app.html`): XFX MODE = a different sound on every key (8 banks, per-key parameter overrides for the whole parameter registry, per-key layer on/off, undo/redo, banks save into the preset as `extra.xfx`); amp envelopes A/D/S/R are vertical faders and each has a Repeat menu: `env.rep` = 0 off, 1..8 extra Attack+Decay cycles, -1 continuous until note-off (also in the SMP slot / global ADSR and the SID envelopes; not for the 6-op DX7 voice). Both are in the in-app manual.

Core (C++, tested - `vv_xfx_env_tests`, part of ctest):
- Model: `EnvCfg.rep`; `XfxTable` + `modelio::buildXfx` build the per-key resolved `LayerCfg` copies on the control thread from `extra.xfx` (same JSON as the web app, so a web preset carries its XFX bank into the native app and back). `xfxApply` handles the pseudo-params (`slotOn`/`useSlot`).
- Synth: a voice started on a programmed key keeps a pointer to that key's resolved layer (`Voice::xcfg`) and uses it for everything it does until it ends; the audio thread only does a hash lookup. `Performance::keyL` + `layersForNote` honour per-key zones / hold / arp assignment and the per-key layer switch (on/off/inherit).
- Lifetime: retired presets are no longer freed while any XFX voice sounds (`FullEngine::trimRetired_`, `Synth::anyXfxVoice`); a wholesale preset swap clears the pointers (`setPreset`), and if the retired list were ever full `dropXfxRefs()` clears them instead of leaving a dangling pointer.
- Envelope repeat: `Synth::EnvRep` schedules extra cycles just in time (cycle = max(10 ms, a+d+2 ms); each cycle dips to 0 over 2 ms, rises to peak over A, falls to sustain over D), 2-quantum horizon so the fixed-size `Param` timelines never overflow. Applies to WT/FM/PRO group envelopes, sampler slot / global envelopes and SID (new retrigger counter `SidVoiceParams::rt` restarts Attack from the current level even in release). Ends at note-off; legato notes and the 6-op voice are skipped.
- Control API: `FullEngine::xfxAction(action, argsJson)` + `xfxExport()`, C-ABI `vv_engine_xfx_action` / `vv_engine_xfx_export` (Bridge). Individual overrides go through this, not `set_value`, because override keys contain dots (`"WT.env.rep"`).
- Tests (`tests/xfx_env_tests.cpp`): attack counts for rep 0/1/3/8, continuous repeat and silence after release, first-cycle timing, per-key override (key 64 repeats 3x while key 60 does not) both through `Synth` and through `FullEngine`, layer switch via `Performance`, the action API (accept/reject/prune/copy/reset), preset-swap safety, disabled XFX applies nothing.

JUCE (UNTESTED - JUCE cannot be built in this sandbox): `Widgets.{h,cpp}` `VFader` + `Panel::vfader`; `addEnv` now draws four vertical faders + a Repeat menu everywhere it is used (layer envelope, sampler global ADSR and slot envelope); new `XfxEditor.h` (on/off, 8 banks + rename/clear, keyboard key picker with audition, per-key layer inherit/on/off, override list with a curated parameter menu or any typed registry path, copy-to-next-key, clear key) opened by an "XFX" button beside the Samples mixer in `PluginEditor.cpp`.
Swift (UNTESTED - never compiled): `VFaderControl`, new `EnvEditor` (faders + Repeat picker), `PresetStore.xfxAction/xfx()/auditionNote`, `XfxView.swift` (same feature set as the JUCE editor) opened from the Sound tab's XFX toolbar button.

NOT ported natively (honest list): the web editor's full parameter browser (tabs, search, per-parameter widgets for all ~950 registry entries, value readouts with units, per-parameter clear buttons, bank save/dirty indicators, undo/redo inside the editor) - the native editors cover the common parameters via a menu and any parameter by typing its registry path, and the engine honours every registry path; XFX does not yet re-resolve a *held* voice when the override changes (same as the web app: held notes keep what they were built with); the SID repeat has timing resolution of one 128-frame quantum (~2.7 ms at 48 kHz) because the SID chip steps per sample inside the quantum; env repeat for a voice started while legato is off only. The web-app SID worklet change ('rt' retrigger param) was only checked for "SID note still sounds".


## GENERATE rebuild (2026-11-10)
- Web: GENERATE is now recipe-driven (`gen/make_recipes.py` -> `gen/recipes.json`, spliced into the web app by `gen/build_web.py`). 8 categories, 38 variants, all five layers, matrix, FX, sequencer, arp, SID wave-table arp and XFX banks. Scale lock (+ a chosen scale) makes sequencer pitches, SID arp steps and XFX-programmed keys follow the scale.
- Native: `presetgen.hpp` is a line-for-line port of the web interpreter (same mulberry32 RNG). `Core/tests/presetgen_parity_tests.cpp` checks 118 cases (every variant, three seeds/scales, and the four fixed XFX presets) against the web output: 118/118 identical. Regenerate the data with `node tools/export_gen_parity.js` and the headers with `python3 tools/export_gen_recipes.py`.
- Named XFX presets (AYFXTWN, ENO XFX, Glitch XFX, IDM kits and clip leads, SID set; Jungle presets removed in Round 20) are in the DMT CYMATICS bank (web folder `XFX`; native `Assets/Presets/DMT CYMATICS - XFX - *.json`).
- Found and fixed while doing this: the native multi-filter lacked the SID 6581 / 8580 filter types (index 11 / 12), so any preset using them was silent. Ported, with bit-exact golden vectors from the web worklet (`mf_t11`, `mf_t12`).
- The 64 synthesized drum hits are now bundled (`Assets/Samples`, 192 samples) and web `ensureBuiltInSample` can build them on demand when a preset names one.
- Per-key XFX addressing of SID wave-table-arp steps now covers 8 steps (was 3).
- Untested: JUCE and Swift UI changes (Drum / XFX in the GENERATE menus, scale passed from the keyboard globals, variant name in the file name); sonic quality was only checked as "audible, finite" by measurement, not by ear.

## Round 19 — Roll REC + Rotary / Amp / Tube
- **Piano-roll REC** (web `ROLLREC`; native `roll_rec.hpp` lock-free ring + `Recorder`, bridge `vv_engine_roll_rec_*`, JUCE `ScreensRoll.cpp` REC/mode/Q/CLR, SwiftUI `RollRecorder.swift` REC row in the pattern editor because SwiftUI has no roll editor). Records notes + velocity, pitch-bend and mod-wheel lanes (`roll.lanes.bend/mod`), latency-compensated, replayed via `applyRollLanes`.
- **Rotary Speaker** (Drive, Stop, Slow/Fast), **Amp + Cabinet** (Twin, JC, Small, Brit), **Tube Overdrive** as a Saturation type (JUCE shows it as "tube"). C++ ports match the web worklet bit-for-bit via `rotary_amp_golden.json`.
- Swift and JUCE changes are uncompiled/untested (written on Linux); Core C++ passes 47/47 ctest.

## Production audit — batches 1–2 (2026-11-20)
- **Batch 1 — `parseWav` hang (assets.hpp):** chunk advance `8 + sz + (sz&1)` wrapped in 32 bits for sz = 0xFFFFFFF7/8 so the scan never advanced (malformed WAV froze the loading thread). Now 64-bit. Test: `wav_robust` (fails on the old code, passes now).
- **Batch 2 — audio-thread allocations on live edits (synth_impl.hpp `updatePreset`, json.hpp):** FX params / vocoder were compared by building `jsonDump` strings on the audio thread (~94% of 17–21 allocations per edit). Replaced by allocation-free `jsonEqual`; an adopting block now allocates 1 time. Test: `live_edit`.
- Not changed (documented): first render after a *preset load* still allocates (up to ~500) and can take ~19 ms — needs off-thread staging, deferred as high-risk.
- Evidence/plan: Stage 3–4 findings; fuzz (660 mutated presets under ASan/UBSan) clean; voice-steal click and wavetable-size concerns not reproduced.
- Full native ctest: 49/49 pass. Swift/JUCE untouched and uncompiled.


## Round 20 — samples and XFX presets
- **Samples:** 32 new synthesized one-shots (16 IDM / hard / modern: Hardstyle Reverse Bass Kick, Gabber Distorted Kick, Trap 808 Slide, Neuro Bass Reese Hit, IDM Microglitch Burst, Autechre Metal Zap, Hard Techno Rumble Kick, Industrial Clang Hit, Dubstep Wobble Bass, Modern Pop Snap Snare, Breakcore Snare Roll, Phonk Cowbell, Hyperpop Detuned Stab, Formant Glitch Chop, Sidechain Pump Riser, Reverse Impact Swell; 16 ambient drones / pads / keys / textures). Web generators in `gen/xs3.js`; native WAVs in `Assets/Samples` + `builtin_samples.json` (224 entries).
- **XFX presets:** Jungle XFX presets removed (Junglist XFX Bass, Amen Akai Chop, Amen Snare Roll, Amen Half-Time Metal, Ragga Tearout). 8 new IDM XFX presets added (Hard IDM Kit, Microglitch Polymeter, Trap Phonk IDM, Breakcore Roll Chops, Autechre Machine Noise, Hazy Tape Lead, Neuro Stab Sequence, Glitch Pluck Arp). Recipes in `gen/idm_named.py`.
- **Regression found and fixed:** the Round 18 recipe rebuild had silently dropped 29 named presets (LUA pads/basses/leads and SID XFX) from the web bank while the native bank still had them; they are restored (`gen/named_legacy.json`), so web and native match again (DMT CYMATICS: 58 presets).
- Regenerated: `presetgen_recipes.hpp`, `Core/tests/data/gen_parity.json`, `Assets/Presets/DMT CYMATICS - *.json`.
- **C++ generator gap closed:** `presetgen.hpp` now implements recipe `lua` (LUA instrument slot) and `clips` (XFX clip generation, identical rng order); previously LUA and clip-lead named presets never matched the web in `presetgen_parity` (hidden because the old parity set had few named cases). Parity: 154/154 cases match.

## Round 21 — icon navigation bar and FX stage header (web to native)
- **Icons:** one source, `tools/export_nav_icons.py` (24x24 grid, 1.6 stroke, round caps), generates `Juce/Source/NavIcons.h` (juce::Graphics drawing) and `Apple/Shared/NavIcons.swift` (SwiftUI paths, `NavIconView`, template-image renderer for TabView). The web app uses the same art as an inline SVG sprite.
- **JUCE tab bar:** `Widgets.cpp` draws tab buttons as dark recessed faces with the icon (properties `tabBar` + `navIcon`; selected = cyan icon, faint tint and cyan edge, hover = brighter silver and a cyan edge). The button text is still the section name (accessibility name and the `getButtonText()` lookups), plus tooltip/title/description, and tapping shows a brief name pill under the tab (`Editor::showHint`) because touch has no hover. A `juce::TooltipWindow` was added to the editor (the existing `setTooltip` calls never showed without one). HOST uses the automation-curve icon; there is no C64 tab in the JUCE editor.
- **JUCE FX stage header** (`ScreensFx.cpp`): power switch reading ON/OFF, SEND TO pills with an LED for all 8 sources (WT FM SMP PRO SID LUA XFX VOC; the JUCE header previously offered only 4), idle warning when an enabled stage has no source, dimmed sources when the stage is off.
- **Swift:** TabView items for Sound, Mod, FX, Seq, Mixer, Presets and Settings use the new icons (`NavIcon.<id>.image()`); iOS always draws tab titles, so the Swift tabs keep their text labels. `FXRackView.swift` StageCard got the same stage header: pills (`SendChipStyle`) for all 8 sources in a wrapping grid, ON/OFF text, idle warning.
- **Not compiled:** JUCE and Swift were written on Linux. `NavIcons.h` was compile-checked against a stub of the juce::Graphics/Path API; everything else has had no compiler pass.


## Round 22 — scope power, sub shapes, wavetables, pro plots, sync/ring icons, noise colours (web to native)
- **Scope power:** off by default and a real stop. JUCE `ScopeView` has an ON/OFF button (global `scopePower`), stops its timer and sets `scopeWantMask` so `PluginProcessor` no longer copies audio to the scope ring; Swift `ScopeView` has the same button (`vv.scope.power`) and `VectaAudioUnit` gates its tap copy (`scopeOn`).
- **Sub oscillator shape:** `subWave` (sine / triangle / square) in `model.hpp`, `subShapedTick` in `synth_common.hpp` (triangle, polyBLEP square) used at every WT sub site; JUCE "Sub shape" choice and Swift picker.
- **Wavetables:** droneSoftTide, droneSoftBloom, epTine, epReed, bassWarmPunch, bassHeavyThump, bassFunkResonant, bassFunkSlap, leadHoover added to `tools/export_wavetables.js` and re-exported (87 tables).
- **Pro plots:** `Core/include/vv/viz.hpp` (filter magnitude model, ADSR plot maths), `Juce/Source/ProPlot.h` (FilterVizView, EnvVizView) used by `addFilter` / `addEnv`, and `Apple/Shared/ProPlots.swift` (same maths and look) used by `FilterEditor` / `EnvEditor`.
- **Sync / ring icons:** `sync` and `ringmod` added to `tools/export_nav_icons.py` (NavIcons.h / .swift regenerated, 17 icons); `WtLinkBadge` shows them at OSC1 when `syncEnabled` / `ringModEnabled` (sync wins).
- **Noise colours:** white / pink / brown / blue / violet in the core (`fillColouredNoise`, slopes 0 / -3 / -6 / +3 / +6 dB/oct); JUCE and Swift noise-type choices extended and added to sample slots and FM operators.
- **Bug fixed (web and core):** hard sync ignored the oscillators' Semitone and Fine tune, so the slave ran at the wrong ratio.
- **Tests:** core `r22` (spectral slopes, sub shapes, sync autocorrelation, plot maths) and the full native ctest 50/50 pass. JUCE and Swift were written on Linux and are not compiled.


## Round 23 — preset tree, MPE touch start, position auto-scan sources (web to native)
- **Top-row preset menu:** the preset name now opens a tree (Factory / User Presets / Banks -> bank -> category -> preset). JUCE: `showPresetTree` in `ScreensFx.cpp` (PopupMenu submenus over `scanPresets`), opened from a click on the name label in `PluginEditor.cpp`. Swift: `PresetTreeMenu` in `PluginRootView.swift` (nested `Menu`s over `PresetLibrary.scan`).
- **MPE start point:** glide offsets are measured from the continuous pitch under the finger at touch-down (`Finger.p0` in `PluginRootView.swift`), not from the key centre. The JUCE keyboard has no MPE drag-glide, so there is nothing to port there.
- **Position auto-scan:** `positionLfoSource` accepts `ampEnv`, `filterEnv`, `pitchEnv` or any matrix source id (`Synth::wtAutoScan`, `modulation.hpp`; `wtNoteStart_` records the WT note-on time). Core `r22` test covers velocity, amp-envelope and pitch-envelope scans. JUCE choice and Swift picker "Position auto-scan" added. Limits: the native source list is the 34 sources `parseModSource` knows (no flute or LUA sources yet), and like the web the matrix evaluates on a 100 ms tick.
- Not compiled: JUCE and Swift were written on Linux.


## Round 25 — extra Matrix and XY destinations (web to native)
- **Web:** `GEN:SID.eqBand1..4Freq/Gain` added (WT/FM/SMP/PRO/LUA already had them); new `GEN:VOC.voc_*` vocoder destinations (mix, formantSt, q, releaseMs, hpf, micGainDb, noiseDb, sibSens, pitchSpeed, chorusRate); XY pads gained `g:` (layer EQ, SID, LUA, vocoder), `fx:` (EQ, Phaser, Flanger, Resonator, Convolution Reverb, Echo, Stereo Spread, Interstellar), `sidcutoff/sidreso/luacutoff/luareso` destinations.
- **Core:** `kGenDefs` gained SID and LUA EQ bands; `kVocDefs` + `D_VOC0..D_VOC_END`; `Synth::vocApply` (live HvParams update, restored when routes are removed); `GEN:LUA.*` now parses. Test `vv_r25_tests`.
- **JUCE:** Matrix list (ScreensMod.cpp) and XY destination list (XYPads.cpp, generated from `kFxpDefs`/`kVocDefs`) updated. **Not compiled here.**
- **Swift:** Matrix list (ModView.swift) updated. The Swift XY pads remain modulation sources only (no per-axis destination menu) - a known gap.
- Not ported to native XY: SID tracker `sid_*` and LUA script parameter destinations (matrix GEN list only has what the core implements).

- **XY pad layer (round 25b):** the pad Layer menu now offers all six oscillator layers (WT, FM, SMP, PRO, SID, LUA) in web, JUCE and Swift. Swift XY pads gained a Layer menu and X / Y destination menus (`XYDest`, a subset of the web list: layer filter / pitch / level / pan / EQ, oscillator position, macros, mixer, vocoder, FX parameters); hold, automation recording and the rest of the web destination list remain web / JUCE only. Not compiled.


## Round 26 - touch transient editor (Swift)
`Apple/Shared/SampleWaveformEditor.swift` reworked after the Vektur Drum sample editor feel (the Drum source was not available in the build environment, so this follows the behaviour asked for rather than copying its code):
- Markers have numbered flags with a 52 pt touch zone; the marker keeps the finger-to-line offset (no jump); a tap never nudges (6 pt dead zone).
- Markers cannot cross their neighbours; the drag is a working copy and is written once on release (no preset/engine writes mid-drag). Snap-to-zero-crossing now applies to markers too.
- Delete: tap selects, red ✕ badge deletes; double-tap a flag deletes; DELETE SELECTED, CLEAR ALL, and the slice-list trash button. Every marker edit (move, add, delete, clear, detect) is undoable (UNDO, 30 levels, cleared on CROP).
- Double-tap empty waveform adds a marker; when zoomed, dragging empty waveform pans; haptics on select / add / delete; optional audition on tap.
- Performance: 64-sample min/max pyramid for peak columns (zoom/pan cost no longer scales with sample length); the canvas redraws at full rate only while a voice plays this slot or a finger is down, otherwise on state change.
- JUCE `SampleEditor.cpp` already had drag, right-click delete and alt-click add; its drag still writes the store on every mouse move (not changed here).
- Added `.github/workflows/ios-build.yml` and `docs/XCODE_GITHUB.md` for the first Xcode build. Never compiled.
