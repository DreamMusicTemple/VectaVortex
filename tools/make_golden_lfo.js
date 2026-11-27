// Golden values for the LFO shape functions (Morph, Morph 2, Organic, Stroll, stairs, smoothing): node tools/make_golden_lfo.js
const fs = require('fs'), path = require('path');
const html = fs.readFileSync(path.join(__dirname, '..', 'Web/full-app.html'), 'utf8');
const a = html.indexOf('function lfoHashNoise'), b = html.indexOf('function sourceValue');
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const lib = new Function('clamp', 'evalCurveY', html.slice(a, b) + '\nreturn { lfoWaveValue };')(clamp, () => 0.5);
const waves = ['sine','triangle','pulse','sawUp','sawDown','sh','noise','staircase','stairsDown','stroll','organic','morph','morph2'];
const lines = [];
for (const w of waves) for (const morph of [0, 0.3, 0.5, 1]) for (const sm of [0, 0.4, 1]) for (const bip of [true, false]) {
  if (!/morph/.test(w) && morph !== 0.5) continue;
  for (let i = 0; i < 64; i++) { const ph = i / 64 + 0.0123; const l = { waveform: w, phaseOffset: 0.07, bipolar: bip, smoothing: sm, morph };
    lines.push([w, morph, sm, bip ? 1 : 0, ph, lib.lfoWaveValue(l, ph, sm, morph)].join(' ')); } }
fs.writeFileSync(path.join(__dirname, '..', 'Core/tests/golden_lfo.txt'), lines.join('\n') + '\n'); console.log(lines.length, 'values');
