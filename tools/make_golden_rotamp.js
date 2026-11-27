// Golden vectors for rotary_amp.hpp: runs the REAL rotary-processor / amp-processor / saturation-processor (tube) out of the web app and writes
// Core/tests/data/rotary_amp_golden.json (every 16th sample of each channel). Usage: node tools/make_golden_rotamp.js [path-to-html]
'use strict';
const H = require('./worklet_harness'), fs = require('fs'), path = require('path');
const html = process.argv[2] || path.join(__dirname, '..', 'Web', 'full-app.html'), sr = 48000, frames = 36000, KEEP = 16;
const mixIn = (seed) => { const n = H.noise(0.25, seed); return (i) => n(i) + 0.2 * Math.sin(2 * Math.PI * 150 * i / sr); };
const cases = [
  { proc: 'rotary-processor', name: 'rot slow', p: { speed: 0, drive: 0.3 } }, { proc: 'rotary-processor', name: 'rot fast drive', p: { speed: 1, drive: 0.5 } },
  { proc: 'rotary-processor', name: 'rot stop', p: { speed: 2, drive: 0 } }, { proc: 'rotary-processor', name: 'rot extremes', p: { speed: 1, drive: 0, depth: 1, width: 1, balance: 0.8, mix: 0.7, level: 3, accel: 2.5 } },
  { proc: 'saturation-processor', name: 'sat tube', p: { drive: 0.6, hard: 3, mix: 1, tone: 8000, oversample: 1 }, ch: 1 }, { proc: 'saturation-processor', name: 'sat tube mix', p: { drive: 1, hard: 3, mix: 0.5, tone: 14000, oversample: 0 }, ch: 1 },
];
for (let m = 0; m < 4; m++) { cases.push({ proc: 'amp-processor', name: 'amp ' + m + ' full', p: { model: m, gain: 0.7, bass: 0.7, mid: 0.3, treble: 0.6, presence: 0.7, master: 0.6, chorus: 0.8, cab: 1 } });
  cases.push({ proc: 'amp-processor', name: 'amp ' + m + ' nocab', p: { model: m, gain: 0.2, cab: 0, mix: 0.6, level: -3 } }); }
const out = [];
cases.forEach((c, k) => { const r = H.run(c.proc, { html, sampleRate: sr, frames, params: c.p, input: mixIn(100 + k), outChannels: c.ch || 2 });
  const ch = r.out.map(a => { const v = []; for (let i = 0; i < a.length; i += KEEP) v.push(Math.fround(a[i])); return v; }); out.push({ proc: c.proc, name: c.name, params: c.p, seed: 100 + k, frames, keep: KEEP, ch }); });
fs.writeFileSync(path.join(__dirname, '..', 'Core', 'tests', 'data', 'rotary_amp_golden.json'), JSON.stringify(out));
console.log('wrote', out.length, 'cases');
