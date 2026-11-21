// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <filesystem>
#include <map>
#include <optional>

#include "support/resources/snapshot.hpp"
namespace sic::support::resources {
std::optional<std::string> read(const std::filesystem::path& path);
Count cpu_count(std::string_view list);
std::string unescape_mount(std::string_view text);
std::vector<std::filesystem::path> cgroup_ancestors(const std::string& membership,
                                                    const std::string& mountinfo);
void apply_cgroup(Snapshot& result, const std::filesystem::path& path);
bool cpu_sets_intersect(std::string_view a, std::string_view b);
std::vector<int> parse_cpu_list(const std::string& text);
// CPU-to-NUMA-node topology is immutable for the process lifetime. Injected
// fixture roots are read directly and never populate the production cache.
std::map<int, int> numa_cpu_nodes(
    const std::filesystem::path& sysfs_root = "/sys/devices/system/node");
// Contiguous node-directory count for process memory-policy masks (cached in production).
unsigned numa_node_count(const std::filesystem::path& sysfs_root = "/sys/devices/system/node");
void apply_numa(Snapshot& result, std::string_view affinity,
                const std::filesystem::path& root = "/sys/devices/system/node");
struct CpuCounts {
  Count logical;
  Count physical;
};
// Platforms without affinity: OS logical/physical counts, then conservative fallbacks.
CpuCounts non_affinity_cpu_counts(Count logical, Count physical, Count hardware_logical);
// Indices of one CPU per physical core. Missing/invalid topology yields owner zero.
// Production sysfs topology is cached per CPU for the process lifetime; injected
// roots are read directly so fixtures never populate the production cache.
std::vector<unsigned> physical_cpu_workers(
    const std::vector<int>& cpus,
    const std::filesystem::path& sysfs_root = "/sys/devices/system/cpu");
// Distinct physical cores in the affinity set, without default-only process limits.
Count affinity_physical_cores(const std::filesystem::path& sysfs_root = "/sys/devices/system/cpu");
// Affinity physical cores capped by existing cgroup and job CPU limits.
// Unknown cgroup ancestry permits one default worker, never caps explicit teams.
// Malformed readable CPU limits throw Error(invalid_format); invalid job CPU
// settings throw Error(invalid_format).
Count process_cpu_allowance(const std::filesystem::path& proc_root = "/proc");
// Explicit SIC_THREADS wins; a null setting selects the process CPU allowance.
int requested_threads(const char* configured);
Snapshot inspect();
bool release_free_heap();
}  // namespace sic::support::resources
