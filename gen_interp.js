  // ============================================================
  // PRESET GENERATOR (magic wand) — recipe-driven. Recipes (gen/make_recipes.py -> recipes.json) describe each sound
  // category as several hand-designed variants whose numbers are drawn from ranges, so every GENERATE gives an original,
  // category-appropriate preset that uses all five layers, the matrix, the FX chain, the sequencer, the arpeggiator,
  // the SID wavetable arp and XFX per-key banks. The interpreter below is mirrored line for line by the native Core
  // (Core/include/vv/presetgen.hpp) and both use the same seeded RNG, so a seed gives the same preset on either side.
  // ============================================================
  const GEN_RECIPES = /*GEN_RECIPES_BEGIN*/__GEN_RECIPES__/*GEN_RECIPES_END*/;
  const GENERATABLE_CATEGORIES = Object.keys(GEN_RECIPES.cats);
  function rand(min,max){ return min + Math.random()*(max-min); }
  function pick(arr){ return arr[Math.floor(Math.random()*arr.length)]; }
  function genMulberry(a){ a >>>= 0; return function(){ a = (a + 0x6D2B79F5) >>> 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }
  // Scale context: active only when a scale is chosen AND Scale lock is on (the keyboard controls above the keys).
  function genScaleCtx(){ const on = !!(scaleName && scaleLockEnabled && SCALES[scaleName]); return { on, root: on ? scaleRootIdx : 0, ints: on ? SCALES[scaleName].slice() : [] }; }
  function genInScale(sc, note){ return sc.ints.includes((((note - sc.root) % 12) + 12) % 12); }
  function genScaleOffsets(sc, lo, hi){ const o = []; for (let p = lo; p <= hi; p++) if (genInScale(sc, 60 + p)) o.push(p); return o; }
  function genR4(x){ return Math.round(x * 10000) / 10000; }
  function genResolve(spec, rng, st){
    if (spec === null || typeof spec !== 'object' || Array.isArray(spec)) return spec;
    if (spec.r)  return genR4(spec.r[0] + rng() * (spec.r[1] - spec.r[0]));
    if (spec.ri) return spec.ri[0] + Math.floor(rng() * (spec.ri[1] - spec.ri[0] + 1));
    if (spec.rl) return genR4(Math.exp(Math.log(spec.rl[0]) + rng() * (Math.log(spec.rl[1]) - Math.log(spec.rl[0]))));
    if (spec.p)  return spec.p[Math.floor(rng() * spec.p.length)];
    if (spec.pu){ const key = JSON.stringify(spec.pu); let u = st.used[key]; if (!u || u.length >= spec.pu.length) u = st.used[key] = [];
      let v, g = 0; do { v = spec.pu[Math.floor(rng() * spec.pu.length)]; g++; } while (u.includes(v) && g < 64); u.push(v); return v; }
    if (spec.sd){ if (st.sc.on && st.sc.ints.length) return st.sc.ints[Math.floor(rng() * st.sc.ints.length)]; return spec.sd[Math.floor(rng() * spec.sd.length)]; }
    return spec;
  }
  function genSet(obj, path, val, warn){
    const seg = path.split('.'); let o = obj;
    for (let i = 0; i < seg.length - 1; i++){ const k = Array.isArray(o) ? +seg[i] : seg[i]; if (o[k] === undefined || o[k] === null || typeof o[k] !== 'object'){ warn.push(path); return; } o = o[k]; }
    const last = Array.isArray(o) ? +seg[seg.length - 1] : seg[seg.length - 1]; o[last] = val;
  }
  // Build one preset's data from a recipe variant. seed: uint32. Returns preset data (same shape as snapshotState), plus .gen metadata.
  function genBuild(recipe, seed, sc, warn){
    warn = warn || []; sc = sc || genScaleCtx();
    const rng = genMulberry(seed), st = { used:{}, sc };
    const data = buildInitPresetData();
    const touched = {}; const touch = p => { const L = (p || '').split(/[.:]/)[p && p.indexOf('layers.') === 0 ? 1 : 0]; if (L && data.layers[L]) touched[L] = true; };
    (recipe.set || []).forEach(([path, spec]) => { touch(path); genSet(data, path, genResolve(spec, rng, st), warn); });
    (recipe.conn || []).forEach(([s, d, a]) => { touch(d); data.connections.push({ source:s, dest:d, amount:genResolve(a, rng, st) }); });
    (recipe.fx || []).forEach(f => {
      const params = defaultFxParams(f.type); (f.params || []).forEach(([k, v]) => { params[k] = genResolve(v, rng, st); });
      const sends = { WT:false, FM:false, SMP:false, PRO:false, SID:false, VOC:false }; (f.sends || []).forEach(L => { sends[L] = true; touched[L] = true; });
      data.fxStages.push({ type:f.type, enabled:true, params, sends });
    });
    if (recipe.seq){ const q = recipe.seq, L = data.layers[q.layer], pat = L.patterns[0]; touched[q.layer] = true;
      pat.length = q.len; pat.steps = []; for (let i = 0; i < Math.max(32, q.len); i++) pat.steps.push({ on:false, pitch:0, ratchet:1, prob:100, tempoRatio:1, plocks:{}, transientIdx:0 });
      const cnt = Math.min(q.len, q.count[0] + Math.floor(rng() * (q.count[1] - q.count[0] + 1)));
      const idx = []; for (let i = 1; i < q.len; i++) idx.push(i);
      for (let i = 0; i < idx.length - 1; i++){ const j = i + Math.floor(rng() * (idx.length - i)); const t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
      const on = [0].concat(idx.slice(0, Math.max(0, cnt - 1)));
      const fb = q.pitches, lo = Math.min.apply(null, fb), hi = Math.max.apply(null, fb);
      let pool = sc.on ? genScaleOffsets(sc, lo, hi) : fb; if (!pool.length) pool = fb;
      on.forEach((si, k) => { const s = pat.steps[si]; s.on = true; s.pitch = (k === 0 && sc.on) ? pool[0] : pool[Math.floor(rng() * pool.length)];
        s.ratchet = q.ratchet[Math.floor(rng() * q.ratchet.length)]; s.prob = 100; });
    }
    if (recipe.wtsteps){ const q = recipe.wtsteps, L = data.layers.SID; touched.SID = true;
      const n = q.n[0] + Math.floor(rng() * (q.n[1] - q.n[0] + 1)); const steps = [];
      for (let i = 0; i < n; i++){ const v = genResolve({ sd:q.pitches }, rng, st);
        steps.push({ wave:q.waves[Math.floor(rng() * q.waves.length)], mode:'relative', value:v, gate:true, ringMod:false, sync:false }); }
      L.wtArp.steps = steps; }
    if (recipe.xfx) genXfx(data, recipe.xfx, rng, st, touched);
    // The sound you hear must be the one generated: the default preset has Wavetable OSC1 sounding, so mute it when the recipe does not use that layer;
    // switch FM engine A on when the recipe uses FM.
    if (!touched.WT) data.layers.WT.oscs[0].level = 0;
    if (touched.FM && !data.layers.FM.engineEnabled[0] && !data.layers.FM.engineEnabled[1]) data.layers.FM.engineEnabled[0] = true;
    data.notes = (recipe.desc || '') + (sc.on ? ' Pitches follow the ' + scaleName + ' scale (root ' + NOTE_NAMES[sc.root] + ') because Scale lock is on.' : '');
    data.gen = { view: recipe.view, name: recipe.name, seed };
    return data;
  }
  function genXfx(data, q, rng, st, touched){
    const sc = st.sc, keys = {}; let cyc = 0;
    q.palette.forEach(e => Object.keys(e.en || {}).forEach(L => { if (e.en[L]) touched[L] = true; }));
    q.palette.forEach(e => (e.o || []).forEach(([id]) => { const L = id.split('.')[0]; if (data.layers[L]) touched[L] = true; }));
    for (let n = q.range[0]; n <= q.range[1]; n++){
      if (sc.on && !genInScale(sc, n)) continue;                       // Scale lock: only the keys that can be played get a voice
      if (rng() > q.fill) continue;
      const pi = q.mode === 'cycle' ? (cyc++ % q.palette.length) : q.mode === 'band' ? Math.min(q.palette.length - 1, Math.floor((n - q.range[0]) / (q.range[1] - q.range[0] + 1) * q.palette.length)) : Math.floor(rng() * q.palette.length);
      const e = q.palette[pi], rec = { en:{}, o:{} };
      Object.keys(e.en || {}).forEach(L => { rec.en[L] = !!e.en[L]; });
      (e.o || []).forEach(([id, spec]) => { rec.o[id] = genResolve(spec, rng, st); });
      keys[n] = rec;
    }
    const banks = []; for (let i = 0; i < 8; i++) banks.push({ name: i === 0 ? q.bank : 'Bank ' + (i + 1), keys: i === 0 ? keys : {}, saved:null });
    data.extra = Object.assign(data.extra || {}, { xfx:{ v:1, enabled:true, active:0, banks } });
  }
  function genVariants(category){ return GEN_RECIPES.cats[category] || []; }
  function genPickVariant(category, seed){ const v = genVariants(category); return v[Math.floor(genMulberry(seed ^ 0x9E3779B9)() * v.length)]; }
  function genNamedPresets(){ return GEN_RECIPES.named || []; }
  function generateRandomPreset(category, seed){
    seed = (seed === undefined ? Math.floor(Math.random() * 4294967296) : seed) >>> 0;
    const v = genPickVariant(category, seed); const warn = [];
    const data = genBuild(v, seed, genScaleCtx(), warn);
    if (warn.length) console.warn('Generator: paths not found in the preset (skipped):', warn.join(', '));
    return { data, variant: v, seed };
  }
