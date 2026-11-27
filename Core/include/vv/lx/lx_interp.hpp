// lx_interp.hpp - lexer, parser and tree-walking interpreter for the LX VM, plus the garbage collector.
#pragma once
#include "lx_vm.hpp"

namespace lx {

// =================================================================== lexer
enum TK { TK_EOF = 256, TK_NAME, TK_NUM, TK_STR, TK_AND, TK_BREAK, TK_DO, TK_ELSE, TK_ELSEIF, TK_END, TK_FALSE, TK_FOR, TK_FUNCTION, TK_GOTO, TK_IF, TK_IN, TK_LOCAL, TK_NIL, TK_NOT, TK_OR, TK_REPEAT, TK_RETURN, TK_THEN, TK_TRUE, TK_UNTIL, TK_WHILE, TK_CONCAT, TK_DOTS, TK_EQ, TK_GE, TK_LE, TK_NE, TK_SHL, TK_SHR, TK_IDIV, TK_DBCOLON };
struct Tok { int t = TK_EOF; double num = 0; const char* s = nullptr; size_t len = 0; int line = 1, col = 1; };
static const char* const kKeywords[] = { "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while" };

struct FuncState {
    FuncState* parent = nullptr; Proto* p = nullptr; Str* names[200]; VarInfo* vars[200]; int nlocals = 0, activeSlots = 0, maxSlots = 0;
    UpDesc ups[60]; Str* upNames[60]; int nups = 0; bool vararg = false; int blockDepth = 0;
};

struct Parser {
    VM& vm; const char* src; size_t len, pos = 0; int line = 1; const char* lineStart; Tok cur, ahead; bool hasAhead = false;
    FuncState* fs = nullptr; void* scratch[6000]; int sc = 0; char sbuf[4096]; int nest = 0;
    Parser(VM& v, const char* s, size_t n) : vm(v), src(s), len(n), lineStart(s) {}

