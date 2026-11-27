// Generates reference ("golden") output from the JavaScript worklets for the C++ tests.
// Usage: node tools/make_golden.js   (writes Core/tests/golden/*.f32 and cases.txt)
'use strict';
const fs = require('fs'), path = require('path');
const H = require('./worklet_harness');
const outDir = path.join(__dirname, '..', 'Core', 'tests', 'golden');
fs.mkdirSync(outDir, { recursive: true });
const cases = [];
const SR = [48000, 44100];
const FULL = (sr) => sr === 48000; // the 44.1 kHz set is a smaller subset (keeps the repo light)

function add(id, proc, sr, params, extra) {
  const frames = sr, noiseUntil = Math.floor(0.25 * sr), seed = 12345, amp = 0.3;
  const gen = H.noise(amp, seed);
  const input = (i) => (i < noiseUntil ? gen(i) : 0);   // burst of noise then silence, so tails are tested too
  // the generator is stateful, so call order matters: harness calls input(i) once per frame in order
  const r = H.run(proc, { sampleRate: sr, frames, input, params: params, outChannels: 2 });
  const f = id + '.f32';
  const buf = Buffer.alloc(r.frames * 2 * 4);
  for (let i = 0; i < r.frames; i++) { buf.writeFloatLE(r.out[0][i], (i * 2) * 4); buf.writeFloatLE(r.out[1][i], (i * 2 + 1) * 4); }
  fs.writeFileSync(path.join(outDir, f), buf);
  const kv = Object.entries(params).map(([k, v]) => k + '=' + v).join(' ');
  cases.push(`${id} ${proc} sr=${sr} frames=${r.frames} noiseUntil=${noiseUntil} seed=${seed} amp=${amp} ${kv}`);
}

for (const sr of SR) {
  // phaser / flanger
  add(`phaser_default_${sr}`, 'phaser-flanger-processor', sr, { mode: 0, rate: 0.4, depth: 0.85, feedback: 0.6, mix: 0.5, stages: 8 });
  if (!FULL(sr)) { add(`rv_hall_${sr}`, 'adv-reverb-processor', sr, { algo: 0, decay: 0.5, size: 0.5, predelay: 0.02, mix: 0.6, mod: 0.4, colour: 0.5 }); add(`rv_flow_${sr}`, 'adv-reverb-processor', sr, { algo: 3, decay: 0.5, size: 0.5, predelay: 0.02, mix: 0.6, mod: 0.4, colour: 0.5 }); add(`rv_moth_${sr}`, 'adv-reverb-processor', sr, { algo: 4, decay: 0.5, size: 0.5, predelay: 0.02, mix: 0.6, mod: 0.4, colour: 0.5 }); add(`flanger_${sr}`, 'phaser-flanger-processor', sr, { mode: 1, rate: 0.5, depth: 0.7, feedback: 0.3, mix: 0.5, stages: 8 }); continue; }
  add(`phaser_4stage_fb_${sr}`, 'phaser-flanger-processor', sr, { mode: 0, rate: 1.3, depth: 1, feedback: 0.9, mix: 0.5, stages: 4 });
  add(`phaser_12stage_${sr}`, 'phaser-flanger-processor', sr, { mode: 0, rate: 0.25, depth: 0.6, feedback: 0.2, mix: 0.7, stages: 12 });
  add(`flanger_${sr}`, 'phaser-flanger-processor', sr, { mode: 1, rate: 0.5, depth: 0.7, feedback: 0.3, mix: 0.5, stages: 8 });
  // advanced reverb: all six algorithms, plus a few awkward settings
  const names = ['hall', 'plateambient', 'kristal', 'flow', 'mothership', 'formant'];
  names.forEach((n, a) => add(`rv_${n}_${sr}`, 'adv-reverb-processor', sr, { algo: a, decay: 0.5, size: 0.5, predelay: 0.02, mix: 0.6, mod: 0.4, colour: 0.5 }));
  add(`rv_hall_long_${sr}`, 'adv-reverb-processor', sr, { algo: 0, decay: 0.9, size: 1.5, predelay: 0.05, mix: 1, mod: 0.8, colour: 0.2 });
  add(`rv_kristal_small_${sr}`, 'adv-reverb-processor', sr, { algo: 2, decay: 0.2, size: 0.1, predelay: 0, mix: 1, mod: 0, colour: 1 });
  add(`rv_flow_big_${sr}`, 'adv-reverb-processor', sr, { algo: 3, decay: 0.8, size: 2, predelay: 0.01, mix: 1, mod: 1, colour: 0.7 });
  add(`rv_moth_shift_${sr}`, 'adv-reverb-processor', sr, { algo: 4, decay: 0.7, size: 0.8, predelay: 0.02, mix: 1, mod: 0.3, colour: 0.8 });
  add(`rv_formant_vowel_${sr}`, 'adv-reverb-processor', sr, { algo: 5, decay: 0.6, size: 0.6, predelay: 0.02, mix: 1, mod: 0.7, colour: 0.1 });
}
fs.writeFileSync(path.join(outDir, 'cases.txt'), cases.join('\n') + '\n');
console.log('wrote ' + cases.length + ' golden cases to ' + outDir);
