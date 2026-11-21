// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/resources.hpp"

#include <flint/acb.h>
#include <sys/resource.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "sic/error.hpp"
#include "sic/support/admission.hpp"
#include "sic/support/kernel_settings.hpp"
#include "support/index.hpp"
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace sic::support::resources {
Bytes process_limit(const Snapshot& snapshot, std::optional<Bytes> user_cap) {
  // Leave 20% of freshly measured headroom for load changes and unmodelled work.
  const Bytes headroom = mul(snapshot.available / 5, 4);
  const Bytes limit = add(snapshot.rss, headroom);
  return user_cap ? std::min(limit, *user_cap) : limit;
}

Budget::Budget() : initial(inspect()) {
  if (const char* value = sic::support::kernel_setting("SIC_MEMORY_MB"))
    user_cap = megabytes(value);
#ifdef SIC_ENABLE_TEST_HOOKS
  if (const char* value = sic::support::kernel_setting("SIC_DEV_MEMORY_MB")) {
    const auto fixed = megabytes(value);
    user_cap = user_cap ? std::min(*user_cap, fixed) : fixed;
  }
#endif
  initial_limit = process_limit(initial, user_cap);
}

Bytes Budget::limit() const { return std::min(initial_limit, process_limit(inspect(), user_cap)); }

Bytes Budget::remaining() const {
  const auto current = inspect();
  const Bytes cap = std::min(initial_limit, process_limit(current, user_cap));
  return cap > current.rss ? cap - current.rss : 0;
}

void Budget::require(const char* stage, Bytes forecast) const {
  auto current = inspect();
  Bytes cap = std::min(initial_limit, process_limit(current, user_cap));
  if (std::max(forecast, current.rss) > cap && release_free_heap()) {
    const Bytes before = current.rss;
    current = inspect();
    cap = std::min(initial_limit, process_limit(current, user_cap));
    report_trim(stage, before, current.rss, std::max(forecast, current.rss) <= cap);
  }
  sic::support::admission::record("memory", stage,
                                  std::max(forecast, current.rss) <= cap ? "admit" : "refuse",
                                  current.rss, forecast, cap);
  if (std::max(forecast, current.rss) > cap)
    throw AdmissionError(std::string("resource admission rejected before ") + stage +
                         ": forecast=" + std::to_string(forecast) +
                         " bytes, process budget=" + std::to_string(cap) + " bytes");
}

void Budget::require_additional(const char* stage, Bytes extra) const {
  auto current = inspect();
  Bytes forecast = add(current.rss, extra);
  Bytes cap = std::min(initial_limit, process_limit(current, user_cap));
  if (forecast > cap && release_free_heap()) {
    const Bytes before = current.rss;
    current = inspect();
    forecast = add(current.rss, extra);
    cap = std::min(initial_limit, process_limit(current, user_cap));
    report_trim(stage, before, current.rss, forecast <= cap);
  }
  sic::support::admission::record("memory-additional", stage, forecast <= cap ? "admit" : "refuse",
                                  current.rss, extra, cap);
  if (forecast > cap)
    throw AdmissionError(std::string("resource admission rejected before ") + stage +
                         ": RSS+workspace=" + std::to_string(forecast) +
                         " bytes, process budget=" + std::to_string(cap) + " bytes");
}

void Budget::report_trim(const char* stage, Bytes before, Bytes after, bool admitted) {
  sic::support::progressf(
      "admission heap trim: stage=%s rss_before=%llu rss_after=%llu admitted=%d\n", stage,
      static_cast<unsigned long long>(before), static_cast<unsigned long long>(after),
      admitted ? 1 : 0);
  std::fflush(stdout);
}

int Budget::workers(int requested, Bytes resident, Bytes per_worker) const {
  if (requested <= 0 || !per_worker) throw std::runtime_error("invalid worker budget");
  const Bytes cap = limit();
  const bool fits = resident < cap && per_worker <= cap - resident;
  sic::support::admission::record("workers", "class worker", fits ? "admit" : "refuse", resident,
                                  per_worker, cap);
  if (!fits) throw AdmissionError("resource admission rejected: no class worker fits");
  return static_cast<int>(std::min<Count>(requested, (cap - resident) / per_worker));
}
}  // namespace sic::support::resources
