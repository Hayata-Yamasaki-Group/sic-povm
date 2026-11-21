// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::construct {
template <class Plan, class Prepare, class Requirement, class Refused, class Callback>
void dispatch_live_classes(std::span<const long> classes, int workers,
                           sic::support::Reservations* pool, Plan plan, Prepare prepare,
                           Requirement requirement, Refused refused, Callback callback) {
  struct Owner {
    sic::support::ThreadGroup thread;
    std::atomic<bool> done{false};
    std::exception_ptr error;
    int workers;
    explicit Owner(int count) : workers(count) {}
    ~Owner() { thread.join(); }
  };
  std::mutex completion_mutex;
  std::condition_variable completion;
  // Declared after completion state: every worker joins before its borrowed
  // callback context, seed owners, leases or notification state can disappear.
  std::vector<std::unique_ptr<Owner>> live;
  int available = workers;
  size_t next = 0;
  auto reap = [&] {
    for (auto it = live.begin(); it != live.end();) {
      if (!(*it)->done.load(std::memory_order_acquire)) {
        ++it;
        continue;
      }
      (*it)->thread.join();
      available += (*it)->workers;
      const auto error = (*it)->error;
      it = live.erase(it);
      if (error) std::rethrow_exception(error);
    }
  };
  auto wait = [&] {
    std::unique_lock lock(completion_mutex);
    completion.wait_for(lock, std::chrono::milliseconds(50), [&] {
      return std::any_of(live.begin(), live.end(), [](const auto& owner) {
        return owner->done.load(std::memory_order_acquire);
      });
    });
  };
  while (next < classes.size() || !live.empty()) {
    reap();
    sic::support::check_stop();
    if (next == classes.size()) {
      if (!live.empty()) wait();
      continue;
    }
    const long c = classes[next];
    const auto allocation =
        plan(c, classes.size() - next, static_cast<int>(live.size()), available);
    if (!allocation) {
      if (!live.empty()) {
        wait();
        continue;
      }
      refused(c, UINT64_MAX);
      ++next;
      continue;
    }
    if (allocation->workers < 1 || allocation->workers > available)
      throw std::logic_error("class scheduler exceeded its live CPU allocation");
    if (!prepare(c, *allocation)) {
      if (!live.empty()) {
        wait();
        continue;
      }
      ++next;
      continue;
    }
    std::optional<sic::support::Reservations::Lease> grant;
    sic::support::resources::Bytes bytes = UINT64_MAX;
    try {
      bytes = requirement(c, *allocation);
      if (pool) grant = pool->try_acquire(bytes);
    } catch (const std::overflow_error&) {
    }
    if (bytes == UINT64_MAX || (pool && !grant)) {
      if (!live.empty()) {
        wait();
        continue;
      }
      refused(c, bytes);
      ++next;
      continue;
    }
    auto chosen = *allocation;
    chosen.callback_bytes = bytes;
    chosen.live = static_cast<int>(live.size()) + 1;
    auto owner = std::make_unique<Owner>(allocation->workers);
    auto* record = owner.get();
    // The record is stable, including if thread creation throws. ThreadGroup
    // carries stop/settings/progress/admission scopes into the worker.
    live.push_back(std::move(owner));
    available -= allocation->workers;
    record->thread.start([&, record, c, chosen, grant = std::move(grant)]() mutable {
      const int saved_workers = flint_set_num_workers(0);
      const int saved_budget = sic::exact::nested_loop_budget;
      sic::exact::nested_loop_budget = chosen.workers;
      try {
        // End the callback and its grant before signalling freed CPU tokens.
        {
          auto owned = std::move(grant);
          callback(c, chosen, std::move(owned));
        }
      } catch (...) {
        record->error = std::current_exception();
      }
      grant.reset();
      flint_cleanup();
      flint_reset_num_workers(saved_workers);
      sic::exact::nested_loop_budget = saved_budget;
      record->done.store(true, std::memory_order_release);
      completion.notify_one();
    });
    ++next;
  }
}
}  // namespace sic::construct
