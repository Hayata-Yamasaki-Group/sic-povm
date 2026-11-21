// SPDX-License-Identifier: AGPL-3.0-or-later
#include "parallel_for.hpp"

#include <flint/flint.h>

#include <algorithm>
#include <thread>
#include <vector>
namespace sic_check::crt::sicx {

unsigned g_threads = kDefaultThreads;

ThreadCleanup::~ThreadCleanup() { flint_cleanup(); }

void parallel_for(size_t n, const std::function<void(size_t, size_t)>& body) {
  const size_t T = std::min<size_t>(g_threads, std::max<size_t>(1, n / 64));
  if (T <= 1) {
    body(0, n);
    return;
  }
  std::vector<std::thread> th;
  for (size_t i = 0; i < T; ++i)
    th.emplace_back([&, i] {
      ThreadCleanup cleanup;
      body(n * i / T, n * (i + 1) / T);
    });
  for (auto& x : th) x.join();
}
}  // namespace sic_check::crt::sicx
