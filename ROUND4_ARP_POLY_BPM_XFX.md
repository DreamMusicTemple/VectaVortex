# Round 4 - ARP popup, Poly, BPM, XFX layer (web + native)

## What changed
| Item | Web (`new-web.html`) | Native (Swift / C++) |
|---|---|---|
| ARP popup | `#arpPopup`, opens on OFF->ON, CLOSE independent of arp state, `ARP SETTINGS` reopen button while ON, Esc closes, not saved | `ArpSection` + `ArpSettingsPopup` (sheet) in `SequencerView.swift`; `showPopup` is `@State` only |
| Poly | moved into Settings > PERFORMANCE, first row, single control | `SettingsView` "Performance" section, first row; removed from `KeyboardOptions`. Same `maxPolyphony` + `legatoMode` bindings |
| BPM | input after AUTOMATION in the layer control row; read-only while a host tempo is present | `PlayFooterView.bpmField` after the layer controls (native has no AUTOMATION button; it is the end of the bottom control row). Hosted: shows `VectaAudioUnit.hostTempo` read-only |
| XFX layer | 7th zone layer, colour `#e9694f`, VIEW/HOLD/SOLO XFX, Zones section, FX sends after LUA | `LayerID` colour 6, footer VIEW/HOLD/SOLO XFX, FX rack send toggles `... LUA, XFX`; core: `kXfxSend`, `xfxBus_`, `XfxTable::zone/hold`, solo/mute bit 6 |
| Persistence | `extra.xfx.hold`, `sends.XFX`; popup visibility never saved | same JSON; new `xfxAction("hold")` |

## C++ / Obj-C++
C++ is used only for the DSP/engine core (`Core/`), reached through the C ABI `Bridge/vv_engine.*`. No new Obj-C++ was needed this round. **JUCE is not required**: `Juce/` is an optional alternative UI and is not part of the Xcode targets.

## Tests
- Core ctest 41/41 (new: `xfx_layer`: send on zone key only, bypass, solo/mute bit 6, hold, state round trip).
- Web: `r4a_live`, `r4b_live`, `r4b_audio`, state/bank/fx/regress/clips/roll/rr all pass.

## Known limitations
- XFX has no layer EQ / mixer strip: its audio is heard through effect sends.
- Solo XFX: non-zone notes follow layer solos; zone notes on a non-soloed layer stay muted by that layer's bus.
- Solo/Mute XFX are view state (not saved), like the other layers; Hold XFX is saved.
- Swift files were written on Linux and are **not compiled**.

## Outstanding on a Mac (do not treat as build-ready until done)
1. Open the project in Xcode, fix first-compile Swift errors (this round touched: `PlayFooterView`, `SettingsView`, `SequencerView`, `PluginRootView`, `LayerIdentity`, `FXRackView`, `VectaAudioUnit`).
2. Signing / team / app-group / AUv3 extension entitlements.
3. Verify on device: ARP popup (OFF->ON opens once; unrelated edits do not reopen), Poly in Settings, BPM read-only inside a host (Logic/GarageBand/AUM), XFX footer buttons, FX send toggles.
4. Confirm `AUHostMusicalContextBlock` is provided in standalone (then `hostTempo` would be non-nil and lock the BPM field); if so, gate `hostTempo` on the extension context.
5. Run `auval -v aumu Vect Vcta` and a DAW project save/restore with an old preset.

## Round 5 - layout + MPE drag-glide
- OS (oversampling) now sits immediately right of BPM (web `#oscLayerBtns`; native `PlayFooterView.osField`).
- PANIC! is pinned to the far right of the bottom row with the RECORDER fixed immediately to its left (web: `#recPanicGroup`, nowrap; native: `KeyboardOptions`, both outside the scrolling area).
- MPE on: press a key on the on-screen piano and drag to any other key - the note glides continuously (no retrigger), through every pitch in between. Offset is `mpeNoteExpr[note].glide` (web) / `MpeExpr::glide` + `vv_engine_mpe_glide` (native C ABI), added on top of the MPE/bend path, so the mod matrix and bend targets still apply.
- The stretched line changes hue within each octave (C red ... B violet), brightness steps per octave, a tick + label marks every C crossed, and a semitone read-out follows the finger. Core test: `xfx_layer` (+-12 st doubles / halves the frequency).
- Native Swift (`KeyboardView`) is uncompiled; native line is clipped to the keyboard view (web draws across the whole window).

