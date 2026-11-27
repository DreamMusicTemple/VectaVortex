// Minimal JSON DOM (parse only + lookups). Used for preset load on the non-realtime thread - never in render.
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace vv {
struct Json;
std::string jsonDump(const Json& j);
struct Json {
  enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
  bool b = false; double n = 0; std::string s;
  std::vector<Json> a;
  std::vector<std::pair<std::string, Json>> o;
  bool isNull() const { return type == Null; }
  bool isObj() const { return type == Obj; }
  bool isArr() const { return type == Arr; }
  bool isNum() const { return type == Num; }
  bool isStr() const { return type == Str; }
  const Json* get(const char* k) const { if (type != Obj) return nullptr; for (auto& kv : o) if (kv.first == k) return &kv.second; return nullptr; }
  bool has(const char* k) const { return get(k) != nullptr; }
  const Json& at(size_t i) const { static Json nul; return (type == Arr && i < a.size()) ? a[i] : nul; }
  size_t size() const { return type == Arr ? a.size() : 0; }
  double num(const char* k, double d) const { auto p = get(k); if (!p) return d; if (p->type == Num) return p->n; if (p->type == Bool) return p->b ? 1 : 0; return d; }
  bool boolean(const char* k, bool d) const { auto p = get(k); if (!p) return d; if (p->type == Bool) return p->b; if (p->type == Num) return p->n != 0; return d; }
  std::string str(const char* k, const std::string& d = "") const { auto p = get(k); return (p && p->type == Str) ? p->s : d; }
  double asNum(double d = 0) const { return type == Num ? n : (type == Bool ? (b ? 1 : 0) : d); }
};
class JsonParser {
  const char* p; const char* e;
  void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }
  [[noreturn]] void fail(const char* m) { throw std::runtime_error(std::string("json: ") + m); }
  static void utf8(std::string& s, unsigned c) {
    if (c < 0x80) s += (char)c; else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 63)); }
    else if (c < 0x10000) { s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 63)); s += (char)(0x80 | (c & 63)); }
    else { s += (char)(0xF0 | (c >> 18)); s += (char)(0x80 | ((c >> 12) & 63)); s += (char)(0x80 | ((c >> 6) & 63)); s += (char)(0x80 | (c & 63)); }
  }
  std::string parseString() {
    ++p; std::string r;
    while (p < e && *p != '"') {
      if (*p == '\\') { ++p; if (p >= e) fail("eof");
        switch (*p) { case 'n': r += '\n'; break; case 't': r += '\t'; break; case 'r': r += '\r'; break; case 'b': r += '\b'; break; case 'f': r += '\f'; break;
          case 'u': { unsigned c = (unsigned)strtoul(std::string(p + 1, p + 5).c_str(), nullptr, 16); p += 4;
            if (c >= 0xD800 && c < 0xDC00 && p + 6 < e && p[1] == '\\' && p[2] == 'u') { unsigned d = (unsigned)strtoul(std::string(p + 3, p + 7).c_str(), nullptr, 16); c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); p += 6; }
            utf8(r, c); break; }
          default: r += *p; }
        ++p;
      } else r += *p++;
    }
    if (p >= e) fail("unterminated string"); ++p; return r;
  }
  Json value(int depth) {
    if (depth > 200) fail("too deep"); ws(); if (p >= e) fail("eof"); Json j;
    char c = *p;
    if (c == '{') { j.type = Json::Obj; ++p; ws(); if (p < e && *p == '}') { ++p; return j; }
      for (;;) { ws(); if (p >= e || *p != '"') fail("key"); std::string k = parseString(); ws(); if (p >= e || *p != ':') fail(":"); ++p; j.o.emplace_back(std::move(k), value(depth + 1)); ws();
        if (p < e && *p == ',') { ++p; continue; } if (p < e && *p == '}') { ++p; break; } fail("obj"); } }
    else if (c == '[') { j.type = Json::Arr; ++p; ws(); if (p < e && *p == ']') { ++p; return j; }
      for (;;) { j.a.push_back(value(depth + 1)); ws(); if (p < e && *p == ',') { ++p; continue; } if (p < e && *p == ']') { ++p; break; } fail("arr"); } }
    else if (c == '"') { j.type = Json::Str; j.s = parseString(); }
    else if (e - p >= 4 && !strncmp(p, "true", 4)) { j.type = Json::Bool; j.b = true; p += 4; }
    else if (e - p >= 5 && !strncmp(p, "false", 5)) { j.type = Json::Bool; j.b = false; p += 5; }
    else if (e - p >= 4 && !strncmp(p, "null", 4)) { p += 4; }
    else { char* endp = nullptr; j.n = strtod(p, &endp); if (endp == p) fail("value"); j.type = Json::Num; p = endp; }
    return j;
  }
