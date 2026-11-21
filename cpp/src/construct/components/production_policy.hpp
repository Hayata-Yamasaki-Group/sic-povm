// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <climits>

#include "exact/arithmetic/dimension_policy.hpp"
#include "sic/sicx/format.hpp"

namespace sic::construct {
// Computational allowances, never wire-format limits. Validate once at run admission,
// then pass the same immutable value through selection, rescaling, writing and checking.
struct ProductionPolicy {
  sic::exact::DimensionPolicy dimension;
  sic::PrecisionPolicy precision;
  sic::sicx::ValuationPolicy valuation;
  sic::sicx::ResourceLimits resources;
  uint64_t exact_service_degree = 4096;
  uint64_t exact_service_bytes = uint64_t{256} << 20;
  uint64_t alignment_samples = 1000000;
  unsigned alignment_sweeps = 64;
  long estimated_shortfall_bits = 1000000;
  void validate() const;
};
}  // namespace sic::construct
