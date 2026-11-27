// Golden trace of the Vektur Void game logic (web shmupStep) for Core/tests/void_golden_tests.cpp: node tools/make_golden_void.js
const fs = require('fs'), path = require('path');
const html = fs.readFileSync(path.join(__dirname, '..', 'Web/full-app.html'), 'utf8');
const a = html.indexOf('const SHMUP_W = 640'), b = html.indexOf('// ---- parameter wiring');
const src = html.slice(a, b);
const mk = s => new Function(s + '\nreturn { shmupNewGame, shmupStep, SHMUP_W, SHMUP_H };')();
const libWeb = mk(src);
const lib = libWeb;
function lcg(seed) { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; }
const dt = 1 / 60;
const fmt = v => Number(v).toPrecision(10);
const out = [];
function run(name, seed, god, frames, inputFn) {
  const G = lib.shmupNewGame(); G.god = god; const rnd = lcg(seed);
  const modes = ['title','play','levelclear','continue','gameover','victory'];
  for (let f = 0; f < frames; f++) {
    lib.shmupStep(G, inputFn(f), dt, rnd);
    if (f % 40 === 0) {
      const bo = G.boss;
      out.push([name, f, modes.indexOf(G.mode), G.level, G.score, G.kills, G.spares, G.continues, G.wLvl, fmt(G.ship.x), fmt(G.ship.y), G.ship.alive ? 1 : 0,
        G.bullets.length, G.bombs.length, G.enemies.length, G.ebul.length, G.pick.length, G.parts.length, G.obstacles.length, G.beams.length,
        bo ? bo.hits : -1, bo ? fmt(bo.y) : 0, fmt(G.spread), fmt(G.shake), fmt(G.t)].join(' '));
    }
  }
}
const inp = f => ({ dx: ((f >> 5) % 3) - 1, dy: ((f >> 6) % 3) - 1, plasma: f > 5, bomb: (f % 50) < 3, start: (f % 600) === 5 });
const inpBoss = f => ({ dx: Math.sin(f * 0.02) * 0.9, dy: Math.cos(f * 0.013) * 0.9, plasma: true, bomb: (f % 40) < 2, start: f === 3 });
run('mortal', 12345, false, 14000, inp);
run('god', 777, true, 60000, inpBoss);
run('gated', 4242, true, 260000, inpBoss);
fs.writeFileSync(path.join(__dirname, '..', 'Core/tests/golden_void.txt'), out.join('\n') + '\n');
const last = out[out.length - 1].split(' '); console.log(out.length, 'rows; god run ends mode', last[2], 'level', last[3]);
const lv = new Set(out.filter(r => r.startsWith('god')).map(r => r.split(' ')[3])); console.log('levels reached', [...lv].join(','));
