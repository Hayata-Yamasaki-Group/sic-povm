// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <span>

#include "exact/runtime/parallel.hpp"
#include "support/resources.hpp"
#include "support/runtime/reservations.hpp"

namespace sic::construct {
// Immutable allocation for one callback, including its native TLS share.
struct ClassAllocation {
  int classes = 1, workers = 1;
  sic::support::resources::Bytes private_bytes = 0, callback_bytes = 0;
  int live = 0;
};

// Choose a CPU split without borrowing any still-live callback tokens.
std::optional<ClassAllocation> class_allocation(int total, int maximum, int live, int available,
                                                int selection_limit);

// The coordinator alone prepares seeds, assigns CPU tokens and admits owners.
// A refusal waits only while a live owner can release capacity; after the last
// owner finishes it is reported once for the ordered serial drain/fallback.
// Prepare uses at most allocation.workers; callback memory is freshly granted
// after preparation. At most one unlaunched prepared seed exists.
template <class Plan, class Prepare, class Requirement, class Refused, class Callback>
void dispatch_live_classes(std::span<const long> classes, int workers,
                           sic::support::Reservations* pool, Plan plan, Prepare prepare,
                           Requirement requirement, Refused refused, Callback callback);
}  // namespace sic::construct

#include "construct/stages/table/class_schedule.tpp"
