// lx_vm.hpp - "LX": the Vektur Lua-subset virtual machine (Lua 5.4 syntax and semantics, doubles only, no libraries that
// reach outside the sandbox). Freestanding, exception-free, runs entirely inside a fixed pool, with an instruction budget,
// call-depth limit and memory cap so a faulty script can never stall or crash the audio engine.
#pragma once
#include "lx_core.hpp"

namespace lx {

enum Type : u8 { T_NIL, T_BOOL, T_NUM, T_STR, T_TAB, T_FUN, T_NAT, T_CELL };
struct VM; struct Table; struct Closure; struct Cell; struct Str; struct Proto; struct Expr; struct Stmt; struct NatDef;

struct Obj { Obj* next; Obj* gnext; u8 type; u8 mark; u8 perm; u8 pad; u32 sz; };
struct Value {
    u8 t;
    union { double n; bool b; Str* s; Table* tb; Closure* c; const NatDef* nd; Cell* cell; Obj* o; };
    Value() : t(T_NIL), n(0) {}
    static Value num(double x) { Value v; v.t = T_NUM; v.n = x; return v; }
    static Value boolean(bool x) { Value v; v.t = T_BOOL; v.n = 0; v.b = x; return v; }
    static Value str(Str* x) { Value v; v.t = T_STR; v.s = x; return v; }
    static Value tab(Table* x) { Value v; v.t = T_TAB; v.tb = x; return v; }
    bool isNil() const { return t == T_NIL; }
    bool truthy() const { return !(t == T_NIL || (t == T_BOOL && !b)); }
};
struct Str : Obj { u32 len, hash; char data[1]; };
struct Node { Value k, v; };
struct Table : Obj { Value* arr; u32 asz, acap; Node* hash; u32 hcap, hused; Table* meta; };
struct Cell : Obj { Value v; };
struct Closure : Obj { Proto* p; u32 nups; Cell* ups[1]; };
typedef int (*NativeFn)(VM& vm, Value* a, int n);
struct NatDef { const char* name; NativeFn fn; };

// ------------------------------------------------------------------ AST
struct VarInfo { int slot; bool captured; Str* name; };
enum EK : u8 { E_NIL, E_TRUE, E_FALSE, E_NUM, E_STR, E_VARARG, E_LOCAL, E_UP, E_GLOBAL, E_INDEX, E_CALL, E_METH, E_FUNC, E_BIN, E_UN, E_AND, E_OR, E_TABLE, E_PAREN };
enum OP : u8 { O_ADD, O_SUB, O_MUL, O_DIV, O_IDIV, O_MOD, O_POW, O_CONCAT, O_EQ, O_NE, O_LT, O_LE, O_GT, O_GE, O_BAND, O_BOR, O_BXOR, O_SHL, O_SHR, O_NEG, O_NOT, O_LEN, O_BNOT };
struct Expr { u8 k, op; u16 line, col; Expr *a, *b; Expr** list; int n; union { double num; Str* str; VarInfo* var; Proto* proto; int idx; }; };
enum SK : u8 { S_LOCAL, S_ASSIGN, S_CALL, S_DO, S_WHILE, S_REPEAT, S_IF, S_FORNUM, S_FORIN, S_LOCALFUNC, S_RETURN, S_BREAK };
struct Stmt { u8 k; u16 line, col; Expr *e1, *e2, *e3; Expr** exprs; int nexprs; Expr** targets; int ntargets; Stmt** body; int nbody; Stmt** body2; int nbody2; VarInfo** vars; int nvars; Proto* proto; int hidden; };
struct UpDesc { bool fromLocal; VarInfo* var; int index; };
struct Proto { u16 nparams; bool vararg; int nslots; Stmt** body; int nbody; UpDesc* ups; int nups; VarInfo** params; Str* name; int line; };

enum Exec { X_NORMAL, X_BREAK, X_RETURN };
enum Fail : u8 { F_NONE, F_ERROR, F_BUDGET, F_MEMORY, F_DEPTH, F_STACK };

struct VM {
    Heap heap; Obj* all = nullptr; Obj* grayList = nullptr;
    Table* globals = nullptr; Table* registry = nullptr; Table* strmeta = nullptr;
    Value* stack = nullptr; int stackCap = 0; Value* sp = nullptr;
    Value ret[40]; int nret = 0;
    // diagnostics / limits
    long budget = 0, budgetStart = 0; int depth = 0, maxDepth = 120; Fail fail = F_NONE; char errmsg[300]; int errLine = 0, errCol = 0; Value errval; bool errHasVal = false; bool pcallFatal = false;
    size_t gcThreshold = 0; size_t gcCount = 0; u64 rngState = 88172645463325252ULL; void* user = nullptr; void (*printHook)(void*, const char*, size_t) = nullptr; const char* chunk = "script";
    NativeFn natNext = nullptr, natIpairsIter = nullptr; u32 stmtCountSinceGC = 0; bool allowGC = true;
    long instrUsed() const { return budgetStart - budget; }

