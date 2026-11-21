// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <thread>
#include <utility>
#include <vector>

#include "sic/support/admission.hpp"
#include "sic/support/kernel_settings.hpp"
#include "sic/support/stop.hpp"

namespace sic::support {
// Own threads with independent lifetimes (I/O transfers and admitted classes),
// or the workers of the shared dispatcher. Compute ranges use the dispatcher.
// Scheduling and cancellation stay with
// each caller: before joining a partially started barrier team, release its
// startup latch; before joining persistent workers, request their shutdown.
class ThreadGroup {
 public:
  ThreadGroup() = default;
  ~ThreadGroup();
  ThreadGroup(const ThreadGroup&) = delete;
  ThreadGroup& operator=(const ThreadGroup&) = delete;
  void reserve(std::size_t count);
  template <class F>
  void start(F&& fn) {
    auto* token = current_stop_token;
    const auto* settings = current_kernel_settings;
    auto* progress = current_progress_sink;
    const bool logging = admission::logging;
    threads_.emplace_back(
        [token, settings, progress, logging, body = std::forward<F>(fn)]() mutable {
          StopScope scope(token);
          KernelSettingsScope kernel_scope(settings);
          ProgressScope progress_scope(progress);
          admission::LogScope log(logging);
          body();
        });
  }
  void join();
  bool empty() const { return threads_.empty(); }
  std::size_t size() const { return threads_.size(); }

 private:
  std::vector<std::thread> threads_;
};
}  // namespace sic::support
