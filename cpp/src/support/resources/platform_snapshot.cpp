// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/resources/platform_snapshot.hpp"

#include <flint/acb.h>
#include <sys/resource.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <compare>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "sic/error.hpp"
#include "sic/support/admission.hpp"
#include "sic/support/kernel_settings.hpp"
#include "sic/support/parse_number.hpp"
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

namespace sic::support::resources {
std::optional<std::string> read(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) return std::nullopt;
  std::string result;
  try {
    result.assign(std::istreambuf_iterator<char>(input), {});
    if (input.bad()) throw sic::Error(ErrorKind::io, "cannot read resource file " + path.string());
  } catch (const std::ios_base::failure& error) {
    throw sic::Error(ErrorKind::io, path.string() + ": " + error.what());
  }
  while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back())))
    result.pop_back();
  return result;
}

Count cpu_count(std::string_view list) {
  Count count = 0;
  while (!list.empty()) {
    const auto comma = list.find(',');
    const auto item = list.substr(0, comma);
    const auto dash = item.find('-');
    const Count first = integer(item.substr(0, dash));
    const Count last = dash == item.npos ? first : integer(item.substr(dash + 1));
    if (last < first) throw sic::Error(sic::ErrorKind::invalid_format, "invalid cpuset interval");
    count = add(count, add(last - first, 1));
    if (comma == list.npos) break;
    list.remove_prefix(comma + 1);
    if (list.empty())
      throw sic::Error(sic::ErrorKind::invalid_format, "invalid trailing cpuset comma");
  }
  return count;
}

std::string unescape_mount(std::string_view text) {
  std::string result;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\\' && i + 3 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '7' &&
        text[i + 2] >= '0' && text[i + 2] <= '7' && text[i + 3] >= '0' && text[i + 3] <= '7') {
      result +=
          static_cast<char>((text[i + 1] - '0') * 64 + (text[i + 2] - '0') * 8 + text[i + 3] - '0');
      i += 3;
    } else {
      result += text[i];
    }
  }
  return result;
}

std::vector<std::filesystem::path> cgroup_ancestors(const std::string& membership,
                                                    const std::string& mountinfo) {
  std::string group;
  std::istringstream members(membership);
  for (std::string line; std::getline(members, line);)
    if (line.starts_with("0::")) group = line.substr(3);
  if (group.empty()) return {};
  const std::filesystem::path group_path(group);
  if (!group_path.is_absolute())
    throw sic::Error(sic::ErrorKind::invalid_format, "relative cgroup membership");
  for (const auto& part : group_path)
    if (part == "..")
      throw sic::Error(sic::ErrorKind::resource, "unresolved cgroup namespace ancestry");
  std::filesystem::path mount, current;
  std::size_t best = 0;
  std::istringstream mounts(mountinfo);
  for (std::string line; std::getline(mounts, line);) {
    if (line.find(" - cgroup2 ") == std::string::npos) continue;
    std::istringstream fields(line);
    std::string id, parent, device, root_text, mount_text;
    fields >> id >> parent >> device >> root_text >> mount_text;
    const std::string root = unescape_mount(root_text);
    if (root != "/" && group != root && !group.starts_with(root + "/")) continue;
    if (root.size() < best) continue;
    best = root.size();
    mount = unescape_mount(mount_text);
    const std::string relative = root == "/" ? group.substr(1) : group.substr(root.size());
    current = relative.empty() ? mount : mount / std::filesystem::path(relative).relative_path();
  }
  if (mount.empty()) return {};
  std::vector<std::filesystem::path> result;
  for (;;) {
    result.push_back(current);
    if (current == mount) break;
    const auto parent = current.parent_path();
    if (parent == current)
      throw sic::Error(sic::ErrorKind::resource, "cgroup mount ancestry mismatch");
    current = parent;
  }
  return result;
}

CpuCounts non_affinity_cpu_counts(Count logical, Count physical, Count hardware_logical) {
  return {std::max<Count>(1, logical ? logical : hardware_logical), std::max<Count>(1, physical)};
}

