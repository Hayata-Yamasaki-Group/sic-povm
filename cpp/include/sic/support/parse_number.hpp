// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#include "sic/error.hpp"

namespace sic::support {

// Full-string, locale-independent conversion with inclusive bounds. No whitespace
// or leading '+'; unsigned types also refuse '-'. Signed types allow '-'.
// Floating values use decimal general format and must be finite. Integer bases
// are 2..36, without prefixes. Canonical spelling/tokenization belongs to callers.
// Choose invalid_argument for options and invalid_format for file/wire records.
template <class T>
T parse_number(std::string_view text, std::string_view name, ErrorKind kind,
               T low = std::numeric_limits<T>::lowest(), T high = std::numeric_limits<T>::max(),
               int base = 10) {
  static_assert((std::is_integral_v<T> && !std::is_same_v<T, bool>) || std::is_floating_point_v<T>);
  auto fail = [&]() -> void {
    throw Error(kind, std::string(name) + ": invalid number or out of range: " + std::string(text));
  };
  if (text.empty() || !(low <= high)) fail();
  T value{};
  std::from_chars_result result;
  if constexpr (std::is_integral_v<T>) {
    if (base < 2 || base > 36) fail();
    result = std::from_chars(text.data(), text.data() + text.size(), value, base);
  } else {
    result = std::from_chars(text.data(), text.data() + text.size(), value);
  }
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) fail();
  if constexpr (std::is_floating_point_v<T>) {
    if (!std::isfinite(value)) fail();
  }
  if (value < low || value > high) fail();
  return value;
}

}  // namespace sic::support