    bool bad() const { return vm.fail != F_NONE; }
    void err(const char* msg, const Tok* at = nullptr) {
        const Tok& t = at ? *at : cur; char buf[240]; int n = 0; const char* m = msg; while (*m && n < 150) buf[n++] = *m++;
        const char* near = " near "; for (int i = 0; near[i]; i++) buf[n++] = near[i];
        if (t.t == TK_EOF) { const char* e = "<eof>"; for (int i = 0; e[i]; i++) buf[n++] = e[i]; }
        else { buf[n++] = '\''; if (t.s) { for (size_t i = 0; i < t.len && i < 30; i++) buf[n++] = t.s[i]; } else if (t.t < 256) buf[n++] = (char)t.t; buf[n++] = '\''; }
        buf[n] = 0; vm.setError(t.line, t.col, buf);
    }
    // ------------------------------------------------------------ scanning
    int peekc(size_t o = 0) const { return pos + o < len ? (u8)src[pos + o] : -1; }
    static bool isalpha_(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
    static bool isdigit_(int c) { return c >= '0' && c <= '9'; }
    void newline() { line++; lineStart = src + pos; }
    bool longBracket(int& level) {          // at '[': returns true if "[=*[" ; level = count of '='
        size_t p = pos + 1; int l = 0; while (p < len && src[p] == '=') { l++; p++; } if (p < len && src[p] == '[') { level = l; return true; } return false;
    }
    bool readLong(int level, char* out, int* olen, bool keep) {   // pos at opening '['; consumes through the closing bracket
        pos += level + 2; if (peekc() == '\r') { pos++; if (peekc() == '\n') pos++; newline(); } else if (peekc() == '\n') { pos++; if (peekc() == '\r') pos++; newline(); }
        int n = 0;
        for (;;) {
            if (pos >= len) { Tok t; t.line = line; t.col = 1; t.t = TK_EOF; err("unfinished long string/comment", &t); return false; }
            int c = peekc();
            if (c == ']') { size_t p = pos + 1; int l = 0; while (p < len && src[p] == '=') { l++; p++; } if (l == level && p < len && src[p] == ']') { pos = p + 1; if (keep) *olen = n; return true; } }
            if (c == '\n') { if (keep && n < 4095) out[n++] = '\n'; pos++; newline(); continue; } if (c == '\r') { pos++; continue; }
            if (keep && n < 4095) out[n++] = (char)c; pos++;
        }
    }
    int utf8(char* out, int n, unsigned long cp) {
        if (cp < 0x80) out[n++] = (char)cp; else if (cp < 0x800) { out[n++] = (char)(0xC0 | (cp >> 6)); out[n++] = (char)(0x80 | (cp & 63)); }
        else if (cp < 0x10000) { out[n++] = (char)(0xE0 | (cp >> 12)); out[n++] = (char)(0x80 | ((cp >> 6) & 63)); out[n++] = (char)(0x80 | (cp & 63)); }
        else { out[n++] = (char)(0xF0 | (cp >> 18)); out[n++] = (char)(0x80 | ((cp >> 12) & 63)); out[n++] = (char)(0x80 | ((cp >> 6) & 63)); out[n++] = (char)(0x80 | (cp & 63)); } return n;
    }
    Tok scan() {
        Tok t;
        for (;;) {
            int c = peekc();
            if (c == '\n') { pos++; newline(); continue; } if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') { pos++; continue; }
            if (c == '-' && peekc(1) == '-') {
                pos += 2; int lv; if (peekc() == '[' && longBracket(lv)) { readLong(lv, nullptr, nullptr, false); if (bad()) return t; continue; }
                while (pos < len && src[pos] != '\n') pos++; continue;
            }
            break;
        }
        t.line = line; t.col = (int)(src + pos - lineStart) + 1; int c = peekc(); t.s = src + pos;
        if (c < 0) { t.t = TK_EOF; t.s = nullptr; return t; }
        if (isalpha_(c)) {
            size_t st = pos; while (isalpha_(peekc()) || isdigit_(peekc())) pos++; t.s = src + st; t.len = pos - st; t.t = TK_NAME;
            for (int i = 0; i < 22; i++) if (slen(kKeywords[i]) == t.len && mcmp(kKeywords[i], t.s, t.len) == 0) { t.t = TK_AND + i; break; } return t;
        }
        if (isdigit_(c) || (c == '.' && isdigit_(peekc(1)))) {
            size_t st = pos;
            if (c == '0' && (peekc(1) == 'x' || peekc(1) == 'X')) { pos += 2; while (isdigit_(peekc()) || (peekc() >= 'a' && peekc() <= 'f') || (peekc() >= 'A' && peekc() <= 'F')) pos++; }
            else { while (isdigit_(peekc()) || peekc() == '.') pos++; if (peekc() == 'e' || peekc() == 'E') { pos++; if (peekc() == '+' || peekc() == '-') pos++; while (isdigit_(peekc())) pos++; } }
            t.s = src + st; t.len = pos - st; if (isalpha_(peekc())) { pos++; t.len = pos - st; err("malformed number", &t); return t; }
            double v; if (!parse_num(t.s, t.len, &v)) { err("malformed number", &t); return t; } t.t = TK_NUM; t.num = v; return t;
        }
        if (c == '"' || c == '\'') {
            int q = c; pos++; int n = 0;
            for (;;) {
                int d = peekc(); if (d < 0 || d == '\n') { err("unfinished string", &t); return t; }
                if (d == q) { pos++; break; }
                if (n > 4090) { err("string literal too long", &t); return t; }
                if (d == '\\') {
                    pos++; int e = peekc(); pos++;
                    switch (e) {
                        case 'n': sbuf[n++] = '\n'; break; case 't': sbuf[n++] = '\t'; break; case 'r': sbuf[n++] = '\r'; break; case 'a': sbuf[n++] = '\a'; break; case 'b': sbuf[n++] = '\b'; break; case 'f': sbuf[n++] = '\f'; break; case 'v': sbuf[n++] = '\v'; break;
                        case '\\': sbuf[n++] = '\\'; break; case '"': sbuf[n++] = '"'; break; case '\'': sbuf[n++] = '\''; break; case '\n': sbuf[n++] = '\n'; newline(); break;
                        case 'x': { int v = 0; for (int i = 0; i < 2; i++) { int h = peekc(); int dv = isdigit_(h) ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1; if (dv < 0) { err("hexadecimal digit expected", &t); return t; } v = v * 16 + dv; pos++; } sbuf[n++] = (char)v; break; }
                        case 'z': while (peekc() == ' ' || peekc() == '\n' || peekc() == '\t' || peekc() == '\r') { if (peekc() == '\n') { pos++; newline(); } else pos++; } break;
                        case 'u': { if (peekc() != '{') { err("missing '{' in \\u{xxxx}", &t); return t; } pos++; unsigned long cp = 0; while (peekc() >= 0 && peekc() != '}') { int h = peekc(); int dv = isdigit_(h) ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1; if (dv < 0) { err("hexadecimal digit expected", &t); return t; } cp = cp * 16 + (unsigned long)dv; pos++; } pos++; n = utf8(sbuf, n, cp); break; }
                        default:
                            if (isdigit_(e)) { int v = e - '0'; for (int i = 0; i < 2 && isdigit_(peekc()); i++) { v = v * 10 + (peekc() - '0'); pos++; } if (v > 255) { err("decimal escape too large", &t); return t; } sbuf[n++] = (char)v; }
                            else { err("invalid escape sequence", &t); return t; }
                    }
                    continue;
                }
                sbuf[n++] = (char)d; pos++;
            }
            Str* s = vm.internPerm(sbuf, (size_t)n); t.t = TK_STR; t.s = s ? s->data : ""; t.len = (size_t)n; strTok = s; return t;
        }
        if (c == '[') { int lv; if (longBracket(lv)) { int n = 0; if (!readLong(lv, sbuf, &n, true)) return t; Str* s = vm.internPerm(sbuf, (size_t)n); t.t = TK_STR; t.s = s ? s->data : ""; t.len = (size_t)n; strTok = s; return t; } }
        pos++;
        switch (c) {
            case '=': if (peekc() == '=') { pos++; t.t = TK_EQ; t.len = 2; } else { t.t = '='; t.len = 1; } return t;
            case '<': if (peekc() == '=') { pos++; t.t = TK_LE; t.len = 2; } else if (peekc() == '<') { pos++; t.t = TK_SHL; t.len = 2; } else { t.t = '<'; t.len = 1; } return t;
            case '>': if (peekc() == '=') { pos++; t.t = TK_GE; t.len = 2; } else if (peekc() == '>') { pos++; t.t = TK_SHR; t.len = 2; } else { t.t = '>'; t.len = 1; } return t;
            case '~': if (peekc() == '=') { pos++; t.t = TK_NE; t.len = 2; } else { t.t = '~'; t.len = 1; } return t;
            case ':': if (peekc() == ':') { pos++; t.t = TK_DBCOLON; t.len = 2; } else { t.t = ':'; t.len = 1; } return t;
            case '/': if (peekc() == '/') { pos++; t.t = TK_IDIV; t.len = 2; } else { t.t = '/'; t.len = 1; } return t;
            case '.': if (peekc() == '.') { pos++; if (peekc() == '.') { pos++; t.t = TK_DOTS; t.len = 3; } else { t.t = TK_CONCAT; t.len = 2; } } else { t.t = '.'; t.len = 1; } return t;
            case '+': case '-': case '*': case '%': case '^': case '#': case '&': case '|': case '(': case ')': case '{': case '}': case ']': case ',': case ';': t.t = c; t.len = 1; return t;
            case '[': t.t = '['; t.len = 1; return t;
        }
        t.t = c; t.len = 1; err("unexpected symbol", &t); return t;
    }
    Str* strTok = nullptr; Str* strOfCur = nullptr; Str* strOfAhead = nullptr;
    void advance() { if (hasAhead) { cur = ahead; strOfCur = strOfAhead; hasAhead = false; } else { cur = scan(); strOfCur = cur.t == TK_STR ? strTok : nullptr; } if (bad()) { cur.t = TK_EOF; cur.s = nullptr; } }
    const Tok& peek() { if (!hasAhead) { ahead = scan(); strOfAhead = ahead.t == TK_STR ? strTok : nullptr; hasAhead = true; if (bad()) ahead.t = TK_EOF; } return ahead; }
    bool check(int t) const { return cur.t == t; }
    bool accept(int t) { if (cur.t == t) { advance(); return true; } return false; }
    static const char* tokName(int t) {
        switch (t) { case TK_END: return "'end'"; case TK_IF: return "'if'"; case TK_WHILE: return "'while'"; case TK_FOR: return "'for'"; case TK_REPEAT: return "'repeat'"; case TK_LOCAL: return "'local'"; case TK_THEN: return "'then'"; case TK_DO: return "'do'"; case TK_NAME: return "<name>"; case TK_IN: return "'in'"; case TK_UNTIL: return "'until'"; case TK_FUNCTION: return "'function'"; case '=': return "'='"; case '(': return "'('"; case ')': return "')'"; case '{': return "'{'"; case '}': return "'}'"; case ']': return "']'"; case ',': return "','"; case ':': return "':'"; case '.': return "'.'"; default: return "token"; }
    }
    void expect(int t) { if (cur.t == t) { advance(); return; } char b[80]; int n = 0; const char* nm = tokName(t); while (*nm && n < 60) b[n++] = *nm++; const char* e = " expected"; for (int i = 0; e[i]; i++) b[n++] = e[i]; b[n] = 0; err(b); }
    void expectMatch(int what, int who, int whereLine) {
        if (cur.t == what) { advance(); return; }
        if (whereLine == cur.line) { expect(what); return; }
        char b[140]; int n = 0; const char* nm = tokName(what); while (*nm) b[n++] = *nm++; const char* m = " expected (to close "; for (int i = 0; m[i]; i++) b[n++] = m[i]; const char* w = tokName(who); while (*w) b[n++] = *w++; const char* at = " at line "; for (int i = 0; at[i]; i++) b[n++] = at[i];
        char nb[12]; int ln = 0, v = whereLine; if (!v) nb[ln++] = '0'; while (v) { nb[ln++] = (char)('0' + v % 10); v /= 10; } while (ln) b[n++] = nb[--ln]; b[n++] = ')'; b[n] = 0; err(b);
    }
    Str* expectName() { if (cur.t != TK_NAME) { err("<name> expected"); return nullptr; } Str* s = vm.internPerm(cur.s, cur.len); advance(); return s; }

    // ------------------------------------------------------------ node allocation (permanent)
    Expr* mkE(u8 k) { Expr* e = (Expr*)vm.heap.perm(sizeof(Expr)); if (!e) { vm.setFail(F_MEMORY, "script too large"); static Expr dummy; return &dummy; } e->k = k; e->line = (u16)cur.line; e->col = (u16)cur.col; return e; }
    Stmt* mkS(u8 k) { Stmt* s = (Stmt*)vm.heap.perm(sizeof(Stmt)); if (!s) { vm.setFail(F_MEMORY, "script too large"); static Stmt dummy; return &dummy; } s->k = k; s->line = (u16)cur.line; s->col = (u16)cur.col; return s; }
    template <class T> T** pushList(int base, int n) { T** out = (T**)vm.heap.perm(sizeof(T*) * (n ? n : 1)); if (!out) { vm.setFail(F_MEMORY, "script too large"); return nullptr; } for (int i = 0; i < n; i++) out[i] = (T*)scratch[base + i]; sc = base; return out; }
    void spush(void* p) { if (sc >= 5990) { vm.setFail(F_ERROR, "chunk too complex"); return; } scratch[sc++] = p; }

    // ------------------------------------------------------------ scopes
    VarInfo* declare(Str* name) {
        if (fs->nlocals >= 195) { err("too many local variables"); return nullptr; }
        VarInfo* v = (VarInfo*)vm.heap.perm(sizeof(VarInfo)); if (!v) { vm.setFail(F_MEMORY, "script too large"); return nullptr; } v->slot = fs->activeSlots++; v->captured = false; v->name = name; if (fs->activeSlots > fs->maxSlots) fs->maxSlots = fs->activeSlots;
        fs->names[fs->nlocals] = name; fs->vars[fs->nlocals++] = v; return v;
    }
    int hiddenSlot() { int s = fs->activeSlots++; if (fs->activeSlots > fs->maxSlots) fs->maxSlots = fs->activeSlots; return s; }
    struct Scope { int nl, as; }; Scope openScope() { return { fs->nlocals, fs->activeSlots }; } void closeScope(Scope s) { fs->nlocals = s.nl; fs->activeSlots = s.as; }
    int findUp(FuncState* f, Str* name) {
        for (int i = 0; i < f->nups; i++) if (f->upNames[i] == name) return i; if (!f->parent) return -1;
        for (int i = f->parent->nlocals - 1; i >= 0; i--) if (f->parent->names[i] == name) { if (f->nups >= 58) { err("too many upvalues"); return -1; } f->parent->vars[i]->captured = true; f->ups[f->nups] = { true, f->parent->vars[i], 0 }; f->upNames[f->nups] = name; return f->nups++; }
        int idx = findUp(f->parent, name); if (idx < 0) return -1; if (f->nups >= 58) { err("too many upvalues"); return -1; } f->ups[f->nups] = { false, nullptr, idx }; f->upNames[f->nups] = name; return f->nups++;
    }
    Expr* singleVar(Str* name) {
        Expr* e = mkE(E_LOCAL);
        for (int i = fs->nlocals - 1; i >= 0; i--) if (fs->names[i] == name) { e->var = fs->vars[i]; return e; }
        int u = findUp(fs, name); if (u >= 0) { e->k = E_UP; e->idx = u; return e; }
        e->k = E_GLOBAL; e->str = name; return e;
    }

    // ------------------------------------------------------------ expressions
    static int leftPri(int op) { switch (op) { case O_OR_: return 1; case O_AND_: return 2; case O_EQ: case O_NE: case O_LT: case O_LE: case O_GT: case O_GE: return 3; case O_BOR: return 4; case O_BXOR: return 5; case O_BAND: return 6; case O_SHL: case O_SHR: return 7; case O_CONCAT: return 9; case O_ADD: case O_SUB: return 10; case O_MUL: case O_DIV: case O_IDIV: case O_MOD: return 11; case O_POW: return 14; } return -1; }
    static int rightPri(int op) { switch (op) { case O_CONCAT: return 8; case O_POW: return 13; default: return leftPri(op); } }
    enum { O_OR_ = 100, O_AND_ = 101 };
    int binop(int t) { switch (t) { case '+': return O_ADD; case '-': return O_SUB; case '*': return O_MUL; case '/': return O_DIV; case TK_IDIV: return O_IDIV; case '%': return O_MOD; case '^': return O_POW; case TK_CONCAT: return O_CONCAT; case TK_EQ: return O_EQ; case TK_NE: return O_NE; case '<': return O_LT; case TK_LE: return O_LE; case '>': return O_GT; case TK_GE: return O_GE; case '&': return O_BAND; case '|': return O_BOR; case '~': return O_BXOR; case TK_SHL: return O_SHL; case TK_SHR: return O_SHR; case TK_AND: return O_AND_; case TK_OR: return O_OR_; } return -1; }
    Expr* expr(int limit = 0) {
        if (++nest > 150) { err("chunk has too many syntax levels"); nest--; return mkE(E_NIL); }
        Expr* l;
        if (cur.t == TK_NOT || cur.t == '-' || cur.t == '#' || cur.t == '~') {
            int t = cur.t; Expr* u = mkE(E_UN); advance(); u->op = t == TK_NOT ? O_NOT : t == '-' ? O_NEG : t == '#' ? O_LEN : O_BNOT; u->a = expr(12);
            if (u->op == O_NEG && u->a->k == E_NUM) { u->a->num = -u->a->num; l = u->a; } else l = u;
        } else l = simple();
        for (;;) {
            int op = binop(cur.t); if (op < 0 || leftPri(op) <= limit) break;
            Expr* e = mkE(E_BIN); e->line = (u16)cur.line; e->col = (u16)cur.col; advance(); Expr* r = expr(rightPri(op));
            e->a = l; e->b = r; if (op == O_AND_) e->k = E_AND; else if (op == O_OR_) e->k = E_OR; else e->op = (u8)op; l = e; if (bad()) break;
        }
        nest--; return l;
    }
    Expr* simple() {
        switch (cur.t) {
            case TK_NUM: { Expr* e = mkE(E_NUM); e->num = cur.num; advance(); return e; }
            case TK_STR: { Expr* e = mkE(E_STR); e->str = strOfCur; advance(); return e; }
            case TK_NIL: { Expr* e = mkE(E_NIL); advance(); return e; } case TK_TRUE: { Expr* e = mkE(E_TRUE); advance(); return e; } case TK_FALSE: { Expr* e = mkE(E_FALSE); advance(); return e; }
            case TK_DOTS: { if (!fs->vararg) { err("cannot use '...' outside a vararg function"); return mkE(E_NIL); } Expr* e = mkE(E_VARARG); advance(); return e; }
            case '{': return tableCons();
            case TK_FUNCTION: { int ln = cur.line; advance(); return funcBody(false, ln, nullptr); }
        }
        return suffixed();
    }
    Expr* primary() {
        if (cur.t == TK_NAME) { Str* n = vm.internPerm(cur.s, cur.len); Tok t = cur; advance(); Expr* e = singleVar(n); e->line = (u16)t.line; e->col = (u16)t.col; return e; }
        if (cur.t == '(') { int ln = cur.line; advance(); Expr* e = expr(); expectMatch(')', '(', ln); Expr* p = mkE(E_PAREN); p->a = e; return p; }
        err("unexpected symbol"); advance(); return mkE(E_NIL);
    }
    Expr* suffixed() {
        Expr* e = primary();
        for (;;) {
            if (bad()) return e;
            switch (cur.t) {
                case '.': { advance(); Expr* i = mkE(E_INDEX); i->a = e; Expr* k = mkE(E_STR); k->str = expectName(); i->b = k; e = i; break; }
                case '[': { advance(); Expr* k = expr(); expect(']'); Expr* i = mkE(E_INDEX); i->a = e; i->b = k; e = i; break; }
                case ':': { advance(); Expr* m = mkE(E_METH); m->a = e; m->str = expectName(); callArgs(m); e = m; break; }
                case '(': case TK_STR: case '{': { Expr* c = mkE(E_CALL); c->a = e; callArgs(c); e = c; break; }
                default: return e;
            }
        }
    }
    void callArgs(Expr* c) {
        int base = sc;
        if (cur.t == TK_STR) { Expr* s = mkE(E_STR); s->str = strOfCur; advance(); spush(s); }
        else if (cur.t == '{') spush(tableCons());
        else if (cur.t == '(') {
            int ln = cur.line; advance();
            if (cur.t != ')') { do { spush(expr()); } while (accept(',') && !bad()); }
            expectMatch(')', '(', ln);
        } else err("function arguments expected");
        c->n = sc - base; c->list = pushList<Expr>(base, c->n);
    }
    Expr* tableCons() {
        Expr* t = mkE(E_TABLE); int ln = cur.line; expect('{'); int base = sc;
        while (cur.t != '}' && !bad()) {
            if (cur.t == TK_NAME && peek().t == '=') { Expr* k = mkE(E_STR); k->str = vm.internPerm(cur.s, cur.len); advance(); advance(); spush(k); spush(expr()); }
            else if (cur.t == '[') { advance(); Expr* k = expr(); expect(']'); expect('='); spush(k); spush(expr()); }
            else { spush(nullptr); spush(expr()); }
            if (!accept(',') && !accept(';')) break;
        }
        expectMatch('}', '{', ln); t->n = (sc - base) / 2; t->list = pushList<Expr>(base, sc - base); return t;
    }
    Expr* funcBody(bool isMethod, int line, Str* name) {
        FuncState* nf = (FuncState*)vm.heap.perm(sizeof(FuncState)); if (!nf) { vm.setFail(F_MEMORY, "script too large"); return mkE(E_NIL); }
        nf->parent = fs; Proto* p = (Proto*)vm.heap.perm(sizeof(Proto)); if (!p) { vm.setFail(F_MEMORY, "script too large"); return mkE(E_NIL); } nf->p = p; p->line = line; p->name = name; fs = nf;
        int ln = cur.line; expect('('); int np = 0; VarInfo* pv[60];
        if (isMethod) pv[np++] = declare(vm.internPerm("self", 4));
        if (cur.t != ')') {
            do { if (cur.t == TK_DOTS) { advance(); nf->vararg = true; break; } Str* n = expectName(); if (np >= 58) { err("too many parameters"); break; } pv[np++] = declare(n); } while (accept(',') && !bad());
        }
        expectMatch(')', '(', ln); p->nparams = (u16)np; p->vararg = nf->vararg;
        p->params = (VarInfo**)vm.heap.perm(sizeof(VarInfo*) * (np ? np : 1)); for (int i = 0; i < np; i++) p->params[i] = pv[i];
        int base = sc; block(); p->nbody = sc - base; p->body = pushList<Stmt>(base, p->nbody); expectMatch(TK_END, TK_FUNCTION, line);
        p->nslots = nf->maxSlots + 1; p->nups = nf->nups; p->ups = (UpDesc*)vm.heap.perm(sizeof(UpDesc) * (nf->nups ? nf->nups : 1));
        for (int i = 0; i < nf->nups; i++) p->ups[i] = nf->ups[i]; fs = nf->parent;
        Expr* e = mkE(E_FUNC); e->proto = p; return e;
    }
    // ------------------------------------------------------------ statements
    bool blockFollow() const { return cur.t == TK_EOF || cur.t == TK_END || cur.t == TK_ELSE || cur.t == TK_ELSEIF || cur.t == TK_UNTIL; }
    void block() {      // appends statements to the scratch stack; caller packs them
        Scope s = openScope();
        while (!blockFollow() && !bad()) {
            if (cur.t == TK_RETURN) { spush(retStat()); break; }
            Stmt* st = statement(); if (st) spush(st);
        }
        closeScope(s);
    }
    Stmt* retStat() {
        Stmt* s = mkS(S_RETURN); advance(); int base = sc;
        if (!blockFollow() && cur.t != ';') { do { spush(expr()); } while (accept(',') && !bad()); }
        accept(';'); s->nexprs = sc - base; s->exprs = pushList<Expr>(base, s->nexprs); return s;
    }
    Stmt* blockStmt(Stmt* s, bool second = false) { int base = sc; block(); int n = sc - base; Stmt** b = pushList<Stmt>(base, n); if (second) { s->body2 = b; s->nbody2 = n; } else { s->body = b; s->nbody = n; } return s; }
    Stmt* ifStat(int line) {
        Stmt* s = mkS(S_IF); s->line = (u16)cur.line; advance(); s->e1 = expr(); expect(TK_THEN); blockStmt(s);
        if (cur.t == TK_ELSEIF) { Stmt* inner = ifStat(line); int base = sc; spush(inner); s->body2 = pushList<Stmt>(base, 1); s->nbody2 = 1; return s; }   // inner consumed through END
        if (accept(TK_ELSE)) blockStmt(s, true);
        expectMatch(TK_END, TK_IF, line); return s;
    }
    Stmt* statement() {
        int line = cur.line;
        switch (cur.t) {
            case ';': advance(); return nullptr;
            case TK_IF: return ifStat(line);
            case TK_WHILE: { Stmt* s = mkS(S_WHILE); advance(); s->e1 = expr(); expect(TK_DO); blockStmt(s); expectMatch(TK_END, TK_WHILE, line); return s; }
            case TK_DO: { Stmt* s = mkS(S_DO); advance(); blockStmt(s); expectMatch(TK_END, TK_DO, line); return s; }
            case TK_FOR: return forStat(line);
            case TK_REPEAT: {
                Stmt* s = mkS(S_REPEAT); advance(); Scope sc2 = openScope(); int base = sc; while (!blockFollow() && !bad()) { if (cur.t == TK_RETURN) { spush(retStat()); break; } Stmt* st = statement(); if (st) spush(st); }
                s->nbody = sc - base; s->body = pushList<Stmt>(base, s->nbody); expectMatch(TK_UNTIL, TK_REPEAT, line); s->e1 = expr(); closeScope(sc2); return s;
            }
            case TK_FUNCTION: {
                advance(); Tok nt = cur; Str* n = expectName(); Expr* target = singleVar(n); target->line = (u16)nt.line; bool method = false; Str* full = n;
                while (cur.t == '.' || cur.t == ':') { bool colon = cur.t == ':'; advance(); Expr* i = mkE(E_INDEX); i->a = target; Expr* k = mkE(E_STR); k->str = expectName(); i->b = k; target = i; full = k->str; if (colon) { method = true; break; } }
                Stmt* s = mkS(S_ASSIGN); s->line = (u16)line; Expr* f = funcBody(method, line, full); int base = sc; spush(target); s->ntargets = 1; s->targets = pushList<Expr>(base, 1); base = sc; spush(f); s->nexprs = 1; s->exprs = pushList<Expr>(base, 1); return s;
            }
            case TK_LOCAL: {
                advance();
                if (accept(TK_FUNCTION)) { Stmt* s = mkS(S_LOCALFUNC); s->line = (u16)line; Str* n = expectName(); VarInfo* v = declare(n); s->e1 = funcBody(false, line, n); int base = sc; spush(v); s->nvars = 1; s->vars = pushList<VarInfo>(base, 1); return s; }
                Stmt* s = mkS(S_LOCAL); Str* names[60]; int nn = 0;
                do { Str* n = expectName(); if (nn < 58) names[nn++] = n; if (accept('<')) { Str* at = expectName(); if (at && !(at->len == 5 && mcmp(at->data, "const", 5) == 0) && !(at->len == 5 && mcmp(at->data, "close", 5) == 0)) err("unknown attribute"); expect('>'); } } while (accept(',') && !bad());
                int base = sc; if (accept('=')) { do { spush(expr()); } while (accept(',') && !bad()); } s->nexprs = sc - base; s->exprs = pushList<Expr>(base, s->nexprs);
                base = sc; for (int i = 0; i < nn; i++) spush(declare(names[i])); s->nvars = nn; s->vars = pushList<VarInfo>(base, nn); return s;
            }
            case TK_RETURN: return retStat();
            case TK_BREAK: { Stmt* s = mkS(S_BREAK); advance(); return s; }
            case TK_GOTO: case TK_DBCOLON: err("goto and labels are not supported in LUA scripts"); advance(); return nullptr;
        }
        // expression statement: call or assignment
        Expr* e = suffixed();
        if (cur.t == '=' || cur.t == ',') {
            Stmt* s = mkS(S_ASSIGN); s->line = (u16)line; int base = sc; spush(e);
            while (accept(',')) spush(suffixed());
            for (int i = base; i < sc; i++) { Expr* t = (Expr*)scratch[i]; if (t->k != E_LOCAL && t->k != E_UP && t->k != E_GLOBAL && t->k != E_INDEX) { err("syntax error (cannot assign to this expression)"); break; } }
            s->ntargets = sc - base; s->targets = pushList<Expr>(base, s->ntargets); expect('='); base = sc; do { spush(expr()); } while (accept(',') && !bad());
            s->nexprs = sc - base; s->exprs = pushList<Expr>(base, s->nexprs); return s;
        }
        if (e->k != E_CALL && e->k != E_METH) { err("syntax error"); return nullptr; }
        Stmt* s = mkS(S_CALL); s->line = (u16)line; s->e1 = e; return s;
    }
    Stmt* forStat(int line) {
        advance(); Str* n1 = expectName(); Stmt* s;
        if (cur.t == '=') {
            s = mkS(S_FORNUM); s->line = (u16)line; advance(); s->e1 = expr(); expect(','); s->e2 = expr(); if (accept(',')) s->e3 = expr(); expect(TK_DO);
            Scope sc2 = openScope(); VarInfo* v = declare(n1); int base = sc; spush(v); s->vars = pushList<VarInfo>(base, 1); s->nvars = 1; blockStmt(s); closeScope(sc2);
        } else {
            s = mkS(S_FORIN); s->line = (u16)line; Str* names[8]; int nn = 0; names[nn++] = n1; while (accept(',')) { Str* n = expectName(); if (nn < 7) names[nn++] = n; } expect(TK_IN);
            int base = sc; do { spush(expr()); } while (accept(',') && !bad()); s->nexprs = sc - base; s->exprs = pushList<Expr>(base, s->nexprs); expect(TK_DO);
            Scope sc2 = openScope(); s->hidden = hiddenSlot(); hiddenSlot(); hiddenSlot(); base = sc; for (int i = 0; i < nn; i++) spush(declare(names[i])); s->nvars = nn; s->vars = pushList<VarInfo>(base, nn); blockStmt(s); closeScope(sc2);
        }
        expectMatch(TK_END, TK_FOR, line); return s;
    }
    Proto* parseChunk() {
        FuncState* f = (FuncState*)vm.heap.perm(sizeof(FuncState)); Proto* p = (Proto*)vm.heap.perm(sizeof(Proto)); if (!f || !p) { vm.setFail(F_MEMORY, "script too large"); return nullptr; }
        f->p = p; f->vararg = true; fs = f; p->vararg = true; p->line = 0; advance();
        int base = sc; block(); if (cur.t != TK_EOF && !bad()) err("'<eof>' expected");
        p->nbody = sc - base; p->body = pushList<Stmt>(base, p->nbody); p->nslots = f->maxSlots + 1; p->nups = 0; p->nparams = 0; p->params = nullptr; p->ups = nullptr; return bad() ? nullptr : p;
    }
};

// =================================================================== interpreter
struct Frame { Value* base; Closure* cl; Value* va; int nva; };
struct Interp {
    VM& vm;
    explicit Interp(VM& v) : vm(v) {}
    NativeFn fnNext = nullptr, fnIpairsIter = nullptr;