    // ---------------------------------------------------------- setup
    bool init(void* pool, size_t bytes, size_t permBytes, int stackValues) {
        heap.init(pool, bytes, permBytes); all = nullptr; stackCap = stackValues; stack = (Value*)heap.perm(sizeof(Value) * (size_t)stackValues); if (!stack) return false;
        sp = stack; fail = F_NONE; depth = 0; errval = Value(); errmsg[0] = 0; gcThreshold = (bytes - permBytes) / 2;
        globals = newTable(0, 32); registry = newTable(4, 4); strmeta = nullptr; itab = nullptr; icap = icount = 0; if (!globals || !registry) return false; return true;
    }
    // ---------------------------------------------------------- objects
    template <class T> T* newObj(u8 type, size_t bytes, bool perm = false) {
        T* o = (T*)(perm ? heap.perm(bytes) : heap.alloc(bytes)); if (!o) { setFail(F_MEMORY, "out of memory (script uses too much memory)"); return nullptr; }
        o->type = type; o->mark = 0; o->perm = perm ? 1 : 0; o->sz = (u32)bytes; o->gnext = nullptr;
        if (!perm) { o->next = all; all = o; } else o->next = nullptr; return o;
    }
    static u32 hashBytes(const char* s, size_t n) { u32 h = 2166136261u ^ (u32)n; size_t step = (n >> 5) + 1; for (size_t i = n; i >= step; i -= step) h = (h ^ (u8)s[i - 1]) * 16777619u; return h; }
    Str* newStr(const char* s, size_t n, bool perm = false) {
        Str* o = newObj<Str>(T_STR, sizeof(Str) + n, perm); if (!o) return nullptr; o->len = (u32)n; o->hash = hashBytes(s, n); mcopy(o->data, s, n); o->data[n] = 0; return o;
    }
    Str* newStr(const char* z) { return newStr(z, slen(z)); }
    // permanent, interned strings (AST constants, library names): pointer equality is then the fast path of every lookup
    Str** itab = nullptr; u32 icap = 0, icount = 0;
    Str* internPerm(const char* s, size_t n) {
        if (!itab) { icap = 4096; itab = (Str**)heap.perm(sizeof(Str*) * icap); if (!itab) return nullptr; }
        u32 h = hashBytes(s, n), m = icap - 1, i = h & m;
        while (itab[i]) { Str* e = itab[i]; if (e->hash == h && e->len == n && mcmp(e->data, s, n) == 0) return e; i = (i + 1) & m; }
        Str* ns = newStr(s, n, true); if (!ns) return nullptr; if (icount * 2 < icap) { itab[i] = ns; icount++; } return ns;
    }
    Table* newTable(u32 narr, u32 nhash) {
        Table* t = newObj<Table>(T_TAB, sizeof(Table)); if (!t) return nullptr; t->arr = nullptr; t->asz = t->acap = 0; t->hash = nullptr; t->hcap = t->hused = 0; t->meta = nullptr;
        if (narr) { t->arr = (Value*)heap.alloc(sizeof(Value) * narr); if (!t->arr) { setFail(F_MEMORY, "out of memory"); return nullptr; } t->acap = (u32)(Heap::clsSize(Heap::cls(sizeof(Value) * narr)) / sizeof(Value)); }
        if (nhash) hashGrow(t, nhash); return t;
    }
    // ---------------------------------------------------------- table internals
    static bool keyEq(const Value& a, const Value& b) {
        if (a.t != b.t) return false;
        switch (a.t) { case T_NUM: return a.n == b.n; case T_BOOL: return a.b == b.b; case T_STR: return a.s == b.s || (a.s->hash == b.s->hash && a.s->len == b.s->len && mcmp(a.s->data, b.s->data, a.s->len) == 0); case T_NIL: return true; default: return a.o == b.o; }
    }
    static u32 hashVal(const Value& v) {
        switch (v.t) { case T_STR: return v.s->hash; case T_NUM: { u64 b; __builtin_memcpy(&b, &v.n, 8); return (u32)(b ^ (b >> 32)) * 2654435761u; } case T_BOOL: return v.b ? 1 : 2; default: { size_t p = (size_t)v.o; return (u32)(p >> 4) * 2654435761u; } }
    }
    Node* hfind(Table* t, const Value& k) {
        if (!t->hcap) return nullptr; u32 m = t->hcap - 1, i = hashVal(k) & m;
        for (u32 n = 0; n < t->hcap; n++) { Node* nd = &t->hash[i]; if (nd->k.t == T_NIL) return nullptr; if (keyEq(nd->k, k)) return nd; i = (i + 1) & m; } return nullptr;
    }
    void hashGrow(Table* t, u32 want) {
        u32 live = 0; for (u32 i = 0; i < t->hcap; i++) if (t->hash[i].k.t != T_NIL && t->hash[i].v.t != T_NIL) live++;
        u32 need = live + want; u32 cap = 4; while (cap < need * 2 + 1) cap <<= 1; Node* nn = (Node*)heap.alloc(sizeof(Node) * cap); if (!nn) { setFail(F_MEMORY, "out of memory"); return; }
        for (u32 i = 0; i < cap; i++) { nn[i].k = Value(); nn[i].v = Value(); }
        Node* old = t->hash; u32 oc = t->hcap; t->hash = nn; t->hcap = cap; t->hused = 0;
        for (u32 i = 0; i < oc; i++) if (old[i].k.t != T_NIL && old[i].v.t != T_NIL) { u32 m = cap - 1, j = hashVal(old[i].k) & m; while (nn[j].k.t != T_NIL) j = (j + 1) & m; nn[j] = old[i]; t->hused++; }
        if (old) heap.release(old, sizeof(Node) * oc);
    }
    Value tget(Table* t, const Value& k) {
        if (k.t == T_NUM) { double d = k.n; if (d >= 1 && d <= (double)t->asz) { u32 i = (u32)d; if ((double)i == d) return t->arr[i - 1]; } }
        else if (k.t == T_NIL) return Value();
        Node* n = hfind(t, k); return n ? n->v : Value();
    }
    Value tgetStr(Table* t, Str* k) { Value v = Value::str(k); Node* n = hfind(t, v); return n ? n->v : Value(); }
    Value tgetS(Table* t, const char* name) { Str* k = internPerm(name, slen(name)); return k ? tgetStr(t, k) : Value(); }
    void tsetS(Table* t, const char* name, const Value& v) { Str* k = internPerm(name, slen(name)); if (k) tsetRaw(t, Value::str(k), v); }
    void tsetRaw(Table* t, const Value& k, const Value& v) {
        if (k.t == T_NIL) { setError(0, 0, "table index is nil"); return; } if (k.t == T_NUM && k.n != k.n) { setError(0, 0, "table index is NaN"); return; }
        if (k.t == T_NUM) {
            double d = k.n; u32 i = (u32)d;
            if (d >= 1 && d < 1e9 && (double)i == d) {
                if (i <= t->asz) { t->arr[i - 1] = v; if (v.t == T_NIL && i == t->asz) { while (t->asz > 0 && t->arr[t->asz - 1].t == T_NIL) t->asz--; } return; }
                if (i == t->asz + 1) {
                    if (v.t == T_NIL) { Node* n = hfind(t, k); if (n) n->v = Value(); return; }
                    if (t->asz >= t->acap) { u32 nc = t->acap ? t->acap * 2 : 4; Value* na = (Value*)heap.alloc(sizeof(Value) * nc); if (!na) { setFail(F_MEMORY, "out of memory (table too large)"); return; } for (u32 j = 0; j < t->asz; j++) na[j] = t->arr[j]; if (t->arr) heap.release(t->arr, sizeof(Value) * t->acap); t->arr = na; t->acap = (u32)(Heap::clsSize(Heap::cls(sizeof(Value) * nc)) / sizeof(Value)); }
                    t->arr[t->asz++] = v; Node* old = hfind(t, k); if (old) old->v = Value();
                    for (;;) { Value nk = Value::num((double)(t->asz + 1)); Node* n = hfind(t, nk); if (!n || n->v.t == T_NIL || t->asz >= t->acap) break; t->arr[t->asz++] = n->v; n->v = Value(); }   // migrate following keys out of the hash
                    return;
                }
            }
        }
        Node* n = hfind(t, k); if (n) { n->v = v; return; } if (v.t == T_NIL) return;
        if (t->hused + 1 > t->hcap * 3 / 4 || !t->hcap) { hashGrow(t, 1); if (fail) return; }
        u32 m = t->hcap - 1, i = hashVal(k) & m; while (t->hash[i].k.t != T_NIL) i = (i + 1) & m; t->hash[i].k = k; t->hash[i].v = v; t->hused++;
    }
    // metatable aware get/set
    Value index(const Value& o, const Value& k, int depthGuard = 0) {
        if (o.t == T_TAB) {
            Value v = tget(o.tb, k); if (v.t != T_NIL || !o.tb->meta) return v;
            Value h = tgetS(o.tb->meta, "__index"); if (h.t == T_NIL) return v; if (depthGuard > 20) { setError(0, 0, "'__index' chain too long"); return Value(); }
            if (h.t == T_FUN || h.t == T_NAT) { Value args[2] = { o, k }; return call1(h, args, 2); } return index(h, k, depthGuard + 1);
        }
        if (o.t == T_STR && strmeta) { Value h = tgetS(strmeta, "__index"); if (h.t == T_TAB) return tget(h.tb, k); }
        setErrorf(0, 0, "attempt to index a %s value", typeName(o)); return Value();
    }
    void setindex(const Value& o, const Value& k, const Value& v, int depthGuard = 0) {
        if (o.t != T_TAB) { setErrorf(0, 0, "attempt to index a %s value", typeName(o)); return; }
        if (o.tb->meta) { Value cur = tget(o.tb, k); if (cur.t == T_NIL) { Value h = tgetS(o.tb->meta, "__newindex"); if (h.t != T_NIL) { if (h.t == T_FUN || h.t == T_NAT) { Value args[3] = { o, k, v }; call1(h, args, 3); return; } if (depthGuard > 20) { setError(0, 0, "'__newindex' chain too long"); return; } setindex(h, k, v, depthGuard + 1); return; } } }
        tsetRaw(o.tb, k, v);
    }
    u32 tlen(Table* t) { return t->asz; }
    bool tnext(Table* t, Value& key, Value& val) {     // iteration: array part, then hash part
        u32 i = 0;
        if (key.t == T_NIL) i = 0;
        else if (key.t == T_NUM && key.n >= 1 && key.n <= (double)t->asz && (double)(u32)key.n == key.n) i = (u32)key.n;
        else { Node* n = hfind(t, key); if (!n) { setError(0, 0, "invalid key to 'next'"); return false; } i = t->asz + (u32)(n - t->hash) + 1; }
        for (; i < t->asz; i++) if (t->arr[i].t != T_NIL) { key = Value::num((double)(i + 1)); val = t->arr[i]; return true; }
        for (u32 h = i - t->asz; h < t->hcap; h++) if (t->hash[h].k.t != T_NIL && t->hash[h].v.t != T_NIL) { key = t->hash[h].k; val = t->hash[h].v; return true; }
        key = Value(); return false;
    }
    // ---------------------------------------------------------- errors
    static const char* typeName(const Value& v) { switch (v.t) { case T_NIL: return "nil"; case T_BOOL: return "boolean"; case T_NUM: return "number"; case T_STR: return "string"; case T_TAB: return "table"; default: return "function"; } }
    void setFail(Fail f, const char* msg) { if (fail == F_NONE || fail == F_ERROR) { fail = f; int n = 0; while (msg[n] && n < 280) { errmsg[n] = msg[n]; n++; } errmsg[n] = 0; } }
    int curLine = 0, curCol = 0;
    void setError(int line, int col, const char* msg) {
        if (fail != F_NONE) return; fail = F_ERROR; errHasVal = false; if (!line) { line = curLine; col = curCol; } errLine = line; errCol = col;
        int n = 0; const char* c = chunk; while (*c && n < 40) errmsg[n++] = *c++; errmsg[n++] = ':'; char nb[24]; int ln = 0, v = line; if (v == 0) nb[ln++] = '0'; while (v) { nb[ln++] = (char)('0' + v % 10); v /= 10; } while (ln) errmsg[n++] = nb[--ln]; errmsg[n++] = ':'; errmsg[n++] = ' ';
        while (*msg && n < 295) errmsg[n++] = *msg++; errmsg[n] = 0; errval = Value(); }
    void setErrorf(int line, int col, const char* fmt, const char* a) { char buf[200]; int n = 0; while (*fmt && n < 190) { if (fmt[0] == '%' && fmt[1] == 's') { const char* p = a; while (*p && n < 190) buf[n++] = *p++; fmt += 2; } else buf[n++] = *fmt++; } buf[n] = 0; setError(line, col, buf); }
    void setBudgetFail() { if (fail == F_NONE) { fail = F_BUDGET; const char* m = "script ran out of execution time (possible endless loop)"; int n = 0; while (m[n]) { errmsg[n] = m[n]; n++; } errmsg[n] = 0; errLine = curLine; errCol = curCol; } }

