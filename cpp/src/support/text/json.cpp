// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/json.hpp"

#include "sic/support/parse_number.hpp"
#include "support/hex.hpp"

namespace sic::support::json_detail {
[[noreturn]] void fail(const std::string& message) { throw JsonError("SICX: " + message); }
}  // namespace sic::support::json_detail

namespace sic::support {
Json Json::integer_value(int64_t value) {
  Json j;
  j.kind = Kind::integer_value;
  j.integer = value;
  return j;
}
}  // namespace sic::support

namespace sic::support {
Json Json::boolean_value(bool value) {
  Json j;
  j.kind = Kind::boolean_value;
  j.boolean = value;
  return j;
}
}  // namespace sic::support

namespace sic::support {
Json Json::string_value(std::string value) {
  Json j;
  j.kind = Kind::string;
  j.string = std::move(value);
  return j;
}
}  // namespace sic::support

namespace sic::support {
Json Json::array_value() {
  Json j;
  j.kind = Kind::array;
  return j;
}
}  // namespace sic::support

namespace sic::support {
Json& Json::operator[](const std::string& key) {
  if (kind != Kind::object) json_detail::fail("JSON value is not an object");
  return object[key];
}
}  // namespace sic::support

namespace sic::support {
const Json& Json::at(const std::string& key) const {
  if (kind != Kind::object) json_detail::fail("JSON value is not an object");
  const auto found = object.find(key);
  if (found == object.end()) json_detail::fail("missing JSON key " + key);
  return found->second;
}
}  // namespace sic::support

namespace sic::support {
int64_t Json::as_int() const {
  if (kind == Kind::integer_value) return integer;
  if (kind != Kind::string) json_detail::fail("JSON value is not a decimal string");
  const std::string& t = string;
  const size_t body = !t.empty() && t[0] == '-' ? 1 : 0;
  if (body == t.size() || t.size() - body > kCanonicalDecimalDigits ||
      t.find_first_not_of("0123456789", body) != std::string::npos ||
      (t.size() - body > 1 && t[body] == '0') || t == "-0")
    json_detail::fail("JSON string is not a canonical decimal integer: " + t);
  try {
    return parse_number<int64_t>(t, "JSON integer", ErrorKind::invalid_format);
  } catch (const Error& error) {
    throw JsonError(error.what());
  }
}
}  // namespace sic::support

namespace sic::support {
uint64_t Json::as_uint() const {
  const auto v = as_int();
  if (v < 0) json_detail::fail("JSON integer is negative");
  return static_cast<uint64_t>(v);
}
}  // namespace sic::support

namespace sic::support {
const std::string& Json::as_str() const {
  if (kind != Kind::string) json_detail::fail("JSON value is not a string");
  return string;
}
}  // namespace sic::support

namespace sic::support {
const std::vector<Json>& Json::as_array() const {
  if (kind != Kind::array) json_detail::fail("JSON value is not an array");
  return array;
}
}  // namespace sic::support

namespace sic::support {
void Json::keys(std::initializer_list<const char*> required,
                std::initializer_list<const char*> optional) const {
  if (kind != Kind::object) json_detail::fail("JSON value is not an object");
  size_t seen = 0;
  for (auto key : required) {
    if (!object.contains(key)) json_detail::fail(std::string("missing JSON key ") + key);
    ++seen;
  }
  for (auto key : optional) seen += object.contains(key);
  if (seen != object.size()) json_detail::fail("unknown JSON key in a fixed object");
}
}  // namespace sic::support

namespace sic::support {
namespace {
void dump(std::string& out, const Json& value, bool bare_integers) {
  switch (value.kind) {
    case Json::Kind::boolean_value:
      out += value.boolean ? "true" : "false";
      return;
    case Json::Kind::integer_value:
      if (!bare_integers) out.push_back('"');
      out += std::to_string(value.integer);
      if (!bare_integers) out.push_back('"');
      return;
    case Json::Kind::string: {
      out.push_back('"');
      for (unsigned char c : value.string) {
        if (c == '"' || c == '\\') {
          out.push_back('\\');
          out.push_back(static_cast<char>(c));
        } else if (c < 0x20) {
          out += "\\u00";
          out += sic::support::hex_unsigned(c, 2);
        } else
          out.push_back(static_cast<char>(c));
      }
      out.push_back('"');
      return;
    }
    case Json::Kind::array:
      out.push_back('[');
      for (size_t i = 0; i < value.array.size(); ++i) {
        if (i) out.push_back(',');
        dump(out, value.array[i], bare_integers);
      }
      out.push_back(']');
      return;
    case Json::Kind::object: {
      out.push_back('{');
      bool first = true;
      for (const auto& [key, item] : value.object) {
        if (!first) out.push_back(',');
        first = false;
        dump(out, Json::string_value(key), bare_integers);
        out.push_back(':');
        dump(out, item, bare_integers);
      }
      out.push_back('}');
      return;
    }
  }
}
}  // namespace

void dump_json(std::string& out, const Json& value) { dump(out, value, false); }

std::string dump_integer_json(const Json& value) {
  std::string out;
  dump(out, value, true);
  return out;
}
}  // namespace sic::support

namespace sic::support {
std::string dump_json(const Json& value) {
  std::string out;
  dump_json(out, value);
  return out;
}
}  // namespace sic::support

namespace sic::support {
[[noreturn]] void JsonParser::error(const char* what) const {
  json_detail::fail(std::string("non-canonical JSON: ") + what + " at byte " + std::to_string(at_));
}
}  // namespace sic::support

