// Rotary Speaker, Amp + Cabinet (Twin / JC / Small / Brit) and the shared biquad / oversampler / tube shaper. Mirror of the web worklet block
// FXROT_WORKLET_BEGIN..END (rotary-processor, amp-processor) in new-web.html / full-app.html; golden-tested against the real JavaScript (rotary_amp_tests).
// No allocation after construction. Doubles throughout.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
namespace vv { namespace rotamp {
inline double tube(double v, double b) { const double tb = std::tanh(b); return (std::tanh(v + b) - tb) / (1 - tb * tb); }
struct Bq {   // RBJ biquad. type 0 lp, 1 hp, 2 peaking, 3 low shelf, 4 high shelf
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set(int type, double f, double q, double gDb, double sr) {
        f = std::min(std::max(f, 10.0), sr * 0.45); const double w = 2 * 3.141592653589793 * f / sr, c = std::cos(w), s = std::sin(w), al = s / (2 * q); double B0, B1, B2, A0, A1, A2;
        if (type == 0) { B0 = (1 - c) / 2; B1 = 1 - c; B2 = (1 - c) / 2; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; }
        else if (type == 1) { B0 = (1 + c) / 2; B1 = -(1 + c); B2 = (1 + c) / 2; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; }
        else { const double A = std::pow(10.0, gDb / 40);
            if (type == 2) { B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A; }
            else { const double sq = 2 * std::sqrt(A) * al;
                if (type == 3) { B0 = A * ((A + 1) - (A - 1) * c + sq); B1 = 2 * A * ((A - 1) - (A + 1) * c); B2 = A * ((A + 1) - (A - 1) * c - sq); A0 = (A + 1) + (A - 1) * c + sq; A1 = -2 * ((A - 1) + (A + 1) * c); A2 = (A + 1) + (A - 1) * c - sq; }
                else { B0 = A * ((A + 1) + (A - 1) * c + sq); B1 = -2 * A * ((A - 1) + (A + 1) * c); B2 = A * ((A + 1) + (A - 1) * c - sq); A0 = (A + 1) - (A - 1) * c + sq; A1 = 2 * ((A - 1) - (A + 1) * c); A2 = (A + 1) - (A - 1) * c - sq; } } }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    inline double p(double x) { const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
    void reset() { x1 = x2 = y1 = y2 = 0; }
};
struct Os2 {   // 2x oversampling around a stateless shaper (see FxOs2 in the web block)
    Bq a, b; double last = 0;
    void prepare(double sr) { a.set(0, sr * 0.45, 0.5412, 0, sr * 2); b.set(0, sr * 0.45, 1.3066, 0, sr * 2); last = 0; }
    void reset() { a.reset(); b.reset(); last = 0; }
};
inline double jsRoundD(double x) { return std::floor(x + 0.5); }

class Rotary {
public:
    static constexpr int N = 2048;
    void prepare(double sr) {
        sr_ = sr; std::fill(dh_, dh_ + N, 0.0); std::fill(dd_, dd_ + N, 0.0); w_ = 0;
        xh1_.set(1, 800, 0.7071, 0, sr); xh2_.set(1, 800, 0.7071, 0, sr); xl1_.set(0, 800, 0.7071, 0, sr); xl2_.set(0, 800, 0.7071, 0, sr);
        xh1_.reset(); xh2_.reset(); xl1_.reset(); xl2_.reset(); os_.prepare(sr); dcx_ = dcy_ = 0; dcR_ = 1 - 2 * 3.141592653589793 * 10 / sr; hw_ = dw_ = ha_ = da_ = 0; oL = oR = 0;
    }
    void setParams(double speed, double drive, double balance, double depth, double width, double accel, double mix, double levelDb) {
        speed_ = speed; drive_ = drive; bal_ = balance; depth_ = depth; width_ = width; accel_ = accel; mix_ = mix; lvl_ = std::pow(10.0, levelDb / 20);
    }
    double oL = 0, oR = 0;
    void tick(double x) {
        const double sr = sr_; const int spd = (int)jsRoundD(speed_);
        const double th = spd == 1 ? 6.7 : spd == 0 ? 0.8 : 0, td = spd == 1 ? 5.7 : spd == 0 ? 0.66 : 0;
        const double ac = std::max(0.1, accel_);
        hw_ += (th - hw_) * (1 - std::exp(-1 / (sr * ((th > hw_ ? 0.9 : 1.6) / ac))));
        dw_ += (td - dw_) * (1 - std::exp(-1 / (sr * ((td > dw_ ? 3.2 : 4.5) / ac))));
        ha_ += hw_ / sr; ha_ -= std::floor(ha_); da_ += dw_ / sr; da_ -= std::floor(da_);
        double y;
        if (drive_ > 0) {
            const double m = (os_.last + x) * 0.5; os_.last = x; os_.b.p(os_.a.p(nl(m))); y = os_.b.p(os_.a.p(nl(x)));
            const double d = y - dcx_ + dcR_ * dcy_; dcx_ = y; dcy_ = d; y = d;
        } else y = x;
        const double hi = xh2_.p(xh1_.p(y)), lo = xl2_.p(xl1_.p(y));
        const int w = w_; dh_[w] = hi; dd_[w] = lo; const int mask = N - 1;
        const double m = width_ * 1.0471975512, dep = depth_, TP = 6.283185307179586; double l = 0, r2 = 0;
        for (int c = 0; c < 2; c++) {
            const double off = c == 0 ? -m : m, ph = TP * ha_ + off, pd = TP * da_ + off;
            const double dhS = (0.0007 + 0.00055 * dep * std::sin(ph)) * sr, ddS = (0.0012 + 0.0003 * dep * std::sin(pd)) * sr;
            const double ah = 1 - 0.6 * dep * (1 - std::cos(ph)) * 0.5, ad = 1 - 0.4 * dep * (1 - std::cos(pd)) * 0.5;
            double rr = w - dhS; double fl = std::floor(rr); int i0 = (int)fl; double f = rr - fl; const double h = dh_[i0 & mask] * (1 - f) + dh_[(i0 + 1) & mask] * f;
            rr = w - ddS; fl = std::floor(rr); i0 = (int)fl; f = rr - fl; const double d = dd_[i0 & mask] * (1 - f) + dd_[(i0 + 1) & mask] * f;
            const double v = h * ah * std::min(1.5, bal_ * 2) + d * ad * std::min(1.5, (1 - bal_) * 2);
            if (c == 0) l = v; else r2 = v;
        }
        w_ = (w + 1) & mask;
        oL = x * (1 - mix_) + l * 0.9 * lvl_ * mix_; oR = x * (1 - mix_) + r2 * 0.9 * lvl_ * mix_;
    }
private:
    double nl(double x) const { const double d = drive_; return tube(x * (1 + d * 9), 0.1 + 0.2 * d) * 0.7 / (1 + d * 2); }
    double sr_ = 48000, dh_[N], dd_[N], dcx_ = 0, dcy_ = 0, dcR_ = 0.999, hw_ = 0, dw_ = 0, ha_ = 0, da_ = 0, speed_ = 0, drive_ = 0, bal_ = 0.5, depth_ = 0.7, width_ = 0.8, accel_ = 1, mix_ = 1, lvl_ = 1; int w_ = 0;
    Bq xh1_, xh2_, xl1_, xl2_; Os2 os_;
};

class Amp {
public:
    static constexpr int N = 4096;
    void prepare(double sr) {
        sr_ = sr; os1_.prepare(sr); os2_.prepare(sr); os3_.prepare(sr); dcx_ = dcy_ = 0; dcR_ = 1 - 2 * 3.141592653589793 * 10 / sr; std::fill(cl_, cl_ + N, 0.0); cw_ = 0; ph_ = 0; oL = oR = 0;
        Bq* all[] = { &inHp_, &bright_, &tBass_, &tMid_, &tTre_, &tPre_, &coup_, &inter_ }; for (Bq* b : all) b->reset(); for (auto& c : cab_) c.reset();
        have_ = false; setParams(0, 0.4, 0.5, 0.5, 0.5, 0.4, 0.5, 1, 0.5, 1, 0);
    }
    void setParams(double model, double gain, double bass, double mid, double treble, double presence, double master, double cab, double chorus, double mix, double levelDb) {
        const int m = std::max(0, std::min(3, (int)jsRoundD(model))); const int cb = cab >= 0.5 ? 1 : 0;
        chorus_ = chorus; mix_ = mix; lvl_ = std::pow(10.0, levelDb / 20);
        if (have_ && m == model_ && gain == gain_ && bass == bass_ && mid == mid_ && treble == treble_ && presence == presence_ && master == master_ && cb == cab_on_) return;
        have_ = true; model_ = m; gain_ = gain; bass_ = bass; mid_ = mid; treble_ = treble; presence_ = presence; master_ = master; cab_on_ = cb; recalc();
    }
    double oL = 0, oR = 0;
    void tick(double x) {
        const int m = model_; double y = inHp_.p(x);
        y = bright_.p(y); y = shape(os1_, 0, y);
        if (m == 3) { y = inter_.p(coup_.p(y)); y = shape(os2_, 1, y); }
        y = tBass_.p(y); y = tMid_.p(y); y = tTre_.p(y); y = tPre_.p(y);
        y = shape(os3_, 2, y) * trim_;
        const double d = y - dcx_ + dcR_ * dcy_; dcx_ = y; dcy_ = d; y = d;
        if (cab_on_) for (int i = 0; i < 6; i++) y = cab_[i].p(y);
        double l = y, r = y;
        if (m == 1 && chorus_ > 0.001) {
            const double sr = sr_; const int mask = N - 1; cl_[cw_] = y; ph_ += 0.8 / sr; ph_ -= std::floor(ph_);
            const double s = std::sin(6.283185307179586 * ph_), dep = (0.0005 + 0.003 * chorus_) * sr, base = 0.007 * sr;
            double rr = cw_ - (base + dep * s), fl = std::floor(rr); int i0 = (int)fl; double f = rr - fl; const double dl = cl_[i0 & mask] * (1 - f) + cl_[(i0 + 1) & mask] * f;
            rr = cw_ - (base - dep * s); fl = std::floor(rr); i0 = (int)fl; f = rr - fl; const double dr = cl_[i0 & mask] * (1 - f) + cl_[(i0 + 1) & mask] * f;
            cw_ = (cw_ + 1) & mask; const double c = chorus_ * 0.5; l = y * (1 - c) + dl * c; r = y * (1 - c) + dr * c;
        }
        oL = x * (1 - mix_) + l * lvl_ * mix_; oR = x * (1 - mix_) + r * lvl_ * mix_;
    }
private:
    double nl(int k, double x) const {
        const int m = model_;
        if (k == 0) { const double v = x * g1_; return m == 0 ? std::tanh(v * 0.8) : m == 1 ? v / std::sqrt(1 + v * v * 0.6) : m == 2 ? tube(v, 0.22) : tube(v, 0.18); }
        if (k == 1) return tube(x * g2_, 0.1) * 0.5;
        const double v = x * pd_; return (m == 1 ? v / std::sqrt(1 + v * v) : tube(v, m == 0 ? 0.03 : 0.08)) / std::sqrt(pd_);
    }
    double shape(Os2& o, int k, double x) { const double m = (o.last + x) * 0.5; o.last = x; o.b.p(o.a.p(nl(k, m))); return o.b.p(o.a.p(nl(k, x))); }
    void recalc() {
        const double sr = sr_, m = model_, b = (bass_ - 0.5) * 2, md = (mid_ - 0.5) * 2, t = (treble_ - 0.5) * 2, pr = (presence_ - 0.5) * 2, g = gain_, Q = 0.7071; Bq* cf = cab_;
        static const double hp[4] = { 40, 30, 80, 60 }; inHp_.set(1, hp[model_], Q, 0, sr);
        if (m == 0) { bright_.set(4, 4000, Q, 6 * (1 - g), sr); tBass_.set(3, 110, Q, 12 * b, sr); tMid_.set(2, 500, 0.7, -7 + 8 * md, sr); tTre_.set(4, 3200, Q, 12 * t, sr); tPre_.set(4, 5500, Q, 5 * pr, sr); coup_.set(1, 20, Q, 0, sr); inter_.set(0, 16000, Q, 0, sr);
            g1_ = 1 + g * 5; g2_ = 1; pd_ = 1 + master_ * 2.5; trim_ = 0.9;
            cf[0].set(1, 70, 0.8, 0, sr); cf[1].set(2, 110, 1, 3, sr); cf[2].set(2, 600, 0.9, -2, sr); cf[3].set(2, 2500, 1, 3, sr); cf[4].set(0, 5500, Q, 0, sr); cf[5].set(0, 8000, Q, 0, sr); }
        else if (m == 1) { bright_.set(4, 6000, Q, 3, sr); tBass_.set(3, 100, Q, 10 * b, sr); tMid_.set(2, 700, 0.8, 8 * md, sr); tTre_.set(4, 4000, Q, 12 * t, sr); tPre_.set(4, 8000, Q, 4 * pr, sr); coup_.set(1, 20, Q, 0, sr); inter_.set(0, 16000, Q, 0, sr);
            g1_ = 1 + g * 3; g2_ = 1; pd_ = 1 + master_ * 2; trim_ = 1.0;
            cf[0].set(1, 70, 0.8, 0, sr); cf[1].set(2, 200, 1, 1.5, sr); cf[2].set(2, 700, 1, -2, sr); cf[3].set(2, 3200, 1, 2, sr); cf[4].set(0, 7500, Q, 0, sr); cf[5].set(0, 10000, Q, 0, sr); }
        else if (m == 2) { bright_.set(4, 3000, Q, 0, sr); tBass_.set(3, 150, Q, 6 * b, sr); tMid_.set(2, 900, 0.8, 4 + 4 * md, sr); tTre_.set(4, 3000, Q, 8 * t, sr); tPre_.set(4, 4000, Q, 3 * pr, sr); coup_.set(1, 80, Q, 0, sr); inter_.set(0, 8000, Q, 0, sr);
            g1_ = 1 + g * 14; g2_ = 1; pd_ = 1 + master_ * 4; trim_ = 0.6;
            cf[0].set(1, 150, 0.9, 0, sr); cf[1].set(2, 200, 1.2, 4, sr); cf[2].set(2, 1700, 1, 4, sr); cf[3].set(0, 4200, Q, 0, sr); cf[4].set(0, 4200, Q, 0, sr); cf[5].set(2, 900, 1, -2, sr); }
        else { bright_.set(4, 3500, Q, 0, sr); tBass_.set(3, 90, Q, 10 * b, sr); tMid_.set(2, 650, 0.8, 2 + 8 * md, sr); tTre_.set(4, 3200, Q, 10 * t, sr); tPre_.set(4, 4500, Q, 8 * pr, sr); coup_.set(1, 100, Q, 0, sr); inter_.set(0, 6500, Q, 0, sr);
            g1_ = 1 + g * 10; g2_ = 3 + g * 20; pd_ = 1 + master_ * 5; trim_ = 0.8;
            cf[0].set(1, 75, 0.8, 0, sr); cf[1].set(2, 95, 1.2, 4, sr); cf[2].set(2, 500, 1, -2.5, sr); cf[3].set(2, 2400, 1.1, 5, sr); cf[4].set(0, 5200, Q, 0, sr); cf[5].set(0, 5200, Q, 0, sr); }
    }
    double sr_ = 48000, gain_ = 0.4, bass_ = 0.5, mid_ = 0.5, treble_ = 0.5, presence_ = 0.4, master_ = 0.5, chorus_ = 0.5, mix_ = 1, lvl_ = 1, g1_ = 1, g2_ = 1, pd_ = 1, trim_ = 1, dcx_ = 0, dcy_ = 0, dcR_ = 0.999, ph_ = 0, cl_[N]; int model_ = 0, cab_on_ = 1, cw_ = 0; bool have_ = false;
    Bq inHp_, bright_, tBass_, tMid_, tTre_, tPre_, coup_, inter_, cab_[6]; Os2 os1_, os2_, os3_;
};
}}   // namespace vv::rotamp
