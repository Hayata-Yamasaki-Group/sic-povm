// SPDX-License-Identifier: AGPL-3.0-or-later
// Shared retained-storage proposals for scheduling and refiner growth.
#pragma once

#include "exact/runtime/memory/ball_storage.hpp"
#include "exact/runtime/memory/phase_accounting.hpp"

namespace sic::exact::phase_memory {

struct StorageContractError : std::runtime_error {
  using std::runtime_error::runtime_error;
};
struct CapacityRefusal {};
struct ObserverException {
  std::exception_ptr cause;
};

// Read-only inspection before expanding a packed/coarse source. The maximum
// per-value retained allocation conservatively covers every copied entry;
// it is deliberately independent of Table's precision and current acb_bits.
struct CopySource {
  sic::support::resources::BitCount bits = 64;
  Bytes maximum_heap = 0;
  bool finite_lagom = true;
  void observe(const BallStorage& storage);
  void observe(acb_srcptr value) {
    BallStorage storage;
    storage.observe(value);
    observe(storage);
  }
  Bytes grid_storage(sic::support::resources::Count dimension) const;
};

struct Options {
  int workers = 1;
  bool modular = false, column_inverse = false, threaded_product = false;
  bool refined_correction = false, parallel_reconstruction = false, reference = false;
  Bytes native_scalar_bytes = 0, native_complex_bytes = 0;
};

Ceilings propose(sic::support::resources::Count dimension, sic::support::resources::BitCount target,
                 sic::support::resources::BitCount inverse_hint, const CopySource& source,
                 const Options& options);

// Ownership/configuration is fixed across one callback and its serial drain.
// Only retained storage ceilings grow. A new seed may require larger ceilings.
void merge(Ceilings& retained, const Ceilings& update);

struct Publication {
  Bytes packed_output = 0;
  int replay_workers = 1;
  bool replay = false;
};

struct RequiredMemory {
  Ceilings ceilings;
  Accounting accounting;
  Bytes private_peak = 0, callback_peak = 0;
};

RequiredMemory requirement(const Ceilings& c, const Publication& publication = {});

// High residual caches have already died when a current-Jacobian correction
// starts. Charge retained owners here, then give only the remainder to its
// separately checked helper. Subtracting the full phase peak disables it.
Bytes correction_baseline(const Accounting& accounting);

Bytes shortcut_workspace(const Ceilings& ceilings, Bytes packed_output);

}  // namespace sic::exact::phase_memory
