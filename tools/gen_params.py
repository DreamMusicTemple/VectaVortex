#!/usr/bin/env python3
"""Generates the native parameter tables from Core/parameters.json (dumped from the web app) plus the native-only
FX/master parameters defined below.  Outputs:
  Core/include/vv/params_generated.hpp          C++ table + enum
  Apple/Shared/Generated/ParameterTable.swift   Swift table used to build the AUParameterTree
  docs/PARAMETER_MAP.md                         human-readable map
Parameter ids are persistent: Core/param_ids.json is append-only so saved host sessions keep working."""
import json, os, re
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
P = lambda *a: os.path.join(root, *a)
web = json.load(open(P('Core', 'parameters.json')))

def spec(address, label, group, lo, hi, default, unit='Generic', cc=None, steps=0):
    return dict(address=address, label=label, group=group, min=lo, max=hi, defaultValue=default, unit=unit, cc=cc, steps=steps)

params = [spec(w['address'], w['label'], w['group'], w['min'], w['max'], w['defaultValue'], w['unit'], w.get('cc')) for w in web]
# native-only parameters (the web app keeps these per FX stage; the AUv3 exposes one fixed rack)
params += [
  spec('reverb_algorithm', 'Reverb algorithm (0 off, 1 hall, 2 plate ambient, 3 kristal, 4 flow, 5 mothership, 6 formant)', 'FX', 0, 6, 0, 'Indexed', None, 6),
  spec('reverb_decay', 'Reverb decay', 'FX', 0, 0.97, 0.5), spec('reverb_size', 'Reverb size', 'FX', 0.1, 2, 0.5),
  spec('reverb_predelay', 'Reverb predelay', 'FX', 0, 0.3, 0.02, 'Seconds'), spec('reverb_mix', 'Reverb mix', 'FX', 0, 1, 0.3),
  spec('reverb_mod', 'Reverb modulation', 'FX', 0, 1, 0.4), spec('reverb_colour', 'Reverb colour / shift / vowel', 'FX', 0, 1, 0.5),
  spec('phaser_on', 'Phaser on', 'FX', 0, 1, 0, 'Boolean', None, 1),
  spec('phaser_mode', 'Phaser / flanger (0 phaser, 1 flanger)', 'FX', 0, 1, 0, 'Indexed', None, 1),
  spec('phaser_rate', 'Phaser rate', 'FX', 0.05, 5, 0.4, 'Hertz'), spec('phaser_depth', 'Phaser depth', 'FX', 0, 1, 0.85),
  spec('phaser_feedback', 'Phaser feedback', 'FX', 0, 0.95, 0.6), spec('phaser_mix', 'Phaser mix', 'FX', 0, 1, 0.5),
  spec('phaser_stages', 'Phaser stages', 'FX', 2, 12, 8, 'Indexed', None, 10),
]
# ---- added with the Vektur Vortex update (append-only ids): SID layer, DX7 6-operator mode, chaos attractors, LFO morph / smoothing
for v in (1, 2, 3):
    params += [
      spec(f'sid_voice{v}_on', f'SID voice {v} on', 'SID', 0, 1, 1 if v == 1 else 0, 'Boolean', None, 1),
      spec(f'sid_voice{v}_level', f'SID voice {v} level', 'SID', 0, 1, 0.8 if v == 1 else 0),
      spec(f'sid_voice{v}_pan', f'SID voice {v} pan', 'SID', -1, 1, (v - 2) * 0.3),
      spec(f'sid_voice{v}_pulsewidth', f'SID voice {v} pulse width', 'SID', 0.01, 0.99, 0.5),
      spec(f'sid_voice{v}_wave', f'SID voice {v} waveform (0 tri, 1 saw, 2 pulse, 3 noise, 4 tri+saw, 5 tri+pulse, 6 saw+pulse)', 'SID', 0, 6, 0, 'Indexed', None, 6),
      spec(f'sid_voice{v}_ring', f'SID voice {v} ring mod', 'SID', 0, 1, 0, 'Boolean', None, 1),
      spec(f'sid_voice{v}_sync', f'SID voice {v} hard sync', 'SID', 0, 1, 0, 'Boolean', None, 1),
    ]
