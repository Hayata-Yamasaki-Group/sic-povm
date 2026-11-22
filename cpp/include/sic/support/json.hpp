// SPDX-License-Identifier: AGPL-3.0-or-later
// Decimal-string and bare-integer JSON shared by production tools (no FLINT dependency).
#pragma once
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sic/error.hpp"

namespace sic::support {
struct JsonError : sic::Error {
  explicit JsonError(const std::string& message);
};
namespace json_detail {
// JSON syntax failures carry invalid_format through JsonError.
[[noreturn]] void fail(const std::string& message);
}  // namespace json_detail
// ------------------------------------------------------------ canonical JSON
// Canonical form: no whitespace, object keys strictly increasing in byte
// order, integers written as decimal strings, strings with
// exactly the escapes \" \\ and \u00XX (lowercase hex) for bytes below 0x20,
// valid UTF-8 otherwise. No floats, no null.
inline constexpr size_t kCanonicalDecimalDigits = 18;
struct Json {
  enum class Kind { boolean_value, integer_value, string, array, object } kind = Kind::object;
  bool boolean = false;
  int64_t integer = 0;
  std::string string;
  std::vector<Json> array;
  std::map<std::string, Json> object;

  Json() = default;
  // Store a signed integer. dump_json writes it as a decimal string;
  // dump_integer_json writes it as a bare number.
  static Json integer_value(int64_t value);
  static Json boolean_value(bool value);
  static Json string_value(std::string value);
  static Json array_value();
  static Json object_value() { return Json(); }
  // Build an array of integer values. Each element must fit in int64_t.
  template <class T>
  static Json integer_array(const std::vector<T>& values) {
    Json j = array_value();
    for (auto v : values) j.array.push_back(integer_value(static_cast<int64_t>(v)));
    return j;
  }
  Json& operator[](const std::string& key);
  const Json& at(const std::string& key) const;
  bool has(const std::string& key) const { return kind == Kind::object && object.contains(key); }
  // Accept stored integers and canonical decimal strings ("0", "-12").
  int64_t as_int() const;
  uint64_t as_uint() const;
  const std::string& as_str() const;
  const std::vector<Json>& as_array() const;
  template <class T>
  std::vector<T> as_uints() const {
    std::vector<T> result;
    for (const auto& v : as_array()) {
      const uint64_t x = v.as_uint();
      if (x > static_cast<uint64_t>(std::numeric_limits<T>::max()))
        json_detail::fail("JSON integer out of range");
      result.push_back(static_cast<T>(x));
    }
    return result;
  }
  void keys(std::initializer_list<const char*> required,
            std::initializer_list<const char*> optional = {}) const;
};

void dump_json(std::string& out, const Json& value);
std::string dump_json(const Json& value);
// For documents with bare signed 64-bit integers, in sorted-key compact form.
std::string dump_integer_json(const Json& value);

inline constexpr unsigned kDefaultJsonDepth = 32;
class JsonParser {
  unsigned maximum_depth_ = kDefaultJsonDepth;
  std::string_view text_;
  size_t at_ = 0;
  bool integer_document_ = false;
  [[noreturn]] void error(const char* what) const;
  char peek() const;
  char token();
  void expect(char c);
  std::string string();
  Json value(int depth);
  void skip_space();
  static Json parse_impl(std::string_view text, unsigned maximum_depth, bool integer_document);

 public:
  static Json parse(std::string_view text, unsigned maximum_depth = kDefaultJsonDepth);
  // Also accept JSON whitespace, unsorted unique keys and bare int64 numbers.
  // Bare integers reject leading zeros and "-0"; floats and null are unsupported.
  static Json parse_integer_document(std::string_view text,
                                     unsigned maximum_depth = kDefaultJsonDepth);
};

}  // namespace sic::support
