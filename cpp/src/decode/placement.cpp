// SPDX-License-Identifier: AGPL-3.0-or-later
// Explicit CPU placement discovery and affinity operations.
#include "decode/placement.hpp"

#include <sched.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>

#include "sic/error.hpp"
#include "sic/support/parse_number.hpp"
#include "support/resources/platform_snapshot.hpp"
namespace sic::decode {
std::vector<int> Placement::parse_list(const std::string& text) {
  return sic::support::resources::parse_cpu_list(text);
}
Placement Placement::detect(bool pin) {
  Placement p;
  p.pin = pin;
  cpu_set_t set;
  CPU_ZERO(&set);
  if (sched_getaffinity(0, sizeof set, &set))
    throw sic::Error(ErrorKind::io, "sched_getaffinity failed");
  const auto node = sic::support::resources::numa_cpu_nodes();
  struct CpuLocation {
    int node, cpu;
    auto operator<=>(const CpuLocation&) const = default;
  };
  std::vector<CpuLocation> order;
  for (int c = 0; c < CPU_SETSIZE; ++c)
    if (CPU_ISSET(c, &set)) order.push_back({node.contains(c) ? node.at(c) : 0, c});
  std::sort(order.begin(), order.end());
  std::set<int> used;
  for (auto [n, c] : order) {
    p.cpus.push_back(c);
    p.node_of.push_back(n);
    used.insert(n);
  }
  p.nodes = static_cast<unsigned>(std::max<size_t>(1, used.size()));
  return p;
}
size_t Placement::slot(unsigned worker, unsigned workers) const {
  return cpus.empty() ? 0 : static_cast<size_t>(worker) * cpus.size() / std::max(1u, workers);
}
// NUMA node of worker `worker` (its pinned CPU's node; node 0 without CPUs).
int Placement::node(unsigned worker, unsigned workers) const {
  return cpus.empty() ? 0 : node_of[slot(worker, workers)];
}
// Pins the calling thread; false if pinning was requested and failed.
bool Placement::bind(unsigned worker, unsigned workers) const {
  if (!pin || cpus.empty()) return true;
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpus[slot(worker, workers)], &set);
  return sched_setaffinity(0, sizeof set, &set) == 0;
}

}  // namespace sic::decode

namespace sic::decode {
std::vector<unsigned> physical_workers(const Placement& place, unsigned workers,
                                       const std::filesystem::path& sysfs_root) {
  using namespace sic::support::resources;
  const auto limit = workers;
  if (!limit) return {};
  std::vector<unsigned> selected;
  if (place.cpus.empty()) {
    selected.resize(limit);
    std::iota(selected.begin(), selected.end(), 0u);
  } else if (!place.pin) {
    selected.resize(
        std::min<std::size_t>(limit, physical_cpu_workers(place.cpus, sysfs_root).size()));
    std::iota(selected.begin(), selected.end(), 0u);
  } else {
    std::vector<int> assigned;
    for (unsigned t = 0; t < workers; ++t) assigned.push_back(place.cpus[place.slot(t, workers)]);
    selected = physical_cpu_workers(assigned, sysfs_root);
  }
  return selected;
}
}  // namespace sic::decode
