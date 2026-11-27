// Wavetable authoring tools (web: EDIT HARMONICS, FORMULA..., audio import, DRAW WAVEFORM). Pure, non-realtime functions that build the frames
// stored in a preset's customWavetables entry. 1024 samples per frame like the web app's WAVETABLE_SIZE.
//
// FORMULA: the web app evaluates the user's text with `new Function('x','n','return (' + expr + ');')`, i.e. arbitrary JavaScript. The native
// formula evaluator is a safe expression parser for the part of JavaScript that wavetable formulas use: numbers, x, n, parentheses, + - * / % **,
// unary + - !, comparisons, && || ?:, and the whole Math object (constants and functions). Statements, arrays, strings, user functions and other
// globals are rejected with "Formula error: ...". Math.random() is a fixed-seed generator so a formula renders the same every time.
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

namespace vv { namespace wttools {

constexpr int kFrame = 1024;
inline void normalize(std::vector<float>& f) { float mx = 0; for (float v : f) mx = std::max(mx, std::fabs(v)); if (mx > 0) for (float& v : f) v /= mx; }

// genFrame: sum of sine harmonics amps[h] * sin(2*pi*(h+1)*phase), normalised to peak 1
inline std::vector<float> harmonicFrame(const std::vector<double>& amps) {
    std::vector<float> f(kFrame);
    for (int n = 0; n < kFrame; n++) { const double ph = (double)n / kFrame; double s = 0; for (size_t h = 0; h < amps.size(); h++) { if (!amps[h]) continue; s += amps[h] * std::sin(2 * 3.141592653589793 * (double)(h + 1) * ph); } f[n] = (float)s; }
    normalize(f); return f;
}

// sliceBufferIntoWavetableFrames: first channel cut into numFrames equal chunks, each resampled (nearest) to one frame and normalised
inline std::vector<std::vector<float>> sliceAudio(const float* data, int len, int numFrames) {
    std::vector<std::vector<float>> frames; if (len <= 0 || !data) return frames;
    numFrames = std::max(1, std::min(numFrames, std::max(1, len / 32)));
    const int chunk = len / numFrames;
    for (int fi = 0; fi < numFrames; fi++) {
        std::vector<float> f(kFrame); const int start = fi * chunk;
        for (int n = 0; n < kFrame; n++) { const int src = start + (int)std::floor((double)n / kFrame * chunk); f[n] = data[std::min(src, len - 1)]; }
        normalize(f); frames.push_back(std::move(f));
    }
    return frames;
}

// ------------------------------------------------------------------ formula
class Formula {
public:
    // Compiles; on failure returns false and fills err (without the "Formula error: " prefix).
    bool compile(const std::string& expr, std::string& err) {
        src_ = expr; pos_ = 0; usesN_ = false; err_.clear(); ok_ = true; root_ = -1; nodes_.clear();
        root_ = parseTernary(); skipWs();
        if (ok_ && pos_ < src_.size()) fail(std::string("unexpected '") + src_[pos_] + "'");
        if (!ok_) { err = err_; return false; }
        // like the web's test call fn(0.25, 0.5): the result must be a number
        double rr = 0; bool isBool = false; rngState_ = 12345; eval(root_, 0.25, 0.5, rr, isBool);
        if (isBool) { err = "Formula must return a number."; return false; }
        return true;
    }
    bool usesN() const { return usesN_; }
    double eval(double x, double n) const { double r; bool b; rngState_ = 12345; eval(root_, x, n, r, b); return r; }
private:
    enum Op { NUM, VX, VN, UNARY_MINUS, UNARY_PLUS, NOT, ADD, SUB, MUL, DIV, MOD, POW, LT, GT, LE, GE, EQ, NE, AND, OR, COND, CALL };
    struct Node { Op op; double num = 0; int a = -1, b = -1, c = -1; int fn = -1; std::vector<int> args; };
    std::string src_, err_; size_t pos_ = 0; bool usesN_ = false, ok_ = true; int root_ = -1; std::vector<Node> nodes_;
    mutable uint32_t rngState_ = 12345;
    void fail(const std::string& m) { if (ok_) { ok_ = false; err_ = m; } }
    void skipWs() { while (pos_ < src_.size() && std::isspace((unsigned char)src_[pos_])) pos_++; }
    bool eat(const char* s) { skipWs(); const size_t n = std::char_traits<char>::length(s); if (src_.compare(pos_, n, s) == 0) { pos_ += n; return true; } return false; }
    bool peekIs(const char* s) { skipWs(); const size_t n = std::char_traits<char>::length(s); return src_.compare(pos_, n, s) == 0; }
    int mk(Op op, int a = -1, int b = -1, int c = -1) { Node nd; nd.op = op; nd.a = a; nd.b = b; nd.c = c; nodes_.push_back(nd); return (int)nodes_.size() - 1; }
    struct Fn { const char* name; int argc; };   // argc -1 = variadic
    static const Fn* fns(int& count) {
        static const Fn F[] = { {"abs",1},{"acos",1},{"acosh",1},{"asin",1},{"asinh",1},{"atan",1},{"atan2",2},{"atanh",1},{"cbrt",1},{"ceil",1},{"cos",1},{"cosh",1},{"exp",1},{"expm1",1},{"floor",1},{"fround",1},{"hypot",-1},
            {"log",1},{"log10",1},{"log1p",1},{"log2",1},{"max",-1},{"min",-1},{"pow",2},{"random",0},{"round",1},{"sign",1},{"sin",1},{"sinh",1},{"sqrt",1},{"tan",1},{"tanh",1},{"trunc",1} };
        count = (int)(sizeof(F) / sizeof(F[0])); return F;
    }
    int parseTernary() {
        int c = parseOr(); if (!ok_) return -1;
        if (eat("?")) { int a = parseTernary(); if (!eat(":")) { fail("expected ':'"); return -1; } int b = parseTernary(); return mk(COND, c, a, b); }
        return c;
    }
    int parseOr() { int l = parseAnd(); while (ok_ && peekIs("||")) { eat("||"); int r = parseAnd(); l = mk(OR, l, r); } return l; }
    int parseAnd() { int l = parseEq(); while (ok_ && peekIs("&&")) { eat("&&"); int r = parseEq(); l = mk(AND, l, r); } return l; }
    int parseEq() {
        int l = parseRel();
        while (ok_) { if (eat("===") || eat("==")) l = mk(EQ, l, parseRel()); else if (eat("!==") || eat("!=")) l = mk(NE, l, parseRel()); else break; }
        return l;
    }
    int parseRel() {
        int l = parseAdd();
        while (ok_) { if (eat("<=")) l = mk(LE, l, parseAdd()); else if (eat(">=")) l = mk(GE, l, parseAdd()); else if (peekIs("<") ) { eat("<"); l = mk(LT, l, parseAdd()); } else if (peekIs(">")) { eat(">"); l = mk(GT, l, parseAdd()); } else break; }
        return l;
    }
    int parseAdd() { int l = parseMul(); while (ok_) { skipWs(); if (pos_ < src_.size() && src_[pos_] == '+') { pos_++; l = mk(ADD, l, parseMul()); } else if (pos_ < src_.size() && src_[pos_] == '-') { pos_++; l = mk(SUB, l, parseMul()); } else break; } return l; }
    int parseMul() {
        int l = parseUnary();
        while (ok_) { skipWs(); if (pos_ >= src_.size()) break; const char c = src_[pos_];
            if (c == '*' && !(pos_ + 1 < src_.size() && src_[pos_ + 1] == '*')) { pos_++; l = mk(MUL, l, parseUnary()); } else if (c == '/') { pos_++; l = mk(DIV, l, parseUnary()); } else if (c == '%') { pos_++; l = mk(MOD, l, parseUnary()); } else break; }
        return l;
    }
    int parseUnary() {
        skipWs();
        if (pos_ < src_.size()) { const char c = src_[pos_];
            if (c == '-') { pos_++; return mk(UNARY_MINUS, parseUnary()); } if (c == '+') { pos_++; return mk(UNARY_PLUS, parseUnary()); } if (c == '!') { pos_++; return mk(NOT, parseUnary()); } }
        return parsePow();
    }
    int parsePow() { int base = parsePrimary(); if (ok_ && peekIs("**")) { eat("**"); int ex = parseUnary(); return mk(POW, base, ex); } return base; }   // right associative through parseUnary
    int parsePrimary() {
        skipWs(); if (pos_ >= src_.size()) { fail("unexpected end of formula"); return -1; }
        const char c = src_[pos_];
        if (c == '(') { pos_++; int e = parseTernary(); if (!eat(")")) { fail("missing ')'"); return -1; } return e; }
        if (std::isdigit((unsigned char)c) || (c == '.' && pos_ + 1 < src_.size() && std::isdigit((unsigned char)src_[pos_ + 1]))) {
            const size_t st = pos_; while (pos_ < src_.size() && (std::isdigit((unsigned char)src_[pos_]) || src_[pos_] == '.')) pos_++;
            if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) { size_t q = pos_ + 1; if (q < src_.size() && (src_[q] == '+' || src_[q] == '-')) q++; if (q < src_.size() && std::isdigit((unsigned char)src_[q])) { pos_ = q; while (pos_ < src_.size() && std::isdigit((unsigned char)src_[pos_])) pos_++; } }
            int nd = mk(NUM); nodes_[nd].num = std::strtod(src_.substr(st, pos_ - st).c_str(), nullptr); return nd;
        }
        if (std::isalpha((unsigned char)c) || c == '_' || c == '$') {
            const std::string id = ident();
            if (id == "x") return mk(VX);
            if (id == "n") { usesN_ = true; return mk(VN); }
            if (id == "Math") {
                if (!eat(".")) { fail("expected '.' after Math"); return -1; }
                skipWs(); if (pos_ >= src_.size() || !(std::isalpha((unsigned char)src_[pos_]) || src_[pos_] == '_')) { fail("expected a Math member"); return -1; }
                const std::string m = ident();
                struct K { const char* n; double v; }; static const K ks[] = { {"PI", 3.141592653589793}, {"E", 2.718281828459045}, {"LN2", 0.6931471805599453}, {"LN10", 2.302585092994046}, {"LOG2E", 1.4426950408889634}, {"LOG10E", 0.4342944819032518}, {"SQRT2", 1.4142135623730951}, {"SQRT1_2", 0.7071067811865476} };
                for (const K& k : ks) if (m == k.n) { int nd = mk(NUM); nodes_[nd].num = k.v; return nd; }
                int cnt; const Fn* F = fns(cnt); int fi = -1; for (int i = 0; i < cnt; i++) if (m == F[i].name) fi = i;
                if (fi < 0) { fail("Math." + m + " is not supported"); return -1; }
                if (!eat("(")) { fail("Math." + m + " must be called"); return -1; }
                int nd = mk(CALL); nodes_[nd].fn = fi;
                if (!eat(")")) { for (;;) { int a = parseTernary(); if (!ok_) return -1; nodes_[nd].args.push_back(a); if (eat(",")) continue; if (eat(")")) break; fail("missing ')'"); return -1; } }
                if (F[fi].argc >= 0 && (int)nodes_[nd].args.size() != F[fi].argc) { fail("Math." + m + " takes " + std::to_string(F[fi].argc) + " argument(s)"); return -1; }
                return nd;
            }
            fail(id + " is not defined"); return -1;
        }
        fail(std::string("unexpected '") + c + "'"); return -1;
    }
    std::string ident() { const size_t st = pos_; while (pos_ < src_.size() && (std::isalnum((unsigned char)src_[pos_]) || src_[pos_] == '_' || src_[pos_] == '$')) pos_++; return src_.substr(st, pos_ - st); }
    static bool truthy(double v) { return v != 0 && !std::isnan(v); }
    void eval(int id, double x, double n, double& out, bool& isBool) const {
        const Node& nd = nodes_[id]; isBool = false; double a = 0, b = 0; bool ba = false, bb = false;
        auto ev = [&](int i, double& r, bool& bl) { eval(i, x, n, r, bl); };
        switch (nd.op) {
            case NUM: out = nd.num; return; case VX: out = x; return; case VN: out = n; return;
            case UNARY_MINUS: ev(nd.a, a, ba); out = -a; return; case UNARY_PLUS: ev(nd.a, a, ba); out = a; return;
            case NOT: ev(nd.a, a, ba); out = truthy(a) ? 0 : 1; isBool = true; return;
            case ADD: ev(nd.a, a, ba); ev(nd.b, b, bb); out = a + b; return; case SUB: ev(nd.a, a, ba); ev(nd.b, b, bb); out = a - b; return;
            case MUL: ev(nd.a, a, ba); ev(nd.b, b, bb); out = a * b; return; case DIV: ev(nd.a, a, ba); ev(nd.b, b, bb); out = a / b; return;
            case MOD: ev(nd.a, a, ba); ev(nd.b, b, bb); out = std::fmod(a, b); return; case POW: ev(nd.a, a, ba); ev(nd.b, b, bb); out = std::pow(a, b); return;
            case LT: ev(nd.a, a, ba); ev(nd.b, b, bb); out = a < b; isBool = true; return; case GT: ev(nd.a, a, ba); ev(nd.b, b, bb); out = a > b; isBool = true; return;
            case LE: ev(nd.a, a, ba); ev(nd.b, b, bb); out = a <= b; isBool = true; return; case GE: ev(nd.a, a, ba); ev(nd.b, b, bb); out = a >= b; isBool = true; return;
            case EQ: ev(nd.a, a, ba); ev(nd.b, b, bb); out = a == b; isBool = true; return; case NE: ev(nd.a, a, ba); ev(nd.b, b, bb); out = a != b; isBool = true; return;
            case AND: ev(nd.a, a, ba); if (!truthy(a)) { out = a; isBool = ba; return; } ev(nd.b, b, bb); out = b; isBool = bb; return;
            case OR: ev(nd.a, a, ba); if (truthy(a)) { out = a; isBool = ba; return; } ev(nd.b, b, bb); out = b; isBool = bb; return;
            case COND: ev(nd.a, a, ba); if (truthy(a)) ev(nd.b, out, isBool); else ev(nd.c, out, isBool); return;
            case CALL: {
                int cnt; const Fn* F = fns(cnt); const std::string f = F[nd.fn].name; double v[8] = {0}; const size_t na = std::min<size_t>(nd.args.size(), 8);
                for (size_t i = 0; i < nd.args.size(); i++) { double r; bool bl; ev(nd.args[i], r, bl); if (i < 8) v[i] = r; }
                const double p = v[0], q = v[1]; double r = 0;
                if (f == "abs") r = std::fabs(p); else if (f == "acos") r = std::acos(p); else if (f == "acosh") r = std::acosh(p); else if (f == "asin") r = std::asin(p); else if (f == "asinh") r = std::asinh(p);
                else if (f == "atan") r = std::atan(p); else if (f == "atan2") r = std::atan2(p, q); else if (f == "atanh") r = std::atanh(p); else if (f == "cbrt") r = std::cbrt(p); else if (f == "ceil") r = std::ceil(p);
                else if (f == "cos") r = std::cos(p); else if (f == "cosh") r = std::cosh(p); else if (f == "exp") r = std::exp(p); else if (f == "expm1") r = std::expm1(p); else if (f == "floor") r = std::floor(p);
                else if (f == "fround") r = (double)(float)p; else if (f == "hypot") { double s = 0; for (size_t i = 0; i < na; i++) s += v[i] * v[i]; r = std::sqrt(s); }
                else if (f == "log") r = std::log(p); else if (f == "log10") r = std::log10(p); else if (f == "log1p") r = std::log1p(p); else if (f == "log2") r = std::log2(p);
                else if (f == "max") { r = -INFINITY; for (size_t i = 0; i < na; i++) { if (std::isnan(v[i])) { r = NAN; break; } r = std::max(r, v[i]); } }
                else if (f == "min") { r = INFINITY; for (size_t i = 0; i < na; i++) { if (std::isnan(v[i])) { r = NAN; break; } r = std::min(r, v[i]); } }
                else if (f == "pow") r = std::pow(p, q); else if (f == "random") { rngState_ = rngState_ * 1664525u + 1013904223u; r = (rngState_ >> 8) / 16777216.0; }
                else if (f == "round") r = std::floor(p + 0.5); else if (f == "sign") r = p > 0 ? 1 : p < 0 ? -1 : p;
                else if (f == "sin") r = std::sin(p); else if (f == "sinh") r = std::sinh(p); else if (f == "sqrt") r = std::sqrt(p); else if (f == "tan") r = std::tan(p); else if (f == "tanh") r = std::tanh(p); else if (f == "trunc") r = std::trunc(p);
                out = r; return; }
        }
        out = 0;
    }
};

// formula frames: x = sample/1024; n = frame position 0..1 when the formula uses it (frameCount frames), else one frame. Non-finite results become 0.
inline bool formulaFrames(const std::string& expr, int frameCount, std::vector<std::vector<float>>& frames, std::string& err) {
    Formula f; frames.clear();
    std::string e = expr; while (!e.empty() && std::isspace((unsigned char)e.front())) e.erase(e.begin()); while (!e.empty() && std::isspace((unsigned char)e.back())) e.pop_back();
    if (e.empty()) { err = "Enter a formula."; return false; }
    if (!f.compile(e, err)) { err = "Formula error: " + err; return false; }
    const int numFrames = f.usesN() ? std::max(1, frameCount) : 1;
    for (int fi = 0; fi < numFrames; fi++) {
        const double n = numFrames > 1 ? (double)fi / (numFrames - 1) : 0; std::vector<float> fr(kFrame);
        for (int si = 0; si < kFrame; si++) { double v = f.eval((double)si / kFrame, n); if (!std::isfinite(v)) v = 0; fr[si] = (float)v; }
        normalize(fr); frames.push_back(std::move(fr));
    }
    return true;
}
}}
