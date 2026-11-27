visual{ name="Neural Network", category="Networks", author="Vektur", shader="none",
  desc="A drifting graph of neurons. Close nodes link up, signals travel along the links on every beat, node size follows the spectrum." }
param("hue",{label="Hue",min=0,max=1,default=0,step=0.001,group="Colour"})
param("saturation",{label="Saturation",min=0,max=2,default=1,group="Colour"})
param("brightness",{label="Brightness",min=0.2,max=2,default=1,group="Colour"})
param("contrast",{label="Contrast",min=0.5,max=2,default=1,group="Colour"})
param("palette",{label="Palette shift",min=0,max=1,default=0.5,step=0.001,group="Colour"})
param("nodes",{label="Node count",min=0.2,max=1,default=1,group="Particles and textures"})
param("nodeSize",{label="Node size",min=1,max=14,default=5,group="Particles and textures",band="bass",depth=0.5})
param("link",{label="Link distance",min=0.05,max=0.4,default=0.17,group="Geometry and movement",band="mid",depth=0.25})
param("drift",{label="Drift speed",min=0,max=2,default=0.5,group="Geometry and movement"})
param("lineWidth",{label="Link width",min=0.3,max=5,default=1.2,group="Geometry and movement"})
param("signals",{label="Signal rate",min=0,max=2,default=1,group="Audio reactivity"})
param("signalSpeed",{label="Signal speed",min=0.2,max=4,default=1.5,group="Animation",band="high",depth=0.4})
param("trail",{label="Trail",min=0.05,max=1,default=0.35,group="Animation"})
param("bloom",{label="Bloom",min=0,max=2,default=0.8,group="Post-processing"})
param("vignette",{label="Vignette",min=0,max=1,default=0.4,group="Post-processing"})
param("grain",{label="Film grain",min=0,max=1,default=0.05,group="Post-processing"})
param("chroma",{label="Chromatic aberration",min=0,max=2,default=0.2,group="Chromatic",band="highmid",depth=0.3})
local function grade(p)
  gfx.post("hue", p.hue) gfx.post("saturation", p.saturation) gfx.post("brightness", p.brightness) gfx.post("contrast", p.contrast)
  gfx.post("bloom", p.bloom) gfx.post("vignette", p.vignette) gfx.post("grain", p.grain)
  gfx.post("chroma", p.chroma)
end
local N = 100
local x, y, vx, vy, bandOf = {}, {}, {}, {}, {}
local pa, pb, pt, np = {}, {}, {}, 0
local MAXP = 60
viz.seed(3)
for i = 1, N do x[i] = viz.rand(); y[i] = viz.rand(); local a = viz.rand() * 6.2831853; vx[i] = math.cos(a) * 0.03; vy[i] = math.sin(a) * 0.03; bandOf[i] = (i % 32) + 1 end
for i = 1, MAXP do pa[i] = 0; pb[i] = 0; pt[i] = 2 end
local act, linkD, nextSig = N, 0.17, 0
local ea, eb, ed, ne = {}, {}, {}, 0
function update(dt, a, p)
  act = math.floor(math.min(N, N * p.nodes * (0.5 + 0.17 * host.quality) + 10))
  if act > N then act = N end
  linkD = p.link
  for i = 1, act do
    x[i] = x[i] + vx[i] * dt * p.drift * (1 + a.full); y[i] = y[i] + vy[i] * dt * p.drift * (1 + a.full)
    if x[i] < 0 then x[i] = 0; vx[i] = -vx[i] end
    if x[i] > 1 then x[i] = 1; vx[i] = -vx[i] end
    if y[i] < 0 then y[i] = 0; vy[i] = -vy[i] end
    if y[i] > 1 then y[i] = 1; vy[i] = -vy[i] end
  end
  -- build the edge list once per frame (shared by signals and drawing)
  ne = 0
  local l2 = linkD * linkD
  for i = 1, act - 1 do
    local xi, yi = x[i], y[i]
    for j = i + 1, act do
      local dx, dy = x[j] - xi, y[j] - yi
      local d2 = dx * dx + dy * dy
      if d2 < l2 and ne < 500 then ne = ne + 1; ea[ne] = i; eb[ne] = j; ed[ne] = 1 - math.sqrt(d2) / linkD end
    end
  end
  nextSig = nextSig - dt
  if (a.beat > 0.9 or nextSig < 0) and ne > 0 then
    local n = 1 + math.floor(p.signals * (2 + a.bass * 8))
    for k = 1, n do
      for s = 1, MAXP do if pt[s] >= 1 then local e = viz.randi(1, ne); pa[s] = ea[e]; pb[s] = eb[e]; pt[s] = 0; break end end
    end
    nextSig = 0.25 / (0.2 + p.signals)
  end
  for s = 1, MAXP do if pt[s] < 1 then pt[s] = pt[s] + dt * p.signalSpeed end end
end
function render(c, w, h, p)
  local ox = (w - h) * 0.5
  c.clear(0, 0, 0, p.trail)
  c.blend("add")
  local r0, g0, b0 = color.hsv(p.palette, 0.7, 1)
  for e = 1, ne do
    local i, j = ea[e], eb[e]
    c.color(r0, g0, b0, ed[e] * 0.55)
    c.line(x[i] * h + ox, y[i] * h, x[j] * h + ox, y[j] * h, p.lineWidth)
  end
  local r1, g1, b1 = color.hsv(p.palette + 0.12, 0.4, 1)
  for s = 1, MAXP do
    if pt[s] < 1 then
      local i, j, t = pa[s], pb[s], pt[s]
      local px, py = (x[i] + (x[j] - x[i]) * t) * h + ox, (y[i] + (y[j] - y[i]) * t) * h
      c.color(r1, g1, b1, 1 - math.abs(t - 0.5) * 0.6)
      c.dot(px, py, 9 * p.nodeSize * 0.4)
    end
  end
  for i = 1, act do
    local e = audio.spec[bandOf[i]]
    local r, g, b = color.hsv(p.palette + 0.3 * e, 0.6, 1)
    c.color(r, g, b, 0.9)
    c.dot(x[i] * h + ox, y[i] * h, p.nodeSize * (0.6 + e * 1.8) * h / 540)
  end
  grade(p)
end
