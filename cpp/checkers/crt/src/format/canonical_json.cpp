// SPDX-License-Identifier: AGPL-3.0-or-later
#include "canonical_json.hpp"

#include "../runtime/diagnostics.hpp"
#include "container.hpp"
#include "integer_codec.hpp"
#include "metadata.hpp"
namespace sic_check::crt::sicx {
auto Jv::get(const string& k) const -> const Jv* {
  for (auto& kv : o)
    if (kv.first == k) return &kv.second;
  return nullptr;
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
    if (++depth > 32) resource("JSON: nesting deeper than 32");
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
    if (++depth > 32) resource("JSON: nesting deeper than 32");
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
  reject("JSON: only strings, arrays and objects are allowed");
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
  for (size_t i = 0; i < s.size();) {
    Uint8 a = (Uint8)s[i++];
    if (a < 128) continue;
    unsigned n = 0, v = 0, minimum = 0;
    if (a >= 0xc2 && a <= 0xdf) {
      n = 1;
      v = a & 31;
      minimum = 128;
    } else if (a >= 0xe0 && a <= 0xef) {
      n = 2;
      v = a & 15;
      minimum = 2048;
    } else if (a >= 0xf0 && a <= 0xf4) {
      n = 3;
      v = a & 7;
      minimum = 65536;
    } else
      reject("JSON: invalid UTF-8 lead");
    if (n > s.size() - i) reject("JSON: truncated UTF-8");
    for (unsigned j = 0; j < n; ++j) {
      Uint8 b = (Uint8)s[i++];
      if ((b & 192) != 128) reject("JSON: invalid UTF-8 continuation");
      v = (v << 6) | (b & 63);
    }
    if (v < minimum || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff))
      reject("JSON: invalid UTF-8 scalar");
  }
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
  JParser P;
  Jv v = P.parse_top(b, n);
  string r;
  ser(r, v);
  if (r.size() != n || memcmp(r.data(), b, n) != 0) reject(string(what) + ": not canonical JSON");
  return v;
}

bool is_uint(const string& s) {
  if (s.empty() || s.size() > 18) return false;
  if (s == "0") return true;
  if (s[0] < '1' || s[0] > '9') return false;
  for (char c : s)
    if (c < '0' || c > '9') return false;
  return true;
}

Uint64 to_u64(const string& s, const char* what) {
  if (!is_uint(s))
    reject(string(what) + ": not a canonical decimal string of at most 18 digits: " + s);
  return strtoull(s.c_str(), nullptr, 10);
}

const Jv& j_get(const Jv& o, const char* k, char t) {
  if (o.t != 'o') reject(string("object expected around ") + k);
  const Jv* v = o.get(k);
  if (!v) reject(string("missing key ") + k);
  if (v->t != t) reject(string("wrong JSON type for ") + k);
  return *v;
}

void j_keys(const Jv& o, std::initializer_list<const char*> keys, const char* what) {
  if (o.t != 'o') reject(string(what) + ": object expected");
  std::set<string> want(keys.begin(), keys.end());
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

Int64 signed_decimal(const string& s, const char* what) {
  bool neg = !s.empty() && s[0] == '-';
  string a = neg ? s.substr(1) : s;
  Uint64 n = to_u64(a, what);
  if (neg && !n) reject("negative decimal zero");
  return neg ? -(Int64)n : (Int64)n;
}
}  // namespace sic_check::crt::sicx
