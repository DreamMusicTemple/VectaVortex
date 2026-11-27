visual{ name="Cyber Grid", category="Retro", author="Vektur", shader="cyber_grid",
  desc="Synthwave horizon: a scrolling neon grid, a banded sun, and eight mountains that follow the spectrum with peak hold." }
param("hue",{label="Hue",min=0,max=1,default=0,step=0.001,group="Colour"})
param("saturation",{label="Saturation",min=0,max=2,default=1,group="Colour"})
param("brightness",{label="Brightness",min=0.2,max=2,default=1,group="Colour"})
param("contrast",{label="Contrast",min=0.5,max=2,default=1,group="Colour"})
param("palette",{label="Palette shift",min=0,max=1,default=0.82,step=0.001,group="Colour"})
param("speed",{label="Scroll speed",min=0,max=4,default=1.1,group="Geometry and movement",band="bass",depth=0.4})
param("horizon",{label="Horizon height",min=-0.2,max=0.25,default=0.02,group="Geometry and movement"})
param("density",{label="Grid density",min=0.5,max=5,default=2,group="Geometry and movement"})
param("sun",{label="Sun size",min=0.1,max=0.5,default=0.27,group="Geometry and movement",band="sub",depth=0.2})
param("peaks",{label="Mountain height",min=0,max=2,default=1,group="Geometry and movement"})
param("lines",{label="Line glow",min=0,max=2,default=1,group="Particles and textures",band="high",depth=0.5})
param("sway",{label="Camera sway",min=0,max=2,default=0.5,group="Animation"})
param("fall",{label="Peak fall rate",min=0.2,max=4,default=1.5,group="Animation"})
param("bloom",{label="Bloom",min=0,max=2,default=0.9,group="Post-processing"})
param("vignette",{label="Vignette",min=0,max=1,default=0.35,group="Post-processing"})
param("grain",{label="Film grain",min=0,max=1,default=0.08,group="Post-processing"})
param("chroma",{label="Chromatic aberration",min=0,max=2,default=0.3,group="Chromatic",band="highmid",depth=0.3})
local function grade(p)
  gfx.post("hue", p.hue) gfx.post("saturation", p.saturation) gfx.post("brightness", p.brightness) gfx.post("contrast", p.contrast)
  gfx.post("bloom", p.bloom) gfx.post("vignette", p.vignette) gfx.post("grain", p.grain)
  gfx.post("chroma", p.chroma)
end
local scroll, swayPh, kick = 0, 0, 0
local hold = { 0, 0, 0, 0, 0, 0, 0, 0 }
function update(dt, a, p)
  kick = math.max(kick * math.exp(-dt * 6), a.beat)
  scroll = scroll + dt * p.speed * (0.6 + a.bass * 1.4)
  swayPh = swayPh + dt * 0.4
  for i = 1, 8 do
    local v = (a.spec[(i - 1) * 4 + 1] + a.spec[(i - 1) * 4 + 2] + a.spec[(i - 1) * 4 + 3] + a.spec[(i - 1) * 4 + 4]) * 0.25
    hold[i] = math.max(v, hold[i] - dt * p.fall * 0.5)
  end
  gfx.uniforms(0, scroll, p.horizon, p.density, p.palette, p.sun, kick, p.lines, math.sin(swayPh) * p.sway)
  for i = 1, 8 do gfx.uniform(7 + i, hold[i] * p.peaks) end
  grade(p)
end
