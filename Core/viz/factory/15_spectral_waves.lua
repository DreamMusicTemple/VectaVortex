visual{ name="Spectral Waves", category="Spectrum", author="Vektur", shader="none",
  desc="A ridge-plot of the live spectrum, scrolling into the distance with hidden-line fills. Peaks glow in the palette colours." }
param("hue",{label="Hue",min=0,max=1,default=0,step=0.001,group="Colour"})
param("saturation",{label="Saturation",min=0,max=2,default=1,group="Colour"})
param("brightness",{label="Brightness",min=0.2,max=2,default=1,group="Colour"})
param("contrast",{label="Contrast",min=0.5,max=2,default=1,group="Colour"})
param("palette",{label="Palette shift",min=0,max=1,default=0.45,step=0.001,group="Colour"})
param("colorSpread",{label="Colour spread",min=0,max=1,default=0.6,group="Colour"})
param("rows",{label="Rows (history)",min=6,max=40,default=26,step=1,group="Geometry and movement"})
param("height",{label="Wave height",min=0.1,max=2,default=0.9,group="Geometry and movement",band="bass",depth=0.25})
param("perspective",{label="Perspective",min=0,max=1,default=0.55,group="Geometry and movement"})
param("lineWidth",{label="Line width",min=0.5,max=5,default=1.8,group="Geometry and movement",band="high",depth=0.3})
param("smooth",{label="Smoothing",min=0,max=0.95,default=0.5,group="Animation"})
param("scroll",{label="Scroll rate",min=2,max=40,default=18,group="Animation"})
param("fill",{label="Hidden-line fill",min=0,max=1,default=0.85,group="Particles and textures"})
param("glow",{label="Peak glow",min=0,max=2,default=1,group="Particles and textures",band="treble",depth=0.5})
param("bloom",{label="Bloom",min=0,max=2,default=0.8,group="Post-processing"})
param("vignette",{label="Vignette",min=0,max=1,default=0.4,group="Post-processing"})
param("grain",{label="Film grain",min=0,max=1,default=0.05,group="Post-processing"})
param("chroma",{label="Chromatic aberration",min=0,max=2,default=0.25,group="Chromatic",band="highmid",depth=0.3})
local function grade(p)
  gfx.post("hue", p.hue) gfx.post("saturation", p.saturation) gfx.post("brightness", p.brightness) gfx.post("contrast", p.contrast)
  gfx.post("bloom", p.bloom) gfx.post("vignette", p.vignette) gfx.post("grain", p.grain)
  gfx.post("chroma", p.chroma)
end
local NB, MAXR = 64, 40
local hist = {}
for r = 1, MAXR do hist[r] = {}; for i = 1, NB do hist[r][i] = 0 end end
local head, acc = 1, 0
local cur = {}
for i = 1, NB do cur[i] = 0 end
function update(dt, a, p)
  for i = 1, NB do
    local f = (i - 1) / (NB - 1) * 31 + 1
    local lo = math.floor(f)
    local hi = math.min(32, lo + 1)
    local v = a.spec[lo] + (a.spec[hi] - a.spec[lo]) * (f - lo)
    cur[i] = cur[i] * p.smooth + v * (1 - p.smooth)
  end
  acc = acc + dt * p.scroll
  while acc >= 1 do
    acc = acc - 1
    head = head - 1; if head < 1 then head = MAXR end
    local row = hist[head]
    for i = 1, NB do row[i] = cur[i] end
  end
end
function render(c, w, h, p)
  c.clear(0, 0, 0, 1)
  local R = math.floor(math.min(p.rows, 10 + host.quality * 10))
  local per = p.perspective
  for k = R, 1, -1 do
    local row = hist[((head - 1 + k - 1) % MAXR) + 1]
    local depth = (k - 1 + acc * 0 ) / R
    local sc = 1 - depth * per * 0.7
    local y0 = h * (0.92 - depth * 0.55 * (0.4 + per))
    local x0, x1 = w * 0.5 - w * 0.46 * sc, w * 0.5 + w * 0.46 * sc
    local r, g, b = color.hsv(p.palette + depth * p.colorSpread, 0.75, 1 - depth * 0.5)
    local prevx, prevy
    c.blend("normal")
    for i = 1, NB do
      local xx = x0 + (x1 - x0) * (i - 1) / (NB - 1)
      local yy = y0 - row[i] * h * 0.5 * p.height * sc
      if i > 1 then
        if p.fill > 0.01 then
          c.color(0, 0, 0, p.fill)
          c.tri(prevx, prevy, xx, yy, prevx, y0 + 2)
          c.tri(xx, yy, xx, y0 + 2, prevx, y0 + 2)
        end
      end
      prevx, prevy = xx, yy
    end
    prevx = nil
    for i = 1, NB do
      local xx = x0 + (x1 - x0) * (i - 1) / (NB - 1)
      local yy = y0 - row[i] * h * 0.5 * p.height * sc
      if prevx then
        c.color(r, g, b, 0.9 - depth * 0.5)
        c.line(prevx, prevy, xx, yy, p.lineWidth * sc)
      end
      prevx, prevy = xx, yy
      if k <= 3 and row[i] > 0.55 then c.blend("add"); c.color(r, g, b, (row[i] - 0.5) * p.glow); c.dot(xx, yy, 10 * p.glow * h / 540); c.blend("normal") end
    end
  end
  grade(p)
end
