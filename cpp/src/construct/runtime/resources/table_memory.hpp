// SPDX-License-Identifier: AGPL-3.0-or-later
// Read-only retained Table ownership audit; keep coarse source headers stable.
// Shared Arith/layout ownership is charged by the caller's resident model.
#pragma once

#include "exact/analytic/table.hpp"
#include "exact/runtime/newton_memory_admission.hpp"
#include "support/runtime/reservations.hpp"

namespace sic::construct::resources {
using sic::exact::Table;
using sic::exact::phase_memory::BallStorage;
using sic::exact::phase_memory::CopySource;
using sic::exact::phase_memory::StorageContractError;
using sic::support::resources::Bytes;

struct RetainedTable {
  CopySource source;
  Bytes owned = 64 * sic::support::resources::kMib;
  bool finite_lagom = true;
};

RetainedTable inspect_retained_table(const Table& table);

}  // namespace sic::construct::resources

namespace sic::construct::resources {
using sic::exact::Table;
using sic::exact::phase_memory::BallStorage;
using sic::exact::phase_memory::CopySource;
using sic::exact::phase_memory::StorageContractError;
using sic::support::resources::Bytes;

// Table's kernels report before allocating each panel. The base covers
// grids/state scratch already admitted by the caller; the added kernel
// maximum covers every potentially concurrent state, including rule caches.
// This intentionally overcounts overlap with the original base forecast.
//
// The Table must outlive this binding, and all build workers must join
// before it dies. On a refusal Table's local Arb owners unwind first; the
// caller then destroys the Table while the scratch lease is still alive.
// The prior callback must not re-enter this binding or its locked getters.
class SeedKernelMemory {
 public:
  SeedKernelMemory(Table& table, sic::support::Reservations::Lease& scratch,
                   sic::support::resources::Bytes base, int concurrent_states);
  ~SeedKernelMemory() { table_.admit_workspace = std::move(prior_); }
  SeedKernelMemory(const SeedKernelMemory&) = delete;
  SeedKernelMemory& operator=(const SeedKernelMemory&) = delete;

  sic::support::resources::Bytes kernel_peak() const;
  sic::support::resources::Bytes required() const;

 private:
  Table& table_;
  sic::support::Reservations::Lease& scratch_;
  const sic::support::resources::Bytes base_;
  const int concurrent_;
  std::function<void(sic::support::resources::Bytes)> prior_;
  mutable std::mutex mutex_;
  sic::support::resources::Bytes peak_ = 0;

  void require(sic::support::resources::Bytes bytes);
};

}  // namespace sic::construct::resources
