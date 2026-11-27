// Golden vectors, set 2: the simpler effects and voice DSP. Usage: node tools/make_golden2.js
// Writes Core/tests/golden2/*.f32 (interleaved float32, nout channels) and cases.txt.
'use strict';
const fs = require('fs'), path = require('path');
const H = require('./worklet_harness');
const outDir = path.join(__dirname, '..', 'Core', 'tests', 'golden2');
fs.mkdirSync(outDir, { recursive: true });
const cases = [];

// Math.random replacement so processors that use it (tape) are reproducible; the C++ test uses the same LCG.
function seedRandom(seed) { let s = seed >>> 0; Math.random = () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; }

function add(id, proc, sr, params, o = {}) {
  const nin = o.nin === undefined ? 1 : o.nin, nout = o.nout || 1;
  const dur = o.dur || 0.5;
  const frames = Math.floor(dur * sr / 128) * 128;
  const noiseUntil = Math.floor((o.burst === undefined ? 0.25 : o.burst) * sr), amp = o.amp || 0.3, seed = 4242;
  const gens = []; for (let c = 0; c < Math.max(1, nin); c++) gens.push(H.noise(amp, seed + c * 7919));
  const input = (i, c) => (i < noiseUntil ? gens[c](i) : 0);
  seedRandom(777);
  const pf = typeof params === 'function' ? (p) => params(p, frames) : params;
  const multi = o.outputs > 1;           // wavetable-sync: one channel from each of two outputs
  const r = H.run(proc, { sampleRate: sr, frames, input, params: pf, inChannels: nin, outChannels: multi ? 1 : nout, outputs: o.outputs || 1, setup: o.setup, ctor: o.ctor });
  const buf = Buffer.alloc(r.frames * nout * 4);
  for (let i = 0; i < r.frames; i++) for (let c = 0; c < nout; c++) buf.writeFloatLE(multi ? r.outputs[c][0][i] : r.out[c][i], (i * nout + c) * 4);
  fs.writeFileSync(path.join(outDir, id + '.f32'), buf);
  const kv = o.kv || Object.entries(params).map(([k, v]) => k + '=' + v).join(' ');
  cases.push(`${id} ${proc} sr=${sr} frames=${r.frames} nin=${nin} nout=${nout} noiseUntil=${noiseUntil} seed=${seed} amp=${amp} ${kv}`);
}

