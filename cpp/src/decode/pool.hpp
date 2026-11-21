// SPDX-License-Identifier: AGPL-3.0-or-later
// Worker pool, NUMA placement and memory admission of the SICX decoder
// (include/sic/decode/decoder.hpp).
//
//  * Placement: the CPUs of the current affinity mask, grouped by NUMA node.
//    Worker t of T is pinned to cpus[t * |cpus| / T], so consecutive workers
//    share a node and form that node's group.
//  * Pool: persistent workers (started and pinned once). run(count, body)
//    executes body(0..count-1) and returns when all are done; the calling
//    thread participates. Indices are handed out in chunks from one range per
//    worker group; a group that runs dry steals from the others. Every index is
//    executed exactly once, so results written per index do not depend on the
//    schedule. A nested run() from a job of this pool executes inline with the
//    current worker index. A budgeted foreign team entered between the calls preserves
//    that index in its owner callback. Other teams may use the pool inline only on its owner.
//  * MemoryBudget: a byte semaphore. A task acquires its upper-bound estimate
//    before it allocates and releases it afterwards; acquire blocks while the
//    admitted total plus the resident baseline would exceed the budget. A task
//    larger than the whole budget is refused (the caller falls back to a
//    streaming path or fails closed). Per-worker peaks are recorded.
#pragma once
#include <flint/flint.h>
#include <malloc.h>
#include <sched.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "sic/decode/placement.hpp"
#include "sic/error.hpp"
#include "sic/support/admission.hpp"
#include "sic/support/flint_raii.hpp"
#include "sic/support/stop.hpp"
#include "support/runtime/worker_team.hpp"

namespace sic::decode {

class Pool {
 public:
  // The placement is copied; the owner is pinned as worker 0 until destruction.
  // Construction inside a team defers pinning/startup until an external dispatch;
  // inline nested work uses the caller without changing its affinity.
  // External and foreign-team inline dispatch require the constructing thread.
  // Same-pool nesting keeps its scratch ID across a budgeted foreign team's
  // owner callback. Other foreign callers are refused with std::logic_error
  // before touching scratch.
  Pool(unsigned workers, const Placement& placement);
  ~Pool() { shutdown(); }
  Pool(const Pool&) = delete;
  Pool& operator=(const Pool&) = delete;
  unsigned size() const { return workers_; }
  unsigned groups() const { return groups_; }
  unsigned physical_parallelism() const { return static_cast<unsigned>(physical_workers_.size()); }
  unsigned bind_failures() const { return bind_failures_.load(); }

 private:
  void shutdown();
  void initialize();

 public:
  // Worker index of the calling thread (0 for the owner thread).
  static unsigned& worker_id() { return sic::support::WorkerTeam::worker_id(); }

  // body(i) for i in [0, count). Below `inline_below` items, or when called
  // from inside a job, runs inline in index order (same results).
  template <class Body>
  void run(size_t count, const Body& body, size_t inline_below = 2, bool physical = false);
  template <class Body>
  void run_grouped(size_t count, const Body& body, size_t inline_below = 16);
  // Sequential groups preserve each output's arithmetic, with four groups per worker.
  size_t group_size(size_t count) const;
  size_t compute_group_size(size_t count, size_t minimum = 1) const;

 private:
  struct alignas(64) Range {
    std::atomic<size_t> next{0};
    size_t end = 0;
    Range() = default;
    Range(const Range& o) : next(o.next.load()), end(o.end) {}
    Range& operator=(const Range& o) {
      next.store(o.next.load());
      end = o.end;
      return *this;
    }
  };
  Placement place_;
  unsigned workers_;
  unsigned groups_ = 0;
  cpu_set_t owner_mask_;
  bool owner_mask_saved_ = false;
  std::atomic<unsigned> bind_failures_{0};
  std::atomic<bool> busy_{false};
  std::vector<unsigned> group_of_, physical_workers_;
  std::vector<Range> ranges_;
  sic::support::WorkerTeam team_;
  std::thread::id owner_ = std::this_thread::get_id();
  bool initialized_ = false;
  static Pool*& current_pool() {
    thread_local Pool* pool = nullptr;
    return pool;
  }
  struct WorkerScope {
    Pool* previous_pool;
    unsigned previous_id;
    WorkerScope(Pool* pool, unsigned id)
        : previous_pool(std::exchange(current_pool(), pool)),
          previous_id(std::exchange(worker_id(), id)) {}
    ~WorkerScope() {
      current_pool() = previous_pool;
      worker_id() = previous_id;
    }
  };
  using Job = void (*)(const void*, size_t);
  Job job_ = nullptr;
  const void* body_ = nullptr;
  void run_erased(size_t count, Job job, const void* body, bool physical);
  size_t grain_ = 1;
  // Hot atomics on separate cache lines (spun on / decremented by every worker).
  alignas(64) std::atomic<bool> failed_{false};

