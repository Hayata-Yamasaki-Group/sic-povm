// SPDX-License-Identifier: AGPL-3.0-or-later
#include "canonical_json.hpp"

#include "../runtime/diagnostics.hpp"
#include "container.hpp"
#include "integer_codec.hpp"
#include "metadata.hpp"
namespace sic_check::norm {
auto Json::at(const std::string& key) const -> const Json& {
  input_require(kind == object && fields.contains(key), "missing JSON key " + key);
  return fields.at(key);
}

auto Json::str() const -> const std::string& {
  input_require(kind == string, "expected JSON string");
  return text;
}

auto Json::items() const -> const std::vector<Json>& {
  input_require(kind == array, "expected JSON array");
  return list;
}

auto Json::keys(std::initializer_list<std::string> wanted) const -> void {
  input_require(kind == object && fields.size() == wanted.size(), "JSON key set");
  for (const auto& key : wanted) (void)at(key);
}

auto JsonParser::take() -> char {
  input_require(pos < data.size(), "truncated JSON");
  return data[pos++];
}

auto JsonParser::quoted() -> std::string {
  input_require(take() == '"', "JSON string opening");
  std::string s;
  for (;;) {
    unsigned char c = take();
    if (c == '"') break;
    input_require(c >= 32, "JSON control byte");
    if (c == '\\') {
      c = take();
      if (c != '"' && c != '\\') {
        input_require(c == 'u' && take() == '0' && take() == '0', "JSON escape");
        auto hex = [&]() {
          char h = take();
          input_require((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f'), "JSON escape hex");
          return h <= '9' ? h - '0' : h - 'a' + 10;
        };
        unsigned hi = hex(), lo = hex();
        c = uint8_t(16 * hi + lo);
        input_require(c < 32, "noncanonical JSON escape");
      }
    } else if (c >= 128) {
      // Reject overlong forms, surrogate code points and values above U+10FFFF.
      unsigned n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
      input_require(c >= 0xc2 && c <= 0xf4, "UTF-8 lead byte");
      uint32_t cp = c & ((1u << (6 - n)) - 1);
      s += char(c);
      for (unsigned j = 0; j < n; ++j) {
        unsigned char next = take();
        input_require((next & 0xc0) == 0x80, "UTF-8 continuation");
        cp = (cp << 6) | (next & 63);
        s += char(next);
      }
      input_require(cp >= (n == 1   ? 0x80u
                           : n == 2 ? 0x800u
                                    : 0x10000u) &&
                        cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff),
                    "UTF-8 scalar");
      continue;
    }
    s += char(c);
  }
  return s;
}

auto JsonParser::value(unsigned depth) -> Json {
  resource(depth <= 32 && ++nodes <= (1u << 20), "JSON depth/node count");
  input_require(pos < data.size(), "JSON truncation");
  Json j;
  if (data[pos] == '"') {
    j.text = quoted();
    return j;
  }
  char open = take();
  input_require(open == '[' || open == '{', "JSON value type");
  j.kind = open == '[' ? Json::array : Json::object;
  const char close = open == '[' ? ']' : '}';
  if (pos < data.size() && data[pos] == close) {
    ++pos;
    return j;
  }
  std::string previous;
  bool first = true;
  for (;;) {
    if (open == '[')
      j.list.push_back(value(depth + 1));
    else {
      std::string key = quoted();
      input_require(first || previous < key, "JSON key order/duplicate");
      previous = key;
      first = false;
      input_require(take() == ':', "JSON colon");
      j.fields.emplace(key, value(depth + 1));
    }
    char c = take();
    if (c == close) return j;
    input_require(c == ',', "JSON separator");
  }
}

JsonParser::JsonParser(const std::string& s) : data(s) {}

auto JsonParser::parse() -> Json {
  Json j = value(0);
  input_require(pos == data.size(), "JSON trailing bytes");
  return j;
}

int64_t number(const Json& j, int64_t low, int64_t high) {
  const auto& s = j.str();
  input_require(!s.empty() && s.size() <= 19, "decimal width");
  size_t start = s[0] == '-' ? 1 : 0;
  input_require(start < s.size() && s.size() - start <= 18 && (s[start] != '0' || s.size() == 1),
                "canonical decimal");
  for (size_t i = start; i < s.size(); ++i)
    input_require(s[i] >= '0' && s[i] <= '9', "decimal digit");
  int64_t out = 0;
  auto r = std::from_chars(s.data(), s.data() + s.size(), out);
  input_require(r.ec == std::errc{} && r.ptr == s.data() + s.size() && out >= low && out <= high,
                "decimal range");
  return out;
}
}  // namespace sic_check::norm
