// SPDX-License-Identifier: AGPL-3.0-or-later
#include "json.hpp"

#include <fstream>
#include <stdexcept>
#include <string_view>

namespace sic_check::stabilizer {
namespace {
[[noreturn]] void invalid() { throw std::runtime_error("invalid checker JSON input"); }
// Release catalogues and numerical ray records use strings for every scalar.
// Whitespace is accepted; duplicate keys, control bytes, non-string scalars,
// trailing data, oversized files and nesting beyond 32 levels are refused.
class Parser {
  std::string_view input;
  size_t pos = 0;
  char next() {
    while (pos < input.size() &&
           (input[pos] == ' ' || input[pos] == '\n' || input[pos] == '\r' || input[pos] == '\t'))
      ++pos;
    return pos == input.size() ? '\0' : input[pos];
  }
  void take(char c) {
    if (next() != c) invalid();
    ++pos;
  }
  unsigned hex4() {
    unsigned value = 0;
    for (int i = 0; i < 4; ++i) {
      if (pos == input.size()) invalid();
      const char h = input[pos++];
      value *= 16;
      if (h >= '0' && h <= '9')
        value += h - '0';
      else if (h >= 'a' && h <= 'f')
        value += h - 'a' + 10;
      else if (h >= 'A' && h <= 'F')
        value += h - 'A' + 10;
      else
        invalid();
    }
    return value;
  }
  void unicode(std::string& result) {
    unsigned value = hex4();
    if (value >= 0xd800 && value <= 0xdbff) {
      if (input.size() - pos < 2 || input[pos++] != '\\' || input[pos++] != 'u') invalid();
      const unsigned low = hex4();
      if (low < 0xdc00 || low > 0xdfff) invalid();
      value = 0x10000 + ((value - 0xd800) << 10) + low - 0xdc00;
    } else if (value >= 0xdc00 && value <= 0xdfff)
      invalid();
    if (value <= 0x7f)
      result += char(value);
    else if (value <= 0x7ff) {
      result += char(0xc0 | (value >> 6));
      result += char(0x80 | (value & 63));
    } else if (value <= 0xffff) {
      result += char(0xe0 | (value >> 12));
      result += char(0x80 | ((value >> 6) & 63));
      result += char(0x80 | (value & 63));
    } else {
      result += char(0xf0 | (value >> 18));
      result += char(0x80 | ((value >> 12) & 63));
      result += char(0x80 | ((value >> 6) & 63));
      result += char(0x80 | (value & 63));
    }
  }
  void utf8(std::string& result, unsigned char first) {
    const unsigned count = first >= 0xc2 && first <= 0xdf   ? 1
                           : first >= 0xe0 && first <= 0xef ? 2
                           : first >= 0xf0 && first <= 0xf4 ? 3
                                                            : 0;
    if (count == 0 || input.size() - pos < count) invalid();
    unsigned value = first & (0x7f >> count);
    const auto begin = pos - 1;
    for (unsigned k = 0; k < count; ++k) {
      const unsigned char c = input[pos++];
      if ((c & 0xc0) != 0x80) invalid();
      value = (value << 6) | (c & 63);
    }
    if ((count == 1 && value < 0x80) || (count == 2 && value < 0x800) ||
        (count == 3 && value < 0x10000) || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
      invalid();
    result.append(input.substr(begin, count + 1));
  }
  std::string quoted() {
    take('"');
    std::string result;
    while (pos < input.size()) {
      unsigned char c = input[pos++];
      if (c == '"') return result;
      if (c < 32) invalid();
      if (c == '\\') {
        if (pos == input.size()) invalid();
        c = input[pos++];
        if (c == '"' || c == '\\' || c == '/')
          result += char(c);
        else if (c == 'n')
          result += '\n';
        else if (c == 'r')
          result += '\r';
        else if (c == 't')
          result += '\t';
        else if (c == 'b')
          result += '\b';
        else if (c == 'f')
          result += '\f';
        else if (c == 'u')
          unicode(result);
        else
          invalid();
      } else if (c >= 128)
        utf8(result, c);
      else
        result += char(c);
    }
    invalid();
  }
  Json value(unsigned depth) {
    if (depth > 32) invalid();
    Json result;
    if (next() == '"') {
      result.kind = Json::Kind::string;
      result.text = quoted();
    } else if (next() == '[') {
      result.kind = Json::Kind::array;
      take('[');
      if (next() != ']') {
        for (;;) {
          result.items.push_back(value(depth + 1));
          if (next() != ',') break;
          take(',');
        }
      }
      take(']');
    } else {
      take('{');
      if (next() != '}') {
        for (;;) {
          const auto key = quoted();
          take(':');
          if (!result.members.emplace(key, value(depth + 1)).second) invalid();
          if (next() != ',') break;
          take(',');
        }
      }
      take('}');
    }
    return result;
  }

 public:
  explicit Parser(std::string_view text) : input(text) {}
  Json parse() {
    auto result = value(0);
    next();
    if (pos != input.size()) invalid();
    return result;
  }
};
}  // namespace
const Json& Json::at(const std::string& key) const {
  if (kind != Kind::object) invalid();
  const auto it = members.find(key);
  if (it == members.end()) invalid();
  return it->second;
}
const std::string& Json::string() const {
  if (kind != Kind::string) invalid();
  return text;
}
const std::vector<Json>& Json::array() const {
  if (kind != Kind::array) invalid();
  return items;
}
Json read_json(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot read " + path);
  std::string text;
  for (char c; in.get(c);) {
    if (text.size() >= 16 * 1024 * 1024) invalid();
    text += c;
  }
  if (!in.eof()) invalid();
  try {
    return Parser(text).parse();
  } catch (const std::runtime_error& ex) {
    throw std::runtime_error(path + ": " + ex.what());
  }
}
}  // namespace sic_check::stabilizer
