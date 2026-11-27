#!/usr/bin/env python3
"""Dumps the AUv3 bridge parameter tree from the web app (window.VectaAUv3.getParameterTree()) to parameters.json.
Needs: pip install playwright && playwright install chromium.   Usage: python3 tools/dump_parameters.py"""
import asyncio, json, subprocess, time, os, sys
from playwright.async_api import async_playwright
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
async def main():
    srv = subprocess.Popen([sys.executable, '-m', 'http.server', '8899', '--directory', os.path.join(root, 'Web')], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); time.sleep(1)
    try:
        async with async_playwright() as p:
            b = await p.chromium.launch(); pg = await b.new_page()
            await pg.goto('http://localhost:8899/full-app.html'); await pg.wait_for_timeout(1500)
            tree = await pg.evaluate('window.VectaAUv3.getParameterTree()')
            json.dump(tree, open(os.path.join(root, 'Core', 'parameters.json'), 'w'), indent=1)
            print(len(tree), 'parameters written')
    finally:
        srv.terminate()
asyncio.run(main())