  std::exception_ptr error_;
  std::mutex mutex_;

  static bool inside() { return sic::support::WorkerTeam::inside(); }

  void work(unsigned t);
};

// Resident set size of this process (bytes), from /proc/self/statm.
uint64_t resident_bytes();

// Memory budget (bytes) of the decoder: a limit on
//   baseline (measured resident set at a stage start, after returning free heap pages)
//   + reservations (estimates of a stage's long-lived arrays)
//   + admitted tasks (estimates of each running task's transient memory).
// Reservations that do not fit are refused (the caller lowers its concurrency or
// fails closed); a task waits until it fits and is refused if it can never fit.
// The accounting is only as good as the estimates (allocator caching, mapped
// file pages and library allocations are not modelled), so admission also
// probes the measured resident set: while other tasks run, a task is not
// started if RSS + its estimate exceeds the budget. Admissions do not nest.
// The resulting peak RSS is validated against the budget by tests and runs, not proved.
class MemoryBudget {
 public:
  // `budget` is the user's limit; the accounting uses 15/16 of it, leaving 1/16
  // for allocator caching and unmodelled allocations (measured overshoot of the
  // accounting alone: 5 % at d1024, 64 workers).
  MemoryBudget(uint64_t budget, unsigned workers);
  uint64_t budget() const { return budget_; }
  uint64_t limit() const { return limit_; }
  // Only between stages (no reservations or tasks outstanding).
  void rebase(uint64_t resident);
  uint64_t fixed() const;
  uint64_t available() const;
  bool try_reserve(uint64_t bytes);
  void unreserve(uint64_t bytes);
  // Production Admission groups require a fresh live sample. The default keeps
  // the accounting-only first-owner contract for direct semaphore clients.
  void acquire(uint64_t bytes, unsigned worker, bool live_group = false);
  void release(uint64_t bytes, unsigned worker);
  uint64_t peak() const;
  uint64_t waits() const;
  uint64_t rss_waits() const;
  uint64_t peak_per_worker() const;

 private:
  void note_peak() { peak_ = std::max(peak_, baseline_ + reserved_ + admitted_); }
  bool fits(uint64_t used, uint64_t bytes) const {
    return sic::support::admission::fits(used, bytes, budget_);
  }
  uint64_t limit_, budget_, baseline_ = 0, reserved_ = 0, admitted_ = 0, peak_ = 0, waits_ = 0,
                            rss_waits_ = 0;
  std::vector<uint64_t> per_worker_, peak_worker_;
  mutable std::mutex mutex_;
  std::condition_variable released_;
  // Structural revision protects baseline/reservations; grants never decrease.
  uint64_t revision_ = 0, granted_ = 0;
};

// RAII reservation of a stage's arrays; refuses (fail closed) when it does not fit.
class ReserveGuard {
 public:
  ReserveGuard(MemoryBudget& b, uint64_t bytes, const char* what);
  ~ReserveGuard();
  ReserveGuard(const ReserveGuard&) = delete;
  ReserveGuard& operator=(const ReserveGuard&) = delete;

 private:
  MemoryBudget& b_;
  uint64_t bytes_;
};

// RAII admission of one task's transient memory (accounted to the calling worker).
class Admission {
 public:
  Admission(MemoryBudget& b, uint64_t bytes) : b_(b), bytes_(bytes), worker_(Pool::worker_id()) {
    b_.acquire(bytes_, worker_, true);
  }
  ~Admission() { b_.release(bytes_, worker_); }
  Admission(const Admission&) = delete;
  Admission& operator=(const Admission&) = delete;

 private:
  MemoryBudget& b_;
  uint64_t bytes_;
  unsigned worker_;
};

}  // namespace sic::decode

#include "decode/pool.tpp"
