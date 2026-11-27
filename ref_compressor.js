// Renders a test signal through the browser's real DynamicsCompressorNode (the app's master limiter settings)
// so the C++ port can be checked against it.  Output: Core/tests/ref/comp_in.f32, comp_out.f32
const { chromium } = require(process.env.PLAYWRIGHT_PATH || '/opt/npm-tools/node_modules/playwright');
const fs = require('fs'), path = require('path');
const root = path.resolve(__dirname, '..');
(async () => {
  const browser = await chromium.launch({ args: ['--no-sandbox'] });
  const page = await browser.newPage();
  const sr = 48000, dur = 4, N = sr * dur;
  const inp = new Float32Array(N); let s = 99;
  const rnd = () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296 * 2 - 1; };
  const bursts = [[0.0, 0.5, 0.1, 220], [0.6, 0.9, 0.5, 330], [1.0, 1.3, 1.0, 110], [1.4, 1.7, 2.0, 440], [1.8, 2.4, 4.0, 150], [2.6, 3.0, 0.7, 1000], [3.2, 3.9, 1.5, 80]];
  for (const [a, b, amp, f] of bursts) for (let i = Math.floor(a * sr); i < Math.floor(b * sr); i++) inp[i] = amp * (Math.sin(2 * Math.PI * f * i / sr) * 0.8 + 0.2 * rnd());
  const out = await page.evaluate(async ({ arr, sr, N }) => {
    const ctx = new OfflineAudioContext(1, N, sr);
    const b = ctx.createBuffer(1, N, sr); b.getChannelData(0).set(new Float32Array(arr));
    const src = ctx.createBufferSource(); src.buffer = b;
    const c = ctx.createDynamicsCompressor();
    c.threshold.value = -8; c.knee.value = 3; c.ratio.value = 20; c.attack.value = 0.003; c.release.value = 0.15;
    src.connect(c); c.connect(ctx.destination); src.start();
    const r = await ctx.startRendering(); return Array.from(r.getChannelData(0));
  }, { arr: Array.from(inp), sr, N });
  fs.mkdirSync(path.join(root, 'Core/tests/ref'), { recursive: true });
  fs.writeFileSync(path.join(root, 'Core/tests/ref/comp_in.f32'), Buffer.from(inp.buffer));
  fs.writeFileSync(path.join(root, 'Core/tests/ref/comp_out.f32'), Buffer.from(new Float32Array(out).buffer));
  let pk = 0; for (const v of out) pk = Math.max(pk, Math.abs(v)); console.log('rendered, out peak', pk);
  await browser.close();
})().catch(e => { console.error(e); process.exit(1); });
