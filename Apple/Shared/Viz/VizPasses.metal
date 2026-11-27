// LUA Visuals: post-processing and overlay passes (hand port of viz_render.js). Image space is GL-style (v up); textures are sampled with tx().
#include <metal_stdlib>
using namespace metal;
struct QuadOut { float4 pos [[position]]; float2 uv; };
constexpr sampler smp(filter::linear, address::clamp_to_edge);
static inline float2 tx(float2 uv) { return float2(uv.x, 1. - uv.y); }
static inline float h21(float2 p) { float3 p3 = fract(float3(p.xyx) * .1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
static inline float2x2 rot(float a) { float c = cos(a), s = sin(a); return float2x2(c, -s, s, c); }

vertex QuadOut quad_vs(uint vid [[vertex_id]]) { float2 c = float2(float(vid & 1u), float(vid >> 1)); QuadOut o; o.pos = float4(c * 2. - 1., 0., 1.); o.uv = c; return o; }

struct Comp { float fb, zoom, rot, warp, time, asp; };
fragment float4 comp_fs(QuadOut in [[stage_in]], texture2d<float> scene [[texture(0)]], texture2d<float> over [[texture(1)]], texture2d<float> prev [[texture(2)]], constant Comp& P [[buffer(0)]]) {
    float2 uv = in.uv; float4 ov = over.sample(smp, tx(uv)); float3 sc = scene.sample(smp, tx(uv)).rgb; float3 base = ov.rgb + sc * (1. - ov.a);
    if (P.fb > .001) { float2 c = (uv - .5) * float2(P.asp, 1.); c = rot(P.rot) * (c / P.zoom); c /= float2(P.asp, 1.); c += .5; c += P.warp * .004 * float2(sin(uv.y * 14. + P.time), cos(uv.x * 12. - P.time * 1.1)); base = max(base, prev.sample(smp, tx(c)).rgb * P.fb); }
    return float4(base, 1.);
}
struct Bright { float2 texel; float thr; float pad; };
fragment float4 bright_fs(QuadOut in [[stage_in]], texture2d<float> t [[texture(0)]], constant Bright& P [[buffer(0)]]) {
    float2 uv = in.uv; float3 s = t.sample(smp, tx(uv)).rgb * .4 + t.sample(smp, tx(uv + P.texel * float2(1, 1))).rgb * .15 + t.sample(smp, tx(uv + P.texel * float2(-1, 1))).rgb * .15 + t.sample(smp, tx(uv + P.texel * float2(1, -1))).rgb * .15 + t.sample(smp, tx(uv + P.texel * float2(-1, -1))).rgb * .15;
    float l = max(s.r, max(s.g, s.b)); float k = clamp((l - P.thr) / (1. - P.thr + 1e-3), 0., 1.); return float4(s * k * k * (3. - 2. * k) * 1.5, 1.);
}
struct Blur { float2 dir; float2 pad; };
fragment float4 blur_fs(QuadOut in [[stage_in]], texture2d<float> t [[texture(0)]], constant Blur& P [[buffer(0)]]) {
    float2 uv = in.uv; float3 c = t.sample(smp, tx(uv)).rgb * .227027; c += (t.sample(smp, tx(uv + P.dir * 1.3846)).rgb + t.sample(smp, tx(uv - P.dir * 1.3846)).rgb) * .3162162; c += (t.sample(smp, tx(uv + P.dir * 3.2308)).rgb + t.sample(smp, tx(uv - P.dir * 3.2308)).rgb) * .0702702; return float4(c, 1.);
}
struct Final { float2 texel; float time, bloom, chroma, warp, vig, grain, hue, sat, bri, con, aa, pad; float2 res; };
static inline float3 hueRot(float3 c, float h) {
    const float3x3 toY = float3x3(float3(.299, .596, .211), float3(.587, -.274, -.523), float3(.114, -.322, .312)); const float3x3 fromY = float3x3(float3(1., 1., 1.), float3(.956, -.272, -1.106), float3(.621, -.647, 1.703));
    float3 y = toY * c; float cs = cos(h * 6.28318), sn = sin(h * 6.28318); y = float3(y.x, y.y * cs - y.z * sn, y.y * sn + y.z * cs); return fromY * y;
}
static inline float3 chromaSample(texture2d<float> t, float2 uv, float chroma) { float2 o = (uv - .5) * chroma * .03; return float3(t.sample(smp, tx(uv + o)).r, t.sample(smp, tx(uv)).g, t.sample(smp, tx(uv - o)).b); }
fragment float4 final_fs(QuadOut in [[stage_in]], texture2d<float> t [[texture(0)]], texture2d<float> b1 [[texture(1)]], texture2d<float> b2 [[texture(2)]], constant Final& P [[buffer(0)]]) {
    float2 vUv = in.uv; float2 uv = vUv; uv += P.warp * .006 * float2(sin(uv.y * 20. + P.time * 1.3), cos(uv.x * 18. - P.time)); float3 c = chromaSample(t, uv, P.chroma); const float3 W = float3(.299, .587, .114);
    if (P.aa > .5) { float l0 = dot(c, W); float3 n = chromaSample(t, uv + float2(0., P.texel.y), P.chroma), s = chromaSample(t, uv - float2(0., P.texel.y), P.chroma), e = chromaSample(t, uv + float2(P.texel.x, 0.), P.chroma), w = chromaSample(t, uv - float2(P.texel.x, 0.), P.chroma);
        float ln = dot(n, W), ls = dot(s, W), le = dot(e, W), lw = dot(w, W); float mx = max(l0, max(max(ln, ls), max(le, lw))), mn = min(l0, min(min(ln, ls), min(le, lw))); float k = smoothstep(.08, .35, mx - mn); c = mix(c, (c * 2. + n + s + e + w) / 6., k * .8); }
    c += (b1.sample(smp, tx(vUv)).rgb * .7 + b2.sample(smp, tx(vUv)).rgb * .6) * P.bloom;
    c *= 1. - P.vig * smoothstep(.25, .95, length((vUv - .5) * float2(1.1, 1.)) * 1.35); float2 fc = float2(in.pos.x, P.res.y - in.pos.y); c += (h21(fc + P.time * 60.) - .5) * P.grain * .12;
    c = hueRot(c, P.hue); float l = dot(c, W); c = mix(float3(l), c, P.sat); c = (c - .5) * P.con + .5; c *= P.bri; c = max(c, 0.); c = 1. - exp(-c * 1.5); c = pow(c, float3(.95)); return float4(c, 1.);
}

// ---- Lua canvas commands as instanced quads. Instance = 12 floats: A(4) B(4) C(4); B.z = additive flag (alpha written 0), B.w = shape type.
struct Inst { float4 A; float4 B; float4 C; };
struct BatchOut { float4 pos [[position]]; float2 p; float4 A [[flat]]; float4 B [[flat]]; float4 C [[flat]]; };
vertex BatchOut batch_vs(uint vid [[vertex_id]], uint iid [[instance_id]], constant Inst* inst [[buffer(0)]], constant float2& res [[buffer(1)]]) {
    Inst I = inst[iid]; float4 aA = I.A, aB = I.B; float2 corner = float2(float(vid & 1u), float(vid >> 1)); float ty = aB.w; float2 lo, hi;
    if (ty < .5) { float w = aB.x * .5 + 1.5; lo = min(aA.xy, aA.zw) - w; hi = max(aA.xy, aA.zw) + w; }
    else if (ty < 1.5) { lo = aA.xy - aA.z - 2.; hi = aA.xy + aA.z + 2.; }
    else if (ty < 2.5) { float r = aA.z + aA.w * .5 + 2.; lo = aA.xy - r; hi = aA.xy + r; }
    else if (ty < 3.5) { lo = aA.xy - 2.; hi = aA.xy + aA.zw + 2.; }
    else if (ty < 4.5) { float r = aA.z * 1.6 + 2.; lo = aA.xy - r; hi = aA.xy + r; }
    else { lo = min(min(aA.xy, aA.zw), aB.xy) - 2.; hi = max(max(aA.xy, aA.zw), aB.xy) + 2.; }
    float2 p = mix(lo, hi, corner); BatchOut o; o.p = p; o.A = I.A; o.B = I.B; o.C = I.C; o.pos = float4(p.x / res.x * 2. - 1., 1. - p.y / res.y * 2., 0., 1.); return o;
}
static inline float sdSeg(float2 p, float2 a, float2 b) { float2 pa = p - a, ba = b - a; float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0., 1.); return length(pa - ba * h); }
static inline float sdTri(float2 p, float2 a, float2 b, float2 c) {
    float2 e0 = b - a, e1 = c - b, e2 = a - c; float2 v0 = p - a, v1 = p - b, v2 = p - c; float2 p0 = v0 - e0 * clamp(dot(v0, e0) / max(dot(e0, e0), 1e-6), 0., 1.), p1 = v1 - e1 * clamp(dot(v1, e1) / max(dot(e1, e1), 1e-6), 0., 1.), p2 = v2 - e2 * clamp(dot(v2, e2) / max(dot(e2, e2), 1e-6), 0., 1.);
    float s = sign(e0.x * e2.y - e0.y * e2.x); float2 d = min(min(float2(dot(p0, p0), s * (v0.x * e0.y - v0.y * e0.x)), float2(dot(p1, p1), s * (v1.x * e1.y - v1.y * e1.x))), float2(dot(p2, p2), s * (v2.x * e2.y - v2.y * e2.x))); return -sqrt(d.x) * sign(d.y);
}
fragment float4 batch_fs(BatchOut in [[stage_in]]) {
    float4 fA = in.A, fB = in.B, fC = in.C; float2 vP = in.p; float ty = fB.w; float cov = 0.;
    if (ty < .5) cov = clamp(.5 - (sdSeg(vP, fA.xy, fA.zw) - fB.x * .5), 0., 1.);
    else if (ty < 1.5) cov = clamp(.5 - (length(vP - fA.xy) - fA.z), 0., 1.);
    else if (ty < 2.5) cov = clamp(.5 - (abs(length(vP - fA.xy) - fA.z) - fA.w * .5), 0., 1.);
    else if (ty < 3.5) { float2 c = fA.xy + fA.zw * .5; float2 q = abs(vP - c) - fA.zw * .5; cov = clamp(.5 - (length(max(q, 0.)) + min(max(q.x, q.y), 0.)), 0., 1.); }
    else if (ty < 4.5) { float d = length(vP - fA.xy) / max(fA.z, .5); cov = exp(-d * d * 2.2) * (1. - smoothstep(1.3, 1.6, d)); }
    else cov = clamp(.5 - sdTri(vP, fA.xy, fA.zw, fB.xy), 0., 1.);
    float a = fC.a * cov; if (a <= 0.002) discard_fragment(); return float4(fC.rgb * a, fB.z > .5 ? 0. : a);
}

// ---- sand particles: one point per state texel (K layout as VizScenes.metal: [0,1] res, [5,6] state res, [7] point size, [24+] u)
struct SandOut { float4 pos [[position]]; float size [[point_size]]; float speed; float seed; };
vertex SandOut sand_vs(uint vid [[vertex_id]], texture2d<float> st [[texture(0)]], constant float* K [[buffer(0)]]) {
    uint w = uint(K[5]); float4 s = st.read(uint2(vid % w, vid / w)); float2 p = s.xy * K[24 + 29]; float2 pos = float2(p.x * K[1] / K[0], p.y) * 2.;
    SandOut o; o.pos = float4(pos, 0., 1.); o.size = K[7]; o.speed = length(s.zw); o.seed = fract(sin(float(vid) * 12.9898) * 43758.5453); return o;
}
static inline float3 palS(float t, float sh) { return .5 + .5 * cos(6.28318 * (t + sh + float3(0., .33, .67))); }
fragment float4 sand_fs(SandOut in [[stage_in]], float2 pc [[point_coord]], constant float* K [[buffer(0)]]) {
    float2 c = pc - .5; float d = length(c); if (d > .5) discard_fragment(); float shade = smoothstep(.5, .1, d); float moving = clamp(in.speed * 1.5, 0., 1.); constant float* U = K + 24;
    float3 sand = mix(float3(.9, .72, .44), float3(1., .88, .66), in.seed) * (.8 + .35 * shade); sand = mix(sand, palS(U[14], 0.) * .4 + sand * .7, U[26]); sand += moving * palS(U[14] + .3, 0.) * .5 * U[27];
    return float4(sand * (.55 + .6 * shade), 1.) * smoothstep(.5, .42, d);
}
