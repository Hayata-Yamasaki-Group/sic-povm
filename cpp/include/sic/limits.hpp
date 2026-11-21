// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdint>
#include <limits>

#include "sic/error.hpp"
namespace sic {
inline constexpr uint64_t kDefaultCoreDimension = uint64_t{1} << 24;
inline constexpr long kDefaultMaximumPrecision = 65536;
// Shared explicit precision-ceiling floor for the exact-tool command lines.
inline constexpr long kMinimumToolPrecision = 256;
// Native arithmetic headroom for checked doubling and guard bits. A resource
// policy may be raised up to this bound; this is not a wire-format limit.
inline constexpr long kNativeMaximumPrecision = std::numeric_limits<long>::max() / 8;
struct PrecisionPolicy {
  long maximum_bits = kDefaultMaximumPrecision;
};
long checked_precision(long target, long guard, long ceiling);
struct InputLimits {
  uint64_t maximum_dimension = kDefaultCoreDimension;
  uint64_t maximum_bytes = uint64_t{512} << 20;
};
}  // namespace sic
