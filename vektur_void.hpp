// Vektur Void: the hidden Easter-egg shoot-em-up (port of the web app's pure game logic: shmupNewGame ... shmupStep).
// Pure simulation, no drawing and no audio: the UI layers (JUCE / SwiftUI) draw a `Game` and feed it `Input`.
// It runs on the UI thread, never on the audio thread (it uses std::vector), and it never touches any synth parameter.
// Every random draw happens in the same order as the JS so a seeded run matches the web logic step for step
// (tests/void_golden_tests.cpp checks that against golden values produced from the web code).
#pragma once
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <functional>

namespace vv { namespace vvoid {

constexpr double W = 640, H = 360;
enum Mode { M_TITLE, M_PLAY, M_LEVELCLEAR, M_CONTINUE, M_GAMEOVER, M_VICTORY };
enum EType { E_DRONE = 0, E_ZIG, E_DIVER, E_TANK, E_SHOOTER, E_MINE };

struct Input { double dx = 0, dy = 0; bool plasma = false, bomb = false, start = false; };
struct Ship { double x = 90, y = H / 2; bool alive = true; double inv = 0, shield = 0, deadT = 0; };
struct Bullet { double x, y, vx, w, h, dmg; bool dead = false; };
struct Bomb { double x, y, vx, vy, r; bool dead = false; };
struct Blast { double x, y, r, t; };
struct Enemy { int type; double x, y, y0; double hp, r, t, fire, phase; int state = 0; double flash = 0; bool dead = false; };
struct EBul { double x, y, vx, vy, r; int col; bool dead = false; };
struct Pick { char kind; double x, y, t; bool dead = false; };
struct Part { double x, y, vx, vy, life, t; };
struct Beam { double y, t; };
struct Rect { double x, y, w, h; };
struct Obstacle { int kind; double x, width; Rect parts[3]; int nparts; Rect base[3] = {}; double age = 0; bool scored = false; };   // kind 0 = bridge, 1 = zwall
struct Boss { double x, y, w, h; int hits, maxHits; double t, flash, dying, cdA, cdB, cdC, cdD, cdE; bool final_; double core; };

struct EnemyDef { double hp, r; int score; };
inline const EnemyDef& enemyDef(int t) {
    static const EnemyDef d[6] = {{1, 11, 100}, {1, 11, 150}, {1, 11, 200}, {4, 16, 400}, {2, 13, 300}, {1, 10, 250}};
    return d[t];
}
// spawn types: 0-5 as EType, 6 = formation
struct LevelDef { int quota; double ivl[2]; std::vector<int> types; bool scenery; double obIvl[2]; std::vector<int> obKinds; };
inline const LevelDef* levelDef(int lv) {
    static const LevelDef L[5] = {
        {22, {1.35, 0.95}, {0, 0, 1}, false, {0, 0}, {}},
        {30, {1.15, 0.8}, {0, 1, 2, 3}, false, {0, 0}, {}},
        {38, {1.0, 0.65}, {0, 1, 2, 3, 4, 5, 6}, false, {0, 0}, {}},
        {26, {1.1, 0.75}, {0, 1, 2, 4}, true, {2.6, 1.8}, {0, 1}},
        {34, {0.95, 0.6}, {0, 1, 2, 3, 4, 5}, true, {2.2, 1.4}, {1, 0, 1}} };
    return lv == 1 ? &L[0] : lv == 2 ? &L[1] : lv == 3 ? &L[2] : lv == 5 ? &L[3] : lv == 6 ? &L[4] : nullptr;
}

using Rnd = std::function<double()>;
// Deterministic generator shared with the golden harness (LCG, same arithmetic as tools/make_golden_void.js).
struct Lcg { uint32_t s; explicit Lcg(uint32_t seed = 1) : s(seed) {} double operator()() { s = s * 1664525u + 1013904223u; return (double)s / 4294967296.0; } };

struct Game {
    int mode = M_TITLE; double t = 0, modeT = 0; long score = 0; int level = 1, spares = 3, continues = 2, wLvl = 0; bool god = false;
    Ship ship; std::vector<Bullet> bullets; std::vector<Bomb> bombs; std::vector<Blast> blasts; std::vector<Enemy> enemies; std::vector<EBul> ebul;
    std::vector<Pick> pick; std::vector<Part> parts; std::vector<Beam> beams; std::vector<Obstacle> obstacles;
    double cdP = 0, cdB = 0, spawnT = 1, obSpawnT = 1.8; int spawned = 0; bool wCapsGiven[8] = {}; double capT = 0;
    bool hasBoss = false; Boss boss{};
    std::string banner; double bannerT = 0, contT = 10, typed = 0;
    double pulseDrive = 0, pulseBlur = 0, spread = 0, symKick = 0; int kills = 0; double shake = 0;
    bool sCapA[8] = {}, sCapB[8] = {};   // persists across "new run" exactly like the web's lazily created G.sCaps
};

inline Game newGame() { Game g; return g; }
inline void dir8(double dx, double dy, double& ox, double& oy) {
    const double m = std::hypot(dx, dy); if (m < 0.35) { ox = 0; oy = 0; return; }
    const double step = 3.141592653589793 / 4; const double a = std::floor(std::atan2(dy, dx) / step + 0.5) * step;   // Math.round
    ox = std::cos(a); oy = std::sin(a);
}
inline bool circle(double ax, double ay, double ar, double bx, double by, double br) { const double dx = ax - bx, dy = ay - by, rr = ar + br; return dx * dx + dy * dy <= rr * rr; }
inline bool circleRect(double cx, double cy, double cr, double rx, double ry, double rw, double rh) {
    const double nx = std::max(rx, std::min(cx, rx + rw)), ny = std::max(ry, std::min(cy, ry + rh)); const double dx = cx - nx, dy = cy - ny; return dx * dx + dy * dy <= cr * cr; }
enum Tint { T_GREEN, T_ORANGE, T_RED };
inline int bossTint(const Boss& b) { const double f = (double)b.hits / b.maxHits; return f < 0.35 ? T_GREEN : (f < 0.7 ? T_ORANGE : T_RED); }

inline void setMode(Game& G, int m) { G.mode = m; G.modeT = 0; }
inline void startLevel(Game& G, int lv) {
    G.level = lv; G.spawned = 0; G.spawnT = 1.2; G.obSpawnT = 1.8; G.enemies.clear(); G.ebul.clear(); G.obstacles.clear(); G.hasBoss = false;
    G.banner = lv == 4 ? "LEVEL 4 - BOSS" : (lv == 7 ? "LEVEL 7 - FINAL BOSS" : "LEVEL " + std::to_string(lv)); G.bannerT = 2.2;
}
inline void newRun(Game& G) {
    const bool god = G.god; bool a[8], b[8]; std::copy(G.sCapA, G.sCapA + 8, a); std::copy(G.sCapB, G.sCapB + 8, b);
    G = newGame(); G.god = god; std::copy(a, a + 8, G.sCapA); std::copy(b, b + 8, G.sCapB);
    startLevel(G, 1); setMode(G, M_PLAY);
}
inline void burst(Game& G, double x, double y, int n, double sp, const Rnd& rnd) {
    for (int i = 0; i < n; i++) {
        const double a = rnd() * 6.28; const double s = sp * (0.3 + rnd()); const double life = 0.4 + rnd() * 0.4;
        G.parts.push_back({x, y, std::cos(a) * s, std::sin(a) * s, life, 0});
    }
}
inline void spawnEnemy(Game& G, const Rnd& rnd, int type, double x, double y) {
    const EnemyDef& d = enemyDef(type); Enemy e{}; e.type = type; e.x = x; e.y = y; e.y0 = y; e.hp = d.hp; e.r = d.r; e.t = 0;
    e.fire = 1 + rnd() * 1.5; e.phase = rnd() * 6.28; G.enemies.push_back(e);
}
inline void spawnWave(Game& G, const Rnd& rnd) {
    const LevelDef* L = levelDef(G.level);
    const int type = L->types[(size_t)std::floor(rnd() * (double)L->types.size())];
    const double y = 40 + rnd() * (H - 110);
    if (type == 6) { for (int i = 0; i < 5; i++) spawnEnemy(G, rnd, E_DRONE, W + 20 + std::abs(i - 2) * 26, H / 2 + (i - 2) * 30); G.spawned += 5; return; }
    spawnEnemy(G, rnd, type, W + 20, y); G.spawned++;
}
// Z-wall slabs are gates (web shmupGateParts): each slides shut and open on its own cycle, staggered by a third of a period.
constexpr double kGatePeriod = 2.2;
inline double gateClosed(double age, int i) { const double q = (age + i * kGatePeriod / 3) / kGatePeriod; const double p = q - std::floor(q);
    return p < 0.40 ? 1.0 : p < 0.52 ? 1 - (p - 0.40) / 0.12 : p < 0.88 ? 0.0 : (p - 0.88) / 0.12; }
inline void gateParts(Obstacle& ob) {
    for (int i = 0; i < ob.nparts; i++) {
        const Rect& b = ob.base[i]; const double c = gateClosed(ob.age, i), h = b.h * c; Rect& P = ob.parts[i];
        if (h < 1) { P = {-1000, -1000, 0, 0}; continue; }
        P.x = b.x; P.w = b.w; P.h = h; P.y = i == 0 ? b.y : i == 2 ? b.y + b.h - h : b.y + (b.h - h) / 2;
    }
}
inline void spawnObstacle(Game& G, const Rnd& rnd, int kind) {
    const double x = W + 40; Obstacle o{}; o.kind = kind; o.x = x;
    if (kind == 0) {
        const double gapY = 60 + rnd() * 120, gapH = 78; o.width = 70; o.nparts = 2; o.parts[0] = {0, 0, 70, gapY}; o.parts[1] = {0, gapY + gapH, 70, H - (gapY + gapH)};
        G.obstacles.push_back(o);
        if (rnd() < 0.65) G.pick.push_back({'W', x + 35, gapY + gapH / 2, 0});
    } else {
        const double h1 = 60 + rnd() * 40, h2 = 60 + rnd() * 40; o.width = 180; o.nparts = 3;
        o.parts[0] = {0, 0, 60, h1}; o.parts[1] = {60, H - h2 - 110, 60, 110}; o.parts[2] = {120, H - h2, 60, h2};
        for (int i = 0; i < 3; i++) o.base[i] = o.parts[i];
        G.obstacles.push_back(o);
        if (rnd() < 0.35) G.pick.push_back({'S', x + 150, H / 2, 0});
    }
}
inline void kill(Game& G, Enemy& e, const Rnd& rnd) {
    if (e.dead) return; e.dead = true; G.score += enemyDef(e.type).score; G.kills++;
    G.spread = std::min(1.0, G.spread + 0.07); G.symKick = (rnd() < 0.5 ? -1 : 1) * 0.4;
    burst(G, e.x, e.y, 10, 90, rnd);
    if (e.type == E_MINE) for (int k = 0; k < 4; k++) { const double a = k * 3.141592653589793 / 2 + 3.141592653589793 / 4; G.ebul.push_back({e.x, e.y, std::cos(a) * 120, std::sin(a) * 120, 3.5, 0}); }
    if (rnd() < 0.05) G.pick.push_back({'S', e.x, e.y, 0});
}
inline void damageEnemy(Game& G, Enemy& e, double dmg, const Rnd& rnd) { e.hp -= dmg; e.flash = 0.08; if (e.hp <= 0) kill(G, e, rnd); }
inline bool playerHit(Game& G, const Rnd& rnd) {
    Ship& s = G.ship; if (!s.alive || s.inv > 0 || G.god) return false; if (s.shield > 0) return false;
    s.alive = false; s.deadT = 1.3; G.shake = 0.4; burst(G, s.x, s.y, 26, 160, rnd); return true;
}
inline void hitBoss(Game& G, const Rnd& rnd) {
    if (!G.hasBoss) return; Boss& b = G.boss; if (b.dying > 0) return;
    b.hits++; b.flash = 0.1; G.score += 100; G.shake = std::max(G.shake, 0.08); burst(G, b.x + b.core, b.y, 8, 120, rnd);
    if (b.hits >= b.maxHits) { b.dying = 3; G.score += b.final_ ? 20000 : 10000; G.ebul.clear(); G.beams.clear(); G.enemies.clear(); }
}
inline void blast(Game& G, double x, double y, const Rnd& rnd) {
    static const double rr[4] = {34, 48, 64, 84}; const double r = rr[G.wLvl]; G.blasts.push_back({x, y, r, 0});
    G.pulseDrive = std::min(1.0, G.pulseDrive + 0.5 + 0.15 * G.wLvl); G.pulseBlur = std::min(1.0, G.pulseBlur + 0.35 + 0.15 * G.wLvl); G.shake = std::max(G.shake, 0.12);
    for (size_t i = 0; i < G.enemies.size(); i++) { Enemy& e = G.enemies[i]; if (!e.dead && circle(x, y, r, e.x, e.y, e.r)) damageEnemy(G, e, 2, rnd); }
    if (G.hasBoss && G.boss.dying <= 0) { const Boss& b = G.boss; const double cx = b.x + b.core, cy = b.y + 10 * std::sin(b.t * 2); if (circle(x, y, r, cx, cy, 14)) hitBoss(G, rnd); }
    burst(G, x, y, 14, 110, rnd);
}
inline void bossFire(Game& G, double dt, const Rnd& rnd) {
    Boss& b = G.boss; const Ship& s = G.ship; const int tint = bossTint(b);
    auto aim = [&](double spd, double off, double r, int col) { const double a = std::atan2(s.y - b.y, s.x - (b.x + b.core + 22)) + off; G.ebul.push_back({b.x + b.core, b.y, std::cos(a) * spd, std::sin(a) * spd, r, col}); };
    b.cdA -= dt; if (b.cdA <= 0) { b.cdA = tint == T_RED ? 0.95 : (tint == T_ORANGE ? 1.2 : 1.5); aim(190, -0.17, 4, 0); aim(190, 0, 4, 0); aim(190, 0.17, 4, 0); }
    b.cdB -= dt;
    if (b.cdB <= 0) { b.cdB = tint == T_GREEN ? 3.2 : 2.6;
        if (tint == T_GREEN) { G.ebul.push_back({b.x + b.core, b.y + 30, -110, 0, 9, 1}); G.ebul.push_back({b.x + b.core, b.y - 30, -110, 0, 9, 1}); }
        else for (int k = -3; k <= 3; k++) { const double a = 3.141592653589793 + k * 0.2; G.ebul.push_back({b.x + b.core, b.y, std::cos(a) * 160, std::sin(a) * 160, 4, 2}); } }
    if (tint != T_GREEN) { b.cdC -= dt; if (b.cdC <= 0) { b.cdC = tint == T_RED ? 4.5 : 6; G.beams.push_back({30 + rnd() * (H - 60), 0}); } }
    if (tint == T_RED) {
        b.cdD -= dt; if (b.cdD <= 0) { b.cdD = 3.2; for (int k = 0; k < 12; k++) { const double a = k * 3.141592653589793 / 6; G.ebul.push_back({b.x + b.core + 52, b.y, std::cos(a) * 130, std::sin(a) * 130, 3.5, 3}); } }
        b.cdE -= dt; if (b.cdE <= 0) { b.cdE = 5; spawnEnemy(G, rnd, E_DRONE, b.x + b.core + 2, b.y - 40); spawnEnemy(G, rnd, E_DRONE, b.x + b.core + 2, b.y + 40); }
    }
}

inline void step(Game& G, const Input& inp, double dt, const Rnd& rnd) {
    G.t += dt; G.modeT += dt; if (G.shake > 0) G.shake = std::max(0.0, G.shake - dt);
    G.pulseDrive *= std::exp(-dt / 0.9); G.pulseBlur *= std::exp(-dt / 0.9); G.symKick *= std::exp(-dt / 0.5); G.spread = std::max(0.0, G.spread - 0.012 * dt);
    { size_t k = 0; for (size_t i = 0; i < G.parts.size(); i++) { Part p = G.parts[i]; p.t += dt; p.x += p.vx * dt; p.y += p.vy * dt; if (p.t < p.life) G.parts[k++] = p; } G.parts.resize(k); }
    { size_t k = 0; for (size_t i = 0; i < G.blasts.size(); i++) { Blast b = G.blasts[i]; b.t += dt; if (b.t < 0.35) G.blasts[k++] = b; } G.blasts.resize(k); }
    if (G.bannerT > 0) G.bannerT -= dt;
    if (G.mode == M_TITLE) { if (inp.plasma || inp.start) newRun(G); return; }
    if (G.mode == M_CONTINUE) {
        G.contT -= dt;
        if (inp.start || (inp.plasma && G.modeT > 0.6)) {
            if (G.continues > 0) { G.continues--; G.spares = 3; Ship& s = G.ship; s.x = 90; s.y = H / 2; s.alive = true; s.inv = 2.5; s.shield = 0; s.deadT = 0; G.ebul.clear(); setMode(G, M_PLAY); }
        } else if (G.contT <= 0) setMode(G, M_GAMEOVER);
        return;
    }
    if (G.mode == M_GAMEOVER) { if ((inp.start || inp.plasma) && G.modeT > 1.5) newRun(G); return; }
    if (G.mode == M_VICTORY) { G.typed += dt * 30; if ((inp.start || inp.plasma) && G.modeT > 6) newRun(G); return; }
    if (G.mode == M_LEVELCLEAR) { if (G.modeT > 2.6) { startLevel(G, G.level + 1); setMode(G, M_PLAY); } return; }
    // ---- play ----
    Ship& s = G.ship;
    if (s.alive) {
        double d0, d1; dir8(inp.dx, inp.dy, d0, d1); const double sp = 175; s.x += d0 * sp * dt; s.y += d1 * sp * dt;
        s.x = std::max(18.0, std::min(W * 0.62, s.x)); s.y = std::max(26.0, std::min(H - 30, s.y));
        if (s.inv > 0) s.inv -= dt; if (s.shield > 0) { s.shield -= dt; if (s.shield <= 0) s.shield = 0; }
        G.cdP -= dt; G.cdB -= dt;
        static const double bw[4] = {12, 22, 34, 48}, bh[4] = {3, 5, 7, 10}, bd[4] = {1, 1, 2, 3};
        if (inp.plasma && G.cdP <= 0) { G.cdP = 0.16; G.bullets.push_back({s.x + 16, s.y, 560, bw[G.wLvl], bh[G.wLvl], bd[G.wLvl]}); }
        if (inp.bomb && G.cdB <= 0) { G.cdB = 0.7; G.bombs.push_back({s.x, s.y + 8, 70, 20, 4 + G.wLvl * 1.5}); }
    } else {
        s.deadT -= dt;
        if (s.deadT <= 0) {
            if (G.spares > 0) { G.spares--; s.alive = true; s.inv = 2.5; s.x = 90; s.y = H / 2; s.shield = 0; }
            else { setMode(G, G.continues > 0 ? M_CONTINUE : M_GAMEOVER); G.contT = 10; return; }
        }
    }
    // bullets
    for (auto& b : G.bullets) b.x += b.vx * dt;
    for (auto& b : G.bullets) {
        if (b.dead) continue;
        for (auto& e : G.enemies) {
            if (e.dead) continue;
            if (circle(b.x + b.w / 2, b.y, std::max(b.h / 2 + 2, 4.0), e.x, e.y, e.r) || (std::abs(b.y - e.y) < e.r + b.h / 2 && b.x + b.w > e.x - e.r && b.x < e.x + e.r)) { damageEnemy(G, e, b.dmg, rnd); b.dead = true; break; }
        }
        if (b.dead) continue;
        if (G.hasBoss && G.boss.dying <= 0) {
            Boss& bo = G.boss; const double cx = bo.x + bo.core, cy = bo.y + 10 * std::sin(bo.t * 2);
            if (std::abs(b.y - cy) < 14 + b.h / 2 && b.x + b.w > cx - 14 && b.x < cx + 14) { hitBoss(G, rnd); b.dead = true; }
            else if (b.x + b.w > bo.x - bo.w / 2 && std::abs(b.y - bo.y) < bo.h / 2) { b.dead = true; burst(G, b.x + b.w, b.y, 4, 80, rnd); }
        }
    }
    { size_t k = 0; for (size_t i = 0; i < G.bullets.size(); i++) if (!G.bullets[i].dead && G.bullets[i].x < W + 60) G.bullets[k++] = G.bullets[i]; G.bullets.resize(k); }
    // bombs
    for (auto& b : G.bombs) {
        b.vy += 330 * dt; b.x += b.vx * dt; b.y += b.vy * dt; bool hit = b.y >= H - 14;
        if (!hit) for (auto& e : G.enemies) if (!e.dead && circle(b.x, b.y, b.r, e.x, e.y, e.r)) { hit = true; break; }
        if (!hit && G.hasBoss && G.boss.dying <= 0 && std::abs(b.x - G.boss.x) < G.boss.w / 2 && std::abs(b.y - G.boss.y) < G.boss.h / 2) hit = true;
        if (hit) { b.dead = true; blast(G, b.x, std::min(b.y, H - 14), rnd); } else if (b.x > W + 30) b.dead = true;
    }
    { size_t k = 0; for (size_t i = 0; i < G.bombs.size(); i++) if (!G.bombs[i].dead) G.bombs[k++] = G.bombs[i]; G.bombs.resize(k); }
    // enemies (index loop: kills may append bullets / pickups, never enemies)
    for (size_t i = 0; i < G.enemies.size(); i++) {
        Enemy& e = G.enemies[i]; e.t += dt; if (e.flash > 0) e.flash -= dt;
        switch (e.type) {
            case E_DRONE: e.x -= 85 * dt; e.y = e.y0 + std::sin(e.t * 2.4 + e.phase) * 38; break;
            case E_ZIG: e.x -= 95 * dt; e.y += ((long)std::floor(e.t / 0.6) % 2 ? 1 : -1) * 95 * dt; e.y = std::max(24.0, std::min(H - 30, e.y)); break;
            case E_DIVER: e.x -= 100 * dt;
                if (e.t < 1.2) { const double df = s.y - e.y; const double sg = df > 0 ? 1 : (df < 0 ? -1 : 0); e.y += sg * std::min(std::abs(df), 140 * dt); } else e.x -= 110 * dt; break;
            case E_TANK: e.x -= 42 * dt; e.fire -= dt; if (e.fire <= 0) { e.fire = 2; G.ebul.push_back({e.x - 12, e.y, -150, 0, 4, 0}); } break;
            case E_SHOOTER:
                if (e.x > W * 0.72) e.x -= 70 * dt; else e.y += std::sin(e.t * 1.5 + e.phase) * 45 * dt * 2;
                e.fire -= dt; if (e.fire <= 0) { e.fire = 1.5; const double a = std::atan2(s.y - e.y, s.x - e.x); G.ebul.push_back({e.x - 10, e.y, std::cos(a) * 170, std::sin(a) * 170, 3.5, 0}); } break;
            case E_MINE: e.x -= 32 * dt; e.y = e.y0 + std::sin(e.t * 1.2 + e.phase) * 20; break;
        }
        if (e.x < -30) e.dead = true;
        if (!e.dead && s.alive && circle(s.x, s.y, 9, e.x, e.y, e.r)) { if (s.shield > 0) kill(G, e, rnd); else playerHit(G, rnd); }
    }
    { size_t k = 0; for (size_t i = 0; i < G.enemies.size(); i++) if (!G.enemies[i].dead) G.enemies[k++] = G.enemies[i]; G.enemies.resize(k); }
    // enemy bullets
    for (auto& b : G.ebul) { b.x += b.vx * dt; b.y += b.vy * dt; if (s.alive && circle(s.x, s.y, s.shield > 0 ? 20 : 7, b.x, b.y, b.r)) { if (s.shield > 0) b.dead = true; else if (playerHit(G, rnd)) b.dead = true; } }
    { size_t k = 0; for (size_t i = 0; i < G.ebul.size(); i++) { const EBul& b = G.ebul[i]; if (!b.dead && b.x > -20 && b.x < W + 20 && b.y > -20 && b.y < H + 20) G.ebul[k++] = b; } G.ebul.resize(k); }
    // beams: 0.9 s telegraph then 0.5 s fire
    for (auto& bm : G.beams) { bm.t += dt; if (bm.t > 0.9 && bm.t < 1.4 && s.alive && std::abs(s.y - bm.y) < 9 + 7 && s.x < (G.hasBoss ? G.boss.x - 70 : W)) playerHit(G, rnd); }
    { size_t k = 0; for (size_t i = 0; i < G.beams.size(); i++) if (G.beams[i].t < 1.4) G.beams[k++] = G.beams[i]; G.beams.resize(k); }
    // pickups
    for (auto& p : G.pick) {
        p.t += dt; p.x -= 55 * dt; p.y += std::sin(p.t * 3) * 20 * dt;
        if (s.alive && circle(s.x, s.y, 12, p.x, p.y, 11)) {
            p.dead = true; G.score += 500;
            if (p.kind == 'W') { if (G.wLvl < 3) G.wLvl++; G.banner = G.wLvl >= 3 ? "WEAPONS MAXED" : "WEAPON UPGRADE " + std::to_string(G.wLvl) + "/3"; G.bannerT = 1.4; }
            else { s.shield = 8; G.banner = "SHIELD ONLINE"; G.bannerT = 1.2; }
        }
        if (p.x < -20) p.dead = true;
    }
    { size_t k = 0; for (size_t i = 0; i < G.pick.size(); i++) if (!G.pick[i].dead) G.pick[k++] = G.pick[i]; G.pick.resize(k); }
    // spawning / progression
    if (G.level != 4 && G.level != 7) {
        const LevelDef* L = levelDef(G.level); if (!L) return;
        if (G.spawned < L->quota) { G.spawnT -= dt; if (G.spawnT <= 0) { const double k = (double)G.spawned / L->quota; G.spawnT = L->ivl[0] + (L->ivl[1] - L->ivl[0]) * k; spawnWave(G, rnd); } }
        const double k = (double)G.spawned / L->quota;
        if (!G.wCapsGiven[G.level] && k >= 0.4) { G.wCapsGiven[G.level] = true; G.pick.push_back({'W', W + 10, 60 + rnd() * (H - 140), 0}); }
        if (!G.sCapA[G.level] && k >= 0.2) { G.sCapA[G.level] = true; G.pick.push_back({'S', W + 10, 60 + rnd() * (H - 140), 0}); }
        if (!G.sCapB[G.level] && k >= 0.7) { G.sCapB[G.level] = true; G.pick.push_back({'S', W + 10, 60 + rnd() * (H - 140), 0}); }
        if (L->scenery) {
            if (G.spawned < L->quota) G.obSpawnT -= dt;   // no new scenery once the quota is met, so the level can end
            if (G.obSpawnT <= 0) { G.obSpawnT = L->obIvl[0] + (L->obIvl[1] - L->obIvl[0]) * std::min(1.0, k); const int kind = L->obKinds[(size_t)std::floor(rnd() * (double)L->obKinds.size())]; spawnObstacle(G, rnd, kind); }
            for (auto& ob : G.obstacles) {
                ob.x -= 70 * dt; if (ob.kind == 1) { ob.age += dt; gateParts(ob); }
                if (!ob.scored && ob.x + ob.width < s.x) { ob.scored = true; G.score += 150; }
                if (s.alive && s.shield <= 0) for (int pi = 0; pi < ob.nparts; pi++) { const Rect& pt = ob.parts[pi]; if (circleRect(s.x, s.y, 9, ob.x + pt.x, pt.y, pt.w, pt.h)) { playerHit(G, rnd); break; } }
            }
            { size_t kk = 0; for (size_t i = 0; i < G.obstacles.size(); i++) if (G.obstacles[i].x > -200) G.obstacles[kk++] = G.obstacles[i]; G.obstacles.resize(kk); }
        }
        if (G.spawned >= L->quota && G.enemies.empty() && G.ebul.empty() && G.obstacles.empty()) { G.banner = "LEVEL " + std::to_string(G.level) + " CLEAR"; G.bannerT = 2.4; setMode(G, M_LEVELCLEAR); }
    } else {
        if (!G.hasBoss && G.bannerT <= 1.0) {
            G.hasBoss = true;
            G.boss = G.level == 7 ? Boss{W + 170, H / 2, 170, 210, 0, 60, 0, 0, 0, 0.9, 2.0, 3.2, 2.4, 3.2, true, -86}
                                  : Boss{W + 150, H / 2, 140, 170, 0, 40, 0, 0, 0, 1.2, 2.5, 4, 3, 4, false, -72};
        }
        if (G.hasBoss) {
            Boss& b = G.boss; b.t += dt; if (b.flash > 0) b.flash -= dt;
            if (b.dying > 0) {
                b.dying -= dt;
                if (rnd() < 0.5) { const double bx = b.x + (rnd() - 0.5) * b.w; const double by = b.y + (rnd() - 0.5) * b.h; burst(G, bx, by, 6, 140, rnd); }
                G.shake = 0.15;
                if (b.dying <= 0) { if (G.level == 7) { setMode(G, M_VICTORY); G.typed = 0; } else { G.banner = "LEVEL " + std::to_string(G.level) + " CLEAR"; G.bannerT = 2.4; setMode(G, M_LEVELCLEAR); } }
            } else {
                const double tx = W - 96; if (b.x > tx) b.x = std::max(tx, b.x - 60 * dt);
                b.y = H / 2 + std::sin(b.t * 0.6) * (b.hits >= b.maxHits * 0.7 ? 85 : 60);
                if (b.x <= tx + 1) bossFire(G, dt, rnd);
                if (s.alive && std::abs(s.x - b.x) < b.w / 2 + 8 && std::abs(s.y - b.y) < b.h / 2 + 8) playerHit(G, rnd);
            }
        }
    }
}

}}  // namespace vv::vvoid
