// SPDX-License-Identifier: AGPL-3.0-or-later
// Resource discovery and conservative allocation forecasts. These are launch
// controls, not certified arithmetic bounds or a replacement for a process cap.
#pragma once
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

#include "index.hpp"
#include "sic/error.hpp"
#include "sic/support/admission.hpp"
#include "sic/support/kernel_settings.hpp"
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

#include "support/resources/allocation_forecast.hpp"
#include "support/resources/checked_bytes.hpp"
#include "support/resources/platform_snapshot.hpp"
#include "support/resources/snapshot.hpp"

namespace sic::support::resources {

// mountinfo encodes spaces and other special characters as octal escapes.

// Use only free RAM on CPU-affinity nodes for first-touch allocations. Reclaimable cache
// is deliberately excluded from this local bound; the global MemAvailable and cgroup caps
// still apply. Missing NUMA sysfs (e.g. a restricted container) leaves the global bound intact.

Bytes process_limit(const Snapshot& snapshot, std::optional<Bytes> user_cap = {});

// Return free allocator pages to the kernel. Arb limbs freed by worker
// threads stay resident in their glibc arenas until a trim, so an RSS-based
// admission would count freed memory as live.
// Returns false where no trim is available.

struct Budget {
  std::optional<Bytes> user_cap;
  Bytes initial_limit;
  Snapshot initial;

  Budget();

  Bytes limit() const;
  Bytes remaining() const;
  // A forecast over the cap first trims the heap and measures again. The cap
  // itself is unchanged; only resident pages the allocator already freed can
  // be returned, so the check stays fail-closed against the measured RSS.
  void require(const char* stage, Bytes forecast) const;
  void require_additional(const char* stage, Bytes extra) const;
  static void report_trim(const char* stage, Bytes before, Bytes after, bool admitted);
  int workers(int requested, Bytes resident, Bytes per_worker) const;
};

// Limb payload plus per-scalar allocator allowance. Forecasts deliberately
// retain zero components, temporary copies and the general unpaired fallback.

// Shared power-of-two plan/root arrays, independent reconstruction rows,
// bounded scalar multiply temporaries, and owned worker stacks. FLINT3.6's
// serial radix-two transform has no hidden row-sized scratch.

// FLINT block multiplication packs wider integers than the ball mantissas.
// Keep a native-workspace margin; this remains an allocation forecast.

// A current point Jacobian, two possible full row/frequency caches, rounded
// state/context and the independently owned correction-vector workspace.

}  // namespace sic::support::resources
