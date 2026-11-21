// SPDX-License-Identifier: AGPL-3.0-or-later
#include <fstream>
#include <iostream>
#include <source_location>

#include "bounds/check_balls.hpp"
#include "core/io/npz.hpp"
#include "data/catalogue.hpp"
#include "data/commands.hpp"
#include "data/decoded_output.hpp"
#include "decode/decoder.hpp"
#include "exact/storage/table_checkpoint.hpp"
#include "sic/data/catalogue.hpp"
#include "sic/data/layout.hpp"
#include "sic/support/byte_source.hpp"
#include "sic/support/stop.hpp"
#include "support/io_faults.hpp"
#include "support/periodic_grid.hpp"
#include "support/resources/numa_policy.hpp"
#include "support/sha256.hpp"

namespace fs = std::filesystem;
using sic::ErrorKind;
using sic::support::ComplexBallVector;
namespace resource = sic::support::resources;
namespace {
int square_fault = 0, square_hits = 0;
}
extern "C" void __real_acb_sqrt(acb_ptr, acb_srcptr, slong);
extern "C" void __wrap_acb_sqrt(acb_ptr out, acb_srcptr input, slong bits) {
  if (square_fault && bits == 128) {
    const int fault = std::exchange(square_fault, 0);
    ++square_hits;
    if (fault == 1) {
      acb_indeterminate(out);
      return;
    }
    throw sic::Error(fault == 2 ? ErrorKind::resource : ErrorKind::cancelled,
                     "injected root refusal");
  }
  __real_acb_sqrt(out, input, bits);
}
namespace {
void check(bool condition, std::source_location at = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error("failure category check at " + std::to_string(at.line()));
}
template <class F>
void category(ErrorKind kind, F action, std::source_location at = std::source_location::current()) {
  try {
    action();
  } catch (const sic::Error& error) {
    if (error.kind() != kind)
      throw std::runtime_error("wrong category at " + std::to_string(at.line()) + ": " +
                               error.what());
    return;
  }
  throw std::runtime_error("missing category at " + std::to_string(at.line()));
}
template <class E, class F>
void native(F action) {
  try {
    action();
  } catch (const sic::Error&) {
    throw std::runtime_error("standard exception became categorized");
  } catch (const E&) {
    return;
  }
  throw std::runtime_error("missing standard exception");
}
void write(const fs::path& path, std::string_view text) {
  std::ofstream out(path, std::ios::binary);
  out << text;
  out.close();
  check(bool(out));
}
std::string read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
struct Environment {
  std::string name;
  std::optional<std::string> previous;
  Environment(const char* key, const char* value) : name(key) {
    if (const char* old = getenv(key)) previous = old;
    if (value)
      setenv(key, value, 1);
    else
      unsetenv(key);
  }
  ~Environment() {
    if (previous)
      setenv(name.c_str(), previous->c_str(), 1);
    else
      unsetenv(name.c_str());
  }
};
void resources(const fs::path& root) {
  for (const char* text : {"", "-1", "x", "1x", "18446744073709551616"}) {
    category(ErrorKind::invalid_format, [&] { resource::integer(text); });
    category(ErrorKind::invalid_argument, [&] { resource::thread_count(text, "workers"); });
  }
  for (const char* text : {"0", "2147483648"})
    category(ErrorKind::invalid_argument, [&] { resource::thread_count(text, "workers"); });
  for (const char* text : {"0", "nan", "-1", "1e100", "bad"})
    category(ErrorKind::invalid_argument, [&] { resource::megabytes(text); });
  for (const char* text : {"3-1", "1,", "x"}) {
    category(ErrorKind::invalid_format, [&] { resource::cpu_count(text); });
    category(ErrorKind::invalid_format, [&] { resource::cpu_sets_intersect(text, "0"); });
  }
  const std::string mounts = "1 0 0:1 / /cgroup rw - cgroup2 cgroup rw\n";
  category(ErrorKind::invalid_format, [&] { resource::cgroup_ancestors("0::relative", mounts); });
  category(ErrorKind::resource, [&] { resource::cgroup_ancestors("0::/../hidden", mounts); });
  category(ErrorKind::resource, [&] {
    resource::cgroup_ancestors("0::/child", "1 0 0:1 / /cgroup/ rw - cgroup2 cgroup rw\n");
  });
  check(resource::cgroup_ancestors("2:cpu:/", mounts).empty());
  fs::create_directories(root / "proc/self");
  Environment allocation("SLURM_CPUS_PER_TASK", nullptr);
  check(resource::process_cpu_allowance(root / "proc") == 1);
  write(root / "proc/self/cgroup", "0::relative\n");
  write(root / "proc/self/mountinfo", mounts);
  category(ErrorKind::invalid_format, [&] { resource::process_cpu_allowance(root / "proc"); });
  write(root / "proc/self/cgroup", "0::/\n");
  write(root / "proc/self/mountinfo", "1 0 0:1 / " + root.string() + " rw - cgroup2 cgroup rw\n");
  for (const char* text : {"bad", "0", "-1"}) {
    Environment job("SLURM_CPUS_PER_TASK", text);
    category(ErrorKind::invalid_format, [&] { resource::process_cpu_allowance(root / "proc"); });
  }
  for (const char* quota : {"1", "0 100", "1 0", "bad 100", "1 100 extra"}) {
    write(root / "cpu.max", quota);
    category(ErrorKind::invalid_format, [&] { resource::process_cpu_allowance(root / "proc"); });
  }
  write(root / "cpu.max", "100 100");
  write(root / "cpuset.cpus.effective", "3-1");
  category(ErrorKind::invalid_format, [&] { resource::process_cpu_allowance(root / "proc"); });
  write(root / "cpuset.cpus.effective", "0-18446744073709551615");
  try {
    resource::process_cpu_allowance(root / "proc");
    check(false);
  } catch (const sic::Error& error) {
    check(error.kind() == ErrorKind::invalid_format);
    check(std::string(error.what()).find(root.string()) != std::string::npos);
  }
  fs::remove(root / "cpuset.cpus.effective");
  fs::remove(root / "cpu.max");
  fs::create_directory(root / "cpu.max");
  category(ErrorKind::io, [&] { resource::process_cpu_allowance(root / "proc"); });
  fs::remove(root / "cpu.max");
  for (const auto& [error, kind] : {std::pair{EINVAL, ErrorKind::unsupported},
                                    {ENOSYS, ErrorKind::unsupported},
                                    {ENOMEM, ErrorKind::resource},
                                    {EPERM, ErrorKind::io}}) {
    faults::mempolicy.at = 1;
    faults::mempolicy.error = error;
    category(kind, [] { resource::apply_numa_policy({-1, 0}, "invalid policy"); });
    check(faults::mempolicy.calls == 1);
    faults::reset();
  }
  resource::Snapshot snapshot;
  fs::create_directory(root / "memory.max");
  category(ErrorKind::io, [&] { resource::apply_cgroup(snapshot, root); });
  fs::remove(root / "memory.max");
  const auto topology = root / "sysfs/cpu0/topology";
  fs::create_directories(topology / "physical_package_id");
  write(topology / "core_id", "0");
  check(resource::physical_cpu_workers({0, 1}, root / "sysfs") == std::vector<unsigned>{0});
  fs::remove(topology / "physical_package_id");
  write(topology / "physical_package_id", "bad");
  check(resource::physical_cpu_workers({0, 1}, root / "sysfs") == std::vector<unsigned>{0});
  write(root / "memory.max", "100");
  category(ErrorKind::io, [&] { resource::apply_cgroup(snapshot, root); });
  write(root / "memory.current", "bad");
  category(ErrorKind::invalid_format, [&] { resource::apply_cgroup(snapshot, root); });
  write(root / "memory.current", "20");
  resource::apply_cgroup(snapshot, root);
  check(snapshot.available == 80);
  native<std::overflow_error>([] { resource::add(UINT64_MAX, 1); });
  native<std::overflow_error>([] { resource::mul(UINT64_MAX, 2); });
  native<std::invalid_argument>([] { sic::support::PeriodicGrid({2}, {}); });
  category(ErrorKind::resource, [] { sic::support::PeriodicGrid({LONG_MAX, 2}, {1, 1}); });
  category(ErrorKind::invalid_argument, [] { sic::checked_precision(0, 0, 256); });
  category(ErrorKind::invalid_argument, [] { sic::checked_precision(LONG_MAX, 0, 256); });
  category(ErrorKind::invalid_argument, [] { sic::checked_precision(64, 0, LONG_MAX); });
  category(ErrorKind::precision, [] { sic::checked_precision(257, 0, 256); });
  category(ErrorKind::precision, [] { sic::checked_precision(256, 1, 256); });
  sic::support::StopToken stop;
  stop.request_stop();
  category(ErrorKind::cancelled, [&] { stop.check(); });
  fs::create_symlink("loop", root / "loop");
  category(ErrorKind::io, [&] { sic::support::StopToken(root / "loop").check(); });
}
void bounds(const fs::path& root) {
  using namespace sic::bounds;
  BallVerificationLimits limits;
  flint_set_num_threads(2);
  category(ErrorKind::invalid_argument, [&] { admit_wh_bounds(2, limits); });
  flint_set_num_threads(1);
  category(ErrorKind::invalid_argument, [&] { admit_wh_bounds(1, limits); });
  limits.maximum_dimension = 2;
  category(ErrorKind::resource, [&] { admit_wh_bounds(4, limits); });
  limits = {};
  limits.maximum_working_bits = limits.working_bits - 1;
  category(ErrorKind::precision, [&] { admit_wh_bounds(2, limits); });
  limits = {};
  category(ErrorKind::invalid_argument, [&] { bound_wh_overlap_deviations(nullptr, 2, limits); });
  ComplexBallVector v(2);
  acb_indeterminate(v[0]);
  category(ErrorKind::invalid_argument,
           [&] { bound_wh_overlap_deviations(v.v.data(), 2, limits); });
  acb_zero(v[0]);
  category(ErrorKind::invalid_argument,
           [&] { bound_wh_overlap_deviations(v.v.data(), 2, limits); });
  arb_add_error_2exp_si(acb_realref(v[0]), -16);
  category(ErrorKind::precision, [&] { bound_wh_overlap_deviations(v.v.data(), 2, limits); });
  acb_one(v[0]);
  arb_add_error_2exp_si(acb_realref(v[0]), -16);
  category(ErrorKind::precision, [&] { bound_wh_overlap_deviations(v.v.data(), 2, limits); });
  limits.maximum_work_score = 1;
  try {
    admit_wh_bounds(2, limits);
    check(false);
  } catch (const resource::AdmissionError& error) {
    check(error.kind() == ErrorKind::resource);
  }
  const auto path = root / "balls";
  sic::support::cli::Arguments args;
  args.pos = {path.string()};
  category(ErrorKind::io, [&] { check_balls(args); });
  fs::create_directory(path);
  category(ErrorKind::io, [&] { check_balls(args); });
  fs::remove(path);
  for (const std::string text :
       {"bad\n", "sic-vector-balls/1.0\n", "sic-vector-balls/1.0\nd x\n",
        "sic-vector-balls/1.0\nd 1\n", "sic-vector-balls/1.0\nd 2\n",
        "sic-vector-balls/1.0\nd 2\nbad\n", "sic-vector-balls/1.0\nd 2\nbad | bad\n",
        "sic-vector-balls/1.0\nd 2\n0 0 0 | 0 0 0\n0 0 0 | 0 0 0\n",
        "sic-vector-balls/1.0\nd 2\n0 0 0 | 0 0 0\n0 0 0 | 0 0 0\nend\nextra"}) {
    write(path, text);
    category(ErrorKind::invalid_format, [&] { check_balls(args); });
  }
  write(path, "sic-vector-balls/1.0\nd " + std::to_string(kMaximumBallDimension + 1) + "\n");
  category(ErrorKind::invalid_format, [&] { check_balls(args); });
  const std::string rows = "sic-vector-balls/1.0\nd 2\n" + sic::data::dump_ball(v[1]) + "\n" +
                           sic::data::dump_ball(v[1]) + "\n";
  write(path, rows);
  category(ErrorKind::invalid_format, [&] { check_balls(args); });
  write(path, rows + "end\ntrailing");
  category(ErrorKind::invalid_format, [&] { check_balls(args); });
}
void decode(const fs::path& fixture) {
  sic::sicx::Reader reader(fixture);
  using Decoder = sic::decode::Decoder;
  auto options = Decoder::Options{};
  options.pin = false;
  options.threads = 1;
  auto invalid = [&](ErrorKind kind, auto change) {
    auto o = options;
    change(o);
    category(kind, [&] { Decoder rejected(reader, o); });
  };
  invalid(ErrorKind::invalid_argument, [](auto& o) { o.threads = 0; });
  invalid(ErrorKind::resource, [](auto& o) { o.maximum_threads = 0; });
  invalid(ErrorKind::precision, [](auto& o) { o.base_bits = o.maximum_precision + 1; });
  invalid(ErrorKind::invalid_argument, [](auto& o) { o.base_bits = 1; });
  Decoder decoder(reader, options);
  auto request = Decoder::Request{reader.meta().representative.begin()->first,
                                  std::vector<uint64_t>(reader.meta().smith.size(), 0)};
  auto bad = request;
  bad.representative = INT64_MAX;
  category(ErrorKind::invalid_argument, [&] { decoder.decode({bad}); });
  bad = request;
  bad.word.push_back(0);
  category(ErrorKind::invalid_argument, [&] { decoder.decode({bad}); });
  if (!request.word.empty()) {
    bad = request;
    bad.word[0] = reader.meta().smith[0];
    category(ErrorKind::invalid_argument, [&] { decoder.decode({bad}); });
  }
  for (int fault : {1, 2, 3}) {
    std::ostringstream progress;
    auto replay_options = options;
    replay_options.target = 64;
    replay_options.delta = 64;
    replay_options.base_bits = 1024;
    replay_options.progress = &progress;
    Decoder replay(reader, replay_options);
    square_fault = fault;
    square_hits = 0;
    if (fault == 1) {
      const auto result = replay.decode({request});
      check(result.size() == 1 && replay.stats().passes == 2 && replay.stats().replays == 1);
      check(progress.str().find("precision refusal at 128") != std::string::npos);
      check(progress.str().find("pass 2 precision=256") != std::string::npos);
    } else {
      category(fault == 2 ? ErrorKind::resource : ErrorKind::cancelled,
               [&] { replay.decode({request}); });
      check(replay.stats().passes == 1 && replay.stats().replays == 0);
    }
    check(square_hits == 1 && square_fault == 0);
  }
  using namespace sic::decode::kernels;
  category(ErrorKind::invalid_argument, [] { precision(1); });
  flint_set_num_threads(2);
  category(ErrorKind::invalid_argument, [] { precision(128); });
  flint_set_num_threads(1);
  ComplexBallVector v(2);
  arb_one(acb_realref(v[0]));
  arf_mul_2exp_si(arb_midref(acb_realref(v[0])), arb_midref(acb_realref(v[0])), 2048);
  arf_add_ui(arb_midref(acb_realref(v[0])), arb_midref(acb_realref(v[0])), 1, ARF_PREC_EXACT,
             ARF_RND_NEAR);
  category(ErrorKind::resource, [&] { bounded(v[0], 128); });
  acb_zero(v[0]);
  category(ErrorKind::precision, [&] { sic::decode::normalize(v, 0, 128); });
  try {
    throw PrecisionRefusal("retry");
  } catch (const PrecisionRefusal& error) {
    check(error.kind() == ErrorKind::precision);
  }
  using namespace sic::decode::detail;
  const std::vector<uint64_t> factors{1};
  native<std::runtime_error>([&] { PrefixPlan({}, factors); });
  // Only the selected precision subtype is replayed; resource and stop escape join unchanged.
  sic::decode::Pool pool(2, sic::decode::Placement::detect(false));
  for (auto kind : {ErrorKind::resource, ErrorKind::cancelled})
    category(kind, [&] { pool.run(2, [&](size_t) { throw sic::Error(kind, "worker refusal"); }); });
}
void storage(const fs::path& root) {
  using namespace sic::exact;
  ComplexBallVector values(2);
  acb_one(values[0]);
  auto backing = BallFile::save(values);
  const auto digest = backing->digest();
  for (int which = 0; which < 2; ++which) {
    const auto path = root / ("backing-" + std::to_string(which));
    faults::reset();
    if (which == 0)
      faults::write.at = 1;
    else
      faults::fsync.at = 1;
    category(ErrorKind::io, [&] { backing->publish(path); });
    check((which == 0 ? faults::write.calls : faults::fsync.calls) >= 1);
    faults::reset();
    check(backing->digest() == digest && sic::support::sha256::file_hex(backing->path()) == digest);
    check(fs::exists(path));
    if (which == 0)
      check(fs::file_size(path) == 0);
    else
      check(sic::support::sha256::file_hex(path) == digest);
    fs::remove(path);
    backing->publish(path);
    check(sic::support::sha256::file_hex(path) == digest);
  }
  Arithmetic ar(2);
  Group group(ar);
  group.generate_standard();
  group.close((long)ar.h() * ar.d * ar.d / 2);
  auto layout = std::make_shared<OrbitTableLayout>(ar.d, ar.h(), group.orbit);
  Table table(ar, 80, 1);
  table.enable_compact(layout);
  table.build_batched();
  table.release_states();
  const std::vector<char> mask(ar.h(), 0);
  const auto previous = root / "previous-checkpoint";
  faults::reset();
  TableCheckpoint::save(table, group, -1, mask, previous);
  const int final_sync = faults::fsync.calls;
  faults::reset();
  const auto old_manifest = sic::support::sha256::file_hex(previous / "manifest");
  for (int which = 0; which < 5; ++which) {
    const auto path = root / ("checkpoint-" + std::to_string(which));
    faults::reset();
    if (which == 0) faults::write.at = 1;
    if (which == 1) faults::fsync.at = 1;
    if (which == 2) faults::link.at = 1;
    if (which == 3) faults::rename.at = 1;
    if (which == 4)
      faults::fsync.at = final_sync;  // Final parent sync after payloads and both manifests.
    category(ErrorKind::io, [&] { TableCheckpoint::save(table, group, -1, mask, path); });
    check((which == 0                 ? faults::write.calls
           : which == 1 || which == 4 ? faults::fsync.calls
           : which == 2               ? faults::link.calls
                                      : faults::rename.calls) >= 1);
    check(fs::exists(path) == (which == 4));
    if (which == 4) check(faults::fsync.calls == final_sync);
    faults::reset();
    check(sic::support::sha256::file_hex(previous / "manifest") == old_manifest);
    if (!fs::exists(path)) {
      // The established fixed partial is retained on failure; retry cleanup is explicit.
      check(fs::exists(fs::path(path.string() + ".partial")));
      fs::remove_all(fs::path(path.string() + ".partial"));
      TableCheckpoint::save(table, group, -1, mask, path);
    }
    long cc = -2;
    std::vector<char> restored_mask;
    const auto restored =
        TableCheckpoint::load(ar, group, layout, 80, 1, path, cc, restored_mask, [](size_t) {});
    check(cc == -1 && restored_mask == mask);
    for (size_t c = 0; c < table.packed.size(); ++c)
      for (size_t q = 0; q < table.packed[c].size(); ++q)
        check(acb_equal(table.packed[c][q], restored->packed[c][q]));
  }
  auto fd = ::open(backing->path().c_str(), O_RDONLY);
  check(fd >= 0);
  std::array<unsigned char, 8> scratch{};
  faults::pread = {1, 0, EINTR};
  (void)sic::support::sha256::range_hash(fd, 0, 8, scratch);
  check(faults::pread.calls == 2);
  faults::reset();
  faults::pread.at = 1;
  category(ErrorKind::io, [&] { sic::support::sha256::range_hash(fd, 0, 8, scratch); });
  faults::reset();
  ::close(fd);
  category(ErrorKind::io, [&] { sic::support::sha256::file_hex(root); });
  category(ErrorKind::io, [&] { sic::support::sha256::file_hex(root / "absent"); });
  faults::reset();
  faults::read = {1, 0, EINTR};
  check(sic::support::sha256::file_hex(backing->path()) == digest);
  check(faults::read.calls >= 2);
  faults::reset();
  faults::read.at = 1;
  category(ErrorKind::io, [&] { sic::support::sha256::file_hex(backing->path()); });
  faults::reset();
  fd = ::open(backing->path().c_str(), O_RDONLY);
  check(fd >= 0);
  category(ErrorKind::io, [&] {
    sic::support::sha256::read_exact(fd, static_cast<off_t>(backing->bytes()), scratch.data(), 1);
  });
  ::close(fd);
  const auto bytes = root / "bytes";
  write(bytes, "x");
  sic::support::LocalByteSource source(bytes);
  write(bytes, "");
  category(ErrorKind::invalid_format, [&] { source.read(0, {scratch.data(), 1}); });
  faults::reset();
  faults::pread.at = 1;
  category(ErrorKind::io, [&] { source.read(0, {scratch.data(), 1}); });
  faults::reset();
  std::span<unsigned char> empty;
  native<std::invalid_argument>([&] { sic::support::sha256::range_hash(-1, 0, 1, empty); });
}
void catalogue(const fs::path& root, const fs::path& fixture) {
  sic::data::ReleaseOptions options;
  options.multiplet = 1;
  options.assets_dir = root / "assets";
  const auto tree = root / "release";
  const auto dir = sic::data::release_layout(fixture, tree, options);
  category(ErrorKind::io, [&] { sic::data::release_layout(fixture, tree, options); });
  category(ErrorKind::io, [&] { sic::data::release_layout(fixture, root / "loop", options); });
  category(ErrorKind::io, [&] { sic::data::file_entry(root / "absent", root); });
  write(dir / "c0.csv", "index,real,imag\n");
  const auto manifest = sic::support::JsonParser::parse(
      read(dir / "manifest.json")
          .substr(0, read(dir / "manifest.json").find_last_not_of('\n') + 1));
  const auto output = root / "release.json";
  const auto original = sic::data::release_catalogue(tree);
  write(output, sic::support::dump_json(original));
  check(!sic::data::read_catalogue(output).empty());
  auto manifest_failure = [&](ErrorKind kind, auto change) {
    auto j = manifest;
    change(j);
    write(dir / "manifest.json", sic::support::dump_json(j));
    category(kind, [&] { sic::data::release_catalogue(tree); });
    write(dir / "manifest.json", sic::support::dump_json(manifest));
  };
  manifest_failure(ErrorKind::unsupported,
                   [](auto& j) { j["schema"].string = "other"; });  // identity-negative
  manifest_failure(ErrorKind::integrity, [](auto& j) { j["id"].string = "d4-m2"; });
  manifest_failure(ErrorKind::integrity,
                   [](auto& j) { j["sicx"]["sha256"].string = std::string(64, '0'); });
  manifest_failure(ErrorKind::integrity,
                   [](auto& j) { j["sicx"]["exact_root"].string = std::string(64, '0'); });
  auto catalogue_failure = [&](auto change) {
    auto j = original;
    change(j);
    write(output, sic::support::dump_json(j));
    category(ErrorKind::invalid_format, [&] { sic::data::read_catalogue(output); });
  };
  catalogue_failure([](auto& j) { j["dimensions"].array[0]["d"].string = "3"; });
  catalogue_failure(
      [](auto& j) { j["dimensions"].array[0]["multiplets"].array[0]["id"].string = "d4-m01"; });
  catalogue_failure([](auto& j) {
    j["dimensions"].array[0]["multiplets"].array[0]["rays"].array[0]["label"].string = "bad";
  });
  catalogue_failure([](auto& j) {
    auto& files = j["dimensions"].array[0]["multiplets"].array[0]["rays"].array[0]["files"].array;
    files.clear();
  });
  catalogue_failure([](auto& j) {
    auto& files = j["dimensions"].array[0]["multiplets"].array[0]["rays"].array[0]["files"].array;
    for (const auto& file : files)
      if (file.at("path").as_str().ends_with(".csv")) {
        auto duplicate = file;
        files.push_back(duplicate);
        break;
      }
  });
  options.multiplet = 2;
  options.tag = "v2.0.0";
  sic::data::release_layout(fixture, tree, options);
  category(ErrorKind::integrity, [&] { sic::data::release_catalogue(tree); });
  fs::remove_all(tree / "d4/m2");
  fs::create_directories(tree / "d4/exceptional");
  write(tree / "d4/exceptional/manifest.json", "{\"schema\":\"other\"}");  // identity-negative
  category(ErrorKind::unsupported, [&] { sic::data::release_catalogue(tree); });
}
void data(const fs::path& root, const fs::path& fixture) {
  auto call = [&](std::string command, std::vector<std::string> paths) {
    sic::support::cli::Arguments args;
    args.pos = std::move(paths);
    return sic::data::run(command, args);
  };
  const auto path = root / "catalogue.json";
  category(ErrorKind::io, [&] { sic::data::read_catalogue(path); });
  fs::create_directory(path);
  category(ErrorKind::io, [&] { sic::data::read_catalogue(path); });
  category(ErrorKind::io, [&] { call("list", {path}); });
  fs::remove(path);
  category(ErrorKind::io, [&] { sic::data::read_catalogue(root); });
  write(path, "{\"schema\":\"other\"}");  // identity-negative
  category(ErrorKind::unsupported, [&] { sic::data::read_catalogue(path); });
  category(ErrorKind::unsupported, [&] { call("list", {path}); });
  write(root / "bad.txt", "2 missing\n");
  category(ErrorKind::invalid_format, [&] { sic::data::read_catalogue(root / "bad.txt"); });
  category(ErrorKind::io, [&] { sic::data::release_catalogue(root / "missing"); });
  const std::string ray = read(fixture);
  const auto json = root / "ray.json";
  write(json, ray);
  auto ray_json = sic::support::JsonParser::parse(ray.substr(0, ray.find_last_not_of('\n') + 1));
  auto ray_failure = [&](ErrorKind kind, auto change) {
    auto j = ray_json;
    change(j);
    write(json, sic::support::dump_json(j));
    category(kind, [&] { call("check", {json, root / "absent.npy"}); });
  };
  ray_failure(ErrorKind::unsupported,
              [](auto& j) { j["schema"].string = "other"; });  // identity-negative
  ray_failure(ErrorKind::invalid_format, [](auto& j) { j["fp64"]["values"].array.clear(); });
  ray_failure(ErrorKind::invalid_format,
              [](auto& j) { j["fp64"]["values"].array[0].array.clear(); });
  ray_failure(ErrorKind::invalid_format,
              [](auto& j) { j["fp64"]["values"].array[0].array[0].string = "x"; });
  ray_failure(ErrorKind::invalid_format,
              [](auto& j) { j["fp64"]["decimal"].array[0].array[0].string = "bad"; });
  ray_failure(ErrorKind::invalid_format, [](auto& j) {
    j["fp64"]["values"].array[0].array[0].string = "7ff0000000000000";
    j["fp64"]["decimal"].array[0].array[0].string = "inf";
  });
  write(json, ray);
  auto binary_failure = [&](std::string name, auto bytes,
                            ErrorKind kind = ErrorKind::invalid_format) {
    const auto file = root / name;
    write(file, {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    category(kind, [&] { call("check", {json, file}); });
  };
  namespace interchange = sic::core::interchange;
  auto npy = interchange::npy_encode(sic::core::Vector(4));
  auto changed = npy;
  changed[8] = 1;
  binary_failure("bad.npy", changed);
  changed = npy;
  changed[10] = 'x';
  binary_failure("bad.npy", changed);
  changed = npy;
  changed.pop_back();
  binary_failure("bad.npy", changed);
  changed = npy;
  auto shape = std::search(changed.begin(), changed.end(), "4,", "4," + 2);
  check(shape != changed.end());
  *(shape + 2) = ';';
  binary_failure("bad.npy", changed);
  binary_failure("short.npy", interchange::npy_encode(sic::core::Vector(3)), ErrorKind::integrity);
  write(root / "unknown.ext", "");
  category(ErrorKind::invalid_format, [&] { call("check", {json, root / "unknown.ext"}); });
  auto mat = interchange::fiducial_mat(sic::core::Vector(4), "ray", "nearest", "bound", "hash", "")
                 .bytes();
  changed = mat;
  changed[128] = 15;
  binary_failure("bad.mat", changed, ErrorKind::unsupported);
  changed = mat;
  changed.resize(132);
  binary_failure("bad.mat", changed);
  changed = mat;
  changed.resize(136);
  binary_failure("bad.mat", changed);
  // The first miMATRIX is psi: flags, dimensions, name, real and imaginary elements.
  for (const size_t offset : {size_t{136}, size_t{152}, size_t{144}, size_t{184}, size_t{224}}) {
    changed = mat;
    changed[offset] ^= 1;
    binary_failure("bad.mat", changed);
  }
  changed = mat;
  changed[140] = 255;  // Sub-element extent exceeds its containing matrix.
  binary_failure("bad.mat", changed);
  changed = mat;
  const auto psi = std::search(changed.begin(), changed.end(), "psi", "psi" + 3);
  check(psi != changed.end());
  *psi = 'x';
  binary_failure("bad.mat", changed);
  for (const std::string csv :
       {"index,real,imag\nbad\n", "index,real,imag\n1,0,0\n", "index,real,imag\n0,0,0\n"})
    binary_failure("bad.csv", csv);
  const auto bad = root / "bad.npy";
  fs::remove(bad);
  category(ErrorKind::io, [&] { call("check", {json, bad}); });
  for (const char* name : {"absent.csv", "absent.fp64.txt"})
    category(ErrorKind::io, [&] { call("check", {json, root / name}); });
  for (const char* name : {"directory.csv", "directory.fp64.txt"}) {
    fs::create_directory(root / name);
    category(ErrorKind::io, [&] { call("check", {json, root / name}); });
  }
  write(bad, "bad");
  category(ErrorKind::invalid_format, [&] { call("check", {json, bad}); });
  write(root / "bad.csv", "bad\n");
  category(ErrorKind::invalid_format, [&] { call("check", {json, root / "bad.csv"}); });
  write(root / "bad.mat", "bad\n");
  category(ErrorKind::invalid_format, [&] { call("check", {json, root / "bad.mat"}); });
  write(root / "bad.fp64.txt", "0");
  category(ErrorKind::invalid_format, [&] { call("check", {json, root / "bad.fp64.txt"}); });
  const auto zip = root / "bundle.npz";
  call("bundle", {zip, "4", json});
  category(ErrorKind::io, [&] { call("bundle", {zip, "4", json}); });
  category(ErrorKind::io, [&] { call("bundle", {root / "missing/out.npz", "4", json}); });
  category(ErrorKind::io, [&] { call("bundle", {root / "loop/out.npz", "4", json}); });
  category(ErrorKind::invalid_argument, [&] {
    call("bundle", {root / "duplicate.npz", "4", json, json});
  });
  auto unsafe = ray_json;
  unsafe["id"].string = "../unsafe";
  write(root / "unsafe.json", sic::support::dump_json(unsafe));
  category(ErrorKind::invalid_format, [&] {
    call("bundle", {root / "unsafe.npz", "4", root / "unsafe.json"});
  });
  sic::support::cli::Arguments output;
  output.pos = {json, root / "numeric"};
  check(sic::data::run("write", output) == 0);
  category(ErrorKind::io, [&] { sic::data::run("write", output); });
  auto archive = read(zip);
  const auto central = archive.find(std::string("PK\x01\x02", 4));
  check(central != std::string::npos);
  archive[central + 16] ^= 1;
  write(zip, archive);
  category(ErrorKind::integrity, [&] { call("check-bundle", {zip, json}); });
  archive[central + 10] = 8;
  write(zip, archive);
  category(ErrorKind::unsupported, [&] { call("check-bundle", {zip, json}); });
  const auto original = read(zip);
  auto zip_failure = [&](auto change, ErrorKind kind = ErrorKind::invalid_format) {
    auto bytes = original;
    bytes[central + 10] = 0;
    bytes[central + 16] ^= 1;  // Restore the original CRC before each independent mutation.
    change(bytes);
    write(zip, bytes);
    category(kind, [&] { call("check-bundle", {zip, json}); });
  };
  zip_failure([](auto& b) { b.resize(21); });
  zip_failure([](auto& b) { b.assign(22, 'x'); });
  zip_failure([&](auto& b) { b[central] = 'X'; });
  zip_failure([&](auto& b) { b[central + 28] = b[central + 29] = char(255); });
  zip_failure([&](auto& b) { b[central + 24] ^= 1; });
  zip_failure([&](auto& b) { b[central + 8] |= 8; }, ErrorKind::unsupported);
  zip_failure([](auto& b) { b[0] = 'X'; });
  zip_failure([](auto& b) { b[8] = 8; });
  zip_failure([&](auto& b) { b[central + 23] = b[central + 27] = 1; });
  // Two central entries can reference the same valid local record: duplicate member.
  zip_failure([&](auto& b) {
    const auto end = b.find(std::string("PK\x05\x06", 4), central);
    check(end != std::string::npos);
    const auto entry = b.substr(central, end - central);
    b.insert(end, entry);
    b[end + entry.size() + 10] = 2;
  });
  category(ErrorKind::integrity, [&] { call("bundle", {root / "wrong.npz", "8", json}); });
  ComplexBallVector v(1);
  category(ErrorKind::io, [&] { sic::data::require_new_output(root); });
  category(ErrorKind::io, [&] { sic::data::require_new_output(root / "loop"); });
  category(ErrorKind::io, [&] { sic::data::write_balls(root / "loop", v); });
  category(ErrorKind::io, [&] { sic::data::write_balls(root, v); });
  category(ErrorKind::io, [&] { sic::data::write_balls(root / "missing/file", v); });
  category(ErrorKind::io, [&] { sic::data::write_decoded_fp64(root / "missing/file", v, {}, 0); });
  sic::data::ReleaseOptions options;
  category(ErrorKind::invalid_argument, [&] { sic::data::release_layout("", root, options); });
}
}  // namespace
int main(int argc, char** argv) try {
  check(argc == 3);
  flint_set_num_threads(1);
  char pattern[] = "/tmp/sic-failure-categories-XXXXXX";
  check(mkdtemp(pattern));
  const fs::path root(pattern);
  struct Cleanup {
    fs::path root;
    ~Cleanup() {
      std::error_code ec;
      fs::remove_all(root, ec);
    }
  } cleanup{root};
  resources(root);
  bounds(root);
  decode(argv[1]);
  storage(root);
  data(root, argv[2]);
  catalogue(root, argv[1]);
  std::cout << "PASS categorized support, data, bounds and decoder failures\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
