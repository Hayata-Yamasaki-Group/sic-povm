// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::exact {
template <class Function>
void RowWorkerTeam::for_each(long size, Function&& function) {
  for_each_indexed(size, [&](long row, int) { function(row); });
}

template <class Function>
void RowWorkerTeam::for_each_indexed(long size, Function&& function) {
  if (size < 0) throw std::invalid_argument("negative row worker range");
  sic::support::check_stop();
  auto run = [&](unsigned id) {
    const long first = size / count_ * id + (size % count_) * id / count_;
    const long end = size / count_ * (id + 1) + (size % count_) * (id + 1) / count_;
    for (long row = first; row < end; ++row) function(row, static_cast<int>(id));
  };
  if (count_ == 1) {
    for (long row = 0; row < size; ++row) function(row, 0);
  } else {
    auto& team = sic::support::local_worker_team();
    // A facade created inline can acquire real workers on a later external call.
    if (!sic::support::WorkerTeam::inside()) team_ = &team;
    team.run(count_, run, {}, sic::support::WorkerTeam::CacheLifetime::retained);
  }
}
}  // namespace sic::exact