    #define BAIL(r) do { if (vm.fail) return r; } while (0)
    void push(const Value& v) { if (vm.sp >= vm.stack + vm.stackCap - 8) { vm.setFail(F_STACK, "script stack overflow"); return; } *vm.sp++ = v; }

    Value arith(int op, const Value& a, const Value& b, const Expr* e) {
        double x, y;
        if (a.t == T_NUM && b.t == T_NUM) { x = a.n; y = b.n; }
        else {
            if (!vm.toNumber(a, x) || !vm.toNumber(b, y) || a.t == T_BOOL || b.t == T_BOOL) {
                if (a.t == T_TAB || b.t == T_TAB) { static const char* names[] = { "__add", "__sub", "__mul", "__div", "__idiv", "__mod", "__pow" }; if (op <= O_POW) { Value h; if (a.t == T_TAB && a.tb->meta) h = vm.tgetS(a.tb->meta, names[op]); if (h.t == T_NIL && b.t == T_TAB && b.tb->meta) h = vm.tgetS(b.tb->meta, names[op]); if (h.t != T_NIL) { Value args[2] = { a, b }; return vm.call1(h, args, 2); } } }
                const Value& bad = (a.t == T_NUM || (a.t == T_STR && vm.toNumber(a, x))) ? b : a; if (e) { const Expr* de = (&bad == &a) ? e->a : e->b; errWith((Expr*)e, "attempt to perform arithmetic on a ", bad, (Expr*)de); } else vm.setErrorf(0, 0, "attempt to perform arithmetic on a %s value", VM::typeName(bad)); return Value();
            }
        }
        switch (op) {
            case O_ADD: return Value::num(x + y); case O_SUB: return Value::num(x - y); case O_MUL: return Value::num(x * y); case O_DIV: return Value::num(x / y);
            case O_IDIV: return Value::num(floor_(x / y)); case O_MOD: { if (y == 0) return Value::num(nan_()); if (isinf_(y)) return Value::num((x >= 0) == (y > 0) ? x : y); double r = fmod_(x, y); if (r != 0 && ((r < 0) != (y < 0))) r += y; return Value::num(r); }
            case O_POW: return Value::num(pow_(x, y));
        }
        return Value();
    }
    bool toInt(const Value& v, long long& out, const Expr* e) {
        double d; if (!vm.toNumber(v, d)) { vm.setErrorf(e ? e->line : 0, e ? e->col : 0, "attempt to perform bitwise operation on a %s value", VM::typeName(v)); return false; }
        if (d != floor_(d) || d > 9.2e18 || d < -9.2e18) { vm.setError(e ? e->line : 0, e ? e->col : 0, "number has no integer representation"); return false; } out = (long long)d; return true;
    }
    bool equal(const Value& a, const Value& b) { if (a.t != b.t) return false; return VM::keyEq(a, b); }
    bool lessThan(const Value& a, const Value& b, bool orEqual, const Expr* e) {
        if (a.t == T_NUM && b.t == T_NUM) return orEqual ? a.n <= b.n : a.n < b.n;
        if (a.t == T_STR && b.t == T_STR) { u32 n = a.s->len < b.s->len ? a.s->len : b.s->len; int c = mcmp(a.s->data, b.s->data, n); if (c == 0) return orEqual ? a.s->len <= b.s->len : a.s->len < b.s->len; return c < 0; }
        if ((a.t == T_TAB && a.tb->meta) || (b.t == T_TAB && b.tb->meta)) { const char* nm = orEqual ? "__le" : "__lt"; Value h; if (a.t == T_TAB && a.tb->meta) h = vm.tgetS(a.tb->meta, nm); if (h.t == T_NIL && b.t == T_TAB && b.tb->meta) h = vm.tgetS(b.tb->meta, nm); if (h.t != T_NIL) { Value args[2] = { a, b }; return vm.call1(h, args, 2).truthy(); } }
        { char bf[120]; int n = 0; const char* m = "attempt to compare "; while (*m) bf[n++] = *m++; const char* t1 = VM::typeName(a); while (*t1) bf[n++] = *t1++; m = " with "; while (*m) bf[n++] = *m++; const char* t2 = VM::typeName(b); while (*t2) bf[n++] = *t2++; bf[n] = 0; vm.setError(e ? e->line : 0, e ? e->col : 0, bf); } return false;
    }
    Value concat(const Value& a, const Value& b, const Expr* e) {
        if ((a.t == T_STR || a.t == T_NUM) && (b.t == T_STR || b.t == T_NUM)) {
            Str* x = vm.tostr(a); BAIL(Value()); Str* y = vm.tostr(b); BAIL(Value()); size_t n = (size_t)x->len + y->len; vm.budget -= (long)(n >> 6); if (n > 200000) { vm.setError(e ? e->line : 0, e ? e->col : 0, "string too long"); return Value(); }
            char* tmp = (char*)vm.heap.alloc(n + 1); if (!tmp) { vm.setFail(F_MEMORY, "out of memory"); return Value(); } mcopy(tmp, x->data, x->len); mcopy(tmp + x->len, y->data, y->len); Str* res = vm.newStr(tmp, n); vm.heap.release(tmp, n + 1); return res ? Value::str(res) : Value();
        }
        if ((a.t == T_TAB && a.tb->meta) || (b.t == T_TAB && b.tb->meta)) { Value h; if (a.t == T_TAB && a.tb->meta) h = vm.tgetS(a.tb->meta, "__concat"); if (h.t == T_NIL && b.t == T_TAB && b.tb->meta) h = vm.tgetS(b.tb->meta, "__concat"); if (h.t != T_NIL) { Value args[2] = { a, b }; return vm.call1(h, args, 2); } }
        const Value& bad = (a.t == T_STR || a.t == T_NUM) ? b : a; vm.setErrorf(e ? e->line : 0, e ? e->col : 0, "attempt to concatenate a %s value", VM::typeName(bad)); return Value();
    }
    int lenOf(const Value& v, double& out, const Expr* e) {
        if (v.t == T_STR) { out = v.s->len; return 1; }
        if (v.t == T_TAB) { if (v.tb->meta) { Value h = vm.tgetS(v.tb->meta, "__len"); if (h.t != T_NIL) { Value a[1] = { v }; Value r = vm.call1(h, a, 1); out = r.t == T_NUM ? r.n : 0; return 1; } } out = vm.tlen(v.tb); return 1; }
        vm.setErrorf(e ? e->line : 0, e ? e->col : 0, "attempt to get length of a %s value", VM::typeName(v)); return 0;
    }
    static bool describe(Expr* e, const char*& kind, const char*& name) {
        if (e->k == E_GLOBAL) { kind = "global"; name = e->str->data; return true; }
        if (e->k == E_LOCAL && e->var->name) { kind = "local"; name = e->var->name->data; return true; }
        if (e->k == E_INDEX && e->b->k == E_STR) { kind = "field"; name = e->b->str->data; return true; }
        if (e->k == E_METH) { kind = "method"; name = e->str->data; return true; }
        return false;
    }
    void errWith(Expr* at, const char* head, const Value& v, Expr* desc) {
        char b[200]; int n = 0; while (*head && n < 80) b[n++] = *head++; const char* tn = VM::typeName(v); while (*tn) b[n++] = *tn++;
        const char *kind, *name; if (desc && describe(desc, kind, name)) { const char* m2 = " value ("; while (*m2) b[n++] = *m2++; while (*kind) b[n++] = *kind++; b[n++] = ' '; b[n++] = '\''; for (int i = 0; name[i] && i < 40; i++) b[n++] = name[i]; b[n++] = '\''; b[n++] = ')'; } else { const char* m2 = " value"; while (*m2) b[n++] = *m2++; }
        b[n] = 0; vm.setError(at->line, at->col, b);
    }
    Value readLocal(VarInfo* v, Frame& f) { Value& s = f.base[v->slot]; return v->captured ? s.cell->v : s; }
    void writeLocal(VarInfo* v, Frame& f, const Value& x) { Value& s = f.base[v->slot]; if (v->captured) s.cell->v = x; else s = x; }
    void declareLocal(VarInfo* v, Frame& f, const Value& x) { Value& s = f.base[v->slot]; if (v->captured) { Cell* c = vm.newObj<Cell>(T_CELL, sizeof(Cell)); if (!c) return; c->v = x; s.t = T_CELL; s.cell = c; } else s = x; }

