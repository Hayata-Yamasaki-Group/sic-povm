// SPDX-License-Identifier: AGPL-3.0-or-later
#include "temporary_directory.hpp"
// Worker-local ball reuse, fail/join and live cancellation contracts.
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>

#include "cpu_allowance_expectation.hpp"
#include "decode/placement.hpp"
#include "decode/pool.hpp"
#include "sic/support/ball_vectors.hpp"

using sic::support::ComplexBallVector;
namespace {
void require(bool pass, const char* message) {
  if (!pass) throw std::runtime_error(message);
}
thread_local bool cache_live = false;
std::atomic<unsigned> cache_cleanups{0};
void cache_cleanup() {
  cache_live = false;
  ++cache_cleanups;
}
void cache_lifetime() {
  {
    sic::decode::Pool pool(4, sic::decode::Placement::detect(false));
    for (unsigned pass = 0; pass < 3; ++pass) {
      std::barrier gate(4);
      pool.run(
          4,
          [&](size_t) {
            gate.arrive_and_wait();
            if (sic::decode::Pool::worker_id()) {
              require(cache_live == (pass != 0), "decoder cache lifetime");
              if (!cache_live) {
                cache_live = true;
                flint_register_cleanup_function(cache_cleanup);
              }
            }
          },
          1);
      require(cache_cleanups == 0, "decoder cache cleaned before facade destruction");
    }
  }
  require(cache_cleanups == 3, "decoder destruction did not clean worker caches");
}
void cross_component_nesting() {
  const auto placement = sic::decode::Placement::detect(true);
  sic::support::WorkerTeam outer;
  std::barrier inner_gate(8);
  std::atomic<unsigned> inner_seen{0};
  outer.run(8, [&](unsigned outer_id) {
    inner_gate.arrive_and_wait();
    inner_seen.fetch_or(1u << outer_id);
    cpu_set_t before, after;
    require(sched_getaffinity(0, sizeof before, &before) == 0, "nested affinity capture");
    sic::decode::Pool inner(2, placement);
    std::vector<unsigned> scratch(2);
    inner.run(37, [&](size_t) {
      require(sic::decode::Pool::worker_id() == 0, "unrelated scratch ID inherited");
      ++scratch.at(sic::decode::Pool::worker_id());
      inner.run(3, [&](size_t) { ++scratch.at(sic::decode::Pool::worker_id()); });
    });
    require(scratch[0] == 148 && scratch[1] == 0, "nested scratch dispatch");
    require(sched_getaffinity(0, sizeof after, &after) == 0 && CPU_EQUAL(&before, &after),
            "nested decoder changed outer affinity");
  });
  require(inner_seen == 255, "nested construction did not exercise every outer worker");
  sic::decode::Pool owned(2, sic::decode::Placement::detect(false));
  std::atomic<unsigned> visits{0};
  std::atomic<unsigned> refused{0};
  std::vector<unsigned> shared_scratch(2);
  std::barrier foreign_gate(8);
  outer.run(8, [&](unsigned outer_id) {
    const auto thread = std::this_thread::get_id();
    foreign_gate.arrive_and_wait();
    const auto body = [&](size_t) {
      require(std::this_thread::get_id() == thread, "foreign nested dispatcher started workers");
      require(sic::decode::Pool::worker_id() == 0, "foreign nested scratch ID");
      ++shared_scratch.at(sic::decode::Pool::worker_id());
      ++visits;
    };
    for (bool grouped : {false, true}) {
      bool rejected = false;
      try {
        if (grouped)
          owned.run_grouped(37, body);
        else
          owned.run(4, body);
      } catch (const std::logic_error&) {
        rejected = true;
        ++refused;
      }
      require(rejected == (outer_id != 0), "foreign team scratch ownership was not enforced");
      require(sic::decode::Pool::worker_id() == outer_id, "foreign caller scratch ID not restored");
    }
  });
  require(visits == 41 && refused == 14 && shared_scratch[0] == 41 && shared_scratch[1] == 0,
          "shared foreign pool touched non-owner scratch");
  sic::decode::Pool second(3, sic::decode::Placement::detect(false));
  std::atomic<unsigned> second_calls{0}, second_refusals{0};
  visits = 0;
  std::barrier pool_gate(2);
  owned.run(
      2,
      [&](size_t) {
        pool_gate.arrive_and_wait();
        const auto thread = std::this_thread::get_id();
        const auto id = sic::decode::Pool::worker_id();
        try {
          second.run(5, [&](size_t) {
            require(std::this_thread::get_id() == thread && sic::decode::Pool::worker_id() == 0,
                    "owner pool-in-pool must use inline scratch zero");
            ++visits;
          });
          require(id == 0, "foreign pool accepted a non-owner worker");
          ++second_calls;
        } catch (const std::logic_error&) {
          require(id != 0, "foreign pool refused its owner");
          ++second_refusals;
        }
        require(sic::decode::Pool::worker_id() == id, "pool-in-pool did not restore scratch ID");
      },
      1);
  require(second_calls == 1 && second_refusals == 1 && visits == 5,
          "pool-in-pool ownership lost work");
  std::barrier nesting_gate(2);
  owned.run(
      2,
      [&](size_t) {
        nesting_gate.arrive_and_wait();
        const auto id = sic::decode::Pool::worker_id();
        owned.run(
            3, [&](size_t) { require(sic::decode::Pool::worker_id() == id, "scratch ID lost"); });
      },
      1);
  // A pool created in an inline owner callback can later initialize normally.
  std::unique_ptr<sic::decode::Pool> deferred;
  outer.run(1, [&](unsigned) {
    deferred = std::make_unique<sic::decode::Pool>(2, sic::decode::Placement::detect(false));
  });
  deferred->run(64, [](size_t) {});
}
void budgeted_foreign_nesting() {
  sic::decode::Pool pool(2, sic::decode::Placement::detect(false));
  std::atomic<unsigned> arrived{0};
  auto gate = [&] {
    const unsigned target = (arrived.fetch_add(1) / 2 + 1) * 2;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (arrived.load() < target && std::chrono::steady_clock::now() < deadline)
      std::this_thread::yield();
    require(arrived.load() >= target, "foreign nesting barrier timed out");
  };
  std::atomic<unsigned> seen{0};
  pool.run(
      2,
      [&](size_t) {
        const unsigned id = sic::decode::Pool::worker_id();
        seen.fetch_or(1u << id);
        // Force both pool scratch slots to remain live throughout foreign dispatch.
        gate();
        sic::support::WorkerTeam foreign;
        foreign.run(
            2,
            [&](unsigned local) {
              if (local) return;
              pool.run(3, [&](size_t) {
                require(sic::decode::Pool::worker_id() == id,
                        "budgeted foreign team replaced pool scratch identity");
              });
            },
            {}, sic::support::WorkerTeam::CacheLifetime::region,
            sic::support::WorkerTeam::NestedDispatch::budgeted);
        require(sic::decode::Pool::worker_id() == id, "foreign team did not restore identity");
        gate();
      },
      1);
  require(seen == 3, "foreign nesting did not exercise both scratch slots");
}
void overwrite_and_failure() {
  auto token = std::make_shared<sic::support::StopToken>();
  sic::support::StopScope stop(token.get());
  sic::decode::MemoryBudget budget(16 << 20, 4);
  sic::decode::ReserveGuard guard(budget, 4 * 4096, "scratch test");
  std::vector<ComplexBallVector> scratch;
  for (unsigned i = 0; i < 4; ++i) scratch.emplace_back(3);
  sic::decode::Pool pool(4, sic::decode::Placement::detect(false));
  // Poison both midpoint and radius, then overwrite at changing precision.
  for (slong bits : {256, 64, 512, 128}) {
    pool.run(64, [&](size_t q) {
      auto& s = scratch[sic::decode::Pool::worker_id()];
      acb_indeterminate(s[0]);
      acb_indeterminate(s[1]);
      acb_indeterminate(s[2]);
      acb_set_ui(s[0], q + 1);
      acb_inv(s[1], s[0], bits);
      acb_mul(s[2], s[1], s[0], bits);
      require(acb_is_finite(s[2]) && arb_contains_si(acb_realref(s[2]), 1) &&
                  arb_contains_zero(acb_imagref(s[2])),
              "stale scratch radius");
    });
  }
  bool failed = false;
  try {
    pool.run(128, [&](size_t q) {
      acb_indeterminate(scratch[sic::decode::Pool::worker_id()][0]);
      if (q == 0) throw std::runtime_error("injected scratch job failure");
    });
  } catch (const std::runtime_error&) {
    failed = true;
  }
  require(failed, "missing worker exception");
  std::atomic<unsigned> completed{0};
  pool.run(128, [&](size_t) {
    auto& s = scratch[sic::decode::Pool::worker_id()];
    acb_one(s[0]);
    require(acb_is_one(s[0]), "failed job contaminated next run");
    ++completed;
  });
  require(completed == 128, "pool did not recover after join");
  bool refused = false;
  try {
    sic::decode::ReserveGuard denied(budget, 32 << 20, "denied scratch");
  } catch (const sic::Error& e) {
    refused = e.kind() == sic::ErrorKind::resource;
  }
  require(refused, "scratch admission did not refuse");
}
void live_group_refusal() {
  sic::decode::MemoryBudget budget(4096, 1);
  bool refused = false;
  try {
    sic::decode::Admission group(budget, 1);
  } catch (const sic::Error& e) {
    refused = e.kind() == sic::ErrorKind::resource;
  }
  require(refused && budget.available() == budget.budget(), "stale first-group RSS admission");
}
void physical_dispatch() {
  sic::decode::Placement absent;
  absent.pin = true;
  absent.cpus = {std::numeric_limits<int>::max() - 1, std::numeric_limits<int>::max() - 1,
                 std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
  require(sic::decode::physical_workers(absent, 4) == std::vector<unsigned>({0}),
          "missing topology did not use the conservative owner fallback");
  absent.pin = false;
  require(sic::decode::physical_workers(absent, 4) == std::vector<unsigned>({0}),
          "no-pin missing topology did not use one core");
  absent.cpus.clear();
  const auto allowance = sic::support::resources::process_cpu_allowance();
  require(allowance == sic_test::expected_cpu_allowance(), "incorrect process allowance");
  const auto defaults = static_cast<unsigned>(std::min<std::uint64_t>(4, allowance));
  const auto empty = sic::decode::physical_workers(absent, defaults);
  require(empty.front() == 0 && empty.size() == defaults, "empty placement lost default workers");
  require(sic::decode::physical_workers(absent, 4).size() == 4,
          "empty placement lost explicit workers");

  const auto allowed = sic::decode::Placement::detect(true);
  const auto distinct =
      sic::decode::physical_workers(allowed, static_cast<unsigned>(allowed.cpus.size()));
  if (distinct.size() >= 2) {
    auto pair = allowed;
    pair.cpus = {allowed.cpus[distinct[0]], allowed.cpus[distinct[1]]};
    pair.node_of = {allowed.node_of[distinct[0]], allowed.node_of[distinct[1]]};
    pair.nodes = pair.node_of[0] == pair.node_of[1] ? 1 : 2;
    require(sic::decode::physical_workers(pair, 8) == std::vector<unsigned>({0, 4}),
            "mapped selection did not retain non-prefix worker IDs");
    sic::decode::Pool pool(8, pair);
    std::barrier selected_gate(2);
    std::atomic<unsigned> selected_mask{0};
    pool.run(
        2,
        [&](size_t) {
          selected_mask.fetch_or(1u << sic::decode::Pool::worker_id());
          selected_gate.arrive_and_wait();
        },
        1, true);
    require(selected_mask == ((1u << 0) | (1u << 4)), "physical dispatch woke the wrong IDs");
    std::barrier ordinary_gate(8);
    std::atomic<unsigned> ordinary_mask{0};
    pool.run(
        8,
        [&](size_t) {
          ordinary_mask.fetch_or(1u << sic::decode::Pool::worker_id());
          ordinary_gate.arrive_and_wait();
        },
        1);
    require(ordinary_mask == 255, "ordinary dispatch lost requested workers");
    for (unsigned round = 0; round < 20; ++round) {
      if (round % 5 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      auto visits = std::make_unique<std::atomic<unsigned>[]>(1024);
      pool.run_grouped(1024, [&](size_t q) { ++visits[q]; });
      pool.run(
          1024, [&](size_t q) { ++visits[q]; }, 1, true);
      pool.run(1024, [&](size_t q) { ++visits[q]; });
      for (unsigned q = 0; q < 1024; ++q)
        require(visits[q] == 3, "selected worker publication/join");
    }
    std::cout << "PASS physical selected IDs 0,4 and ordinary-team reuse\n";
  } else {
    std::cout << "Single-core affinity: non-prefix two-core case unavailable\n";
  }
  // Synthetic NUMA labels, real duplicate CPU: group one has no selected worker.
  auto single = allowed;
  single.cpus = {allowed.cpus[0], allowed.cpus[0]};
  single.node_of = {allowed.node_of[0], allowed.node_of[0] + 1};
  single.nodes = 2;
  sic::decode::Pool pool(8, single);
  require(pool.physical_parallelism() == 1, "duplicate CPU topology was not deduplicated");
  for (unsigned round = 0; round < 20; ++round) {
    auto visits = std::make_unique<std::atomic<unsigned>[]>(64);
    pool.run(
        64, [&](size_t q) { ++visits[q]; }, 1, true);
    pool.run(64, [&](size_t q) { ++visits[q]; });
    for (unsigned q = 0; q < 64; ++q) require(visits[q] == 2, "empty NUMA quota omitted work");
  }
}
void allowance_seams() {
  using namespace sic::support::resources;
  sic_test::TemporaryDirectory work("sic-decoder-cpu");
  const auto fixture = work.path;
  const char* value = std::getenv("SLURM_CPUS_PER_TASK");
  struct Restore {
    std::filesystem::path fixture;
    std::optional<std::string> slurm;
    ~Restore() {
      if (slurm)
        setenv("SLURM_CPUS_PER_TASK", slurm->c_str(), 1);
      else
        unsetenv("SLURM_CPUS_PER_TASK");
      std::filesystem::remove_all(fixture);
    }
  } restore{fixture, value ? std::optional<std::string>(value) : std::nullopt};
  unsetenv("SLURM_CPUS_PER_TASK");
  const auto proc = fixture / "proc", sysfs = fixture / "sysfs";
  std::filesystem::create_directories(proc / "self");
  sic::decode::Placement place;
  place.cpus = {0, 1, 2, 3};
  place.node_of = {0, 0, 0, 0};
  for (int cpu : place.cpus) {
    const auto topology = sysfs / ("cpu" + std::to_string(cpu)) / "topology";
    std::filesystem::create_directories(topology);
    std::ofstream(topology / "physical_package_id") << "0\n";
    std::ofstream(topology / "core_id") << cpu << '\n';
  }
  // Cgroup-v1 cannot resolve v2 ancestry: only the default is reduced.
  std::ofstream(proc / "self/cgroup") << "2:cpu:/job\n";
  std::ofstream(proc / "self/mountinfo") << "1 0 0:1 / /unavailable rw - cgroup cgroup rw\n";
  require(process_cpu_allowance(proc) == 1, "unknown ancestry default must be one");
  for (bool pin : {false, true}) {
    place.pin = pin;
    require(sic::decode::physical_workers(place, 4, sysfs).size() == 4,
            "unknown ancestry clamped explicit physical workers");
    require(sic::decode::physical_workers(place, static_cast<unsigned>(process_cpu_allowance(proc)),
                                          sysfs)
                    .size() == 1,
            "unknown ancestry default physical workers must be one");
  }
  std::ofstream(proc / "self/cgroup") << "0::/\n";
  std::ofstream(proc / "self/mountinfo")
      << "1 0 0:1 / " << fixture.string() << " rw - cgroup2 cgroup rw\n";
  for (const char* malformed :
       {"invalid 100000", "0 100000", "max", "max 0", "100000 100000 trailing"}) {
    std::ofstream(fixture / "cpu.max") << malformed << '\n';
    bool categorized = false;
    try {
      (void)process_cpu_allowance(proc);
    } catch (const sic::Error& error) {
      categorized = error.kind() == sic::ErrorKind::invalid_format;
    }
    require(categorized, "malformed cpu.max did not throw Error(invalid_format)");
  }
  std::ofstream(fixture / "cpu.max") << "max 100000\n";
  require(process_cpu_allowance(proc) == sic_test::expected_physical_cpus(),
          "unlimited resolved ancestry must preserve all affinity cores");
  for (const char* malformed : {"invalid", "0"}) {
    setenv("SLURM_CPUS_PER_TASK", malformed, 1);
    bool categorized = false;
    try {
      (void)process_cpu_allowance(proc);
    } catch (const sic::Error& error) {
      categorized = error.kind() == sic::ErrorKind::invalid_format;
    }
    require(categorized, "malformed SLURM setting did not throw Error(invalid_format)");
  }
  // A valid, restrictive default ceiling must not clamp explicit physical teams.
  setenv("SLURM_CPUS_PER_TASK", "1", 1);
  require(sic::decode::physical_workers(place, 4, sysfs).size() == 4,
          "SLURM allowance clamped explicit physical workers");
  auto placement = sic::decode::Placement::detect(false);
  placement.cpus.resize(std::min<std::size_t>(4, placement.cpus.size()));
  placement.node_of.resize(placement.cpus.size());
  const auto cores = sic::support::resources::physical_cpu_workers(placement.cpus).size();
  sic::decode::Pool pool(4, placement);
  require(pool.size() == 4, "SLURM allowance clamped explicit Pool");
  require(pool.physical_parallelism() == cores,
          "explicit Pool physical parallelism differs from its placement cores");
  std::cout << "PASS unknown ancestry explicit/default teams and categorized CPU inputs\n";
}
void concurrent_grants() {
  sic::decode::Pool pool(8, sic::decode::Placement::detect(false));
  sic::decode::MemoryBudget budget(1ull << 30, 8);
  budget.rebase(sic::decode::resident_bytes());
  const auto before = budget.available();
  std::atomic<unsigned> completed{0};
  pool.run(4096, [&](size_t) {
    sic::decode::Admission admission(budget, 4096);
    ++completed;
  });
  require(completed == 4096 && budget.available() == before && budget.peak() <= budget.budget(),
          "concurrent group grant/release accounting");
  // A cumulative-counter refusal must leave a logically empty semaphore empty.
  sic::decode::MemoryBudget huge(UINT64_MAX, 1);
  huge.acquire(1ull << 63, 0);
  huge.release(1ull << 63, 0);
  bool refused = false;
  try {
    huge.acquire(1ull << 63, 0);
  } catch (const sic::Error& e) {
    refused = e.kind() == sic::ErrorKind::resource;
  }
  require(refused && huge.available() == huge.budget(), "grant overflow mutated accounting");
}
void dispatch_generations() {
  sic::decode::Pool pool(8, sic::decode::Placement::detect(false));
  pool.run(
      0, [](size_t) { throw std::runtime_error("empty dispatch invoked body"); }, 0);
  // Alternate tiny targeted publications with wide steals. Every index must be
  // visited once, and returning from run must join all users of its stack body.
  for (size_t round = 0; round < 500; ++round) {
    if (round % 100 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const size_t count = round % 37;
    auto visits = std::make_unique<std::atomic<unsigned>[]>(count);
    pool.run(
        count, [&](size_t q) { ++visits[q]; }, 1);
    for (size_t q = 0; q < count; ++q) require(visits[q] == 1, "dispatch lost/duplicated a job");
    const size_t grouped_count = round % 5 ? count : count + 1024;
    auto grouped = std::make_unique<std::atomic<unsigned>[]>(grouped_count);
    pool.run_grouped(grouped_count, [&](size_t q) { ++grouped[q]; });
    for (size_t q = 0; q < grouped_count; ++q)
      require(grouped[q] == 1, "grouped dispatch lost/duplicated a job");
  }
}
void live_cancellation() {
  auto token = std::make_shared<sic::support::StopToken>();
  sic::support::StopScope stop(token.get());
  std::vector<ComplexBallVector> scratch;
  for (unsigned i = 0; i < 4; ++i) scratch.emplace_back(3);
  sic::decode::Pool pool(4, sic::decode::Placement::detect(false));
  std::mutex mutex;
  std::condition_variable changed;
  unsigned entered = 0;
  bool release = false;
  std::atomic<unsigned> live{0};
  std::thread cancel([&] {
    std::unique_lock lock(mutex);
    changed.wait(lock, [&] { return entered >= 2; });
    token->request_stop();
    release = true;
    changed.notify_all();
  });
  bool cancelled = false;
  try {
    pool.run(64, [&](size_t q) {
      struct Live {
        std::atomic<unsigned>& count;
        explicit Live(std::atomic<unsigned>& c) : count(c) { ++count; }
        ~Live() { --count; }
      } in_job(live);
      auto& s = scratch[sic::decode::Pool::worker_id()];
      acb_set_ui(s[0], q + 1);
      acb_inv(s[1], s[0], 256);
      {
        std::unique_lock lock(mutex);
        ++entered;
        changed.notify_all();
        changed.wait(lock, [&] { return release; });
      }
      sic::support::check_stop();
    });
  } catch (const sic::Error& e) {
    cancelled = e.kind() == sic::ErrorKind::cancelled;
  }
  cancel.join();
  require(cancelled && live == 0, "cancelled workers not joined before scratch release");
}
}  // namespace
int main() {
  try {
    flint_set_num_threads(1);
    cache_lifetime();
    cross_component_nesting();
    budgeted_foreign_nesting();
    overwrite_and_failure();
    live_group_refusal();
    physical_dispatch();
    allowance_seams();
    concurrent_grants();
    dispatch_generations();
    live_cancellation();
    std::cout << "PASS scratch overwrite, retry, admission, exception and live cancellation/join\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
