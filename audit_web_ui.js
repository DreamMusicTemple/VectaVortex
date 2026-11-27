// Live-DOM inventory of every interactive control per page of the web app (run in Chromium).
const {chromium}=require('/opt/npm-tools/node_modules/playwright'); const path=require('path');
(async()=>{
 const b=await chromium.launch({executablePath:process.env.CHROMIUM||undefined,args:['--no-sandbox']}); const pg=await b.newPage({viewport:{width:1600,height:1000}});
 await pg.goto('file://'+path.resolve(__dirname,'../Web/full-app.html')); await pg.waitForTimeout(1500);
 const pages=['osc','xy','matrix','fx','seq','lfo','xmod','vocoder','presets','settings'];
 const res={};
 const grab=()=>pg.evaluate(()=>{ const out=[]; const act=document.querySelector('.page.active'); if(!act) return out;
  const lab=el=>{ let t=''; const f=el.closest('.field'); if(f){const l=f.querySelector('label'); if(l) t=l.textContent;} if(!t&&el.id){const l=document.querySelector('label[for="'+el.id+'"]'); if(l)t=l.textContent;} if(!t) t=el.title||el.getAttribute('aria-label')||''; const p=el.closest('.panel'); const h=p&&p.querySelector('h3,h2'); return (h?h.textContent.trim().slice(0,40)+' > ':'')+t.trim().slice(0,50); };
  act.querySelectorAll('button,select,input,textarea,canvas[id]').forEach(el=>{ const tag=el.tagName.toLowerCase(); const ty=el.type||''; if(tag==='input'&&ty==='file')return;
    out.push([tag==='button'?'button':tag==='select'?'select':tag==='canvas'?'canvas':tag+':'+ty, el.id||'', tag==='button'?el.textContent.trim().slice(0,40):lab(el)].join(' | ')); }); return out; });
 for(const p of pages){ await pg.evaluate(p=>{const t=document.querySelector('.tab[data-page="'+p+'"]'); t&&t.click();},p); await pg.waitForTimeout(400); res[p]=await grab(); }
 // global (outside pages)
 res.global=await pg.evaluate(()=>{const out=[]; document.querySelectorAll('button,select,input').forEach(el=>{ if(el.closest('.page'))return; out.push([el.tagName.toLowerCase()+':'+(el.type||''),el.id||'',(el.textContent||el.title||'').trim().slice(0,40)].join(' | '));}); return out;});
 // osc page layer sub-tabs
 for(const k of ['WT','FM','SMP','PRO']){ await pg.evaluate(()=>document.querySelector('.tab[data-page="osc"]').click()); await pg.evaluate(k=>{const b=document.getElementById('quickView'+(k==='PRO'?'PRO':k)); b&&b.click();},k); await pg.waitForTimeout(400); res['osc_'+k]=await grab(); }
 for(const k in res) console.log('## '+k+' ('+res[k].length+')\n'+res[k].join('\n')+'\n');
 await b.close();})();
