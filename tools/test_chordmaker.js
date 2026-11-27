// Node tests for the chord-maker reference algorithm (musicality properties); also dumps golden progressions for the C++ parity test.
const fs=require('fs'), path=require('path');
const CM=new Function(fs.readFileSync(path.join(__dirname,'chordmaker_core.js'),'utf8')+'\nreturn {CM_SCALES,cmScale,cmRng,cmGenerate,cmMutate,cmPlayNotes,cmChordName,cmEuclid,cmMidi,cmBase,cmRevoice,cmRerollChord};')();
const {CM_SCALES,cmScale,cmRng,cmGenerate,cmMutate,cmPlayNotes,cmChordName,cmEuclid,cmMidi}=CM;
let fails=0; const ok=(c,m)=>{ if(!c){ fails++; console.log('FAIL',m); } };
const base={key:0,scale:'major',naturalness:0.8,density:0.5,sparseness:0.2,randomness:0.1,steps:8,octave:3,voicing:'close',euclid:{on:false,pulses:5,steps:8,rotate:0}};
// 1. every scale: all chord tones stay in key/mode, and outputs are finite/ordered
let total=0;
for (const sc of CM_SCALES){ const S=sc[3]; for (let key=0;key<12;key+=5) for (const nat of [0,0.5,1]) for (let seed=1;seed<=20;seed++){
  const cfg=Object.assign({},base,{scale:sc[0],key,naturalness:nat,density:seed/20}); const prog=cmGenerate(cfg,cmRng(seed*7919+key),[]);
  ok(prog.length===8,'length '+sc[0]);
  for (const c of prog){ if(c.rest) continue; ok(c.tones.length>=2&&c.tones.length<=7,'size '+c.tones.length);
    for (const t of c.tones){ const m=cmMidi(S,cfg,t); const pc=((m-key)%12+12)%12; ok(S.indexOf(pc)>=0,'out of scale '+sc[0]+' pc '+pc); ok(m>=24&&m<=110,'range '+m); total++; } }
  const notes=cmPlayNotes(prog[0],Object.assign({},cfg,{randomness:1}),cmRng(5)); for (const m of notes){ const pc=((m-key)%12+12)%12; ok(S.indexOf(pc)>=0,'random note out of scale'); }
}}
// 2. high naturalness -> starts and ends on the tonic much more often than low naturalness; smoother voice leading
function stats(nat){ let tonicStart=0,tonicEnd=0,move=0,cnt=0; for (let s=1;s<=300;s++){ const cfg=Object.assign({},base,{naturalness:nat,sparseness:0}); const p=cmGenerate(cfg,cmRng(s*131),[]); if(p[0].deg===0)tonicStart++; if(p[p.length-1].deg===0)tonicEnd++;
  for(let i=1;i<p.length;i++){ const a=p[i-1].tones.map(t=>cmMidi(cmScale('major'),cfg,t)), b=p[i].tones.map(t=>cmMidi(cmScale('major'),cfg,t)); let near=0; for(const x of b){ near+=Math.min(...a.map(y=>Math.abs(x-y))); } const ma=a.reduce((x,y)=>x+y,0)/a.length, mb=b.reduce((x,y)=>x+y,0)/b.length; move+=near/b.length+Math.abs(ma-mb); cnt++; } }
  return {tonicStart:tonicStart/300,tonicEnd:tonicEnd/300,move:move/cnt}; }
