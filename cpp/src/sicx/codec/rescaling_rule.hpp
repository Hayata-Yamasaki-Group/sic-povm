// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/sicx/rescaling_rule.hpp"
#include "sicx/codec/integer.hpp"

namespace sic::sicx {
// Minimum table entries per active worker, including the calling thread.
inline constexpr uint64_t kRescalingParallelGrain = 32768;
#ifdef SIC_ENABLE_TEST_HOOKS
extern thread_local uint64_t rescaling_thread_starts;
extern thread_local uint64_t rescaling_parallel_grain;
#endif
uint64_t pair_val2(const fmpz* x, const fmpz* y);
uint64_t own_weight(const ElementId& id, const std::vector<unsigned>& degrees);
}  // namespace sic::sicx
