#!/usr/bin/env python3
"""Regenerates recipes.json and splices the generator (gen_interp.js + recipes) into /home/claude/new-web.html."""
import json, re, sys
sys.path.insert(0, '/home/claude/gen')
import make_recipes
rec = json.dumps(make_recipes.export(), separators=(',', ':'))
json.dump(make_recipes.export(), open('/home/claude/gen/recipes.json', 'w'), separators=(',', ':'))
p = '/home/claude/new-web.html'; s = open(p).read()
B, E = '/*GEN_RECIPES_BEGIN*/', '/*GEN_RECIPES_END*/'
if B in s:
    i = s.index(B) + len(B); j = s.index(E)
    s = s[:i] + rec + s[j:]
else:
    a = s.index("  const GENERATABLE_CATEGORIES = ['Arp'")
    b = s.index("  function nudgeValue(v)")
    js = open('/home/claude/gen/gen_interp.js').read().replace('__GEN_RECIPES__', rec)
    s = s[:a] + js + s[b:]
open(p, 'w').write(s)
print('spliced', len(rec), 'bytes of recipes')
