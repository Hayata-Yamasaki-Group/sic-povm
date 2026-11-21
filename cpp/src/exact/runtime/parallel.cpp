// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/runtime/parallel.hpp"

namespace sic::exact {
FlintWorkerCap::FlintWorkerCap(int workers) : previous(0) {
  if (workers <= 0) throw std::invalid_argument("FLINT worker cap must be positive");
  previous = flint_set_num_workers(workers - 1);
}

ClassNativeShare::ClassNativeShare(int owned_threads, int total_threads, int class_workers)
    : saved_workers_(flint_get_num_threads() - 1), saved_nested_(nested_loop_budget) {
  if (owned_threads < 1 || total_threads < 1 || class_workers < 1 ||
      owned_threads > total_threads / class_workers)
    throw std::invalid_argument("class native share exceeds its admitted allocation");
  // flint_set_num_workers() can only lower the inherited limit. The class
  // callback inherits zero workers from parallel_for, so an authorized
  // owned share needs the explicit TLS reset, never set_num_threads().
  flint_reset_num_workers(owned_threads - 1);
  nested_loop_budget = owned_threads;
}

}  // namespace sic::exact
