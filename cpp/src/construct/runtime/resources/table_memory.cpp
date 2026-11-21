// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/runtime/resources/table_memory.hpp"

namespace sic::construct::resources {
RetainedTable inspect_retained_table(const Table& table) {
  using sic::support::resources::add;
  using sic::support::resources::mul;
  RetainedTable result;
  auto vector = [&](const std::vector<acb_struct>& values, bool input) {
    result.owned = add(result.owned, mul(sizeof(acb_struct) + 64, values.capacity()));
    // One pass supplies both the resident census and the per-entry copy
    // ceiling. Do not rescan a large coarse table for the same allocations.
    for (const auto& value : values) {
      BallStorage storage;
      storage.observe(&value);
      result.owned = add(result.owned, storage.heap_bytes);
      result.finite_lagom = result.finite_lagom && storage.finite_lagom;
      if (input) result.source.observe(storage);
    }
  };
  result.owned = add(result.owned, sizeof(Table));
  result.owned = add(
      result.owned,
      add(mul(table.packed.capacity(), sizeof(sic::support::ComplexBallVector)),
          add(mul(table.val.capacity(), sizeof(std::vector<acb_struct>)),
              add(mul(table.sd.capacity(), sizeof(Table::SData)),
                  mul(add(add(table.soff.capacity(), table.s_c.capacity()), table.s_l.capacity()),
                      sizeof(sic::support::Index))))));
  vector(table.zero_value.v, true);
  for (const auto& values : table.packed) vector(values.v, true);
  for (const auto& values : table.val) vector(values, true);
  for (const auto& state : table.sd) {
    vector(state.L, false);
    vector(state.S, false);
  }

  for (const auto& [key, rule] : table.gl_cache) {
    (void)key;
    result.owned = add(result.owned, add(256, mul(rule.capacity(), sizeof(rule[0]) + 128)));
    for (const auto& pair : rule)
      for (const arb_struct* value : {&pair.abscissa, &pair.weight}) {
        const auto bytes = arb_allocated_bytes(value);
        if (bytes < 0) throw std::overflow_error("negative rule allocation count");
        result.owned = add(result.owned, static_cast<Bytes>(bytes));
        result.finite_lagom = result.finite_lagom && arb_is_finite(value) && ARB_IS_LAGOM(value);
      }
  }
  return result;
}

SeedKernelMemory::SeedKernelMemory(Table& table, sic::support::Reservations::Lease& scratch,
                                   sic::support::resources::Bytes base, int concurrent_states)
    : table_(table), scratch_(scratch), base_(base), concurrent_(concurrent_states) {
  if (!base || scratch.bytes() < base || concurrent_states <= 0)
    throw std::invalid_argument("invalid prepared-seed kernel owner");
  prior_ = std::move(table_.admit_workspace);
  try {
    table_.admit_workspace = [this](sic::support::resources::Bytes bytes) { require(bytes); };
  } catch (...) {
    table_.admit_workspace = std::move(prior_);
    throw;
  }
}

void SeedKernelMemory::require(sic::support::resources::Bytes bytes) {
  // One lease has one mutator. Nonstreamed cycle states can call here
  // concurrently, so protect both the maximum and lease growth.
  std::lock_guard lock(mutex_);
  const auto next = std::max(peak_, bytes);
  sic::support::resources::Bytes needed;
  try {
    needed = sic::support::resources::add(base_, sic::support::resources::mul(next, concurrent_));
  } catch (const std::overflow_error&) {
    throw sic::support::resources::AdmissionError(
        "owned prepared-seed kernel forecast unrepresentable");
  }
  if (needed > scratch_.bytes() && !scratch_.try_resize(needed))
    throw sic::support::resources::AdmissionError("owned prepared-seed kernel growth refused");
  if (prior_) prior_(bytes);  // retain the existing fresh Budget gate
  peak_ = next;
}

}  // namespace sic::construct::resources

namespace sic::construct::resources {
sic::support::resources::Bytes SeedKernelMemory::kernel_peak() const {
  std::lock_guard lock(mutex_);
  return peak_;
}

sic::support::resources::Bytes SeedKernelMemory::required() const {
  std::lock_guard lock(mutex_);
  return sic::support::resources::add(base_, sic::support::resources::mul(peak_, concurrent_));
}

}  // namespace sic::construct::resources
