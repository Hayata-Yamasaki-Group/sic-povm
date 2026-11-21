// SPDX-License-Identifier: AGPL-3.0-or-later
#include "canonical_json.hpp"

#include "../runtime/diagnostics.hpp"
#include "ball_codec.hpp"
#include "container.hpp"
#include "integer_codec.hpp"
#include "metadata.hpp"
#include "sha256.hpp"
namespace sic_check::verifier {
bool utf8_ok(const string& s) {
  size_t i = 0, n = s.size();
  while (i < n) {
    Uint8 c = s[i];
    int k;
    uint32_t cp;
    if (c < 0x80) {
      i++;
      continue;
    } else if ((c & 0xe0) == 0xc0) {
      k = 1;
      cp = c & 0x1f;
    } else if ((c & 0xf0) == 0xe0) {
      k = 2;
      cp = c & 0x0f;
    } else if ((c & 0xf8) == 0xf0) {
      k = 3;
      cp = c & 0x07;
    } else
      return false;
    if (i + k >= n + 0 && i + k > n - 0) return false;
    if (i + k >= n + 1) return false;
    for (int j = 1; j <= k; j++) {
      if (i + j >= n || ((Uint8)s[i + j] & 0xc0) != 0x80) return false;
      cp = cp << 6 | ((Uint8)s[i + j] & 0x3f);
    }
    if ((k == 1 && cp < 0x80) || (k == 2 && cp < 0x800) || (k == 3 && cp < 0x10000) ||
        cp > 0x10ffff || (cp >= 0xd800 && cp < 0xe000))
      return false;
    i += k + 1;
  }
  return true;
}

auto JParser::parse_top(const Uint8* b, size_t n) -> Jv {
  p = b;
  e = b + n;
  Jv v = value();
  if (p != e) reject("JSON: trailing bytes");
  return v;
}

auto JParser::value() -> Jv {
  if (p >= e) reject("JSON: unexpected end");
  if (*p == '"') {
    Jv v;
    v.t = 's';
    v.s = str();
    return v;
  }
  if (*p == '[') {
    if (++depth > 32) throw PrecErr("JSON: nesting deeper than 32");
    p++;
    Jv v;
    v.t = 'a';
    if (p < e && *p == ']') {
      p++;
      depth--;
      return v;
    }
    for (;;) {
      v.a.push_back(value());
      if (p >= e) reject("JSON: unterminated array");
      if (*p == ',') {
        p++;
        continue;
      }
      if (*p == ']') {
        p++;
        break;
      }
      reject("JSON: bad array");
    }
    depth--;
    return v;
  }
  if (*p == '{') {
    if (++depth > 32) throw PrecErr("JSON: nesting deeper than 32");
    p++;
    Jv v;
    v.t = 'o';
    if (p < e && *p == '}') {
      p++;
      depth--;
      return v;
    }
    for (;;) {
      if (p >= e || *p != '"') reject("JSON: object key expected");
      string k = str();
      if (!v.o.empty() && !(v.o.back().first < k))
        reject("JSON: object keys not strictly increasing");
      if (p >= e || *p != ':') reject("JSON: ':' expected");
      p++;
      v.o.emplace_back(k, value());
      if (p >= e) reject("JSON: unterminated object");
      if (*p == ',') {
        p++;
        continue;
      }
      if (*p == '}') {
        p++;
        break;
      }
      reject("JSON: bad object");
    }
    depth--;
    return v;
  }
  reject("JSON: bare number/literal or whitespace/garbage (only strings, arrays, objects allowed)");
}

auto JParser::str() -> string {
  p++;
  string s;
  for (;;) {
    if (p >= e) reject("JSON: unterminated string");
    Uint8 c = *p++;
    if (c == '"') break;
    if (c < 0x20) reject("JSON: raw control byte in string");
    if (c == '\\') {
      if (p >= e) reject("JSON: bad escape");
      Uint8 x = *p++;
      if (x == '"')
        s += '"';
      else if (x == '\\')
        s += '\\';
      else if (x == 'u') {
        if (e - p < 4 || p[0] != '0' || p[1] != '0') reject("JSON: \\u escape must be \\u00XX");
        auto hv = [](Uint8 ch) -> int {
          if (ch >= '0' && ch <= '9') return ch - '0';
          if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
          return -1;
        };
        int a = hv(p[2]), b = hv(p[3]);
        if (a < 0 || b < 0) reject("JSON: \\u hex must be lowercase");
        int v = a * 16 + b;
        if (v >= 0x20) reject("JSON: \\u escape for byte >= 0x20");
        s += (char)v;
        p += 4;
      } else
        reject("JSON: disallowed escape");
    } else
      s += (char)c;
  }
  if (!utf8_ok(s)) reject("JSON: invalid UTF-8");
  return s;
}

void ser_str(string& o, const string& s) {
  o += '"';
  for (Uint8 c : s) {
    if (c == '"')
      o += "\\\"";
    else if (c == '\\')
      o += "\\\\";
    else if (c < 0x20) {
      char b[8];
      snprintf(b, 8, "\\u00%02x", c);
      o += b;
    } else
      o += (char)c;
  }
  o += '"';
}

void ser(string& o, const Jv& v) {
  if (v.t == 's')
    ser_str(o, v.s);
  else if (v.t == 'a') {
    o += '[';
    for (size_t i = 0; i < v.a.size(); i++) {
      if (i) o += ',';
      ser(o, v.a[i]);
    }
    o += ']';
  } else {
    o += '{';
    for (size_t i = 0; i < v.o.size(); i++) {
      if (i) o += ',';
      ser_str(o, v.o[i].first);
      o += ':';
      ser(o, v.o[i].second);
    }
    o += '}';
  }
}

Jv parse_canonical(const Uint8* b, size_t n, const char* what) {
  if (!n) reject("empty JSON payload");
  JParser P;
  Jv v = P.parse_top(b, n);
  string r;
  ser(r, v);
  if (r.size() != n || memcmp(r.data(), b, n) != 0)
    reject(string(what) + ": not canonical (re-serialization differs)");
  return v;
}

bool is_uint(const string& s) {
  if (s.empty() || s.size() > 20) return false;
  if (s == "0") return true;
  if (s[0] < '1' || s[0] > '9') return false;
  for (char c : s)
    if (c < '0' || c > '9') return false;
  return true;
}

bool is_int(const string& s) {
  if (s.size() && s[0] == '-') return s.size() > 1 && s != "-0" && is_uint(s.substr(1));
  return is_uint(s);
}

bool is_bigdec(const string& s) {
  if (s.empty()) return false;
  if (s == "0") return true;
  if (s[0] < '1' || s[0] > '9') return false;
  for (char c : s)
    if (c < '0' || c > '9') return false;
  return true;
}

Uint64 to_u64(const string& s, const char* what) {
  if (s.size() > 18) reject(string(what) + ": more than 18 digits");
  if (!is_uint(s)) reject(string(what) + ": not a canonical decimal unsigned string: " + s);
  if (s.size() == 20 && s > "18446744073709551615") reject(string(what) + ": overflow");
  return strtoull(s.c_str(), nullptr, 10);
}

Int64 to_i64(const string& s, const char* what) {
  if (s.size() > 19 || (s.size() == 19 && s[0] != '-'))
    reject(string(what) + ": more than 18 digits");
  if (!is_int(s)) reject(string(what) + ": not a canonical decimal integer: " + s);
  if (s.size() > 19) reject(string(what) + ": integer too large");
  return strtoll(s.c_str(), nullptr, 10);
}

const Jv& j_get(const Jv& o, const char* k, char t) {
  if (o.t != 'o') reject(string("EXMT: object expected around ") + k);
  const Jv* v = o.get(k);
  if (!v) reject(string("EXMT: missing key ") + k);
  if (v->t != t) reject(string("EXMT: wrong JSON type for ") + k);
  return *v;
}

void j_keys(const Jv& o, std::initializer_list<const char*> keys, const char* what) {
  if (o.t != 'o') reject(string(what) + ": object expected");
  std::set<string> want;
  for (auto k : keys) want.insert(k);
  for (auto& kv : o.o)
    if (!want.count(kv.first)) reject(string(what) + ": unknown key " + kv.first);
  for (auto& k : want)
    if (!o.get(k)) reject(string(what) + ": missing key " + k);
}

vector<Uint64> j_uvec(const Jv& v, const char* what) {
  if (v.t != 'a') reject(string(what) + ": array expected");
  vector<Uint64> r;
  for (auto& x : v.a) {
    if (x.t != 's') reject(string(what) + ": string expected");
    r.push_back(to_u64(x.s, what));
  }
  return r;
}
}  // namespace sic_check::verifier
