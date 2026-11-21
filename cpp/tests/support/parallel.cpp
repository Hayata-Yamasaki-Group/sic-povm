// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/runtime/parallel.hpp"

#include <array>
#include <barrier>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include "assertions.hpp"
#include "exact/runtime/row_worker_team.hpp"
#include "sic/error.hpp"
#include "support/admitted_worker_team.hpp"
#include "temporary_directory.hpp"

namespace {
struct NestedFailure : sic::Error {
  NestedFailure() : sic::Error(sic::ErrorKind::precision, "nested state fixture") {}
};
void budgeted_nesting() {
  for (int budget : {1, 2, 4, 8}) {
    std::array<std::array<std::atomic<unsigned>, 8>, 2> visits{};
    std::barrier outer_gate(2);
    std::atomic<bool> bad{false};
    sic::exact::parallel_for(2, 16, [&](long outer) {
      outer_gate.arrive_and_wait();
      sic::exact::ClassNativeShare share(budget, 16, 2);
      const auto owner = std::this_thread::get_id();
      std::barrier inner_gate(budget);
      // A barrier on the first item per worker detects accidental serialization.
      sic::exact::parallel_for(budget, 8, [&](long inner) {
        inner_gate.arrive_and_wait();
        ++visits[outer][inner];
        if (budget == 1 && owner != std::this_thread::get_id()) bad = true;
        if (sic::exact::nested_loop_budget != 1) bad = true;
      });
      if (sic::exact::nested_loop_budget != budget) bad = true;
    });
    for (const auto& row : visits)
      for (int q = 0; q < 8; ++q) SIC_CHECK(row[q] == (q < budget ? 1u : 0u));
    SIC_CHECK(!bad);
  }
}
void nested_state_contract() {
  flint_set_num_threads(16);
  std::atomic<bool> bad{false};
  sic::exact::parallel_for(2, 16, [&](long) {
    const auto thread = std::this_thread::get_id();
    const auto physical = sic::support::WorkerTeam::worker_id();
    sic::exact::ClassNativeShare share(8, 16, 2);
    auto restored = [&] {
      if (flint_get_num_threads() != 8 || sic::exact::nested_loop_budget != 8 ||
          std::this_thread::get_id() != thread || sic::support::WorkerTeam::worker_id() != physical)
        bad = true;
    };
    for (long count : {1, 3, 8}) {
      std::vector<std::atomic<unsigned>> visits(count);
      std::barrier gate(std::min<long>(8, count));
      sic::exact::parallel_for(count, 8, [&](long index) {
        ++visits[index];
        gate.arrive_and_wait();
        const int expected = count == 1 ? 8 : std::max<int>(1, 8 / count);
        if (flint_get_num_threads() != (count == 1 ? 8 : 1) ||
            sic::exact::nested_loop_budget != expected ||
            (count == 1 && std::this_thread::get_id() != thread))
          bad = true;
      });
      for (const auto& count : visits)
        if (count != 1) bad = true;
      restored();
    }
    bool caught = false;
    try {
      sic::exact::parallel_for(19, 8, [&](long i) {
        if (flint_get_num_threads() != 1 || sic::exact::nested_loop_budget != 1) bad = true;
        if (i == 7) throw NestedFailure();
      });
    } catch (const NestedFailure& e) {
      caught = e.kind() == sic::ErrorKind::precision;
    }
    if (!caught) bad = true;
    restored();
    {
      sic::support::KernelSettings invalid{{"SIC_PARALLEL_GRAIN", "0"}};
      sic::support::KernelSettingsScope settings(&invalid);
      caught = false;
      try {
        sic::exact::parallel_for(8, 8, [&](long) { bad = true; });
      } catch (const sic::Error& error) {
        caught = error.kind() == sic::ErrorKind::invalid_argument;
      }
      if (!caught) bad = true;
    }
    restored();
    {
      sic::support::StopToken token;
      sic::support::StopScope stop(&token);
      std::atomic<long> completed{0};
      caught = false;
      try {
        sic::exact::parallel_for(200000, 8, [&](long) {
          if (flint_get_num_threads() != 1 || sic::exact::nested_loop_budget != 1) bad = true;
          if (++completed == 1000) token.request_stop();
        });
      } catch (const sic::Error& e) {
        caught = e.kind() == sic::ErrorKind::cancelled;
      }
      if (!caught || completed < 1000 || completed >= 200000) bad = true;
      restored();
    }
    sic::exact::RowWorkerTeam rows(3);
    caught = false;
    try {
      rows.for_each(17, [&](long i) {
        if (flint_get_num_threads() != 1 || std::this_thread::get_id() != thread ||
            sic::support::WorkerTeam::worker_id() != physical)
          bad = true;
        if (i == 7) throw NestedFailure();
      });
    } catch (const NestedFailure& e) {
      caught = e.kind() == sic::ErrorKind::precision;
    }
    if (!caught) bad = true;
    restored();
    sic::support::local_worker_team().run(3, [&](unsigned) {
      if (flint_get_num_threads() != 1 || std::this_thread::get_id() != thread ||
          sic::support::WorkerTeam::worker_id() != physical)
        bad = true;
    });
    restored();
  });
  SIC_CHECK(!bad);
  flint_set_num_threads(3);
}
}  // namespace

