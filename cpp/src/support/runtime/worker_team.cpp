// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/runtime/worker_team.hpp"

#include <flint/flint.h>
#include <flint/thread_support.h>

#include <cerrno>
#include <chrono>
#include <climits>
#include <stdexcept>
#include <system_error>
#include <utility>
#if defined(__linux__)
#include <sched.h>
#endif

namespace sic::support {
namespace {
thread_local unsigned dispatch_depth = 0;
// Capture before any facade pins its owner. Later growth must not inherit that pin.
struct ProcessAffinity {
#if defined(__linux__)
  cpu_set_t mask{};
  bool valid = sched_getaffinity(0, sizeof mask, &mask) == 0;
  void apply() const {
    if (valid && sched_setaffinity(0, sizeof mask, &mask))
      throw std::system_error(errno, std::generic_category(), "worker affinity");
  }
#else
  void apply() const {}
#endif
};
const ProcessAffinity& process_affinity() {
  static const ProcessAffinity affinity;
  return affinity;
}
struct NativeWorkerSuppression {
  int previous = flint_set_num_workers(0);
  ~NativeWorkerSuppression() { flint_reset_num_workers(previous); }
};
template <class Ready>
bool spin_until(Ready ready) {
  const auto limit = std::chrono::steady_clock::now() + std::chrono::microseconds(50);
  for (unsigned k = 0;; ++k) {
    if (ready()) return true;
    if ((k & 63) == 63 && std::chrono::steady_clock::now() > limit) return ready();
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#else
    std::this_thread::yield();
#endif
  }
}
void publish(std::atomic<unsigned>& pending) {
  if (pending.exchange(1, std::memory_order_release) == 2) pending.notify_one();
}
}  // namespace

WorkerTeam::WorkerTeam(GrowthAdmission admission) : admission_(admission) {
  (void)process_affinity();
}
bool WorkerTeam::inside() { return dispatch_depth != 0; }
unsigned WorkerTeam::depth() { return dispatch_depth; }
unsigned& WorkerTeam::worker_id() {
  thread_local unsigned id = 0;
  return id;
}

WorkerTeam::~WorkerTeam() { shutdown(); }

void WorkerTeam::shutdown() {
  if (running_.load()) throw std::logic_error("worker team shutdown during dispatch");
  stopping_.store(true, std::memory_order_release);
  for (auto& slot : slots_) publish(slot->pending);
  threads_.join();
  slots_.clear();
  stopping_.store(false, std::memory_order_relaxed);
}

void WorkerTeam::prepare(unsigned count) {
  if (inside()) return;
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error("worker team preparation requires its owner thread");
  grow(count);
}

void WorkerTeam::grow(unsigned count) {
  if (count <= size()) return;
  if (admission_) admission_(count - size());
  slots_.reserve(count - 1);
  threads_.reserve(count - 1);
  // Threads wait with neutral contexts, never borrowing a request's lifetime.
  WorkerContextScope neutral({});
  try {
    while (size() < count) {
      const unsigned id = size();
      auto slot = std::make_unique<Slot>();
      auto* stable = slot.get();
      slots_.push_back(std::move(slot));
      threads_.start([this, id, stable] { worker_main(id, *stable); });
      while (!stable->started.load(std::memory_order_acquire))
        stable->started.wait(false, std::memory_order_acquire);
      if (stable->startup_failure) std::rethrow_exception(stable->startup_failure);
    }
  } catch (...) {
    shutdown();  // Includes every successfully started worker on partial failure.
    throw;
  }
}

void WorkerTeam::worker_main(unsigned id, Slot& slot) {
  try {
    process_affinity().apply();
  } catch (...) {
    slot.startup_failure = std::current_exception();
  }
  slot.started.store(true, std::memory_order_release);
  slot.started.notify_one();
  if (slot.startup_failure) return;
  for (;;) {
    if (!spin_until([&] { return slot.pending.load(std::memory_order_acquire) != 0; })) {
      unsigned empty = 0;
      if (slot.pending.compare_exchange_strong(empty, 2, std::memory_order_acq_rel,
                                               std::memory_order_acquire))
        slot.pending.wait(2, std::memory_order_acquire);
    }
    if (stopping_.load(std::memory_order_acquire)) {
      flint_cleanup();
      return;
    }
    slot.pending.store(0, std::memory_order_relaxed);
    invoke(id);
    // Regions release transient caches on completion. Retained facades release
    // them explicitly, or at worker shutdown, matching their scratch lifetime.
    if (caches_ == CacheLifetime::region) flint_cleanup();
    if (active_.fetch_sub(1, std::memory_order_acq_rel) == 1) active_.notify_one();
  }
}

void WorkerTeam::invoke(unsigned id) {
  WorkerContextScope context(context_);
  NativeWorkerSuppression native_workers;
  const unsigned saved_id = worker_id();
  // A budgeted nested team borrows its owner's thread. Preserve that thread's
  // enclosing scratch identity; the callback argument carries this team's id.
  if (!inside()) worker_id() = id;
  ++dispatch_depth;
  try {
    job_(body_, id);
  } catch (...) {
    std::lock_guard lock(failure_mutex_);
    if (!failure_) failure_ = std::current_exception();
  }
  --dispatch_depth;
  worker_id() = saved_id;
}

void WorkerTeam::run_erased(unsigned count, Job job, void* body, std::span<const unsigned> selected,
                            CacheLifetime caches, NestedDispatch nested) {
  if (!count) return;
  if (!selected.empty() && selected.size() != count)
    throw std::invalid_argument("worker selection size mismatch");
  if (inside() && nested == NestedDispatch::inline_only) {
    NativeWorkerSuppression native_workers;
    for (unsigned q = 0; q < count; ++q) job(body, selected.empty() ? q : selected[q]);
    return;
  }
  if (std::this_thread::get_id() != owner_)
    throw std::logic_error("worker team dispatch requires its owner thread");
  if (running_) throw std::logic_error("worker team dispatch already active");
  unsigned capacity = count;
  if (!selected.empty()) {
    if (selected[0] != 0) throw std::invalid_argument("worker selection must include owner zero");
    for (unsigned q = 1; q < count; ++q)
      if (selected[q] <= selected[q - 1])
        throw std::invalid_argument("worker selection must be strictly increasing");
    if (selected.back() == UINT_MAX) throw std::invalid_argument("worker selection overflow");
    capacity = selected.back() + 1;
  }
  grow(capacity);
  running_ = true;
  job_ = job;
  body_ = body;
  context_ = WorkerContext::capture();
  caches_ = caches;
  active_.store(count - 1, std::memory_order_relaxed);
  for (unsigned q = 1; q < count; ++q) {
    const auto id = selected.empty() ? q : selected[q];
    publish(slots_[id - 1]->pending);
  }
  invoke(0);
  (void)spin_until([&] { return active_.load(std::memory_order_acquire) == 0; });
  for (auto active = active_.load(std::memory_order_acquire); active;
       active = active_.load(std::memory_order_acquire))
    active_.wait(active, std::memory_order_acquire);
  job_ = nullptr;
  body_ = nullptr;
  context_ = {};
  running_ = false;
  auto failure = std::exchange(failure_, {});
  if (failure) std::rethrow_exception(failure);
}
}  // namespace sic::support