    // ---------------------------------------------------------- strings / conversion
    bool toNumber(const Value& v, double& out) { if (v.t == T_NUM) { out = v.n; return true; } if (v.t == T_STR) return parse_num(v.s->data, v.s->len, &out); return false; }
    Str* tostr(const Value& v) {
        char b[64]; switch (v.t) {
            case T_NIL: return newStr("nil", 3); case T_BOOL: return v.b ? newStr("true", 4) : newStr("false", 5); case T_STR: return v.s;
            case T_NUM: { int n = fmt_num(b, 64, v.n); return newStr(b, (size_t)n); }
            default: { const char* tn = v.t == T_TAB ? "table: 0x" : "function: 0x"; int n = 0; while (tn[n]) { b[n] = tn[n]; n++; } size_t id = ((size_t)v.o - (size_t)heap.base) >> 4; char hx[20]; int hn = 0; if (!id) hx[hn++] = '0'; while (id) { hx[hn++] = "0123456789abcdef"[id & 15]; id >>= 4; } while (hn) b[n++] = hx[--hn]; return newStr(b, (size_t)n); }
        }
    }
    // ---------------------------------------------------------- calls
    Value call1(const Value& fn, Value* args, int nargs) { int n = callv(fn, args, nargs); return n > 0 ? ret[0] : Value(); }
    int callv(const Value& fn, Value* args, int nargs);                 // copies args to the VM stack; results in ret[0..nret)
    int callFrame(const Value& fn, Value* argsOnStack, int nargs);     // args already at sp
    // ---------------------------------------------------------- GC
    void markVal(const Value& v);
    void markObj(Obj* o);
    void gcRun();
    void gcMaybe() { if (allowGC && depth <= 1 && (heap.used > gcThreshold)) gcRun(); }
    // ---------------------------------------------------------- compile / run (implemented in lx_interp.hpp)
    bool compile(const char* src, size_t len, Value* outFn);
    // registry refs for host-held objects
    int ref(const Value& v) { Value k = Value::num(0); for (u32 i = 1;; i++) { if (i > registry->asz) { tsetRaw(registry, Value::num((double)i), v); return (int)i; } if (registry->arr[i - 1].t == T_NIL) { registry->arr[i - 1] = v; return (int)i; } } (void)k; }
    void unref(int r) { if (r >= 1 && (u32)r <= registry->asz) registry->arr[r - 1] = Value(); }
    Value getref(int r) { return (r >= 1 && (u32)r <= registry->asz) ? registry->arr[r - 1] : Value(); }
};
}  // namespace lx
