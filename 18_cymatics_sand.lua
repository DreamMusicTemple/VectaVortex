visual{ name="Cymatics - Resonant Sand", category="Cymatics", author="Vektur", shader="sand_plate",
  desc="Up to hundreds of thousands of grains on a vibrating circular plate. Grains slide toward the nodal lines of the same Bessel modes, scatter when the sound changes and settle when it holds." }
param("hue",{label="Hue",min=0,max=1,default=0,step=0.001,group="Colour"})
param("saturation",{label="Saturation",min=0,max=2,default=1,group="Colour"})
param("brightness",{label="Brightness",min=0.2,max=2,default=1,group="Colour"})
param("contrast",{label="Contrast",min=0.5,max=2,default=1,group="Colour"})
param("palette",{label="Colour shift",min=0,max=1,default=0.08,step=0.001,group="Colour"})
param("tint",{label="Grain tint",min=0,max=1,default=0.15,group="Colour"})
param("density",{label="Particle density",min=0.1,max=1,default=1,group="Particles and textures"})
param("grainSize",{label="Grain size",min=1,max=4,default=1.6,group="Particles and textures"})
param("mass",{label="Particle mass",min=0.2,max=3,default=1,group="Particles and textures"})
param("friction",{label="Particle friction",min=0,max=1,default=0.5,group="Particles and textures"})
param("vibration",{label="Vibration intensity",min=0,max=2,default=1,group="Audio reactivity",band="bass",depth=0.5})
param("resonance",{label="Plate resonance",min=0.4,max=2.5,default=1,group="Geometry and movement",band="sub",depth=0.2})
param("nodal",{label="Nodal complexity",min=1,max=3,default=3,step=1,group="Geometry and movement"})
param("symmetry",{label="Radial symmetry",min=1,max=16,default=6,step=1,group="Geometry and movement"})
param("harmonic",{label="Harmonic pattern",min=0,max=2,default=1,group="Geometry and movement",band="mid",depth=0.5})
param("freqOffset",{label="Wave frequency offset",min=-1,max=1,default=0,group="Geometry and movement"})
param("accumulation",{label="Accumulation",min=0,max=3,default=1.2,group="Particles and textures"})
param("settling",{label="Settling speed",min=0,max=1,default=0.6,group="Animation"})
param("scatter",{label="Scatter",min=0,max=2,default=0.8,group="Animation",band="high",depth=0.5})
param("persistence",{label="Pattern persistence",min=0,max=1,default=0.5,group="Animation"})
param("speed",{label="Simulation speed",min=0.2,max=2,default=1,group="Animation"})
param("depth",{label="Surface depth",min=0,max=2,default=1,group="Chromatic"})
param("lighting",{label="Lighting",min=0,max=2,default=1,group="Chromatic"})
param("glowMove",{label="Motion glow",min=0,max=2,default=0.5,group="Chromatic"})
param("plate",{label="Plate size",min=0.4,max=1,default=0.88,group="Geometry and movement"})
param("freeze",{label="Freeze pattern",kind="switch",default=0,group="Animation"})
param("freezeColor",{label="Animate lighting and colour while frozen",kind="switch",default=1,group="Animation"})
param("bloom",{label="Bloom",min=0,max=2,default=0.45,group="Post-processing"})
param("vignette",{label="Vignette",min=0,max=1,default=0.4,group="Post-processing"})
param("grain",{label="Film grain",min=0,max=1,default=0.03,group="Post-processing"})
local function grade(p)
  gfx.post("hue", p.hue) gfx.post("saturation", p.saturation) gfx.post("brightness", p.brightness) gfx.post("contrast", p.contrast)
  gfx.post("bloom", p.bloom) gfx.post("vignette", p.vignette) gfx.post("grain", p.grain)
end
local ks, A0, A1, A2, E = 5, 0, 0, 0, 0
local ph = { 0, 0, 0 }
local colorPh, stab, prevFlux = 0, 1, 0
local held = false
local function ease(cur, target, dt, tau) return cur + (target - cur) * (1 - math.exp(-dt / tau)) end
function update(dt, a, p)
  local frozen = p.freeze > 0.5
  local vib, dts, st = 0, 1, 0
  if not frozen or not held then
    local f = math.max(a.peakhz, 30)
    local fn = math.log(f / 30) / math.log(2000 / 30)
    if fn < 0 then fn = 0 elseif fn > 1 then fn = 1 end
    local kT = (2.5 + (fn + p.freqOffset * 0.5) * 9) * p.resonance
    if kT < 1.5 then kT = 1.5 end
    if a.level < 0.003 then kT = (4.5 + math.sin(a.t * 0.17) * 0.3) * p.resonance end
    -- persistence: a slow plate keeps its pattern when the pitch wobbles
    local tau = 0.05 + p.persistence * 1.6
    ks = ease(ks, kT, dt, tau)
    A0 = ease(A0, 0.5 + a.sub * 0.8 + a.bass * 0.8, dt, tau * 0.5)
    A1 = ease(A1, p.harmonic * (0.2 + a.mid * 1.2), dt, tau * 0.5)
    A2 = ease(A2, p.harmonic * (0.1 + a.highmid * 1.0 + a.high * 0.4), dt, tau * 0.5)
    for i = 1, 3 do ph[i] = ph[i] + dt * 0.4 * i * (i % 2 == 0 and 1 or -1) * 0 end
    -- vibration energy: loud and changing sound shakes the sand; a steady tone lets it settle
    local drive = p.vibration * (0.15 + a.level * 3 + a.sub + a.bass * 0.6)
    local changing = math.min(1, math.abs(a.flux) * 5 + a.beat)
    stab = ease(stab, 1 - changing, dt, 0.5 + p.settling)
    local target = drive * (1 - p.settling * stab * 0.92)
    if target > E then E = ease(E, target, dt, 0.05) else E = ease(E, target, dt, 0.3) end
    held = true
    vib = E; st = p.settling * stab
  else
    vib = 0; dts = 0; st = 1
  end
  if not frozen or p.freezeColor > 0.5 then colorPh = colorPh + dt * 0.1 end
  local n0 = math.floor(p.symmetry)
  gfx.uniform(1, math.floor(p.nodal))
  gfx.uniforms(2, n0, ks, A0, ph[1], math.floor(p.symmetry / 2) + 1, ks * 1.58, A1, ph[2], 0, ks * 0.62, A2, ph[3])
  gfx.uniform(14, p.palette + colorPh)
  gfx.uniforms(18, p.accumulation * (0.4 + stab * 0.6) + 0.2, vib, 1.2 + p.friction * 7, p.scatter, p.mass, st, dts * p.speed, p.depth, p.tint, p.glowMove, p.grainSize, p.plate, p.density, p.lighting)
  grade(p)
end
