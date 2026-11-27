// Writes the scenario files under Core/tests/scenarios (then run: node tools/ref_render.js Core/tests/scenarios/*.json).
const fs = require('fs'), path = require('path');
const dir = path.resolve(__dirname, '../Core/tests/scenarios'); fs.mkdirSync(dir, { recursive: true });
const out = [];
function sc(name, o) { const s = Object.assign({ name, sr: 48000, dur: 2.5, events: [], patch: [] }, o); out.push(s); fs.writeFileSync(path.join(dir, name + '.json'), JSON.stringify(s)); }
const on = (t, layers, note, extra) => Object.assign({ t, on: true, layers, note, vel: 0.8 }, extra || {});
const off = (t, layers, note) => ({ t, off: true, layers, note });
const WT = ['WT'], FM = ['FM'], PRO = ['PRO'];
const W = (i, k, v) => [`layers.WT.oscs.${i}.${k}`, v];
// ---- wavetable
sc('wt_default', { dur: 3, events: [on(0, WT, 60), off(1.5, WT, 60)] });
sc('wt_chord', { events: [on(0, WT, 60), on(0, WT, 64), on(0.05, WT, 67), off(1.2, WT, 60), off(1.2, WT, 64), off(1.4, WT, 67)] });
sc('wt_unison', { patch: [W(0, 'unison', 5), W(0, 'detune', 25), W(0, 'width', 0.8)], events: [on(0, WT, 55), off(1.4, WT, 55)] });
sc('wt_two_osc', { patch: [W(1, 'enabled', true), W(1, 'level', 0.6), W(1, 'table', 'sawBright'), W(1, 'oct', -1), W(1, 'detune', 0), W(0, 'table', 'digitalMorph'), W(0, 'position', 0.4), W(1, 'position', 0.7), W(1, 'pan', 0.5), W(0, 'pan', -0.5)], events: [on(0, WT, 57), off(1.4, WT, 57)] });
sc('wt_sub_noise', { patch: [W(0, 'sub', 1), W(1, 'enabled', true), W(1, 'noiseOn', true), W(1, 'noiseType', 'pink'), W(1, 'level', 0.15)], events: [on(0, WT, 48), off(1.4, WT, 48)] });
sc('wt_sync', { patch: [['layers.WT.syncEnabled', true], W(1, 'enabled', true), W(1, 'level', 0.5), W(1, 'oct', 1), W(1, 'detune', 700), W(1, 'table', 'sawBright'), W(0, 'level', 0.4), W(0, 'sub', 2)], events: [on(0, WT, 52), off(1.3, WT, 52)] });
sc('wt_pwm_phase_seg', { patch: [W(0, 'table', 'glassy'), W(0, 'pwm', 0.3), W(0, 'phase', 0.25), W(0, 'startEnd', 0.6), W(0, 'segment', 0.5), W(0, 'position', 0.6), W(0, 'pingPong', true)], events: [on(0, WT, 62), off(1.3, WT, 62)] });
sc('wt_env', { patch: [['layers.WT.env', { a: 0.2, d: 0.3, s: 0.4, r: 0.5 }]], events: [on(0, WT, 60), off(1.0, WT, 60)] });
sc('wt_env2', { patch: [W(1, 'enabled', true), W(1, 'level', 0.6), W(1, 'table', 'sawBright'), ['layers.WT.env', { a: 0.01, d: 0.4, s: 0.2, r: 0.2 }], ['layers.WT.env2', { a: 0.3, d: 0.1, s: 0.8, r: 0.6 }]], events: [on(0, WT, 60), off(1.1, WT, 60)] });
['moog4pole', 'diode4pole', 'onepole6db', 'twopole12db', 'statevariable', 'comb', 'highpass', 'bandpass', 'formant'].forEach((ft, i) => {
  sc('wt_filter_' + ft, { patch: [W(0, 'table', 'sawBright'), W(0, 'position', 0.5), [`layers.WT.oscs.0.filter`, { type: ft, cutoff: 1200, reso: 35, keytrack: 50, volume: 0.9, lpgDecay: 0.8 }]], events: [on(0, WT, 57 + i), off(1.2, WT, 57 + i)] });
});
[['male', 'i'], ['female', 'u']].forEach(([tb, vw]) => sc('wt_filter_formant_' + tb, { patch: [W(0, 'table', 'sawBright'), W(0, 'position', 0.5), ['layers.WT.oscs.0.filter', { type: 'formant', cutoff: 500, reso: 40, keytrack: 0, volume: 0.9, lpgDecay: 0.8, formantTable: tb, vowel: vw }]], events: [on(0, WT, 55), off(1.2, WT, 55)] }));
sc('wt_filter_env', { patch: [W(0, 'table', 'sawBright'), ['layers.WT.oscs.0.filter', { type: 'moog4pole', cutoff: 500, reso: 30, keytrack: 0, volume: 1, lpgDecay: 0.8 }], ['layers.WT.oscs.0.filterEnv', { amount: 6000, a: 0.05, d: 0.4, s: 0.3, r: 0.5, shape: null }]], events: [on(0, WT, 48), off(1.0, WT, 48)] });
sc('wt_lpg', { patch: [W(0, 'table', 'sawBright'), ['layers.WT.oscs.0.filter', { type: 'lpg', cutoff: 8000, reso: 20, keytrack: 0, volume: 1, lpgDecay: 0.4 }]], events: [on(0, WT, 60), off(0.5, WT, 60)] });
sc('wt_glide', { patch: [W(0, 'glideTime', 0.3)], events: [on(0, WT, 55), off(0.6, WT, 55), on(0.6, WT, 67), off(1.8, WT, 67)] });
sc('wt_glide_unison', { patch: [W(0, 'glideTime', 0.25), W(0, 'unison', 3), W(0, 'detune', 20), W(0, 'oct', 1)], events: [on(0, WT, 50), off(0.5, WT, 50), on(0.5, WT, 62), off(1.6, WT, 62)] });
sc('wt_zones', { patch: [W(0, 'rangeLow', 60), W(0, 'rangeHigh', 72), W(0, 'pitchKeytrack', 50)], events: [on(0, WT, 48), on(0, WT, 64), off(1.2, WT, 64)] });
sc('wt_plocks', { events: [on(0, WT, 60, { plocks: { position: 0.8, cutoff: 900, level: 0.5 } }), off(1.2, WT, 60)] });
sc('wt_legato', { settings: { maxPolyphony: 1, legato: true }, events: [on(0, WT, 60), on(0.5, WT, 64, { legato: true }), off(1.4, WT, 64)] });
sc('wt_eq', { settings: { applyEq: true }, patch: [['layers.WT.eq', { band1: { freq: 150, gain: 6 }, band2: { freq: 800, gain: -8 }, band3: { freq: 3000, gain: 9 }, band4: { freq: 9000, gain: -6 } }], W(0, 'table', 'sawBright')], events: [on(0, WT, 60), off(1.3, WT, 60)] });
sc('wt_master', { settings: { masterVolume: 0.6, masterTone: 0.5, wtFmMix: 0.2 }, patch: [W(0, 'table', 'sawBright')], events: [on(0, WT, 60), on(0, WT, 64), on(0, WT, 67), off(1.3, WT, 60), off(1.3, WT, 64), off(1.3, WT, 67)] });
sc('wt_limiter', { patch: [W(0, 'table', 'sawBright'), W(0, 'level', 1), W(0, 'unison', 8), W(0, 'detune', 10), ['layers.WT.env', { a: 0.001, d: 0.2, s: 1, r: 0.2 }]], events: [on(0, WT, 48), on(0, WT, 55), on(0, WT, 60), on(0, WT, 64), on(0, WT, 67), off(1.3, WT, 48), off(1.3, WT, 55), off(1.3, WT, 60), off(1.3, WT, 64), off(1.3, WT, 67)] });
// ---- FM
const FMo = (i, k, v) => [`layers.FM.oscs.${i}.${k}`, v];
const fmOn = [['layers.FM.engineEnabled', [true, false]]];
sc('fm_default', { patch: fmOn, events: [on(0, FM, 60), off(1.4, FM, 60)] });
for (let a = 0; a < 6; a++) sc('fm_algo' + a, { patch: [...fmOn, ['layers.FM.fmAlgorithms', [a, 2]], FMo(1, 'level', 0.6), FMo(2, 'level', 0.5), FMo(3, 'level', 0.4), FMo(1, 'ratio', 2), FMo(2, 'ratio', 3), FMo(3, 'ratio', 1.5), FMo(0, 'feedback', 0.3)], events: [on(0, FM, 57), off(1.2, FM, 57)] });
sc('fm_two_engines', { patch: [['layers.FM.engineEnabled', [true, true]], FMo(4, 'level', 0.7), FMo(5, 'level', 0.5), FMo(5, 'ratio', 3), ['layers.FM.engineOctave', [0, 1]], ['layers.FM.engineFine', [0, 7]], ['layers.FM.fmAlgorithms', [2, 0]], ['layers.FM.fmSub', [1, 0]], FMo(1, 'level', 0.5), FMo(0, 'wave', 'triangle'), FMo(4, 'wave', 'square')], events: [on(0, FM, 55), off(1.3, FM, 55)] });
sc('fm_feedback', { patch: [...fmOn, FMo(1, 'level', 0.6), FMo(1, 'ratio', 2), FMo(0, 'feedback', 0.3)], events: [on(0, FM, 60), off(1.4, FM, 60)] });
sc('fm_glide', { patch: [...fmOn, ['layers.FM.engineGlide', [0.25, 0]], FMo(1, 'level', 0.6), FMo(0, 'glideTime', 0.2), FMo(1, 'glideTime', 0.2)], events: [on(0, FM, 50), off(0.5, FM, 50), on(0.5, FM, 62), off(1.6, FM, 62)] });
sc('fm_filter', { patch: [...fmOn, FMo(1, 'level', 0.6), FMo(1, 'ratio', 3), ['layers.FM.oscs.0.filter', { type: 'diode4pole', cutoff: 1500, reso: 40, keytrack: 100, volume: 1, lpgDecay: 0.8 }], ['layers.FM.oscs.0.filterEnv', { amount: 3000, a: 0.02, d: 0.3, s: 0.2, r: 0.3, shape: null }]], events: [on(0, FM, 57), off(1.0, FM, 57)] });
// ---- procedural
['pluck', 'modal', 'breath', 'chaos'].forEach(m => sc('pro_' + m, { patch: [['layers.PRO.oscs.0.enabled', true], ['layers.PRO.oscs.0.model', m], ['layers.PRO.oscs.1.enabled', true], ['layers.PRO.oscs.1.model', m], ['layers.PRO.oscs.1.oct', 1], ['layers.PRO.oscs.1.level', 0.4], ['layers.PRO.oscs.1.pan', 0.6]], events: [on(0, PRO, 57), off(1.2, PRO, 57)] }));
sc('wt_glide_gap', { patch: [W(0, 'glideTime', 0.3)], events: [on(0, WT, 55), off(0.3, WT, 55), on(1.0, WT, 67), off(2.0, WT, 67)] });
// ---- layer mixes
sc('mix_wt_fm', { settings: { wtFmMix: 0.5 }, patch: [...fmOn, FMo(1, 'level', 0.5)], events: [on(0, ['WT', 'FM'], 60), off(1.3, ['WT', 'FM'], 60)] });
// ---- sampler
const SM = ['SMP']; const SP = (i, k, v) => [`layers.SMP.oscs.${i}.${k}`, v];
const smp = (i, name, extra) => [SP(i, 'name', name), SP(i, 'level', 0.8), ...(extra || [])];
sc('smp_default', { patch: smp(0, 'Flute (synthesized)'), events: [on(0, SM, 60), off(1.5, SM, 60)] });
sc('smp_pitch', { patch: smp(0, 'Choir Ahh (synthesized)', [SP(0, 'oct', 1), SP(0, 'fineTune', 17), SP(0, 'root', 55)]), events: [on(0, SM, 67), on(0.3, SM, 52), off(1.6, SM, 67), off(1.6, SM, 52)] });
sc('smp_loop_points', { patch: smp(0, 'Warm Pad (synthesized)', [SP(0, 'loop', true), SP(0, 'loopStartFrac', 0.2), SP(0, 'loopEndFrac', 0.5)]), events: [on(0, SM, 60), off(2.0, SM, 60)] });
sc('smp_noloop', { patch: smp(0, 'Bells & Chimes (synthesized)', [SP(0, 'loop', false)]), events: [on(0, SM, 60), off(2.2, SM, 60)] });
sc('smp_pingpong', { patch: smp(0, 'Flute (synthesized)', [SP(0, 'pingPong', true), SP(0, 'start', 0.1), SP(0, 'end', 0.3)]), events: [on(0, SM, 62), off(1.8, SM, 62)] });
sc('smp_reverse_flag', { patch: smp(0, 'Bells & Chimes (synthesized)', [SP(0, 'reverse', true), SP(0, 'loop', true)]), events: [on(0, SM, 60), off(1.5, SM, 60)] });
sc('smp_plock_slice', { patch: smp(0, 'Beat Loop (synthesized, original pattern)', [SP(0, 'loop', false)]), events: [on(0, SM, 60, { plocks: { samplePosition: 0.3, sampleEndPosition: 0.45 } }), off(1.2, SM, 60), on(1.3, SM, 60, { plocks: { samplePosition: 0.3, sampleEndPosition: 0.45, sampleReverse: true } }), off(2.4, SM, 60)] });
sc('smp_env', { patch: smp(0, 'Warm Pad (synthesized)', [SP(0, 'env', { a: 0.25, d: 0.3, s: 0.5, r: 0.6 })]), events: [on(0, SM, 60), off(1.1, SM, 60)] });
sc('smp_global_adsr', { patch: smp(0, 'Warm Pad (synthesized)', [['layers.SMP.globalAdsrActive', true], ['layers.SMP.globalAdsr', { a: 0.05, d: 0.2, s: 0.7, r: 0.8 }]]), events: [on(0, SM, 64), off(1.0, SM, 64)] });
sc('smp_fades', { patch: smp(0, 'Bells & Chimes (synthesized)', [SP(0, 'loop', false), SP(0, 'fadeIn', 0.3), SP(0, 'fadeOut', 0.5), SP(0, 'start', 0.1), SP(0, 'end', 0.6)]), events: [on(0, SM, 60), off(1.9, SM, 60)] });
sc('smp_two_slots', { settings: { sampleMix: 0.5 }, patch: [...smp(0, 'Flute (synthesized)', [SP(0, 'pan', -0.6)]), ...smp(1, 'Choir Ahh (synthesized)', [SP(1, 'pan', 0.6), SP(1, 'oct', -1)])], events: [on(0, SM, 60), off(1.4, SM, 60)] });
sc('smp_filter', { patch: smp(0, 'Warm Pad (synthesized)', [['layers.SMP.oscs.0.filter', { type: 'moog4pole', cutoff: 900, reso: 30, keytrack: 50, volume: 1, lpgDecay: 0.8 }], ['layers.SMP.oscs.0.filterEnv', { amount: 4000, a: 0.05, d: 0.4, s: 0.3, r: 0.4, shape: null }]]), events: [on(0, SM, 55), off(1.3, SM, 55)] });
sc('smp_zones_noise', { patch: [...smp(0, 'Flute (synthesized)', [SP(0, 'rangeLow', 48), SP(0, 'rangeHigh', 59)]), ...smp(1, 'Choir Ahh (synthesized)', [SP(1, 'rangeLow', 60), SP(1, 'rangeHigh', 72)]), SP(2, 'noiseOn', true), SP(2, 'level', 0.1)], events: [on(0, SM, 55), on(0.2, SM, 64), off(1.2, SM, 55), off(1.2, SM, 64)] });
sc('smp_glide', { patch: smp(0, 'Flute (synthesized)', [SP(0, 'glideTime', 0.3)]), events: [on(0, SM, 55), off(0.6, SM, 55), on(0.6, SM, 67), off(1.8, SM, 67)] });
// ---- FX chain (stage params as the web app stores them; sends route layers into the chain)
const stg = (type, params, sends, enabled) => ({ type, enabled: enabled !== false, params, sends: Object.assign({ WT: true, FM: false, SMP: false, PRO: false }, sends || {}) });
const fxsc = (name, stages, extra) => sc('fx_' + name, Object.assign({ dur: 2.5, patch: [['fxStages', stages], ['layers.WT.oscs.0.table', 'sawBright']], events: [on(0, WT, 57), on(0, WT, 64), off(0.9, WT, 57), off(0.9, WT, 64)] }, extra || {}));
fxsc('reverb_hall', [stg('reverb', { algo: 'hall', decay: 0.7, size: 0.8, predelay: 0.02, mix: 0.5, mod: 0.4, colour: 0.5 })]);
fxsc('reverb_plate_amb', [stg('reverb', { algo: 'plate ambient', decay: 0.6, size: 0.5, predelay: 0.03, mix: 0.4, mod: 0.5, colour: 0.3 })]);
fxsc('reverb_room', [stg('reverb', { algo: 'room', decay: 0.6, size: 0.6, predelay: 0.01, mix: 0.5 })]);
fxsc('reverb_shimmer', [stg('reverb', { algo: 'shimmer', decay: 0.7, size: 0.6, predelay: 0.01, mix: 0.5, shimA: 12, shimB: -12 })]);
fxsc('delay_stereo', [stg('delay', { mode: 'stereo', time: 0.23, feedback: 0.5, mix: 0.4, sync: false, syncDiv: '1/8' })]);
fxsc('delay_pingpong_sync', [stg('delay', { mode: 'pingpong', time: 0.3, feedback: 0.45, mix: 0.5, sync: true, syncDiv: '1/8.' })]);
fxsc('delay_reverse', [stg('delay', { mode: 'reverse', time: 0.4, feedback: 0.3, mix: 0.5, sync: false, syncDiv: '1/8' })]);
fxsc('saturation', [stg('saturation', { type: 'hard', drive: 0.7, tone: 4000, mix: 0.8 })]);
fxsc('saturation_os', [stg('saturation', { type: 'soft', drive: 0.6, tone: 8000, mix: 1 })], { patch: [['fxStages', [stg('saturation', { type: 'soft', drive: 0.6, tone: 8000, mix: 1 })]], ['oversample', '4x'], ['layers.WT.oscs.0.table', 'sawBright']] });
fxsc('wavefolder', [stg('wavefolder', { drive: 6, symmetry: 0.3, mix: 0.9, mode: 'sine', pwm: 0.5 })]);
fxsc('filter', [stg('filter', { filterType: 'moog4pole', cutoff: 900, resonance: 40, mix: 0.7 })]);
fxsc('resonator', [stg('resonator', { freq: 330, resonance: 25, mix: 0.6 })]);
fxsc('phaser', [stg('phaser', { rate: 0.4, depth: 0.85, feedback: 0.6, mix: 0.5, stages: 8 })]);
fxsc('flanger', [stg('flanger', { rate: 0.25, depth: 0.5, feedback: 0.3, mix: 0.5 })]);
fxsc('chorus', [stg('chorus', { chorus1On: true, chorus2On: true, mix: 0.6 })]);
fxsc('spectral', [stg('spectral', { freeze: 0, blur: 0.4, shift: 20, threshold: 0.1, spread: 0.5, mix: 0.8 })]);
fxsc('panning_auto', [stg('panning', { width: 1, autoPanOn: true, rate: 2, shape: 'triangle', mode: 'stereo', distance: 0.3, mix: 1 })]);
fxsc('panning_3d', [stg('panning', { width: 1, autoPanOn: false, rate: 1, shape: 'square', mode: '3dspace', distance: 0.6, mix: 0.7 })]);
fxsc('glitch_bits', [stg('glitch', { bitDepth: 6, srReduction: 4, stutterOn: false, stutterDivision: 0.25, stutterAmount: 0.5, mix: 0.8, seqOn: false, seqSteps: [true, true, true, true, true, true], seqRate: '1/16', seqIntensity: 0.3, seqTempoDiv: '1' })]);
fxsc('chain3', [stg('saturation', { type: 'normal', drive: 0.5, tone: 6000, mix: 0.6 }), stg('delay', { mode: 'stereo', time: 0.2, feedback: 0.4, mix: 0.3, sync: false, syncDiv: '1/8' }, { WT: false }), stg('reverb', { algo: 'hall', decay: 0.6, size: 0.6, predelay: 0.02, mix: 0.3, mod: 0.4, colour: 0.5 }, { WT: false })]);
fxsc('sends_mixed', [stg('delay', { mode: 'pingpong', time: 0.25, feedback: 0.4, mix: 0.5, sync: false, syncDiv: '1/8' }, { WT: true, FM: true }), stg('chorus', { chorus1On: true, chorus2On: false, mix: 0.5 }, { WT: false, FM: false, SMP: true })], { settings: { applyEq: true }, patch: [['fxStages', [stg('delay', { mode: 'pingpong', time: 0.25, feedback: 0.4, mix: 0.5, sync: false, syncDiv: '1/8' }, { WT: true, FM: true }), stg('chorus', { chorus1On: true, chorus2On: false, mix: 0.5 }, { WT: false, FM: false, SMP: true })]], ['layers.WT.oscs.0.table', 'sawBright'], ['layers.FM.oscs.0.level', 0.8], ['layers.FM.engineEnabled', [true, false]], ['layers.SMP.oscs.0.name', 'Flute (synthesized)'], ['layers.SMP.oscs.0.level', 0.6]], events: [on(0, ['WT', 'FM', 'SMP'], 60), off(0.9, ['WT', 'FM', 'SMP'], 60)] });
fxsc('disabled_stage', [stg('saturation', { type: 'hard', drive: 0.9, tone: 3000, mix: 1 }, {}, false), stg('chorus', { chorus1On: true, chorus2On: false, mix: 0.5 })]);
const mbp = (fx) => ({ width: 1, autoPanOn: false, rate: 1, shape: 'square', mode: 'stereo', distance: 0.3, mix: 1, mb: { on: true, xover: [200, 1500, 6000], bands: [{ on: true, width: 0.4, depth: 0, speed: 0.25, fx: 0 }, { on: true, width: 0.7, depth: 0.5, speed: 0.4, fx: 0 }, { on: true, width: 0.9, depth: 0.6, speed: 0.65, fx: fx || 0 }, { on: true, width: 1, depth: 0.8, speed: 0.9, fx: 0 }] } });
fxsc('panning_multiband', [stg('panning', mbp(0))]);
fxsc('panning_mb_patched', [stg('panning', mbp(2)), stg('saturation', { type: 'hard', drive: 0.8, tone: 5000, mix: 1 }, { WT: false })]);

