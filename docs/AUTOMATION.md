# Host automation (AUv3 parameter tree)

200 parameters (`PARAMETER_MAP.md`, ids frozen in `Core/param_ids.json`). The mapping to the live preset is `Core/include/vv/param_bridge.hpp`;
scaling matches the web app's `window.VectaAUv3` bridge so web and native sessions agree.

| Host parameters | Target | Scaling |
|---|---|---|
| `master_volume` | master gain | direct, immediate (command ring) |
| `mod_wheel`, `macro_1..4` | mod wheel / macros | direct, immediate |
| `<wavetable|fm|sampler>_filter_cutoff / resonance / keytrack` | `layers.X.filter.*` | 0..1 -> 20..18000 Hz / 0..130 / 0..200 |
| `<wavetable|fm|sampler>_amp_attack / decay / release` | `layers.X.env.a/d/r` | 0..1 -> 0.001..3.001 s |
| `<...>_amp_sustain` | `layers.X.env.s` | direct |
| `lfo_1..4_rate_hz` | `lfo.lfoN.hz` | 0..1 -> 0.1..20 Hz |
| `reverb_/delay_/saturation_mix_all_*` | `params.mix` of every stage of that type | direct |
| `tempo` | transport tempo + `bpm` | BPM |
| `master_tone`, `wt_fm_mix` | globals | direct |
| `procedural_*` filter/envelope | `layers.PRO.*` | real units |
| `wt_osc1..2_*`, `fm_op1..8_*`, `smp_slot1..16_*`, `procedural_gena/b_*` | `layers.X.oscs.N.*` | real units; sampler start < end is enforced |
| `reverb_algorithm` (0 off, 1..6) | creates/enables the first reverb stage, sets `algo` | indexed |
| `reverb_*` (decay, size, predelay, mix, mod, colour) | first reverb stage `params.*` | only if the stage exists |
| `phaser_on`, `phaser_mode` | creates/enables a phaser or flanger stage, disables the other | |
| `phaser_*` (rate, depth, feedback, mix, stages) | that stage `params.*` | only if the stage exists |

Threading: `vv_engine_set_param` (any thread, including the render thread) stores the value and flags it. `vv_engine_pump_params` (a 30 Hz background
timer, started by `VectaAudioUnit`) applies all flagged parameters in one preset edit. After a preset load or UI edit,
`vv_engine_sync_params_from_preset` moves the stored host values to the preset's and `refreshParametersFromEngine()` updates the `AUParameter`s
without echoing them back. Flags set before the first preset exists are kept until it does (restored sessions are not lost).
Test: `engine_tests` "automation: ..." cases.
