// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/resources.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <numeric>

#include "assertions.hpp"
#include "temporary_directory.hpp"

static bool throws(const std::function<void()>& action) {
  try {
    action();
    return false;
  } catch (const std::exception&) {
    return true;
  }
}

int main() {
  SIC_CHECK(sic::support::resources::add(2, 3) == 5);
  SIC_CHECK(sic::support::resources::mul(17, 19) == 323);
  SIC_CHECK(sic::support::resources::mul(0, UINT64_MAX) == 0);
  SIC_CHECK(throws([] { sic::support::resources::add(UINT64_MAX, 1); }));
  SIC_CHECK(throws([] { sic::support::resources::mul(UINT64_MAX, 2); }));
  SIC_CHECK(sic::support::resources::integer("123") == 123);
  for (const char* text : {"", "-1", "+1", "1x", " 1", "18446744073709551616"})
    SIC_CHECK(throws([&] { sic::support::resources::integer(text); }));
  SIC_CHECK(sic::support::resources::megabytes("0.5") == sic::support::resources::kMib / 2);
  for (const char* text : {"0", "-1", "nan", "inf", "1x", "1 2", "1e100"})
    SIC_CHECK(throws([&] { sic::support::resources::megabytes(text); }));
  SIC_CHECK(sic::support::resources::thread_count("2", "threads") == 2);
  SIC_CHECK(sic::support::resources::requested_threads("3") == 3);
  SIC_CHECK(throws([] { sic::support::resources::requested_threads("0"); }));
  SIC_CHECK(sic::support::resources::requested_threads(nullptr) ==
            static_cast<int>(sic::support::resources::process_cpu_allowance()));
  SIC_CHECK(throws([] { sic::support::resources::thread_count("0", "threads"); }));
  SIC_CHECK(throws([] { sic::support::resources::thread_count("2147483648", "threads"); }));
  SIC_CHECK(sic::support::resources::cpu_count("0-63,128-191") == 128);
  SIC_CHECK(sic::support::resources::cpu_count("0,2,5-7") == 5);
  SIC_CHECK(throws([] { sic::support::resources::cpu_count("3-1"); }));
  SIC_CHECK(throws([] { sic::support::resources::cpu_count("0,"); }));
  SIC_CHECK(sic::support::resources::cpu_sets_intersect("0-31,128-159", "0-63,128-191"));
  SIC_CHECK(!sic::support::resources::cpu_sets_intersect("0-31,128-159", "64-127,192-255"));
  SIC_CHECK(sic::support::resources::cpu_sets_intersect("64,128", "0-63,128-191"));
  SIC_CHECK(!sic::support::resources::cpu_sets_intersect("", "0-63"));
  SIC_CHECK(throws([] { sic::support::resources::cpu_sets_intersect("3-1", "0"); }));
  SIC_CHECK(sic::support::resources::unescape_mount("/a\\040b") == "/a b");
  const std::string mounts = "1 0 0:1 / /cgroup rw - cgroup2 cgroup rw\n";
  const auto paths = sic::support::resources::cgroup_ancestors("0::/parent/child\n", mounts);
  SIC_CHECK(paths.size() == 3);
  SIC_CHECK(paths[0] == "/cgroup/parent/child");
  SIC_CHECK(paths[1] == "/cgroup/parent");
  SIC_CHECK(paths[2] == "/cgroup");
  SIC_CHECK(sic::support::resources::cgroup_ancestors("0::/\n", mounts).size() == 1);
  const auto subtree = sic::support::resources::cgroup_ancestors(
      "0::/parent/child\n", "1 0 0:1 /parent /limited rw - cgroup2 cgroup rw\n");
  SIC_CHECK(subtree.size() == 2);
  SIC_CHECK(subtree[0] == "/limited/child");
  SIC_CHECK(subtree[1] == "/limited");
  SIC_CHECK(sic::support::resources::cgroup_ancestors(
                "0::/other\n", "1 0 0:1 /parent /limited rw - cgroup2 cgroup rw\n")
                .empty());
  SIC_CHECK(
      throws([&] { sic::support::resources::cgroup_ancestors("0::/../../hidden\n", mounts); }));

  sic_test::TemporaryDirectory scratch("sic-resource-test");
  const auto fixture = scratch.path;
  std::filesystem::create_directories(fixture / "parent/child");
  auto write = [&](const std::filesystem::path& path, const char* value) {
    std::ofstream output(fixture / path);
    output << value << '\n';
  };
  // Synthetic SMT siblings: no affinity change and no work on CPUs 128-255.
  for (const char* cpu : {"cpu84", "cpu85"}) {
    const auto topology = std::filesystem::path("cpu") / cpu / "topology";
    std::filesystem::create_directories(fixture / topology);
    write(topology / "physical_package_id", "1");
    write(topology / "core_id", "20");
  }
  const auto topology_root = fixture / "cpu";
  SIC_CHECK(sic::support::resources::physical_cpu_workers({84, 85}, topology_root) ==
            std::vector<unsigned>({0}));
  write("cpu/cpu85/topology/core_id", "21");
  SIC_CHECK(sic::support::resources::physical_cpu_workers({84, 85}, topology_root) ==
            std::vector<unsigned>({0, 1}));
  SIC_CHECK(sic::support::resources::physical_cpu_workers({85, 84, 85}, topology_root) ==
            std::vector<unsigned>({0, 1}));
  write("cpu/cpu85/topology/core_id", "-1");
  SIC_CHECK(sic::support::resources::physical_cpu_workers({84, 85}, topology_root) ==
            std::vector<unsigned>({0}));
  std::filesystem::remove(fixture / "cpu/cpu85/topology/core_id");
  SIC_CHECK(sic::support::resources::physical_cpu_workers({84, 85}, topology_root) ==
            std::vector<unsigned>({0}));
  SIC_CHECK(sic::support::resources::physical_cpu_workers({}, topology_root).empty());
  write("parent/memory.max", "800");
  write("parent/memory.current", "700");
  write("parent/cpu.max", "50000 100000");
  write("parent/child/memory.max", "500");
  write("parent/child/memory.high", "300");
  write("parent/child/memory.current", "10");
  write("parent/child/cpu.max", "250000 100000");
  write("parent/child/cpuset.cpus.effective", "0-1,4");
  std::filesystem::create_directories(fixture / "numa/node0");
  std::filesystem::create_directories(fixture / "numa/node1");
  write("numa/node0/cpulist", "0-63,128-191");
  write("numa/node1/cpulist", "64-127,192-255");
  write("numa/node0/meminfo", "Node 0 MemFree: 1024 kB");
  write("numa/node1/meminfo", "Node 1 MemFree: 2048 kB");
  sic::support::resources::Snapshot local;
  local.memory_known = true;
  local.available = 10 * sic::support::resources::kMib;
  sic::support::resources::apply_numa(local, "0-31,128-159", fixture / "numa");
  SIC_CHECK(local.available ==
            sic::support::resources::kMib);  // Node 1 belongs to a separate allocation.
  local.available = 10 * sic::support::resources::kMib;
  sic::support::resources::apply_numa(local, "0,64", fixture / "numa");
  SIC_CHECK(local.available == 3 * sic::support::resources::kMib);
  write("numa/node0/meminfo", "Node 0 MemFree: 0 kB");
  sic::support::resources::apply_numa(local, "0", fixture / "numa");
  SIC_CHECK(local.available == 0);
  local.available = 10 * sic::support::resources::kMib;
  sic::support::resources::apply_numa(local, "0,64", fixture / "missing-numa");
  SIC_CHECK(local.available == 10 * sic::support::resources::kMib);
  std::filesystem::remove(fixture / "numa/node1/meminfo");
  sic::support::resources::apply_numa(local, "0,64", fixture / "numa");
  SIC_CHECK(local.available ==
            10 * sic::support::resources::kMib);  // Never cap from a partial matched-node sum.
  write("numa/node1/meminfo", "Node 1 Unknown: 2048 kB");
  sic::support::resources::apply_numa(local, "0,64", fixture / "numa");
  SIC_CHECK(local.available == 10 * sic::support::resources::kMib);
#if defined(__linux__)
  std::filesystem::create_directories(fixture / "proc/self");
  SIC_CHECK(sic::support::resources::process_cpu_allowance(fixture / "proc") == 1);
  const char* old_slurm = std::getenv("SLURM_CPUS_PER_TASK");
  const std::optional<std::string> saved_slurm =
      old_slurm ? std::optional<std::string>(old_slurm) : std::nullopt;
  struct RestoreSlurm {
    std::optional<std::string> saved;
    ~RestoreSlurm() {
      if (saved)
        setenv("SLURM_CPUS_PER_TASK", saved->c_str(), 1);
      else
        unsetenv("SLURM_CPUS_PER_TASK");
    }
  } restore_slurm{saved_slurm};
  setenv("SLURM_CPUS_PER_TASK", "0", 1);
  SIC_CHECK(throws([&] { sic::support::resources::process_cpu_allowance(fixture / "proc"); }));
  setenv("SLURM_CPUS_PER_TASK", "invalid", 1);
  SIC_CHECK(throws([&] { sic::support::resources::process_cpu_allowance(fixture / "proc"); }));
  unsetenv("SLURM_CPUS_PER_TASK");
  write("proc/self/cgroup", "2:cpu:/parent/child");
  write("proc/self/mountinfo", "1 0 0:1 / /unavailable rw - cgroup cgroup rw");
  SIC_CHECK(sic::support::resources::process_cpu_allowance(fixture / "proc") == 1);
  write("proc/self/cgroup", "0::/parent/child");
  const std::string synthetic_mount = "1 0 0:1 / " + fixture.string() + " rw - cgroup2 cgroup rw";
  write("proc/self/mountinfo", synthetic_mount.c_str());
  SIC_CHECK(sic::support::resources::process_cpu_allowance(fixture / "proc") == 1);
  write("parent/cpu.max", "0 100000");
  SIC_CHECK(throws([&] { sic::support::resources::process_cpu_allowance(fixture / "proc"); }));
  write("parent/cpu.max", "50000 100000");

#endif

  sic::support::resources::Snapshot snapshot;
  snapshot.available = 1000;
  snapshot.memory_known = true;
  snapshot.cpus = 16;
  // Logical admission preserves explicit SMT requests; defaults are separate.
  sic::support::resources::apply_cgroup(snapshot, fixture / "no-cpu-ceiling");
  SIC_CHECK(snapshot.cpus == 16);
  sic::support::resources::apply_cgroup(snapshot, fixture / "parent/child");
  SIC_CHECK(snapshot.available == 290);
  SIC_CHECK(snapshot.cpus == 2);
  sic::support::resources::apply_cgroup(snapshot, fixture / "parent");
  SIC_CHECK(snapshot.available == 100);
  SIC_CHECK(snapshot.cpus == 1);
  snapshot.rss = 50;
  SIC_CHECK(sic::support::resources::process_limit(snapshot) == 130);
  SIC_CHECK(sic::support::resources::process_limit(snapshot, 100) == 100);
  SIC_CHECK(sic::support::resources::process_limit(snapshot, 1000000) ==
            130);  // a user hint cannot raise real headroom
  write("parent/memory.current", "900");
  sic::support::resources::apply_cgroup(snapshot, fixture / "parent");
  SIC_CHECK(snapshot.available == 0);
  write("memory.max", "100");
  SIC_CHECK(throws([&] { sic::support::resources::apply_cgroup(snapshot, fixture); }));

  SIC_CHECK(sic::support::resources::linear_correction_workspace(2048, 841, 64) >
            sic::support::resources::linear_correction_vectors(4094, 841, 64));
  SIC_CHECK(
      throws([] { sic::support::resources::linear_correction_workspace(UINT64_MAX, 841, 64); }));
  SIC_CHECK(sic::support::resources::ghost_reconstruction_scratch(2048, 18323, 64) >
            sic::support::resources::ghost_reconstruction_scratch(2048, 18323, 1));
  SIC_CHECK(sic::support::resources::ghost_reconstruction_scratch(8, 256, 64) ==
            sic::support::resources::ghost_reconstruction_scratch(8, 256, 8));

  SIC_CHECK(sic::support::resources::leaf_block_bytes(262144, 4096) >
            256 * sic::support::resources::kMib);
  SIC_CHECK(throws([] { sic::support::resources::leaf_block_bytes(262144, UINT64_MAX); }));

  SIC_CHECK(throws([] { sic::support::resources::complex_bytes(UINT64_MAX); }));
  return sic_test::report();
}
