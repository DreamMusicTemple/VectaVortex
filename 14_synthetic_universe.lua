visual{ name="Synthetic Universe", category="Space", author="Vektur", shader="synthetic_universe",
  desc="A nebula shader under a Lua-simulated sky: parallax stars, orbiting planets sized by the bands, comets on the beat." }
param("hue",{label="Hue",min=0,max=1,default=0,step=0.001,group="Colour"})
param("saturation",{label="Saturation",min=0,max=2,default=1,group="Colour"})
param("brightness",{label="Brightness",min=0.2,max=2,default=1,group="Colour"})
param("contrast",{label="Contrast",min=0.5,max=2,default=1,group="Colour"})
param("palette",{label="Palette shift",min=0,max=1,default=0.7,step=0.001,group="Colour"})
param("nebula",{label="Nebula density",min=0.3,max=2.5,default=1.4,group="Particles and textures",band="mid",depth=0.3})
param("nebulaLight",{label="Nebula brightness",min=0.2,max=3,default=1.2,group="Particles and textures",band="bass",depth=0.4})
param("detail",{label="Nebula detail",min=2,max=6,default=5,step=1,group="Particles and textures"})
param("stars",{label="Star count",min=0.1,max=1,default=1,group="Particles and textures"})
param("planets",{label="Planets",min=0,max=8,default=5,step=1,group="Geometry and movement"})
param("orbit",{label="Orbit scale",min=0.2,max=1.2,default=0.7,group="Geometry and movement",band="sub",depth=0.2})
param("planetSize",{label="Planet size",min=2,max=30,default=12,group="Geometry and movement",band="highmid",depth=0.5})
param("fly",{label="Fly-through speed",min=0,max=3,default=0.5,group="Animation"})
param("swirl",{label="Nebula swirl",min=0,max=2,default=0.6,group="Animation"})
param("comets",{label="Comets on beat",min=0,max=2,default=1,group="Audio reactivity"})
param("bloom",{label="Bloom",min=0,max=2,default=1.0,group="Post-processing"})
param("vignette",{label="Vignette",min=0,max=1,default=0.45,group="Post-processing"})
param("grain",{label="Film grain",min=0,max=1,default=0.07,group="Post-processing"})
param("chroma",{label="Chromatic aberration",min=0,max=2,default=0.2,group="Chromatic",band="highmid",depth=0.3})
local function grade(p)
  gfx.post("hue", p.hue) gfx.post("saturation", p.saturation) gfx.post("brightness", p.brightness) gfx.post("contrast", p.contrast)
  gfx.post("bloom", p.bloom) gfx.post("vignette", p.vignette) gfx.post("grain", p.grain)
  gfx.post("chroma", p.chroma)
end
local NS = 260
local sx, sy, sz = {}, {}, {}
viz.seed(11)
for i = 1, NS do sx[i] = viz.rand() * 2 - 1; sy[i] = viz.rand() * 2 - 1; sz[i] = viz.rand() end
local ph, orbitT, kick, nStars = 0, 0, 0, NS
local cometX, cometY, cometVX, cometVY, cometLife = {}, {}, {}, {}, {}
for i = 1, 6 do cometLife[i] = 0; cometX[i] = 0; cometY[i] = 0; cometVX[i] = 0; cometVY[i] = 0 end
function update(dt, a, p)
  kick = math.max(kick * math.exp(-dt * 6), a.beat)
  ph = ph + dt * p.swirl * (1 + a.full)
  orbitT = orbitT + dt * (0.25 + a.mid * 0.5)
  nStars = math.floor(NS * p.stars * (0.4 + 0.2 * host.quality))
  for i = 1, nStars do
    sz[i] = sz[i] - dt * p.fly * (0.05 + a.bass * 0.25)
    if sz[i] <= 0.02 then sz[i] = 1; sx[i] = viz.rand() * 2 - 1; sy[i] = viz.rand() * 2 - 1 end
  end
  if a.beat > 0.9 and p.comets > 0.05 then
    for i = 1, 6 do
      if cometLife[i] <= 0 then
        local ang = viz.rand() * 6.2831853
        cometX[i] = math.cos(ang) * 0.6; cometY[i] = math.sin(ang) * 0.6
        cometVX[i] = -math.cos(ang + 0.5) * (0.4 + viz.rand() * 0.4); cometVY[i] = -math.sin(ang + 0.5) * (0.4 + viz.rand() * 0.4)
        cometLife[i] = 1.2
        break
      end
    end
  end
  for i = 1, 6 do if cometLife[i] > 0 then cometLife[i] = cometLife[i] - dt; cometX[i] = cometX[i] + cometVX[i] * dt; cometY[i] = cometY[i] + cometVY[i] * dt end end
  gfx.uniforms(0, ph, p.palette, p.nebula, p.nebulaLight * (1 + kick * 0.3), p.swirl, math.min(p.detail, 2 + host.quality * 1.3))
end
function render(c, w, h, p)
  local cx, cy = w * 0.5, h * 0.5
  c.wipe()
  c.blend("add")
  for i = 1, nStars do
    local z = sz[i]
    local px, py = cx + sx[i] / z * h * 0.12, cy + sy[i] / z * h * 0.12
    if px > 0 and px < w and py > 0 and py < h then
      local b = (1 - z) * 0.9 + 0.1
      local r, g, bl = color.hsv(p.palette + 0.05 + sz[i] * 0.2, 0.25, 1)
      c.color(r, g, bl, b)
      c.dot(px, py, 1.2 + (1 - z) * 3 * h / 540)
    end
  end
  local np = math.floor(p.planets)
  for k = 1, np do
    local f = k / np
    local rad = (0.12 + f * 0.36) * p.orbit * h
    local ang = orbitT / (0.4 + f) + k * 1.7
    local e = audio.spec[((k * 4) % 32) + 1]
    local px, py = cx + math.cos(ang) * rad * 1.4, cy + math.sin(ang) * rad * 0.6
    c.blend("normal")
    c.color(1, 1, 1, 0.08)
    c.ring(cx, cy, rad * 1.4, 1)
    local r, g, b = color.hsv(p.palette + f * 0.4, 0.55, 0.9)
    c.color(r, g, b, 0.95)
    c.circle(px, py, p.planetSize * (0.5 + f * 0.6 + e) * h / 540)
    c.blend("add")
    c.color(r, g, b, 0.5)
    c.dot(px, py, p.planetSize * (1.6 + e * 2) * h / 540)
  end
  for i = 1, 6 do
    if cometLife[i] > 0 then
      local px, py = cx + cometX[i] * h * 0.5, cy + cometY[i] * h * 0.5
      c.color(0.8, 0.9, 1, cometLife[i] * 0.7)
      c.line(px, py, px - cometVX[i] * h * 0.25, py - cometVY[i] * h * 0.25, 2 * h / 540)
      c.dot(px, py, 8 * h / 540)
    end
  end
  grade(p)
end