std::vector<int> parse_cpu_list(const std::string& text) {
#if defined(__linux__)
  constexpr int maximum_cpu_count = CPU_SETSIZE;
#else
  constexpr int maximum_cpu_count = std::numeric_limits<int>::max();
#endif
  std::vector<int> out;
  std::stringstream in(text);
  for (std::string item; std::getline(in, item, ',');) {
    if (item.empty()) continue;
    const auto dash = item.find('-');
    const int a =
        support::parse_number<int>(item.substr(0, dash), "CPU list", ErrorKind::invalid_format, 0,
                                   std::numeric_limits<int>::max());
    const int b = dash == std::string::npos
                      ? a
                      : support::parse_number<int>(item.substr(dash + 1), "CPU list",
                                                   ErrorKind::invalid_format, 0,
                                                   std::numeric_limits<int>::max());
    if (b < a) throw sic::Error(ErrorKind::invalid_format, "CPU list: reversed range");
    for (int c = a; c <= b && c < maximum_cpu_count; ++c) out.push_back(c);
  }
  return out;
}

std::map<int, int> numa_cpu_nodes(const std::filesystem::path& sysfs_root) {
  const auto discover = [&] {
    std::map<int, int> nodes;
    for (int node = 0; node < 64; ++node) {
      std::ifstream input(sysfs_root / ("node" + std::to_string(node)) / "cpulist");
      std::string text;
      if (!input || !std::getline(input, text) || text.empty()) continue;
      for (int cpu : parse_cpu_list(text)) nodes[cpu] = node;
    }
    return nodes;
  };
  if (sysfs_root != "/sys/devices/system/node") return discover();
  static const auto nodes = discover();
  return nodes;
}

unsigned numa_node_count(const std::filesystem::path& sysfs_root) {
  const auto discover = [&] {
    unsigned nodes = 0;
    while (nodes < 64 && std::filesystem::exists(sysfs_root / ("node" + std::to_string(nodes))))
      ++nodes;
    return nodes;
  };
  if (sysfs_root != "/sys/devices/system/node") return discover();
  static const auto count = discover();
  return count;
}

namespace {
#if !defined(__linux__)
CpuCounts platform_cpu_counts() {
  static const auto counts = [] {
    Count logical = 0, physical = 0;
#if defined(__APPLE__)
    auto query = [](const char* name) -> Count {
      int value = 0;
      std::size_t size = sizeof value;
      return sysctlbyname(name, &value, &size, nullptr, 0) == 0 && value > 0
                 ? static_cast<Count>(value)
                 : 0;
    };
    logical = query("hw.logicalcpu");
    physical = query("hw.physicalcpu");
#endif
    return non_affinity_cpu_counts(logical, physical, std::thread::hardware_concurrency());
  }();
  return counts;
}
#endif
}  // namespace

std::vector<unsigned> physical_cpu_workers(const std::vector<int>& cpus,
                                           const std::filesystem::path& sysfs_root) {
  struct Core {
    Count package;
    Count core;
    auto operator<=>(const Core&) const = default;
  };
  auto read_core = [&](int cpu) -> std::optional<Core> {
    const auto root = sysfs_root / ("cpu" + std::to_string(cpu)) / "topology";
    try {
      const auto package = read(root / "physical_package_id"), core = read(root / "core_id");
      if (!package || !core) return std::nullopt;
      return Core{integer(*package), integer(*core)};
    } catch (const sic::Error& error) {
      if (error.kind() != ErrorKind::invalid_format && error.kind() != ErrorKind::io) throw;
      return std::nullopt;
    }
  };
  static std::mutex mutex;
  static std::map<int, std::optional<Core>> cache;
  std::set<Core> cores;
  std::vector<unsigned> selected;
  for (unsigned i = 0; i < cpus.size(); ++i) {
    std::optional<Core> core;
    if (sysfs_root == "/sys/devices/system/cpu") {
      std::lock_guard lock(mutex);
      auto it = cache.find(cpus[i]);
      if (it == cache.end()) it = cache.emplace(cpus[i], read_core(cpus[i])).first;
      core = it->second;
    } else {
      core = read_core(cpus[i]);
    }
    if (!core) return cpus.empty() ? std::vector<unsigned>{} : std::vector<unsigned>{0};
    if (cores.insert(*core).second) selected.push_back(i);
  }
  return selected;
}

