// SPDX-License-Identifier: AGPL-3.0-or-later
#include <fstream>
#include <set>
#include <type_traits>

#include "assertions.hpp"
#include "construct/model/options.hpp"
#include "construct/model/worker_policy.hpp"
#include "cpu_allowance_expectation.hpp"
#include "support/resources.hpp"
#include "temporary_directory.hpp"

using namespace sic::support::resources;
static_assert(std::is_same_v<ByteCount, std::uint64_t>);
static_assert(std::is_same_v<Bytes, ByteCount>);
static_assert(std::is_same_v<Count, std::uint64_t>);
static_assert(std::is_same_v<BitCount, std::uint64_t>);
static_assert(std::is_same_v<Microseconds, std::uint64_t>);

namespace {
void check_defaults(int physical) {
  const auto snapshot = inspect();
  const auto allowance = process_cpu_allowance();
  SIC_CHECK(allowance == sic_test::expected_cpu_allowance());
  SIC_CHECK(snapshot.cpus >= allowance);
  std::printf("affinity physical=%d logical=%llu allowance=%llu\n", physical,
              static_cast<unsigned long long>(snapshot.cpus),
              static_cast<unsigned long long>(allowance));
  sic::support::KernelSettings absent;
  {
    sic::support::KernelSettingsScope scope(&absent);
    sic::construct::ProducerOptions options(8, {}, {}, absent);
    SIC_CHECK(options.requested_threads.get() == static_cast<int>(allowance));
    SIC_CHECK(sic::construct::default_producer_workers(8) ==
              std::min(32, static_cast<int>(allowance)));
  }
  // Explicit counts win even above the narrowed affinity. Admission remains separate.
  sic::support::KernelSettings explicit_threads{{"SIC_THREADS", "7"}};
  {
    sic::support::KernelSettingsScope scope(&explicit_threads);
    sic::construct::ProducerOptions options(8, {}, {}, explicit_threads);
    SIC_CHECK(options.requested_threads.get() == 7);
  }
  for (const char* invalid : {"0", "-1", "2147483648"}) {
    sic::support::KernelSettings bad{{"SIC_THREADS", invalid}};
    sic::support::KernelSettingsScope scope(&bad);
    bool producer_refused = false;
    sic::construct::ProducerOptions options(8, {}, {}, bad);
    try {
      (void)options.requested_threads.get();
    } catch (const std::exception&) {
      producer_refused = true;
    }
    SIC_CHECK(producer_refused);
  }
}
}  // namespace

