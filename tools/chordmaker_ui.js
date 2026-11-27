// ===== CHORD MAKER UI + playback (web) =====
const CM_RATES = [['÷4',0.25],['÷3',1/3],['÷2',0.5],['÷1.5',2/3],['×1',1],['×1.5',1.5],['×2',2],['×3',3],['×4',4]];
function CM_DEFAULT(){ return { on:false, key:0, scale:'major', naturalness:0.7, density:0.4, sparseness:0.15, randomness:0.1, prob:100, steps:8, octave:3, voicing:'close',
  rate:1, gate:0.9, strum:0, vel:0.8, target:'ALL', euclid:{ on:false, pulses:5, steps:8, rotate:0 }, seed:0, progression:[], history:[] }; }
state.chordMaker = CM_DEFAULT();
const cmRT = { acc:0, first:true, step:0, onset:0, active:[], timers:[], playing:-1 };
function cmTargets(){ const t=state.chordMaker.target; return t==='ALL' ? LAYER_KEYS_WFS.slice() : [t]; }
function cmNewSeed(){ try{ const a=new Uint32Array(1); crypto.getRandomValues(a); return a[0]; }catch(e){ return (Math.random()*4294967296)>>>0; } }
function cmRemember(){ const cm=state.chordMaker; cm.history.push(cmSignature(cm.progression)); if (cm.history.length>16) cm.history.shift(); }
function cmDoGenerate(){ const cm=state.chordMaker; cm.seed=cmNewSeed(); cm.progression=cmGenerate(cm, cmRng(cm.seed), cm.history); cmRemember(); cmRenderStrip(); }
function cmFitLength(){
  const cm=state.chordMaker, count=cmChordCount(cm), p=cm.progression;
  while (p.length>count) p.pop();
  while (p.length<count){ p.push({ deg:0, tones:[0,2,4], rest:false, locked:false }); cmRerollChord(p, p.length-1, cm, cmRng(cmNewSeed())); }
  cmRenderStrip();
}
function cmAction(kind){
  const cm=state.chordMaker; if (!cm.progression.length) { cmDoGenerate(); return; }
  const rnd=cmRng(cmNewSeed()), p=cm.progression;
  if (kind==='generate') cmDoGenerate();
  else if (kind==='mutate'){ cmMutate(p, cm, rnd, 0.35); cmRemember(); }
  else if (kind==='revoice') cmRevoice(p, cm, rnd);
  else if (kind==='rotL' && p.length>1) p.push(p.shift());
  else if (kind==='rotR' && p.length>1) p.unshift(p.pop());
  else if (kind==='reverse') p.reverse();
  cmRenderStrip();
}
function cmAudition(i){
  const cm=state.chordMaker, c=cm.progression[i]; if (!c || c.rest) return; const arr=cmTargets(), notes=cmPlayNotes(c, cm, Math.random);
  notes.forEach(n=>noteOn(arr, n, cm.vel)); setTimeout(()=>notes.forEach(n=>noteOff(arr, n)), 700);
}
function cmRenderStrip(){
  const el=document.getElementById('cmStrip'); if (!el) return; const cm=state.chordMaker; el.innerHTML='';
  const eu=document.getElementById('cmEuDots'); if (eu){ eu.textContent = cm.euclid.on ? cmEuclid(cm.euclid.pulses, cm.euclid.steps, cm.euclid.rotate).map(x=>x?'●':'○').join(' ') : ''; }
  cm.progression.forEach((c,i)=>{
    const d=document.createElement('div'); d.className='cm-card'+(c.locked?' locked':'')+(c.rest?' rest':'')+(i===cmRT.playing?' playing':''); d.dataset.i=i;
    d.innerHTML = '<div class="cm-name">'+(c.rest?'rest':cmChordName(c,cm))+'</div><div class="cm-sub">'+(c.rest?'':('deg '+(c.deg+1)+' · '+c.tones.length+' notes'))+'</div>'
      + '<div class="cm-btns"><button data-a="aud" title="Audition">▶</button><button data-a="dice" title="Re-roll this chord">⚄</button><button data-a="lock" title="Lock (kept when mutating)">'+(c.locked?'🔒':'🔓')+'</button></div>';
    d.querySelectorAll('button').forEach(b=>b.addEventListener('click', ()=>{ const a=b.dataset.a;
      if (a==='aud') cmAudition(i); else if (a==='lock'){ c.locked=!c.locked; cmRenderStrip(); } else if (a==='dice'){ cmRerollChord(cm.progression, i, cm, cmRng(cmNewSeed())); cmRenderStrip(); } }));
    el.appendChild(d);
  });
}
function cmMarkPlaying(i){ cmRT.playing=i; const el=document.getElementById('cmStrip'); if (!el) return; [...el.children].forEach((c,k)=>c.classList.toggle('playing', k===i)); }
function cmReleaseActive(){ cmRT.timers.forEach(clearTimeout); cmRT.timers=[]; cmRT.active.forEach(a=>{ try{ noteOff(a.arr, a.n); }catch(e){} }); cmRT.active=[]; }
function cmReset(){ cmReleaseActive(); cmRT.acc=0; cmRT.first=true; cmRT.step=0; cmRT.onset=0; cmMarkPlaying(-1); }
function cmStop(){ cmReleaseActive(); cmMarkPlaying(-1); }
function chordStep(stepDur){
  const cm=state.chordMaker, p=cm.progression; if (!p.length) return;
  const eu=cm.euclid, G = eu.on ? eu.steps : cm.steps, idx = cmRT.step % G; cmRT.step++;
  let ci, hold=1;
  if (eu.on){
    const pat=cmEuclid(eu.pulses, eu.steps, eu.rotate); if (!pat[idx]) return;
    ci = cmRT.onset % p.length; cmRT.onset++;
    hold=1; for (let k=1;k<=G;k++){ if (pat[(idx+k)%G]){ hold=k; break; } }
  } else ci = idx % p.length;
  const c=p[ci]; cmMarkPlaying(ci);
  cmReleaseActive();
  if (!c || c.rest) return;
  if (Math.random()*100 >= cm.prob) return;
  const arr=cmTargets(), notes=cmPlayNotes(c, cm, Math.random), strum=cm.strum/1000;
  notes.forEach((n,k)=>{
    const on = ()=>{ noteOn(arr, n, Math.min(1, Math.max(0.05, cm.vel*(0.92+Math.random()*0.16)))); cmRT.active.push({ arr, n }); };
    if (k===0 || strum<=0) on(); else cmRT.timers.push(setTimeout(on, k*strum*1000));
  });
  cmRT.timers.push(setTimeout(()=>{ notes.forEach(n=>{ try{ noteOff(arr, n); }catch(e){} }); cmRT.active=[]; }, Math.max(30, stepDur*hold*cm.gate*1000)));
}
function chordTick(dt){
  const cm=state.chordMaker; if (!cm.on) return; const stepDur=(60/state.bpm)*2/cm.rate;
  if (cmRT.first){ cmRT.first=false; cmRT.acc=0; chordStep(stepDur); return; }
  cmRT.acc+=dt; if (cmRT.acc>=stepDur){ cmRT.acc-=stepDur; chordStep(stepDur); }
}
const CM_CTRLS = [['cmKey','key','i'],['cmScale','scale','s'],['cmNat','naturalness','f'],['cmDen','density','f'],['cmSpa','sparseness','f'],['cmRnd','randomness','f'],['cmProb','prob','f'],
  ['cmLen','steps','i'],['cmOct','octave','i'],['cmVoicing','voicing','s'],['cmRate','rate','f'],['cmGate','gate','f'],['cmStrum','strum','f'],['cmVel','vel','f'],['cmTarget','target','s']];
