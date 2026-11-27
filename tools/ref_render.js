// Reference renderer: plays a scenario through the REAL web app (Chromium, OfflineAudioContext) and writes the audio,
// so the C++ engine can be compared against what the app actually produces.
//
//   node tools/ref_render.js <scenario.json> [more.json ...]
//
// scenario = { name, sr, dur, preset: <snapshotState-like JSON or omitted for the default init state>,
//              settings: {masterVolume, masterTone, wtFmMix, maxPolyphony, legato}, events: [
//                { t: seconds (rounded down to a 128-frame boundary), on|off: true, layers: ["WT"], note: 60, vel: 0.8, plocks: {}, legato: false } ] }
// Output (next to the scenario): <name>.ref.f32 (L then R, float32) and <name>.resolved.json (the exact state the app was in).
const { chromium } = require(process.env.PLAYWRIGHT_PATH || '/opt/npm-tools/node_modules/playwright');
const fs = require('fs'), path = require('path');
const root = path.resolve(__dirname, '..');

async function renderOne(browser, file) {
  const sc = JSON.parse(fs.readFileSync(file, 'utf8'));
  const sr = sc.sr || 48000, dur = sc.dur || 3;
  const page = await browser.newPage();
  await page.route(/^https?:/, r => r.abort());
  page.on('pageerror', e => console.log('  pageerror:', e.message));
  await page.addInitScript(({ SR, DUR }) => {
    class OAC extends OfflineAudioContext {
      constructor() { super({ numberOfChannels: 2, length: Math.ceil(SR * DUR / 128) * 128, sampleRate: SR }); }
      startRendering() { this._rendering = true; return super.startRendering(); }
      // While a scenario event is being applied the context must stay suspended (noteOn() itself calls ctx.resume()).
      resume() { return (this._rendering && !this._hold) ? super.resume() : Promise.resolve(); }
      realResume() { this._hold = false; return super.resume(); }
    }
    window.AudioContext = OAC; window.webkitAudioContext = OAC;
    // The app disconnects finished voices from wall-clock setTimeout()s; during an offline render those would cut tails at random
    // audio times. Cleanup timers (>= 20 ms) are dropped so the reference is deterministic.
    const _st = window.setTimeout.bind(window); window.setTimeout = (fn, ms, ...a) => (ms !== undefined && ms >= 20) ? 0 : _st(fn, ms, ...a);
    // The modulation scheduler is a 100 ms setInterval in wall-clock time; during an offline render its ticks would land at random
    // audio times. Its callbacks are captured instead and (only when a scenario asks for "ticks") run at exact 100 ms audio intervals.
    window.__ticks = []; const _si = window.setInterval.bind(window);
    window.setInterval = (fn, ms, ...a) => { if (ms === 100) { window.__ticks.push(fn); return 0; } return _si(fn, ms, ...a); };
    window.__pn = 0; performance.now = () => window.__pn;   // the scheduler's dt comes from performance.now(): drive it from audio time
    window.__seed = 777;
    Math.random = () => { window.__seed = (Math.imul(window.__seed, 1664525) + 1013904223) >>> 0; return window.__seed / 4294967296; };
  }, { SR: sr, DUR: dur });
  await page.goto('file://' + path.join(root, 'Web/full-app.html'), { waitUntil: 'domcontentloaded' });
  await page.waitForTimeout(700);
  page.on('console', m => { if (process.env.REF_TRACE) console.log(m.text()); });
  const res = await page.evaluate(async (sc) => {
    const D = window.__presetDebug; const SR = sc.sr || 48000;
    await D.initAudio();
    const ctx = D.getCtx();
    const st = D.state();
    let preset = sc.preset;
    if (sc.patch && sc.patch.length) {
      preset = preset || await D.snapshotState();
      for (const [pth, val] of sc.patch) {
        const ks = pth.split('.'); let o = preset;
        for (let i = 0; i < ks.length - 1; i++) { const k = /^\d+$/.test(ks[i]) ? +ks[i] : ks[i]; if (o[k] === undefined || o[k] === null) o[k] = /^\d+$/.test(ks[i + 1]) ? [] : {}; o = o[k]; }
        const lk = /^\d+$/.test(ks[ks.length - 1]) ? +ks[ks.length - 1] : ks[ks.length - 1];
        if (val === '__push__') o[lk] = o[lk] || []; else o[lk] = val;
      }
    }
    if (preset) await D.loadPresetData(preset);
    if (sc.vocoder) {   // the web app only builds the vocoder when a vocoder control is touched, not when a preset loads
      const el = document.getElementById('vocOn'); el.checked = true; el.dispatchEvent(new Event('change'));
    }
    const S = sc.settings || {};
    if (S.masterVolume !== undefined) st.masterVolume = S.masterVolume;
    if (S.masterTone !== undefined) { st.masterTone = S.masterTone; D.getMasterToneFilter().frequency.setTargetAtTime(20000 * Math.pow(400 / 20000, Math.max(0, Math.min(1, S.masterTone))), ctx.currentTime, 0.01); }
    if (S.applyEq) { const nodes = D.getLayerEQNodes(); for (const k of ['WT', 'FM', 'SMP', 'PRO']) { const q = nodes[k] && nodes[k].node; if (!q) continue; ['1', '2', '3', '4'].forEach(n => { q.parameters.get('freq' + n).setTargetAtTime(st.layers[k].eq['band' + n].freq, ctx.currentTime, 0.01); q.parameters.get('gain' + n).setTargetAtTime(st.layers[k].eq['band' + n].gain, ctx.currentTime, 0.01); }); } }
    if (S.wtFmMix !== undefined) { st.wtFmMix = S.wtFmMix; D.applyWtFmMix(); }
    if (S.sampleMix !== undefined) { st.sampleMix = { on: true, pos: S.sampleMix }; }
    if (S.modwheel !== undefined) st.modwheel = S.modwheel;
    if (S.mpe !== undefined) st.mpeEnabled = S.mpe;
    if (S.maxPolyphony !== undefined) st.maxPolyphony = S.maxPolyphony;
    if (S.legato !== undefined) st.legatoMode = S.legato;
    if (S.masterVolume !== undefined && D.getMasterGain()) D.getMasterGain().gain.value = S.masterVolume;
    const resolved = await D.snapshotState();
    // the exact sample audio each sampler slot plays (built-in samples contain random noise, so a bundled WAV is a different realisation)
    const smpBufs = (st.layers.SMP.oscs || []).map(o => { const b = o.buffer; if (!b || !b.getChannelData) return null; const ch = Math.min(2, b.numberOfChannels), n = b.length, a = new Float32Array(n * ch);
      for (let c = 0; c < ch; c++) { const d = b.getChannelData(c); for (let i = 0; i < n; i++) a[i * ch + c] = d[i]; }
      let bin = ''; const u8 = new Uint8Array(a.buffer); for (let i = 0; i < u8.length; i += 0x8000) bin += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000)); return { sr: b.sampleRate, ch, b64: btoa(bin) }; });
    window.__seed = 777;   // from here on Math.random() is the same sequence the C++ parity tests use
    // schedule events on 128-frame boundaries
    const evs = (sc.events || []).slice().sort((a, b) => a.t - b.t);
    const byT = {};
    evs.forEach(e => { const f = Math.floor(e.t * SR / 128) * 128; (byT[f] = byT[f] || []).push(e); });
    if (sc.ticks) { const nT = Math.floor(sc.dur * 10); const endF = Math.ceil(SR * sc.dur / 128) * 128;
      for (let k = 1; k <= nT; k++) { const f = Math.round(k * 0.1 * SR / 128) * 128; if (f <= 0 || f >= endF) continue; (byT[f] = byT[f] || []).push({ tick: true }); } }
    Object.keys(byT).map(Number).sort((a, b) => a - b).forEach(f => {
      const apply = async () => {
        ctx._hold = true;
        for (const e of byT[f]) {
          if (e.on) D.noteOn(e.layers, e.note, e.vel === undefined ? 0.8 : e.vel, e.plocks ? Object.fromEntries(e.layers.map(l => [l, e.plocks])) : {}, !!e.legato);
          else if (e.off) D.noteOff(e.layers, e.note);
          else if (e.midi) D.handleMidiMessage({ data: e.midi });
          else if (e.fn) { /* reserved */ }
        }
        await new Promise(r => setTimeout(r, 0));   // let initAudio().then(...) inside noteOn run while the context is still suspended
        if (byT[f].some(e => e.tick)) { window.__pn = f / SR * 1000; for (const fn of window.__ticks) { try { fn(); } catch (e) { console.log('tick error', e && e.message); } } await new Promise(r => setTimeout(r, 0)); if (sc.trace) { const S = D.state(); console.log('TICK', f, JSON.stringify({ ph: ['lfo1','lfo2','lfo3','lfo4'].map(k => S.lfo[k].phase), fx: D.getFxStages().map(x => x._live || x.params), t: ctx.currentTime })); } }
      };
      if (f === 0) { /* first events run before rendering starts */ }
      else ctx.suspend(f / SR).then(async () => { await apply(); ctx.realResume(); });
    });
    if (byT[0]) { for (const e of byT[0]) { if (e.on) D.noteOn(e.layers, e.note, e.vel === undefined ? 0.8 : e.vel, e.plocks ? Object.fromEntries(e.layers.map(l => [l, e.plocks])) : {}, !!e.legato); else if (e.off) D.noteOff(e.layers, e.note); else if (e.midi) D.handleMidiMessage({ data: e.midi }); } await new Promise(r => setTimeout(r, 0)); }
    const buf = await ctx.startRendering();
    const L = buf.getChannelData(0), R = buf.getChannelData(1);
    const out = new Float32Array(L.length * 2); out.set(L, 0); out.set(R, L.length);
    let bin = ''; const u8 = new Uint8Array(out.buffer); for (let i = 0; i < u8.length; i += 0x8000) bin += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
    let av = []; try { av = Object.keys(D.getActiveVoices()); } catch (e) {}
    return { b64: btoa(bin), frames: L.length, resolved, av, smpBufs };
  }, { ...sc, sr });
  await page.close();
  const rdir = path.join(path.dirname(file), 'ref'); fs.mkdirSync(rdir, { recursive: true });
  const base = path.join(rdir, path.basename(file).replace(/\.json$/, ''));
  fs.writeFileSync(base + '.ref.f32', Buffer.from(res.b64, 'base64'));
  fs.writeFileSync(base + '.resolved.json', JSON.stringify(res.resolved));
  (res.smpBufs || []).forEach((b, i) => { if (!b) return; const pcm = Buffer.from(b.b64, 'base64'), h = Buffer.alloc(44);
    h.write('RIFF', 0); h.writeUInt32LE(36 + pcm.length, 4); h.write('WAVEfmt ', 8); h.writeUInt32LE(16, 16); h.writeUInt16LE(3, 20); h.writeUInt16LE(b.ch, 22); h.writeUInt32LE(b.sr, 24);
    h.writeUInt32LE(b.sr * b.ch * 4, 28); h.writeUInt16LE(b.ch * 4, 32); h.writeUInt16LE(32, 34); h.write('data', 36); h.writeUInt32LE(pcm.length, 40);
    fs.writeFileSync(base + '.smp' + i + '.wav', Buffer.concat([h, pcm])); });
  console.log(`rendered ${path.basename(file)}: ${res.frames} frames`, process.env.REF_DEBUG ? 'voices left: ' + res.av.join(',') : '');
}
(async () => {
  const files = process.argv.slice(2); if (!files.length) { console.error('usage: node ref_render.js scenario.json ...'); process.exit(1); }
  const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required', '--no-sandbox'] });
  for (const f of files) await renderOne(browser, f);
  await browser.close();
})().catch(e => { console.error(e); process.exit(1); });
