// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/runtime/worker_team.hpp"

#include <array>
#include <barrier>
#include <chrono>
#include <numeric>
#if defined(__linux__)
#include <sched.h>
#endif

#include <thread>

#include "assertions.hpp"
#include "exact/runtime/parallel.hpp"
#include "exact/runtime/row_worker_team.hpp"
#include "sic/error.hpp"
#include "sicx/container/verify.tpp"
#include "support/admitted_worker_team.hpp"
#include "support/resources.hpp"
#include "support/resources/checked_bytes.hpp"

namespace {
struct WorkerFailure : sic::Error {
  std::shared_ptr<int> request;
  explicit WorkerFailure(std::shared_ptr<int> value)
      : sic::Error(sic::ErrorKind::integrity, "worker fixture"), request(std::move(value)) {}
};
thread_local bool cache_live = false;
std::atomic<unsigned> cleanups{0};
void clean_cache() {
  cache_live = false;
  ++cleanups;
}
void mark_cache() {
  if (!cache_live) {
    cache_live = true;
    flint_register_cleanup_function(clean_cache);
  }
}
void cache_lifetimes() {
  cleanups = 0;
  {
    sic::support::WorkerTeam team;
    for (unsigned pass = 0; pass < 3; ++pass)
      team.run(
          4,
          [&](unsigned id) {
            if (id) {
              if (cache_live != (pass != 0)) std::abort();
              mark_cache();
            }
          },
          {}, sic::support::WorkerTeam::CacheLifetime::retained);
    SIC_CHECK(cleanups == 0);
  }
  SIC_CHECK(cleanups == 3);
  cleanups = 0;
  {
    sic::exact::RowWorkerTeam team(4);
    for (unsigned pass = 0; pass < 3; ++pass)
      team.for_each_indexed(4, [&](long, int id) {
        if (id) {
          if (cache_live != (pass != 0)) std::abort();
          mark_cache();
        }
      });
    SIC_CHECK(cleanups == 0);
  }
  SIC_CHECK(cleanups == 3);
  SIC_CHECK(sic::support::local_worker_team().size() == 1);
  cleanups = 0;
  {
    sic::support::WorkerTeam outer;
    std::unique_ptr<sic::exact::RowWorkerTeam> rows;
    outer.run(1, [&](unsigned) { rows = std::make_unique<sic::exact::RowWorkerTeam>(4); });
    for (unsigned pass = 0; pass < 3; ++pass)
      rows->for_each_indexed(4, [&](long, int id) {
        if (id) {
          if (cache_live != (pass != 0)) std::abort();
          mark_cache();
        }
      });
    SIC_CHECK(cleanups == 0);
    rows.reset();
    SIC_CHECK(cleanups == 3);
    SIC_CHECK(sic::support::local_worker_team().size() == 1);
  }
  cleanups = 0;
  std::barrier gate(4);
  sic::exact::parallel_for(4, 4, [&](long) {
    if (sic::support::WorkerTeam::worker_id()) mark_cache();
    gate.arrive_and_wait();
  });
  SIC_CHECK(cleanups == 3);
}
void idle_phase_release() {
  auto& shared = sic::support::local_worker_team();
  sic::support::release_local_worker_teams();
  SIC_CHECK(shared.size() == 1);
  cleanups = 0;
  {
    sic::exact::RowWorkerTeam rows(4);
    rows.for_each_indexed(4, [&](long, int id) {
      if (id) mark_cache();
      // Releasing from a callback must not stop or join its active team.
      sic::support::release_local_worker_teams();
    });
    SIC_CHECK(shared.size() == 4);
    SIC_CHECK(cleanups == 0);
    sic::support::release_local_worker_teams();
    SIC_CHECK(shared.size() == 1);
    SIC_CHECK(cleanups == 3);
    rows.for_each_indexed(4, [&](long, int id) {
      if (id) {
        if (cache_live) std::abort();
        mark_cache();
      }
    });
    SIC_CHECK(shared.size() == 4);
    { sic::exact::RowWorkerTeam temporary(4); }
    SIC_CHECK(shared.size() == 1);
    SIC_CHECK(cleanups == 6);
    rows.for_each_indexed(4, [&](long, int id) {
      if (id) {
        if (cache_live) std::abort();
        mark_cache();
      }
    });
    SIC_CHECK(shared.size() == 4);
  }
  SIC_CHECK(cleanups == 9);
  sic::support::release_local_worker_teams();
  SIC_CHECK(shared.size() == 1);
  std::array<unsigned, 4> visits{};
  shared.run(4, [&](unsigned id) { ++visits[id]; });
  SIC_CHECK((visits == std::array<unsigned, 4>{1, 1, 1, 1}));
}
void refused_growth() {
  static bool refuse = false;
  sic::support::WorkerTeam team([](unsigned) {
    if (refuse) throw sic::support::resources::AdmissionError("fixture refused growth");
  });
  team.run(3, [](unsigned) {});
  refuse = true;
  bool caught = false;
  try {
    team.run(8, [](unsigned) { std::abort(); });
  } catch (const sic::support::resources::AdmissionError& error) {
    caught = error.kind() == sic::ErrorKind::resource;
  }
  SIC_CHECK(caught);
  SIC_CHECK(team.size() == 3);
  std::array<unsigned, 3> visits{};
  team.run(3, [&](unsigned id) { ++visits[id]; });
  SIC_CHECK((visits == std::array<unsigned, 3>{1, 1, 1}));
  refuse = false;
  team.run(8, [](unsigned) {});
  SIC_CHECK(team.size() == 8);
}
void callback_shutdown() {
  sic::support::WorkerTeam team;
  for (unsigned caller : {0, 1, 3}) {
    std::array<unsigned, 4> visits{};
    team.run(4, [&](unsigned id) {
      if (id == caller) {
        bool refused = false;
        try {
          team.shutdown();
        } catch (const std::logic_error&) {
          refused = true;
        }
        SIC_CHECK(refused);
      }
      ++visits[id];
    });
    SIC_CHECK((visits == std::array<unsigned, 4>{1, 1, 1, 1}));
    SIC_CHECK(team.size() == 4);
  }
  team.shutdown();
  SIC_CHECK(team.size() == 1);
  team.run(4, [](unsigned) {});
}
void refused_regrowth() {
  auto& team = sic::support::local_worker_team();
  sic::exact::RowWorkerTeam surviving(4);
  for (bool facade_release : {false, true}) {
    if (facade_release) {
      sic::exact::RowWorkerTeam temporary(4);
    } else {
      sic::support::release_local_worker_teams();
    }
    SIC_CHECK(team.size() == 1);
    {
      // A later admission must measure the current memory ceiling again.
      sic::support::KernelSettings limited{{"SIC_MEMORY_MB", "1"}};
      sic::support::KernelSettingsScope scope(&limited);
      bool refused = false;
      try {
        surviving.for_each(4, [](long) { std::abort(); });
      } catch (const sic::support::resources::AdmissionError& error) {
        refused = error.kind() == sic::ErrorKind::resource;
      }
      SIC_CHECK(refused);
      SIC_CHECK(team.size() == 1);
    }
    std::array<unsigned, 4> visits{};
    surviving.for_each(4, [&](long i) { ++visits[i]; });
    SIC_CHECK((visits == std::array<unsigned, 4>{1, 1, 1, 1}));
    SIC_CHECK(team.size() == 4);
  }
}
void growth_affinity() {
#if defined(__linux__)
  cpu_set_t allowance;
  SIC_CHECK(sched_getaffinity(0, sizeof allowance, &allowance) == 0);
  // Construction captures the process allowance before the owner narrows its mask.
  sic::support::WorkerTeam team;
  cpu_set_t pinned;
  CPU_ZERO(&pinned);
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
    if (CPU_ISSET(cpu, &allowance)) {
      CPU_SET(cpu, &pinned);
      break;
    }
  SIC_CHECK(sched_setaffinity(0, sizeof pinned, &pinned) == 0);
  for (unsigned width : {2, 4, 8}) {
    std::atomic<bool> bad{false};
    team.run(width, [&](unsigned id) {
      cpu_set_t actual;
      if (sched_getaffinity(0, sizeof actual, &actual) ||
          !CPU_EQUAL(&actual, id ? &allowance : &pinned))
        bad = true;
    });
    SIC_CHECK(!bad);
  }
  SIC_CHECK(sched_setaffinity(0, sizeof allowance, &allowance) == 0);
#endif
}
void exercise(unsigned count) {
  sic::support::WorkerTeam team;
  std::vector<std::thread::id> owners(count);
  std::vector<std::vector<unsigned>> order(count);
  for (unsigned pass = 0; pass < 1000; ++pass) {
    // Exceed the 50 microsecond spin window before the next publication.
    if (pass % 100 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    team.run(count, [&](unsigned id) {
      if (!pass) owners[id] = std::this_thread::get_id();
      if (owners[id] != std::this_thread::get_id() || sic::support::WorkerTeam::worker_id() != id ||
          !sic::support::WorkerTeam::inside())
        std::abort();
      order[id].push_back(pass);
      // A nested dispatch on either this team or another facade stays inline.
      const auto owner = std::this_thread::get_id();
      team.run(3, [&](unsigned) {
        if (std::this_thread::get_id() != owner || sic::support::WorkerTeam::worker_id() != id)
          std::abort();
      });
    });
  }
  for (unsigned id = 0; id < count; ++id) {
    SIC_CHECK(order[id].size() == 1000);
    for (unsigned q = 0; q < 1000; ++q) SIC_CHECK(order[id][q] == q);
    for (unsigned other = 0; other < id; ++other) SIC_CHECK(owners[id] != owners[other]);
  }
  auto request = std::make_shared<int>(11);
  std::weak_ptr<int> lifetime = request;
  bool caught = false;
  try {
    team.run(count, [&](unsigned id) {
      if (id == count - 1) throw WorkerFailure(request);
    });
  } catch (const WorkerFailure& e) {
    caught = e.kind() == sic::ErrorKind::integrity && e.request == request;
  }
  SIC_CHECK(caught);
  request.reset();
  SIC_CHECK(lifetime.expired());
  unsigned sum = 0;
  team.run(1, [&](unsigned) { sum = 23; });
  SIC_CHECK(sum == 23);
  team.shutdown();
  team.run(count, [](unsigned) {});
}
}  // namespace
int main() {
  growth_affinity();
  refused_growth();
  callback_shutdown();
  refused_regrowth();
  cache_lifetimes();
  idle_phase_release();
  for (unsigned workers : {1, 3, 8, 16}) {
    std::vector<std::atomic<unsigned>> visits(10003);
    sic::sicx::parallel_for(visits.size(), workers, [&](size_t i) { ++visits[i]; });
    for (const auto& count : visits) SIC_CHECK(count == 1);
  }
  for (unsigned count : {1, 2, 3, 8, 16}) exercise(count);
  sic::support::WorkerTeam team;
  const std::array<unsigned, 3> selected{0, 2, 5};
  std::array<unsigned, 6> visits{};
  team.run(
      3, [&](unsigned id) { ++visits[id]; }, selected);
  SIC_CHECK((visits == std::array<unsigned, 6>{1, 0, 1, 0, 0, 1}));
  return sic_test::report();
}
