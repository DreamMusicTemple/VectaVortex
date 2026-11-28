# Vektur Vortex — native vs web UI discrepancy report and restoration plan

Reference: the web app (`index.html`, the 14-icon nav with separate pages) and your screenshots. Native: `Apple/Shared/*`.

## 1. Where native differed (before this pass)

| # | Area | Web | Native (before) | Priority |
|---|---|---|---|---|
| 1 | Navigation | 14 icon pages: Oscillators, XY, Matrix, FX, Sequencer, LFO, Cross Mod, Vocoder, Scope, Mixer, C64/SID, Spectrogram, Presets, Settings | bottom system tab bar, then 9 icons; LFO/Matrix/XMod/Vocoder/XY merged behind an added segmented picker; extra "Play" and "Params" pages that the web does not have | P1 |
| 2 | Oscillator page | OSC1 and OSC2 side by side in two columns | one column stacked | P1 |
| 3 | Persistent bottom area | macro dial tiles + master volume/tone dials, sample-slot bar, XFX MODE, ARP/EQ-AMP ENVELOPE/ZONES/Scale/Root/Octaves/PLAY/HIDE KEYBOARD/OCT row, pitch + mod strips, keyboard | macros/keyboard only on some screens; sliders | P1 |
| 4 | Rotary dials | large round dials, mono label, value box | small dials; sliders for macros / master | P2 |
| 5 | Buttons / toggles / dropdowns | dark rectangular buttons, small switches, compact selects | iOS bordered pills, large iOS switches/steppers/menus | P2/P3 |
| 6 | Panels | dark glass, accent title bar | plain Form | P3 |
| 7 | Chord Maker, 16-lane drum sequencer, Wire overview, Scope modes | present | present but not laid out like web | P1/P2 |

## 2. Done so far (builds green, in TestFlight except step 3)
1. Top icon nav, web-style panels, dial grid with value boxes (step 1–2).
2. Persistent macro / master dial tiles and keyboard under screens; no sliders (step 2).
3. **This pass (P1 navigation):** 14-icon nav in web order; LFO, Matrix, Cross Mod, Vocoder and XY are separate pages; removed the added segmented picker; removed the native-only "Params" page from the nav. Scope and Spectrogram open the scope screen; C64/SID opens the SID layer.

## 3. Remaining plan (in order)
1. OSC1 | OSC2 two-column layout (Sound page).
2. Bottom area: sample-slot bar, XFX MODE tile, ARP / EQ-AMP ENVELOPE / ZONES / scale / root / octaves / PLAY / OCT row, pitch+mod strips.
3. Sequencer page: chord maker and 16-lane drum sequencer layout.
4. FX, Mixer, Presets, Settings page layouts against your screenshots.
5. Control styling: compact switches, selects, steppers (web sizes).
6. Verification after each step: simulator build, unsigned archive, screenshots, TestFlight.

## 4. Needs your approval (differences from the web I did not change by myself)
- Web has no "Play" page; native used it for the scope + keyboard. I mapped Scope and Spectrogram icons to it for now.
- Web has no parameter browser page; the native-only "Params" page is removed from the nav (code kept).
- C64 page: web shows it as its own page; native shows it as the SID layer on the Sound page.

## 5. Preserved
No DSP, C++ engine, MIDI, parameter tree or AUv3 code was changed.