// ---- modulation matrix (the web scheduler tick is driven at exact 100 ms audio intervals: "ticks": true)
const cn = (source, dest, amount) => ({ source, dest, amount });
const msc = (name, conns, o) => sc('mod_' + name, Object.assign({ dur: 2.5, ticks: true, events: [on(0, WT, 60), off(1.8, WT, 60)] }, o, { patch: [['connections', conns], ...((o && o.patch) || [])] }));
const lfoP = (k, hz, wf, extra) => [[`lfo.lfo${k}.hz`, hz], [`lfo.lfo${k}.waveform`, wf || 'sine'], ...(extra || [])];
const wtSaw = [W(0, 'table', 'sawBright'), W(0, 'position', 0.5)];
const wtFilt = (ft, cut, res) => ['layers.WT.oscs.0.filter', { type: ft || 'moog4pole', cutoff: cut || 1500, reso: res || 25, keytrack: 0, volume: 1, lpgDecay: 0.8 }];
msc('lfo_cutoff', [cn('lfo1', 'WT:osc1Cutoff', 40)], { patch: [...wtSaw, wtFilt(), ...lfoP(1, 2, 'sine')] });
msc('lfo_reso', [cn('lfo1', 'WT:osc1Reso', 60), cn('lfo2', 'WT:cutoff', 30)], { patch: [...wtSaw, wtFilt('twopole12db', 1200, 30), ...lfoP(1, 1.5, 'triangle'), ...lfoP(2, 0.7, 'sine')] });
msc('lfo_pitch', [cn('lfo1', 'WT:pitch', 8)], { patch: [...wtSaw, ...lfoP(1, 5, 'sine')] });
msc('lfo_pitch_unison', [cn('lfo1', 'WT:pitch', 3)], { patch: [...wtSaw, W(0, 'unison', 3), W(0, 'detune', 20), W(0, 'sub', 1), ...lfoP(1, 3, 'sine')] });
msc('lfo_scan', [cn('lfo2', 'WT:osc1Scan', 60), cn('lfo3', 'WT:osc1Pwm', 40)], { patch: [W(0, 'table', 'digitalMorph'), W(0, 'position', 0.4), ...lfoP(2, 1.5, 'triangle'), ...lfoP(3, 1, 'sawUp')] });
msc('lfo_scan_shared', [cn('lfo2', 'WT:scan', 50), cn('lfo1', 'WT:startEnd', -40), cn('lfo3', 'WT:phase', 30)], { patch: [W(0, 'table', 'glassy'), W(1, 'enabled', true), W(1, 'level', 0.5), W(1, 'table', 'digitalMorph'), ...lfoP(1, 1.2, 'sine'), ...lfoP(2, 1, 'triangle'), ...lfoP(3, 0.8, 'sawDown')] });
msc('lfo_segment', [cn('lfo1', 'WT:osc1Segment', -50)], { patch: [W(0, 'table', 'digitalMorph'), W(0, 'position', 0.6), ...lfoP(1, 1.3, 'sine')] });
msc('lfo_level_fvol', [cn('lfo1', 'WT:level', 60), cn('lfo2', 'WT:filterVolume', -40)], { compareUntil: 2.14, patch: [...wtSaw, wtFilt('diode4pole', 3000, 20), ...lfoP(1, 3, 'sine'), ...lfoP(2, 1.1, 'triangle')] });
msc('lfo_tune', [cn('lfo1', 'WT:osc1Octave', 50), cn('lfo2', 'WT:osc1Semi', 30), cn('lfo3', 'WT:osc1Fine', 50)], { patch: [...wtSaw, ...lfoP(1, 0.9, 'sine'), ...lfoP(2, 1.3, 'triangle'), ...lfoP(3, 2, 'sine')] });
msc('lfo_autoscan', [], { patch: [W(0, 'table', 'digitalMorph'), W(0, 'positionLfoSource', 'lfo1'), ...lfoP(1, 0.8, 'triangle')] });
msc('sources', [cn('velocity', 'WT:osc1Cutoff', 30), cn('modwheel', 'WT:pitch', 4), cn('envWT', 'WT:osc1Reso', 40), cn('envWT', 'WT:osc1Cutoff', 25)], { settings: { modwheel: 0.8 }, patch: [...wtSaw, wtFilt(), ['layers.WT.env', { a: 0.3, d: 0.4, s: 0.5, r: 0.3 }]], events: [on(0, WT, 60, { vel: 0.4 }), off(1.8, WT, 60)] });
msc('macros', [cn('macro1', 'WT:osc1Cutoff', 50), cn('lfo1', 'GLOBAL:macro1', 40), cn('macro2', 'WT:pitch', 6)], { patch: [...wtSaw, wtFilt(), ['macros', [0.5, 0.75, 0, 0]], ...lfoP(1, 1.5, 'sine')] });
msc('env_follow', [cn('envFollow', 'WT:osc1Cutoff', 60)], { patch: [...wtSaw, wtFilt(), ['envFollow', { a: 0.4, d: 0.5, s: 0.3, r: 0.4, shape: null, lastInfo: null }]] });
msc('lfo_cross', [cn('lfo1', 'LFO2:rate', 60), cn('lfo3', 'LFO2:amount', -40), cn('lfo4', 'LFO2:phase', 50), cn('lfo2', 'WT:osc1Cutoff', 40)], { patch: [...wtSaw, wtFilt(), ...lfoP(1, 0.9, 'sine'), ...lfoP(2, 3, 'triangle'), ...lfoP(3, 1.1, 'sine'), ...lfoP(4, 0.6, 'sine')] });
['triangle', 'pulse', 'sawUp', 'sawDown', 'sh', 'noise'].forEach(wf => msc('wave_' + wf, [cn('lfo1', 'WT:osc1Cutoff', 45)], { patch: [...wtSaw, wtFilt(), ...lfoP(1, 2.3, wf), ['lfo.lfo1.phaseOffset', 0.2]] }));
msc('wave_unipolar_custom', [cn('lfo1', 'WT:osc1Cutoff', 45)], { patch: [...wtSaw, wtFilt(), ...lfoP(1, 1.7, 'custom'), ['lfo.lfo1.bipolar', false], ['lfo.lfo1.shape', [{ x: 0, y: 0.1 }, { x: 0.3, y: 0.9 }, { x: 0.7, y: 0.2 }, { x: 1, y: 0.8 }]]] });
msc('lfo_sync', [cn('lfo1', 'WT:osc1Cutoff', 45)], { patch: [...wtSaw, wtFilt(), ['lfo.lfo1.rateMode', 'sync'], ['lfo.lfo1.division', 0.5], ['bpm', 100]] });
msc('lfo_retrig', [cn('lfo1', 'WT:osc1Cutoff', 45)], { patch: [...wtSaw, wtFilt(), ...lfoP(1, 1.3, 'sawUp'), ['lfo.lfo1.mode', 'retrig']], events: [on(0, WT, 60), off(0.8, WT, 60), on(1.05, WT, 64), off(2.0, WT, 64)] });
msc('lfo_held', [cn('lfo1', 'WT:osc1Cutoff', 45)], { patch: [...wtSaw, wtFilt(), ...lfoP(1, 1.3, 'sawUp'), ['lfo.lfo1.mode', 'retrig'], ['lfo.lfo1.held', true]], events: [on(0.3, WT, 60), off(1.0, WT, 60), on(1.4, WT, 64), off(2.2, WT, 64)] });
msc('chord_poly', [cn('lfo1', 'WT:osc1Cutoff', 40), cn('lfo2', 'WT:pitch', 3), cn('velocity', 'WT:level', 30)], { compareUntil: 2.14, patch: [...wtSaw, wtFilt(), ...lfoP(1, 2, 'sine'), ...lfoP(2, 4, 'triangle')], events: [on(0, WT, 60, { vel: 0.5 }), on(0.2, WT, 64, { vel: 0.9 }), on(0.45, WT, 67), off(1.8, WT, 60), off(1.8, WT, 64), off(1.9, WT, 67)] });
msc('global_mix_tone', [cn('lfo1', 'GLOBAL:wtFmMix', 40), cn('lfo2', 'GLOBAL:masterTone', 50)], { settings: { wtFmMix: 0.5 }, patch: [...fmOn, FMo(1, 'level', 0.5), ...wtSaw, ...lfoP(1, 1.2, 'sine'), ...lfoP(2, 0.8, 'triangle')], events: [on(0, ['WT', 'FM'], 60), off(1.8, ['WT', 'FM'], 60)] });
// FM
const FMon = (conns, o) => msc('fm_' + conns.name, conns.c, Object.assign({ events: [on(0, FM, 60), off(1.8, FM, 60)] }, o, { patch: [...fmOn, FMo(1, 'level', 0.6), FMo(1, 'ratio', 2), ...((o && o.patch) || [])] }));
FMon({ name: 'ratio', c: [cn('lfo1', 'FM:op2Ratio', 30)] }, { patch: lfoP(1, 1.3, 'sine') });
FMon({ name: 'feedback', c: [cn('lfo1', 'FM:engAFeedback', 40)] }, { patch: [FMo(0, 'feedback', 0.3), ...lfoP(1, 1.7, 'triangle')] });
FMon({ name: 'cutoff_pitch', c: [cn('lfo1', 'FM:engACutoff', 40), cn('lfo2', 'FM:pitch', 20), cn('lfo2', 'FM:level', 20)] }, { compareUntil: 2.14, patch: [['layers.FM.oscs.0.filter', { type: 'moog4pole', cutoff: 2000, reso: 25, keytrack: 0, volume: 1, lpgDecay: 0.8 }], ...lfoP(1, 1.5, 'sine'), ...lfoP(2, 4, 'sine')] });
FMon({ name: 'tune', c: [cn('lfo1', 'FM:engAFine', 50), cn('lfo2', 'FM:engASemi', 20), cn('lfo3', 'FM:engAOctave', 30)] }, { patch: [['layers.FM.fmSub', [1, 0]], ...lfoP(1, 2, 'sine'), ...lfoP(2, 0.9, 'triangle'), ...lfoP(3, 1.1, 'sine')] });
// sampler / procedural
msc('smp_pitch_cutoff', [cn('lfo1', 'SMP:pitch', 15), cn('lfo2', 'SMP:osc1Cutoff', 30)], { patch: [...smp(0, 'Warm Pad (synthesized)', [['layers.SMP.oscs.0.filter', { type: 'moog4pole', cutoff: 3000, reso: 25, keytrack: 0, volume: 1, lpgDecay: 0.8 }]]), ...lfoP(1, 3, 'sine'), ...lfoP(2, 1.2, 'triangle')], events: [on(0, SM, 60), off(1.8, SM, 60)] });
msc('pro_gen', [cn('lfo1', 'PRO:genAChaos', 60), cn('lfo2', 'PRO:genATone', 40), cn('lfo3', 'PRO:pitch', 5), cn('lfo2', 'PRO:genADamp', -50)], { patch: [['layers.PRO.oscs.0.enabled', true], ['layers.PRO.oscs.0.model', 'modal'], ...lfoP(1, 1.7, 'sine'), ...lfoP(2, 0.9, 'triangle'), ...lfoP(3, 4, 'sine')], events: [on(0, PRO, 57), off(1.8, PRO, 57)] });
// FX (direct-offset destinations: filter / wavefolder / resonator-free cases that the web applies without accumulating)
msc('fx_filter', [cn('lfo1', 'FX:filterCutoff', 25), cn('lfo2', 'FX:filterResonance', 30)], { dur: 1.6, events: [on(0, WT, 60), off(0.9, WT, 60)], patch: [...wtSaw, ['fxStages', [stg('filter', { filterType: 'moog4pole', cutoff: 1500, resonance: 18, mix: 1 })]], ...lfoP(1, 1.5, 'sine'), ...lfoP(2, 0.7, 'triangle')] });
msc('fx_wavefolder', [cn('lfo1', 'FX:foldDrive', 20), cn('lfo2', 'FX:foldSymmetry', 50), cn('lfo3', 'FX:foldMix', -30)], { dur: 1.6, events: [on(0, WT, 60), off(0.9, WT, 60)], patch: [...wtSaw, ['fxStages', [stg('wavefolder', { drive: 4, symmetry: 0.35, mix: 0.8, mode: 'sine', pwm: 0.5 })]], ...lfoP(1, 1.5, 'sine'), ...lfoP(2, 0.9, 'triangle'), ...lfoP(3, 1.1, 'sine')] });
console.log('wrote', out.length, 'scenarios');

