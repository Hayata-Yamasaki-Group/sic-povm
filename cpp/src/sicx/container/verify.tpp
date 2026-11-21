// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>

#include "sic/support/stop.hpp"
#include "support/admitted_worker_team.hpp"
namespace sic::sicx {
template <class F>
inline void parallel_for(size_t count, unsigned threads, F body) {
  threads =
      std::max(1u, std::min<unsigned>(threads, static_cast<unsigned>(std::max<size_t>(count, 1))));
  std::atomic<size_t> next{0};
  std::exception_ptr error;
  std::mutex error_mutex;
  auto worker = [&] {
    for (;;) {
      const size_t i = next.fetch_add(1);
      if (i >= count) return;
      try {
        sic::support::check_stop();
        body(i);
      } catch (...) {
        std::lock_guard lock(error_mutex);
        if (!error) error = std::current_exception();
        next = count;
        return;
      }
    }
  };
  sic::support::local_worker_team().run(threads, [&](unsigned) { worker(); });
  if (error) std::rethrow_exception(error);
}
}  // namespace sic::sicx
