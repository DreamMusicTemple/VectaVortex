# LUA Visuals - native (iOS / iPadOS)

What is verified and what is not.

**Built and tested (C++, run on Linux with g++):**
- `Core/include/vv/lx/lx_viz.hpp` - the visual script engine (same code as the web WASM build). `vv_viz_tests` loads all 18 factory scripts (`Core/viz/factory/*.lua`), runs 240 frames each, checks no faults, instruction budgets, animation, and that **every parameter changes the output**.
- `Core/include/vv/lx/viz_host.hpp` + `Bridge/vv_viz.{h,cpp}` - UI-thread audio analysis (FFT, 9 bands + user range, beat, flux, centroid, peak frequency, 32-bin spectrum), per-parameter band assignment, randomise/reset, bad-script rejection. `vv_viz_host_tests` checks tone-to-band response, decay to silence, assignment effect, error recovery and runs every factory script through the C bridge with live analysis and randomised parameters.
- `cd Core && cmake -B build && cmake --build build && ctest` runs both (targets `viz`, `viz_host`).

**Written but NOT compiled (no Mac / Xcode available when this was written):**
- `Apple/Shared/Viz/VizScenes.metal` - generated from the web GLSL by `tools/gen_metal_viz.py` (re-run it after changing `viz_shaders.js`). The generator lints for leftover GLSL; it cannot compile MSL.
- `Apple/Shared/Viz/VizPasses.metal` - feedback, bloom, grade, Lua-canvas batch and sand-particle passes (hand port of `viz_render.js`).
- `Apple/Shared/Viz/VizMetalRenderer.swift`, `VizHost.swift`, `VizGenData.swift`, `LuaVisualView.swift`; `ScopeView.swift` gains an OSCILLOSCOPE / LUA VISUALS switch.
- Expect a first-build round of small compile fixes (Swift concurrency annotations, MSL overload details). No frame-rate numbers exist for a real iPad.

**Differences from the web build:** audio comes from the same master tap the scope uses (`scopeSnapshot`, 2048 samples, so low-frequency resolution is about 23 Hz per bin versus about 12 Hz on the web); at most 4096 canvas shapes per frame are drawn (the script limit is 14,000); the sand state uses nearest sampling; export/import use the same `.vizpreset.json` format as the web app (web presets import here and vice versa).