// ---- cross-modulation and vocoder
const XM = (i, on, src, dst, type, amount) => [`xmod.${i}`, { on, src, dst, type, amount }];
const wtfm = ['WT', 'FM'];
const xmEv = [on(0, wtfm, 60), off(1.5, wtfm, 60)];
['RM', 'AM', 'PM'].forEach(t => sc('xmod_' + t.toLowerCase(), { patch: [...fmOn, FMo(1, 'level', 0.5), W(0, 'table', 'sawBright'), XM(0, true, 'WT', 'FM', t, 0.6)], events: xmEv }));
sc('xmod_fm_wt_to_fm', { patch: [...fmOn, FMo(1, 'level', 0.5), XM(0, true, 'WT', 'FM', 'FM', 0.4)], events: xmEv });
sc('xmod_fm_fm_to_wt', { patch: [...fmOn, FMo(1, 'level', 0.5), XM(0, true, 'FM', 'WT', 'FM', 0.3), W(0, 'table', 'sawBright')], events: xmEv });
sc('xmod_chain_two', { patch: [...fmOn, FMo(1, 'level', 0.5), W(0, 'table', 'sawBright'), XM(0, true, 'WT', 'FM', 'RM', 0.5), XM(1, true, 'SMP', 'FM', 'AM', 0.5), ...smp(0, 'Warm Pad (synthesized)', [SP(0, 'loop', true)])], events: [on(0, ['WT', 'FM', 'SMP'], 60), off(1.5, ['WT', 'FM', 'SMP'], 60)] });
sc('xmod_loop', { patch: [...fmOn, FMo(1, 'level', 0.5), W(0, 'table', 'sawBright'), XM(0, true, 'WT', 'FM', 'PM', 0.5), XM(1, true, 'FM', 'WT', 'RM', 0.3)], events: xmEv });
msc('xmod_amount', [cn('lfo1', 'GLOBAL:xmod1', 40)], { patch: [...fmOn, FMo(1, 'level', 0.5), W(0, 'table', 'sawBright'), XM(0, true, 'WT', 'FM', 'RM', 0.5), ...lfoP(1, 1.5, 'sine')], events: [on(0, wtfm, 60), off(1.8, wtfm, 60)] });
const voc = (o) => ['vocoder', Object.assign({ on: true, mode: 'classic', bands: 8, spacing: 'log', mix: 0.8, sibSens: 0, slots: Array(16).fill(true) }, o || {})];
const vEv = [on(0, ['WT', 'SMP'], 60), off(1.5, ['WT', 'SMP'], 60)];
sc('vocoder_log8', { vocoder: true, patch: [voc(), W(0, 'table', 'sawBright'), ...smp(0, 'Warm Pad (synthesized)', [SP(0, 'loop', true)])], events: vEv });
sc('vocoder_lin16', { vocoder: true, patch: [voc({ bands: 16, spacing: 'linear', mix: 0.5 }), W(0, 'table', 'sawBright'), ...smp(0, 'Choir Ahh (synthesized)', [SP(0, 'loop', true)])], events: vEv });
const vsm = (n, o, ex) => sc(n, { vocoder: true, patch: [voc(o), W(0, 'table', 'sawBright'), ...smp(0, 'Warm Pad (synthesized)', [SP(0, 'loop', true)])], events: vEv, ...(ex || {}) });
vsm('vocoder_classic24_q', { bands: 24, q: 8, releaseMs: 40, poles4: true, formantSt: 5, mix: 1, hpf: 120, comp: true, micGainDb: 3 });
vsm('vocoder_formant_lpc', { mode: 'formant', formantSt: -4, mix: 1, comp: false });
vsm('vocoder_pitch', { mode: 'pitch', pitchSpeed: 0, pitchScale: 'major', mix: 1, comp: false });
vsm('vocoder_spectral', { mode: 'spectral', formantSt: 3, releaseMs: 60, mix: 1, comp: false });
vsm('vocoder_drive', { bands: 12, drive: 60, mix: 0.7, comp: true });
sc('vocoder_slots', { vocoder: true, patch: [voc({ slots: [true, false, ...Array(14).fill(false)] }), W(0, 'table', 'sawBright'), ...smp(0, 'Warm Pad (synthesized)', [SP(0, 'loop', true)]), ...smp(1, 'Choir Ahh (synthesized)', [SP(1, 'loop', true)])], events: vEv });