## Round 5b - glide smoothing + black screen
- Black screen: the glide overlay canvas inherited the app-wide `canvas{background:#000}` rule; it is now explicitly transparent.
- Glide stepping: the web glide used to ride the slower voice tick. It is now pushed every animation frame, eased (0.18/frame) with a 12 ms parameter time constant, and applied as an exact pitch ratio per oscillator type (cents / Hz / playback rate). Measured slew <= 18 cents per 60 fps frame over a 11-semitone drag. Native: 120 Hz easing timer in `KeyboardView`; the engine adds its own 20 ms smoothing; the glide now moves every oscillator (bend targets gate only the wheel).
- Also fixed: `applyPitchBendNow` referenced an undefined `activeVoices` (pitch wheel input threw); it now iterates `forEachActiveVoice`.

## Round 6 - VIEW LAYER dropdown, XFX in every sequencer, XFX preset loader, 6 IDM XFX presets

**Web (`index.html` / `Web/full-app.html`)**
- The seven VIEW buttons are replaced by ONE `VIEW LAYER ▾` button. It opens a menu (WAVETABLE, FM, SAMPLER, PROCEDURAL, SID, LUA, XFX); choosing a name shows that layer's window immediately (XFX opens the XFX Mode editor; any other choice closes it first). Esc / outside click closes the menu.
- XFX MODE is a 7th sequencer target. Sequencer window: new `XFX MODE` tab (own 8 patterns / piano roll / chain, stored as `extra.xfxSeq`). Clips: layer `XFX`. Chord Sequencer: `Play on` = XFX MODE. Drum sequencer: `Lane output` = XFX MODE keys (lane n plays XFX key `first key + n`). Every sequenced note is expanded to the layers that note's XFX key programs (`xfxExpand`); a key with no programming plays the normal layers.
- XFX Mode editor header: `LOAD PRESET (Bank 1) ▾` lists every preset of the XFX folder; picking one loads it with its XFX keys in Bank 1 and the overlay stays open.
- 6 new factory XFX presets (XFX folder 4 -> 10; with the 2 generator recipes 6 -> 12): **Breakcore IDM Drums XFX** (172 BPM), **Polymeter Glitch Drums XFX** (150, lanes of 15/13/16/14/11/7/9/10/12/5/32/6/8 steps), **Stutter Machine XFX** (164, 32-step lanes) - each with the drum sequencer ON and routed to XFX; **Squarepusher / Aphex Acid / Autechre Arp Lead XFX** (170 / 160 / 148) - per-key wavetable leads plus 8 XFX clips in Clip bank 1 (slots 1-8, 1/4 launch quantise).
- Recipe format additions (`genBuild`): `recipe.drumSeq {xfxBase, lanes[{len, steps:"x..x"}]}` and `recipe.clips {count, bars[], grid[], density[], leap, ratchet, pitches[], names[], quant}`.

**Native (Swift written without a Mac - uncompiled; C++ core tested)**
- `PlayFooterView`: `viewLayerMenu` (SwiftUI Menu) replaces the seven VIEW buttons.
- Core: `clips.hpp` kSeqLayers = 7 (layer "XFX" = index 6), `Performance::seqLayer/seqMask` expand XFX roll / clip / chord / drum notes through `layersForNote`; `Preset::xfxSeq` (extra.xfxSeq), `drumXfx/drumBase` (drumSeq.target "xfx"), drumSeq now also read from `extra.drumSeq` (web presets). Test: `Core/tests/xfx_seq_tests.cpp` (roll, drum, clip layer; a key with every layer switched off stays silent).
- Swift: Sequencer `Pattern` tab has an XFX MODE entry (patterns at `extra.xfxSeq`, created by `PresetStore.ensureXfxSeq()`), Chord `Play on` has XFX, clip layer picker has XFX, `XfxView` has `LOAD XFX PRESET` (category XFX, Bank 1), "XFX" added to `presetCategories`. The 6 new presets are exported to `Assets/Presets` (`tools/export_cymatics_bank.js`).
- Not ported: the native app has no drum-sequencer editor UI, so the XFX output option is carried by presets only (engine support is in).

**Mac tasks**: compile the Swift changes; check the VIEW LAYER menu; open Sequencer > Pattern > XFX MODE; load "Squarepusher Lead XFX" from XFX MODE and launch clips from Bank 1.

## Round 7

