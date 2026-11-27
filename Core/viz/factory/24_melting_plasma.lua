visual{ name="Melting Plasma", category="Psychedelic", author="Vektur", shader="plasma_field",
  desc="Five drifting plasma cores that warp and melt into each other in loud, saturated colour." }
param("hue",{label="Hue",min=0,max=1,default=0,step=0.001,group="Colour"})
param("saturation",{label="Saturation",min=0,max=2,default=1.8,group="Colour"})
param("brightness",{label="Brightness",min=0.2,max=2,default=1,group="Colour"})
param("contrast",{label="Contrast",min=0.5,max=2,default=1,group="Colour"})
param("palette",{label="Palette shift",min=0,max=1,default=0.35,step=0.001,group="Colour"})
param("freqA",{label="Field frequency A",min=0.5,max=6,default=3.2,group="Geometry and movement",band="bass",depth=0.4})
param("freqB",{label="Field frequency B",min=0.5,max=6,default=3.8,group="Geometry and movement",band="mid",depth=0.4})
param("freqC",{label="Field frequency C",min=0.5,max=6,default=2.9,group="Geometry and movement",band="high",depth=0.4})
param("cores",{label="Metaball cores",min=0,max=4,default=5,step=1,group="Particles and textures"})
param("sharp",{label="Sharpness",min=0,max=4,default=0.8,group="Particles and textures"})
param("warp",{label="Domain warp",min=0,max=1.5,default=1.1,group="Geometry and movement"})
param("speed",{label="Speed",min=0,max=3,default=1.4,group="Animation"})
param("bloom",{label="Bloom",min=0,max=2,default=0.5,group="Post-processing"})
param("vignette",{label="Vignette",min=0,max=1,default=0.3,group="Post-processing"})
param("grain",{label="Film grain",min=0,max=1,default=0.05,group="Post-processing"})
param("chroma",{label="Chromatic aberration",min=0,max=2,default=0.8,group="Chromatic",band="highmid",depth=0.3})
local function grade(p)
  gfx.post("hue", p.hue) gfx.post("saturation", p.saturation) gfx.post("brightness", p.brightness) gfx.post("contrast", p.contrast)
  gfx.post("bloom", p.bloom) gfx.post("vignette", p.vignette) gfx.post("grain", p.grain)
  gfx.post("chroma", p.chroma)
end
local ph = 0
function update(dt, a, p)
  ph = ph + dt * p.speed * (1 + a.level * 2)
  gfx.uniforms(0, ph, p.freqA, p.freqB, p.freqC, p.palette + a.centroid * 0.2, p.cores, p.sharp * (1 + a.beat * 0.5), p.warp)
  grade(p)
end
