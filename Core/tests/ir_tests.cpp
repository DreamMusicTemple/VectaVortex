// IR convolution reverb + EQ rack slot: generator parity with the web module, shaping, WAV/AIFF import, resampling, library, partitioned convolver vs direct
// convolution, click-free IR switching, allocation-free audio path, CPU cost, and the EQ slot equalling the per-layer EQ.
#include <cstdio>
#include <cmath>
#include <chrono>
#include <string>
#include <vector>
#include <new>
#include <cstdlib>
#include <random>
#include "vv/fx_chain.hpp"
#include "vv/mod_defs.hpp"
#include "ir_parity.inc"
static thread_local bool g_count = false; static thread_local long g_allocs = 0;
void* operator new(size_t n) { if (g_count) g_allocs++; void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
using namespace vv;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
static const double SR = 48000;

static std::vector<uint8_t> wav(int ch, int bits, int sr, int frames, bool flt = false, bool aiff = false) {
    const int bps = bits / 8, n = frames * ch * bps; std::vector<uint8_t> b;
    auto w32 = [&](uint32_t v, bool be) { for (int k = 0; k < 4; k++) b.push_back((uint8_t)(be ? v >> (24 - 8 * k) : v >> (8 * k))); }; auto w16 = [&](uint32_t v, bool be) { for (int k = 0; k < 2; k++) b.push_back((uint8_t)(be ? v >> (8 - 8 * k) : v >> (8 * k))); };
    auto tag = [&](const char* t) { for (int k = 0; k < 4; k++) b.push_back((uint8_t)t[k]); };
    if (!aiff) { tag("RIFF"); w32(36 + n, false); tag("WAVE"); tag("fmt "); w32(16, false); w16(flt ? 3 : 1, false); w16(ch, false); w32(sr, false); w32(sr * ch * bps, false); w16(ch * bps, false); w16(bits, false); tag("data"); w32(n, false); }
    else { tag("FORM"); w32(46 + n, true); tag("AIFF"); tag("COMM"); w32(18, true); w16(ch, true); w32(frames, true); w16(bits, true); const int e = (int)std::floor(std::log2((double)sr)); w16(16383 + e, true); const uint64_t m = (uint64_t)(sr / std::pow(2.0, e) * std::pow(2.0, 63)); w32((uint32_t)(m >> 32), true); w32((uint32_t)m, true); tag("SSND"); w32(8 + n, true); w32(0, true); w32(0, true); }
    for (int i = 0; i < frames; i++) for (int c = 0; c < ch; c++) { const double v = (i == 10 ? 0.5 : 0) + (i == 20 ? -0.25 * (c + 1) : 0);
        if (flt) { float f = (float)v; uint32_t w; memcpy(&w, &f, 4); w32(w, false); } else if (bits == 16) w16((uint32_t)(int16_t)std::lround(v * 32767), aiff); else if (bits == 24) { const int32_t a = (int32_t)std::lround(v * 8388607); if (aiff) { b.push_back((uint8_t)(a >> 16)); b.push_back((uint8_t)(a >> 8)); b.push_back((uint8_t)a); } else { b.push_back((uint8_t)a); b.push_back((uint8_t)(a >> 8)); b.push_back((uint8_t)(a >> 16)); } }
        else if (bits == 8) b.push_back((uint8_t)std::lround(v * 127 + 128)); else w32((uint32_t)(int32_t)std::lround(v * 2147483647.0), aiff); }
    return b;
}
static double rms(const std::vector<float>& a, size_t f, size_t t) { double s = 0; for (size_t i = f; i < t && i < a.size(); i++) s += (double)a[i] * a[i]; return std::sqrt(s / std::max<size_t>(1, t - f)); }

int main() {
    // 1. factory table and parity with the web generator --------------------------------------------------------------------------------------------------
    CHECK(ir::kFactoryCount >= 33 && ir::kFactoryCount == kIrParityCount, "factory count %d vs %d", ir::kFactoryCount, kIrParityCount);
    for (int c = 0; c < ir::kCategoryCount; c++) { int k = 0; for (int i = 0; i < ir::kFactoryCount; i++) if (std::string(ir::kFactory[i].cat) == ir::kCategories[c]) k++; CHECK(k >= 3, "category %s has %d", ir::kCategories[c], k); }
    double worst = 0;
    for (int i = 0; i < ir::kFactoryCount; i++) {
        const auto& P = kIrParity[i]; CHECK(std::string(P.id) == ir::kFactory[i].id, "order %s", P.id);
        ir::Buf b = ir::generate(ir::kFactory[i], SR); CHECK((int)b.L.size() == P.n, "%s length %zu vs %d", P.id, b.L.size(), P.n);
        double sl = 0, sr = 0, e = 0; for (size_t k = 0; k < b.L.size(); k++) { sl += std::fabs(b.L[k]); sr += std::fabs(b.R[k]); e += (double)b.L[k] * b.L[k] + (double)b.R[k] * b.R[k]; }
        CHECK(std::fabs(sl / P.sumL - 1) < 2e-4 && std::fabs(sr / P.sumR - 1) < 2e-4, "%s sums %.5f %.5f vs %.5f %.5f", P.id, sl, sr, P.sumL, P.sumR);
        CHECK(std::fabs(e / 2 - 1) < 1e-3, "%s unit energy %.5f", P.id, e / 2);
        for (int k = 0; k < 6; k++) { const double dl = std::fabs(b.L[(size_t)P.idx[k]] - P.L[k]), dr = std::fabs(b.R[(size_t)P.idx[k]] - P.R[k]); worst = std::max(worst, std::max(dl, dr)); CHECK(dl < 2e-5 + 1e-3 * std::fabs(P.L[k]) && dr < 2e-5 + 1e-3 * std::fabs(P.R[k]), "%s sample %d: %.9g/%.9g vs %.9g/%.9g", P.id, P.idx[k], b.L[(size_t)P.idx[k]], b.R[(size_t)P.idx[k]], P.L[k], P.R[k]); }
    }
    printf("generator parity with the web module: worst sample difference %.3g\n", worst);
    { ir::Buf base = ir::generate(ir::kFactory[0], SR); ir::Shape s; s.start = 0.1; s.end = 0.8; s.lenPct = 70; s.earlyLate = 0.6; s.damping = 0.5; s.reverse = true; s.irGain = -3; ir::Buf o = ir::process(base, s);
      CHECK((int)o.L.size() == kShapedN, "shaped length %zu vs %d", o.L.size(), kShapedN);
      for (int k = 0; k < 5; k++) { const size_t ix = (size_t)kShapedIdx[k]; CHECK(std::fabs(o.L[ix] - kShapedL[k]) < 3e-5 + 2e-3 * std::fabs(kShapedL[k]) && std::fabs(o.R[ix] - kShapedR[k]) < 3e-5 + 2e-3 * std::fabs(kShapedR[k]), "shaped sample %d: %.8g vs %.8g", kShapedIdx[k], o.L[ix], kShapedL[k]); } }

    // 2. shaping --------------------------------------------------------------------------------------------------------------------------------------------------
    { ir::Buf base = ir::generate(ir::kFactory[0], SR); const int N = (int)base.L.size(); ir::Shape s; ir::Buf p = ir::process(base, s); CHECK((int)p.L.size() == N, "identity length");
      double md = 0; for (int i = 0; i < N; i++) md = std::max(md, (double)std::fabs(p.L[(size_t)i] - base.L[(size_t)i])); CHECK(md < 0.05, "default shape is (nearly) identity: %.4f", md);
      s = ir::Shape(); s.start = 0.25; s.end = 0.75; p = ir::process(base, s); CHECK(std::abs((int)p.L.size() - (int)std::ceil(0.5 * N)) <= 2, "trim length %zu", p.L.size());
      s = ir::Shape(); s.lenPct = 50; p = ir::process(base, s); CHECK(std::abs((int)p.L.size() - N / 2) <= 1, "length 50%%");
      ir::Shape r0; r0.normalise = false; ir::Shape r1 = r0; r1.reverse = true; ir::Buf a = ir::process(base, r0), b = ir::process(base, r1); bool ok = true; for (int i = 0; i < 200; i++) if (std::fabs(b.L[(size_t)i] - a.L[a.L.size() - 1 - (size_t)i]) > 1e-6) ok = false; CHECK(ok, "reverse mirrors");
      ir::Shape g = r0; g.irGain = 6; ir::Buf gg = ir::process(base, g); size_t k = 0; while (k < a.L.size() && std::fabs(a.L[k]) < 1e-3) k++; CHECK(std::fabs(gg.L[k] / a.L[k] - 1.9953) < 0.01, "gain +6 dB (%.4f)", gg.L[k] / a.L[k]);
      auto en = [&](const ir::Buf& x, double f, double t) { double e = 0; for (size_t i = (size_t)(f * SR); i < (size_t)(t * SR) && i < x.L.size(); i++) e += (double)x.L[i] * x.L[i]; return e; };
      ir::Shape el = r0; el.earlyLate = 1; ir::Buf e1 = ir::process(base, el); const double q0 = en(a, 0, 0.05) / (en(a, 0.2, 0.5) + 1e-30), q1 = en(e1, 0, 0.05) / (en(e1, 0.2, 0.5) + 1e-30); CHECK(q1 < q0 * 0.2, "early/late +1 favours late (%.3f -> %.3f)", q0, q1);
      ir::Shape dm = r0; dm.damping = 1; ir::Buf d1 = ir::process(base, dm); double hA = 0, hB = 0; for (size_t i = 19201; i < 38400; i++) { hA += std::pow((double)a.L[i] - a.L[i - 1], 2); hB += std::pow((double)d1.L[i] - d1.L[i - 1], 2); } CHECK(hB < hA * 0.5, "damping removes HF");
      ir::Buf sil; sil.sr = SR; sil.L.assign((size_t)(SR * 2), 0.f); sil.R = sil.L; sil.L[(size_t)(SR / 2)] = 1; sil.R[(size_t)(SR / 2)] = 1; sil.L[(size_t)SR] = 0.01f; double st, en2; ir::autoTrim(sil, st, en2); CHECK(st > 0.2 && st < 0.26 && en2 > 0.49 && en2 < 0.56, "auto trim %.3f %.3f", st, en2); }

    // 3. resampler -------------------------------------------------------------------------------------------------------------------------------------------------
    { const double pairs[3][2] = {{44100, 48000}, {96000, 48000}, {48000, 44100}};
      for (auto& pr : pairs) { std::vector<float> x((size_t)pr[0]); for (size_t i = 0; i < x.size(); i++) x[i] = (float)std::sin(2 * M_PI * 1000 * (double)i / pr[0]); auto y = ir::resample(x, pr[0], pr[1]); double err = 0; int cnt = 0;
        for (size_t i = 200; i + 200 < y.size(); i++) { err += std::fabs(y[i] - std::sin(2 * M_PI * 1000 * (double)i / pr[1])); cnt++; } CHECK(std::abs((int)y.size() - (int)pr[1]) <= 1 && err / cnt < 0.01, "resample %.0f->%.0f err %.4f len %zu", pr[0], pr[1], err / cnt, y.size()); }
      std::vector<float> h((size_t)96000); for (size_t i = 0; i < h.size(); i++) h[i] = (float)std::sin(2 * M_PI * 30000 * (double)i / 96000); auto y = ir::resample(h, 96000, 48000); double pw = 0; for (size_t i = 200; i + 200 < y.size(); i++) pw += (double)y[i] * y[i]; CHECK(std::sqrt(pw / (y.size() - 400)) < 0.05, "alias rejected"); }

    // 4. import ----------------------------------------------------------------------------------------------------------------------------------------------------
    { struct C { int ch, bits; bool flt, aiff; } cases[] = {{1, 16, false, false}, {2, 16, false, false}, {1, 24, false, false}, {2, 32, true, false}, {2, 8, false, false}, {2, 16, false, true}, {1, 24, false, true}};
      for (auto& c : cases) { auto b = wav(c.ch, c.bits, 44100, 500, c.flt, c.aiff); ir::Parsed r = ir::parseFile(b.data(), b.size()); CHECK(r.ok, "parse %dch %db flt=%d aiff=%d: %s", c.ch, c.bits, c.flt, c.aiff, r.error.c_str());
        if (r.ok) { const double tol = c.bits == 8 ? 0.02 : 0.001; CHECK(r.sr == 44100 && r.frames == 500 && r.ch == c.ch && std::fabs(r.L[10] - 0.5) < tol, "meta/sample %.0f %zu %d %.4f", r.sr, r.frames, r.ch, r.L[10]); CHECK(c.ch == 2 ? std::fabs(r.R[20] + 0.5) < tol : std::fabs(r.R[10] - 0.5) < tol, "R channel"); } }
      std::vector<std::vector<uint8_t>> bad; bad.push_back({}); bad.push_back(std::vector<uint8_t>(100, 7)); { std::string t(400, 'x'); bad.push_back(std::vector<uint8_t>(t.begin(), t.end())); } bad.push_back(wav(1, 16, 44100, 4)); bad.push_back(wav(1, 16, 1000, 500));
      { auto s = wav(1, 16, 44100, 500); std::fill(s.begin() + 44, s.end(), 0); bad.push_back(s); } { auto s = wav(2, 32, 44100, 500, true); uint32_t nan = 0x7fc00000; memcpy(&s[44 + 40], &nan, 4); bad.push_back(s); }
      { auto s = wav(2, 16, 44100, 500); s.resize(700); bad.push_back(s); }
      for (size_t i = 0; i < bad.size(); i++) { ir::Parsed r = ir::parseFile(bad.data() ? bad[i].data() : nullptr, bad[i].size()); if (i == 7) CHECK(r.ok || !r.error.empty(), "truncated file handled"); else CHECK(!r.ok && !r.error.empty(), "invalid %zu rejected: %s", i, r.ok ? "accepted" : r.error.c_str()); }
      // fuzz: random byte flips must never crash and always end in ok or a message
      std::mt19937 rng(7); auto base = wav(2, 16, 44100, 800); for (int t = 0; t < 400; t++) { auto s = base; const int nf = 1 + (int)(rng() % 6); for (int k = 0; k < nf; k++) s[rng() % std::min<size_t>(s.size(), 80)] = (uint8_t)rng(); if (rng() % 4 == 0) s.resize(rng() % s.size()); ir::Parsed r = ir::parseFile(s.data(), s.size()); CHECK(r.ok || !r.error.empty(), "fuzz %d", t); } }

    // 5. library ---------------------------------------------------------------------------------------------------------------------------------------------------
    std::string uid;
    { auto b = wav(1, 16, 22050, 4800); b[44 + 20] = 0x30; b[44 + 21] = 0x75; std::string id, err; std::vector<std::string> w; CHECK(ir::Library::get().add(b.data(), b.size(), "My Room", id, err, w), "add: %s", err.c_str()); uid = id; CHECK(id.rfind("u:", 0) == 0 && id.size() == 10, "id %s", id.c_str());
      std::string id2; CHECK(ir::Library::get().add(b.data(), b.size(), "My Room", id2, err, w) && id2 == id, "same file -> same id (stable across launches)");
      bool miss = false; auto src = ir::Library::get().source(id, SR, &miss); CHECK(!miss && std::abs((int)src->L.size() - (int)(4800 * SR / 22050)) <= 2, "converted to engine rate: %zu", src->L.size());
      auto fb = ir::Library::get().source("u:ffffffff", SR, &miss); CHECK(miss && fb->name == "Concert Hall", "missing IR falls back to the default hall"); CHECK(ir::Library::get().missingJson().find("u:ffffffff") != std::string::npos, "missing IR is listed");
      CHECK(ir::Library::get().userJson().find("My Room") != std::string::npos, "user list"); CHECK(ir::Library::get().remove(id) && !ir::Library::get().has(id), "remove");
      // file round trip through scanDir
      std::system("rm -rf /tmp/vvir && mkdir -p /tmp/vvir"); std::FILE* f = std::fopen("/tmp/vvir/abcd1234__Cave.wav", "wb"); std::fwrite(b.data(), 1, b.size(), f); std::fclose(f); std::FILE* g = std::fopen("/tmp/vvir/notes.txt", "wb"); std::fputs("x", g); std::fclose(g);
      CHECK(ir::Library::get().scanDir("/tmp/vvir") == 1 && ir::Library::get().userJson().find("Cave") != std::string::npos, "scanDir registers audio files only"); uid = id; }

    // 6. partitioned convolver against direct convolution -----------------------------------------------------------------------------------------------------------
    { std::mt19937 rng(3); std::uniform_real_distribution<float> U(-1, 1); const int lens[] = {1, 100, 2048, 2049, 6144, 12288, 12289, 30000, 80000}; double worstRel = 0;
      for (int N : lens) {
        std::vector<float> hl((size_t)N), hr((size_t)N); for (int i = 0; i < N; i++) { const float d = std::exp(-4.0f * i / N); hl[(size_t)i] = U(rng) * d; hr[(size_t)i] = U(rng) * d; }
        const int M = 128 * 40; std::vector<float> xl((size_t)M), xr((size_t)M); for (int i = 0; i < M; i++) { xl[(size_t)i] = U(rng); xr[(size_t)i] = U(rng); }
        ir::PartConv pc; pc.build(hl.data(), hr.data(), N); std::vector<float> yl((size_t)M), yr((size_t)M); for (int i = 0; i < M; i += 128) pc.process(&xl[(size_t)i], &xr[(size_t)i], &yl[(size_t)i], &yr[(size_t)i], 128);
        double maxErr = 0, maxRef = 0; for (int n = 0; n < M; n++) { double rl = 0, rr = 0; const int kmax = std::min(n, N - 1); for (int k = 0; k <= kmax; k++) { rl += (double)hl[(size_t)k] * xl[(size_t)(n - k)]; rr += (double)hr[(size_t)k] * xr[(size_t)(n - k)]; }
            maxErr = std::max(maxErr, std::max(std::fabs(rl - yl[(size_t)n]), std::fabs(rr - yr[(size_t)n]))); maxRef = std::max(maxRef, std::max(std::fabs(rl), std::fabs(rr))); }
        worstRel = std::max(worstRel, maxErr / maxRef); CHECK(maxErr < 2e-4 * maxRef + 1e-6, "IR length %d: max error %.3g (ref peak %.3g), %d levels", N, maxErr, maxRef, pc.levelCount()); }
      printf("partitioned convolver vs direct convolution: worst relative error %.3g\n", worstRel);
      // arbitrary host buffer sizes
      const int N = 20000; std::vector<float> h((size_t)N); for (int i = 0; i < N; i++) h[(size_t)i] = U(rng) * std::exp(-3.0f * i / N); const int M = 128 * 30; std::vector<float> x((size_t)M); for (auto& v : x) v = U(rng);
      auto runBlocks = [&](int blk) { ir::PartConv pc; pc.build(h.data(), h.data(), N); std::vector<float> o((size_t)M), o2((size_t)M); for (int i = 0; i < M; i += blk) { const int n = std::min(blk, M - i); pc.process(&x[(size_t)i], &x[(size_t)i], &o[(size_t)i], &o2[(size_t)i], n); } return o; };
      auto ref = runBlocks(128); auto b256 = runBlocks(256), b512 = runBlocks(512); double d = 0; for (int i = 0; i < M; i++) d = std::max(d, (double)std::fabs(ref[(size_t)i] - b256[(size_t)i])); for (int i = 0; i < M; i++) d = std::max(d, (double)std::fabs(ref[(size_t)i] - b512[(size_t)i])); CHECK(d < 1e-5, "128 / 256 / 512 host buffers agree: %.3g", d);
      auto b64 = runBlocks(64); double d2 = 0; for (int i = 128; i < M; i++) d2 = std::max(d2, (double)std::fabs(ref[(size_t)(i - 128)] - b64[(size_t)i])); CHECK(d2 < 1e-5, "64-frame buffers: same result delayed by exactly one chunk (%.3g)", d2); }

    // 7. IrStage: impulse response reproduced, pre-delay, mix, click-free switching, allocation-free audio path ----------------------------------------------------------------------
    {
        FxEnv env; env.sr = SR; auto run = [&](IrStage& st, const std::vector<float>& inL, const std::vector<float>& inR, std::vector<float>& oL, std::vector<float>& oR) { oL.resize(inL.size()); oR.resize(inL.size()); for (size_t i = 0; i < inL.size(); i += 128) st.process(&inL[i], &inR[i], &oL[i], &oR[i], 128); };
        IrStage st(SR); st.configure(JsonParser::parse("{\"irId\":\"f:plate-short\",\"mix\":1,\"outLevel\":0}"), true, env); CHECK(ir::Worker::get().waitIdle(15000), "worker idle");
        const int M = 128 * 400; std::vector<float> in(M, 0.f), z(M, 0.f), oL, oR; in[0] = 1; std::vector<float> warm(128, 0.f), dummy(128); st.process(warm.data(), warm.data(), dummy.data(), dummy.data(), 128);   // adopts the engine
        run(st, in, z, oL, oR); ir::Buf pr = ir::process(*ir::Library::get().source("f:plate-short", SR), ir::Shape());
        // wet path has hp (20 Hz) / lp (20 kHz) / mono-in: compare against IR run through the same dry impulse; input is L-only impulse into the L IR channel
        double sab = 0, saa = 0, sbb = 0; for (int i = 0; i < 20000; i++) { sab += (double)oL[(size_t)i] * pr.L[(size_t)i]; saa += (double)oL[(size_t)i] * oL[(size_t)i]; sbb += (double)pr.L[(size_t)i] * pr.L[(size_t)i]; }
        const double corrRaw = sab / std::sqrt(saa * sbb + 1e-30); auto smooth = [&](auto get) { std::vector<double> r(20000 - 8); for (int i = 0; i < 20000 - 8; i++) { double a = 0; for (int k = 0; k < 8; k++) a += get(i + k); r[(size_t)i] = a / 8; } return r; };
        auto s1 = smooth([&](int i) { return (double)oL[(size_t)i]; }), s2 = smooth([&](int i) { return (double)pr.L[(size_t)i]; }); double q1 = 0, q2 = 0, q3 = 0; for (size_t i = 0; i < s1.size(); i++) { q1 += s1[i] * s2[i]; q2 += s1[i] * s1[i]; q3 += s2[i] * s2[i]; }
        const double corrLP = q1 / std::sqrt(q2 * q3 + 1e-30); CHECK(corrRaw > 0.75 && corrLP > 0.98, "stage output reproduces the IR (corr %.4f, low-passed %.4f; the 20 kHz output filter changes only the top octave)", corrRaw, corrLP);
        // mix 0 passes the dry signal untouched
        IrStage d0(SR); d0.configure(JsonParser::parse("{\"irId\":\"f:hall-concert\",\"mix\":0}"), true, env); ir::Worker::get().waitIdle(15000); std::vector<float> a0, b0; run(d0, in, z, a0, b0); CHECK(std::fabs(a0[0] - 1) < 1e-4, "mix 0 passes the dry click (%.5f)", a0[0]);
        // pre-delay shifts the response by 100 ms
        IrStage pd(SR); pd.configure(JsonParser::parse("{\"irId\":\"f:plate-short\",\"mix\":1,\"predelay\":100}"), true, env); ir::Worker::get().waitIdle(15000); pd.process(warm.data(), warm.data(), dummy.data(), dummy.data(), 128); std::vector<float> pL, pR; run(pd, in, z, pL, pR);
        int pk0 = 0, pk1 = 0; for (int i = 0; i < 20000; i++) { if (std::fabs(oL[(size_t)i]) > std::fabs(oL[(size_t)pk0])) pk0 = i; if (std::fabs(pL[(size_t)i + 0]) > std::fabs(pL[(size_t)pk1])) pk1 = i; }
        double sh = 0; { double best = -1; int bs = 0; for (int s = 4000; s < 5600; s++) { double c = 0; for (int i = 0; i < 6000; i++) c += (double)pL[(size_t)(i + s)] * oL[(size_t)i]; if (c > best) { best = c; bs = s; } } sh = bs / SR * 1000; } CHECK(std::fabs(sh - 100) < 1.5, "pre-delay 100 ms shifts the response by %.2f ms", sh); (void)pk0; (void)pk1;
        // width 0 -> mono, balance -1 -> right silent
        IrStage wd(SR); wd.configure(JsonParser::parse("{\"irId\":\"f:plate-short\",\"mix\":1,\"width\":0}"), true, env); ir::Worker::get().waitIdle(15000); wd.process(warm.data(), warm.data(), dummy.data(), dummy.data(), 128); std::vector<float> wl, wr; run(wd, in, in, wl, wr); double dd = 0, ss = 0; for (size_t i = 0; i < wl.size(); i++) { dd += std::pow((double)wl[i] - wr[i], 2); ss += (double)wl[i] * wl[i]; } CHECK(dd / (ss + 1e-30) < 1e-4, "width 0 is mono (%.2g)", dd / (ss + 1e-30));
        IrStage bl(SR); bl.configure(JsonParser::parse("{\"irId\":\"f:plate-short\",\"mix\":1,\"balance\":-1}"), true, env); ir::Worker::get().waitIdle(15000); bl.process(warm.data(), warm.data(), dummy.data(), dummy.data(), 128); std::vector<float> bL, bR; run(bl, in, in, bL, bR); double er = 0, el = 0; for (size_t i = 0; i < bL.size(); i++) { er += (double)bR[i] * bR[i]; el += (double)bL[i] * bL[i]; } CHECK(er / (el + 1e-30) < 1e-6, "balance -1 silences the right channel");
        // click-free IR switch while noise plays; audio path allocation-free
        IrStage sw(SR); sw.configure(JsonParser::parse("{\"irId\":\"f:hall-concert\",\"mix\":1}"), true, env); ir::Worker::get().waitIdle(15000); std::mt19937 rng(5); std::uniform_real_distribution<float> U(-0.2f, 0.2f);
        const int blocks = 600; std::vector<float> nl((size_t)blocks * 128), nr((size_t)blocks * 128), ol((size_t)blocks * 128), orr((size_t)blocks * 128); for (auto& v : nl) v = U(rng); nr = nl;
        long allocs = 0; double maxD = 0, sumD = 0; long cnt = 0; double worstBlockMs = 0, totMs = 0;
        for (int b = 0; b < blocks; b++) {
            if (b == 200) sw.configure(JsonParser::parse("{\"irId\":\"f:church-small\",\"mix\":1}"), false, env);
            if (b == 205) ir::Worker::get().waitIdle(15000);
            g_allocs = 0; g_count = true; const auto t0 = std::chrono::steady_clock::now(); sw.process(&nl[(size_t)b * 128], &nr[(size_t)b * 128], &ol[(size_t)b * 128], &orr[(size_t)b * 128], 128); const auto t1 = std::chrono::steady_clock::now(); g_count = false; allocs += g_allocs;
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count(); worstBlockMs = std::max(worstBlockMs, ms); totMs += ms; }
        for (size_t i = 128 * 20; i + 1 < ol.size(); i++) { const double d = std::fabs(ol[i + 1] - ol[i]); maxD = std::max(maxD, d); sumD += d; cnt++; }
        CHECK(allocs == 0, "audio path allocated %ld times (including the IR swap)", allocs); CHECK(maxD / (sumD / cnt) < 14, "IR switch has no discontinuity (peak/mean slope %.2f)", maxD / (sumD / cnt));
        printf("IrStage 2-level churches/halls: %.4f ms mean, %.3f ms worst per 128-frame block (budget 2.67 ms)\n", totMs / blocks, worstBlockMs);
        // CPU at the 12 s worst case
        IrStage big(SR); big.configure(JsonParser::parse("{\"irId\":\"f:exp-frozen\",\"mix\":1}"), true, env); ir::Worker::get().waitIdle(30000); big.process(warm.data(), warm.data(), dummy.data(), dummy.data(), 128); double w2 = 0, t2 = 0; const int nb = 800;
        for (int b = 0; b < nb; b++) { const auto t0 = std::chrono::steady_clock::now(); big.process(&nl[(size_t)(b % 600) * 128], &nr[(size_t)(b % 600) * 128], &ol[(size_t)(b % 600) * 128], &orr[(size_t)(b % 600) * 128], 128); const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); w2 = std::max(w2, ms); t2 += ms; }
        printf("IrStage %.1f s IR: %.4f ms mean, %.3f ms worst per 128-frame block\n", big.irSeconds(), t2 / nb, w2); CHECK(t2 / nb < 1.5, "12 s IR mean cost %.3f ms per block", t2 / nb);
        // a missing user IR does not fail, is reported
        IrStage ms(SR); ms.configure(JsonParser::parse("{\"irId\":\"u:deadbeef\",\"mix\":1}"), true, env); ir::Worker::get().waitIdle(15000); ms.process(warm.data(), warm.data(), dummy.data(), dummy.data(), 128); CHECK(ms.missingIr() && ms.engineActive(), "missing IR: default used and flagged");
        // IR-shaping params change the sound through a rebuild (no crash with rapid changes)
        IrStage rp(SR); rp.configure(JsonParser::parse("{\"irId\":\"f:hall-concert\",\"mix\":1}"), true, env); for (int k = 0; k < 40; k++) { char js[200]; std::snprintf(js, sizeof js, "{\"irId\":\"f:hall-concert\",\"mix\":1,\"lenPct\":%d,\"damping\":%g}", 20 + k, k / 40.0); rp.configure(JsonParser::parse(js), false, env); rp.process(warm.data(), warm.data(), dummy.data(), dummy.data(), 128); }
        ir::Worker::get().waitIdle(15000); rp.process(warm.data(), warm.data(), dummy.data(), dummy.data(), 128); CHECK(rp.engineActive() && rp.irSeconds() > 0.2, "rapid parameter changes settle on the last request (%.2f s)", rp.irSeconds());
    }

    // 8. EQ rack slot equals the existing EQ ---------------------------------------------------------------------------------------------------------------------------------------------
    {
        FxEnv env; env.sr = SR; EqStage eq(SR); eq.configure(JsonParser::parse("{\"f1\":120,\"g1\":4,\"f2\":1000,\"g2\":12,\"f3\":2500,\"g3\":-6,\"f4\":8000,\"g4\":3,\"out\":0,\"mix\":1}"), true, env);
        std::vector<float> x((size_t)48000), y((size_t)48000), a((size_t)48000), b((size_t)48000); for (size_t i = 0; i < x.size(); i++) x[i] = (float)(0.3 * std::sin(2 * M_PI * 1000 * (double)i / SR) + 0.1 * std::sin(2 * M_PI * 220 * (double)i / SR));
        for (size_t i = 0; i < x.size(); i += 128) eq.process(&x[i], &x[i], &a[i], &b[i], 128);
        EQ ref; ref.prepare(SR); EQ::Params ep; const double f[4] = {120, 1000, 2500, 8000}, g[4] = {4, 12, -6, 3}; for (int k = 0; k < 4; k++) { ep.freq[k] = f[k]; ep.gain[k] = g[k]; } for (size_t i = 0; i < x.size(); i += 128) ref.process(&x[i], &y[i], 128, ep);
        double md = 0; for (size_t i = 0; i < x.size(); i++) md = std::max(md, (double)std::fabs(a[i] - y[i])); CHECK(md < 1e-6, "EQ slot output equals the layer EQ code exactly (%.3g)", md); CHECK(std::fabs(a[20000] - b[20000]) < 1e-9, "both channels equal for mono input");
        // gain at 1 kHz and wet/dry / output trim
        EqStage e2(SR); e2.configure(JsonParser::parse("{\"f2\":1000,\"g2\":12,\"mix\":1}"), true, env); std::vector<float> t1(48000), t2(48000); for (size_t i = 0; i < 48000; i += 128) e2.process(&x[i], &x[i], &t1[i], &t2[i], 128); // x has a 220 Hz part too, so use a pure tone below
        std::vector<float> s((size_t)48000), o1(48000), o2(48000); for (size_t i = 0; i < s.size(); i++) s[i] = (float)(0.3 * std::sin(2 * M_PI * 1000 * (double)i / SR)); EqStage e3(SR); e3.configure(JsonParser::parse("{\"f2\":1000,\"g2\":12,\"mix\":1}"), true, env); for (size_t i = 0; i < s.size(); i += 128) e3.process(&s[i], &s[i], &o1[i], &o2[i], 128);
        CHECK(std::fabs(20 * std::log10(rms(o1, 24000, 48000) / rms(s, 24000, 48000)) - 12) < 0.5, "+12 dB bell at 1 kHz: %.2f dB", 20 * std::log10(rms(o1, 24000, 48000) / rms(s, 24000, 48000)));
        EqStage e4(SR); e4.configure(JsonParser::parse("{\"f2\":1000,\"g2\":12,\"mix\":0}"), true, env); for (size_t i = 0; i < s.size(); i += 128) e4.process(&s[i], &s[i], &o1[i], &o2[i], 128); CHECK(std::fabs(20 * std::log10(rms(o1, 24000, 48000) / rms(s, 24000, 48000))) < 0.05, "mix 0 is dry");
        EqStage e5(SR); e5.configure(JsonParser::parse("{\"f2\":1000,\"g2\":12,\"mix\":1,\"out\":-6}"), true, env); for (size_t i = 0; i < s.size(); i += 128) e5.process(&s[i], &s[i], &o1[i], &o2[i], 128); CHECK(std::fabs(20 * std::log10(rms(o1, 24000, 48000) / rms(s, 24000, 48000)) - 6) < 0.5, "output -6 dB");
        std::vector<float> lo(48000, 0.f); EqStage e6(SR); e6.configure(JsonParser::parse("{\"f2\":1000,\"g2\":12,\"mix\":1}"), true, env); for (size_t i = 0; i < s.size(); i += 128) e6.process(&s[i], &lo[i], &o1[i], &o2[i], 128); CHECK(rms(o2, 24000, 48000) < 1e-6 && rms(o1, 24000, 48000) > 0.1, "EQ slot keeps the stereo image");
        // allocation-free
        EqStage e7(SR); e7.configure(JsonParser::parse("{\"mix\":1}"), true, env); g_allocs = 0; g_count = true; for (size_t i = 0; i < 12800; i += 128) { e7.configure(JsonParser::parse("{\"f2\":1000,\"g2\":3,\"mix\":1}"), false, env); e7.process(&s[i], &s[i], &o1[i], &o2[i], 128); } g_count = false; /* configure parses JSON (UI-side); the process call itself must not allocate */
        g_allocs = 0; g_count = true; for (size_t i = 0; i < 12800; i += 128) e7.process(&s[i], &s[i], &o1[i], &o2[i], 128); g_count = false; CHECK(g_allocs == 0, "EQ slot process allocates %ld times", g_allocs);
    }
    // 9. chain integration: both stage types build from preset JSON, multiple EQ instances ------------------------------------------------------------------------------------------------
    {
        FxChain ch; ch.prepare(SR); std::vector<FxStageCfg> cfg; for (const char* t : {"eq", "ir", "eq"}) { FxStageCfg c; c.id = (int)cfg.size() + 1; c.type = t; c.params = JsonParser::parse(std::string(t) == "ir" ? "{\"irId\":\"f:room-small\",\"mix\":0.5}" : "{\"g2\":6,\"f2\":1000}"); c.sends[0] = true; cfg.push_back(c); }
        ch.configure(cfg, 120, "normal"); CHECK(ch.stageCount() == 3, "3 stages built (multiple EQ instances)"); ir::Worker::get().waitIdle(15000);
        std::vector<float> in(128, 0.2f), L(128), R(128); const float* sl[kFxLayers]; const float* sr[kFxLayers]; static float zero[128] = {}; for (int k = 0; k < kFxLayers; k++) { sl[k] = zero; sr[k] = zero; } sl[0] = in.data(); sr[0] = in.data();
        bool fin = true; for (int b = 0; b < 200; b++) { ch.process(sl, sr, L.data(), R.data(), 128); for (int i = 0; i < 128; i++) if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) fin = false; } CHECK(fin, "chain output finite");
        bool hasFxp = false; for (int i = 0; i < kNumFxp; i++) if (std::string(kFxpDefs[i].type) == "ir" && std::string(kFxpDefs[i].key) == "mix") hasFxp = true; CHECK(hasFxp, "FXP automation destinations exist");
    }
    if (failures) { printf("%d FAILED\n", failures); return 1; }
    printf("ir_tests: all passed\n"); return 0;
}
