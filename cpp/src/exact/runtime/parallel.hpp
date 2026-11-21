// SPDX-License-Identifier: AGPL-3.0-or-later
// Reusable parallel_for over an index range (dynamic scheduling).
#pragma once
#include <flint/flint.h>
#include <flint/thread_support.h>

#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>

#include "sic/support/kernel_settings.hpp"
#include "sic/support/stop.hpp"
#include "support/admitted_worker_team.hpp"
#include "support/resources/checked_bytes.hpp"

namespace sic::exact {

inline thread_local int nested_loop_budget = std::numeric_limits<int>::max();

// Calls fn(i) for i in [0, n), on up to nthreads threads. fn must be thread-safe.
template <class F>
void parallel_for(long n, int nthreads, F&& fn);

}  // namespace sic::exact

namespace sic::exact {

class ClassNativeShare {
 public:
  ClassNativeShare(int owned_threads, int total_threads, int class_workers);

  ~ClassNativeShare() {
    flint_reset_num_workers(saved_workers_);
    nested_loop_budget = saved_nested_;
  }

  ClassNativeShare(const ClassNativeShare&) = delete;
  ClassNativeShare& operator=(const ClassNativeShare&) = delete;

 private:
  int saved_workers_, saved_nested_;
};

// FLINT's worker limit is thread-local. This guard only lowers inherited
// capacity; it never resizes the global pool or enlarges an outer class share.
struct FlintWorkerCap {
  int previous;
  explicit FlintWorkerCap(int workers);
  ~FlintWorkerCap() { flint_reset_num_workers(previous); }
  FlintWorkerCap(const FlintWorkerCap&) = delete;
  FlintWorkerCap& operator=(const FlintWorkerCap&) = delete;
};

}  // namespace sic::exact

#include "exact/runtime/parallel.tpp"
