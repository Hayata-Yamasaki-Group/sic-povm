// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/components/production_policy.hpp"

namespace sic::construct {
void ProductionPolicy::validate() const {
  (void)dimension.checked(1);
  (void)sic::checked_precision(128, 128, precision.maximum_bits);
  if (!exact_service_degree || exact_service_degree > sic::sicx::kMaximumFieldDegree ||
      !exact_service_bytes || !valuation.table_bytes ||
      valuation.table_bytes > valuation.total_bytes || !alignment_samples ||
      alignment_sweeps > INT_MAX || estimated_shortfall_bits < 1 ||
      estimated_shortfall_bits > sic::kNativeMaximumPrecision || !resources.json_depth ||
      resources.json_depth > 1024 || !resources.text_chunk_bytes || !resources.block_bytes ||
      !resources.directory_entries || !resources.reader_memory_bytes ||
      !resources.chunk_verification_bytes || !resources.inflight_verification_bytes)
    throw sic::Error(ErrorKind::invalid_argument, "invalid production policy");
}

}  // namespace sic::construct
