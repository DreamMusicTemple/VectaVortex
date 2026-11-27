// Golden vectors for the SID processor: node tools/make_golden_sid.js -> Core/tests/golden_sid/*.f32 + cases.txt
'use strict';
const fs = require('fs'), path = require('path'); const H = require('./worklet_harness');
const out = path.join(__dirname, '..', 'Core', 'tests', 'golden_sid'); fs.mkdirSync(out, { recursive: true });
const sr = 48000, frames = 128 * 375; // 1 s
// case: per voice [freq, wave, pw, ring, sync, atk, dec, sus, rel]; gate drops at frame gateOff
const cases = [
  { id: 'tri_saw_pulse', gateOff: 30000, v: [[220,1,0.5,0,0,0.01,0.1,0.7,0.2],[330,2,0.5,0,0,0.02,0.15,0.5,0.3],[110,4,0.25,0,0,0.005,0.2,0.8,0.1]] },
  { id: 'combined_ring_sync', gateOff: 36000, v: [[220,3,0.5,1,0,0.01,0.1,0.7,0.2],[261.63,6,0.4,1,1,0.01,0.1,0.7,0.2],[311.13,5,0.6,0,1,0.01,0.1,0.7,0.2]] },
  { id: 'noise', gateOff: 24000, v: [[440,8,0.5,0,0,0.001,0.05,0.3,0.05],[220,9,0.5,0,0,0.01,0.1,0.7,0.5],[100,8,0.5,0,0,0.5,0.5,1,1]] },
];
const lines = [];
for (const c of cases) {
  const par = (p) => { const o = {}; c.v.forEach((a, v) => { Object.assign(o, { ['freq'+v]: a[0], ['wave'+v]: a[1], ['pw'+v]: a[2], ['ring'+v]: a[3], ['sync'+v]: a[4], ['atk'+v]: a[5], ['dec'+v]: a[6], ['sus'+v]: a[7], ['rel'+v]: a[8], ['gate'+v]: p < c.gateOff ? 1 : 0 }); }); return o; };
  const r = H.run('sid-processor', { sampleRate: sr, frames, params: par, inChannels: 0, outChannels: 1, outputs: 3 });
  const buf = Buffer.alloc(r.frames * 3 * 4);
  for (let i = 0; i < r.frames; i++) for (let v = 0; v < 3; v++) buf.writeFloatLE(r.outputs[v][0][i], (i * 3 + v) * 4);
  fs.writeFileSync(path.join(out, c.id + '.f32'), buf);
  lines.push([c.id, r.frames, c.gateOff, ...c.v.flat()].join(' '));
}
fs.writeFileSync(path.join(out, 'cases.txt'), lines.join('\n') + '\n'); console.log('wrote', cases.length);
