// Exports the web app's permanent "DMT CYMATICS" artist bank to Assets/Presets/DMT CYMATICS - <category> - <name>.json
// (raw preset data, same shape as the other factory presets). Run after changing buildCymaticsBankPresets() in the web app.
const { chromium } = require(process.env.PLAYWRIGHT_PATH || '/opt/npm-tools/node_modules/playwright');
const fs = require('fs'), path = require('path');
const root = path.resolve(__dirname, '..'), out = path.join(root, 'Assets/Presets');
(async () => {
  const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required', '--no-sandbox'] });
  const page = await browser.newPage();
  page.on('pageerror', e => console.error('pageerror', e.message));
  await page.goto('file://' + path.join(root, 'Web/full-app.html'), { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => window.__presetDebug && window.__presetDebug.state, null, { timeout: 30000 });
  const bank = await page.evaluate(() => { const a = window.__presetDebug.state().artists.find(x => x.name === 'DMT CYMATICS'); const o = []; Object.keys(a.folders).forEach(c => a.folders[c].forEach(p => o.push({ cat: c, name: p.name, data: p.data }))); return o; });
  for (const f of fs.readdirSync(out)) if (f.startsWith('DMT CYMATICS - ')) fs.unlinkSync(path.join(out, f));
  const safe = s => s.replace(/[\/\\:*?"<>|]/g, '-');
  bank.forEach(p => fs.writeFileSync(path.join(out, `DMT CYMATICS - ${safe(p.cat)} - ${safe(p.name)}.json`), JSON.stringify(p.data)));
  console.log('exported', bank.length, 'presets'); await browser.close();
})();