- **Polyphonic MPE (max 10).** Web: every pointer on the touch keyboard has its own glide (`mpeGlides`), the 11th simultaneous touch is ignored, and MIDI MPE keeps per-channel notes and steals the oldest beyond 10. Native: `FullEngine::mpeVoiceOn/Off` cap per-channel notes at `kMpeMaxPoly = 10` (oldest stolen); `PluginRootView` keyboard uses a `MultiTouchSurface` (UIKit touches) so each finger plays and glides its own note. Test: `vv_mpe_poly_tests`.
- **LUA visuals**: 8 new presets (4 Tunnel, 4 Psychedelic) -> `Core/viz/factory/19-26*.lua`, names in `VizHost.swift`.
- **LUA sound presets** (15): 4 Ambient, 4 Jump Up D&B (new 'Drum and Bass' category), 4 Vangelis CS-80, 3 Hoover. Scripts appended to `lua_factory.hpp` (31 total); the web builds named DMT CYMATICS presets that load them into a LUA slot.
- **XFX presets** (12): 8 SID chiptune (Hubbard, Commando, Galway arp clips + SID wtArp, C64 drum kit, ring/sync, PWM pads, polymetric noise percussion, Turrican clips) and 4 Amen jungle. The jungle presets map 16 slices (kick/snare/ghost/hat/crash, from the transient detector) of `Amen_Break.wav` to keys, ratchet the snares (`env.rep`), run the drum sequencer on XFX output and apply the **Akai time stretch** (`quality: "akai"`): the sample is chopped into 7-20 ms grains that are repeated or skipped with a ~1.5 ms crossfade and no phase alignment (metallic, phasey, stuttering). Also selectable as "Akai" in the sampler stretch dialog. Stretched slots are rebuilt after preset load (`restoreStretchedSlots`, transients kept). Presets are exported to `Assets/Presets/` (55 DMT CYMATICS presets).
- Swift remains uncompiled (written on Linux).

## Round 8

- **Backgrounds:** "Metaphysical library" and "Samurai vs ninja" added (web `--bgimg-meta/ninja`; native `Assets/Backgrounds/meta.jpg, ninja.jpg`).
- **Dial styles:** Aqua (liquid ring with surface-tension bulge, droplets, drop-shaped pointer), Scientific hardware (engraved 0-100 scale with vernier, knurled skirt, jewelled cap, LED trim arc) and Holo ring (after the supplied reference: glass plate, segmented rings, growing LED dots, white index bar). Top-down light plus a semi-transparent cast shadow in the dial's outline. Web: Settings > Potentiometer style (`POT_DRAW`); native: `Apple/Shared/DialStyles.swift`, chosen in Settings > Appearance.
- **Panel opacity** 100% / 50% / 10% (web: panels; native has no panel boxes, so it scales the dark tint under the controls and the footer).
- Swift remains uncompiled.

## Round 11 - Draw Envelope, Pitch Envelope, UI smoothness

**Web (`index.html` / `full-app.html`)**
- `VVCurve` (shared editor) + `VVFrame` (single rAF scheduler). Drag nodes, bend segments (grabbed point tracks the pointer), add/delete (double-click / right-click / Delete), FINE 0.15x, SNAP, UNDO, zoom, STRAIGHT / BEZIER (`lin` flag per segment). One evaluator, `evalCurveY`, feeds the picture and the audio (LFO custom shape, wavetable-from-curve, filter-envelope shape, pitch envelopes).
- Filter-envelope drawn shape is now audible (`shapeOn`, `shapeTime`); old presets keep ADSR behaviour.
- PITCH ENVELOPE per WT OSC1/2, FM A/B, Procedural A/B (EQ / AMP ENVELOPE section): on, amount (+-48 st), time, bipolar, legato retrigger, drawn curve in semitones. Rendered at note-on to a value curve on the oscillator's own frequency / cents param (independent of bend, drift, MPE glide, retune offsets).
- Displays: `vizLoop` is rAF-synchronous (was a 16 ms setTimeout), LFO playhead interpolated between 10 ms ticks, filter graph smoothed (18 ms one-pole, snaps while touched) and pointer-event driven with coalesced events.
- Dials: log taper for wide positive ranges, no stepping (`step=any`), incremental drag, Shift/Ctrl = 0.1x, sideways drag = progressive fine.

**Native**
- Core: `PegCfg` in `OscCfg` (`pitchEnv`), `PegRt` per voice (6 slots), applied multiplicatively to WT (incl. sync pair), FM engine base + sub, Procedural frequency; `evalCurveY` honours `lin`. Legato + `retrig:false` joins at the curve end. Lock-free `lfoPhaseSnap/lfoRateHz` atomics + C bridge `vv_engine_lfo_phase/_rate_hz`. Test: `pitch_env_tests`.
- Swift (uncompiled, written on Linux): `CurveCanvas` (EQ-style gestures, STRAIGHT/BEZIER/SNAP/FINE/UNDO/zoom), `PitchEnvEditor` + `LfoPreview` (`PitchEnvelopeView.swift`), `SliderRow` taper + fine drag, display-rate `TimelineView(.animation)` via `BatterySaving.frameInterval()`.
- Not done: pitch-envelope release stage / loop; native filter-envelope drawn shape audio; FM/PRO C++ pitch-env test (WT is covered).

