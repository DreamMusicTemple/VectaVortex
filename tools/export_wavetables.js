// Exports every built-in wavetable of the web app to a binary asset the native engine loads.
// Format (little-endian): uint32 frameCount, uint32 frameSize, then frameCount*frameSize float32.
// Noise tables (whiteNoise, pinkNoise, noisySpectral) use Math.random in the web app; here Math.random is a
// seeded LCG (re-seeded per table) so the native asset is reproducible.
const { chromium } = require(process.env.PLAYWRIGHT_PATH || '/opt/npm-tools/node_modules/playwright');
const fs = require('fs'), path = require('path');
const root = path.resolve(__dirname, '..');
(async () => {
  const browser = await chromium.launch({ args: ['--no-sandbox'] });
  const page = await browser.newPage();
  await page.route(/^https?:/, r => r.abort());
  await page.addInitScript(() => {
    window.__seed = 12345;
    Math.random = () => { window.__seed = (Math.imul(window.__seed, 1664525) + 1013904223) >>> 0; return window.__seed / 4294967296; };
  });
  await page.goto('file://' + path.join(root, 'Web/full-app.html'), { waitUntil: 'domcontentloaded' });
  await page.waitForTimeout(800);
  const out = await page.evaluate(() => {
    const D = window.__presetDebug;
    const names = ['analogMorph','digitalMorph','oddHarmonics','evenHarmonics','formantAEIOU','bellMetallic','glassy','vocalOoh','squareMorph','sawBright','pwmLike','organDrawbars','noisySpectral','subBass','stringsEnsemble','fmLikeBell','wavefolded','whiteNoise','pinkNoise','growlBass','bitcrushed8bit','screechLead','additiveStaircase','talkingFormant','ensembleShimmer','resoSweep','metallicClang','subGrowl','digitalAlias','pluckDecay','vowelDiphthong','glitchStutter','warmPWM','crystalShimmer','vortexSpiral','prismRefraction','nebulaDrift','quantumFlutter','origamiFold','dnbReese','dnbNeuroFold','dnbTechStep','dnbTalkingGrowl','rhodesTine','rhodesBark','droneWarmPad','droneSubChoir','droneAirDrift','percClickThump','percWoodBurst','glassHarmonica','glassShatter','supersaw','vocalIRMale','vocalIRFemale','vocalIRChoir','ambientGlassPad','ambientCloudDrift','ambientSineStack','ambientBreath','ambientCrystalline','ambientVelvetDrone','ambientHalo','ambientMistVeil','bassGrowlFangs','bassAcidSquelch','bassDistortedSaw','bassWobbleCrunch','bassMetalGrind','bassSubCrush','bassFeedbackRoar','bassIndustrialStomp','chipNesVoices','chipSidCombined','dnbDarkChoir','dnbHollowChoir','dnbCathedralChoir','dnbDemonChoir','droneSoftTide','droneSoftBloom','epTine','epReed','bassWarmPunch','bassHeavyThump','bassFunkResonant','bassFunkSlap','leadHoover'];
    const res = {};
    for (const n of names) { window.__seed = 12345; const fr = D.getWavetable(n); const sz = fr[0].length; const flat = new Float32Array(fr.length * sz); fr.forEach((f, i) => flat.set(f, i * sz)); res[n] = { frames: fr.length, size: sz, b64: btoa(String.fromCharCode(...new Uint8Array(flat.buffer))) }; }
    return res;
  });
  const dir = path.join(root, 'Assets/Wavetables'); fs.mkdirSync(dir, { recursive: true });
  const manifest = [];
  for (const [n, v] of Object.entries(out)) {
    const body = Buffer.from(v.b64, 'base64'); const hdr = Buffer.alloc(8); hdr.writeUInt32LE(v.frames, 0); hdr.writeUInt32LE(v.size, 4);
    fs.writeFileSync(path.join(dir, n + '.vvwt'), Buffer.concat([hdr, body])); manifest.push({ name: n, frames: v.frames, size: v.size });
  }
  fs.writeFileSync(path.join(dir, 'manifest.json'), JSON.stringify(manifest, null, 1));
  console.log('exported', manifest.length, 'tables');
  await browser.close();
})().catch(e => { console.error(e); process.exit(1); });
