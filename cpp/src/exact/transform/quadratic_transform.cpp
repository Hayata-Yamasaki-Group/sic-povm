// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/transform/quadratic_transform.hpp"

namespace sic::exact {
std::uint64_t reverse_low_bits(std::uint64_t value, int bits) {
  if (!bits) return value;
  value = ((value >> 1) & 0x5555555555555555ULL) | ((value & 0x5555555555555555ULL) << 1);
  value = ((value >> 2) & 0x3333333333333333ULL) | ((value & 0x3333333333333333ULL) << 2);
  value = ((value >> 4) & 0x0f0f0f0f0f0f0f0fULL) | ((value & 0x0f0f0f0f0f0f0f0fULL) << 4);
  value = ((value >> 8) & 0x00ff00ff00ff00ffULL) | ((value & 0x00ff00ff00ff00ffULL) << 8);
  value = ((value >> 16) & 0x0000ffff0000ffffULL) | ((value & 0x0000ffff0000ffffULL) << 16);
  value = (value >> 32) | (value << 32);
  return value >> (64 - bits);
}

}  // namespace sic::exact