// ---- mic / speaker character FX (+ mix dials), generic FXP / GEN destinations, Note source
[['sm57', 'cab412'], ['u87', 'hifi'], ['r121', 'studio'], ['phone', 'pocket'], ['walkie', 'oldtv'], ['piezo', 'laptop'], ['tincan', 'bucket'], ['carbon', 'mega']].forEach(([mic, spk], i) =>
  sc('micfx_' + mic, { patch: smp(0, 'Warm Pad (synthesized)', [SP(0, 'loop', true), SP(0, 'mic', mic), SP(0, 'spk', spk), SP(0, 'micMix', 0.7), SP(0, 'spkMix', 0.6)]), events: [on(0, SM, 60), off(1.5, SM, 60)] }));
msc('gen_micmix', [cn('lfo1', 'GEN:SMP.micMix', 70), cn('lfo2', 'GEN:SMP.spkMix', 60)], { dur: 2.5, events: [on(0, SM, 60), off(1.8, SM, 60)], patch: [...smp(0, 'Warm Pad (synthesized)', [SP(0, 'loop', true), SP(0, 'mic', 'u87'), SP(0, 'spk', 'cab412'), SP(0, 'micMix', 0.4), SP(0, 'spkMix', 0.3)]), ...lfoP(1, 1.2, 'sine'), ...lfoP(2, 0.8, 'triangle')] });
msc('gen_wt_env_pan', [cn('lfo1', 'GEN:WT.pan', 80), cn('lfo2', 'GEN:WT.attack', 30), cn('lfo3', 'GEN:WT.release', -20), cn('lfo2', 'GEN:WT.sustain', 40)], { dur: 3, events: [on(0, WT, 60), off(0.6, WT, 60), on(0.9, WT, 64), off(1.5, WT, 64), on(1.8, WT, 67), off(2.3, WT, 67)], patch: [...wtSaw, ['layers.WT.env', { a: 0.05, d: 0.2, s: 0.6, r: 0.4 }], ...lfoP(1, 1.1, 'triangle'), ...lfoP(2, 0.7, 'sine'), ...lfoP(3, 0.9, 'sine')] });
msc('gen_wt_width_detune', [cn('lfo1', 'GEN:WT.width', 60), cn('lfo2', 'GEN:WT.detune', 50)], { dur: 3, events: [on(0, WT, 60), off(0.6, WT, 60), on(0.9, WT, 64), off(1.5, WT, 64), on(1.8, WT, 67), off(2.3, WT, 67)], patch: [...wtSaw, W(0, 'unison', 4), W(0, 'detune', 12), W(0, 'width', 0.4), ...lfoP(1, 0.9, 'sine'), ...lfoP(2, 1.3, 'triangle')] });
msc('gen_pro_level_tune', [cn('lfo1', 'GEN:PRO.level', 50), cn('lfo2', 'GEN:PRO.semi', 40), cn('lfo3', 'GEN:PRO.fine', 50), cn('lfo1', 'GEN:PRO.pan', 60)], { dur: 3, events: [on(0, PRO, 60), off(0.6, PRO, 60), on(0.9, PRO, 64), off(1.5, PRO, 64), on(1.8, PRO, 67), off(2.3, PRO, 67)], patch: [['layers.PRO.oscs.0.enabled', true], ['layers.PRO.oscs.0.model', 'pluck'], ...lfoP(1, 0.9, 'sine'), ...lfoP(2, 1.3, 'triangle'), ...lfoP(3, 0.6, 'sine')] });
msc('gen_smp_tune_env', [cn('lfo1', 'GEN:SMP.octave', 35), cn('lfo2', 'GEN:SMP.attack', 40), cn('lfo3', 'GEN:SMP.pan', 70)], { dur: 3, events: [on(0, SM, 60), off(0.6, SM, 60), on(0.9, SM, 64), off(1.5, SM, 64), on(1.8, SM, 67), off(2.3, SM, 67)], patch: [...smp(0, 'Flute (synthesized)', [SP(0, 'loop', true)]), ...lfoP(1, 0.9, 'sine'), ...lfoP(2, 0.7, 'triangle'), ...lfoP(3, 1.1, 'sine')] });
msc('fxp_reverb_delay', [cn('lfo1', 'FXP:reverb:size', 50), cn('lfo2', 'FXP:reverb:decay', 30), cn('lfo3', 'FXP:delay:time', 25)], { dur: 3, events: [on(0, WT, 60), off(0.3, WT, 60), on(1.2, WT, 67), off(1.5, WT, 67)], patch: [...wtSaw, ['fxStages', [stg('reverb', { algo: 'hall', size: 1, decay: 0.7, predelay: 0.02, mod: 0.3, colour: 0.5, mix: 0.4 }), stg('delay', { mode: 'pingpong', time: 0.25, feedback: 0.4, mix: 0.4, sync: false, syncDiv: '1/8' })]], ...lfoP(1, 0.8, 'sine'), ...lfoP(2, 0.6, 'triangle'), ...lfoP(3, 0.9, 'sine')] });
const lfo3 = [...lfoP(1, 1.1, 'sine'), ...lfoP(2, 0.7, 'triangle'), ...lfoP(3, 1.4, 'sine')];
msc('fxp_phaser_flanger_chorus', [cn('lfo1', 'FXP:phaser:depth', 40), cn('lfo2', 'FXP:flanger:depth', 30), cn('lfo3', 'FXP:chorus:mix', 30)], { dur: 2.5, events: [on(0, WT, 60), off(1.4, WT, 60)], patch: [...wtSaw, ['fxStages', [stg('phaser', { rate: 0.5, depth: 0.5, feedback: 0.3, stages: 4, mix: 0.5 }), stg('flanger', { rate: 0.4, depth: 0.5, feedback: 0.3, mix: 0.5 }), stg('chorus', { chorus1On: true, chorus2On: false, mix: 0.5 })]], ...lfo3] });
msc('fxp_sat_filter_glitch', [cn('lfo1', 'FXP:saturation:tone', 30), cn('lfo2', 'FXP:filter:mix', 40), cn('lfo3', 'FXP:glitch:srReduction', 30), cn('lfo1', 'FXP:glitch:mix', 20)], { dur: 2.5, events: [on(0, WT, 60), off(1.4, WT, 60)], patch: [...wtSaw, ['fxStages', [stg('saturation', { type: 'soft', drive: 0.5, tone: 6000, mix: 0.6 }), stg('filter', { filterType: 'moog4pole', cutoff: 1500, resonance: 18, mix: 0.7 }), stg('glitch', { bitDepth: 12, srReduction: 4, stutterOn: false, stutterDivision: 0.25, stutterAmount: 0.2, mix: 0.5, seqOn: false, seqSteps: [true, true, true, true, true, true], seqRate: '1/16', seqIntensity: 0.3, seqTempoDiv: '1' })]], ...lfo3] });
msc('fxp_panning', [cn('lfo1', 'FXP:panning:width', 40), cn('lfo2', 'FXP:panning:mix', 30), cn('lfo3', 'FXP:panning:rate', 20), cn('lfo1', 'FXP:panning:distance', 30)], { dur: 2.5, events: [on(0, WT, 60), off(1.4, WT, 60)], patch: [...wtSaw, ['fxStages', [stg('panning', { width: 1, autoPanOn: true, rate: 0.8, shape: 'triangle', mode: '3dspace', distance: 0.3, mix: 0.7 })]], ...lfo3] });
msc('note_source', [cn('note', 'WT:osc1Cutoff', 40), cn('note', 'WT:pitch', 3), cn('note', 'WT:osc1Scan', 40)], { dur: 3, events: [on(0, WT, 48), off(0.6, WT, 48), on(0.8, WT, 60), off(1.4, WT, 60), on(1.6, WT, 79), off(2.2, WT, 79)], patch: [W(0, 'table', 'digitalMorph'), W(0, 'position', 0.4), wtFilt('moog4pole', 1200, 30)] });

// ---- MPE and wavetable-shape mod source
const mp = (t, st, d1, d2) => ({ t, midi: [st, d1, d2], layers: WT });
msc('mpe_timbre', [cn('mpeTimbre', 'WT:osc1Cutoff', 60)], { settings: { mpe: true }, patch: [...wtSaw, wtFilt()], ticks: true, events: [mp(0, 0x91, 60, 100), mp(0.3, 0xB1, 74, 100), mp(0.9, 0xB1, 74, 20), mp(1.8, 0x81, 60, 0)] });
msc('mpe_pressure', [cn('mpePressure', 'WT:pitch', 20)], { settings: { mpe: true }, patch: [...wtSaw], ticks: true, events: [mp(0, 0x92, 64, 100), mp(0.3, 0xD2, 100, 0), mp(1.2, 0xD2, 10, 0), mp(1.8, 0x82, 64, 0)] });
msc('wt_shape_source', [cn('wtOsc1Shape', 'WT:osc1Cutoff', 70), cn('lfo1', 'WT:osc1Scan', 40)], { patch: [...wtSaw, wtFilt(), ...lfoP(1, 0.8, 'triangle')] });
