visual{ name="Geometric Pulse", category="Geometric", author="Vektur", shader="none",
  desc="Nested rotating polygons with star-polygon chords. Bass pulses the radius, every beat launches an expanding ring." }
param("hue",{label="Hue",min=0,max=1,default=0,step=0.001,group="Colour"})
param("saturation",{label="Saturation",min=0,max=2,default=1,group="Colour"})
param("brightness",{label="Brightness",min=0.2,max=2,default=1,group="Colour"})
param("contrast",{label="Contrast",min=0.5,max=2,default=1,group="Colour"})
param("palette",{label="Palette shift",min=0,max=1,default=0.9,step=0.001,group="Colour"})
param("colorSpread",{label="Colour spread",min=0,max=1,default=0.5,group="Colour"})
param("sides",{label="Polygon sides",min=3,max=12,default=6,step=1,group="Geometry and movement"})
param("layers",{label="Layers",min=1,max=14,default=8,step=1,group="Geometry and movement"})
param("star",{label="Star step",min=1,max=5,default=2,step=1,group="Geometry and movement"})
param("size",{label="Size",min=0.2,max=1.2,default=0.7,group="Geometry and movement",band="bass",depth=0.35})
param("lineWidth",{label="Line width",min=0.5,max=6,default=1.6,group="Geometry and movement",band="high",depth=0.3})
param("spin",{label="Rotation speed",min=-2,max=2,default=0.35,group="Animation"})
param("twist",{label="Layer twist",min=-1,max=1,default=0.12,group="Animation",band="mid",depth=0.4})
param("rings",{label="Beat rings",min=0,max=2,default=1,group="Audio reactivity"})
param("trail",{label="Trail",min=0.1,max=1,default=0.5,group="Animation"})
param("bloom",{label="Bloom",min=0,max=2,default=0.9,group="Post-processing"})
param("vignette",{label="Vignette",min=0,max=1,default=0.4,group="Post-processing"})
param("grain",{label="Film grain",min=0,max=1,default=0.05,group="Post-processing"})
param("chroma",{label="Chromatic aberration",min=0,max=2,default=0.25,group="Chromatic",band="highmid",depth=0.3})
param("feedback",{label="Feedback trails",min=0,max=0.95,default=0.25,group="Post-processing"})
local function grade(p)
  gfx.post("hue", p.hue) gfx.post("saturation", p.saturation) gfx.post("brightness", p.brightness) gfx.post("contrast", p.contrast)
  gfx.post("bloom", p.bloom) gfx.post("vignette", p.vignette) gfx.post("grain", p.grain)
  gfx.post("chroma", p.chroma)
  gfx.post("feedback", p.feedback) gfx.post("zoom", 1 + p.feedback * 0.02) gfx.post("rotate", p.feedback * 0.004)
end
local rot, kick = 0, 0
local rg, rr = {}, {}
for i = 1, 8 do rg[i] = 0; rr[i] = 2 end
function update(dt, a, p)
  kick = math.max(kick * math.exp(-dt * 7), a.beat)
  rot = rot + dt * p.spin * (0.6 + a.mid)
  if a.beat > 0.9 and p.rings > 0.05 then for i = 1, 8 do if rr[i] >= 1 then rr[i] = 0; break end end end
  for i = 1, 8 do if rr[i] < 1 then rr[i] = rr[i] + dt * 0.9 end end
end
function render(c, w, h, p)
  local cx, cy = w * 0.5, h * 0.5
  c.clear(0, 0, 0, p.trail)
  c.blend("add")
  local L = math.floor(math.min(p.layers, 3 + host.quality * 4))
  local n = math.floor(p.sides)
  local k = math.floor(p.star)
  if k >= n then k = n - 1 end
  if k < 1 then k = 1 end
  local R0 = h * 0.5 * p.size * (1 + kick * 0.15)
  for l = 1, L do
    local f = l / L
    local R = R0 * (0.15 + 0.85 * f) * (1 + audio.spec[(l % 32) + 1] * 0.25)
    local ang = rot * (l % 2 == 0 and 1 or -1) + l * p.twist
    local r, g, b = color.hsv(p.palette + f * p.colorSpread, 0.75, 1)
    c.color(r, g, b, 0.85)
    for i = 0, n - 1 do
      local a1 = ang + i * 6.2831853 / n
      local a2 = ang + (i + 1) * 6.2831853 / n
      local a3 = ang + (i + k) * 6.2831853 / n
      local x1, y1 = cx + math.cos(a1) * R, cy + math.sin(a1) * R
      c.line(x1, y1, cx + math.cos(a2) * R, cy + math.sin(a2) * R, p.lineWidth)
      if k > 1 then c.line(x1, y1, cx + math.cos(a3) * R, cy + math.sin(a3) * R, p.lineWidth * 0.6) end
    end
  end
  for i = 1, 8 do
    if rr[i] < 1 then
      local r, g, b = color.hsv(p.palette + 0.5, 0.5, 1)
      c.color(r, g, b, (1 - rr[i]) * 0.8 * p.rings)
      c.ring(cx, cy, R0 * (0.3 + rr[i] * 1.4), p.lineWidth * (2 - rr[i]))
    end
  end
  grade(p)
end