### Round 12 - footer layout
Web + native: all layer HOLD / SOLO buttons on ONE row (labels HOLD PRO / SOLO PRO); RECORDER elements and PANIC! moved to the far right of the options row, right of the CHORD pill and readout (no extra bottom row). Web: Samples block 15% wider (362 px), 72 px tall, SAMPLES button 4 px lower. Native has no Samples slider block, so nothing to port there. Swift uncompiled.

## Round 13 – Arp layers, octaves, divisions
- Arp layer list now includes SID and LUA (ALL covers them); octave range 1–4.
- Divisions: 1/64 … 1/2, triplets, 1 bar, 2 bars; arp rate clamp widened to 0.02–80 Hz.

## Round 14 – Generative arp, Stage 1 (Standard mode)
- ARP settings popup: STANDARD | ADVANCED selector (`arp.mode`, saved with presets). Both modes share one clock / scheduler / note manager.
- Playback modes: Up, Down, Up/Down, Down/Up, Random, Random Walk (neighbour steps), Pendulum (ends repeat), Spiral (outside-in), Chord Strum (staggered, direction alternates), Octave Orbit (cycles held notes, octave sweeps up/down across the range) + legacy Chord (all) and As played.
- New controls: Gate (5–95 %), Swing (50–75 %, falls back to the global swing when at 50), Velocity, Note repeat (1–4 per step), Transpose (±24 st), Latch (global latch, in addition to per-layer Hold).
- Native: `ArpCfg` gains mode/gate/swing/velocity/repeat/transpose; `Performance::arpNextEvents` mirrors the web generator (fixed buffers, no allocation on the audio thread); Swift `ArpSettingsPopup` updated; tests in `performance_tests.cpp` (`testArpModes`).

## Round 15 – Generative arp, Stage 2 (Advanced algorithms)
- ADVANCED mode: 35 seeded algorithms, each a pure function of (seed, parameters, held notes, step) - reproducible, no state, no allocation. Web: `ARP_ALGOS` / `arpAdvGen()`; native: `Core/include/vv/arp_adv.hpp`. Both use the same integer hash and operation order; `arp_adv_tests` compares 70 cases / 4900 steps against a golden dump from the web (0 differences) and fuzzes every algorithm for range safety.
- Groups: Generative note ordering (Fractal, Fibonacci, Brownian Motion, Kaleidoscope, Cascade, Call & Response, Interval Jump, Pedal Tone, Scale Walk, Chord Inversion); Mathematical (Euclidean, Markov Chain, Cellular Automata, Chaos, Golden Ratio, Probability, Neural, Random Mutation, Morph, Time Warp); Rhythmic (Ratchet, Stutter, Offbeat, Syncopated, Polyrhythm, Triplet Shuffle, Groove, Burst, Reverse Gate, Ghost Notes); Harmonic (Broken Chord, Harmonic, Voice Leading, Octave Jump, Interval Sequence).
- Parameters (`arp.adv.*`): probability, density, complexity, mutation, variation, randomness, humanise, octSpread, ratchet, velVar, accent, gateVar, groove, patLen, cycleVar, rotation, skipProb, rhythmDisp, scale, seed. The popup shows only the ones the selected algorithm uses plus a core set (probability, pattern length, rotation, humanisation, velocity var., gate variation, note skip). Octave range, division, gate, swing, velocity, repeat, transpose, latch and layers are shared with Standard.
- NEW SEED picks a new random seed (= a new pattern); the same seed always reproduces the same pattern. Cycle Variation re-seeds some cycles; Random Mutation and Morph evolve across cycles.
- Swift: `ArpAdvInfo.swift` (table generated from the web list) and `ArpAdvancedSection` in `SequencerView.swift`.
- Still to come: Generate / Randomise / Mutate / A-B / morph / save-load pattern management (Stage 3), modulation destinations (Stage 4), manual + validation (Stage 5).

