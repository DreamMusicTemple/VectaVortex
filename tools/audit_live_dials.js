// Held-note audit: for every oscillator-layer dial (and optionally FX dials) hold a note, turn the dial half way through, and report the dials whose
// movement does not change the held sound (they only take effect on the next note). usage: node audit_live_dials.js [layers=WT,FM,SMP,PRO,SID] [outfile]
const { chromium } = require('/opt/npm-tools/node_modules/playwright'); const path = require('path'); const fs = require('fs');
const root = path.resolve(__dirname, '..'); const SR = 48000, DUR = 1.6, CH = 0.6;
const layers = (process.argv[2] || 'WT,FM,SMP,PRO,SID').split(','); const outFile = process.argv[3] || '/tmp/live_audit.json';
async function run(browser, layer, idx, kind) {   // idx = -1: baseline (no change); kind 'osc' | 'fx:<type>'
  const page = await browser.newPage(); await page.route(/^https?:/, r => r.abort());
  await page.addInitScript(({ SR, DUR }) => {
    class OAC extends OfflineAudioContext { constructor() { super({ numberOfChannels: 2, length: Math.ceil(SR * DUR / 128) * 128, sampleRate: SR }); } startRendering() { this._rendering = true; return super.startRendering(); }
      resume() { return (this._rendering && !this._hold) ? super.resume() : Promise.resolve(); } realResume() { this._hold = false; return super.resume(); } }
    window.AudioContext = OAC; window.webkitAudioContext = OAC;
    const _st = window.setTimeout.bind(window); window.setTimeout = (fn, ms, ...a) => (ms !== undefined && ms >= 20) ? 0 : _st(fn, ms, ...a);
    window.__ticks = []; const _si = window.setInterval.bind(window); window.setInterval = (fn, ms, ...a) => { if (ms === 100) { window.__ticks.push(fn); return 0; } return _si(fn, ms, ...a); };
    window.__pn = 0; performance.now = () => window.__pn; window.__seed = 777;
    Math.random = () => { window.__seed = (Math.imul(window.__seed, 1664525) + 1013904223) >>> 0; return window.__seed / 4294967296; };
  }, { SR, DUR });
  await page.goto('file://' + path.join(root, 'Web/full-app.html'), { waitUntil: 'domcontentloaded' }); await page.waitForTimeout(600);
  const res = await page.evaluate(async ({ layer, idx, kind, CH, SR }) => {
    const D = window.__presetDebug; await D.initAudio(); const ctx = D.getCtx();
    const LB = { WT: 0, FM: 1, SMP: 2, PRO: 3, SID: 4 };
    const PATCH = { WT: [['layers.WT.oscs.0.unison', 3], ['layers.WT.oscs.0.detune', 8], ['layers.WT.oscs.0.width', 0.5], ['layers.WT.oscs.1.enabled', true], ['layers.WT.oscs.1.unison', 3], ['layers.WT.oscs.1.detune', 8], ['layers.WT.oscs.1.width', 0.5], ['layers.WT.oscs.1.table', 'sawBright']],
      FM: [['layers.FM.engineEnabled', [true, true]], ...[0,1,2,3,4,5,6,7].map(i => ['layers.FM.oscs.' + i + '.level', 0.5])], SMP: [['layers.SMP.oscs.0.name', 'Flute (synthesized)'], ['layers.SMP.oscs.0.level', 0.8]],
      PRO: [['layers.PRO.oscs.0.enabled', true], ['layers.PRO.oscs.0.model', 'pluck'], ['layers.PRO.oscs.1.enabled', true], ['layers.PRO.oscs.1.model', 'modal']], SID: [0, 1, 2].flatMap(i => [['layers.SID.oscs.' + i + '.enabled', true], ['layers.SID.oscs.' + i + '.level', 0.5], ['layers.SID.oscs.' + i + '.wave', ['pulse', 'sawtooth', 'triangle'][i]]]) }[layer];
    if (PATCH.length) { const pr = await D.snapshotState(); for (const [pth, val] of PATCH) { const ks = pth.split('.'); let o = pr; for (let i = 0; i < ks.length - 1; i++) { const k = /^\d+$/.test(ks[i]) ? +ks[i] : ks[i]; if (o[k] === undefined || o[k] === null) o[k] = /^\d+$/.test(ks[i + 1]) ? [] : {}; o = o[k]; } const lk = /^\d+$/.test(ks[ks.length - 1]) ? +ks[ks.length - 1] : ks[ks.length - 1]; o[lk] = val; } await D.loadPresetData(pr); }
    document.getElementById('oscLayerBtns').children[LB[layer]].click(); await new Promise(r => setTimeout(r, 5));
    document.querySelector('.tab[data-page="osc"]').click();
    let dials = [...document.querySelectorAll('#oscLayerContent input[data-knobified]')];
    let info = null;
    if (kind.startsWith('fx:')) { const t = kind.slice(3); const ft = document.getElementById('fxAddType'); }
    if (idx >= 0) { const el = dials[idx]; info = { n: dials.length, cls: el.className, key: el.dataset.paramKey, min: +el.min, max: +el.max, v: +el.value };
      const mid = (info.min + info.max) / 2; info.to = info.v < mid ? info.min + (info.max - info.min) * 0.75 : info.min + (info.max - info.min) * 0.25; }
    else info = { n: dials.length };
    D.noteOn([layer], 60, 0.8, {}, false);
    await new Promise(r => setTimeout(r, 0));
    if (idx >= 0) {
      ctx.suspend(CH).then(async () => { ctx._hold = true; const el = dials[idx]; el.value = String(info.to); el.dispatchEvent(new Event('input', { bubbles: true })); el.dispatchEvent(new Event('change', { bubbles: true }));
        await new Promise(r => setTimeout(r, 0)); for (const fn of window.__ticks) { try { fn(); } catch (e) {} } ctx.realResume(); });
    }
    const buf = await ctx.startRendering(); const L = buf.getChannelData(0), R = buf.getChannelData(1);
    const out = new Float32Array(L.length * 2); out.set(L, 0); out.set(R, L.length); let bin = ''; const u8 = new Uint8Array(out.buffer); for (let i = 0; i < u8.length; i += 0x8000) bin += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
    return { b64: btoa(bin), info };
  }, { layer, idx, kind, CH, SR });
  await page.close(); const f = new Float32Array(Buffer.from(res.b64, 'base64').buffer.slice(0)); return { f, info: res.info };
}
(async () => {
  const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required', '--no-sandbox'] }); const report = {};
  for (const L of layers) {
    const base = await run(browser, L, -1, 'osc'); const n = base.info.n; const h = base.f.length / 2; const s0 = Math.floor(SR * (CH + 0.05));
    const rms = (a, b) => { let e = 0, p = 0; for (let i = s0; i < h; i++) { const d = a[i] - b[i]; e += d * d; p += b[i] * b[i]; } return Math.sqrt(e / Math.max(1e-12, p)); };
    let pBase = 0; for (let i = s0; i < h; i++) pBase += base.f[i] * base.f[i];
    report[L] = { n, silentBase: pBase < 1e-9, dials: [] };
    for (let i = 0; i < n; i++) { const r = await run(browser, L, i, 'osc'); const d = rms(r.f, base.f); report[L].dials.push({ i, cls: r.info.cls.split(' ').filter(c => /^(o-|f-|fm-|env|ef|eq|sg|pr|sid)/.test(c)).join(' ') || r.info.cls, key: r.info.key, from: r.info.v, to: +r.info.to.toFixed(4), delta: +d.toFixed(5) });
      process.stdout.write(L + ' ' + i + '/' + n + ' ' + (d < 1e-4 ? 'DEAD ' : 'ok   ') + d.toFixed(4) + ' ' + report[L].dials[i].cls + '\n'); fs.writeFileSync(outFile, JSON.stringify(report, null, 1)); }
  }
  await browser.close();
})();
