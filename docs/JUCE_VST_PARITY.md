# JUCE parity for the desktop VST3 / AU build (status 2026-11-20)

The JUCE target (`Juce/CMakeLists.txt`) already builds Standalone, VST3, AUv3 and AU around the shared C++ engine, so a desktop-DAW release needs no new project: it needs the JUCE UI to match the web and Swift apps. This list comes from a live-DOM inventory of the web app (`tools/audit_web_ui.js`) matched against the JUCE and Swift sources, then checked by hand. Matching by label is fuzzy, so treat it as a work list, not a proof.

## Closed in this pass
- **ADVANCED arpeggiator** (35 generative algorithms, seed + NEW SEED, scale, per-algorithm parameters, GENERATE / RANDOMISE / MUTATE / REGEN VARIATION / RESET): `ArpAdvPanel.h` + generated `ArpAdvInfo.h`, mode switch, full Order list (12), Gate and Swing knobs. Engine already existed (`arp_adv.hpp`).
- Round 22-23 items already in JUCE: scope ON/OFF, sub shape, noise colours, sample noise type, pro filter/envelope plots (`ProPlot.h`), sync / ring-mod badge, preset tree menu, Position auto-scan sources.

## Staged port (started 2026-11-20)
- **Stage 1 (audit result):** the Vocoder, Settings and Presets screens already carry every control Swift has (the first audit matched labels loosely). Audio-input device picking, sample rate and latency are host-managed in a plugin. Nothing to add.
- **Stage 2 (done, uncompiled):** `IrPanel.h` (convolution reverb: factory IRs by category, import / replace / remove, trim silence, IR envelope; no audition button in a plugin) wired into the FX rack together with the missing `eq` and `ir` effect types; `ClipLauncher.h` (8 banks x 8 slots, launch / stop-all, record, ROLL to CLIP, CLIP to ROLL, copy / paste, undo, restore factory) as the new **Clips** tab of the Sequencer screen.
- **Stage 3 (done, uncompiled):** arpeggiator A/B slots + COPY > OTHER + Morph A<>B + pattern library (SAVE / LOAD / DELETE) in `ArpAdvPanel.h` (engine already read `arp.ab` / `arp.morph`); scope Spectrogram OPTIONS in `PluginEditor.cpp` (FFT size 1024-4096, window, log / linear band scale, palette, source L+R / L / R / Mid / Side, peak-hold) and the same in Swift `ScopeView.swift`. The "sequencer extras" in the earlier list are web step-grid labels (lane output, SEL ALL, COPY-DRAG, grid size ...): the piano roll replaced that grid in both native apps, so there is nothing to port; strum is in the Chord maker.
- **Stage 4 (done, uncompiled):** `LuaEditor.h` + a **LUA** tab on the Sound screen (four script slots: source editor, RUN / VALIDATE / LIBRARY, error line + column, live stats, script parameters built from the compiled script, per-slot output / velocity / seed / common performance knobs, layer envelope / filter / EQ). Still web-only: LUA VISUALS scope engine.
- **Stage 5 (audit + CI):** host automation was already in place (`HostParam`, Host Params screen, `refreshHostParams` on preset load). Added `.github/workflows/juce-vst3.yml` (core tests, then VST3 + Standalone on Linux / Windows / macOS, then pluginval strictness 5). Still needs a real machine: first build, signing / notarisation, Windows and macOS installer.

## Still missing in JUCE (rows already closed above are struck through) (Swift has them unless noted)
| Area | What is missing | Engine |
|---|---|---|
| LUA layer | ~~editor, library, run, errors~~ (stage 4); LUA VISUALS scope engine still missing | `lua_host.hpp`, `lua_factory.hpp` |
| ~~Convolution reverb~~ | done in stage 2 | in core |
| ~~Clip launcher~~ | done in stage 2 (clip library files and bank rename not yet) | in core |
| Scope analysis | ~~spectrogram options~~ (stage 3); scope voice-source lanes (VOICES button) still missing | tap only |
| ~~Arpeggiator~~ | done in stage 3 | `arp_adv.hpp` |
| Sequencer | lane output, XFX first key, strum ms, SC source picker, pattern/bank buttons (1-4 / 1-8), STOP ALL, grid size, SEL ALL, COPY-DRAG, CHORDS to NOTES, CLEAR PATTERN | partly |
| Presets | Generate-for-layer selector, export/import single preset file buttons, notes box parity | in core |
| Vocoder | audio input device, gate threshold, sibilance %, formant shift, envelope release, band Q, correction speed (some present under other labels) | in core |
| Settings | tuning reference A4, standalone BPM, note polyphony list (some present under other labels) | in core |

Host-managed in a plugin and intentionally not ported: sample rate / latency / apply engine, MIDI enable and hardware profile, DAW recorder start/stop (the DAW records).

## VST-specific work that is not parity
- Host automation: expose the main parameters as `AudioProcessorParameter`s (today state is a JSON blob in the `Store`).
- Resizable editor with saved size (partly there: `uiWidth` / `uiHeight`), HiDPI check, keyboard focus in DAWs.
- Plugin validation (pluginval), VST3 on Windows and macOS builds, code signing / notarisation.
- Sample/IR/wavetable assets inside the plugin bundle on every OS.

## Not compiled
The JUCE sources written in Rounds 22-23 and in this pass were written on Linux without a JUCE build (the GUI dev packages are missing in this environment). Build `vv_ui_snapshot` on a machine with JUCE before relying on them.