namespace {
void apply_cpu_cgroup(Snapshot& result, const std::filesystem::path& path) {
  try {
    if (const auto text = read(path / "cpu.max")) {
      std::istringstream fields(*text);
      std::string quota, period, extra;
      if (!(fields >> quota >> period) || fields >> extra)
        throw sic::Error(sic::ErrorKind::invalid_format, "invalid cgroup CPU quota/period fields");
      const Count p = integer(period);
      if (!p) throw sic::Error(sic::ErrorKind::invalid_format, "zero cgroup CPU period");
      if (quota != "max") {
        const Count q = integer(quota);
        if (!q) throw sic::Error(sic::ErrorKind::invalid_format, "zero cgroup CPU quota");
        const Count ceiling = std::max<Count>(1, q / p);
        result.cpus = std::min(result.cpus, ceiling);
        result.constraints.push_back((path / "cpu.max").string() + "=" + *text);
      }
    }
    if (const auto text = read(path / "cpuset.cpus.effective"); text && !text->empty()) {
      const Count ceiling = cpu_count(*text);
      result.cpus = std::min(result.cpus, ceiling);
    }
  } catch (const sic::Error& error) {
    throw sic::Error(error.kind(), path.string() + ": " + error.what());
  } catch (const std::overflow_error& error) {
    throw sic::Error(ErrorKind::invalid_format, path.string() + ": " + error.what());
  }
}

std::vector<std::filesystem::path> process_cgroups(
    const std::filesystem::path& proc_root = "/proc") {
#if defined(__linux__)
  const auto membership = read(proc_root / "self/cgroup"),
             mounts = read(proc_root / "self/mountinfo");
  if (!membership || !mounts)
    throw sic::Error(sic::ErrorKind::io, "cannot inspect Linux cgroup membership/mounts");
  const auto paths = cgroup_ancestors(*membership, *mounts);
  if (paths.empty())
    throw sic::Error(sic::ErrorKind::resource, "cannot resolve cgroup-v2 ancestor limits");
  return paths;
#else
  return {};
#endif
}

Snapshot affinity_snapshot() {
  Snapshot result;
#if defined(__linux__)
  cpu_set_t affinity;
  CPU_ZERO(&affinity);
  if (sched_getaffinity(0, sizeof affinity, &affinity) == 0)
    result.cpus = std::max(1, CPU_COUNT(&affinity));
#else
  result.cpus = platform_cpu_counts().logical;
#endif
  return result;
}

void apply_job_cpus(Snapshot& result) {
  if (const char* value = std::getenv("SLURM_CPUS_PER_TASK")) {
    Count ceiling;
    try {
      ceiling = integer(value);
      if (!ceiling) throw sic::Error(sic::ErrorKind::invalid_format, "empty CPU allocation");
    } catch (const sic::Error& error) {
      throw sic::Error(error.kind(), std::string("SLURM_CPUS_PER_TASK: ") + error.what());
    }
    result.cpus = std::min(result.cpus, ceiling);
  }
  if (!result.cpus) throw sic::Error(sic::ErrorKind::resource, "empty CPU allocation");
}
}  // namespace

Count affinity_physical_cores(const std::filesystem::path& sysfs_root) {
  Count cores = 1;
#if defined(__linux__)
  cpu_set_t affinity;
  CPU_ZERO(&affinity);
  if (sched_getaffinity(0, sizeof affinity, &affinity) == 0) {
    std::vector<int> cpus;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
      if (CPU_ISSET(cpu, &affinity)) cpus.push_back(cpu);
    cores = std::max<Count>(1, physical_cpu_workers(cpus, sysfs_root).size());
  }
#else
  cores = platform_cpu_counts().physical;
#endif
  return cores;
}