const hi=stats(1), lo=stats(0); console.log('natural=1',hi,'natural=0',lo);
ok(hi.tonicStart>lo.tonicStart+0.3,'tonic start'); ok(hi.tonicEnd>lo.tonicEnd+0.3,'tonic end');
{ // same chords re-voiced: high naturalness moves less than low naturalness
  const S=cmScale('major'); let mv=[0,0]; for (let s=1;s<=200;s++){ const cfg=Object.assign({},base,{naturalness:0.5,sparseness:0}); const p=cmGenerate(cfg,cmRng(s*17),[]);
    [1,0].forEach((nat,k)=>{ const q=JSON.parse(JSON.stringify(p)); const c2=Object.assign({},cfg,{naturalness:nat}); CM.cmRevoice(q,c2,cmRng(s)); for(let i=1;i<q.length;i++){ const a=q[i-1].tones.map(t=>cmMidi(S,c2,t)), b=q[i].tones.map(t=>cmMidi(S,c2,t)); let near=0; for(const x of b) near+=Math.min(...a.map(y=>Math.abs(x-y))); const ma=a.reduce((x,y)=>x+y,0)/a.length, mb=b.reduce((x,y)=>x+y,0)/b.length; mv[k]+=near/b.length+Math.abs(ma-mb); } }); }
  console.log('voice-leading cost nat=1',mv[0],'nat=0',mv[1]); ok(mv[0]<mv[1],'voice leading smoother with naturalness'); }
// 3. density -> chord sizes, sparseness -> rests / thinner chords
const avg=(d,sp)=>{ let n=0,c=0,r=0,t=0; for (let s=1;s<=200;s++){ const p=cmGenerate(Object.assign({},base,{density:d,sparseness:sp}),cmRng(s),[]); for(const x of p){ t++; if(x.rest) r++; else { n+=x.tones.length; c++; } } } return {size:n/c,rest:r/t}; };
ok(avg(1,0).size>avg(0,0).size+2,'density'); ok(avg(0.5,1).rest>avg(0.5,0).rest+0.3,'sparseness rests'); ok(avg(0.5,1).size<avg(0.5,0).size,'sparseness thins');
// 4. always original: 200 generations in a row never repeat a signature in the rolling history, and are not all identical
{ const hist=[]; const seen=new Set(); for (let s=0;s<200;s++){ const p=cmGenerate(base,cmRng(1000+s*3),hist); const sig=JSON.stringify(p.map(c=>[c.deg,c.tones])); seen.add(sig); hist.push(p.map(c=>c.rest?'-':c.deg+':'+c.tones.length).join(',')); if(hist.length>16)hist.shift(); } ok(seen.size>=195,'originality '+seen.size); }
// 5. euclid
ok(JSON.stringify(cmEuclid(3,8,0).map(x=>x?1:0))==='[1,0,0,1,0,0,1,0]','E(3,8)'); ok(cmEuclid(5,8,0).filter(x=>x).length===5,'E(5,8) count'); ok(cmEuclid(4,16,3).filter(x=>x).length===4,'rot count');
// 6. names
ok(cmChordName({deg:0,tones:[0,2,4],rest:false},base)==='C','C major name'); ok(cmChordName({deg:1,tones:[1,3,5],rest:false},base)==='Dm','Dm name');
// golden for C++
const gold=[]; for (const [sc,key,nat,den,sp,vo,eu] of [['major',0,0.8,0.5,0.2,'close',false],['hijaz',2,0.3,0.9,0.4,'open',false],['hirajoshi',7,0.6,0.2,0.0,'drop2',false],['ragaTodi',5,1,1,0,'close',false],['minor',9,0.5,0.5,0.3,'close',true],['blues',4,0.0,0.7,0.5,'open',false]]){
  const cfg=Object.assign({},base,{scale:sc,key,naturalness:nat,density:den,sparseness:sp,voicing:vo,euclid:{on:eu,pulses:5,steps:8,rotate:1}}); const rnd=cmRng(424242+key); const prog=cmGenerate(cfg,rnd,[]);
  const m=cmMutate(JSON.parse(JSON.stringify(prog)),cfg,cmRng(99),0.5); const pn=cmPlayNotes(prog[0],Object.assign({},cfg,{randomness:0.8}),cmRng(7));
  gold.push({cfg,seed:424242+key,prog,mutated:m,play:pn,names:prog.map(c=>cmChordName(c,cfg))}); }
fs.writeFileSync(path.join(__dirname,'../Core/tests/chordmaker_golden.json'),JSON.stringify(gold));
console.log(fails?'FAILED '+fails:'chordmaker JS tests passed ('+total+' tones checked)'); process.exit(fails?1:0);
