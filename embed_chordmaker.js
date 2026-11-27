// Embeds tools/chordmaker_core.js and tools/chordmaker_ui.js into Web/full-app.html between marker comments (idempotent).
// Usage: node tools/embed_chordmaker.js   (the HTML must contain the "CHORDMAKER-CORE" markers; first run inserts them before "function startTransport")
const fs=require('fs'), path=require('path'); const f=path.join(__dirname,'../Web/full-app.html'); let h=fs.readFileSync(f,'utf8');
const core=fs.readFileSync(path.join(__dirname,'chordmaker_core.js'),'utf8'), ui=fs.readFileSync(path.join(__dirname,'chordmaker_ui.js'),'utf8');
for (const s of [core,ui]) if (s.includes('${')||s.includes('`')) throw new Error('chord maker source must not contain template characters');
const block='/*CHORDMAKER-BEGIN*/\n'+core+'\n'+ui+'/*CHORDMAKER-END*/\n';
if (h.includes('/*CHORDMAKER-BEGIN*/')) h=h.replace(/\/\*CHORDMAKER-BEGIN\*\/[\s\S]*?\/\*CHORDMAKER-END\*\/\n/, ()=>block);
else { const k=h.indexOf('  function startTransport(){'); if (k<0) throw new Error('startTransport not found'); h=h.slice(0,k)+block+h.slice(k); }
fs.writeFileSync(f,h); console.log('embedded chord maker');
