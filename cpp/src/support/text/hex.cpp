// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/hex.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sic::support {
namespace {
constexpr char kHexDigits[] = "0123456789abcdef";
}
std::string hex_encode(std::span<const uint8_t> bytes) {
  if (bytes.size() > std::string{}.max_size() / 2)
    throw std::length_error("hex encoding size exceeds string range");
  std::string result(2 * bytes.size(), '0');
  for (size_t i = 0; i < bytes.size(); ++i) {
    result[2 * i] = kHexDigits[bytes[i] >> 4];
    result[2 * i + 1] = kHexDigits[bytes[i] & 15];
  }
  return result;
}
std::string hex_unsigned(uint64_t value, unsigned minimum_digits) {
  std::string result;
  do {
    result.push_back(kHexDigits[value & 15]);
    value >>= 4;
  } while (value);
  result.resize(std::max<size_t>(result.size(), minimum_digits), '0');
  std::reverse(result.begin(), result.end());
  return result;
}
}  // namespace sic::support
