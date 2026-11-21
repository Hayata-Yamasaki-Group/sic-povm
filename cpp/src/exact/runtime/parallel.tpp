// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::exact {
template <class F>
void parallel_for(long n, int nthreads, F&& fn) {
  if (n <= 0) return;
  auto check_stop = [&](long i) {
    if ((i & 65535) == 0) sic::support::check_stop();
  };
  nthreads = std::min(nthreads, nested_loop_budget);
  if (nthreads <= 1 || n == 1) {
    for (long i = 0; i < n; ++i) {
      check_stop(i);
      fn(i);
    }
    return;
  }
  const int nt = (int)std::min<long>(nthreads, n);
  // Cheap full-field maps must not contend on one atomic cache line for
  // every element. Keep at least sixteen dynamic chunks per worker and
  // cap the grain so irregular numerical work remains balanced.
  long grain = std::max<long>(1, std::min<long>(1024, (n / nt) / 16));
  if (const char* configured = sic::support::kernel_setting("SIC_PARALLEL_GRAIN")) {
    const auto requested = sic::support::resources::integer(configured);
    if (!requested || requested > (sic::support::resources::Count)std::numeric_limits<long>::max())
      throw sic::Error(sic::ErrorKind::invalid_argument,
                       "SIC_PARALLEL_GRAIN must be a positive long");
    grain = std::min<long>(n, requested);
  }
  const long chunks = 1 + (n - 1) / grain;
  std::atomic<long> next(0);
  std::atomic<bool> cancelled(false);
  std::exception_ptr failure;
  std::mutex failure_mutex;
  auto run = [&](unsigned) {
    // The outer loop already owns the worker slots. Suppress nested FLINT
    // work only for its duration; restore the caller's serial-kernel budget.
    const int saved_workers = flint_set_num_workers(0);
    const int saved_budget = nested_loop_budget;
    nested_loop_budget = std::max(1, nthreads / nt);
    try {
      while (!cancelled) {
        const long chunk = next.fetch_add(1, std::memory_order_relaxed);
        if (chunk < 0 || chunk >= chunks) break;
        const long first = chunk * grain, end = first + std::min(grain, n - first);
        for (long i = first; i < end && !cancelled; ++i) {
          check_stop(i);
          fn(i);
        }
      }
    } catch (...) {
      std::lock_guard<std::mutex> lock(failure_mutex);
      if (!failure) failure = std::current_exception();
      cancelled = true;
    }
    flint_reset_num_workers(saved_workers);
    nested_loop_budget = saved_budget;
  };
  sic::support::local_worker_team().run(nt, run, {},
                                        sic::support::WorkerTeam::CacheLifetime::region,
                                        sic::support::WorkerTeam::NestedDispatch::budgeted);
  if (failure) std::rethrow_exception(failure);
}

}  // namespace sic::exact
