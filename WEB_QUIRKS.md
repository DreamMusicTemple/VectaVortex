# Web app quirks and deliberate native deviations

Rows marked "Same" are reproduced as far as the 117 browser reference scenarios exercise them; they were not each isolated in a dedicated test.

The C++ port reproduces the web app sample for sample wherever the web behaviour is deterministic (see PORT_PLAN.md). The items below are
places where the web app is quirky, timer-dependent, or unmeasurable offline. Each says what the native engine does.

| # | Web behaviour | Native |
|---|---|---|
| 1 | The EQ is not applied until an EQ knob is touched. | NOT reproduced: the native engine applies the preset's EQ settings from the start. A web preset with non-flat but untouched EQ will sound different until this is decided. |
| 2 | The 100 ms modulation tick overwrites sequencer p-lock position / pulse width / phase / start-end / segment on WT oscillators. | Same. |
| 3 | Sampler glide adds a Hz offset to the playback rate. | Same. |
| 4 | The arpeggiator interval is re-read every 500 ms (setInterval reset), so tempo/rate changes lag. | Native arp clock follows the current rate every step (no 500 ms lag). |
| 5 | FX modulation for reverb / saturation / delay etc. accumulates every tick (drifts); wavefolder / filter / granular use direct offsets. | Direct-offset model for all. Not browser-verified for the cumulative ones. |
| 6 | Pitch bend adds cents as Hz on WT oscillators. | Same. |
| 7 | The sequencer ticks every 10 ms, so tempo drifts and steps land on the 10 ms grid. | Exact sample timing. `Performance::seqTickSeconds = 0.01` emulates the browser grid for parity tests. |
| 8 | Oscillator-level mod destinations only affect newly started notes. | Same. |
| 9 | The first sounding step of a pattern is step 1; step 0 only plays after the loop wraps. | Same (kept for preset compatibility). |
| 10 | `state.noteRepeat` is UI only. | Not ported. |
| 11 | Swing, max polyphony and legato are not part of the saved state. | The preset parser reads them if present; defaults otherwise. |
| 12 | Mono legato: the old voice gets `stop(0.008)` but the voice still rings for its envelope release. | Same (confirmed in the unit test). |
| 13 | Test-harness artefact: with a non-zero level offset, voices dropped from the pool keep sounding because the harness drops cleanup timers. | Scenario `compareUntil` limits the comparison window. |
| 14 | Loading a preset in the web app does not rebuild the vocoder (it keeps the previous bands/spacing until the panel is touched). | NOT reproduced: the native vocoder is rebuilt from the preset. |
| 15 | Vocoder spacing option value is `lin`; anything other than `log` is linear. | Same (`== "log"` test). |
| 16 | Pitch bend in the MPE path is applied in Hz on the WT oscillators (bend-in-Hz bug). | NOT copied: the native engine bends in cents. Covered by a native unit test, not a browser comparison. |
| 17 | Phase-modulation cross-mod has a start transient the native engine does not reproduce (about 2.5e-3 relative RMS in `xmod_pm`, `xmod_loop`); steady state matches. | Tolerance override in `tolerances.txt`. Cause unknown. |
| 18 | A held note's non-modulated dials are not updated while it sounds (they are read when the note is built). | Same; live edits apply from the next note. Modulated dials update live. |
| 19 | FX stages in a freshly added web stage have all sends off. | The native host-automation path (`reverb_algorithm`, `phaser_on`) creates the stage with all four sends on, because the AU has no send UI of its own. |

