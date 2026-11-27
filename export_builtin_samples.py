#!/usr/bin/env python3
"""Usage: export_builtin_samples.py [name-prefix]  (with a prefix only those samples are rendered and appended to the manifest).
Renders the web app's built-in synthesised samples to 16-bit WAV files (Assets/Samples/<name>.wav) so the native app can bundle
them instead of regenerating them in C++. They stay filed under Misc in the sample tree.  Needs playwright + chromium."""
import asyncio, base64, json, os, re, struct, subprocess, sys, time, wave
from playwright.async_api import async_playwright
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
out = os.path.join(root, 'Assets', 'Samples'); os.makedirs(out, exist_ok=True)
async def main():
    srv = subprocess.Popen([sys.executable, '-m', 'http.server', '8898', '--directory', os.path.join(root, 'Web')], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); time.sleep(1)
    try:
        async with async_playwright() as p:
            b = await p.chromium.launch(args=['--autoplay-policy=no-user-gesture-required']); pg = await b.new_page()
            await pg.goto('http://localhost:8898/full-app.html'); await pg.wait_for_timeout(1500)
            await pg.evaluate('window.__presetDebug.initAudio()'); await pg.wait_for_timeout(25000)   # built-ins are generated in idle slices
            data = await pg.evaluate("""()=>window.__presetDebug.state().sampleLibrary.filter(s=>s.builtin).map(s=>{
                const ch=s.buffer.numberOfChannels, n=s.buffer.length, a=s.buffer.getChannelData(0), i16=new Int16Array(n);
                for(let i=0;i<n;i++){ const v=Math.max(-1,Math.min(1,a[i])); i16[i]=Math.round(v*32767); }
                let bin=''; const u8=new Uint8Array(i16.buffer); for(let i=0;i<u8.length;i+=0x8000) bin+=String.fromCharCode.apply(null,u8.subarray(i,i+0x8000));
                return {name:s.name, sr:s.buffer.sampleRate, ch, b64:btoa(bin)}; })""")
            only = sys.argv[1] if len(sys.argv) > 1 else None
            manifest = json.load(open(os.path.join(root, 'Assets', 'builtin_samples.json'))) if only else []
            if only: data = [x for x in data if x['name'].startswith(only)]; manifest = [m for m in manifest if not m['name'].startswith(only)]
            for s in data:
                fn = re.sub(r'[^A-Za-z0-9]+', '_', s['name']).strip('_') + '.wav'
                w = wave.open(os.path.join(out, fn), 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(int(s['sr'])); w.writeframes(base64.b64decode(s['b64'])); w.close()
                manifest.append({'name': s['name'], 'file': fn, 'folder': 'Misc'})
            json.dump(manifest, open(os.path.join(root, 'Assets', 'builtin_samples.json'), 'w'), indent=1)
            print(len(manifest), 'samples written')
    finally: srv.terminate()
asyncio.run(main())
