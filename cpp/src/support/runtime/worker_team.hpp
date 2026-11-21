// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <type_traits>
#include <vector>

#include "sic/support/thread_group.hpp"
#include "support/runtime/worker_context.hpp"

namespace sic::support {
// Synchronous, reusable dispatch owned by one external thread. Callbacks may
// dispatch nested work inline, or explicitly dispatch an admitted nested budget.
// Each selected worker is invoked once; index scheduling belongs to the caller.
// No callback, request context or exception is retained after dispatch returns.
class WorkerTeam {
 public:
  using GrowthAdmission = void (*)(unsigned additional);
  enum class CacheLifetime { region, retained };
  enum class NestedDispatch { inline_only, budgeted };
  explicit WorkerTeam(GrowthAdmission admission = nullptr);
  ~WorkerTeam();
  WorkerTeam(const WorkerTeam&) = delete;
  WorkerTeam& operator=(const WorkerTeam&) = delete;

  template <class F>
  void run(unsigned count, F&& body, std::span<const unsigned> selected = {},
           CacheLifetime caches = CacheLifetime::region,
           NestedDispatch nested = NestedDispatch::inline_only) {
    run_erased(
        count,
        [](void* context, unsigned id) {
          (*static_cast<std::remove_reference_t<F>*>(context))(id);
        },
        const_cast<void*>(static_cast<const void*>(&body)), selected, caches, nested);
  }
  unsigned size() const { return static_cast<unsigned>(slots_.size()) + 1; }
  // Release idle workers. A request during dispatch throws std::logic_error.
  void shutdown();
  void prepare(unsigned count);
  static bool inside();
  static unsigned depth();
  // Enclosing worker/scratch identity, preserved on a nested team's owner.
  // Use the callback argument for the current dispatch's local worker index.
  static unsigned& worker_id();

 private:
  struct alignas(64) Slot {
    std::atomic<unsigned> pending{0};
    std::atomic<bool> started{false};
    std::exception_ptr startup_failure;
  };
  using Job = void (*)(void*, unsigned);
  std::thread::id owner_ = std::this_thread::get_id();
  std::vector<std::unique_ptr<Slot>> slots_;
  ThreadGroup threads_;
  alignas(64) std::atomic<unsigned> active_{0};
  std::atomic<bool> stopping_{false};
  Job job_ = nullptr;
  void* body_ = nullptr;
  WorkerContext context_;
  CacheLifetime caches_ = CacheLifetime::region;
  std::exception_ptr failure_;
  std::mutex failure_mutex_;
  GrowthAdmission admission_;
  std::atomic<bool> running_{false};
  void grow(unsigned count);
  void worker_main(unsigned id, Slot& slot);
  void invoke(unsigned id);
  void run_erased(unsigned count, Job job, void* body, std::span<const unsigned> selected,
                  CacheLifetime caches, NestedDispatch nested);
};

}  // namespace sic::support
