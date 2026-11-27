// Loads the AudioWorklet processors straight out of the web app (full-app.html) and runs them offline in Node,
// so C++ ports can be checked against the real JavaScript DSP ("golden" vectors).
//
//   const H = require('./worklet_harness');
//   const out = H.run('phaser-flanger-processor', { sampleRate: 48000, params: {...}, frames: 48000, input: H.noise(0.3, 1), outChannels: 2 });
'use strict';
const fs = require('fs'), path = require('path');

function loadWorkletSource(htmlPath) {
  const html = fs.readFileSync(htmlPath, 'utf8');
  const start = html.indexOf('const WORKLET_CODE = `');
  if (start < 0) throw new Error('WORKLET_CODE not found in ' + htmlPath);
  const a = start + 'const WORKLET_CODE = `'.length;
  const b = html.indexOf('`;', a);
  // evaluate it as a template literal, exactly as the browser does (handles escape sequences)
  return (0, eval)('`' + html.slice(a, b) + '`');
}

const cache = {};
function classes(htmlPath, sampleRate) {
  const key = htmlPath + '@' + sampleRate;
  if (cache[key]) return cache[key];
  const src = loadWorkletSource(htmlPath);
  const registry = {};
  const g = globalThis;
  g.sampleRate = sampleRate; g.currentTime = 0; g.currentFrame = 0;
  g.AudioWorkletProcessor = class { constructor() { this.port = { onmessage: null, postMessage() {} }; } };
  g.registerProcessor = (n, c) => { registry[n] = c; };
  (0, eval)(src);
  cache[key] = registry;
  return registry;
}

// deterministic white noise (same generator the C++ tests use)
function noise(amp, seed) {
  let s = (seed >>> 0) || 1;
  return (i) => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return (s / 4294967296 * 2 - 1) * amp; };
}
function impulse(i) { return i === 0 ? 1 : 0; }

function run(name, o) {
  const sr = o.sampleRate || 48000, block = o.block || 128;
  const reg = classes(o.html || path.join(__dirname, '..', 'Web', 'full-app.html'), sr);
  globalThis.sampleRate = sr;   // processors read the global at run time; the registry is cached per rate
  const P = new reg[name](o.ctor || {});
  if (o.setup) o.setup(P);   // e.g. deliver a wavetable through port.onmessage
  const frames = Math.floor((o.frames || sr) / block) * block;
  const nIn = o.inChannels || 1, nOut = o.outChannels || 1;
  const input = o.input || (() => 0);
  const nOutputs = o.outputs || 1;  // some processors (wavetable-sync) have more than one output
  const outsAll = []; for (let k = 0; k < nOutputs; k++) { const a = []; for (let c = 0; c < nOut; c++) a.push(new Float32Array(frames)); outsAll.push(a); }
  const outs = outsAll[0];
  const ins = []; for (let c = 0; c < nIn; c++) ins.push(new Float32Array(frames));
  const ib = [], obAll = []; for (let c = 0; c < nIn; c++) ib.push(new Float32Array(block));
  for (let k = 0; k < nOutputs; k++) { const a = []; for (let c = 0; c < nOut; c++) a.push(new Float32Array(block)); obAll.push(a); }
  const ob = obAll[0];
  for (let p = 0; p < frames; p += block) {
    const par = {}; const pv = typeof o.params === 'function' ? o.params(p) : o.params;
    for (const d of (reg[name].parameterDescriptors || [])) par[d.name] = [d.defaultValue];   // parameters a case does not set take the processor's own default, as in the browser
    for (const k of Object.keys(pv)) par[k] = [pv[k]];
    for (let c = 0; c < nIn; c++) for (let i = 0; i < block; i++) { const v = input(p + i, c); ib[c][i] = v; ins[c][p + i] = ib[c][i]; }
    P.process([ib], obAll, par);
    for (let k = 0; k < nOutputs; k++) for (let c = 0; c < nOut; c++) outsAll[k][c].set(obAll[k][c], p);
  }
  return { out: outs, outputs: outsAll, in: ins, frames, sampleRate: sr };
}
module.exports = { run, noise, impulse, classes };
