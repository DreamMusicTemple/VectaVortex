visual{ name="Chromatic Dream", category="Atmospheric", author="Vektur", shader="chromatic_dream",
  desc="Soft layered clouds whose red, green and blue channels drift apart on the beat." }
param("hue",{label="Hue",min=0,max=1,default=0,step=0.001,group="Colour"})
param("saturation",{label="Saturation",min=0,max=2,default=1,group="Colour"})
param("brightness",{label="Brightness",min=0.2,max=2,default=1,group="Colour"})
param("contrast",{label="Contrast",min=0.5,max=2,default=1,group="Colour"})
param("palette",{label="Palette shift",min=0,max=1,default=0.8,step=0.001,group="Colour"})
param("split",{label="Channel split",min=0,max=2,default=0.5,group="Chromatic",band="bass",depth=0.7})
param("scale",{label="Cloud scale",min=0.5,max=4,default=1.6,group="Geometry and movement"})
param("layers",{label="Cloud layers",min=1,max=4,default=3,step=1,group="Particles and textures"})
param("softness",{label="Softness",min=0,max=2,default=1,group="Particles and textures"})
param("leaks",{label="Light leaks",min=0,max=2,default=0.8,group="Particles and textures",band="mid",depth=0.5})
param("drift",{label="Drift",min=0,max=2,default=0.6,group="Animation"})
param("speed",{label="Speed",min=0,max=3,default=0.5,group="Animation"})
param("bloom",{label="Bloom",min=0,max=2,default=0.8,group="Post-processing"})
param("vignette",{label="Vignette",min=0,max=1,default=0.3,group="Post-processing"})
param("grain",{label="Film grain",min=0,max=1,default=0.1,group="Post-processing"})
param("chroma",{label="Chromatic aberration",min=0,max=2,default=0.4,group="Chromatic",band="highmid",depth=0.3})
local function grade(p)
  gfx.post("hue", p.hue) gfx.post("saturation", p.saturation) gfx.post("brightness", p.brightness) gfx.post("contrast", p.contrast)
  gfx.post("bloom", p.bloom) gfx.post("vignette", p.vignette) gfx.post("grain", p.grain)
  gfx.post("chroma", p.chroma)
end
local ph = 0
function update(dt, a, p)
  ph = ph + dt * p.speed * (1 + a.level * 2)
  gfx.uniforms(0, ph, p.split * (0.6 + a.beat + a.bass), p.scale, p.palette, p.softness, p.leaks * (1 + a.mid), math.min(p.layers, 1 + host.quality), p.drift)
  grade(p)
end