for (const sr of [48000, 44100]) {
  const full = sr === 48000;
  // EQ
  add(`eq_flat_${sr}`, 'eq-processor', sr, { freq1: 120, gain1: 0, freq2: 600, gain2: 0, freq3: 2500, gain3: 0, freq4: 8000, gain4: 0 });
  add(`eq_boost_${sr}`, 'eq-processor', sr, { freq1: 90, gain1: 9, freq2: 400, gain2: -8, freq3: 3000, gain3: 12, freq4: 9000, gain4: -10 });
  // resonator
  add(`res_${sr}`, 'resonator-processor', sr, { freq: 330, resonance: 25, mix: 0.6 });
  // delay modes
  for (let m = 0; m < 4; m++) add(`delay_m${m}_${sr}`, 'delay-processor', sr, { mode: m, time: 0.12, feedback: 0.55, mix: 0.5 }, { nout: 2, dur: 1.0 });
  // chorus
  add(`chorus_both_${sr}`, 'chorus-processor', sr, { chorus1On: 1, chorus2On: 1, mix: 0.6 });
  if (!full) { add(`pan_auto_${sr}`, 'panning-processor', sr, { width: 0.8, autoPanOn: 1, rate: 2, shape: 2, mode: 0, distance: 0 }, { nout: 2 });
    add(`sat_os4_${sr}`, 'saturation-processor', sr, { drive: 0.7, hard: 0, tone: 5000, mix: 0.8, oversample: 2 });
    add(`fold_os2_${sr}`, 'wavefolder-processor', sr, { drive: 5, symmetry: 0.2, mix: 1, oversample: 1, mode: 0, pwm: 0.5 });
    continue; }
  // panning: all shapes, 3D mode
  for (let sh = 0; sh < 3; sh++) add(`pan_shape${sh}_${sr}`, 'panning-processor', sr, { width: 1.2, autoPanOn: 1, rate: 3, shape: sh, mode: 0, distance: 0 }, { nout: 2 });
  add(`pan_width_${sr}`, 'panning-processor', sr, { width: 2, autoPanOn: 0, rate: 1, shape: 0, mode: 0, distance: 0 }, { nout: 2 });
  add(`pan_3d_${sr}`, 'panning-processor', sr, { width: 0, autoPanOn: 0, rate: 1, shape: 0, mode: 1, distance: 0.6 }, { nout: 2 });
  // band pan (stereo in)
  add(`bandpan_${sr}`, 'band-pan-processor', sr, { on: 1, width: 0.8, depth: 0.7, speed: 1.5, phase0: 0.25, decor: 9 }, { nin: 2, nout: 2 });
  add(`bandpan_off_${sr}`, 'band-pan-processor', sr, { on: 0, width: 0.8, depth: 0.7, speed: 1.5, phase0: 0, decor: 12 }, { nin: 2, nout: 2 });
  // saturation: normal/hard/soft x oversample 0/1/2
  [[0, 0], [1, 0], [2, 1], [0, 2]].forEach(([hard, os]) => add(`sat_h${hard}_o${os}_${sr}`, 'saturation-processor', sr, { drive: 0.6, hard, tone: 7000, mix: 0.7, oversample: os }));
  // wavefolder: all modes, oversampled
  [[0, 0], [1, 1], [2, 0], [3, 2]].forEach(([mode, os]) => add(`fold_m${mode}_o${os}_${sr}`, 'wavefolder-processor', sr, { drive: 4, symmetry: 0.15, mix: 1, oversample: os, mode, pwm: 0.35 }));
  // tape (with dust + crackle: random numbers come from the seeded LCG)
  add(`tape_${sr}`, 'tape-processor', sr, { wowRate: 0.9, wowDepth: 0.5, flutterRate: 9, flutterDepth: 0.4, dust: 0.8, crackle: 0.6, mix: 0.9 }, { nout: 2, dur: 1.0 });
}
// ---------------- voice DSP ----------------
function makeTable(nFrames, size, variant) {
  const frames = [];
  for (let f = 0; f < nFrames; f++) {
    const a = new Float32Array(size);
    for (let i = 0; i < size; i++) {
      const t = i / size; let v = 0;
      for (let h = 1; h <= f + 2; h++) v += Math.sin(2 * Math.PI * h * t + variant * h * 0.3) / h;
      a[i] = v * 0.6;
    }
    frames.push(a);
  }
  return frames;
}
function saveTable(name, frames) {
  const nF = frames.length, size = frames[0].length, b = Buffer.alloc(nF * size * 4);
  frames.forEach((f, k) => f.forEach((v, i) => b.writeFloatLE(v, (k * size + i) * 4)));
  fs.writeFileSync(path.join(outDir, name + '.f32'), b);
}
const T0 = makeTable(8, 512, 0), T1 = makeTable(5, 256, 1);
saveTable('table0', T0); saveTable('table1', T1);
const loadT = (msg, fr) => (P) => P.port.onmessage({ data: { type: msg, frames: fr } });