int main() {
  // Exercise the non-affinity policy and memory-only worker admission.
  const auto mac = non_affinity_cpu_counts(18, 9, 256);
  SIC_CHECK(mac.logical == 18 && mac.physical == 9);
  const auto fallback = non_affinity_cpu_counts(0, 0, 12);
  SIC_CHECK(fallback.logical == 12 && fallback.physical == 1);
  SIC_CHECK(non_affinity_cpu_counts(0, 0, 0).logical == 1);
  // Caller budgets are bounded by work, not a rediscovered physical cap.
  SIC_CHECK(sic::construct::newton_class_workers({}, 8, 20) == 8);
  SIC_CHECK(sic::construct::newton_class_workers({}, 8, 3) == 3);
  sic::support::resources::Budget budget;
  budget.initial.cpus = mac.logical;
  SIC_CHECK(budget.workers(9, 0, 1) == 9);
  SIC_CHECK(budget.workers(30, 0, 1) == 30);
  sic::support::KernelSettings explicit_nine{{"SIC_THREADS", "9"}};
  sic::construct::ProducerOptions explicit_options(8, {}, {}, explicit_nine);
  SIC_CHECK(explicit_options.requested_threads.get() == 9);
#if defined(__linux__)
  cpu_set_t original;
  CPU_ZERO(&original);
  if (sched_getaffinity(0, sizeof original, &original))
    throw std::runtime_error("cannot read test affinity");
  struct Restore {
    cpu_set_t saved;
    ~Restore() {
      if (sched_setaffinity(0, sizeof saved, &saved)) std::terminate();
    }
  } restore{original};
  // Unknown cgroup ancestry limits defaults; explicit class teams retain
  // the requested count up to total threads and work.
  sic_test::TemporaryDirectory work("sic-newton-cpu");
  const auto fixture = work.path;
  struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() { std::filesystem::remove_all(root); }
  } cleanup{fixture};
  const auto proc = fixture / "proc", sysfs = fixture / "sysfs";
  std::filesystem::create_directories(proc / "self");
  std::ofstream(proc / "self/cgroup") << "2:cpu:/job\n";
  std::ofstream(proc / "self/mountinfo") << "1 0 0:1 / /unavailable rw - cgroup cgroup rw\n";
  SIC_CHECK(process_cpu_allowance(proc) == 1);
  int affinity_count = 0;
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (!CPU_ISSET(cpu, &original)) continue;
    const auto topology = sysfs / ("cpu" + std::to_string(cpu)) / "topology";
    std::filesystem::create_directories(topology);
    std::ofstream(topology / "physical_package_id") << "0\n";
    // Synthetic SMT pairs exercise physical-core accounting.
    std::ofstream(topology / "core_id") << affinity_count++ / 2 << '\n';
  }
  const int distinct = (affinity_count + 1) / 2;
  SIC_CHECK(affinity_physical_cores(sysfs) == static_cast<Count>(distinct));
  {
    const char* value = std::getenv("SLURM_CPUS_PER_TASK");
    struct RestoreSlurm {
      std::optional<std::string> value;
      ~RestoreSlurm() {
        if (value)
          setenv("SLURM_CPUS_PER_TASK", value->c_str(), 1);
        else
          unsetenv("SLURM_CPUS_PER_TASK");
      }
    } restore_slurm{value ? std::optional<std::string>(value) : std::nullopt};
    SIC_CHECK(setenv("SLURM_CPUS_PER_TASK", "1", 1) == 0);
    SIC_CHECK(process_cpu_allowance() == 1);
    sic::support::KernelSettings requested{{"SIC_THREADS", "3"}};
    sic::construct::ProducerOptions explicit_team(8, {}, {}, requested);
    const int nth = explicit_team.requested_threads.get();
    SIC_CHECK(nth == 3 && !explicit_team.class_threads.get());
    SIC_CHECK(sic::construct::newton_class_workers(explicit_team.class_threads.get(), nth, 20) ==
              nth);
    SIC_CHECK(sic::construct::newton_class_workers(distinct + 1, distinct + 1, distinct + 1) ==
              distinct + 1);
    SIC_CHECK(sic::construct::newton_class_workers(1, 20, 20) == 1);
    SIC_CHECK(sic::construct::newton_class_workers(20, 2, 20) == 2);
    SIC_CHECK(sic::construct::newton_class_workers(20, 20, 1) == 1);
    SIC_CHECK(sic::construct::newton_class_workers(
                  {}, static_cast<int>(process_cpu_allowance(proc)), 20) == 1);
  }
  cpu_set_t narrowed;
  CPU_ZERO(&narrowed);
  std::set<std::pair<int, int>> cores;
  int selected = 0;
  std::vector<int> narrowed_cpus;
  for (int cpu = 0; cpu < CPU_SETSIZE && selected < 2; ++cpu) {
    if (!CPU_ISSET(cpu, &original)) continue;
    // Independent topology expectation: count package/core pairs in the selected mask.
    const auto root = std::filesystem::path("/sys/devices/system/cpu") /
                      ("cpu" + std::to_string(cpu)) / "topology";
    int package = -1, core = -1;
    if (!(std::ifstream(root / "physical_package_id") >> package) ||
        !(std::ifstream(root / "core_id") >> core))
      throw std::runtime_error("test requires readable physical CPU topology");
    CPU_SET(cpu, &narrowed);
    cores.emplace(package, core);
    narrowed_cpus.push_back(cpu);
    SIC_CHECK(physical_cpu_workers(narrowed_cpus).size() == cores.size());
    ++selected;
    if (sched_setaffinity(0, sizeof narrowed, &narrowed))
      throw std::runtime_error("cannot narrow test affinity");
    check_defaults(static_cast<int>(cores.size()));
    // One allowed CPU always has a one-core allowance, including fractional quotas.
    if (selected == 1) SIC_CHECK(process_cpu_allowance() == 1);
  }
  SIC_CHECK(selected > 0);
#else
  check_defaults(static_cast<int>(process_cpu_allowance()));
#endif
  return sic_test::report();
}
