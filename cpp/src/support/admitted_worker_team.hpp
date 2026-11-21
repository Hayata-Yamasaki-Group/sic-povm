// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "support/runtime/worker_team.hpp"

namespace sic::support {
// One memory-admitted team per calling thread and nesting depth. Explicit worker
// counts have already been resolved by the process-allowance/option owner.
// Workers spin for at most 50 microseconds between dispatches, then sleep.
WorkerTeam& local_worker_team();
// Between external phases, release idle native thread resources, including their
// allocator/stack state. Facades keep valid team references and regrow lazily.
// A call from an active worker callback does nothing.
void release_local_worker_teams() noexcept;
}  // namespace sic::support
