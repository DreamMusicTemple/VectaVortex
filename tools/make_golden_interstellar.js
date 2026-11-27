// Golden vectors for the Interstellar worklet: runs the web processor offline and stores input + output so the C++ engine can be compared.
// Usage: node tools/make_golden_interstellar.js   (writes Core/tests/golden_is/*.json, *.f32, cases.txt)
'use strict';
const fs = require('fs'), path = require('path');
const H = require('./worklet_harness');
const outDir = path.join(__dirname, '..', 'Core', 'tests', 'golden_is');
fs.mkdirSync(outDir, { recursive: true });
const cases = [];
function input(sr, kind) {
  const n = H.noise(0.25, 99);
  return (i) => {
    const t = i / sr;
    if (kind === 'chord') return Math.fround(0.28 * (Math.sin(2 * Math.PI * 220 * t) + Math.sin(2 * Math.PI * 277.18 * t) + Math.sin(2 * Math.PI * 329.63 * t)) / 3 * (1 + 0.3 * Math.sin(2 * Math.PI * 0.5 * t)) + 0.01 * n(i));
    return Math.fround(n(i) * (i < sr * 2 ? 1 : 0.2));
  };
}
function add(id, sr, secs, params, bpm, kind, seed) {
  const frames = Math.floor(secs * sr / 128) * 128, gen = input(sr, kind || 'chord');
  const x = new Float32Array(frames); for (let i = 0; i < frames; i++) x[i] = gen(i);   // generated once (the noise source is stateful)
  const inp = (i) => x[i];
  const r = H.run('interstellar-processor', { sampleRate: sr, frames, input: (i) => inp(i), params: {}, inChannels: 1, outChannels: 2, ctor: { processorOptions: { seed: seed || 1 } },
    setup: (P) => P.port.onmessage({ data: { p: params, bpm: bpm || 120 } }) });
  const buf = Buffer.alloc(frames * 3 * 4);
  for (let i = 0; i < frames; i++) { buf.writeFloatLE(x[i], (i * 3) * 4); buf.writeFloatLE(r.out[0][i], (i * 3 + 1) * 4); buf.writeFloatLE(r.out[1][i], (i * 3 + 2) * 4); }
  fs.writeFileSync(path.join(outDir, id + '.f32'), buf);
  fs.writeFileSync(path.join(outDir, id + '.json'), JSON.stringify({ params, bpm: bpm || 120, sr, frames, seed: seed || 1 }));
  cases.push(id);
}
const P = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'Assets', 'Interstellar', 'presets.json'), 'utf8'));
const pick = (name) => P.presets.find((p) => p.name === name).params;
add('defaults_eco_48000', 48000, 6, Object.assign({}, P.defaults, { quality: 'eco' }));
add('defaults_normal_48000', 48000, 6, P.defaults);
add('lush_deepfield_48000', 48000, 6, pick('Deep Field'));
add('aurora_choir_48000', 48000, 8, Object.assign({}, pick('Aurora Choir'), { quality: 'eco' }));
add('glass_cathedral_44100', 44100, 6, Object.assign({}, pick('Glass Cathedral'), { quality: 'eco' }));
add('orbit_loop_sync_48000', 48000, 6, Object.assign({}, pick('Orbit Loop'), { quality: 'eco' }), 128);
add('capture_dust_48000', 48000, 8, Object.assign({}, pick('Cathedral of Dust'), { quality: 'eco', captureLen: 3 }));
add('freeze_noise_48000', 48000, 6, Object.assign({}, P.defaults, { quality: 'eco', freeze: 1, revFreeze: 1, mix: 0.7 }), 120, 'noise');
add('free_pitch_noise_48000', 48000, 6, Object.assign({}, P.defaults, { quality: 'eco', snap: 'free', density: 1, evolve: 1, drift: 1, segOrder: 'random', segments: 8, segTime: 1 }), 120, 'noise', 7);
fs.writeFileSync(path.join(outDir, 'cases.txt'), cases.join('\n') + '\n');
console.log('wrote', cases.length, 'cases');
