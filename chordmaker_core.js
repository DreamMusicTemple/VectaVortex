// ===== CHORD MAKER core (generative chord progressions) — shared reference implementation =====
// The C++ port (Core/include/vv/chordmaker.hpp) implements exactly this algorithm with the same PRNG, so a given seed + settings
// produces the same progression on both sides (tools/test_chordmaker.js and Core/tests/chordmaker_tests.cpp compare them).
// Embedded in Web/full-app.html between the CHORDMAKER-CORE markers by tools/embed_chordmaker.js.
const CM_SCALES = [
  // [id, label, group, intervals]  (12-TET approximations for the world scales)
  ['major','Major (Ionian)','Western',[0,2,4,5,7,9,11]], ['dorian','Dorian','Western',[0,2,3,5,7,9,10]], ['phrygian','Phrygian','Western',[0,1,3,5,7,8,10]],
  ['lydian','Lydian','Western',[0,2,4,6,7,9,11]], ['mixolydian','Mixolydian','Western',[0,2,4,5,7,9,10]], ['minor','Natural minor (Aeolian)','Western',[0,2,3,5,7,8,10]],
  ['locrian','Locrian','Western',[0,1,3,5,6,8,10]], ['harmonicMinor','Harmonic minor','Western',[0,2,3,5,7,8,11]], ['melodicMinor','Melodic minor','Western',[0,2,3,5,7,9,11]],
  ['majorPent','Major pentatonic','Western',[0,2,4,7,9]], ['minorPent','Minor pentatonic','Western',[0,3,5,7,10]], ['blues','Blues','Western',[0,3,5,6,7,10]],
  ['wholeTone','Whole tone','Western',[0,2,4,6,8,10]], ['diminished','Diminished (whole-half)','Western',[0,2,3,5,6,8,9,11]], ['lydianDominant','Lydian dominant','Western',[0,2,4,6,7,9,10]],
  ['altered','Altered (super-locrian)','Western',[0,1,3,4,6,8,10]], ['neapolitanMinor','Neapolitan minor','Western',[0,1,3,5,7,8,11]],
  ['hijaz','Hijaz / Phrygian dominant (Arabic, Flamenco)','World',[0,1,4,5,7,8,10]], ['doubleHarmonic','Double harmonic (Byzantine, Bhairav)','World',[0,1,4,5,7,8,11]],
  ['hungarianMinor','Hungarian minor (Romani)','World',[0,2,3,6,7,8,11]], ['persian','Persian','World',[0,1,4,5,6,8,11]], ['enigmatic','Enigmatic','World',[0,1,4,6,8,10,11]],
  ['hirajoshi','Hirajoshi (Japan)','World',[0,2,3,7,8]], ['inSen','In Sen (Japan)','World',[0,1,5,7,10]], ['iwato','Iwato (Japan)','World',[0,1,5,6,10]],
  ['yo','Yo (Japan)','World',[0,2,5,7,9]], ['kumoi','Kumoi (Japan)','World',[0,2,3,7,9]], ['chineseGong','Chinese Gong','World',[0,4,6,7,11]],
  ['pelog','Pelog (Bali / Java, approx.)','World',[0,1,3,7,8]], ['slendro','Slendro (Java, approx.)','World',[0,2,5,7,9]],
  ['ragaTodi','Raga Todi (India)','World',[0,1,3,6,7,8,11]], ['ragaMarwa','Raga Marwa (India)','World',[0,1,4,6,9,11]], ['ragaPurvi','Raga Purvi (India)','World',[0,1,4,6,7,8,11]],
  ['maqamRast','Maqam Rast (approx.)','World',[0,2,4,5,7,9,10]], ['maqamSaba','Maqam Saba (approx.)','World',[0,2,3,4,6,8,10]], ['egyptian','Egyptian (Suspended pentatonic)','World',[0,2,5,7,10]]
];
function cmScale(id){ for (let i=0;i<CM_SCALES.length;i++) if (CM_SCALES[i][0]===id) return CM_SCALES[i][3]; return CM_SCALES[0][3]; }
const CM_FUNC = [1.0,0.15,0.4,0.55,0.6,0.8,0.2,0.85,0.5,0.55,0.45,0.3];            // how "stable" a root at this pitch class (from the key) feels
const CM_MOTION = [0.1,0.3,0.6,0.5,0.45,1.0,0.2,0.7,0.5,0.6,0.55,0.4];             // root motion by semitones (5 = up a fourth / down a fifth)
const CM_NAMES = ['C','C#','D','Eb','E','F','F#','G','Ab','A','Bb','B'];
function cmRng(seed){ let a = seed>>>0; return function(){ a = (a+0x6D2B79F5)>>>0; let t = a; t = Math.imul(t^(t>>>15), t|1); t ^= t + Math.imul(t^(t>>>7), t|61); return ((t^(t>>>14))>>>0)/4294967296; }; }
function cmPc(S, idx){ const n=S.length; const o=Math.floor(idx/n); return S[idx-o*n] + 12*o; }                  // semitones above the key for an absolute scale index
function cmBase(cfg){ return 12*(cfg.octave+1) + cfg.key; }
function cmMidi(S, cfg, idx){ return cmBase(cfg) + cmPc(S, idx); }
function cmEuclid(pulses, steps, rotate){ const out=[]; for (let i=0;i<steps;i++){ const j=(((i-rotate)%steps)+steps)%steps; out.push(((j*pulses)%steps) < pulses); } return out; }
function cmDegWeights(S, from, w){
  const n=S.length, out=[]; let sum=0;
  for (let b=0;b<n;b++){
    let nat;
    if (from < 0) nat = CM_FUNC[S[b]%12];
    else { const d=((S[b]-S[from])%12+12)%12; nat = CM_FUNC[S[b]%12]*CM_MOTION[d]; }
    let v = (1-w) + w*nat;
    if (b===from) v *= 0.3;
    out.push(v); sum += v;
  }
  return { w:out, sum:sum };
}
function cmPick(rnd, ws){ let r = rnd()*ws.sum; for (let i=0;i<ws.w.length;i++){ r -= ws.w[i]; if (r<0) return i; } return ws.w.length-1; }
function cmDominant(S){ let best=-1, bv=-1; for (let d=1; d<S.length; d++){ const v=CM_FUNC[S[d]%12]; if (v>bv){ bv=v; best=d; } } return best<0?0:best; }
function cmSizeFor(cfg, rnd){
  let size = 3 + Math.round(cfg.density*3);
  if (cfg.naturalness < 0.6 && rnd() < (1-cfg.naturalness)*0.5) size += (rnd()<0.5 ? -1 : 1);
  return Math.max(2, Math.min(7, size));
}
function cmTones(S, cfg, deg, size, rnd){
  const t=[]; for (let k=0;k<size;k++) t.push(deg+2*k);
  for (let k=t.length-1; k>=1; k--) if (t.length>2 && rnd() < cfg.sparseness*0.5) t.splice(k,1);
  const n=S.length;
  if (cfg.voicing==='open'){ if (t.length>1) t[1]+=n; if (t.length>3) t[3]+=n; }
  else if (cfg.voicing==='drop2'){ if (t.length>2) t[t.length-2]-=n; }
  return t;
}
function cmSortNum(a){ return a.slice().sort((x,y)=>x-y); }
function cmVoiceLead(prog, S, cfg, rnd){
  const n=S.length, center = cmBase(cfg)+7; let prev=null;
  for (let i=0;i<prog.length;i++){
    const c=prog[i]; if (c.rest) continue;
    if (c.locked){ prev = cmSortNum(c.tones.map(t=>cmMidi(S,cfg,t))); continue; }
    const base = cmSortNum(c.tones); let bestCost=1e18, best=null;
    for (let inv=0; inv<base.length; inv++){
      const cand=[]; for (let k=0;k<base.length;k++) cand.push(k<inv ? base[k]+n : base[k]);
      const cs=cmSortNum(cand), mid=cs.map(t=>cmMidi(S,cfg,t));
      let mean=0; for (let k=0;k<mid.length;k++) mean+=mid[k]; mean/=mid.length;
      let cost;
      if (!prev) cost = Math.abs(mean-center);
      else {
        let pm=0; for (let k=0;k<prev.length;k++) pm+=prev[k]; pm/=prev.length;
        let near=0; for (let k=0;k<mid.length;k++){ let m=1e9; for (let j=0;j<prev.length;j++){ const d=Math.abs(mid[k]-prev[j]); if (d<m) m=d; } near+=m; }
        cost = Math.abs(mean-pm) + near/mid.length;
      }
      cost += (1-cfg.naturalness)*rnd()*6;
      if (cost<bestCost){ bestCost=cost; best=cs; }
    }
    let t=best, guard=0;
    for(;;){ let m=0; for (let k=0;k<t.length;k++) m+=cmMidi(S,cfg,t[k]); m/=t.length; if (guard++>6) break;
      if (m-center>6) t=t.map(x=>x-n); else if (center-m>6) t=t.map(x=>x+n); else break; }
    c.tones=cmSortNum(t); prev=c.tones.map(x=>cmMidi(S,cfg,x));
  }
}
function cmChordCount(cfg){ return cfg.euclid.on ? Math.max(1, Math.min(cfg.euclid.pulses, cfg.euclid.steps)) : cfg.steps; }
function cmSignature(prog){ return prog.map(c=>c.rest?'-':c.deg+':'+c.tones.length).join(','); }
function cmGenerate(cfg, rnd, history){
  const S=cmScale(cfg.scale), n=S.length, count=cmChordCount(cfg), w=cfg.naturalness;
  let prog=null;
  for (let attempt=0; attempt<10; attempt++){
    const degs=[];
    for (let i=0;i<count;i++){
      if (i===0) degs.push(rnd()<w ? 0 : cmPick(rnd, cmDegWeights(S,-1,w)));
      else degs.push(cmPick(rnd, cmDegWeights(S, degs[i-1], w)));
    }
    if (count>=4 && w>0.4){
      if (rnd()<w) degs[count-2]=cmDominant(S);
      if (rnd()<w) degs[count-1]=0;
    }
    prog=[];
    for (let i=0;i<count;i++){
      let rest=false; if (i>0 && !cfg.euclid.on && cfg.sparseness>0 && rnd() < cfg.sparseness*0.5) rest=true;
      const size=cmSizeFor(cfg, rnd);
      prog.push({ deg:degs[i], tones:rest?[]:cmTones(S,cfg,degs[i],size,rnd), rest:rest, locked:false });
    }
    cmVoiceLead(prog,S,cfg,rnd);
    const distinct = new Set(prog.filter(c=>!c.rest).map(c=>c.deg)).size;
    const sig = cmSignature(prog);
    const ok = (count<3 || distinct>=Math.min(3,n)) && !(history && history.indexOf(sig)>=0);
    if (ok) break;
  }
  return prog;
}
function cmMutate(prog, cfg, rnd, amount){
  const S=cmScale(cfg.scale), w=cfg.naturalness; let changed=0;
  for (let i=0;i<prog.length;i++){
    const c=prog[i]; if (c.locked) continue;
    if (rnd() < amount){
      const from = i>0 ? prog[i-1].deg : -1;
      c.deg = cmPick(rnd, cmDegWeights(S, from, w)); c.rest = false;
      c.tones = cmTones(S, cfg, c.deg, cmSizeFor(cfg, rnd), rnd); changed++;
    }
  }
  if (!changed){ for (let i=0;i<prog.length;i++){ const c=prog[i]; if (!c.locked){ c.deg=cmPick(rnd, cmDegWeights(S, i>0?prog[i-1].deg:-1, w)); c.rest=false; c.tones=cmTones(S,cfg,c.deg,cmSizeFor(cfg,rnd),rnd); break; } } }
  cmVoiceLead(prog,S,cfg,rnd); return prog;
}
function cmRevoice(prog, cfg, rnd){ const S=cmScale(cfg.scale); cmVoiceLead(prog,S,cfg,rnd); return prog; }
function cmRerollChord(prog, i, cfg, rnd){
  const S=cmScale(cfg.scale), c=prog[i]; if (!c) return prog;
  c.deg = cmPick(rnd, cmDegWeights(S, i>0?prog[i-1].deg:-1, cfg.naturalness)); c.rest=false; c.tones=cmTones(S,cfg,c.deg,cmSizeFor(cfg,rnd),rnd);
  cmVoiceLead(prog,S,cfg,rnd); return prog;
}
// MIDI notes of a chord for one trigger, with the live Randomness applied (notes move to a neighbouring degree of the same scale)
function cmPlayNotes(c, cfg, rnd){
  const S=cmScale(cfg.scale), out=[];
  for (let k=0;k<c.tones.length;k++){
    let idx=c.tones[k];
    if (rnd() < cfg.randomness){ const dir = rnd()<0.5 ? -1 : 1; const big = rnd(); idx += dir * ((cfg.randomness>0.7 && big<0.3) ? 2 : 1); }
    const m=cmMidi(S,cfg,idx); if (out.indexOf(m)<0) out.push(m);
  }
  return out;
}
function cmChordName(c, cfg){
  if (c.rest || !c.tones.length) return '—';
  const S=cmScale(cfg.scale), mid=c.tones.map(t=>cmMidi(S,cfg,t)); let low=mid[0]; for (let i=1;i<mid.length;i++) if (mid[i]<low) low=mid[i];
  const rootIdx=c.deg, root=((cmBase(cfg)+cmPc(S,rootIdx))%12+12)%12;
  const set=[]; for (let i=0;i<mid.length;i++){ const iv=(((mid[i]-root)%12)+12)%12; if (set.indexOf(iv)<0) set.push(iv); }
  set.sort((a,b)=>a-b); const key=set.join(',');
  const Q={ '0,4,7':'', '0,3,7':'m', '0,3,6':'dim', '0,4,8':'aug', '0,2,7':'sus2', '0,5,7':'sus4', '0,4,7,10':'7', '0,4,7,11':'maj7', '0,3,7,10':'m7', '0,3,7,11':'m(maj7)', '0,3,6,10':'m7b5',
    '0,3,6,9':'dim7', '0,2,4,7':'add9', '0,2,3,7':'m(add9)', '0,2,4,7,11':'maj9', '0,2,4,7,10':'9', '0,2,3,7,10':'m9', '0,4,7,9':'6', '0,3,7,9':'m6', '0,4,6,7':'(#11)', '0,1,4,7':'(b9)', '0,7,11':'maj7(no3)', '0,7,10':'7(no3)', '0,4,11':'maj7(no5)', '0,3,10':'m7(no5)', '0,4,10':'7(no5)', '0,3,11':'m(maj7)(no5)' };
  const nm=CM_NAMES[root];
  if (Q[key]!==undefined) return nm+Q[key];
  return nm+'('+set.slice(1).join(' ')+')';
}
// ===== END CHORD MAKER core =====