params += [
  spec('fm_sixop_on', 'FM 6-operator (DX7) mode on', 'FM', 0, 1, 0, 'Boolean', None, 1),
  spec('fm_sixop_voice_on', 'FM 6-operator voice sounding', 'FM', 0, 1, 0, 'Boolean', None, 1),
  spec('fm_sixop_trim', 'FM 6-operator output trim', 'FM', 0, 2, 1),
  spec('fm_sixop_pan', 'FM 6-operator pan', 'FM', -1, 1, 0),
  spec('fm_sixop_transpose', 'FM 6-operator transpose (octaves)', 'FM', -4, 4, 0),
  spec('chaos_lorenz_rate', 'Chaos Lorenz rate', 'Modulation', 0, 2, 0.5), spec('chaos_rossler_rate', 'Chaos Rossler rate', 'Modulation', 0, 2, 0.5),
]
for n in range(1, 7):
    params += [spec(f'lfo_{n}_morph', f'LFO {n} morph', 'Modulation', 0, 1, 0.5), spec(f'lfo_{n}_smoothing', f'LFO {n} smoothing', 'Modulation', 0, 1, 0)]
# ---- added with Round 3 (append-only ids): EQ effects-rack slot and IR convolution reverb slot (host/AU automation, real units)
params += [spec('fxeq_on', 'EQ slot on', 'FX', 0, 1, 0, 'Boolean', None, 1)]
for b, fq in ((1, 120), (2, 600), (3, 2500), (4, 8000)):
    params += [spec(f'fxeq_band{b}_freq', f'EQ slot band {b} frequency', 'FX', 20, 18000, fq, 'Hertz'), spec(f'fxeq_band{b}_gain', f'EQ slot band {b} gain (dB)', 'FX', -15, 15, 0)]
params += [spec('fxeq_output', 'EQ slot output (dB)', 'FX', -24, 24, 0), spec('fxeq_mix', 'EQ slot wet/dry', 'FX', 0, 1, 1)]
params += [spec('fxir_on', 'IR reverb on', 'FX', 0, 1, 0, 'Boolean', None, 1),
  spec('fxir_factory', 'IR reverb factory IR (index into the factory list)', 'FX', 0, 34, 0, 'Indexed', None, 34),
  spec('fxir_mix', 'IR reverb wet/dry', 'FX', 0, 1, 0.3), spec('fxir_predelay', 'IR reverb pre-delay (ms)', 'FX', 0, 500, 0),
  spec('fxir_length', 'IR reverb IR length (%)', 'FX', 1, 100, 100), spec('fxir_early_late', 'IR reverb early/late balance', 'FX', -1, 1, 0),
  spec('fxir_input_level', 'IR reverb input level (dB)', 'FX', -24, 12, 0), spec('fxir_output_level', 'IR reverb output level (dB)', 'FX', -24, 12, 0),
  spec('fxir_low_cut', 'IR reverb low cut (Hz)', 'FX', 20, 2000, 20, 'Hertz'), spec('fxir_high_cut', 'IR reverb high cut (Hz)', 'FX', 1000, 20000, 20000, 'Hertz'),
  spec('fxir_damping', 'IR reverb damping', 'FX', 0, 1, 0), spec('fxir_tone', 'IR reverb tone', 'FX', -1, 1, 0),
  spec('fxir_width', 'IR reverb stereo width', 'FX', 0, 2, 1), spec('fxir_balance', 'IR reverb L/R balance', 'FX', -1, 1, 0),
  spec('fxir_start', 'IR reverb IR start', 'FX', 0, 0.98, 0), spec('fxir_end', 'IR reverb IR end', 'FX', 0.02, 1, 1), spec('fxir_gain', 'IR reverb IR gain (dB)', 'FX', -24, 24, 0)]
MIX_KEYS = ['wt1','wt2','fma','fmb','fm6op'] + [f'smp{i}' for i in range(1, 17)] + ['prc1','prc2','sid']
for k in MIX_KEYS:
    params.append(spec(f'mixer_{k}_gain', f'Mixer {k.upper()} fader (dB)', 'Mixer', -60, 12, 0))
