// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstddef>
#include <functional>
namespace sic_check::crt::sicx {
inline constexpr unsigned kDefaultThreads = 1;
extern unsigned g_threads;
// Declare before a worker's FLINT values so its thread-local caches are released
// after those values have been destroyed, including on exceptional exits.
struct ThreadCleanup {
  ThreadCleanup() = default;
  ~ThreadCleanup();
  ThreadCleanup(const ThreadCleanup&) = delete;
  ThreadCleanup& operator=(const ThreadCleanup&) = delete;
};
void parallel_for(size_t n, const std::function<void(size_t, size_t)>& body);
}  // namespace sic_check::crt::sicx
