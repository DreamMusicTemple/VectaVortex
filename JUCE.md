# JUCE UI + AUv3 / Standalone (recommended UI path)

The JUCE project in `Juce/` replaces the SwiftUI UI. It wraps the verified C++ engine (`Bridge/vv_engine.*`) and ports the web UI: Play (scope, macros, wheels, keyboard), Sound, Mod, Seq, FX, XY pads (with hold / snap-back / automation), Presets, Settings and a Host-parameter list (200 automatable params).

## Verified on Linux
- Engine: 131 reference scenarios + ctest 8/8.
- JUCE UI builds (JUCE 8.0.6) and runs headless; all 9 tabs plus the piano roll and both game themes render to PNG; store edits reach the engine; MIDI produces audio.
  `cmake -S Juce -B build -DJUCE_DIR=<juce>` then build target `vv_ui_snapshot`, run under `xvfb-run -a`.

## Added in this revision (all in `Juce/Source`)
- **Piano-roll step sequencer** (`ScreensSeq.cpp` StepGrid): full width, click toggles a step, drag sets pitch, shift/right-click selects. Same as web app.
- **Send to Vektur Sonic** (scope header button, `PluginEditor.cpp`): sets `Processor::sendToVectaSonic` (saved in state). XY preview button. Warnings: MONO (L==R), NO SIGNAL (~2 s silence), host-mono (`outputIsMono`).
  Workflow assumption: in AUM (or any host) load Vektur Sonic as an insert FX after Vektur Vortex; it then gets the plain stereo L/R pair. **Vektur Sonic's real input protocol was not available - confirm it takes plain L/R audio.**
- **Vektur Void** (hidden Easter egg: Settings > EASTER EGG): the web `shmupStep` game, logic in `Core/include/vv/vektur_void.hpp` (golden-tested frame for frame against the web), UI in `Juce/Source/VoidView.*`. The old FX-embedded space shooter is gone (the web decoupled it).
  The rules are headless-tested (`/Juce/Tests`, bot reaches victory, continue/game-over flow, FX mapping).
- 53 wavetables (`Assets/Wavetables/*.vvwt`, 14 new), Samples-mix slider +12.5 %, Octave buttons +50 %, mono font 11 pt, MixerFader.
- Not ported to JUCE: the glowing gradient-ring control skin and the Ocean background (web app only for now); touch behaviour of the game stick is untested.

## NOT verified (needs a Mac)
- iOS / AUv3 / Standalone / VST3 builds, signing, auval, device performance, touch behaviour.
- Recording, WSOLA UI, cymatics, reverb visualiser, XY pitch-osc selector are not ported.

## Mac steps
1. Xcode + CMake installed. `cmake -S Juce -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<TEAMID>` (JUCE is fetched; or pass `-DJUCE_DIR=`).
2. Open `build-ios/VectaVortexJuce.xcodeproj`; set bundle IDs/signing for the Standalone app and AUv3 extension; build to a device.
3. macOS: same without `-DCMAKE_SYSTEM_NAME=iOS` builds AU, VST3, AUv3, Standalone.
4. Assets are copied into `Resources/Assets`; override with `$VV_ASSETS`.

## Licence
JUCE is AGPLv3 or commercial. App Store distribution of closed source needs a JUCE commercial licence (or AGPL compliance). Confirm before release.

Latest tweaks: Samples mix slider 315 px (web `.mix-slider-block-lg`, JUCE MixerFader), 2 px gap above the keyboard, spectrogram removed from the Play macro row and opened from a new SPECTROGRAM tab after SCOPE (web only; JUCE has no spectrogram), "Table" label left-aligned above the wavetable dropdown (web).

Sampler editor (latest): `SampleEditor.h/.cpp` - responsive waveform panel on the Sampler page + FULL SCREEN EDIT overlay (markers, 16 slices, zoom/pan, crop/cut/normalize/detect, slice list with audition, live playhead). Dials can take a long-press modulation route via `Knob::setModDest` (wavetable oscillator dials so far); the new Hz (PWM speed) dial sits beside Pulse width. Max polyphony 64. Details and honest gaps: NATIVE_PARITY_AUDIT.md.

Presets (latest): a saved preset file now carries the engine preset + instrument globals (master tone, WT/FM mix, sample mix, bend range, MPE) + a free-text **notes** field (Presets tab, `Store::exportPresetFile`, `notes()`). Web-app presets additionally carry master volume, polyphony, swing, MPC pads, XY pads, imported wavetables and imported samples; JUCE reads the web `extra` block for the globals it supports (XY pads / MPC pads / swing / polyphony from web presets are ignored by JUCE).

Grain Cloud Command Centre: ANIMATION switch on the Granular FX stage (`GrainBridgeView.*`), a control-room panel (annunciator tiles, buffer and stereo-field CRTs, seven faders, four envelope rockers) bound both ways to every Granular parameter. The input-level meter is fed by an optional callback (`GrainBridgeView(store, base, levelFn)`); FxScreen does not pass one yet, so the meter and Follow lamps stay idle in the native build until it is wired to the processor's scope ring.