# de-duplicate (the web tree already has master_volume etc.)
seen = set(); uniq = []
for p in params:
    if p['address'] in seen: continue
    seen.add(p['address']); uniq.append(p)
params = uniq

# persistent ids
idf = P('Core', 'param_ids.json')
ids = json.load(open(idf)) if os.path.exists(idf) else {}
nxt = (max(ids.values()) + 1) if ids else 0
for p in params:
    if p['address'] not in ids: ids[p['address']] = nxt; nxt += 1
json.dump(ids, open(idf, 'w'), indent=1)
for p in params: p['id'] = ids[p['address']]
params.sort(key=lambda p: p['id'])
maxid = max(p['id'] for p in params)

def cid(a): return 'kP_' + re.sub(r'[^a-zA-Z0-9]', '_', a)
def f(x): return repr(float(x))

# ---- C++
L = ['// GENERATED by tools/gen_params.py - do not edit.', '#pragma once', '#include <cstdint>', 'namespace vv {', 'enum ParamId : uint32_t {']
for p in params: L.append(f'    {cid(p["address"])} = {p["id"]},')
L += [f'    kParamIdLimit = {maxid + 1}', '};', 'struct ParamInfo { const char* address; const char* label; const char* group; double minValue, maxValue, defaultValue; const char* unit; int cc; int steps; uint32_t id; };',
      f'constexpr int kParamCount = {len(params)};', 'inline constexpr ParamInfo kParams[kParamCount] = {']
for p in params:
    L.append('    { "%s", "%s", "%s", %s, %s, %s, "%s", %d, %d, %d },' % (p['address'], p['label'].replace('"', "'"), p['group'], f(p['min']), f(p['max']), f(p['defaultValue']), p['unit'], p['cc'] if p['cc'] is not None else -1, p['steps'], p['id']))
L += ['};', '} // namespace vv', '']
open(P('Core', 'include', 'vv', 'params_generated.hpp'), 'w').write('\n'.join(L))

# ---- Swift
os.makedirs(P('Apple', 'Shared', 'Generated'), exist_ok=True)
S = ['// GENERATED by tools/gen_params.py - do not edit.', 'import Foundation', '',
     'struct ParamSpec {', '    let id: UInt64', '    let address: String', '    let label: String', '    let group: String', '    let minValue: Float', '    let maxValue: Float', '    let defaultValue: Float', '    let unit: String', '    let cc: Int', '    let steps: Int', '}', '',
     'enum ParameterTable {', '    static let all: [ParamSpec] = [']
for p in params:
    S.append('        ParamSpec(id: %d, address: "%s", label: "%s", group: "%s", minValue: %s, maxValue: %s, defaultValue: %s, unit: "%s", cc: %d, steps: %d),' % (p['id'], p['address'], p['label'].replace('"', "'"), p['group'], f(p['min']), f(p['max']), f(p['defaultValue']), p['unit'], p['cc'] if p['cc'] is not None else -1, p['steps']))
S += ['    ]', '    static let byAddress: [String: ParamSpec] = Dictionary(uniqueKeysWithValues: all.map { ($0.address, $0) })', '}', '']
open(P('Apple', 'Shared', 'Generated', 'ParameterTable.swift'), 'w').write('\n'.join(S))

# ---- doc
M = ['# Parameter map', '', 'Generated by `tools/gen_params.py`. Ids are persistent (`Core/param_ids.json`, append-only).', '', f'{len(params)} parameters.', '', '| id | address | group | min | max | default | unit | MIDI CC |', '|---|---|---|---|---|---|---|---|']
for p in params: M.append(f'| {p["id"]} | `{p["address"]}` | {p["group"]} | {p["min"]} | {p["max"]} | {p["defaultValue"]} | {p["unit"]} | {p["cc"] if p["cc"] is not None else ""} |')
open(P('docs', 'PARAMETER_MAP.md'), 'w').write('\n'.join(M) + '\n')
print(len(params), 'parameters generated')
