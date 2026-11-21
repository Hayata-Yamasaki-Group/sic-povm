// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/runtime/row_worker_team.hpp"

#include "support/admitted_worker_team.hpp"

namespace sic::exact {
RowWorkerTeam::RowWorkerTeam(int count) : count_(count) {
  if (count <= 0) throw std::invalid_argument("row worker count must be positive");
  prepare();
}
RowWorkerTeam::~RowWorkerTeam() {
  if (team_ && !sic::support::WorkerTeam::inside()) team_->shutdown();
}
void RowWorkerTeam::prepare() {
  if (count_ > 1 && !sic::support::WorkerTeam::inside()) {
    team_ = &sic::support::local_worker_team();
    team_->prepare(count_);
  }
}
}  // namespace sic::exact