function cmSyncUI(){
  const cm=state.chordMaker, g=id=>document.getElementById(id); if (!g('cmPanel')) return;
  CM_CTRLS.forEach(([id,k])=>{ const e=g(id); if (!e) return; e.value=cm[k]; const v=g(id+'Val'); if (v) v.textContent = (typeof cm[k]==='number') ? (Math.abs(cm[k])<10 && k!=='key' && k!=='steps' && k!=='octave' ? (+cm[k]).toFixed(2) : String(Math.round(cm[k]*100)/100)) : ''; });
  g('cmOn').checked=cm.on; g('cmEuOn').checked=cm.euclid.on; g('cmEuP').value=cm.euclid.pulses; g('cmEuN').value=cm.euclid.steps; g('cmEuR').value=cm.euclid.rotate;
  cmRenderStrip();
}
function cmInitUI(){
  const g=id=>document.getElementById(id); if (!g('cmPanel')) return; const cm=()=>state.chordMaker;
  g('cmKey').innerHTML=CM_NAMES.map((n,i)=>'<option value="'+i+'">'+n+'</option>').join('');
  let h='', grp=''; CM_SCALES.forEach(s=>{ if (s[2]!==grp){ if (grp) h+='</optgroup>'; grp=s[2]; h+='<optgroup label="'+grp+'">'; } h+='<option value="'+s[0]+'">'+s[1]+'</option>'; }); g('cmScale').innerHTML=h+'</optgroup>';
  g('cmRate').innerHTML=CM_RATES.map(r=>'<option value="'+r[1]+'">'+r[0]+'</option>').join('');
  g('cmTarget').innerHTML='<option value="ALL">All layers</option>'+LAYER_KEYS_WFS.map(k=>'<option value="'+k+'">'+k+'</option>').join('');
  g('cmVoicing').innerHTML='<option value="close">Close</option><option value="open">Open (spread)</option><option value="drop2">Drop 2</option>';
  CM_CTRLS.forEach(([id,k,t])=>{ const e=g(id); if (!e) return; const fn=()=>{ let v=e.value; v = t==='i' ? parseInt(v,10) : t==='f' ? parseFloat(v) : v; cm()[k]=v;
      const lab=g(id+'Val'); if (lab && typeof v==='number') lab.textContent = (k==='steps'||k==='octave'||k==='key') ? String(v) : v.toFixed(2);
      if (k==='steps') cmFitLength(); if (k==='scale') cmRevoice(cm().progression, cm(), cmRng(cmNewSeed())); if (k==='scale'||k==='key'||k==='voicing') cmRenderStrip(); };
    e.addEventListener(e.tagName==='SELECT'?'change':'input', fn); });
  g('cmOn').addEventListener('change', e=>{ cm().on=e.target.checked; if (e.target.checked && !cm().progression.length) cmDoGenerate(); if (!e.target.checked) cmStop(); else cmRT.first=true; });
  const eu=()=>cm().euclid;
  g('cmEuOn').addEventListener('change', e=>{ eu().on=e.target.checked; cmFitLength(); });
  [['cmEuP','pulses',1,32],['cmEuN','steps',1,32],['cmEuR','rotate',0,31]].forEach(([id,k,lo,hi])=>g(id).addEventListener('input', e=>{ let v=parseInt(e.target.value,10); if (isNaN(v)) return; eu()[k]=Math.max(lo,Math.min(hi,v)); if (eu().pulses>eu().steps) eu().pulses=eu().steps; cmFitLength(); }));
  [['cmGen','generate'],['cmMut','mutate'],['cmRevoiceBtn','revoice'],['cmRotL','rotL'],['cmRotR','rotR'],['cmReverseBtn','reverse']].forEach(([id,a])=>g(id).addEventListener('click', ()=>cmAction(a)));
  cmDoGenerate(); cmSyncUI();
}
// ===== END CHORD MAKER UI =====
