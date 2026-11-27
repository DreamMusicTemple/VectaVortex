visual{ name="Cymatics - Liquid Resonance", category="Cymatics", author="Vektur", shader="cymatics_liquid",
  desc="A circular plate of liquid vibrating in Bessel modes J(n)(kr)cos(n*theta). The dominant pitch sets the wavenumber, bands drive amplitude, symmetry sets the nodal pattern." }
param("hue",{label="Hue",min=0,max=1,default=0,step=0.001,group="Colour"})
param("saturation",{label="Saturation",min=0,max=2,default=1,group="Colour"})
param("brightness",{label="Brightness",min=0.2,max=2,default=1,group="Colour"})
param("contrast",{label="Contrast",min=0.5,max=2,default=1,group="Colour"})
param("palette",{label="Colour shift",min=0,max=1,default=0.55,step=0.001,group="Colour"})
param("colorAnim",{label="Colour animation",min=0,max=2,default=0.4,group="Colour"})
param("symmetry",{label="Circular symmetry",min=1,max=16,default=6,step=1,group="Geometry and movement"})
param("radialWaves",{label="Radial wave count",min=0,max=16,default=3,step=1,group="Geometry and movement",band="bass",depth=0.15})
param("complexity",{label="Pattern complexity",min=1,max=4,default=3,step=1,group="Geometry and movement"})
param("plate",{label="Plate size",min=0.4,max=1,default=0.88,group="Geometry and movement"})
param("resonance",{label="Resonance (wavenumber)",min=0.4,max=2.5,default=1,group="Geometry and movement",band="sub",depth=0.2})
param("amplitude",{label="Wave amplitude",min=0,max=2.5,default=1.1,group="Geometry and movement",band="bass",depth=0.5})
param("interference",{label="Interference",min=0,max=2,default=0.9,group="Geometry and movement",band="mid",depth=0.6})
param("harmonic",{label="Harmonic detail",min=0,max=2,default=0.8,group="Geometry and movement",band="highmid",depth=0.6})
param("ripple",{label="Ripple intensity",min=0,max=2,default=0.7,group="Particles and textures",band="treble",depth=0.7})
param("rippleFreq",{label="Ripple frequency",min=6,max=60,default=26,group="Particles and textures"})
param("centre",{label="Central displacement",min=0,max=2,default=0.6,group="Geometry and movement",band="sub",depth=0.7})
param("distortion",{label="Liquid distortion",min=0,max=2,default=0.4,group="Geometry and movement"})
param("viscosity",{label="Fluid viscosity",min=0,max=1,default=0.45,group="Animation"})
param("tension",{label="Surface tension",min=0,max=1,default=0.5,group="Particles and textures"})
param("geometric",{label="Organic to geometric",min=0,max=1,default=0.3,group="Particles and textures"})
param("speed",{label="Wave speed",min=0,max=3,default=0.7,group="Animation"})
param("reflection",{label="Reflection",min=0,max=1.5,default=0.9,group="Chromatic"})
param("specular",{label="Surface highlights",min=0,max=2,default=1,group="Chromatic"})
param("glow",{label="Glow",min=0,max=2,default=0.8,group="Chromatic",band="high",depth=0.4})
param("transparency",{label="Transparency",min=0,max=1,default=0.3,group="Chromatic"})
param("freeze",{label="Freeze pattern",kind="switch",default=0,group="Animation"})
param("freezeColor",{label="Animate colours while frozen",kind="switch",default=1,group="Animation"})
param("bloom",{label="Bloom",min=0,max=2,default=0.7,group="Post-processing"})
param("vignette",{label="Vignette",min=0,max=1,default=0.35,group="Post-processing"})
param("grain",{label="Film grain",min=0,max=1,default=0.04,group="Post-processing"})
param("chroma",{label="Chromatic aberration",min=0,max=2,default=0.2,group="Chromatic",band="highmid",depth=0.3})
local function grade(p)
  gfx.post("hue", p.hue) gfx.post("saturation", p.saturation) gfx.post("brightness", p.brightness) gfx.post("contrast", p.contrast)
  gfx.post("bloom", p.bloom) gfx.post("vignette", p.vignette) gfx.post("grain", p.grain)
  gfx.post("chroma", p.chroma)
end
local ks, A0, A1, A2, A3 = 5, 0, 0, 0, 0
local ph = { 0, 0, 0, 0 }
local colorPh = 0
local held = nil
local function ease(cur, target, dt, tau) return cur + (target - cur) * (1 - math.exp(-dt / tau)) end
function update(dt, a, p)
  local frozen = p.freeze > 0.5
  if frozen == false or held == nil then
    local f = math.max(a.peakhz, 30)
    local fn = math.log(f / 30) / math.log(2000 / 30)
    if fn < 0 then fn = 0 elseif fn > 1 then fn = 1 end
    local kT = (2.5 + fn * 9) * p.resonance
    if a.level < 0.003 then kT = (4 + math.sin(a.t * 0.2) * 0.4) * p.resonance end
    local tau = 0.04 + p.viscosity * 0.9
    ks = ease(ks, kT, dt, tau)
    local amp = p.amplitude
    A0 = ease(A0, amp * (0.45 + a.sub * 1.3 + a.bass * 0.9), dt, tau * 0.6)
    A1 = ease(A1, amp * p.interference * (0.18 + a.mid * 1.5), dt, tau * 0.6)
    A2 = ease(A2, amp * p.harmonic * (0.12 + a.highmid * 1.4), dt, tau * 0.6)
    A3 = ease(A3, amp * p.ripple * 0.7 * (0.1 + a.treble * 1.3 + a.high * 0.6), dt, tau * 0.6)
    for i = 1, 4 do ph[i] = ph[i] + dt * p.speed * (0.5 + i * 0.37) * (i % 2 == 0 and 1 or -1) end
    held = true
  end
  if frozen == false or p.freezeColor > 0.5 then colorPh = colorPh + dt * p.colorAnim end
  local n0 = math.floor(p.symmetry)
  local n1 = math.floor(p.radialWaves)
  local n2 = math.min(24, n0 * 2)
  local M = math.floor(p.complexity)
  local k1 = ks * 1.58
  gfx.uniform(0, colorPh)
  gfx.uniform(1, M)
  gfx.uniforms(2, n0, ks, A0, ph[1], n1, k1, A1, ph[2], n2, ks * 2.31, A2, ph[3], 0, ks * 0.62, A3, ph[4])
  gfx.uniforms(18, p.viscosity, p.tension, p.reflection, p.glow, p.palette, p.ripple, p.rippleFreq, p.centre * (0.3 + a.sub), p.distortion, p.transparency, p.symmetry, p.plate, p.specular, p.geometric)
  grade(p)
end
