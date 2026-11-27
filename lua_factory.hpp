// lua_factory.hpp - the 31 factory LUA scripts (generated from the web app's script library; identical sources).
#pragma once
#include <cstddef>
namespace vv { struct LuaFactoryScript { const char* id; const char* src; };
static const LuaFactoryScript kLuaFactory[] = {
{ "additive_organ", R"LUASRC(-- Additive Harmonics: drawbar-style additive organ built from sine partials
instrument{ name="Additive Harmonics", category="Additive", author="Vektur", version=1, tags={"additive","organ","drawbars"},
  desc="Eight harmonic drawbars (partials 1-8 and 10 mapped to sine oscillators)." }
param("h1", {label="1st", min=0, max=1, default=1,   group="Drawbars"})
param("h2", {label="2nd", min=0, max=1, default=0.6, group="Drawbars"})
param("h3", {label="3rd", min=0, max=1, default=0.5, group="Drawbars"})
param("h4", {label="4th", min=0, max=1, default=0.3, group="Drawbars"})
param("h5", {label="5th", min=0, max=1, default=0.2, group="Drawbars"})
param("h6", {label="6th", min=0, max=1, default=0.15, group="Drawbars"})
param("h8", {label="8th", min=0, max=1, default=0.1, group="Drawbars"})
param("tilt", {label="Tilt", min=-1, max=1, default=0})
local ORDER = {1, 2, 3, 4, 5, 6, 8}
function init(v)
  v.h = {}
  for i = 1, #ORDER do v.h[i] = vv.osc(v, {wave="sine", ratio=ORDER[i], level=0}) end
end
function control(v)
  local ids = {"h1","h2","h3","h4","h5","h6","h8"}
  for i = 1, #ORDER do
    local tilt = 1 + v.p.tilt * (i - 4) * 0.15
    v.h[i].level = v.p[ids[i]] * 0.22 * math.max(0, tilt)
  end
end
)LUASRC" },
{ "ambient_drone", R"LUASRC(-- Generative Ambient Drone: seeded random chord tones that slowly drift in level
instrument{ name="Generative Ambient Drone", category="Generative Ambient", author="Vektur", version=1, tags={"ambient","generative","drone","seeded"},
  desc="Each note picks scale tones from the layer seed, so the same seed always gives the same drone. Change the seed for a new one." }
param("scale", {label="Scale", kind="menu", options={"Minor","Dorian","Lydian","Pentatonic","Whole tone"}, default=1})
param("tones", {label="Tones",  min=2, max=8, default=5, step=1})
param("drift", {label="Drift",  min=0.02, max=1, default=0.12, unit="Hz"})
param("air",   {label="Air",    min=0, max=1, default=0.2})
local SC = {"minor","dorian","lydian","pentatonic","wholetone"}
function init(v)
  v.tn = {} v.m = {}
  local n = math.floor(v.p.tones + 0.5)
  local scale = SC[v.p.scale + 1]
  for i = 1, n do
    local deg = math.floor(vv.rand(0, 8))
    local semi = vv.scale(scale, deg, 0) - 12 * math.floor(vv.rand(0, 2))
    v.tn[i] = vv.osc(v, {wave=(i % 2 == 0) and "tri" or "sine", level=0, ratio=2 ^ (semi / 12), detune=vv.rand(-6, 6), pan=vv.rand(-0.7, 0.7)})
    v.m[i] = vv.mod(v, {kind="lfo", shape="smooth", rate=vv.rand(0.3, 1) , bipolar=false})
  end
  v.n = n
  v.air = vv.osc(v, {wave="noise", level=0})
end
function control(v)
  for i = 1, v.n do
    v.m[i].rate = v.p.drift * (0.4 + i * 0.13)
    v.tn[i].level = (0.12 / math.sqrt(v.n)) * (0.25 + 0.75 * v.m[i].out) * 2.2
  end
  v.air.level = v.p.air * 0.03
end
)LUASRC" },
{ "arpeggiator", R"LUASRC(-- Algorithmic Arpeggiator: tempo-synced step sequence of scale degrees with a plucked envelope per step
instrument{ name="Algorithmic Arp", category="Rhythmic Generators", author="Vektur", version=1, tags={"arp","sequence","rhythm","tempo"},
  desc="Steps through a scale pattern in time with the host tempo. The pattern length and scale are parameters." }
param("div",   {label="Rate", kind="menu", options={"1/4","1/8","1/16","1/8T"}, default=1})
param("scale", {label="Scale", kind="menu", options={"Minor","Major","Dorian","Pentatonic","Blues"}, default=0})
param("steps", {label="Steps", min=2, max=8, default=6, step=1})
param("gate",  {label="Gate",  min=0.05, max=1, default=0.5})
param("pluck", {label="Pluck", min=0, max=1, default=0.6})
local DIVS = {"1/4","1/8","1/16","1/8T"}
local BEATS = {1, 0.5, 0.25, 1/3}
local SC = {"minor","major","dorian","pentatonic","blues"}
local PATTERN = {0, 2, 4, 2, 5, 4, 7, 4}
function init(v)
  v.o = vv.osc(v, {wave="saw", level=0.5})
  v.o2 = vv.osc(v, {wave="square", level=0.25, ratio=2})
  v.last = -1
end
function control(v)
  local len = math.floor(v.p.steps + 0.5)
  local b = BEATS[v.p.div + 1]
  local pos = v.beat / b
  local idx = math.floor(pos)
  local ph = pos - idx
  local deg = PATTERN[(idx % len) + 1]
  v.pitch = vv.scale(SC[v.p.scale + 1], deg, 0)
  local g = (ph < v.p.gate) and 1 or 0
  local env = math.exp(-ph * (2 + v.p.pluck * 10))
  v.level = g * (1 - v.p.pluck + v.p.pluck * env)
  v.o2.level = 0.25 * env
end
)LUASRC" },
{ "basic_va", R"LUASRC(-- Basic VA: classic virtual-analogue voice (two detuned oscillators plus a sub)
instrument{ name="Basic VA", category="Virtual Analogue", author="Vektur", version=1, tags={"saw","square","bass","lead"},
  desc="Two detuned oscillators with sub and drive. Start here." }
param("wave",   {label="Wave",   kind="menu", options={"Saw","Square","Triangle","Pulse"}, default=0})
param("detune", {label="Detune", min=0, max=50, default=9, unit="ct"})
param("sub",    {label="Sub",    min=0, max=1,  default=0.35})
param("pw",     {label="Pulse W",min=0.05, max=0.95, default=0.5})
param("drive",  {label="Drive",  min=0, max=1, default=0.1})
local WAVES = {"saw","square","tri","pulse"}
function init(v)
  local w = WAVES[v.p.wave + 1]
  v.a = vv.osc(v, {wave=w, level=0.5, pw=v.p.pw})
  v.b = vv.osc(v, {wave=w, level=0.5, pw=v.p.pw})
  v.s = vv.osc(v, {wave="sine", level=v.p.sub * 0.6, ratio=0.5})
end
function control(v)
  local d = v.p.detune
  v.a.detune = -d  v.b.detune = d
  v.a.pw = v.p.pw  v.b.pw = v.p.pw
  v.a.drive = v.p.drive  v.b.drive = v.p.drive
  v.s.level = v.p.sub * 0.6
end
)LUASRC" },
{ "chaos_osc", R"LUASRC(-- Chaotic Modulation Oscillator: a Lorenz attractor drives FM depth and pitch wobble
instrument{ name="Chaos Oscillator", category="Experimental Oscillators", author="Vektur", version=1, tags={"chaos","lorenz","fm","experimental"},
  desc="A chaos modulator (lorenz/logistic/henon) steers FM index and ratio. Output is unpredictable but seeded." }
param("shape", {label="Attractor", kind="menu", options={"Lorenz","Logistic","Henon"}, default=0})
param("rate",  {label="Chaos rate", min=0.1, max=40, default=3, unit="Hz"})
param("index", {label="FM amount", min=0, max=8, default=2.5})
param("wobble",{label="Pitch wobble", min=0, max=300, default=40, unit="ct"})
local SHAPES = {"lorenz","logistic","henon"}
function init(v)
  v.m = vv.osc(v, {wave="sine", ratio=1.5, out=false})
  v.c = vv.osc(v, {wave="sine", fmsrc=1, fm=1, level=0.6})
  v.s = vv.osc(v, {wave="tri", ratio=0.5, level=0.2})
  v.k = vv.mod(v, {kind="chaos", shape=SHAPES[v.p.shape + 1], rate=v.p.rate})
end
function control(v)
  v.k.rate = v.p.rate
  v.c.fm = v.p.index * (0.5 + 0.5 * v.k.out)
  v.m.ratio = 1.5 + 0.5 * v.k.out
  v.c.detune = v.k.out * v.p.wobble
end
)LUASRC" },
{ "evolving_wavetable", R"LUASRC(-- Evolving Wavetable: generated 32-frame morphing wavetable scanned by a slow LFO
instrument{ name="Evolving Wavetable", category="Wavetable Generators", author="Vektur", version=1, tags={"wavetable","morph","evolving","pad"},
  desc="The wavetable is generated in the setup block, band-limited per octave, and scanned by an LFO." }
param("speed", {label="Scan speed", min=0.02, max=4, default=0.18, unit="Hz"})
param("depth", {label="Scan depth", min=0, max=1, default=0.8})
param("pos",   {label="Position",  min=0, max=1, default=0.1})
param("bright",{label="Brightness",min=1, max=24, default=10})
-- Setup: build the table once (frames 0..1 morph from sine-ish to bright with a formant bump)
vv.wavetable("morph", 32, function(f, ph)
  local s, n = 0, 14
  for h = 1, n do
    local roll = 1 / h
    local bump = math.exp(-((h - (2 + f * 9)) ^ 2) / 6) * (0.6 + f)
    s = s + math.sin(6.2831853 * h * ph + f * h * 0.4) * (roll + bump * 0.5)
  end
  return s
end, {size=1024})
function init(v)
  v.o  = vv.osc(v, {wave="morph", level=0.55, pos=0.1, unison=3, spread=9, width=0.7})
  v.o2 = vv.osc(v, {wave="morph", level=0.25, ratio=2, pos=0.5})
  v.l  = vv.mod(v, {kind="lfo", shape="tri", rate=v.p.speed, bipolar=false})
end
function control(v)
  v.l.rate = v.p.speed
  v.o.pos  = vv.clamp(v.p.pos + (v.l.out - 0.5) * v.p.depth, 0, 1)
  v.o2.pos = 1 - v.o.pos
end
)LUASRC" },
{ "fifths_drone", R"LUASRC(-- Fifths Drone: root, fifth and octave sines with beating detune
instrument{ name="Fifths Drone", category="Drones", author="Vektur", version=1, tags={"drone","fifth","beating","meditation"},
  desc="Held drone: stacked fifths and octaves with slow beating between detuned pairs." }
param("beat",  {label="Beating", min=0, max=3, default=0.4, unit="Hz"})
param("fifth", {label="Fifth",   min=0, max=1, default=0.5})
param("oct",   {label="Octaves", min=0, max=1, default=0.4})
function init(v)
  v.r1 = vv.osc(v, {wave="sine", level=0.3, pan=-0.3})
  v.r2 = vv.osc(v, {wave="sine", level=0.3, pan=0.3})
  v.f1 = vv.osc(v, {wave="tri",  level=0.1, ratio=1.5, pan=-0.5})
  v.f2 = vv.osc(v, {wave="tri",  level=0.1, ratio=1.5, pan=0.5})
  v.o1 = vv.osc(v, {wave="sine", level=0.1, ratio=2})
  v.o0 = vv.osc(v, {wave="sine", level=0.15, ratio=0.5})
  v.l  = vv.mod(v, {kind="lfo", shape="smooth", rate=0.07, bipolar=false})
end
function control(v)
  local hz = v.freq
  local cents = v.p.beat / hz * 1731
  v.r1.detune = -cents  v.r2.detune = cents
  v.f1.detune = -cents  v.f2.detune = cents
  v.f1.level = 0.2 * v.p.fifth * (0.6 + 0.4 * v.l.out)  v.f2.level = v.f1.level
  v.o1.level = 0.2 * v.p.oct  v.o0.level = 0.3 * v.p.oct
end
)LUASRC" },
{ "fm_bell", R"LUASRC(-- FM Bell: two-operator phase modulation with a decaying index
instrument{ name="FM Bell", category="FM", author="Vektur", version=1, tags={"fm","bell","metallic","keys"},
  desc="Inharmonic modulator ratio gives bell-like partials; the index decays like a struck bar." }
param("ratio", {label="Mod ratio", min=0.5, max=8, default=3.5})
param("index", {label="Index",     min=0, max=10, default=4})
param("decay", {label="Decay",     min=0.1, max=4, default=1.2, unit="s"})
param("tone",  {label="Overtone",  min=0, max=1, default=0.3})
function init(v)
  v.mod = vv.osc(v, {wave="sine", ratio=v.p.ratio, out=false})
  v.car = vv.osc(v, {wave="sine", fmsrc=1, fm=v.p.index, level=0.7})
  v.hi  = vv.osc(v, {wave="sine", ratio=5.4, level=0.1, fmsrc=1, fm=1})
  v.env = vv.mod(v, {kind="env", a=0.001, d=v.p.decay, s=0, r=0.4})
end
function control(v)
  v.mod.ratio = v.p.ratio
  v.car.fm = v.p.index * (0.15 + v.env.out)
  v.hi.level = 0.25 * v.p.tone * v.env.out
  v.level = 0.35 + 0.65 * v.env.out
end
)LUASRC" },
{ "fold_lead", R"LUASRC(-- Foldback Lead: waveshaping and wavefolding with an envelope-controlled fold amount
instrument{ name="Foldback Lead", category="Experimental DSP", author="Vektur", version=1, tags={"fold","waveshaping","lead","harsh"},
  desc="A triangle into the oscillator foldback stage; fold amount follows a decay envelope plus modulation wheel style parameter." }
param("fold",  {label="Fold",  min=0, max=1, default=0.5})
param("env",   {label="Env amount", min=0, max=1, default=0.6})
param("decay", {label="Env decay", min=0.05, max=2, default=0.4, unit="s"})
param("drive", {label="Drive", min=0, max=1, default=0.3})
function init(v)
  v.o = vv.osc(v, {wave="tri", level=0.5, fold=0.5, drive=0.3})
  v.o2 = vv.osc(v, {wave="sine", level=0.25, ratio=2, fold=0.3})
  v.e = vv.mod(v, {kind="env", a=0.002, d=v.p.decay, s=0.25, r=0.3})
end
function control(v)
  v.e.d = v.p.decay
  v.o.fold = vv.clamp(v.p.fold + v.p.env * v.e.out, 0, 1)
  v.o2.fold = v.o.fold * 0.6
  v.o.drive = v.p.drive
end
)LUASRC" },
{ "grain_cloud", R"LUASRC(-- Grain Cloud: a stochastic cloud of short sine grains (granular-style, built from oscillators)
instrument{ name="Grain Cloud", category="Granular", author="Vektur", version=1, tags={"granular","cloud","random","texture"},
  desc="Sixteen sine 'grains' re-pick random pitches and fire at random times from a sample-and-hold modulator. Not a sample-based granulator." }
param("density", {label="Density", min=0.5, max=20, default=7, unit="Hz"})
param("spread",  {label="Pitch spread", min=0, max=24, default=7, unit="st"})
param("size",    {label="Grain level", min=0, max=1, default=0.5})
local N = 12
function init(v)
  v.g = {} v.m = {}
  for i = 1, N do
    v.g[i] = vv.osc(v, {wave="sine", level=0, pan=vv.rand(-0.9, 0.9)})
    v.m[i] = vv.mod(v, {kind="lfo", shape="sh", rate=v.p.density * (0.5 + vv.rand(0, 1)), bipolar=false})
  end
  v.base = {}
  for i = 1, N do v.base[i] = vv.rand(-1, 1) end
end
function control(v)
  for i = 1, N do
    local x = v.m[i].out
    v.g[i].detune = (v.base[i] * 0.5 + x - 0.5) * v.p.spread * 100
    v.g[i].level = v.p.size * 0.1 * (x > 0.45 and 1 or 0.05)
    v.m[i].rate = v.p.density * (0.5 + i / N)
  end
end
)LUASRC" },
{ "motion_pad", R"LUASRC(-- Modulation Motion: stacked LFO, step and smooth-random modulators on one pad
instrument{ name="Motion Pad", category="Modulation", author="Vektur", version=1, tags={"modulation","steps","lfo","pad"},
  desc="Demonstrates every modulator type: tempo-synced LFO, step sequence, envelope follower and smooth random." }
param("div",    {label="Sync rate", kind="menu", options={"1/1","1/2","1/4","1/8"}, default=1})
param("steps",  {label="Step depth", min=0, max=1, default=0.5})
param("random", {label="Random",     min=0, max=1, default=0.4})
local DIVS = {"1/1","1/2","1/4","1/8"}
function init(v)
  v.a = vv.osc(v, {wave="saw", level=0.35, unison=3, spread=10})
  v.b = vv.osc(v, {wave="square", level=0.2, pw=0.5, ratio=0.5})
  v.lfo = vv.mod(v, {kind="lfo", shape="tri", div=DIVS[v.p.div + 1], bipolar=true})
  v.seq = vv.mod(v, {kind="steps", steps={0, 0.5, 0.25, 1, 0.75, 0.25, 0.5, 0}, div="1/8"})
  v.rnd = vv.mod(v, {kind="lfo", shape="smooth", rate=0.35, bipolar=true})
end
function control(v)
  v.b.pw = 0.5 + 0.35 * v.lfo.out
  v.a.detune = v.seq.out * v.p.steps * 15
  v.a.level = 0.35 + 0.15 * v.rnd.out * v.p.random
  v.pitch = (v.seq.out > 0.7 and 0.0 or 0.0) + v.rnd.out * v.p.random * 0.3
end
)LUASRC" },
{ "organic_texture", R"LUASRC(-- Organic Random Texture: breathy noise with wandering pitched partials
instrument{ name="Organic Texture", category="Organic Textures", author="Vektur", version=1, tags={"texture","breath","random","organic"},
  desc="Smoothed random modulation moves a cluster of partials against a noise bed." }
param("noise",  {label="Breath", min=0, max=1, default=0.35})
param("wander", {label="Wander", min=0.05, max=3, default=0.5, unit="Hz"})
param("cluster",{label="Cluster",min=0, max=50, default=18, unit="ct"})
function init(v)
  v.n = vv.osc(v, {wave="noise", level=0.1})
  v.p1 = vv.osc(v, {wave="sine", level=0.3})
  v.p2 = vv.osc(v, {wave="tri",  level=0.2, ratio=2.01})
  v.p3 = vv.osc(v, {wave="sine", level=0.15, ratio=3.02})
  v.r1 = vv.mod(v, {kind="lfo", shape="smooth", rate=v.p.wander, bipolar=true})
  v.r2 = vv.mod(v, {kind="lfo", shape="smooth", rate=v.p.wander * 1.7, bipolar=true})
  v.r3 = vv.mod(v, {kind="noise", rate=4, amount=1})
  v.f  = vv.mod(v, {kind="follow", src=2})
end
function control(v)
  v.r1.rate = v.p.wander  v.r2.rate = v.p.wander * 1.7
  v.p1.detune = v.r1.out * v.p.cluster
  v.p2.detune = v.r2.out * v.p.cluster
  v.p3.detune = -v.r1.out * v.p.cluster * 0.5
  v.n.level = v.p.noise * 0.12 * (0.6 + 0.4 * math.abs(v.r2.out))
  v.p1.level = 0.3 * (0.7 + 0.3 * v.r2.out)
end
)LUASRC" },
{ "pwm_pad", R"LUASRC(-- PWM Pad: pulse oscillators with slow independent pulse-width motion
instrument{ name="PWM Pad", category="Virtual Analogue", author="Vektur", version=1, tags={"pwm","pad","chorus"},
  desc="Two pulse oscillators with opposing slow PWM LFOs." }
param("rate",  {label="PWM Rate",  min=0.05, max=8, default=0.4, unit="Hz"})
param("depth", {label="PWM Depth", min=0, max=0.45, default=0.3})
param("detune",{label="Detune",    min=0, max=30, default=7, unit="ct"})
function init(v)
  v.o1 = vv.osc(v, {wave="pulse", level=0.5, pw=0.5})
  v.o2 = vv.osc(v, {wave="pulse", level=0.5, pw=0.5})
  v.l1 = vv.mod(v, {kind="lfo", shape="tri", rate=v.p.rate})
  v.l2 = vv.mod(v, {kind="lfo", shape="tri", rate=v.p.rate * 1.37, phase=0.5})
end
function control(v)
  v.l1.rate = v.p.rate  v.l2.rate = v.p.rate * 1.37
  v.o1.pw = 0.5 + v.l1.out * v.p.depth
  v.o2.pw = 0.5 + v.l2.out * v.p.depth
  v.o1.detune = -v.p.detune  v.o2.detune = v.p.detune
end
)LUASRC" },
{ "ring_bell", R"LUASRC(-- Ring Mod Metal: ring and amplitude modulation between two oscillators
instrument{ name="Ring Mod Metal", category="Experimental Oscillators", author="Vektur", version=1, tags={"ring","am","metal","clang"},
  desc="Oscillator 1 is ring-modulated by oscillator 2 (inharmonic ratio) and amplitude-modulated by a slow LFO." }
param("ratio", {label="Mod ratio", min=0.25, max=9, default=2.41})
param("ring",  {label="Ring",      min=0, max=1, default=0.8})
param("am",    {label="AM depth",  min=0, max=1, default=0.3})
param("rate",  {label="AM rate",   min=0.1, max=12, default=3.2, unit="Hz"})
function init(v)
  v.m = vv.osc(v, {wave="sine", ratio=v.p.ratio, out=false})
  v.c = vv.osc(v, {wave="saw", level=0.5, ringsrc=1, ring=v.p.ring})
  v.l = vv.mod(v, {kind="lfo", shape="sine", rate=v.p.rate, bipolar=false})
end
function control(v)
  v.m.ratio = v.p.ratio
  v.c.ring = v.p.ring
  v.l.rate = v.p.rate
  v.level = 1 - v.p.am * v.l.out
end
)LUASRC" },
{ "supersaw", R"LUASRC(-- Supersaw: 7-voice detuned saw stack with stereo spread
instrument{ name="Supersaw", category="Virtual Analogue", author="Vektur", version=1, tags={"supersaw","trance","pad","lead"},
  desc="Seven detuned saws per oscillator. Detune and Spread shape the width." }
param("detune", {label="Detune", min=0, max=60, default=22, unit="ct"})
param("spread", {label="Width",  min=0, max=1,  default=0.8})
param("sub",    {label="Sub",    min=0, max=1,  default=0.25})
param("mix",    {label="Square mix", min=0, max=1, default=0.2})
function init(v)
  v.a = vv.osc(v, {wave="saw", level=0.45, unison=7, spread=v.p.detune, width=v.p.spread})
  v.b = vv.osc(v, {wave="square", level=0.2, unison=3, spread=v.p.detune*0.5, width=v.p.spread, ratio=1})
  v.s = vv.osc(v, {wave="sine", level=0.3, ratio=0.5})
end
function control(v)
  v.a.spread = v.p.detune  v.a.width = v.p.spread
  v.b.spread = v.p.detune * 0.5  v.b.width = v.p.spread  v.b.level = 0.4 * v.p.mix
  v.s.level = v.p.sub * 0.5
end
)LUASRC" },
{ "template", R"LUASRC(-- Empty template: copy this to start your own instrument
instrument{ name="My Instrument", category="User Scripts", author="Me", version=1 }
param("level", {label="Level", min=0, max=1, default=0.7})
function init(v)
  v.o = vv.osc(v, {wave="saw", level=0.5})
end
function control(v)
  v.o.level = v.p.level * 0.6
end
)LUASRC" },
{ "amb_glass_cathedral", R"LUASRC(-- Glass Cathedral: slow additive pad whose upper partials breathe like light through stained glass
instrument{ name="Glass Cathedral", category="Generative Ambient", author="Vektur", version=1, tags={"ambient","pad","glass","additive","evolving"},
  desc="A morphing additive bank played by three detuned layers and a soft air layer. Upper partials swell and fade on two slow, unrelated breathing LFOs." }
param("morph",  {label="Morph",        min=0, max=1,    default=0.4})
param("rate",   {label="Breath rate",  min=0.02, max=0.6, default=0.11, unit="Hz"})
param("detune", {label="Detune",       min=0, max=30,   default=9, unit="ct"})
param("air",    {label="Air",          min=0, max=1,    default=0.25})
param("width",  {label="Width",        min=0, max=1,    default=0.85})
vv.additiveBank("glasscath", 16, 24, function(f, h)
  local base = 1 / (h ^ 1.15)
  local bell = math.exp(-((h - (2 + f * 14)) ^ 2) / 6)
  local odd = (h % 2 == 1) and 1 or 0.55
  return base * odd * (0.35 + bell * 2.2)
end)
function init(v)
  v.a = vv.osc(v, {wave="glasscath", level=0.22, pos=0.2, unison=3, spread=v.p.detune, width=v.p.width})
  v.b = vv.osc(v, {wave="glasscath", level=0.12, pos=0.5, ratio=2, unison=2, spread=v.p.detune * 0.6, width=v.p.width})
  v.c = vv.osc(v, {wave="sine", level=0.12, ratio=0.5})
  v.air = vv.osc(v, {wave="noise", level=0})
  v.l1 = vv.mod(v, {kind="lfo", shape="smooth", rate=v.p.rate, bipolar=false})
  v.l2 = vv.mod(v, {kind="lfo", shape="tri", rate=v.p.rate * 0.63, bipolar=false, phase=0.3})
end
function control(v)
  v.l1.rate = v.p.rate  v.l2.rate = v.p.rate * 0.63
  v.a.pos = v.p.morph * 0.6 + v.l1.out * 0.4
  v.b.pos = 1 - (v.p.morph * 0.5 + v.l2.out * 0.5)
  v.b.level = 0.05 + 0.12 * v.l2.out
  v.a.spread = v.p.detune  v.a.width = v.p.width
  v.air.level = v.p.air * 0.025 * (0.4 + v.l1.out)
end
)LUASRC" },
{ "amb_tidal_drift", R"LUASRC(-- Tidal Drift: five sine/triangle partials whose levels wash in and out like waves on a shore
instrument{ name="Tidal Drift", category="Generative Ambient", author="Vektur", version=1, tags={"ambient","drone","pad","slow","waves"},
  desc="Root, fifth, octave, twelfth and double octave, each on its own slow smooth LFO with a little chaotic pitch wander. Very quiet movement, very wide." }
param("tide",  {label="Tide speed", min=0.02, max=0.5, default=0.09, unit="Hz"})
param("wander",{label="Pitch wander", min=0, max=12, default=3, unit="ct"})
param("sub",   {label="Sub",   min=0, max=1, default=0.35})
param("warmth",{label="Warmth", min=0, max=1, default=0.5})
local R = {1, 1.5, 2, 3, 4}
function init(v)
  v.pt = {} v.m = {}
  for i = 1, 5 do
    v.pt[i] = vv.osc(v, {wave=(i % 2 == 0) and "tri" or "sine", level=0, ratio=R[i], detune=(i - 3) * 2, pan=(i - 3) * 0.32})
    v.m[i] = vv.mod(v, {kind="lfo", shape="smooth", rate=0.05 + i * 0.031, bipolar=false, phase=i * 0.19})
  end
  v.s = vv.osc(v, {wave="sine", level=0.2, ratio=0.5})
  v.w = vv.mod(v, {kind="chaos", shape="lorenz", rate=0.25})
end
function control(v)
  for i = 1, 5 do
    v.m[i].rate = v.p.tide * (0.6 + i * 0.35)
    local lv = 0.2 / (1 + (i - 1) * 0.55)
    v.pt[i].level = lv * (0.15 + 0.85 * v.m[i].out) * (0.6 + 0.8 * v.p.warmth * (i <= 2 and 1 or 0.4))
    v.pt[i].detune = (i - 3) * 2 + v.w.out * v.p.wander * (i - 2)
  end
  v.s.level = 0.28 * v.p.sub
end
)LUASRC" },
{ "amb_aurora_choir", R"LUASRC(-- Aurora Choir: a sung-vowel additive bank that scans slowly from "oo" through "ah" to "ee"
instrument{ name="Aurora Choir", category="Generative Ambient", author="Vektur", version=1, tags={"ambient","choir","vowel","formant","pad"},
  desc="Formant peaks are painted onto the harmonic series, so the pad sings a slow vowel sweep without a filter. Two unison layers scan in opposite directions." }
param("vowel", {label="Vowel", min=0, max=1, default=0.35})
param("scan",  {label="Scan speed", min=0.01, max=0.4, default=0.07, unit="Hz"})
param("depth", {label="Scan depth", min=0, max=1, default=0.6})
param("detune",{label="Detune", min=0, max=30, default=11, unit="ct"})
param("breath",{label="Breath", min=0, max=1, default=0.2})
vv.additiveBank("aurorachoir", 24, 40, function(f, h)
  local hz = 110 * h
  local f1 = 300 + 520 * f
  local f2 = 800 + 1500 * math.sin(f * 3.14159) * 0.5 + 900 * f
  local f3 = 2500 + 300 * f
  local a = math.exp(-((hz - f1) / 160) ^ 2) + 0.65 * math.exp(-((hz - f2) / 280) ^ 2) + 0.3 * math.exp(-((hz - f3) / 400) ^ 2)
  return a / (h ^ 0.55) * 0.6 + 0.02 / h
end)
function init(v)
  v.a = vv.osc(v, {wave="aurorachoir", level=0.24, pos=v.p.vowel, unison=4, spread=v.p.detune, width=0.9})
  v.b = vv.osc(v, {wave="aurorachoir", level=0.16, pos=v.p.vowel, ratio=2, unison=3, spread=v.p.detune * 0.8, width=0.9})
  v.n = vv.osc(v, {wave="noise", level=0})
  v.l = vv.mod(v, {kind="lfo", shape="smooth", rate=v.p.scan, bipolar=true})
  v.k = vv.mod(v, {kind="lfo", shape="tri", rate=v.p.scan * 1.7, bipolar=false})
end
function control(v)
  v.l.rate = v.p.scan  v.k.rate = v.p.scan * 1.7
  v.a.pos = vv.clamp(v.p.vowel + v.l.out * 0.5 * v.p.depth, 0, 1)
  v.b.pos = vv.clamp(1 - v.a.pos * 0.8, 0, 1)
  v.a.spread = v.p.detune  v.b.spread = v.p.detune * 0.8
  v.n.level = v.p.breath * 0.02 * (0.5 + v.k.out)
end
)LUASRC" },
{ "amb_deep_space_hum", R"LUASRC(-- Deep Space Hum: a low FM drone with a slowly breathing index and a lorenz-driven sub
instrument{ name="Deep Space Hum", category="Drones", author="Vektur", version=1, tags={"ambient","drone","fm","space","dark"},
  desc="A soft two-operator phase-modulation hum with a fifth below, a slowly drifting modulation index and chaotic detune. Dark, huge and static, like a distant engine room." }
param("index", {label="FM index", min=0, max=6, default=1.6})
param("ratio", {label="Mod ratio", min=0.5, max=4, default=1.5})
param("drift", {label="Drift", min=0.01, max=0.4, default=0.06, unit="Hz"})
param("sub",   {label="Sub", min=0, max=1, default=0.5})
param("hiss",  {label="Hiss", min=0, max=1, default=0.15})
function init(v)
  v.m = vv.osc(v, {wave="sine", ratio=v.p.ratio, out=false})
  v.c = vv.osc(v, {wave="sine", fmsrc=1, fm=v.p.index, level=0.3, unison=2, spread=5, width=0.9})
  v.d = vv.osc(v, {wave="tri", ratio=0.75, level=0.12, pan=-0.4})
  v.s = vv.osc(v, {wave="sine", ratio=0.5, level=0.2})
  v.h = vv.osc(v, {wave="noise", level=0})
  v.l = vv.mod(v, {kind="lfo", shape="smooth", rate=v.p.drift, bipolar=false})
  v.k = vv.mod(v, {kind="chaos", shape="lorenz", rate=0.2})
end
function control(v)
  v.l.rate = v.p.drift
  v.m.ratio = v.p.ratio
  v.c.fm = v.p.index * (0.3 + 0.7 * v.l.out)
  v.c.detune = v.k.out * 4
  v.d.level = 0.1 + 0.06 * v.l.out
  v.s.level = 0.25 * v.p.sub
  v.h.level = v.p.hiss * 0.015 * (0.3 + v.l.out)
end
)LUASRC" },
{ "dnb_jumpup_wob", R"LUASRC(-- Jump Up Wob: bouncy mid-range wobble bass with a tempo-synced LFO driving fold and drive
instrument{ name="Jump Up Wob", category="Drum and Bass", author="Vektur", version=1, tags={"dnb","jump up","wobble","bass"},
  desc="Saw and square stack, a sub, and a tempo-synced wobble that folds and drives the signal. Choose the wobble division, depth and growl." }
param("div",   {label="Wobble rate", kind="menu", options={"1/4","1/8","1/16","1/8T","1/4T"}, default=1})
param("depth", {label="Wobble depth", min=0, max=1, default=0.7})
param("growl", {label="Growl", min=0, max=1, default=0.55})
param("sub",   {label="Sub", min=0, max=1, default=0.6})
param("shape", {label="Wobble shape", kind="menu", options={"Sine","Tri","Square","Ramp"}, default=1})
local DIVS = {"1/4","1/8","1/16","1/8T","1/4T"}
local SHAPES = {"sine","tri","square","rsaw"}
function init(v)
  v.a = vv.osc(v, {wave="saw", level=0.32, unison=3, spread=14, width=0.5, fold=0.2, drive=0.3})
  v.b = vv.osc(v, {wave="square", level=0.18, detune=-7, fold=0.1})
  v.s = vv.osc(v, {wave="sine", level=0.4, ratio=0.5})
  v.w = vv.mod(v, {kind="lfo", shape=SHAPES[v.p.shape + 1], div=DIVS[v.p.div + 1], bipolar=false})
end
function control(v)
  local w = v.w.out * v.p.depth
  v.a.fold = vv.clamp(0.1 + w * 0.8 * v.p.growl, 0, 1)
  v.a.drive = 0.2 + v.p.growl * 0.6
  v.b.fold = vv.clamp(w * 0.5, 0, 1)
  v.a.level = 0.18 + 0.2 * (1 - w * 0.5)
  v.s.level = 0.5 * v.p.sub
end
)LUASRC" },
{ "dnb_jumpup_growl", R"LUASRC(-- Jump Up Growl: phase-modulation growl bass whose index is chopped by a triplet LFO
instrument{ name="Jump Up Growl", category="Drum and Bass", author="Vektur", version=1, tags={"dnb","jump up","growl","fm","bass"},
  desc="A saw carrier phase-modulated by an inharmonic operator. The index is chopped by a tempo-synced LFO and a fast envelope for the classic vowel-growl bark." }
param("index", {label="FM index", min=0, max=12, default=5})
param("ratio", {label="Mod ratio", min=0.5, max=6, default=2})
param("chop",  {label="Chop depth", min=0, max=1, default=0.7})
param("bark",  {label="Bark decay", min=0.03, max=0.6, default=0.18, unit="s"})
param("div",   {label="Chop rate", kind="menu", options={"1/8","1/16","1/4T","1/8T"}, default=2})
local DIVS = {"1/8","1/16","1/4T","1/8T"}
function init(v)
  v.m = vv.osc(v, {wave="sine", ratio=v.p.ratio, out=false})
  v.c = vv.osc(v, {wave="saw", fmsrc=1, fm=v.p.index, level=0.34, unison=2, spread=9})
  v.s = vv.osc(v, {wave="sine", ratio=0.5, level=0.4})
  v.l = vv.mod(v, {kind="lfo", shape="tri", div=DIVS[v.p.div + 1], bipolar=false})
  v.e = vv.mod(v, {kind="env", a=0.002, d=v.p.bark, s=0.25, r=0.15})
end
function control(v)
  v.m.ratio = v.p.ratio
  v.e.d = v.p.bark
  v.c.fm = v.p.index * (0.2 + 0.8 * (v.l.out * v.p.chop + (1 - v.p.chop) * 0.6)) * (0.6 + 0.6 * v.e.out)
  v.c.drive = 0.35
end
)LUASRC" },
{ "dnb_jumpup_stab", R"LUASRC(-- Jump Up Stab: detuned saw and square chord stab with a quick pitch drop and a snappy level envelope
instrument{ name="Jump Up Stab", category="Drum and Bass", author="Vektur", version=1, tags={"dnb","jump up","stab","chord","rave"},
  desc="Three detuned saws and a pulse an octave up with a short pitch drop into the note. Plays full, punchy stabs; add a minor third and a fifth with the chord menu." }
param("chord", {label="Chord", kind="menu", options={"Single","Minor","Major","Fifth","Minor 7th"}, default=1})
param("drop",  {label="Pitch drop", min=0, max=12, default=3, unit="st"})
param("snap",  {label="Snap", min=0.03, max=0.8, default=0.16, unit="s"})
param("detune",{label="Detune", min=0, max=50, default=18, unit="ct"})
local CH = { {0}, {0,3,7}, {0,4,7}, {0,7,12}, {0,3,7,10} }
function init(v)
  v.o = {}
  local c = CH[v.p.chord + 1]
  for i = 1, #c do
    v.o[i] = vv.osc(v, {wave=(i % 2 == 1) and "saw" or "square", level=0.3 / math.sqrt(#c), semi=c[i], unison=3, spread=v.p.detune, width=0.8})
  end
  v.n = #c
  v.u = vv.osc(v, {wave="pulse", pw=0.3, level=0.1, ratio=2})
  v.e = vv.mod(v, {kind="env", a=0.001, d=v.p.snap, s=0.2, r=0.12})
end
function control(v)
  v.e.d = v.p.snap
  local d = v.p.drop * v.e.out
  local c = CH[v.p.chord + 1]
  for i = 1, v.n do
    v.o[i].semi = c[i] + d
    v.o[i].spread = v.p.detune
  end
  v.u.semi = d
  v.level = 0.35 + 0.65 * v.e.out
end
)LUASRC" },
{ "dnb_jumpup_bounce", R"LUASRC(-- Jump Up Bounce: step-sequenced octave bounce bass for rolling jump-up basslines
instrument{ name="Jump Up Bounce", category="Drum and Bass", author="Vektur", version=1, tags={"dnb","jump up","bounce","rhythm","bass"},
  desc="A sixteenth-note step sequencer on level and octave: the bass hops between the note, its octave and a gap, giving the stuttering bounce of a jump-up line. Gate, swing of accents and the grit are parameters." }
param("gate",  {label="Gate", min=0.1, max=1, default=0.7})
param("grit",  {label="Grit", min=0, max=1, default=0.5})
param("hop",   {label="Octave hop", min=0, max=1, default=0.6})
param("sub",   {label="Sub", min=0, max=1, default=0.5})
local LV  = {1, 0.0, 0.8, 1, 0.0, 0.9, 0.7, 0.0, 1, 0.8, 0.0, 1, 0.6, 0.9, 0.0, 0.8}
local OCT = {0, 0, 12, 0, 0, 0, 12, 0, 0, 12, 0, 0, 0, 12, 0, 7}
function init(v)
  v.a = vv.osc(v, {wave="saw", level=0.34, unison=3, spread=12, width=0.4, drive=0.4})
  v.b = vv.osc(v, {wave="square", level=0.16, detune=5, fold=0.2})
  v.s = vv.osc(v, {wave="sine", ratio=0.5, level=0.4})
end
function control(v)
  local pos = v.beat * 4
  local idx = math.floor(pos)
  local ph = pos - idx
  local k = (idx % 16) + 1
  local g = (ph < v.p.gate) and 1 or 0
  v.level = LV[k] * g
  local st = OCT[k] * v.p.hop
  v.a.semi = st  v.b.semi = st
  v.a.drive = 0.2 + v.p.grit * 0.7
  v.b.fold = v.p.grit * 0.5
  v.s.level = 0.5 * v.p.sub
end
)LUASRC" },
{ "cs80_blade_brass", R"LUASRC(-- CS80 Blade Brass: the sweeping, swelling brass of the Yamaha CS-80 (two layers, ribbon scoop, slow vibrato)
instrument{ name="CS80 Blade Brass", category="Virtual Analogue", author="Vektur", version=1, tags={"cs80","vangelis","brass","analog","cinematic"},
  desc="Two layers of detuned saw and pulse oscillators with a pitch scoop into every note, a slow delayed vibrato and an opening brightness swell. A tribute to the CS-80 sound of 1982 film scores. Layer filter and envelope shape the attack." }
param("scoop",  {label="Ribbon scoop", min=0, max=300, default=70, unit="ct"})
param("swell",  {label="Swell time", min=0.05, max=2, default=0.6, unit="s"})
param("vib",    {label="Vibrato", min=0, max=40, default=9, unit="ct"})
param("vrate",  {label="Vib rate", min=2, max=9, default=5.2, unit="Hz"})
param("detune", {label="Layer detune", min=0, max=40, default=14, unit="ct"})
param("pw",     {label="Pulse width", min=0.1, max=0.5, default=0.32})
function init(v)
  v.a1 = vv.osc(v, {wave="saw", level=0.2, detune=-v.p.detune, pan=-0.35})
  v.a2 = vv.osc(v, {wave="saw", level=0.2, detune=v.p.detune, pan=0.35})
  v.p1 = vv.osc(v, {wave="pulse", pw=v.p.pw, level=0.16, detune=v.p.detune * 0.5, pan=0.2})
  v.p2 = vv.osc(v, {wave="pulse", pw=v.p.pw, level=0.16, detune=-v.p.detune * 0.5, pan=-0.2, ratio=1})
  v.sw = vv.mod(v, {kind="env", a=v.p.swell, d=0.5, s=1, r=0.4})
  v.sc = vv.mod(v, {kind="env", a=0.001, d=0.18, s=0, r=0.1})
  v.vb = vv.mod(v, {kind="lfo", shape="sine", rate=v.p.vrate, bipolar=true})
  v.vd = vv.mod(v, {kind="env", a=0.9, d=0.3, s=1, r=0.2})
end
function control(v)
  v.vb.rate = v.p.vrate
  local cents = -v.p.scoop * v.sc.out + v.vb.out * v.p.vib * v.vd.out
  v.a1.detune = -v.p.detune + cents  v.a2.detune = v.p.detune + cents
  v.p1.detune = v.p.detune * 0.5 + cents  v.p2.detune = -v.p.detune * 0.5 + cents
  v.p1.pw = v.p.pw + 0.1 * v.sw.out  v.p2.pw = v.p.pw + 0.1 * v.sw.out
  local b = 0.35 + 0.65 * v.sw.out
  v.a1.drive = 0.15 * b  v.a2.drive = 0.15 * b
  v.level = 0.55 + 0.45 * v.sw.out
end
)LUASRC" },
{ "cs80_vangelis_strings", R"LUASRC(-- CS80 Vangelis Strings: lush, slowly-attacking poly strings with a gentle delayed vibrato
instrument{ name="CS80 Vangelis Strings", category="Virtual Analogue", author="Vektur", version=1, tags={"cs80","vangelis","strings","pad","analog"},
  desc="Two unison saw stacks, a pulse layer and a sub, with a delayed ensemble vibrato that grows after the note starts. Slow, wide and cinematic." }
param("detune", {label="Detune", min=0, max=40, default=17, unit="ct"})
param("width",  {label="Width", min=0, max=1, default=0.9})
param("vib",    {label="Vibrato", min=0, max=30, default=7, unit="ct"})
param("delay",  {label="Vib delay", min=0.1, max=3, default=1.1, unit="s"})
param("pulse",  {label="Pulse layer", min=0, max=1, default=0.5})
function init(v)
  v.a = vv.osc(v, {wave="saw", level=0.2, unison=5, spread=v.p.detune, width=v.p.width})
  v.b = vv.osc(v, {wave="saw", level=0.14, ratio=2, unison=3, spread=v.p.detune * 0.8, width=v.p.width})
  v.pl = vv.osc(v, {wave="pulse", pw=0.4, level=0.1, unison=2, spread=v.p.detune * 0.5, width=v.p.width})
  v.s = vv.osc(v, {wave="sine", level=0.12, ratio=0.5})
  v.l = vv.mod(v, {kind="lfo", shape="sine", rate=5.4, bipolar=true})
  v.m = vv.mod(v, {kind="lfo", shape="smooth", rate=0.18, bipolar=true})
  v.d = vv.mod(v, {kind="env", a=v.p.delay, d=0.5, s=1, r=0.3})
end
function control(v)
  local c = v.l.out * v.p.vib * v.d.out
  v.a.detune = c  v.b.detune = c  v.pl.detune = c
  v.pl.pw = 0.4 + 0.12 * v.m.out
  v.pl.level = 0.18 * v.p.pulse
  v.a.spread = v.p.detune  v.a.width = v.p.width
end
)LUASRC" },
{ "cs80_ribbon_lead", R"LUASRC(-- CS80 Ribbon Lead: expressive mono-style lead with a ribbon-controller scoop and delayed vibrato
instrument{ name="CS80 Ribbon Lead", category="Virtual Analogue", author="Vektur", version=1, tags={"cs80","vangelis","lead","ribbon","analog"},
  desc="Pulse and saw oscillators with a pitch scoop up into every note, a vibrato that blooms after a short delay and a gentle brightness swell. Turn Mono and Glide on in the LUA slot for the full ribbon feel." }
param("scoop", {label="Scoop", min=0, max=400, default=160, unit="ct"})
param("time",  {label="Scoop time", min=0.02, max=0.8, default=0.14, unit="s"})
param("vib",   {label="Vibrato", min=0, max=50, default=14, unit="ct"})
param("delay", {label="Vib delay", min=0.05, max=1.5, default=0.35, unit="s"})
param("tone",  {label="Tone", min=0, max=1, default=0.55})
function init(v)
  v.a = vv.osc(v, {wave="saw", level=0.26, detune=-6, unison=2, spread=8})
  v.b = vv.osc(v, {wave="pulse", pw=0.35, level=0.2, detune=6, ratio=1})
  v.c = vv.osc(v, {wave="saw", level=0.1, ratio=2, detune=3})
  v.sc = vv.mod(v, {kind="env", a=0.001, d=v.p.time, s=0, r=0.1})
  v.vl = vv.mod(v, {kind="lfo", shape="sine", rate=5.6, bipolar=true})
  v.vd = vv.mod(v, {kind="env", a=v.p.delay, d=0.2, s=1, r=0.2})
  v.m = vv.mod(v, {kind="lfo", shape="smooth", rate=0.4, bipolar=true})
end
function control(v)
  v.sc.d = v.p.time
  local cents = -v.p.scoop * v.sc.out + v.vl.out * v.p.vib * v.vd.out
  v.a.detune = -6 + cents  v.b.detune = 6 + cents  v.c.detune = 3 + cents
  v.b.pw = 0.3 + 0.1 * v.m.out + 0.1 * v.p.tone
  v.c.level = 0.14 * v.p.tone
end
)LUASRC" },
{ "cs80_poly_pad", R"LUASRC(-- CS80 Poly Pad: warm slow-PWM pad with a chorus-like detune and a breathing sub
instrument{ name="CS80 Poly Pad", category="Virtual Analogue", author="Vektur", version=1, tags={"cs80","vangelis","pad","pwm","warm"},
  desc="Four pulse oscillators on independent slow pulse-width LFOs, detuned like an analog ensemble, over a sine sub. Warm and wide; a very slow pad for Vangelis-style chords." }
param("rate",   {label="PWM rate", min=0.03, max=1.5, default=0.19, unit="Hz"})
param("depth",  {label="PWM depth", min=0, max=0.42, default=0.3})
param("detune", {label="Detune", min=0, max=35, default=13, unit="ct"})
param("sub",    {label="Sub", min=0, max=1, default=0.5})
param("bright", {label="Brightness", min=0, max=1, default=0.5})
function init(v)
  v.o = {} v.l = {}
  for i = 1, 4 do
    v.o[i] = vv.osc(v, {wave="pulse", pw=0.5, level=0.17, detune=(i - 2.5) * v.p.detune * 0.8, pan=(i - 2.5) * 0.45})
    v.l[i] = vv.mod(v, {kind="lfo", shape="tri", rate=v.p.rate * (0.8 + i * 0.17), phase=i * 0.25})
  end
  v.s = vv.osc(v, {wave="sine", level=0.18, ratio=0.5})
  v.hi = vv.osc(v, {wave="saw", level=0.04, ratio=2})
end
function control(v)
  for i = 1, 4 do
    v.l[i].rate = v.p.rate * (0.8 + i * 0.17)
    v.o[i].pw = 0.5 + v.l[i].out * v.p.depth
    v.o[i].detune = (i - 2.5) * v.p.detune * 0.8
  end
  v.s.level = 0.28 * v.p.sub
  v.hi.level = 0.1 * v.p.bright
end
)LUASRC" },
{ "hoover_classic", R"LUASRC(-- Hoover Classic: the 1991 "Mentasm" rave hoover: detuned pulse stack with a pitch dive and swirling PWM
instrument{ name="Hoover Classic", category="Virtual Analogue", author="Vektur", version=1, tags={"hoover","rave","hardcore","mentasm","lead"},
  desc="Several heavily detuned pulse-width-modulated oscillators dive down from high above the note into it, with a sub and a growly drive. The signature scream of early rave and hardcore." }
param("dive",   {label="Pitch dive", min=0, max=24, default=12, unit="st"})
param("speed",  {label="Dive time", min=0.03, max=1.5, default=0.28, unit="s"})
param("detune", {label="Detune", min=0, max=80, default=38, unit="ct"})
param("pwm",    {label="PWM depth", min=0, max=0.45, default=0.32})
param("growl",  {label="Growl", min=0, max=1, default=0.5})
function init(v)
  v.o = {}
  for i = 1, 4 do
    v.o[i] = vv.osc(v, {wave="pulse", pw=0.5, level=0.1, unison=5, spread=v.p.detune, width=1, detune=(i - 2.5) * 11, pan=(i - 2.5) * 0.5, drive=0.3})
  end
  v.s = vv.osc(v, {wave="saw", level=0.14, ratio=0.5, unison=3, spread=12})
  v.d = vv.mod(v, {kind="env", a=0.001, d=v.p.speed, s=0, r=0.2})
  v.w = vv.mod(v, {kind="lfo", shape="tri", rate=0.7, bipolar=true})
  v.w2 = vv.mod(v, {kind="lfo", shape="tri", rate=1.1, bipolar=true, phase=0.4})
end
function control(v)
  v.d.d = v.p.speed
  local dv = v.d.out * v.p.dive
  for i = 1, 4 do
    v.o[i].semi = dv
    v.o[i].spread = v.p.detune
    v.o[i].pw = 0.5 + ((i % 2 == 0) and v.w.out or v.w2.out) * v.p.pwm
    v.o[i].drive = 0.15 + 0.6 * v.p.growl
  end
  v.s.spread = v.p.detune * 0.4
end
)LUASRC" },
{ "hoover_rave", R"LUASRC(-- Hoover Rave: hard-sync hoover with a sweeping slave oscillator and a long pitch fall
instrument{ name="Hoover Rave", category="Virtual Analogue", author="Vektur", version=1, tags={"hoover","rave","sync","hardcore","lead"},
  desc="A pulse master and a hard-synced saw slave whose ratio sweeps down as the note decays, giving the tearing sync-sweep hoover. Pitch dive and a widely detuned unison are included." }
param("sweep",  {label="Sync sweep", min=0, max=8, default=3.5})
param("fall",   {label="Fall time", min=0.05, max=2, default=0.5, unit="s"})
param("dive",   {label="Pitch dive", min=0, max=12, default=5, unit="st"})
param("detune", {label="Detune", min=0, max=80, default=30, unit="ct"})
param("body",   {label="Body", min=0, max=1, default=0.5})
function init(v)
  v.m = vv.osc(v, {wave="pulse", pw=0.45, level=0.18, unison=3, spread=v.p.detune, width=1})
  v.s = vv.osc(v, {wave="saw", level=0.2, ratio=2, sync="hard", syncsrc=1, unison=3, spread=v.p.detune, width=1})
  v.th = vv.osc(v, {wave="pulse", pw=0.3, level=0.1, ratio=0.5, unison=2, spread=v.p.detune * 0.5})
  v.e = vv.mod(v, {kind="env", a=0.001, d=v.p.fall, s=0.15, r=0.25})
  v.l = vv.mod(v, {kind="lfo", shape="tri", rate=0.9, bipolar=true})
end
function control(v)
  v.e.d = v.p.fall
  v.s.ratio = 1 + v.p.sweep * (0.15 + 0.85 * v.e.out)
  local dv = v.p.dive * v.e.out
  v.m.semi = dv  v.s.semi = dv  v.th.semi = dv
  v.m.pw = 0.45 + 0.2 * v.l.out
  v.m.spread = v.p.detune  v.s.spread = v.p.detune
  v.th.level = 0.2 * v.p.body
end
)LUASRC" },
{ "hoover_reese", R"LUASRC(-- Hoover Reese: a dark, slow-diving hoover bass-lead hybrid with a phasing detuned saw core
instrument{ name="Hoover Reese", category="Drum and Bass", author="Vektur", version=1, tags={"hoover","reese","dnb","bass","lead"},
  desc="Two big detuned saw stacks beating against each other (the Reese core) with a slow pitch swoop, a pulse layer on a PWM LFO and a sub. Use it as a bass or play an octave up as a lead." }
param("beat",   {label="Reese beat", min=0, max=60, default=26, unit="ct"})
param("swoop",  {label="Swoop", min=0, max=12, default=4, unit="st"})
param("time",   {label="Swoop time", min=0.05, max=1.5, default=0.35, unit="s"})
param("grit",   {label="Grit", min=0, max=1, default=0.45})
param("sub",    {label="Sub", min=0, max=1, default=0.6})
function init(v)
  v.a = vv.osc(v, {wave="saw", level=0.22, detune=-v.p.beat, unison=3, spread=10, width=0.6, drive=0.3})
  v.b = vv.osc(v, {wave="saw", level=0.22, detune=v.p.beat, unison=3, spread=10, width=0.6, drive=0.3})
  v.c = vv.osc(v, {wave="pulse", pw=0.5, level=0.1, ratio=2})
  v.s = vv.osc(v, {wave="sine", level=0.4, ratio=0.5})
  v.e = vv.mod(v, {kind="env", a=0.001, d=v.p.time, s=0, r=0.2})
  v.l = vv.mod(v, {kind="lfo", shape="tri", rate=0.3, bipolar=true})
  v.m = vv.mod(v, {kind="lfo", shape="smooth", rate=0.11, bipolar=true})
end
function control(v)
  v.e.d = v.p.time
  local dv = v.e.out * v.p.swoop
  v.a.semi = dv  v.b.semi = dv  v.c.semi = dv
  v.a.detune = -v.p.beat * (1 + 0.25 * v.m.out)
  v.b.detune = v.p.beat * (1 + 0.25 * v.m.out)
  v.c.pw = 0.5 + 0.3 * v.l.out
  v.a.drive = 0.1 + 0.7 * v.p.grit  v.b.drive = v.a.drive
  v.s.level = 0.5 * v.p.sub
end
)LUASRC" },
};
static const size_t kLuaFactoryCount = 31;
}  // namespace vv
