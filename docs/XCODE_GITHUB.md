# Xcode + GitHub: first-build guide

Nothing in `Apple/` (Swift, SwiftUI, AUv3) has been compiled yet. This is the shortest path from the zip to a first build, with GitHub catching errors for you.

## 1. Put the project on GitHub
1. Unzip `VectaVortex-iOS-native.zip`. Make a repository whose root is either the `VectaVortex-iOS` folder or the folder that contains it.
2. If the root is the parent folder, copy `VectaVortex-iOS/.github/workflows/*.yml` to `<root>/.github/workflows/`. Both workflows find the project folder themselves.
3. Commit and push to `main`. The `ios-build` workflow (macOS runner) runs the C++ core tests, generates the Xcode project with XcodeGen and builds the app plus its AUv3 extension for the simulator, unsigned.
4. If it fails, open the run: the **Errors** step summary lists every `error:` line and the `xcodebuild-log` artifact has the full log. Send those lines back and each one gets fixed.

`*.xcodeproj` is generated from `Apple/project.yml` and is git-ignored on purpose, so there is nothing to merge.

## 2. Build on your Mac
1. Xcode 26 or newer (needed for App Store upload), then `brew install xcodegen`.
2. `cd Apple && xcodegen generate && open VectaVortex.xcodeproj`
3. Select the `VectaVortex` scheme (it builds the AUv3 extension with it). Put your Team ID in `DEVELOPMENT_TEAM` in `project.yml` (or choose it under Signing & Capabilities for both targets) and run `xcodegen generate` again.
4. Run on a simulator first, then a real iPhone or iPad. Audio latency and AUv3 hosting can't be judged in the simulator.
5. Bundle ids: `com.dreammusictemple.vectavortex` and `.auv3`. Change them in `project.yml` if you want others.

Further steps: `MAC_DAY_ONE.md` (known first-build suspects), `APPSTORE_CHECKLIST.md`, `RELEASE_AUDIT.md`.

## 3. What to check by hand after the first build: transient editor
Sampler slot > waveform (and FULL SCREEN EDIT):
- Touch a numbered yellow flag and drag: the marker follows the finger without jumping, stops at its neighbours and does not change the sound until you lift.
- Tap a flag: it is selected, shows a red ✕ and (if enabled) plays its slice. Tap the ✕, or DELETE SELECTED, or double-tap the flag: the slice is deleted. UNDO brings it back.
- Double-tap the empty waveform: a new marker. CLEAR ALL empties the list.
- Zoomed in, drag empty waveform to pan; pinch to zoom. With the whole sample in view, dragging empty waveform still moves the nearer start/end handle.
- Scrub a long sample (a few minutes) while zoomed out: panning and zooming should stay smooth.