Count process_cpu_allowance(const std::filesystem::path& proc_root) {
  Snapshot result;
  result.cpus = affinity_physical_cores();
  std::vector<std::filesystem::path> paths;
  try {
    paths = process_cgroups(proc_root);
  } catch (const sic::Error& error) {
    if (error.kind() != ErrorKind::io && error.kind() != ErrorKind::resource) throw;
    // CPU defaults remain usable without visible cgroup-v2 ancestry. Memory
    // inspection stays strict; an unknown CPU ceiling permits only one worker.
    result.cpus = 1;
  }
  for (const auto& path : paths) apply_cpu_cgroup(result, path);
  apply_job_cpus(result);
  return result.cpus;
}

int requested_threads(const char* configured) {
  return configured ? thread_count(configured, "SIC_THREADS")
                    : static_cast<int>(process_cpu_allowance());
}

int default_threads(int ceiling) { return std::min(ceiling, requested_threads(nullptr)); }

void apply_cgroup(Snapshot& result, const std::filesystem::path& path) {
  for (const char* name : {"memory.max", "memory.high"}) {
    const auto limit_text = read(path / name);
    if (!limit_text || *limit_text == "max") continue;
    const auto usage_text = read(path / "memory.current");
    if (!usage_text)
      throw sic::Error(ErrorKind::io, "finite cgroup memory limit without readable usage");
    const Bytes limit = integer(*limit_text), usage = integer(*usage_text);
    const Bytes free = limit > usage ? limit - usage : 0;
    result.available = result.memory_known ? std::min(result.available, free) : free;
    result.memory_known = true;
    result.constraints.push_back((path / name).string() + "=" + *limit_text +
                                 " current=" + *usage_text);
  }
  apply_cpu_cgroup(result, path);
}

bool cpu_sets_intersect(std::string_view a, std::string_view b) {
  auto intervals = [](std::string_view list) {
    struct CpuInterval {
      Count first;
      Count last;
    };
    std::vector<CpuInterval> result;
    while (!list.empty()) {
      const auto comma = list.find(','), dash = list.substr(0, comma).find('-');
      const auto item = list.substr(0, comma);
      const auto first = integer(item.substr(0, dash));
      const auto last = dash == item.npos ? first : integer(item.substr(dash + 1));
      if (last < first) throw sic::Error(ErrorKind::invalid_format, "invalid cpuset interval");
      result.emplace_back(first, last);
      if (comma == list.npos) break;
      list.remove_prefix(comma + 1);
      if (list.empty())
        throw sic::Error(ErrorKind::invalid_format, "invalid trailing cpuset comma");
    }
    return result;
  };
  const auto left = intervals(a), right = intervals(b);
  for (auto [lo, hi] : left)
    for (auto [other_lo, other_hi] : right)
      if (lo <= other_hi && other_lo <= hi) return true;
  return false;
}

void apply_numa(Snapshot& result, std::string_view affinity,
                const std::filesystem::path& root) try {
  if (affinity.empty()) return;
  std::error_code error;
  const std::filesystem::directory_iterator nodes(root, error);
  if (error) return;
  Bytes free = 0;
  std::string selected;
  for (const auto& node : nodes) {
    const auto name = node.path().filename().string();
    if (!name.starts_with("node") || name.size() == 4 ||
        name.find_first_not_of("0123456789", 4) != name.npos)
      continue;
    const auto cpus = read(node.path() / "cpulist");
    if (!cpus || !cpu_sets_intersect(affinity, *cpus)) continue;
    const auto text = read(node.path() / "meminfo");
    if (!text) return;  // Never sum a partial set of allowed nodes.
    std::optional<Bytes> node_free;
    std::istringstream rows(*text);
    for (std::string line; std::getline(rows, line);) {
      std::istringstream fields(line);
      std::string prefix, id, key, value, unit;
      fields >> prefix >> id >> key >> value >> unit;
      if (prefix == "Node" && key == "MemFree:" && unit == "kB")
        node_free = mul(integer(value), 1024);
    }
    if (!node_free) return;
    free = add(free, *node_free);
    selected += (selected.empty() ? "" : ",") + name;
  }
  if (!selected.empty()) {
    result.available = result.memory_known ? std::min(result.available, free) : free;
    result.memory_known = true;
    result.constraints.push_back("NUMA affinity MemFree=" + std::to_string(free) +
                                 " nodes=" + selected);
  }
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(ErrorKind::io, error.what());
}

