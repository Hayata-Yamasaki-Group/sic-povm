// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "support/resources/platform_snapshot.hpp"
#if defined(__linux__)
#include <sched.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace sic_test {
// Independent topology count for the exact production-root allowance expectation.
inline sic::support::resources::Count expected_physical_cpus() {
  using namespace sic::support::resources;
  Count expected = 1;
#if defined(__linux__)
  cpu_set_t affinity;
  CPU_ZERO(&affinity);
  if (sched_getaffinity(0, sizeof affinity, &affinity))
    throw std::runtime_error("cannot read test affinity");
  std::set<std::pair<int, int>> cores;
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (!CPU_ISSET(cpu, &affinity)) continue;
    const auto root = std::filesystem::path("/sys/devices/system/cpu") /
                      ("cpu" + std::to_string(cpu)) / "topology";
    int package = -1, core = -1;
    if (!(std::ifstream(root / "physical_package_id") >> package) ||
        !(std::ifstream(root / "core_id") >> core) || package < 0 || core < 0)
      throw std::runtime_error("test requires readable physical CPU topology");
    cores.emplace(package, core);
  }
  expected = std::max<Count>(1, cores.size());
#elif defined(__APPLE__)
  unsigned physical = 0;
  std::size_t bytes = sizeof physical;
  if (sysctlbyname("hw.physicalcpu", &physical, &bytes, nullptr, 0) == 0 && physical)
    expected = physical;
#endif
  return expected;
}

// Apply the same visible limits, without calling the allowance implementation.
inline sic::support::resources::Count expected_cpu_allowance() {
  using namespace sic::support::resources;
  Count expected = expected_physical_cpus();
#if defined(__linux__)
  const auto membership = read("/proc/self/cgroup"), mounts = read("/proc/self/mountinfo");
  std::vector<std::filesystem::path> ancestors;
  if (membership && mounts) {
    try {
      ancestors = cgroup_ancestors(*membership, *mounts);
    } catch (const std::runtime_error&) {
      // Unresolved namespace ancestry intentionally limits defaults to one.
    }
  }
  if (ancestors.empty()) expected = 1;
  for (const auto& path : ancestors) {
    if (const auto text = read(path / "cpu.max")) {
      std::istringstream fields(*text);
      std::string quota;
      Count period;
      if (!(fields >> quota >> period) || !period)
        throw std::runtime_error("test requires valid cpu.max");
      if (quota != "max")
        expected = std::min(expected, std::max<Count>(1, std::stoull(quota) / period));
    }
    if (const auto text = read(path / "cpuset.cpus.effective"); text && !text->empty())
      expected = std::min(expected, cpu_count(*text));
  }
#endif
  if (const char* value = std::getenv("SLURM_CPUS_PER_TASK"))
    expected = std::min<Count>(expected, std::stoull(value));
  return expected;
}
}  // namespace sic_test