for (const sr of [48000, 44100]) {
  const full = sr === 48000;
  const WTK = 'tab0=8x512 ';
  const wt = (id, par, extra) => add(id, 'wavetable-processor', sr, par, { nin: 0, setup: loadT('loadTable', T0), kv: WTK + Object.entries(par).map(([k, v]) => k + '=' + v).join(' ') + ' ' + (extra || '') });
  wt(`wt_plain_${sr}`, { frequency: 220.5, position: 0.37, pulseWidth: 0.5, phaseOffset: 0, startEnd: 1, segment: 1, pingPong: 0 });
  wt(`wt_warp_${sr}`, { frequency: 331.2, position: 0.9, pulseWidth: 0.3, phaseOffset: 0.2, startEnd: 0.7, segment: 0.5, pingPong: 0 });
  wt(`wt_pingpong_${sr}`, { frequency: 150, position: 1.6, pulseWidth: 0.6, phaseOffset: 0, startEnd: 1, segment: 0.8, pingPong: 1 });
  wt(`wt_clamp_${sr}`, { frequency: 440, position: -0.3, pulseWidth: 0.5, phaseOffset: 0.5, startEnd: 0.25, segment: 1, pingPong: 0 });
  if (!full) continue;
  // position sweeps from block to block (k-rate steps), plain and ping-pong
  add(`wt_sweep_${sr}`, 'wavetable-processor', sr, (p, fr) => ({ frequency: 261.6, position: p / fr * 1.4 - 0.2, pulseWidth: 0.5, phaseOffset: 0, startEnd: 1, segment: 1, pingPong: 0 }), { nin: 0, setup: loadT('loadTable', T0), kv: WTK + 'freq=261.6 sweep=1 pp=0' });
  add(`wt_sweep_pp_${sr}`, 'wavetable-processor', sr, (p, fr) => ({ frequency: 261.6, position: p / fr * 5 - 1, pulseWidth: 0.4, phaseOffset: 0, startEnd: 1, segment: 0.6, pingPong: 1 }), { nin: 0, setup: loadT('loadTable', T0), kv: WTK + 'freq=261.6 sweep=2 pp=1' });
  // hard sync pair, different table sizes
  const sy = (id, f0, f1, pos0, pos1) => add(id, 'wavetable-sync-processor', sr, { frequency0: f0, frequency1: f1, position0: pos0, position1: pos1, pulseWidth0: 0.5, pulseWidth1: 0.35, phaseOffset0: 0, phaseOffset1: 0.1, startEnd0: 1, startEnd1: 0.8, segment0: 1, segment1: 0.7, pingPong0: 0, pingPong1: 1 },
    { nin: 0, nout: 2, outputs: 2, setup: (P) => { loadT('loadTable0', T0)(P); loadT('loadTable1', T1)(P); } });
  sy(`wtsync_a_${sr}`, 110, 387.5, 0.3, 1.4);
  sy(`wtsync_b_${sr}`, 200.3, 201.1, 0.8, 0.2);
  // multi filter, all 13 types, white noise throughout, cutoff sweeping up two octaves between blocks
  for (let t = 0; t <= 12; t++) add(`mf_t${t}_${sr}`, 'multi-filter-processor', sr, (p, fr) => ({ cutoff: 300 * Math.pow(2, 3 * p / fr), resonance: t === 5 ? 90 : 70, filterType: t }), { burst: 0.5, kv: `type=${t} reso=${t === 5 ? 90 : 70} base=300` });
  add(`mf_t0_hires_${sr}`, 'multi-filter-processor', sr, { cutoff: 1500, resonance: 125, filterType: 0 }, { burst: 0.5 });
  // FM
  const fmk = (algo, fb, ratios, levels, waves, cents) => { const o = { baseFreq: 220, feedback0: fb, algorithm: algo }; for (let i = 0; i < 4; i++) { o['ratio' + i] = ratios[i]; o['level' + i] = levels[i]; o['wave' + i] = waves[i]; o['pitchCents' + i] = cents[i]; } return o; };
  [0, 1, 2, 3, 4, 5, 7].forEach((a) => add(`fm_algo${a}_${sr}`, 'fm-processor', sr, fmk(a, a === 2 ? 0.4 : 0, [1, 2, 3.01, 0.5], [0.9, 0.7, 0.5, 0.8], [0, 1, 2, 0], [0, 5, -7, 12]), { nin: 0 }));
  // glitch
  const gl = (id, par, o2) => add(id, 'glitch-processor', sr, par, Object.assign({ burst: 1.0, dur: 1.0 }, o2));
  // spectral FX (stereo out): pass-through, then everything at once with freeze engaging part-way
  add(`spec_pass_${sr}`, 'spectralfx-processor', sr, { freeze: 0, blur: 0, shift: 0, threshold: 0, spread: 0, mix: 1 }, { nout: 2, burst: 0.5, dur: 0.5 });
  add(`spec_all_${sr}`, 'spectralfx-processor', sr, (p, fr) => ({ freeze: p >= fr * 0.3 ? 1 : 0, blur: 0.5, shift: 12, threshold: 0.05, spread: 0.7, mix: 0.8 }), { nout: 2, burst: 0.5, dur: 0.5, kv: 'freezeAt=0.3 blur=0.5 shift=12 threshold=0.05 spread=0.7 mix=0.8' });
  add(`spec_shiftneg_${sr}`, 'spectralfx-processor', sr, { freeze: 0, blur: 0.2, shift: -30, threshold: 0, spread: 0.3, mix: 1 }, { nout: 2, burst: 0.5, dur: 0.5 });
  // granular (Math.random is seeded, so the grain positions are reproducible)
  add(`gran_a_${sr}`, 'granular-processor', sr, { grainSize: 60, density: 40, pitch: 7, spread: 0.8, panCenter: 0.2, envShape: 0, followerAmount: 0.5, mix: 0.9 }, { nout: 2, burst: 0.5, dur: 1.0 });
  add(`gran_env_${sr}`, 'granular-processor', sr, (p, fr) => ({ grainSize: 30, density: 80, pitch: -5, spread: 1, panCenter: -0.3, envShape: Math.floor(p / fr * 4), followerAmount: 0, mix: 1 }), { nout: 2, burst: 0.5, dur: 1.0, kv: 'sweepEnv=1 grainSize=30 density=80 pitch=-5 spread=1 panCenter=-0.3 followerAmount=0 mix=1' });
  // procedural: each model, a model switch, and a custom seed
  [0, 1, 2, 3].forEach((m) => add(`proc_m${m}_${sr}`, 'procedural-processor', sr, { frequency: 196, model: m, bright: 0.6, damp: 0.4, body: m === 1 ? 0.55 : 0.3, chaos: 0.7 }, { nin: 1, dur: 1.0 }));
  add(`proc_switch_${sr}`, 'procedural-processor', sr, (p, fr) => ({ frequency: 330, model: Math.floor(p / fr * 4), bright: 0.4, damp: 0.6, body: 0.8, chaos: 0.2 }), { nin: 1, dur: 1.0, kv: 'switch=1 frequency=330 bright=0.4 damp=0.6 body=0.8 chaos=0.2' });
  // the newer procedural controls: bow model (no stiffness: the bow friction loop is chaotic and amplifies 1-ulp libm differences between V8 and glibc), Stiffness (pluck), Strike material (modal), Bore (breath)
  add(`proc_bow_${sr}`, 'procedural-processor', sr, { frequency: 147, model: 4, bright: 0.55, damp: 0.35, body: 0.6, chaos: 0.4 }, { nin: 1, dur: 1.0 });
  add(`proc_pluck_st_${sr}`, 'procedural-processor', sr, { frequency: 220, model: 0, bright: 0.6, damp: 0.4, body: 0.3, chaos: 0.3, stiffness: 0.8, excite: 1 }, { nin: 1, dur: 1.0 });
  add(`proc_modal_ex_${sr}`, 'procedural-processor', sr, { frequency: 262, model: 1, bright: 0.6, damp: 0.4, body: 0.55, chaos: 0.3, excite: 3 }, { nin: 1, dur: 1.0 });
  add(`proc_breath_bore_${sr}`, 'procedural-processor', sr, { frequency: 196, model: 2, bright: 0.6, damp: 0.4, body: 0.3, chaos: 0.5, bore: 0.7 }, { nin: 1, dur: 1.0 });
  add(`proc_seed_${sr}`, 'procedural-processor', sr, { frequency: 110, model: 0, bright: 0.9, damp: 0.1, body: 0.9, chaos: 0 }, { nin: 1, dur: 1.0, ctor: { processorOptions: { seed: 99 } }, kv: 'frequency=110 model=0 bright=0.9 damp=0.1 body=0.9 chaos=0 pseed=99' });
  // original reverb: every algorithm (+ idle), shimmer interval, spring with a hit and a scrape, reverse over a longer run
  [0, 1, 2, 3, 4, 5, 9].forEach((a) => add(`lrv_a${a}_${sr}`, 'reverb-processor', sr, { algo: a, decay: 0.6, size: 0.7, predelay: 0.03, mix: 0.5, shimA: 7, shimB: -5 }, { dur: a === 5 ? 1.6 : 1.0,
    setup: a === 1 ? (P) => { P.port.onmessage({ data: { type: 'tap', s: 0.8 } }); P.port.onmessage({ data: { type: 'stroke', v: 0.6 } }); } : undefined,
    kv: `algo=${a} decay=0.6 size=0.7 predelay=0.03 mix=0.5 shimA=7 shimB=-5` + (a === 1 ? ' hit=1' : '') }));
  gl(`gl_crush_${sr}`, { bitDepth: 5, srReduction: 7, stutterOn: 0, stutterIntervalSamples: 11025, stutterAmount: 0.5, modeA: 0, modeB: 0, modeC: 0 });
  gl(`gl_stutter_${sr}`, { bitDepth: 16, srReduction: 1, stutterOn: 1, stutterIntervalSamples: 3000, stutterAmount: 0.7, modeA: 0, modeB: 0, modeC: 0 });
  const modes = (id, a, b, c) => add(id, 'glitch-processor', sr, (p, fr) => ({ bitDepth: 12, srReduction: 1, stutterOn: 0, stutterIntervalSamples: 4000, stutterAmount: 0.5, modeA: p >= fr / 2 ? a : 0, modeB: p >= fr / 2 ? b : 0, modeC: p >= fr / 2 ? c : 0 }),
    { burst: 1.0, dur: 1.0, kv: `modeA=${a} modeB=${b} modeC=${c} switchAt=0.5 bit=12` });
  modes(`gl_reverse_${sr}`, 3, 0, 0); modes(`gl_gate_${sr}`, 4, 0, 0); modes(`gl_pitch_${sr}`, 5, 0, 0); modes(`gl_stop_${sr}`, 6, 0, 0);
  modes(`gl_revpitch_${sr}`, 3, 5, 0); modes(`gl_stutterforce_${sr}`, 1, 3, 0); modes(`gl_crushforce_${sr}`, 2, 6, 0);
}
fs.writeFileSync(path.join(outDir, 'cases.txt'), cases.join('\n') + '\n');
console.log('wrote ' + cases.length + ' golden cases to ' + outDir);