    Value makeClosure(Proto* p, Frame& f) {
        Closure* c = vm.newObj<Closure>(T_FUN, sizeof(Closure) + sizeof(Cell*) * (p->nups ? p->nups : 1)); if (!c) return Value(); c->p = p; c->nups = (u32)p->nups;
        for (int i = 0; i < p->nups; i++) { const UpDesc& u = p->ups[i]; c->ups[i] = u.fromLocal ? f.base[u.var->slot].cell : f.cl->ups[u.index]; }
        Value v; v.t = T_FUN; v.c = c; return v;
    }
    int evalMulti(Expr* e, Frame& f) {            // pushes all results on the stack, returns the count
        if (e->k == E_CALL || e->k == E_METH) { int n = callExpr(e, f); if (vm.fail) return 0; for (int i = 0; i < n; i++) push(vm.ret[i]); return n; }
        if (e->k == E_VARARG) { for (int i = 0; i < f.nva; i++) push(f.va[i]); return f.nva; }
        push(eval(e, f)); return 1;
    }
    Value eval(Expr* e, Frame& f) {
        switch (e->k) {
            case E_NIL: return Value(); case E_TRUE: return Value::boolean(true); case E_FALSE: return Value::boolean(false);
            case E_NUM: return Value::num(e->num); case E_STR: return Value::str(e->str);
            case E_VARARG: return f.nva > 0 ? f.va[0] : Value();
            case E_LOCAL: return readLocal(e->var, f);
            case E_UP: return f.cl->ups[e->idx]->v;
            case E_GLOBAL: { Value k = Value::str(e->str); Node* n = vm.hfind(vm.globals, k); return n ? n->v : Value(); }
            case E_PAREN: return eval(e->a, f);
            case E_INDEX: {
                Value o = eval(e->a, f); BAIL(Value()); Value k = eval(e->b, f); BAIL(Value());
                if (o.t == T_TAB) { Value v = vm.tget(o.tb, k); if (v.t != T_NIL || !o.tb->meta) return v; vm.curLine = e->line; vm.curCol = e->col; return vm.index(o, k); }
                vm.curLine = e->line; vm.curCol = e->col;
                if (o.t != T_STR) { errWith(e, "attempt to index a ", o, e->a); return Value(); }
                return vm.index(o, k);
            }
            case E_CALL: case E_METH: { int n = callExpr(e, f); BAIL(Value()); return n > 0 ? vm.ret[0] : Value(); }
            case E_FUNC: return makeClosure(e->proto, f);
            case E_AND: { Value a = eval(e->a, f); BAIL(Value()); if (!a.truthy()) return a; return eval(e->b, f); }
            case E_OR: { Value a = eval(e->a, f); BAIL(Value()); if (a.truthy()) return a; return eval(e->b, f); }
            case E_UN: {
                Value a = eval(e->a, f); BAIL(Value());
                switch (e->op) {
                    case O_NOT: return Value::boolean(!a.truthy());
                    case O_NEG: { double x; if (a.t == T_NUM) return Value::num(-a.n); if (a.t == T_TAB && a.tb->meta) { Value h = vm.tgetS(a.tb->meta, "__unm"); if (h.t != T_NIL) { Value ar[2] = { a, a }; return vm.call1(h, ar, 2); } } if (vm.toNumber(a, x) && a.t != T_BOOL) return Value::num(-x); vm.setErrorf(e->line, e->col, "attempt to perform arithmetic on a %s value", VM::typeName(a)); return Value(); }
                    case O_LEN: { double d; if (!lenOf(a, d, e)) return Value(); return Value::num(d); }
                    case O_BNOT: { long long x; if (!toInt(a, x, e)) return Value(); return Value::num((double)(~x)); }
                }
                return Value();
            }
            case E_BIN: {
                Value a = eval(e->a, f); BAIL(Value()); Value b = eval(e->b, f); BAIL(Value());
                switch (e->op) {
                    case O_ADD: if (a.t == T_NUM && b.t == T_NUM) return Value::num(a.n + b.n); return arith(e->op, a, b, e);
                    case O_SUB: if (a.t == T_NUM && b.t == T_NUM) return Value::num(a.n - b.n); return arith(e->op, a, b, e);
                    case O_MUL: if (a.t == T_NUM && b.t == T_NUM) return Value::num(a.n * b.n); return arith(e->op, a, b, e);
                    case O_DIV: if (a.t == T_NUM && b.t == T_NUM) return Value::num(a.n / b.n); return arith(e->op, a, b, e);
                    case O_IDIV: case O_MOD: case O_POW: return arith(e->op, a, b, e);
                    case O_CONCAT: return concat(a, b, e);
                    case O_EQ: if (a.t == T_TAB && b.t == T_TAB && a.tb != b.tb && a.tb->meta) { Value h = vm.tgetS(a.tb->meta, "__eq"); if (h.t != T_NIL) { Value ar[2] = { a, b }; return Value::boolean(vm.call1(h, ar, 2).truthy()); } } return Value::boolean(equal(a, b));
                    case O_NE: return Value::boolean(!equal(a, b));
                    case O_LT: return Value::boolean(lessThan(a, b, false, e)); case O_LE: return Value::boolean(lessThan(a, b, true, e));
                    case O_GT: return Value::boolean(lessThan(b, a, false, e)); case O_GE: return Value::boolean(lessThan(b, a, true, e));
                    case O_BAND: case O_BOR: case O_BXOR: case O_SHL: case O_SHR: {
                        long long x, y; if (!toInt(a, x, e) || !toInt(b, y, e)) return Value(); unsigned long long ux = (unsigned long long)x; long long r;
                        switch (e->op) { case O_BAND: r = x & y; break; case O_BOR: r = x | y; break; case O_BXOR: r = x ^ y; break; case O_SHL: r = (y >= 64 || y <= -64) ? 0 : (y >= 0 ? (long long)(ux << y) : (long long)(ux >> -y)); break; default: r = (y >= 64 || y <= -64) ? 0 : (y >= 0 ? (long long)(ux >> y) : (long long)(ux << -y)); }
                        return Value::num((double)r);
                    }
                }
                return Value();
            }
            case E_TABLE: {
                int pos = 0; for (int i = 0; i < e->n; i++) if (!e->list[2 * i]) pos++;
                Table* t = vm.newTable((u32)pos, (u32)(e->n - pos)); if (!t) return Value(); Value tv = Value::tab(t); push(tv);   // anchored on the stack while its fields are evaluated
                int idx = 1;
                for (int i = 0; i < e->n && !vm.fail; i++) {
                    Expr* ke = e->list[2 * i]; Expr* ve = e->list[2 * i + 1];
                    if (!ke) { if (i == e->n - 1 && (ve->k == E_CALL || ve->k == E_METH || ve->k == E_VARARG)) { Value* mark = vm.sp; int n = evalMulti(ve, f); for (int j = 0; j < n; j++) vm.tsetRaw(t, Value::num((double)idx++), mark[j]); vm.sp = mark; } else { Value v = eval(ve, f); BAIL(Value()); vm.tsetRaw(t, Value::num((double)idx++), v); } }
                    else { Value k = eval(ke, f); BAIL(Value()); Value v = eval(ve, f); BAIL(Value()); if (k.t == T_NIL) { vm.setError(ke->line, ke->col, "table index is nil"); return Value(); } vm.tsetRaw(t, k, v); }
                }
                vm.sp--; return tv;
            }
        }
        return Value();
    }
    int callExpr(Expr* e, Frame& f) {
        Value* argBase = vm.sp; Value fn; int nargs = 0; const char* what = nullptr; const char* kind = "global";
        if (e->k == E_METH) {
            Value obj = eval(e->a, f); BAIL(0); vm.curLine = e->line; vm.curCol = e->col; fn = vm.index(obj, Value::str(e->str)); BAIL(0); push(obj); nargs = 1; what = e->str->data; kind = "method";
        } else { fn = eval(e->a, f); BAIL(0); if (e->a->k == E_GLOBAL) { what = e->a->str->data; } else if (e->a->k == E_INDEX && e->a->b->k == E_STR) { what = e->a->b->str->data; kind = "field"; } }
        for (int i = 0; i < e->n; i++) {
            Expr* a = e->list[i];
            if (i == e->n - 1 && (a->k == E_CALL || a->k == E_METH || a->k == E_VARARG)) nargs += evalMulti(a, f); else { Value v = eval(a, f); BAIL(0); push(v); nargs++; }
            BAIL(0);
        }
        if (vm.fail) return 0;
        vm.curLine = e->line; vm.curCol = e->col;
        if (fn.t != T_FUN && fn.t != T_NAT && !(fn.t == T_TAB && fn.tb->meta && vm.tgetS(fn.tb->meta, "__call").t != T_NIL)) {
            errWith(e, "attempt to call a ", fn, e->k == E_METH ? e : e->a); vm.sp = argBase; return 0;
        }
        int r = vm.callFrame(fn, argBase, nargs); vm.sp = argBase; return r;
    }
    Exec execBlock(Stmt** body, int n, Frame& f) {
        for (int i = 0; i < n; i++) {
            Exec x = exec(body[i], f); if (x != X_NORMAL || vm.fail) return vm.fail ? X_RETURN : x;
            if (vm.depth <= 1 && vm.heap.used > vm.gcThreshold && vm.allowGC) vm.gcRun();
        }
        return X_NORMAL;
    }
    void assignTo(Expr* t, const Value& v, Frame& f) {
        switch (t->k) {
            case E_LOCAL: writeLocal(t->var, f, v); break; case E_UP: f.cl->ups[t->idx]->v = v; break;
            case E_GLOBAL: vm.tsetRaw(vm.globals, Value::str(t->str), v); break;
            case E_INDEX: { Value o = eval(t->a, f); BAIL(); Value k = eval(t->b, f); BAIL(); vm.curLine = t->line; vm.curCol = t->col; if (o.t != T_TAB) { vm.setErrorf(t->line, t->col, "attempt to index a %s value", VM::typeName(o)); return; } if (k.t == T_NIL) { vm.setError(t->line, t->col, "table index is nil"); return; } if (!o.tb->meta) vm.tsetRaw(o.tb, k, v); else vm.setindex(o, k, v); break; }
            default: break;
        }
    }
    Exec exec(Stmt* s, Frame& f) {
        if (--vm.budget < 0) { vm.curLine = s->line; vm.setBudgetFail(); return X_RETURN; }
        vm.curLine = s->line; vm.curCol = s->col;
        switch (s->k) {
            case S_CALL: callExpr(s->e1, f); return X_NORMAL;
            case S_LOCAL: {
                Value* base = vm.sp; int n = 0;
                for (int i = 0; i < s->nexprs; i++) { Expr* e = s->exprs[i]; if (i == s->nexprs - 1 && (e->k == E_CALL || e->k == E_METH || e->k == E_VARARG)) n += evalMulti(e, f); else { Value v = eval(e, f); BAIL(X_RETURN); push(v); n++; } BAIL(X_RETURN); }
                for (int i = 0; i < s->nvars; i++) declareLocal(s->vars[i], f, i < n ? base[i] : Value());
                vm.sp = base; return X_NORMAL;
            }
            case S_LOCALFUNC: { VarInfo* v = s->vars[0]; declareLocal(v, f, Value()); Value c = makeClosure(s->e1->proto, f); BAIL(X_RETURN); writeLocal(v, f, c); return X_NORMAL; }
            case S_ASSIGN: {
                if (s->ntargets == 1 && s->nexprs == 1 && s->exprs[0]->k != E_CALL && s->exprs[0]->k != E_METH && s->exprs[0]->k != E_VARARG) { Value v = eval(s->exprs[0], f); BAIL(X_RETURN); assignTo(s->targets[0], v, f); return vm.fail ? X_RETURN : X_NORMAL; }
                Value* base = vm.sp; int n = 0;
                for (int i = 0; i < s->nexprs; i++) { Expr* e = s->exprs[i]; if (i == s->nexprs - 1 && (e->k == E_CALL || e->k == E_METH || e->k == E_VARARG)) n += evalMulti(e, f); else { Value v = eval(e, f); BAIL(X_RETURN); push(v); n++; } BAIL(X_RETURN); }
                for (int i = s->ntargets - 1; i >= 0; i--) { assignTo(s->targets[i], i < n ? base[i] : Value(), f); BAIL(X_RETURN); }
                vm.sp = base; return X_NORMAL;
            }
            case S_DO: return execBlock(s->body, s->nbody, f);
            case S_IF: { Value c = eval(s->e1, f); BAIL(X_RETURN); if (c.truthy()) return execBlock(s->body, s->nbody, f); return execBlock(s->body2, s->nbody2, f); }
            case S_WHILE: {
                for (;;) {
                    Value c = eval(s->e1, f); BAIL(X_RETURN); if (!c.truthy()) break;
                    Exec x = execBlock(s->body, s->nbody, f); if (x == X_BREAK) break; if (x == X_RETURN) return x;
                    if (--vm.budget < 0) { vm.setBudgetFail(); return X_RETURN; }
                }
                return X_NORMAL;
            }
            case S_REPEAT: {
                for (;;) {
                    Exec x = X_NORMAL; for (int i = 0; i < s->nbody; i++) { x = exec(s->body[i], f); if (x != X_NORMAL || vm.fail) break; if (vm.depth <= 1 && vm.heap.used > vm.gcThreshold && vm.allowGC) vm.gcRun(); }
                    if (vm.fail) return X_RETURN; if (x == X_BREAK) break; if (x == X_RETURN) return x;
                    Value c = eval(s->e1, f); BAIL(X_RETURN); if (c.truthy()) break;
                    if (--vm.budget < 0) { vm.setBudgetFail(); return X_RETURN; }
                }
                return X_NORMAL;
            }
            case S_FORNUM: {
                Value a = eval(s->e1, f); BAIL(X_RETURN); Value b = eval(s->e2, f); BAIL(X_RETURN); Value st = s->e3 ? eval(s->e3, f) : Value::num(1); BAIL(X_RETURN);
                double x, y, z; if (!vm.toNumber(a, x) || a.t != T_NUM) { vm.setError(s->line, s->col, "'for' initial value must be a number"); return X_RETURN; } if (!vm.toNumber(b, y) || b.t != T_NUM) { vm.setError(s->line, s->col, "'for' limit must be a number"); return X_RETURN; } if (!vm.toNumber(st, z) || st.t != T_NUM) { vm.setError(s->line, s->col, "'for' step must be a number"); return X_RETURN; }
                if (z == 0) { vm.setError(s->line, s->col, "'for' step is zero"); return X_RETURN; }
                VarInfo* v = s->vars[0];
                for (double i = x; z > 0 ? i <= y : i >= y; i += z) {
                    declareLocal(v, f, Value::num(i)); BAIL(X_RETURN);
                    Exec r = execBlock(s->body, s->nbody, f); if (r == X_BREAK) break; if (r == X_RETURN) return r;
                    if (--vm.budget < 0) { vm.setBudgetFail(); return X_RETURN; }
                }
                return X_NORMAL;
            }
            case S_FORIN: {
                Value* base = vm.sp; int n = 0;
                for (int i = 0; i < s->nexprs; i++) { Expr* e = s->exprs[i]; if (i == s->nexprs - 1 && (e->k == E_CALL || e->k == E_METH || e->k == E_VARARG)) n += evalMulti(e, f); else { Value v = eval(e, f); BAIL(X_RETURN); push(v); n++; } BAIL(X_RETURN); }
                Value* hs = f.base + s->hidden; hs[0] = n > 0 ? base[0] : Value(); hs[1] = n > 1 ? base[1] : Value(); hs[2] = n > 2 ? base[2] : Value(); vm.sp = base;
                Value ctl = hs[2];
                for (;;) {
                    Value fn = hs[0]; Value k, v; int nres = 0; Value rv[8];
                    if (fn.t == T_NAT && fn.nd->fn == fnNext && hs[1].t == T_TAB) { k = ctl; if (!vm.tnext(hs[1].tb, k, v)) { BAIL(X_RETURN); break; } rv[0] = k; rv[1] = v; nres = 2; }
                    else if (fn.t == T_NAT && fn.nd->fn == fnIpairsIter && hs[1].t == T_TAB) { double i = ctl.n + 1; Value vv = vm.tget(hs[1].tb, Value::num(i)); if (vv.t == T_NIL && !hs[1].tb->meta) break; if (hs[1].tb->meta) vv = vm.index(hs[1], Value::num(i)); if (vv.t == T_NIL) break; rv[0] = Value::num(i); rv[1] = vv; nres = 2; }
                    else {
                        Value* ab = vm.sp; push(hs[1]); push(ctl); BAIL(X_RETURN); vm.curLine = s->line; vm.curCol = s->col;
                        if (fn.t != T_FUN && fn.t != T_NAT) { vm.setError(s->line, s->col, "attempt to call a non-function value in 'for' loop"); return X_RETURN; }
                        int r = vm.callFrame(fn, ab, 2); vm.sp = ab; BAIL(X_RETURN); nres = r > 8 ? 8 : r; for (int j = 0; j < nres; j++) rv[j] = vm.ret[j];
                        if (nres == 0 || rv[0].t == T_NIL) break;
                    }
                    ctl = rv[0]; hs[2] = ctl;
                    for (int j = 0; j < s->nvars; j++) declareLocal(s->vars[j], f, j < nres ? rv[j] : Value());
                    Exec r = execBlock(s->body, s->nbody, f); if (r == X_BREAK) break; if (r == X_RETURN) return r;
                    if (--vm.budget < 0) { vm.setBudgetFail(); return X_RETURN; }
                }
                return X_NORMAL;
            }
            case S_RETURN: {
                Value* base = vm.sp; int n = 0;
                for (int i = 0; i < s->nexprs; i++) { Expr* e = s->exprs[i]; if (i == s->nexprs - 1 && (e->k == E_CALL || e->k == E_METH || e->k == E_VARARG)) n += evalMulti(e, f); else { Value v = eval(e, f); BAIL(X_RETURN); push(v); n++; } BAIL(X_RETURN); }
                if (n > 38) { vm.setError(s->line, s->col, "too many return values"); return X_RETURN; } for (int i = 0; i < n; i++) vm.ret[i] = base[i]; vm.nret = n; vm.sp = base; return X_RETURN;
            }
            case S_BREAK: return X_BREAK;
        }
        return X_NORMAL;
    }
    #undef BAIL
};

// =================================================================== VM members needing the interpreter
inline int VM::callFrame(const Value& fn, Value* args, int nargs) {
    if (fail) return 0;
    if (--budget < 0) { setBudgetFail(); return 0; } budget -= 2;
    if (++depth > maxDepth) { depth--; setFail(F_DEPTH, "script call stack too deep (runaway recursion?)"); return 0; }
    struct Dep { VM& v; ~Dep() { v.depth--; } } dep{*this};
    sp = args + nargs;
    if (fn.t == T_NAT) { nret = 0; int n = fn.nd->fn(*this, args, nargs); if (fail) return 0; nret = n; return n; }
    if (fn.t == T_TAB) {   // __call
        Value h = fn.tb->meta ? tgetS(fn.tb->meta, "__call") : Value(); if (h.t == T_NIL) { setError(0, 0, "attempt to call a table value"); return 0; }
        if (sp + 2 >= stack + stackCap - 8) { setFail(F_STACK, "script stack overflow"); return 0; }
        for (int i = nargs; i > 0; i--) args[i] = args[i - 1]; args[0] = fn; sp = args + nargs + 1; depth--; int r = callFrame(h, args, nargs + 1); depth++; return r;
    }
    Closure* cl = fn.c; Proto* p = cl->p; Value* base; Value* va = nullptr; int nva = 0;
    if (p->vararg) { nva = nargs > p->nparams ? nargs - p->nparams : 0; va = args + p->nparams; base = args + nargs; if (base + p->nslots + 64 >= stack + stackCap) { setFail(F_STACK, "script stack overflow"); return 0; } for (int i = 0; i < p->nparams; i++) base[i] = i < nargs ? args[i] : Value(); }
    else { base = args; if (base + p->nslots + 64 >= stack + stackCap) { setFail(F_STACK, "script stack overflow"); return 0; } for (int i = nargs < p->nparams ? nargs : p->nparams; i < p->nparams; i++) base[i] = Value(); }
    for (int i = p->nparams; i < p->nslots; i++) base[i] = Value();
    Frame fr{ base, cl, va, nva }; Interp ip(*this); ip.fnNext = natNext; ip.fnIpairsIter = natIpairsIter;
    for (int i = 0; i < p->nparams; i++) if (p->params[i]->captured) { Cell* c = newObj<Cell>(T_CELL, sizeof(Cell)); if (!c) return 0; c->v = base[i]; base[i].t = T_CELL; base[i].cell = c; }
    sp = base + p->nslots; nret = 0;
    Exec x = ip.execBlock(p->body, p->nbody, fr); if (fail) return 0; if (x != X_RETURN) nret = 0; return nret;
}
inline int VM::callv(const Value& fn, Value* args, int nargs) {
    if (sp + nargs + 4 >= stack + stackCap - 8) { setFail(F_STACK, "script stack overflow"); return 0; }
    Value* ab = sp; for (int i = 0; i < nargs; i++) ab[i] = args[i]; sp = ab + nargs; int r = callFrame(fn, ab, nargs); sp = ab; return r;
}

// =================================================================== garbage collector (stop-the-world, only at safe points)
inline void VM::markObj(Obj* o) { if (!o || o->perm || o->mark) return; o->mark = 1; if (o->type != T_STR) { o->gnext = grayList; grayList = o; } }
inline void VM::markVal(const Value& v) { switch (v.t) { case T_STR: case T_TAB: case T_FUN: case T_CELL: markObj(v.o); break; default: break; } }
inline void VM::gcRun() {
    gcCount++; grayList = nullptr;
    markObj(globals); markObj(registry); if (strmeta) markObj(strmeta); markVal(errval);
    for (Value* p = stack; p < sp; p++) markVal(*p);
    while (grayList) {
        Obj* o = grayList; grayList = o->gnext;
        if (o->type == T_TAB) { Table* t = (Table*)o; if (t->meta) markObj(t->meta); for (u32 i = 0; i < t->asz; i++) markVal(t->arr[i]); for (u32 i = 0; i < t->hcap; i++) if (t->hash[i].k.t != T_NIL && t->hash[i].v.t != T_NIL) { markVal(t->hash[i].k); markVal(t->hash[i].v); } }
        else if (o->type == T_FUN) { Closure* c = (Closure*)o; for (u32 i = 0; i < c->nups; i++) markObj(c->ups[i]); }
        else if (o->type == T_CELL) markVal(((Cell*)o)->v);
    }
    Obj** pp = &all;
    while (*pp) {
        Obj* o = *pp;
        if (o->mark) { o->mark = 0; pp = &o->next; continue; }
        *pp = o->next;
        if (o->type == T_TAB) { Table* t = (Table*)o; if (t->arr) heap.release(t->arr, sizeof(Value) * t->acap); if (t->hash) heap.release(t->hash, sizeof(Node) * t->hcap); }
        heap.release(o, o->sz);
    }
    size_t cap = heap.size - heap.permLimit; gcThreshold = heap.used * 2 > cap / 2 ? (heap.used + cap / 4 < cap * 9 / 10 ? heap.used + cap / 4 : cap * 9 / 10) : cap / 2;
    if (gcThreshold < heap.used + 4096) gcThreshold = heap.used + 4096;
}
inline bool VM::compile(const char* src, size_t len, Value* outFn) {
    Parser* ps = (Parser*)heap.perm(sizeof(Parser)); if (!ps) { setFail(F_MEMORY, "script too large"); return false; }
    new (ps, PlaceTag{}) Parser(*this, src, len); Proto* p = ps->parseChunk(); if (fail || !p) return false;
    Closure* c = newObj<Closure>(T_FUN, sizeof(Closure)); if (!c) return false; c->p = p; c->nups = 0; outFn->t = T_FUN; outFn->c = c; return true;
}
}  // namespace lx