namespace sic::support {
char JsonParser::peek() const {
  if (at_ >= text_.size()) error("truncated");
  return text_[at_];
}
}  // namespace sic::support

namespace sic::support {
void JsonParser::skip_space() {
  if (!integer_document_) return;
  while (at_ < text_.size() && std::string_view(" \t\r\n").find(text_[at_]) != text_.npos) ++at_;
}

char JsonParser::token() {
  skip_space();
  return peek();
}

void JsonParser::expect(char c) {
  if (token() != c) error("unexpected character");
  ++at_;
}
}  // namespace sic::support

namespace sic::support {
std::string JsonParser::string() {
  expect('"');
  std::string out;
  for (;;) {
    const unsigned char c = static_cast<unsigned char>(peek());
    ++at_;
    if (c == '"') break;
    if (c < 0x20) error("raw control byte");
    if (c == '\\') {
      const char e = peek();
      ++at_;
      if (e == '"' || e == '\\')
        out.push_back(e);
      else if (e == 'u') {
        if (at_ + 4 > text_.size() || text_.substr(at_, 2) != "00") error("escape");
        int v = 0;
        for (int k = 2; k < 4; ++k) {
          const char h = text_[at_ + k];
          if (h >= '0' && h <= '9')
            v = 16 * v + (h - '0');
          else if (h >= 'a' && h <= 'f')
            v = 16 * v + (h - 'a' + 10);
          else
            error("escape digit");
        }
        if (v >= 0x20) error("unnecessary escape");
        at_ += 4;
        out.push_back(static_cast<char>(v));
      } else
        error("escape");
      continue;
    }
    if (c >= 0x80) {  // UTF-8 validation (no overlongs, no surrogates)
      int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : -1;
      if (extra < 0 || c > 0xf4 || c == 0xc0 || c == 0xc1) error("invalid UTF-8");
      uint32_t code = c & (0x3f >> extra);
      out.push_back(static_cast<char>(c));
      for (int k = 0; k < extra; ++k) {
        const unsigned char t = static_cast<unsigned char>(peek());
        ++at_;
        if ((t & 0xc0) != 0x80) error("invalid UTF-8");
        code = (code << 6) | (t & 0x3f);
        out.push_back(static_cast<char>(t));
      }
      if ((extra == 2 && (code < 0x800 || (code >= 0xd800 && code < 0xe000))) ||
          (extra == 3 && (code < 0x10000 || code > 0x10ffff)))
        error("invalid UTF-8");
      continue;
    }
    out.push_back(static_cast<char>(c));
  }
  return out;
}
}  // namespace sic::support

namespace sic::support {
Json JsonParser::value(int depth) {
  if (static_cast<unsigned>(depth) > maximum_depth_) error("nesting");
  const char c = token();
  if (c == '{') {
    ++at_;
    Json result = Json::object_value();
    if (token() == '}') {
      ++at_;
      return result;
    }
    std::string last;
    for (bool first = true;; first = false) {
      std::string key = string();
      if (!integer_document_ && !first && !(last < key)) error("keys not strictly increasing");
      expect(':');
      if (!result.object.emplace(key, value(depth + 1)).second) error("duplicate key");
      last = std::move(key);
      if (token() == ',') {
        ++at_;
        continue;
      }
      expect('}');
      return result;
    }
  }
  if (c == '[') {
    ++at_;
    Json result = Json::array_value();
    if (token() == ']') {
      ++at_;
      return result;
    }
    for (;;) {
      result.array.push_back(value(depth + 1));
      if (token() == ',') {
        ++at_;
        continue;
      }
      expect(']');
      return result;
    }
  }
  if (c == '"') return Json::string_value(string());
  if (integer_document_ && (c == '-' || (c >= '0' && c <= '9'))) {
    const size_t start = at_++;
    while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') ++at_;
    const auto literal = text_.substr(start, at_ - start);
    const auto digits = c == '-' ? literal.substr(1) : literal;
    if (digits.empty() || (digits.front() == '0' && literal.size() != 1))
      error("invalid integer spelling");
    return Json::integer_value(
        parse_number<int64_t>(literal, "JSON integer", ErrorKind::invalid_format));
  }
  error("bare JSON number or literal (every number is a decimal string)");
}
}  // namespace sic::support

namespace sic::support {
Json JsonParser::parse(std::string_view text, unsigned maximum_depth) {
  return parse_impl(text, maximum_depth, false);
}

Json JsonParser::parse_integer_document(std::string_view text, unsigned maximum_depth) {
  return parse_impl(text, maximum_depth, true);
}

Json JsonParser::parse_impl(std::string_view text, unsigned maximum_depth, bool integer_document) {
  JsonParser parser;
  if (maximum_depth > 1024)
    throw sic::Error(ErrorKind::invalid_argument, "JSON depth exceeds native recursion profile");
  parser.maximum_depth_ = maximum_depth;
  parser.text_ = text;
  parser.integer_document_ = integer_document;
  Json result = parser.value(0);
  parser.skip_space();
  if (parser.at_ != text.size()) parser.error("trailing bytes");
  if (!integer_document && dump_json(result) != text) parser.error("re-serialization differs");
  return result;
}
}  // namespace sic::support

namespace sic::support {
JsonError::JsonError(const std::string& message) : sic::Error(ErrorKind::invalid_format, message) {}
}  // namespace sic::support