int main() {
  flint_set_num_threads(3);
  bool caught = false;
  try {
    sic::exact::parallel_for(100, 4, [](long i) {
      if (i == 3) throw std::runtime_error("worker failure");
    });
  } catch (const std::runtime_error& error) {
    caught = std::string(error.what()) == "worker failure";
  }
  SIC_CHECK(caught);
  SIC_CHECK(flint_get_num_threads() == 3);
  sic_test::TemporaryDirectory scratch("sic-stop");
  const auto stop_path = scratch.path / "stop";
  {
    std::ofstream stop(stop_path);
    stop << "stop\n";
  }
  sic::support::ScopedStopFile stop_scope(stop_path.c_str());
  caught = false;
  try {
    sic::exact::parallel_for(100, 2, [](long) {});
  } catch (const std::runtime_error& error) {
    caught = std::string(error.what()).find("requested stop") != std::string::npos;
  }
  SIC_CHECK(caught);

  std::filesystem::remove(stop_path);
  SIC_CHECK(flint_get_num_threads() == 3);
  caught = false;
  try {
    sic::exact::parallel_for(8, 2, [](long i) {
      sic::exact::parallel_for(8, 2, [&](long j) {
        if (i == 3 && j == 2) throw std::runtime_error("nested failure");
      });
    });
  } catch (const std::runtime_error& error) {
    caught = std::string(error.what()) == "nested failure";
  }
  SIC_CHECK(caught);
  std::atomic<long> sum{0};
  std::atomic<bool> nested_threads{false};
  sic::exact::parallel_for(100, 4, [&](long i) {
    if (flint_get_num_threads() != 1) nested_threads = true;
    sum += i;
  });
  SIC_CHECK(sum == 4950);
  SIC_CHECK(!nested_threads);
  SIC_CHECK(flint_get_num_threads() == 3);
  const char* old_grain = std::getenv("SIC_PARALLEL_GRAIN");
  const bool had_grain = old_grain != nullptr;
  const std::string saved_grain = old_grain ? old_grain : "";
  for (const char* grain : {"1", "7", "1024"}) {
    setenv("SIC_PARALLEL_GRAIN", grain, 1);
    std::vector<std::atomic<int>> visits(10003);
    sic::exact::parallel_for(visits.size(), 7, [&](long i) { ++visits[i]; });
    for (const auto& count : visits) SIC_CHECK(count == 1);
    caught = false;
    try {
      sic::exact::parallel_for(10003, 7, [](long i) {
        if (i == 4097) throw std::runtime_error("chunk failure");
      });
    } catch (const std::runtime_error& error) {
      caught = std::string(error.what()) == "chunk failure";
    }
    SIC_CHECK(caught);
    SIC_CHECK(flint_get_num_threads() == 3);
  }
  if (had_grain)
    setenv("SIC_PARALLEL_GRAIN", saved_grain.c_str(), 1);
  else
    unsetenv("SIC_PARALLEL_GRAIN");
  for (int workers : {1, 2, 3, 8, 16}) {
    std::vector<unsigned long> values(4097);
    for (unsigned pass = 0; pass < 50; ++pass) {
      sic::exact::parallel_for(values.size(), workers, [&](long i) {
        values[i] = static_cast<unsigned long>(i) * i + pass;
        const auto owner = std::this_thread::get_id();
        sic::exact::parallel_for(3, workers, [&](long) {
          if (std::this_thread::get_id() != owner) std::abort();
        });
      });
      unsigned long ordered_sum = 0, expected = 0;
      for (size_t i = 0; i < values.size(); ++i) {
        ordered_sum += values[i];
        expected += i * i + pass;
      }
      SIC_CHECK(ordered_sum == expected);
    }
  }
  {
    sic::support::StopToken token;
    sic::support::StopScope scope(&token);
    std::atomic<long> completed{0};
    caught = false;
    try {
      sic::exact::parallel_for(1000000, 8, [&](long) {
        if (++completed == 1000) token.request_stop();
      });
    } catch (const sic::Error& error) {
      caught = error.kind() == sic::ErrorKind::cancelled;
    }
    SIC_CHECK(caught);
    SIC_CHECK(completed >= 1000 && completed < 1000000);
  }
  sic::exact::parallel_for(100, 8, [](long) {});
  budgeted_nesting();
  nested_state_contract();
  flint_set_num_threads(1);
  return sic_test::report();
}
