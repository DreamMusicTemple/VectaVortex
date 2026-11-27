visual{ name="Fractal Bloom", category="Fractals", author="Vektur", shader="fractal_bloom",
  desc="A Julia set that blossoms with the music: the orbit of c follows the low bands, orbit traps paint flowers." }
param("hue",{label="Hue",min=0,max=1,default=0,step=0.001,group="Colour"})
param("saturation",{label="Saturation",min=0,max=2,default=1,group="Colour"})
param("brightness",{label="Brightness",min=0.2,max=2,default=1,group="Colour"})
param("contrast",{label="Contrast",min=0.5,max=2,default=1,group="Colour"})
param("palette",{label="Palette shift",min=0,max=1,default=0.15,step=0.001,group="Colour"})
param("zoom",{label="Zoom (log)",min=-0.5,max=2.5,default=0.1,group="Geometry and movement"})
param("orbit",{label="Orbit radius",min=0,max=0.3,default=0.05,group="Geometry and movement",band="bass",depth=0.15})
param("orbitSpeed",{label="Orbit speed",min=0,max=1.5,default=0.25,group="Animation"})
param("spin",{label="Rotation",min=-1,max=1,default=0.1,group="Animation"})
param("iterations",{label="Iterations",min=20,max=160,default=80,step=1,group="Particles and textures"})
param("flowers",{label="Orbit-trap flowers",min=0,max=2,default=1,group="Particles and textures",band="mid",depth=0.4})
param("glow",{label="Edge glow",min=0,max=2,default=0.8,group="Particles and textures",band="high",depth=0.5})
param("pulse",{label="Beat pulse",min=0,max=1,default=0.5,group="Audio reactivity"})
param("bloom",{label="Bloom",min=0,max=2,default=0.7,group="Post-processing"})
param("vignette",{label="Vignette",min=0,max=1,default=0.35,group="Post-processing"})
param("grain",{label="Film grain",min=0,max=1,default=0.06,group="Post-processing"})
param("chroma",{label="Chromatic aberration",min=0,max=2,default=0.2,group="Chromatic",band="highmid",depth=0.3})
local function grade(p)
  gfx.post("hue", p.hue) gfx.post("saturation", p.saturation) gfx.post("brightness", p.brightness) gfx.post("contrast", p.contrast)
  gfx.post("bloom", p.bloom) gfx.post("vignette", p.vignette) gfx.post("grain", p.grain)
  gfx.post("chroma", p.chroma)
end
local ang, rot, zph, kick = 0, 0, 0, 0
function update(dt, a, p)
  kick = math.max(kick * math.exp(-dt * 5), a.beat)
  ang = ang + dt * p.orbitSpeed * (1 + a.mid)
  rot = rot + dt * p.spin
  local r = p.orbit + a.bass * 0.006 + a.sub * 0.004
  local it = math.min(p.iterations, 30 + host.quality * 45)
  gfx.uniforms(0, p.zoom + a.sub * 0.25, -0.7453 + r * math.cos(ang), 0.1127 + r * math.sin(ang * 1.13), rot, it, p.palette, p.flowers, p.glow, p.pulse * kick)
  grade(p)
end
