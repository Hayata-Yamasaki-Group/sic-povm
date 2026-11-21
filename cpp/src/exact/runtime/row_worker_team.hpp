// SPDX-License-Identifier: AGPL-3.0-or-later
// Reusable independent-row workers. The caller retains serial reductions and
// supplies an explicit share of its class/kernel CPU allocation.
#pragma once
#include <stdexcept>

#include "sic/support/stop.hpp"
#include "support/admitted_worker_team.hpp"

namespace sic::exact {

// Static row partitions over the dispatcher's shared team. The caller keeps
// reductions in row order. Nested calls execute inline; overlapping external
// dispatches and destruction during a dispatch are not permitted.
// Facade destruction joins idle native workers; other live facades retain valid
// dispatcher references and regrow their workers lazily on the next dispatch.
class RowWorkerTeam {
 public:
  explicit RowWorkerTeam(int count);
  ~RowWorkerTeam();
  RowWorkerTeam(const RowWorkerTeam&) = delete;
  RowWorkerTeam& operator=(const RowWorkerTeam&) = delete;

  template <class Function>
  void for_each(long size, Function&& function);
  template <class Function>
  void for_each_indexed(long size, Function&& function);
  int size() const noexcept { return count_; }
  // Recheck lazy growth before entering an owning algorithm's startup boundary.
  void prepare();

 private:
  int count_;
  sic::support::WorkerTeam* team_ = nullptr;
};

}  // namespace sic::exact

#include "exact/runtime/row_worker_team.tpp"