## Round 16 – Generative arp, Stage 3 (pattern management)
- A pattern is the seeded parameter set (`arp.adv`), so everything below is exactly reproducible.
- GENERATE: new seed. RANDOMISE: new seed + randomised settings (algorithm kept). MUTATE: `mutSalt`+1 / `mutAmt` = Mutation (min 15 %) - re-rolls only some steps, the rest of the pattern stays. REGEN VARIATION: `varSeed`+1 - re-rolls only the variation layer (probability, skips, humanise, velocity/gate variation, ratchet, octave spread), notes unchanged. RESET: defaults for the current algorithm. Pattern length / complexity are the existing Pattern Length / Complexity parameters.
- A/B: two pattern slots (`arp.ab` = {cur, a, b}); the current slot is `arp.adv`. Switching stores the current pattern and loads the other (an empty slot starts as a copy). COPY TO OTHER copies the current pattern across.
- Morph A↔B (`arp.morph` = {on, amt}): each step is drawn from A or B (B with probability = amount, deterministic from A's seed and the step), so 0 % = pure A, 100 % = pure B.
- Saved patterns (`arp.lib` = [{name, adv}]): SAVE / LOAD / DELETE; stored inside the preset.
- Automatic evolution over cycles uses the existing Cycle Variation, Random Mutation and Morph algorithms.
- Native: `ArpAdvCfg` gains mutSalt/mutAmt/varSeed; `ArpCfg` holds the other A/B slot + morph; `arpadv::genMorph`; Swift `ArpPatternSection`. Tests: golden parity now covers mutate/variation seeds and 6 morph cases; property tests for mutate, variation, morph endpoints.
- Fix: the web `arpAdvParams()` returned a fresh copy each call so slider edits could land on a stale object; it now edits the live pattern in place.

## Round 17 – Arp-aware RANDOMISE
- While the arpeggiator is on and sounding (held or latched notes), the piano-roll RANDOMISE and the step-sequencer RANDOMISE draw their pitches from the arp's harmony instead of the plain keyboard scale: chord tones (including the arp's Transpose) plus a scale that contains them. Order of preference for the scale: Scale Walk's scale (Advanced mode) > the keyboard scale if it contains the arp's notes > the best-fitting of major / minor / dorian / mixolydian / harmonic minor / phrygian / lydian (root preferring the lowest held note) > chord tones + 4th/5th when no scale contains the notes (clusters).
- Notes are picked melodically, in time order: chord tones are favoured on strong beats, small steps are favoured, repeated notes and semitone clashes with the chord on strong beats are discouraged. A short "fitted to arp: A minor" hint appears next to the button.
- Setting: Settings > Keyboard > "RANDOMISE fits the arp" (web `vv.fitArp` in localStorage, native `vv.fitArp`), on by default. With the arp off or silent, RANDOMISE behaves as before.
- Native: `Core/include/vv/arp_harmony.hpp` (pure functions), `Performance::arpSnapshot` (published once per audio quantum with relaxed atomics, so the UI thread never locks or touches arp state), C ABI `vv_engine_arp_fit`. Tests: `arp_harmony_tests` (golden parity with the web, in-key / range / strong-beat properties, snapshot through Performance).

## Round 18 – GENERATE / MODIFY use the new arp
- The Arp recipes (FM Pluck Arp, Acid Arp, Glass Arp) now go through `arp_set()` in `gen/make_recipes.py`: roughly 70 % of generated presets use the ADVANCED arp (an algorithm that suits the recipe: e.g. Acid = Euclidean / Syncopated / Ratchet / Markov / Offbeat / Stutter / Groove / Chaos; Glass = Fractal / Kaleidoscope / Fibonacci / Scale Walk / Cascade / Brownian / Golden Ratio / Morph; FM Pluck = Pedal Tone / Broken Chord / Harmonic / Voice Leading / Call & Response / Interval Sequence / Fractal) with a fresh random seed and musical ranges for the generative parameters; the rest use STANDARD with the new playback modes (Pendulum, Spiral, Random Walk, Octave Orbit, Down/Up, Strum...). All presets also get Gate, Swing, Velocity, Note Repeat, the new divisions (1/64 ... triplets) and octave range up to 4.
- MODIFY now also evolves an advanced arp pattern: Mutate (re-rolls some steps) + the 0..1 generative settings nudged by up to ±10 % (web `modifyCurrentPreset`, native `presetgen::modify`).
- INIT preset / FX defaults re-dumped from the web (they had drifted: `sends.LUA`, reverb / granular defaults, arp fields); native `presetgen` sends now include LUA. `export_gen_parity.js` re-run: 118 cases, 0 differ (the web and C++ generators produce identical arp settings per seed).
- Drum-sequencer lanes in generated XFX / per-key presets: the native generator now writes `extra.drumSeq` exactly like the web (recipe lanes -> sequencer on, routed to the XFX keys; other XFX recipes -> an empty, switched-off sequencer). The parity test no longer skips anything: 118 / 118 identical.
