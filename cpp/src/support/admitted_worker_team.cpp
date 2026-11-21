// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/admitted_worker_team.hpp"

#include <memory>
#include <vector>

#include "support/resources.hpp"

namespace sic::support {
namespace {
std::vector<std::unique_ptr<WorkerTeam>>& local_teams() {
  thread_local std::vector<std::unique_ptr<WorkerTeam>> teams;
  return teams;
}
}  // namespace
WorkerTeam& local_worker_team() {
  auto& teams = local_teams();
  const auto depth = WorkerTeam::depth();
  if (teams.size() <= depth) teams.resize(depth + 1);
  if (!teams[depth])
    teams[depth] = std::make_unique<WorkerTeam>([](unsigned additional) {
      resources::Budget().require_additional("worker team stacks/context",
                                             resources::mul(additional, resources::kMib));
    });
  return *teams[depth];
}
void release_local_worker_teams() noexcept {
  if (WorkerTeam::inside()) return;
  for (auto& team : local_teams())
    if (team) team->shutdown();
}
}  // namespace sic::support
