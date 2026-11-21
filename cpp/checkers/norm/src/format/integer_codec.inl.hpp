// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "../runtime/diagnostics.hpp"

namespace sic_check::norm {
inline uint64_t add_checked(uint64_t a, uint64_t b) {
  input_require(b <= UINT64_MAX - a, "addition overflow");
  return a + b;
}

inline uint64_t mul_checked(uint64_t a, uint64_t b) {
  input_require(!a || b <= UINT64_MAX / a, "multiplication overflow");
  return a * b;
}

}  // namespace sic_check::norm
