// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "../format/container.hpp"

namespace sic_check::crt {
struct UsageError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

inline uint64_t number(std::string_view value, uint64_t minimum, uint64_t maximum) {
  uint64_t result = 0;
  auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, 10);
  if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
      result < minimum || result > maximum)
    throw UsageError("expected unsigned decimal in " + std::to_string(minimum) + ".." +
                     std::to_string(maximum));
  return result;
}
inline std::string trusted_hash(const std::string& value) {
  try {
    sicx::parse_hash(value);
  } catch (const sicx::Reject& e) {
    throw UsageError(e.what());
  }
  return value;
}
}  // namespace sic_check::crt
