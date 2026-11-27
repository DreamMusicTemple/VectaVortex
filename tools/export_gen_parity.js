// Produces Core/tests/data/gen_parity.json: for a set of (category, variant, seed, scale) cases, the leaves in which the web app's genBuild() output differs from the INIT preset.
// Core/tests/presetgen_parity_tests.cpp rebuilds the same cases with the C++ interpreter and compares. Needs playwright. Usage: node tools/export_gen_parity.js [path/to/full-app.html]
const { chromium } = require('playwright'); const fs = require('fs'), path = require('path');
(async () => {
  const html = path.resolve(process.argv[2] || path.join(__dirname, '../Web/full-app.html'));
  const b = await chromium.launch(); const p = await b.newPage(); await p.goto('file://' + html); await p.waitForTimeout(3500);
  const out = await p.evaluate(() => {
    const G = window.__gen, R = G.recipes(), init = window.__vvInit();
    const flat = (o, pre, m) => { if (o === null || typeof o !== 'object') { m[pre] = o; return m; } if (Array.isArray(o)) { if (!o.length) m[pre + '#len'] = 0; o.forEach((v, i) => flat(v, pre + '.' + i, m)); } else { const k = Object.keys(o); if (!k.length) m[pre + '#empty'] = 1; k.forEach(key => flat(o[key], pre ? pre + '.' + key : key, m)); } return m; };
    const base = flat(JSON.parse(JSON.stringify(init, (k, v) => (k === 'buffer' ? null : v))), '', {});
    const diff = d => { const m = flat(JSON.parse(JSON.stringify(d, (k, v) => (k === 'buffer' ? null : v))), '', {}); const o = {}; Object.keys(m).forEach(k => { if (k === 'gen' || k.startsWith('gen.') || k === 'notes') return; if (!(k in base) || JSON.stringify(base[k]) !== JSON.stringify(m[k])) o[k] = m[k]; }); return o; };
    const cases = []; const A = { on: true, root: 9, ints: [0, 3, 5, 7, 10] }, B = { on: true, root: 2, ints: [0, 2, 4, 5, 7, 9, 11] }, OFF = { on: false, root: 0, ints: [] };
    Object.keys(R.cats).forEach(c => R.cats[c].forEach((v, vi) => [[11, OFF], [4242, A], [987654, B]].forEach(([seed, sc]) => {
      G.setScale(sc.on ? 'x' : '', sc.root, sc.on); const r = G.build(c, { v: vi, s: seed }, sc);
      cases.push({ cat: c, variant: vi, seed, scale: sc, name: v.name, warn: r.warn.length, diff: diff(r.data) }); })));
    R.named.forEach((v, i) => { const r = G.buildNamed(i); cases.push({ named: i, name: v.name, seed: v.seed, scale: OFF, warn: r.warn.length, diff: diff(r.data) }); });
    return cases;
  });
  fs.writeFileSync(path.join(__dirname, '../Core/tests/data/gen_parity.json'), JSON.stringify(out));
  console.log(out.length, 'cases,', Math.round(JSON.stringify(out).length / 1024), 'KB'); await b.close();
})();