Snapshot inspect() {
  Snapshot result = affinity_snapshot();
  std::string affinity;
#if defined(__linux__)
  if (const auto text = read("/proc/meminfo")) {
    std::istringstream fields(*text);
    for (std::string line; std::getline(fields, line);) {
      std::istringstream row(line);
      std::string key, value, unit;
      row >> key >> value >> unit;
      if (key == "MemAvailable:" && unit == "kB") {
        result.available = mul(integer(value), 1024);
        result.memory_known = true;
      }
    }
  }
  if (const auto text = read("/proc/self/status")) {
    std::istringstream fields(*text);
    for (std::string line; std::getline(fields, line);) {
      std::istringstream row(line);
      std::string key, value, unit;
      row >> key >> value >> unit;
      if (key == "VmRSS:") result.rss = mul(integer(value), 1024);
      if (key == "VmSize:") result.virtual_size = mul(integer(value), 1024);
      if (key == "Cpus_allowed_list:") {
        affinity = value;
        const Count ceiling = cpu_count(value);
        result.cpus = std::min(result.cpus, ceiling);
      }
    }
  }
  apply_numa(result, affinity);
  for (const auto& path : process_cgroups()) apply_cgroup(result, path);
#elif defined(__APPLE__)
  vm_statistics64_data_t vm{};
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  vm_size_t page = 0;
  const mach_port_t host = mach_host_self();
  if (host_page_size(host, &page) == KERN_SUCCESS &&
      host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) ==
          KERN_SUCCESS) {
    result.available = mul(add(vm.free_count, vm.inactive_count), page);
    result.memory_known = true;
  }
  mach_port_deallocate(mach_task_self(), host);
  mach_task_basic_info_data_t task{};
  count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&task),
                &count) == KERN_SUCCESS) {
    result.rss = task.resident_size;
    result.virtual_size = task.virtual_size;
  }
#endif
  if (!result.memory_known)
    throw sic::Error(ErrorKind::resource, "usable physical memory is unknown");
  // SLURM limits can be stricter than the visible host/cgroup allocation.
  apply_job_cpus(result);
  std::optional<Bytes> job_limit;
  if (const char* value = std::getenv("SLURM_MEM_PER_NODE")) {
    const Count mib = integer(value);
    if (mib) job_limit = mul(mib, kMib);  // SLURM uses zero to mean all node memory.
  }
  if (const char* value = std::getenv("SLURM_MEM_PER_CPU")) {
    const Count mib = integer(value);
    if (mib) {
      const Bytes cap = mul(mul(mib, result.cpus), kMib);
      job_limit = job_limit ? std::min(*job_limit, cap) : cap;
    }
  }
  if (job_limit) {
    const Bytes free = *job_limit > result.rss ? *job_limit - result.rss : 0;
    result.available = std::min(result.available, free);
    result.constraints.push_back("SLURM memory=" + std::to_string(*job_limit));
  }
  struct rlimit as_limit {};
  if (getrlimit(RLIMIT_AS, &as_limit) == 0 && as_limit.rlim_cur != RLIM_INFINITY) {
    const Bytes free =
        as_limit.rlim_cur > result.virtual_size ? as_limit.rlim_cur - result.virtual_size : 0;
    result.available = std::min(result.available, free);
    result.constraints.push_back("RLIMIT_AS=" + std::to_string(as_limit.rlim_cur));
  }
  if (!result.cpus) throw sic::Error(ErrorKind::resource, "empty CPU allocation");
  return result;
}

bool release_free_heap() {
#if defined(__GLIBC__)
  malloc_trim(0);
  return true;
#else
  return false;
#endif
}
}  // namespace sic::support::resources