public:
  static Json parse(const std::string& s) { JsonParser jp; jp.p = s.data(); jp.e = s.data() + s.size(); Json j = jp.value(0); jp.ws(); return j; }
};

// ---- serialisation and path edits (non-realtime: used for preset export and live parameter edits)
inline void jsonEscape(std::string& out, const std::string& s) {
  out += '"';
  for (unsigned char c : s) { switch (c) { case '"': out += "\\\""; break; case '\\': out += "\\\\"; break; case '\n': out += "\\n"; break; case '\r': out += "\\r"; break; case '\t': out += "\\t"; break;
    default: if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; } else out += (char)c; } }
  out += '"';
}
inline void jsonDumpTo(std::string& out, const Json& j) {
  switch (j.type) {
    case Json::Null: out += "null"; break;
    case Json::Bool: out += j.b ? "true" : "false"; break;
    case Json::Num: { char b[40]; if (j.n == (double)(long long)j.n && j.n < 1e15 && j.n > -1e15) snprintf(b, sizeof b, "%lld", (long long)j.n); else snprintf(b, sizeof b, "%.17g", j.n); out += b; break; }
    case Json::Str: jsonEscape(out, j.s); break;
    case Json::Arr: out += '['; for (size_t i = 0; i < j.a.size(); i++) { if (i) out += ','; jsonDumpTo(out, j.a[i]); } out += ']'; break;
    case Json::Obj: out += '{'; for (size_t i = 0; i < j.o.size(); i++) { if (i) out += ','; jsonEscape(out, j.o[i].first); out += ':'; jsonDumpTo(out, j.o[i].second); } out += '}'; break;
  }
}
inline std::string jsonDump(const Json& j) { std::string s; jsonDumpTo(s, j); return s; }
// Structural equality without building strings (used on the audio thread instead of comparing jsonDump() texts). Object members compare in order, like the dump did.
inline bool jsonEqual(const Json& x, const Json& y) {
  if (x.type != y.type) return false;
  switch (x.type) {
    case Json::Null: return true;
    case Json::Bool: return x.b == y.b;
    case Json::Num: return x.n == y.n || (x.n != x.n && y.n != y.n);
    case Json::Str: return x.s == y.s;
    case Json::Arr: { if (x.a.size() != y.a.size()) return false; for (size_t i = 0; i < x.a.size(); i++) if (!jsonEqual(x.a[i], y.a[i])) return false; return true; }
    case Json::Obj: { if (x.o.size() != y.o.size()) return false; for (size_t i = 0; i < x.o.size(); i++) if (x.o[i].first != y.o[i].first || !jsonEqual(x.o[i].second, y.o[i].second)) return false; return true; }
  }
  return false;
}
inline bool jsonIsIndex(const std::string& k) { return !k.empty() && k.find_first_not_of("0123456789") == std::string::npos; }
// Dot path ("layers.WT.oscs.0.level"); missing containers are created (object, or array when the next key is numeric).
inline Json* jsonPathPtr(Json& root, const std::string& path, bool create) {
  Json* cur = &root; size_t i = 0;
  while (i <= path.size()) {
    size_t d = path.find('.', i); if (d == std::string::npos) d = path.size();
    const std::string key = path.substr(i, d - i); const bool nextIdx = d < path.size() && jsonIsIndex(path.substr(d + 1, path.find('.', d + 1) - d - 1));
    if (jsonIsIndex(key)) {
      const size_t idx = (size_t)strtoul(key.c_str(), nullptr, 10);
      if (cur->type != Json::Arr) { if (!create) return nullptr; *cur = Json(); cur->type = Json::Arr; }
      if (idx >= cur->a.size()) { if (!create || idx > 4096) return nullptr; cur->a.resize(idx + 1); }
      cur = &cur->a[idx];
    } else {
      if (cur->type != Json::Obj) { if (!create) return nullptr; *cur = Json(); cur->type = Json::Obj; }
      Json* found = nullptr; for (auto& kv : cur->o) if (kv.first == key) { found = &kv.second; break; }
      if (!found) { if (!create) return nullptr; cur->o.emplace_back(key, Json()); found = &cur->o.back().second; }
      cur = found;
    }
    (void)nextIdx; i = d + 1; if (d == path.size()) break;
  }
  return cur;
}
}
