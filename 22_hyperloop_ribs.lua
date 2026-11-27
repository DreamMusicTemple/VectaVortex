visual{ name="Hyperloop Ribs", category="Tunnels", author="Vektur", shader="infinite_tunnel",
  desc="A round, wide-ribbed pipe that rolls and wobbles as it flies. Warm amber lights, a thin haze." }
param("hue",{label="Hue",min=0,max=1,default=0,step=0.001,group="Colour"})
param("saturation",{label="Saturation",min=0,max=2,default=1,group="Colour"})
param("brightness",{label="Brightness",min=0.2,max=2,default=1,group="Colour"})
param("contrast",{label="Contrast",min=0.5,max=2,default=1,group="Colour"})
param("palette",{label="Palette shift",min=0,max=1,default=0.3,step=0.001,group="Colour"})
param("speed",{label="Flight speed",min=0,max=4,default=1.8,group="Geometry and movement",band="bass",depth=0.5})
param("shape",{label="Shape (round to square)",min=0,max=1,default=0.1,group="Geometry and movement"})
param("ribs",{label="Ribs per unit",min=0.5,max=6,default=1.2,group="Geometry and movement"})
param("segments",{label="Wall segments",min=3,max=24,default=5,step=1,group="Geometry and movement"})
param("roll",{label="Roll speed",min=-1.5,max=1.5,default=0.9,group="Animation"})
param("wobble",{label="Wall wobble",min=0,max=2,default=1.2,group="Animation",band="mid",depth=0.5})
param("lights",{label="Neon intensity",min=0,max=3,default=0.6,group="Particles and textures",band="high",depth=0.6})
param("fog",{label="Fog",min=0,max=0.4,default=0.2,group="Particles and textures"})
param("flash",{label="Beat flash",min=0,max=1,default=0.4,group="Audio reactivity"})
param("bloom",{label="Bloom",min=0,max=2,default=0.7,group="Post-processing"})
param("vignette",{label="Vignette",min=0,max=1,default=0.45,group="Post-processing"})
param("grain",{label="Film grain",min=0,max=1,default=0.06,group="Post-processing"})
param("chroma",{label="Chromatic aberration",min=0,max=2,default=0.35,group="Chromatic",band="highmid",depth=0.3})
local function grade(p)
  gfx.post("hue", p.hue) gfx.post("saturation", p.saturation) gfx.post("brightness", p.brightness) gfx.post("contrast", p.contrast)
  gfx.post("bloom", p.bloom) gfx.post("vignette", p.vignette) gfx.post("grain", p.grain)
  gfx.post("chroma", p.chroma)
end
local travel, rollPh, kick = 0, 0, 0
function update(dt, a, p)
  kick = math.max(kick * math.exp(-dt * 6), a.beat)
  travel = travel + dt * p.speed * (1 + a.sub * 0.8)
  rollPh = rollPh + dt * p.roll
  gfx.uniforms(0, travel, rollPh, p.shape, p.ribs, p.segments, p.lights, p.palette, p.fog, p.wobble, p.flash * kick)
  grade(p)
end
