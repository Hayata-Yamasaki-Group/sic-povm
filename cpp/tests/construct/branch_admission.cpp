// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/components/branch_admission.hpp"

#include <stdexcept>
int main() {
  using sic::construct::branch_scratch_bytes;

  std::vector<long> quadratic(28, 2), mixed = quadratic;
  mixed[0] = 3;
  // Synthetic large-tower geometry: a 40k-bit path needs no field-sized allocation.
  const auto base = branch_scratch_bytes(mixed, 20000, 10000, 0);
  if (base >= sic::support::resources::kGib ||
      branch_scratch_bytes(mixed, 40000, 10000, 0) <= base ||
      branch_scratch_bytes(mixed, 20000, 10000, 123456) != base + 123456)
    throw std::runtime_error(
        "branch forecast reintroduced field-sized storage or lost source scratch");
  auto wide = mixed;
  wide[0] = 31;
  if (branch_scratch_bytes(wide, 20000, 10000, 0) <= base)
    throw std::runtime_error("odd-root scratch missing");
  bool refused = false;
  try {
    (void)branch_scratch_bytes({1}, 128, 0, 0);
  } catch (...) {
    refused = true;
  }
  if (!refused) throw std::runtime_error("invalid tower accepted");
  refused = false;
  try {
    (void)branch_scratch_bytes(mixed, UINT64_MAX, 1, 0);
  } catch (...) {
    refused = true;
  }
  if (!refused) throw std::runtime_error("branch precision overflow accepted");
}
