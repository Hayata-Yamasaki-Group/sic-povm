// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdint>
#include <limits>
#include <string>

#include "sic/error.hpp"
namespace sic::support {
// All compact point/group/coset/permutation storage uses this signed type.
// Negative values are sentinels or signed orbit encodings; mathematical signs
// and 2-adic valuations have separate types. Widen storage by changing this alias.
using Index = int32_t;
inline constexpr Index kNoIndex = -1;
inline Index checked_index(uint64_t value) {
  if (value > static_cast<uint64_t>(std::numeric_limits<Index>::max()))
    throw sic::Error(sic::ErrorKind::resource, "producer index capacity exceeded");
  return static_cast<Index>(value);
}
inline void admit_index_count(uint64_t count) {
  // Keep one positive loop endpoint representable, in addition to the sentinel.
  (void)checked_index(count);
}
}  // namespace sic::support
