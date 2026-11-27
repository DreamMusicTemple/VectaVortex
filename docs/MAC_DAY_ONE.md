# Mac day one

1. Install **Xcode 26 or newer** (App Store uploads require the iOS 26 SDK) and open it once to accept the licence and install components.
2. Terminal: `brew install xcodegen cmake`
3. Sanity-check the DSP on the Mac: `cd Core && ./run_tests.sh` (all tests should pass; same results as Linux).
4. `cd Apple && xcodegen generate` -> creates `VectaVortex.xcodeproj`. Open it.
5. In Xcode: select each target (VectaVortex, VectaVortexAUv3) > Signing & Capabilities > pick your Team. Confirm bundle ids (`com.dreammusictemple.vectavortex`, `.auv3`) or change them in `project.yml` and regenerate.
6. Build the **simulator** first. (`project.yml` bundles `Assets/Wavetables`, `Assets/Samples`, `Assets/Presets`, `builtin_samples.json` and `default_preset.json` into BOTH the AUv3 extension and the standalone app (the app registers the Audio Unit in-process, so it needs its own copy; if the duplicate ~16 MB bothers you, move the assets into a shared framework); `InstrumentLoader.swift` loads them on a background queue. 13 MB of samples are loaded eagerly right now - check the extension memory in Instruments and make it lazy if it is tight.) Expect a handful of Swift compile errors (nothing was compiled before). Known candidates:
   * `AURenderEvent` case `.midi` may need to be spelled `.MIDI` depending on SDK.
   * Bridging-header path and the static library link for `VVEngine`.
   * SwiftUI `ViewBuilder` blocks with more than 10 children (the screens were split into `Group`s by hand, but one may be missed).
   * The `AUParameterTree` observer/`ObserverGate` code in `VectaAudioUnit.swift` and the `PresetStore` debounce timers.
   Send me the error text and I will fix each.
7. Run on a **real iPhone/iPad** (audio latency and AU hosting cannot be judged in the simulator).
8. Test the AUv3 in a host: GarageBand, AUM, or Logic for iPad. Check: appears as an instrument, plays notes, parameters automate, state saves/reloads with the song.
9. Then follow `APPSTORE_CHECKLIST.md`.
10. Automation check (new): in a host, automate `Wavetable filter cutoff` and `Reverb algorithm`; the Sound / FX screens should follow within ~0.2 s. Load a factory preset: the host knobs should jump to its values. `Reverb mix` and the others only edit an existing stage; set the `Reverb algorithm` first.

## Update
Prefer the JUCE route first: see JUCE.md.

## Handover addendum (latest revision)
- Build the JUCE target first (`docs/JUCE.md`). Check on device: piano roll drag, Send to Vektur Sonic in AUM (Vektur Sonic as insert after Vektur Vortex; confirm L/R input), mono warning, the FX games on touch (stick, buttons).
- Game rules (Vektur Void) are plain C++ in `Core/include/vv/vektur_void.hpp`; `Apple/Shared/VektorVoidView.swift` and the Settings EASTER EGG button have never been compiled.
- Nothing in Swift/Xcode/AUv3 has ever been compiled; expect a first-build fix-up round.
- XFX MODE + amp-envelope vertical faders / Repeat (2026-11-10): engine and C-ABI are tested (`ctest -R xfx_env`). New/changed UI that has never been compiled: JUCE `Source/XfxEditor.h`, `VFader` in `Widgets.{h,cpp}`, the XFX button in `PluginEditor.cpp`; Swift `Apple/Shared/XfxView.swift`, `VFaderControl` + `EnvEditor` in `Controls.swift`, `PresetStore.xfxAction`, the XFX toolbar button in `SoundView.swift`. After the first build, check by ear: Repeat = 3 on an amp envelope gives 4 attacks, Continuous keeps pulsing until the key is released, and an XFX override on one key changes only that key. Details: `NATIVE_PARITY_AUDIT.md`.
- GENERATE rebuild (2026-11-10): engine/C-ABI tested (`ctest -R presetgen`). New never-compiled code: `Store::generatePreset` (JUCE, reads scaleIdx/scaleRoot/scaleLock) and `PresetStore.generate` (Swift, reads the vv.kb.* defaults) call `vv_engine_preset_generate`; GENERATE menus gained Drum and XFX.
