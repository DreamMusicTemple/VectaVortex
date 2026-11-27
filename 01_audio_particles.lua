visual{ name="Audio Particles", category="Particles", author="Vektur", shader="none",
  desc="A particle fountain simulated in Lua: the beat fires radial bursts, mids swirl the cloud, highs add turbulence, lows pull it home." }
param("hue",{label="Hue",min=0,max=1,default=0,step=0.001,group="Colour"})
param("saturation",{label="Saturation",min=0,max=2,default=1,group="Colour"})
param("brightness",{label="Brightness",min=0.2,max=2,default=1,group="Colour"})
param("contrast",{label="Contrast",min=0.5,max=2,default=1,group="Colour"})
param("palette",{label="Palette shift",min=0,max=1,default=0.1,step=0.001,group="Colour"})
param("colorSpeed",{label="Colour drift speed",min=0,max=2,default=0.3,group="Colour"})
param("count",{label="Particle count",min=0.1,max=1,default=1,group="Particles and textures"})
param("size",{label="Particle size",min=1,max=14,default=4,group="Particles and textures",band="bass",depth=0.3})
param("burst",{label="Burst strength",min=0,max=2,default=1,group="Audio reactivity"})
param("swirl",{label="Swirl",min=-3,max=3,default=0.8,group="Geometry and movement",band="mid",depth=0.5})
param("pull",{label="Gravity to centre",min=0,max=3,default=0.6,group="Geometry and movement"})
param("turbulence",{label="Turbulence",min=0,max=3,default=0.6,group="Geometry and movement",band="high",depth=0.6})
param("spread",{label="Spawn radius",min=0,max=0.6,default=0.08,group="Geometry and movement"})
param("trail",{label="Trail length",min=0.02,max=1,default=0.18,group="Animation"})
param("streak",{label="Streak length",min=0,max=2,default=0.6,group="Animation"})
param("bloom",{label="Bloom",min=0,max=2,default=0.9,group="Post-processing"})
param("vignette",{label="Vignette",min=0,max=1,default=0.35,group="Post-processing"})
param("grain",{label="Film grain",min=0,max=1,default=0.05,group="Post-processing"})
param("chroma",{label="Chromatic aberration",min=0,max=2,default=0.25,group="Chromatic",band="highmid",depth=0.3})
local function grade(p)
  gfx.post("hue", p.hue) gfx.post("saturation", p.saturation) gfx.post("brightness", p.brightness) gfx.post("contrast", p.contrast)
  gfx.post("bloom", p.bloom) gfx.post("vignette", p.vignette) gfx.post("grain", p.grain)
  gfx.post("chroma", p.chroma)
end
local N = 1500
local x, y, vx, vy, life, hue = {}, {}, {}, {}, {}, {}
local cx, cy = 0.5, 0.5
local hueClock, kick = 0, 0
viz.seed(7)
for i = 1, N do x[i] = 0.5; y[i] = 0.5; vx[i] = 0; vy[i] = 0; life[i] = viz.rand(); hue[i] = viz.rand() end
local function respawn(i, speed, spread)
  local a = viz.rand() * 6.2831853
  local r = viz.rand() * spread
  x[i] = 0.5 + math.cos(a) * r; y[i] = 0.5 + math.sin(a) * r
  local s = speed * (0.4 + viz.rand())
  vx[i] = math.cos(a) * s; vy[i] = math.sin(a) * s
  life[i] = 0.6 + viz.rand() * 0.9; hue[i] = viz.rand()
end
local active = 1
function update(dt, a, p)
  kick = math.max(kick * math.exp(-dt * 8), a.beat)
  hueClock = hueClock + dt * p.colorSpeed * 0.1
  active = math.floor(math.min(N, N * p.count * (0.25 + 0.25 * host.quality) * 1.0 + 100))
  if active > N then active = N end
  local burstNow = a.beat > 0.9
  local t = a.t
  for i = 1, active do
    local dx, dy = x[i] - 0.5, y[i] - 0.5
    local d = math.sqrt(dx * dx + dy * dy) + 0.001
    -- forces: pull home, swirl around centre, noise turbulence
    local fx = -dx * p.pull * 0.6 + (-dy / d) * p.swirl * (0.2 + a.mid * 0.9)
    local fy = -dy * p.pull * 0.6 + (dx / d) * p.swirl * (0.2 + a.mid * 0.9)
    local nz = viz.noise(x[i] * 4 + t * 0.4, y[i] * 4, i * 0.013)
    local nz2 = viz.noise(x[i] * 4, y[i] * 4 + t * 0.4, i * 0.017 + 9)
    fx = fx + nz * p.turbulence * (0.2 + a.high * 1.6)
    fy = fy + nz2 * p.turbulence * (0.2 + a.high * 1.6)
    if burstNow then fx = fx + (dx / d) * p.burst * 3 * (0.5 + viz.rand()); fy = fy + (dy / d) * p.burst * 3 * (0.5 + viz.rand()) end
    vx[i] = (vx[i] + fx * dt) * (1 - dt * 0.9); vy[i] = (vy[i] + fy * dt) * (1 - dt * 0.9)
    x[i] = x[i] + vx[i] * dt; y[i] = y[i] + vy[i] * dt
    life[i] = life[i] - dt * (0.18 + a.full * 0.25)
    if life[i] <= 0 or x[i] < -0.2 or x[i] > 1.2 or y[i] < -0.2 or y[i] > 1.2 then respawn(i, 0.15 + a.bass * 0.5 + kick * 0.3, p.spread) end
  end
end
function render(c, w, h, p)
  local asp = w / h
  c.clear(0, 0, 0, p.trail)
  c.blend("add")
  local sz = p.size * h / 540
  for i = 1, active do
    local l = life[i]
    if l > 0 then
      local r, g, b = color.hsv(hue[i] * 0.35 + p.palette + hueClock, 0.8, 0.9)
      local fade = math.min(1, l * 2)
      c.color(r, g, b, fade * 0.9)
      local px, py = (x[i] - 0.5) * h * 1.0 + w * 0.5, y[i] * h
      if p.streak > 0.02 then c.line(px, py, px - vx[i] * p.streak * h * 0.05, py - vy[i] * p.streak * h * 0.05, sz * 0.5) end
      c.dot(px, py, sz * (0.7 + l * 0.6) * (1 + kick * 0.5))
    end
  end
  grade(p)
end
