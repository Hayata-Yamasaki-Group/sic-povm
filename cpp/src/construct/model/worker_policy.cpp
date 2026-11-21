// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/model/worker_policy.hpp"

#include <algorithm>

#include "support/resources/platform_snapshot.hpp"

namespace sic::construct {
int default_producer_workers(unsigned exponent) {
  // Small analytic grids do not amortize a large worker team's dispatch and
  // allocator costs. Saturate at the physical cores, without counting SMT.
  const int work_limit = exponent < 9 ? std::min(32, 1 << (exponent > 2 ? exponent - 2 : 0)) : 64;
  return std::min(static_cast<int>(sic::support::resources::requested_threads(nullptr)),
                  work_limit);
}
int newton_class_workers(std::optional<int> class_threads, int threads, int remaining) {
  return std::min({class_threads.value_or(threads), threads, remaining});
}
}  // namespace sic::construct
