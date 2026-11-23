// SPDX-License-Identifier: AGPL-3.0-or-later
#include <sys/resource.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <fstream>
#include <iostream>
#include <optional>
#include <source_location>
#include <system_error>

#include "../fixtures/fixture.hpp"
#include "access/range_cache.hpp"
#include "access/source_arguments.hpp"
#include "finalization_cases.hpp"
#include "producer_cases.hpp"
#include "sic/sicx/finalize.hpp"
#include "sic/sicx/json.hpp"
#include "sic/sicx/text.hpp"
#include "sicx/codec/ball.hpp"
#include "sicx/codec/integer.hpp"
#include "sicx/codec/rescaling_rule.hpp"
#include "sicx/codec/unrescaled.hpp"
#include "sicx/container/directory.hpp"
#include "sicx/container/leaf_index.hpp"
#include "sicx/container/producer_writer.hpp"
#include "sicx/container/rescaling_input.hpp"
#include "sicx/exact/exact.hpp"
#include "sicx/exact/tower.hpp"
#include "sicx/io/io.hpp"
#include "sicx/model/branches.hpp"
#include "sicx/model/unit_group.hpp"
#include "support/sha256.hpp"
#include "transport/fetch.hpp"
#include "transport/parts.hpp"
#include "transport/repair.hpp"

namespace fs = std::filesystem;
using namespace sic::sicx;
using sic::ErrorKind;

namespace {
void check(bool ok, const std::string& why) {
  if (!ok) throw std::runtime_error(why);
}
template <class F>
void expect(ErrorKind kind, F&& operation,
            const std::source_location& location = std::source_location::current()) {
  try {
    operation();
  } catch (const sic::Error& error) {
    check(error.kind() == kind, std::string("wrong category at ") +
                                    std::to_string(location.line()) + ": " + error.what());
    return;
  }
  throw std::runtime_error("expected categorized failure at " + std::to_string(location.line()));
}

#ifdef SIC_ENABLE_TEST_HOOKS
enum class NativeFault { none, stat, truncate, sync, usage };
thread_local NativeFault native_fault = NativeFault::none;
thread_local size_t native_hits = 0;
struct NativeScope {
  NativeFault previous = native_fault;
  size_t previous_hits = native_hits;
  explicit NativeScope(NativeFault fault) {
    native_fault = fault;
    native_hits = 0;
  }
  ~NativeScope() {
    native_fault = previous;
    native_hits = previous_hits;
  }
};
template <class F>
void expect_native(NativeFault fault, F&& operation) {
  NativeScope scope(fault);
  expect(ErrorKind::io, std::forward<F>(operation));
  check(native_hits != 0, "native fault was not reached");
}
extern "C" int __real_fstat(int, struct stat*);
extern "C" int __wrap_fstat(int fd, struct stat* state) {
  if (native_fault == NativeFault::stat) {
    ++native_hits;
    errno = EIO;
    return -1;
  }
  return __real_fstat(fd, state);
}
extern "C" int __real_ftruncate(int, off_t);
extern "C" int __wrap_ftruncate(int fd, off_t size) {
  if (native_fault == NativeFault::truncate) {
    ++native_hits;
    errno = EIO;
    return -1;
  }
  return __real_ftruncate(fd, size);
}
extern "C" int __real_getrusage(int, struct rusage*);
extern "C" int __wrap_getrusage(int who, struct rusage* usage) {
  if (native_fault == NativeFault::usage) {
    ++native_hits;
    errno = EIO;
    return -1;
  }
  return __real_getrusage(who, usage);
}
#endif

ByteBuffer bytes(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  check(bool(input), "open test file");
  return {std::istreambuf_iterator<char>(input), {}};
}
void write(const fs::path& path, const ByteBuffer& data) {
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char*>(data.data()), data.size());
  check(bool(output), "write test file");
}
[[maybe_unused]] void copy_fixture(const fs::path& from, const fs::path& to) {
  fs::copy_file(from, to, fs::copy_options::overwrite_existing);
}
struct TestSource : sic::support::ByteSource {
  ByteBuffer data;
  std::optional<ErrorKind> failure;
  uint64_t fail_at = 0;
  bool fail_cleanup = false;
  mutable size_t invalidations = 0;
  explicit TestSource(ByteBuffer value) : data(std::move(value)) {}
  uint64_t size() const override { return data.size(); }
  void read(uint64_t offset, std::span<uint8_t> output) const override {
    if (failure && offset <= fail_at && fail_at - offset < output.size())
      throw sic::Error(*failure, "original read failure");
    ::check(offset <= data.size() && output.size() <= data.size() - offset, "test read bounds");
    std::copy_n(data.data() + offset, output.size(), output.data());
  }
  void invalidate(sic::support::ByteRange) const override {
    ++invalidations;
    if (fail_cleanup) throw sic::Error(ErrorKind::io, "cleanup failure");
  }
};

void primitives(const Reader& good, const fs::path& fixture, const fs::path& work) {
  const ByteBuffer bad_varint{0x80, 0};
  expect(ErrorKind::invalid_format, [&] { Cursor(bad_varint.data(), bad_varint.size()).varint(); });
  expect(ErrorKind::invalid_format, [&] { Cursor(bad_varint.data(), 0).take(1); });
  expect(ErrorKind::invalid_format, [] { parse_ball_part("2 0 0 0", 1); });
  ByteBuffer large;
  put_varint(large, 2 * (kMaxIntegerBytes + 1));
  IntCodec codec;
  sic::support::flint::Fmpz integer;
  expect(ErrorKind::resource, [&] {
    Cursor cursor(large.data(), large.size());
    codec.get(cursor, integer);
  });
  ByteBuffer leaf{0, 0, 1, 3};
  leaf.insert(leaf.end(), large.begin(), large.end());
  leaf.push_back(0);
  expect(ErrorKind::resource, [&] { leaf_spans(leaf); });
  expect(ErrorKind::invalid_format, [] { leaf_spans(ByteBuffer{0, 0, 1, 3, 1, 0}); });
  ResourceLimits leaf_limit;
  leaf_limit.directory_entries = 0;
  expect(ErrorKind::resource, [&] { leaf_spans(ByteBuffer{0, 0, 1, 2}, leaf_limit); });
  expect(ErrorKind::invalid_argument, [] { sic::transport::parse_u64("-1"); });
  expect(ErrorKind::invalid_format,
         [] { sic::transport::parse_u64("-1", ErrorKind::invalid_format); });
  expect(ErrorKind::invalid_format,
         [] { sic::transport::number(sic::support::Json::string_value("01")); });
  sic::transport::SplitOptions lookup_options;
  lookup_options.file = fixture;
  lookup_options.parts_path = work / "lookup.parts.json";
  lookup_options.prefix = "d4-m1";
  const auto lookup = sic::transport::split(lookup_options);
  check(&sic::transport::part_at(lookup, 0) == &lookup.parts.front(), "valid part lookup");
  for (const auto offset : {lookup.size, UINT64_MAX})
    expect(ErrorKind::invalid_argument, [&] { sic::transport::part_at(lookup, offset); });
  auto malformed_lookup = lookup;
  malformed_lookup.parts.front().offset = 1;
  expect(ErrorKind::invalid_format, [&] { sic::transport::part_at(malformed_lookup, 0); });
  malformed_lookup = lookup;
  malformed_lookup.parts.front().size = 1;
  expect(ErrorKind::invalid_format, [&] { sic::transport::part_at(malformed_lookup, 1); });
  auto meta = meta_to_json(good.meta());
  meta["version"] = sic::support::Json::array_value();
  expect(ErrorKind::invalid_format, [&] { meta_from_json(meta); });
  meta = meta_to_json(good.meta());
  meta["version"].array[0] = sic::support::Json::integer_value(2);
  expect(ErrorKind::unsupported, [&] { meta_from_json(meta); });
  meta = meta_to_json(good.meta());
  meta["entry_rule"]["name"] =
      sic::support::Json::string_value("another-entry/1.0");  // identity-negative
  expect(ErrorKind::unsupported, [&] { meta_from_json(meta); });
  meta = meta_to_json(good.meta());
  meta["generator_rescaling"]["rule"] =
      sic::support::Json::string_value("another-rescaling/1.0");  // identity-negative
  expect(ErrorKind::unsupported, [&] { meta_from_json(meta); });
  expect(ErrorKind::invalid_argument, [] { field_radicand(0); });
  expect(ErrorKind::invalid_argument, [] { UnitGroup group(0); });
  expect(ErrorKind::invalid_argument, [] { divisor_positions(0); });
  Reader::ElementChunk element;
  element.spans = std::make_shared<const std::vector<LeafSpan>>();
  expect(ErrorKind::invalid_argument, [&] { element.leaf(0); });
  {
    StreamWriter writer(work / "adopt", StreamOptions(false));
    expect(ErrorKind::invalid_argument, [&] { writer.adopt(1); });
  }
  const auto resume_path = work / "resume-mode";
  StreamOptions part_options;
  part_options.part = true;
  { StreamWriter part_writer(resume_path, part_options); }
  StreamOptions resume_options;
  resume_options.resume = true;
  expect(ErrorKind::integrity, [&] { StreamWriter writer(resume_path, resume_options); });
  const auto journal_path = resume_path.string() + ".partial.journal";
  auto journal = bytes(journal_path);
  journal[48] = 2;
  write(journal_path, journal);
  expect(ErrorKind::invalid_format, [&] { StreamWriter writer(resume_path, resume_options); });
  expect(ErrorKind::invalid_argument, [] { Reader(std::shared_ptr<sic::support::ByteSource>{}); });
  expect(ErrorKind::invalid_argument, [&] { sic::transport::split_ranges(good, 0); });
  expect(ErrorKind::unsupported, [&] { sic::transport::split_ranges(good, 1); });
  expect(ErrorKind::invalid_argument, [] { require_multiplet("m0"); });
  sic::support::flint::Arb ball;
  arb_zero(ball);
  arb_add_error_2exp_si(ball, 0);
  expect(ErrorKind::precision, [&] { certified_round(ball, "test"); });
  expect(ErrorKind::precision, [&] { certified_round32(ball, "test"); });
  expect(ErrorKind::io, [&] { read_bounded_text(work / "missing"); });
  std::istringstream line("unterminated");
  std::string text;
  expect(ErrorKind::invalid_format, [&] { bounded_line(line, text, 100); });
  std::istringstream broken("line\n");
  broken.setstate(std::ios::badbit);
  expect(ErrorKind::io, [&] { bounded_line(broken, text, 100); });
  std::istringstream overlong("long line\n");
  expect(ErrorKind::resource, [&] { bounded_line(overlong, text, 1); });
  const auto empty = work / "empty";
  write(empty, {});
  sic::access::Fd fd(::open(empty.c_str(), O_RDONLY));
  uint8_t value;
  expect(ErrorKind::invalid_format, [&] { pread_exact(fd.value, &value, 1, 0); });
  expect(ErrorKind::io, [&] { pread_exact(-1, &value, 1, 0); });
  expect(ErrorKind::io, [&] { StreamWriter::peek_journal(work / "absent-journal"); });
  std::vector<unsigned char> buffer(16);
  check(sic::transport::hash_range(-1, 0, 1, buffer).empty(), "I/O hash miss was not repairable");
  expect(ErrorKind::io,
         [&] { filesystem_io([&] { return fs::file_size(work / "absent-file"); }); });
  // Native failures and internal invariants must not be recategorized.
  bool standard = false;
  try {
    filesystem_io([] { throw std::bad_alloc(); });
  } catch (const std::bad_alloc&) {
    standard = true;
  }
  check(standard, "allocation type lost");
  standard = false;
  try {
    pwrite_all(-1, nullptr, 0, UINT64_MAX);
  } catch (const std::overflow_error&) {
    standard = true;
  }
  check(standard, "native overflow type lost");
  standard = false;
  try {
    sic::sicx::exact::Tower tower(good.meta());
    sic::sicx::exact::QuadraticElement zero;
    tower.kinv(zero);
  } catch (const std::logic_error&) {
    standard = true;
  }
  check(standard, "internal invariant type lost");
  standard = false;
  try {
    sic::sicx::exact::Tower tower(good.meta());
    tower.inverse_general(tower.zero(0), 0);
  } catch (const std::logic_error&) {
    standard = true;
  }
  check(standard, "inverse invariant type lost");
  sic::sicx::exact::Builder builder(good, {});
  builder.build_exact();
  sic::sicx::exact::Builder::Images images;
  for (unsigned i = 0; i < builder.T.t; ++i) images.push_back(builder.T.zero(i + 1));
  expect(ErrorKind::construction, [&] { builder.check_endomorphism(images, false, "test"); });
  // Branch refusal retains the subtype and precision category.
  auto branch_meta = good.meta();
  const auto unresolved = [&](size_t, auto) {};
  for (const auto ceiling : {256L, 512L}) {
    bool subtype = false;
    try {
      canonical_branches(branch_meta, unresolved, ceiling);
    } catch (const BranchRefusal& error) {
      subtype = error.kind() == ErrorKind::precision;
    }
    check(subtype, "branch refusal subtype lost");
  }
}

void request_and_state_failures(const Reader& good, const fs::path& fixture, const fs::path& work) {
  const auto zero_hash = std::string(64, '0');
  sic::transport::SplitOptions split;
  split.file = fixture;
  split.parts_path = work / "state.parts.json";
  split.assets_dir = work / "state-assets";
  split.prefix = "d4-m1";
  const auto parts = sic::transport::split(split);
  sic::access::RemoteOptions base;
  base.source = split.assets_dir.string();
  base.cache = work / "state-cache";
  base.prefetch_bytes = 0;
  base.max_fraction = 1;
  const auto remote = [&](const auto& options) {
    return sic::access::remote_byte_source(split.parts_path, options);
  };
  for (unsigned field = 0; field < 17; ++field) {
    auto bad = base;
    switch (field) {
      case 0:
        bad.cancellation.reset();
        break;
      case 1:
        bad.jobs = 0;
        break;
      case 2:
        bad.jobs = bad.limits.maximum_jobs + 1;
        break;
      case 3:
        bad.retries = bad.limits.maximum_retries + 1;
        break;
      case 4:
        bad.range_bytes = 0;
        break;
      case 5:
        bad.range_bytes = bad.limits.maximum_extent_bytes + 1;
        break;
      case 6:
        bad.max_ranges = 0;
        break;
      case 7:
        bad.max_ranges = bad.limits.maximum_ranges + 1;
        break;
      case 8:
        bad.timeout_ms = 0;
        break;
      case 9:
        bad.connect_timeout_ms = 0;
        break;
      case 10:
        bad.coalesce_gap = bad.limits.maximum_extent_bytes + 1;
        break;
      case 11:
        bad.prefetch_bytes = bad.limits.maximum_prefetch_bytes + 1;
        break;
      case 12:
        bad.max_fraction = 0;
        break;
      case 13:
        bad.max_fraction = 2;
        break;
      case 14:
        bad.max_fraction = std::numeric_limits<double>::quiet_NaN();
        break;
      case 15:
        bad.exact_root = "bad";
        break;
      case 16:
        bad.root = "BAD";
        break;
    }
    expect(ErrorKind::invalid_argument, [&] { remote(bad); });
  }
  auto bad = base;
  bad.source = "ftp://invalid";
  expect(ErrorKind::unsupported, [&] { remote(bad); });
  bad.source = "file://relative";
  expect(ErrorKind::invalid_argument, [&] { remote(bad); });
  bad = base;
  bad.memory_budget = 1;
  expect(ErrorKind::resource, [&] { remote(bad); });
  for (const bool exact : {false, true}) {
    auto pinned = base;
    (exact ? pinned.exact_root : pinned.root) = zero_hash;
    auto source = remote(pinned);
    expect(ErrorKind::integrity, [&] { Reader reader(source); });
    sic::support::cli::Arguments args;
    args.options[exact ? "--expect-exact-root" : "--expect-root"] = {{zero_hash}};
    sic::access::SourceArguments local(args);
    expect(ErrorKind::integrity, [&] { Reader reader(local.open(fixture)); });
  }
  auto source = remote(base);
  source->bind_identity(parts.exact_root, parts.file_root);
  expect(ErrorKind::integrity, [&] { source->bind_identity(zero_hash, parts.file_root); });
  base.cache = work / "fresh-identity-cache";
  source = remote(base);
  expect(ErrorKind::integrity, [&] { source->bind_identity(zero_hash, parts.file_root); });
  source = remote(base);
  expect(ErrorKind::invalid_format, [&] { source->bind_identity("bad", parts.file_root); });
  {
    sic::support::cli::Arguments args;
    args.options["--expect-root"] = {{"bad"}};
    sic::access::SourceArguments local(args);
    expect(ErrorKind::invalid_argument, [&] { local.open(fixture); });
  }
  {
    const auto saved = [](const char* key) -> std::optional<std::string> {
      const char* value = std::getenv(key);
      return value ? std::optional<std::string>(value) : std::nullopt;
    };
    struct Restore {
      std::optional<std::string> home, xdg;
      ~Restore() {
        if (home)
          setenv("HOME", home->c_str(), 1);
        else
          unsetenv("HOME");
        if (xdg)
          setenv("XDG_CACHE_HOME", xdg->c_str(), 1);
        else
          unsetenv("XDG_CACHE_HOME");
      }
    } restore{saved("HOME"), saved("XDG_CACHE_HOME")};
    unsetenv("HOME");
    unsetenv("XDG_CACHE_HOME");
    auto missing = base;
    missing.cache.clear();
    expect(ErrorKind::invalid_argument, [&] { remote(missing); });
    check(bool(remote(base)), "explicit cache requires HOME");
  }
  auto snapshot = parts;
  ++snapshot.size;
  expect(ErrorKind::integrity, [&] { sic::transport::validate_layout(snapshot, good); });
  for (unsigned field = 0; field < 6; ++field) {
    auto invalid = split;
    invalid.parts_path = work / ("bad-split-" + std::to_string(field) + ".json");
    switch (field) {
      case 0:
        invalid.prefix = "bad";
        break;
      case 1:
        invalid.part_size = 1;
        break;
      case 2:
        invalid.prefix = "d8-m1";
        break;
      case 3:
        invalid.upload_cmd = "false";
        invalid.assets_dir.clear();
        break;
      case 4:
        invalid.upload_cmd = "false";
        invalid.assets_dir = work / "upload";
        break;
      case 5:
        invalid.assets_dir = split.assets_dir;
        break;
    }
    expect(field == 2   ? ErrorKind::integrity
           : field == 5 ? ErrorKind::io
                        : ErrorKind::invalid_argument,
           [&] { sic::transport::split(invalid); });
  }
  for (unsigned mode = 0; mode < 3; ++mode) {
    auto upload = split;
    upload.assets_dir = work / ("upload-case-" + std::to_string(mode));
    fs::create_directory(upload.assets_dir);
    upload.file = upload.assets_dir / "source.sicx";
    copy_fixture(fixture, upload.file);
    upload.parts_path = upload.assets_dir / "parts.json";
    upload.upload_cmd = mode == 0   ? "false"
                        : mode == 1 ? "true"
                                    : "truncate -s 0 \""
                                      "$"
                                      "{0%/*}/source.sicx\"";
    upload.confirm_cmd = "printf " + std::to_string(mode == 1 ? 0 : parts.size);
    expect(mode == 0 ? ErrorKind::io : ErrorKind::integrity,
           [&] { sic::transport::split(upload); });
  }
  sic::transport::TransferJob job;
  job.parts = parts;
  job.out_dir = work;
  job.final_path = work / "state-download";
  expect(ErrorKind::io, [&] { sic::transport::repair(job); });
  write(job.final_path, {1});
  expect(ErrorKind::integrity, [&] { sic::transport::fetch({job, 0, false}); });
  write(job.final_path.string() + ".repairing", {2});
  expect(ErrorKind::io, [&] { sic::transport::repair(job); });
  expect(ErrorKind::io, [&] { sic::transport::open_rw(work / "missing-dir/file", false); });
  expect(ErrorKind::io, [] { sic::transport::preallocate(-1, 1); });
  expect(ErrorKind::resource, [&] { sic::transport::check_disk(work, UINT64_MAX, "test"); });
  expect(ErrorKind::io, [] { sic::transport::capture_sh("false"); });
  expect(ErrorKind::integrity, [&] { sic::transport::final_checks(job.final_path, parts); });
  write(job.final_path, ByteBuffer(parts.size));
  expect(ErrorKind::integrity, [&] { sic::transport::final_checks(job.final_path, parts); });

  auto scheduled = base;
  scheduled.prefetch_bytes = 1;
  scheduled.range_bytes = 1;
  scheduled.jobs = 1;
  scheduled.max_ranges = 1;
  scheduled.cache = work / "schedule-limits";
  auto scheduling = remote(scheduled);
  const std::array<sic::support::ByteRange, 2> disjoint{{{0, 1}, {2, 1}}};
  expect(ErrorKind::resource, [&] { scheduling->schedule(disjoint); });
  const sic::support::ByteRange contiguous{0, 3};
  expect(ErrorKind::resource, [&] { scheduling->schedule({&contiguous, 1}); });
  source = remote(base);
  expect(ErrorKind::integrity, [&] { source->bind_layout({}, 0, 8); });
  auto blocked_cache = base;
  blocked_cache.cache = work / "blocked-cache";
  write(blocked_cache.cache, {});
  expect(ErrorKind::io, [&] { remote(blocked_cache); });
  const auto path = work / "state-writer";
  StreamOptions options;
  {
    StreamWriter writer(path, options);
    writer.put_note("key", {1});
  }
  expect(ErrorKind::io, [&] { StreamWriter writer(path, options); });
  options.resume = true;
  options.plan[0] = 1;
  expect(ErrorKind::integrity, [&] { StreamWriter writer(path, options); });
  options.plan[0] = 0;
  {
    StreamWriter writer(path, options);
    expect(ErrorKind::integrity, [&] { writer.put_note("key", {2}); });
  }
  const auto journal = fs::path(path.string() + ".partial.journal");
  const auto accepted = bytes(journal);
  write(journal, {1});
  expect(ErrorKind::invalid_format, [&] { StreamWriter writer(path, options); });
  expect(ErrorKind::invalid_format, [&] { StreamWriter::peek_journal(journal); });
  write(journal, accepted);
  fs::remove(path.string() + ".partial");
  options.resume = false;
  expect(ErrorKind::integrity, [&] { StreamWriter writer(path, options); });
  write(path, {1});
  expect(ErrorKind::io, [&] { StreamWriter writer(path, options); });
  expect(ErrorKind::io, [&] { StreamWriter writer(work / "missing-dir/file", options); });
  const auto missing_journal = work / "missing-journal";
  write(missing_journal.string() + ".partial", {});
  expect(ErrorKind::io, [&] { StreamWriter writer(missing_journal, StreamOptions(true, true)); });
  const auto directory_partial = work / "directory-partial";
  fs::create_directory(directory_partial.string() + ".partial");
  write(directory_partial.string() + ".partial.journal", accepted);
  expect(ErrorKind::io, [&] { StreamWriter writer(directory_partial, StreamOptions(true, true)); });
  expect(ErrorKind::io, [&] { StreamWriter writer(work / std::string(234, 'x')); });
  for (const bool index_exists : {false, true}) {
    const auto part_path = work / (index_exists ? "existing-index" : "blocked-index");
    StreamOptions part_mode;
    part_mode.part = true;
    StreamWriter writer(part_path, part_mode);
    if (index_exists)
      write(part_path.string() + ".idx", {});
    else
      fs::create_directory(part_path.string() + ".idx.partial");
    expect(ErrorKind::io, [&] { writer.finish(); });
  }
  const auto replay_path = work / "replay-differs";
  {
    StreamWriter writer(replay_path);
    writer.chunk("PROV", 0, ByteBuffer{1});
  }
  {
    StreamWriter writer(replay_path, StreamOptions(true, true));
    expect(ErrorKind::integrity, [&] { writer.chunk("PROV", 0, ByteBuffer{2}); });
  }
  StreamOptions limited;
  limited.durable = false;
  limited.limits.directory_entries = 0;
  {
    StreamWriter writer(work / "directory-limit", limited);
    expect(ErrorKind::resource, [&] { writer.chunk("PROV", 0, {}); });
  }
#ifdef SIC_ENABLE_TEST_HOOKS
  const auto native_resume = work / "native-resume";
  { StreamWriter writer(native_resume); }
  for (const auto failure : {NativeFault::stat, NativeFault::truncate})
    expect_native(failure, [&] { StreamWriter writer(native_resume, StreamOptions(true, true)); });
  const auto native_reopen = work / "native-reopen";
  copy_fixture(fixture, native_reopen);
  std::vector<IndexEntry> exact_prefix(good.index().begin(),
                                       good.index().begin() + good.exact_count());
  expect_native(NativeFault::truncate, [&] {
    StreamWriter::reopen(native_reopen, {}, exact_prefix, good.index()[good.exact_count()].offset,
                         good.exact_root());
  });
  for (const auto failure : {NativeFault::truncate, NativeFault::sync}) {
    StreamWriter writer(work / (failure == NativeFault::sync ? "native-sync" : "native-truncate"),
                        StreamOptions(false));
    for (size_t i = 0; i < good.index().size(); ++i) {
      const auto& entry = good.index()[i];
      writer.chunk(entry.type_name(), entry.flags, good.chunk(i), entry.object, entry.first,
                   entry.count);
    }
    expect_native(failure, [&] { writer.finish(); });
  }
#endif
  const auto directory_reopen = work / "directory-reopen";
  fs::create_directory(directory_reopen);
  expect(ErrorKind::io, [&] {
    StreamWriter::reopen(directory_reopen, {}, good.index(), good.ldir_offset(), good.exact_root());
  });
  const auto pending = work / "pending-finalization";
  write(pending.string() + ".partial.journal", {});
  expect(ErrorKind::io, [&] {
    StreamWriter::reopen(pending, {}, good.index(), good.ldir_offset(), good.exact_root());
  });
  const auto part = work / "state-part";
  StreamOptions part_options;
  part_options.part = true;
  {
    StreamWriter writer(part, part_options);
    writer.chunk("PROV", 0, ByteBuffer{1});
    writer.finish();
    expect(ErrorKind::invalid_argument, [&] { writer.chunk("PROV", 0, ByteBuffer{1}); });
  }
  const auto idx = fs::path(part.string() + ".idx");
  const auto index = bytes(idx), data = bytes(part);
  StreamWriter target(work / "append-target", StreamOptions(false));
#ifdef SIC_ENABLE_TEST_HOOKS
  expect_native(NativeFault::stat, [&] { append_part(target, part); });
#endif
  auto corrupt = index;
  corrupt.back() ^= 1;
  write(idx, corrupt);
  expect(ErrorKind::integrity, [&] { append_part(target, part); });
  write(idx, index);
  corrupt = data;
  corrupt.back() ^= 1;
  write(part, corrupt);
  expect(ErrorKind::integrity, [&] { append_part(target, part); });
  IndexEntry entry = IndexEntry::get(index.data() + 16);
  expect(ErrorKind::integrity, [&] { StreamWriter::read_chunk_at(part, entry); });
  expect(ErrorKind::io, [&] { StreamWriter::read_chunk_at(work / "absent-part", entry); });
  fs::remove(part);
  expect(ErrorKind::io, [&] { append_part(target, part); });
  fs::remove(idx);
  expect(ErrorKind::io, [&] { append_part(target, part); });
  fs::create_directory(idx);
  expect(ErrorKind::io, [&] { append_part(target, part); });
  expect(ErrorKind::io, [&] { StreamWriter::peek_journal(idx); });
}

void file_element_failures(const Reader& good, const fs::path& fixture, const fs::path& work) {
  sicx_tests::Fixture f(fixture, work / "file-elements");
  auto rows = f.good;
  bool replaced = false;
  for (auto& row : rows) {
    if (row.entry.type_name() != "TOWR") continue;
    const auto spans = leaf_spans(row.bytes);
    ByteBuffer payload;
    for (const auto& span : spans) {
      if (good.meta().elements[span.header.element].kind == "wsq" && !replaced) {
        put_varint(payload, span.header.element);
        put_varint(payload, span.header.first);
        put_varint(payload, span.header.count);
        put_varint(payload, 2 * span.header.count);
        replaced = true;
      } else
        payload.insert(payload.end(), row.bytes.begin() + span.offset,
                       row.bytes.begin() + span.offset + span.length);
    }
    row.bytes = std::move(payload);
  }
  check(replaced, "fixture needs a wsq element");
  const auto path = work / "zero-wsq.sicx";
  sicx_tests::write(path, rows);
  Reader reader(path);
  exact::DocumentBuilder builder(reader, {});
  expect(ErrorKind::invalid_format, [&] { builder.build_json("input.sicx", ""); });
  auto odd = good.meta();
  odd.degrees = {3};
  odd.width = {1, 3};
  odd.D = 3;
  odd.smith = {3};
  odd.unit_generator_words = {{0}};
  odd.rescaling_shifts = {0};
  odd.branches.resize(1);
  odd.elements.clear();
  odd.representative.clear();
  odd.rays.clear();
  const auto odd_path = work / "zero-actdelta.sicx";
  std::vector<sicx_tests::Row> odd_rows(1);
  std::memcpy(odd_rows[0].entry.type.data(), "EXMT", 4);
  odd_rows[0].entry.flags = kRequired;
  const auto add = [&](ElementId id, long value, bool zero_divisor = false) {
    ElementInfo info;
    info.kind = id.kind;
    info.level = id.level;
    info.axis = id.axis;
    info.k = id.k;
    info.position = id.position;
    info.scale = "1";
    info.max_bits = 2;
    if (zero_divisor) info.levels = {0};
    info.layout = SubLayout(odd.degrees, odd.width, info.levels);
    derive_blocks(info, odd.block_coordinates);
    const auto element = odd.elements.size();
    odd.elements.push_back(info);
    sicx_tests::Row row;
    std::memcpy(row.entry.type.data(), id.kind == "rep" ? "BODY" : "TOWR", 4);
    row.entry.flags = kRequired;
    row.entry.object = element;
    row.entry.count = zero_divisor ? 2 : 1;
    put_varint(row.bytes, element);
    put_varint(row.bytes, 0);
    put_varint(row.bytes, row.entry.count);
    put_varint(row.bytes, zero_divisor ? 5 : value ? 3 : 2);
    if (value) {
      sic::support::flint::Fmpz x;
      IntCodec codec;
      fmpz_set_si(x, value);
      codec.put(row.bytes, x);
      fmpz_zero(x);
      codec.put(row.bytes, x);
      if (zero_divisor) {
        fmpz_set_si(x, -value);
        codec.put(row.bytes, x);
        fmpz_zero(x);
        codec.put(row.bytes, x);
      }
    }
    odd_rows.push_back(std::move(row));
  };
  for (int64_t k = 0; k < 3; ++k) add({"fcoef", 0, -1, k, -1}, k == 0 ? -2 : 0);
  add({"actdelta", 0, -1, -1, -1}, 0);
  for (int64_t k = 0; k < 3; ++k) add({"actodd", 0, 0, k, -1}, 0);
  for (auto pos : divisor_positions(odd.d)) add({"rep", 1, -1, -1, static_cast<int64_t>(pos)}, 1);
  sicx_tests::set_json(odd_rows[0], meta_to_json(odd));
  sicx_tests::write(odd_path, odd_rows);
  Reader odd_reader(odd_path);
  exact::DocumentBuilder odd_builder(odd_reader, {});
  expect(ErrorKind::invalid_format, [&] { odd_builder.build_json("input.sicx", ""); });
  auto singular = odd_rows;
  singular[1].bytes = ByteBuffer{0, 0, 1, 2};
  const auto singular_path = work / "singular-polynomial.sicx";
  sicx_tests::write(singular_path, singular);
  Reader singular_reader(singular_path);
  exact::DocumentBuilder singular_builder(singular_reader, {});
  expect(ErrorKind::invalid_format, [&] {
    try {
      singular_builder.build_json("input.sicx", "");
    } catch (const sic::Error& error) {
      check(std::string(error.what()).find("trace-dual basis") != std::string::npos,
            "singular polynomial missed its owning check");
      throw;
    }
  });
  odd.degrees = {2, 3};
  odd.width = {1, 2, 6};
  odd.D = 6;
  odd.smith = {6};
  odd.rescaling_shifts = {0, 0};
  odd.branches.resize(2, odd.branches.front());
  odd.elements.clear();
  odd_rows.resize(1);
  add({"wsq", 0, -1, -1, -1}, 1);
  for (int64_t k = 0; k < 3; ++k) add({"fcoef", 1, -1, k, -1}, k == 0 ? -2 : 0);
  add({"actdelta", 1, -1, -1, -1}, -2, true);
  for (int64_t k = 0; k < 3; ++k) add({"actodd", 1, 0, k, -1}, 0);
  for (auto pos : divisor_positions(odd.d)) add({"rep", 2, -1, -1, static_cast<int64_t>(pos)}, 1);
  sicx_tests::set_json(odd_rows[0], meta_to_json(odd));
  const auto divisor_path = work / "nonzero-actdelta.sicx";
  sicx_tests::write(divisor_path, odd_rows);
  Reader divisor_reader(divisor_path);
  exact::DocumentBuilder divisor_builder(divisor_reader, {});
  expect(ErrorKind::invalid_format, [&] {
    try {
      divisor_builder.build_json("input.sicx", "");
    } catch (const sic::Error& error) {
      check(std::string(error.what()).find("actdelta element") != std::string::npos,
            "nonzero divisor missed its owning check");
      throw;
    }
  });
  auto rescaling_rows = f.good;
  auto rescaling_json = sicx_tests::json(rescaling_rows[0]);
  rescaling_json["generator_rescaling"]["shifts"].array[0] =
      sic::support::Json::integer_value(8 * kMaxIntegerBytes + 1);
  sicx_tests::set_json(rescaling_rows[0], rescaling_json);
  const auto rescaling_path = work / "reconstruction-resource.sicx";
  sicx_tests::write(rescaling_path, rescaling_rows);
  expect(ErrorKind::resource, [&] {
    Reader limited(rescaling_path);
    verify_exact(limited, 1);
  });
  exact::BuildOptions precision;
  precision.places = 15;
  exact::DocumentBuilder limited_builder(good, precision);
  expect(ErrorKind::precision, [&] { limited_builder.build_json("input.sicx", ""); });
  const auto digest_path = work / "logical-digest.sicx";
  sicx_tests::write(digest_path, f.good, [](auto& logical, auto&) { logical[1].sha256[0] ^= 1; });
  expect(ErrorKind::integrity, [&] {
    Reader damaged(digest_path);
    damaged.chunk(1);
  });
  for (const bool first_chunk : {true, false}) {
    auto oversized_rows = f.good;
    if (first_chunk) {
      auto json = sicx_tests::json(oversized_rows[0]);
      for (auto& element : json["elements"].array)
        element["scale"] = sic::support::Json::string_value(std::string(3000, '9'));
      sicx_tests::set_json(oversized_rows[0], json);
    } else {
      std::erase_if(oversized_rows,
                    [](const auto& row) { return row.entry.type_name() == "PROV"; });
      sicx_tests::Row padding;
      std::memcpy(padding.entry.type.data(), "PROV", 4);
      auto json = sic::support::Json::object_value();
      json["padding"] = sic::support::Json::string_value(std::string(65536, 'x'));
      sicx_tests::set_json(padding, json);
      const auto position =
          std::find_if(oversized_rows.begin(), oversized_rows.end(),
                       [](const auto& row) { return row.entry.type_name() == "RAYS"; });
      oversized_rows.insert(position, std::move(padding));
    }
    const auto path = work / (first_chunk ? "large-first.sicx" : "large-later.sicx");
    sicx_tests::write(path, oversized_rows);
    Reader oversized(path);
    const auto ceiling = oversized.size() - sic::transport::suffix_offset(oversized);
    check((oversized.index()[0].length + 16 > ceiling) == first_chunk,
          "custom ceiling did not isolate the intended physical chunk");
    expect(ErrorKind::unsupported, [&] {
      try {
        sic::transport::split_ranges(oversized, ceiling);
      } catch (const sic::Error& error) {
        check(std::string(error.what()).find("indivisible transport chunk") != std::string::npos,
              "custom ceiling reached a different refusal");
        throw;
      }
    });
  }
  const auto codec_path = work / "unsupported-codec.sicx";
  sicx_tests::write(codec_path, f.good, [](auto&, auto& index) { index[1].codec = 1; });
  auto source = std::make_shared<TestSource>(bytes(codec_path));
  expect(ErrorKind::unsupported, [&] { Reader unsupported(source); });
  check(source->invalidations != 0, "unsupported codec bootstrap retained");
}

void finalization_failures(const Reader& good, const fs::path& fixture, const fs::path& work) {
  auto meta = good.meta();
  std::vector<FinalizeRay> rays{{"c0", std::vector<uint64_t>(meta.smith.size()), 0}};
  std::vector<std::vector<BallPart>> parts(1, std::vector<BallPart>(2 * meta.d));
  for (auto& part : parts[0]) part.mantissa.resize(1);
  sic::support::flint::Arb value;
  expect(ErrorKind::invalid_argument, [&] { outward_ball_part(value, 0); });
  expect(ErrorKind::invalid_argument, [&] { outward_ball_part(value, kMaximumBallLimbs + 1); });
  arb_indeterminate(value);
  expect(ErrorKind::invalid_argument, [&] { outward_ball_part(value, 1); });
  expect(ErrorKind::invalid_argument, [&] { build_finalization(meta, {}, {}, 128); });
  expect(ErrorKind::invalid_argument, [&] { build_finalization(meta, rays, {}, 128); });
  for (const auto bits : {uint64_t(0), kMaximumWorkingBits + 1})
    expect(ErrorKind::invalid_argument, [&] { build_finalization(meta, rays, parts, bits); });
  auto bad_parts = parts;
  bad_parts[0][0].mantissa.clear();
  expect(ErrorKind::invalid_argument, [&] { build_finalization(meta, rays, bad_parts, 128); });
  bad_parts = parts;
  bad_parts[0].back().mantissa.resize(2);
  expect(ErrorKind::invalid_argument, [&] { build_finalization(meta, rays, bad_parts, 128); });
  auto bad_rays = rays;
  bad_rays[0].anchor = -1;
  expect(ErrorKind::invalid_argument, [&] { build_finalization(meta, bad_rays, parts, 128); });
  expect(ErrorKind::invalid_argument, [] { ray_class("bad"); });
  expect(ErrorKind::invalid_argument, [] { require_multiplet("bad"); });
  expect(ErrorKind::resource,
         [] { validate_branch_string(std::string(kMaximumBranchTextBytes + 1, '0')); });
  const LeafSource one = [](const LeafVisitor& visit) {
    FmpzPair pair;
    fmpz_one(pair.x);
    visit(0, pair.x, pair.y);
  };
  auto shifts = good.meta().rescaling_shifts;
  shifts[0] = 8 * kMaxIntegerBytes + 1;
  auto unscaled = unrescaled_source(good.meta(), {"wsq", 0, -1, -1, -1}, one, shifts);
  expect(ErrorKind::resource, [&] { unscaled([](auto, auto, auto) {}); });
  auto json = meta_to_json(good.meta());
  json["elements"].array[0]["max_bits"] =
      sic::support::Json::integer_value(8 * kMaxIntegerBytes + 1);
  expect(ErrorKind::resource, [&] { meta_from_json(json); });
  json = meta_to_json(good.meta());
  json["required_features"].array.push_back(sic::support::Json::string_value("unknown-feature"));
  expect(ErrorKind::unsupported, [&] { meta_from_json(json); });
  json = rays_to_json(good.meta());
  json["decoder"] = sic::support::Json::object_value();
  json["decoder"]["name"] =
      sic::support::Json::string_value("unknown-decoder");  // identity-negative
  json["decoder"]["source_commit"] = sic::support::Json::string_value(std::string(40, '0'));
  expect(ErrorKind::unsupported, [&] { rays_from_json(json, meta); });
  const auto path = work / "final-root";
  copy_fixture(fixture, path);
  expect(ErrorKind::integrity, [&] { finalize_in_place(path, meta, {}, false, Digest{}); });
  std::vector<IndexEntry> prefix(good.index().begin(), good.index().begin() + good.exact_count());
  {
    auto staged = StreamWriter::reopen(path, {}, prefix, good.index()[good.exact_count()].offset,
                                       good.exact_root());
  }
  expect(ErrorKind::integrity, [&] { finalize_in_place(path, good.meta(), {}, true, Digest{}); });
}

void text_failures(const Reader& reader, const fs::path& fixture, const fs::path& work) {
  std::ostringstream output;
  StreamSink sink(output);
  dump_text_json(reader, sic::support::sha256::file_hex(fixture), sink, 1);
  const auto original = output.str();
  const auto first_end = original.find('\n');
  const auto header = sic::support::JsonParser::parse(original.substr(0, first_end));
  for (unsigned field = 0; field < 4; ++field) {
    auto changed = header;
    if (field == 0)
      changed["schema"] = sic::support::Json::string_value("unknown-schema");  // identity-negative
    if (field == 1)
      changed["required_features"].array.push_back(sic::support::Json::string_value("unknown"));
    if (field == 2)
      changed["content_hash"] =
          sic::support::Json::string_value("unknown-hash");  // identity-negative
    if (field == 3) changed["sicx_version"].array[0] = sic::support::Json::integer_value(2);
    const auto input = work / ("header-" + std::to_string(field) + ".jsonl");
    const auto text = sic::support::dump_json(changed) + original.substr(first_end);
    write(input, ByteBuffer(text.begin(), text.end()));
    expect(ErrorKind::unsupported, [&] { text_json_to_binary(input, input.string() + ".sicx"); });
  }
  std::ostringstream broken_output;
  broken_output.setstate(std::ios::badbit);
  StreamSink broken_sink(broken_output);
  expect(ErrorKind::io, [&] { broken_sink.line("test"); });
  const auto existing_text = work / "existing-text";
  write(existing_text, {});
  expect(ErrorKind::io, [&] { text_json_to_binary(fixture, existing_text); });
  const auto last = original.rfind('\n', original.size() - 2) + 1;
  const auto final =
      sic::support::JsonParser::parse(original.substr(last, original.size() - last - 1));
  size_t count = 0;
  for (const auto key : {"content_sha256", "merkle_root", "exact_root", "sicx_sha256"}) {
    auto changed = final;
    changed[key] = sic::support::Json::string_value(std::string(64, '0'));
    const auto input = work / ("bad-text-" + std::to_string(count++) + ".jsonl");
    const auto target = input.string() + ".sicx";
    const auto text = original.substr(0, last) + sic::support::dump_json(changed) + "\n";
    write(input, ByteBuffer(text.begin(), text.end()));
    expect(ErrorKind::integrity, [&] { text_json_to_binary(input, target); });
    check(!fs::exists(target), "bad text published output");
  }
  auto index = sic::support::Json::object_value();
  index["schema"] = sic::support::Json::string_value("sicx-text-json-parts/1.0");
  index["parts"] = sic::support::Json::array_value();
  const auto input = work / "empty.parts.json";
  auto text = sic::support::dump_json(index) + "\n";
  write(input, ByteBuffer(text.begin(), text.end()));
  expect(ErrorKind::invalid_format, [&] { LineSource source(input); });
  {
    PartSink parts(work / "text-parts", 100);
    parts.line("test");
    parts.finish();
  }
  const auto part_file = work / "text-parts.part-00000.jsonl";
  write(part_file, ByteBuffer{'b', 'a', 'd', '\n'});
  expect(ErrorKind::integrity, [&] {
    LineSource source(work / "text-parts.parts.json");
    std::string line;
    while (source.next(line)) {
    }
  });
  expect(ErrorKind::io, [&] { LineSource source(work / "missing-text"); });
  {
    PartSink parts(work / "text-parts", 100);
    expect(ErrorKind::io, [&] { parts.line("existing"); });
  }
  {
    PartSink parts(work / "text-parts", 100);
    expect(ErrorKind::io, [&] { parts.finish(); });
  }
  {
    PartSink parts(work / "missing-parent/text", 100);
    parts.line("failed stream");
    expect(ErrorKind::io, [&] { parts.finish(); });
  }
  fs::create_directory(work / "index-only");
  {
    PartSink parts(work / "index-only/text", 100);
    fs::remove(work / "index-only");
    expect(ErrorKind::io, [&] { parts.finish(); });
  }
  expect(ErrorKind::resource,
         [] { sic::transport::parse_parts(std::string(kMaxTextChunk + 1, ' ')); });
  index["schema"] =
      sic::support::Json::string_value("sicx-text-json-parts/2.0");  // identity-negative
  text = sic::support::dump_json(index) + "\n";
  write(input, ByteBuffer(text.begin(), text.end()));
  expect(ErrorKind::unsupported, [&] { LineSource source(input); });
}

void reader_failures(const ByteBuffer& original) {
  Reader good(std::make_shared<TestSource>(original));
  const size_t payload = 1;
  check(good.index().size() > payload, "fixture has no payload");
  for (const bool bootstrap : {false, true}) {
    for (const auto kind : {ErrorKind::invalid_format, ErrorKind::integrity, ErrorKind::io,
                            ErrorKind::resource, ErrorKind::cancelled, ErrorKind::unsupported}) {
      for (const bool cleanup_failure : {false, true}) {
        auto source = std::make_shared<TestSource>(original);
        source->failure = kind;
        source->fail_at = bootstrap ? 0 : good.index()[payload].offset + 16;
        source->fail_cleanup = cleanup_failure;
        bool preserved = false;
        try {
          Reader reader(source);
          if (!bootstrap) reader.chunk(payload);
        } catch (const sic::Error& error) {
          preserved = error.kind() == kind && std::string(error.what()) == "original read failure";
        }
        check(preserved, "reader replaced the original failure");
        const bool corruption = kind == ErrorKind::integrity || kind == ErrorKind::invalid_format ||
                                (bootstrap && kind == ErrorKind::unsupported);
        check((source->invalidations != 0) == corruption, "wrong fragment retirement policy");
      }
    }
  }
  auto damaged = original;
  damaged[0] ^= 1;
  auto source = std::make_shared<TestSource>(damaged);
  expect(ErrorKind::invalid_format, [&] { Reader reader(source); });
  check(source->invalidations != 0, "malformed bootstrap retained");
  damaged = original;
  damaged[8] = 2;
  source = std::make_shared<TestSource>(damaged);
  expect(ErrorKind::unsupported, [&] { Reader reader(source); });
  check(source->invalidations != 0, "unsupported bootstrap retained");
  damaged = original;
  damaged[good.index()[payload].offset + 16] ^= 1;
  source = std::make_shared<TestSource>(damaged);
  Reader reader(source);
  expect(ErrorKind::integrity, [&] { reader.chunk(payload); });
  check(source->invalidations == 1, "digest corruption retained");
  ResourceLimits limit;
  limit.directory_entries = 0;
  expect(ErrorKind::resource,
         [&] { Reader limited(std::make_shared<TestSource>(original), limit); });
}

#ifdef SIC_ENABLE_TEST_HOOKS
std::string fault_operation;
bool after_rename = false, rename_seen = false;
size_t fault_hits = 0;
std::optional<ErrorKind> read_kind;
std::optional<ErrorKind> full_kind;
// Asset renames have no immediate parent sync. Their next parent sync belongs
// to manifest publication in support::atomic_write; fault the actual syscall.
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd) {
  if (native_fault == NativeFault::sync) {
    ++native_hits;
    errno = EIO;
    return -1;
  }
  struct stat state {};
  if (after_rename && rename_seen && fault_operation == "part_asset" && ::fstat(fd, &state) == 0 &&
      S_ISDIR(state.st_mode)) {
    ++fault_hits;
    errno = EIO;
    return -1;
  }
  return __real_fsync(fd);
}
void fault(std::string_view operation, const fs::path& target) {
  if (operation == "read" && read_kind) throw sic::Error(*read_kind, "injected range read");
  if (operation == "full_validation" && full_kind) {
    if (*full_kind != ErrorKind::io) throw sic::Error(*full_kind, "injected full validation");
    throw fs::filesystem_error("injected full stat", target,
                               std::make_error_code(std::errc::io_error));
  }
  if (operation == fault_operation) {
    rename_seen = true;
    if (after_rename) return;
  } else if (!(after_rename && rename_seen && operation == "parent_sync"))
    return;
  ++fault_hits;
  throw fs::filesystem_error("injected I/O", target, std::make_error_code(std::errc::io_error));
}
struct FaultScope {
  FaultScope(std::string operation, bool after = false) {
    fault_operation = std::move(operation);
    after_rename = after;
    rename_seen = false;
    fault_hits = 0;
    io_fault_hook = fault;
  }
  ~FaultScope() {
    io_fault_hook = nullptr;
    after_rename = rename_seen = false;
    read_kind.reset();
    full_kind.reset();
  }
};
template <class Operation>
void rename_failure(const std::string& name, bool after, Operation&& operation) {
  FaultScope hook(name, after);
  expect(ErrorKind::io, std::forward<Operation>(operation));
  check(fault_hits == 1, "rename or parent-sync seam not reached: " + name);
  std::cout << name << (after ? " parent sync" : " rename") << " fault passed\n";
}

void range_failures(const fs::path& work) {
  sic::transport::Parts parts;
  parts.size = 4;
  parts.sha256 = std::string(64, 'a');
  sic::access::RemoteOptions options;
  sic::access::RangeCache cache(parts, options);
  cache.directory_ = work / "ranges";
  fs::create_directory(cache.directory_);
  cache.store({0, 4}, ByteBuffer{1, 2, 3, 4});
  sic::access::RangeCache::Entry entry{4, cache.entry_name({0, 4})};
  for (const auto kind : {ErrorKind::io, ErrorKind::invalid_format, ErrorKind::integrity,
                          ErrorKind::resource, ErrorKind::cancelled}) {
    cache.bad_.clear();
    FaultScope hook("none");
    read_kind = kind;
    if (kind == ErrorKind::resource || kind == ErrorKind::cancelled) {
      expect(kind, [&] { cache.load(0, entry); });
      check(cache.bad_.empty(), "noncorruption range read marked bad");
    } else {
      check(cache.load(0, entry).empty() && cache.bad_.contains(0),
            "bad range did not become miss");
    }
    check(fs::exists(cache.directory_ / entry.name), "range load deleted data");
  }
  for (const bool whole : {false, true}) {
    auto broken = parts;
    broken.sha256 = sic::access::checksum(std::string_view("data"));
    broken.parts = {{0, 0, 4, whole ? sic::access::checksum("data") : std::string(64, '0'),
                     "d4-m1.sicx.part-0000"}};
    if (whole) broken.sha256 = std::string(64, '0');
    sic::access::RangeCache verify(broken, options);
    verify.directory_ = work / (whole ? "whole-integrity" : "part-integrity");
    fs::create_directory(verify.directory_);
    verify.store({0, 4}, ByteBuffer{'d', 'a', 't', 'a'});
    verify.scan();
    expect(ErrorKind::integrity, [&] { verify.verify_complete_parts(); });
  }
  cache.store({0, 1}, ByteBuffer{1});
  cache.store({1, 1}, ByteBuffer{2});
  auto limited_options = options;
  limited_options.max_ranges = 1;
  sic::access::RangeCache limited(parts, limited_options);
  limited.directory_ = cache.directory_;
  bool resource_refusal = false;
  try {
    limited.scan();
  } catch (const sic::access::NeedFull& error) {
    resource_refusal = error.kind() == ErrorKind::resource;
  }
  check(resource_refusal, "cache extent refusal lost subtype or resource category");
}

void rename_failures(const fs::path& fixture, const fs::path& work) {
  finalization_publication_failures(fixture, work);
  Reader reader(fixture);
  auto index = reader.index();
  index.resize(reader.exact_count());
  for (const bool after : {false, true}) {
    const auto dir = work / (after ? "sync" : "rename");
    fs::create_directory(dir);
    const auto journal_target = dir / "journal.sicx";
    rename_failure("journal", after, [&] { StreamWriter writer(journal_target); });
    check(fs::exists(journal_target.string() + ".partial"), "journal failure lost partial");
    check(!fs::exists(journal_target), "journal failure published output");
    check(fs::exists(journal_target.string() + ".partial.journal") == after,
          "journal commit state differs");
    const auto reopen = dir / "reopen.sicx";
    copy_fixture(fixture, reopen);
    rename_failure("reopen", after, [&] {
      StreamWriter::reopen(reopen, {}, index, reader.ldir_offset(), reader.exact_root());
    });
    check(fs::exists(reopen) != after && fs::exists(reopen.string() + ".partial") == after,
          "reopen lost source");
    check(bytes(after ? fs::path(reopen.string() + ".partial") : reopen) == bytes(fixture),
          "reopen changed accepted bytes");
    check(fs::exists(reopen.string() + ".partial.journal"), "reopen lost recovery journal");
    if (after) {
      // The before-rename attempt leaves precisely the resumable journal-only state.
      const auto resume = dir / "resume.sicx";
      copy_fixture(fixture, resume);
      rename_failure("reopen", false, [&] {
        StreamWriter::reopen(resume, {}, index, reader.ldir_offset(), reader.exact_root());
      });
      rename_failure("resume_reopen", true, [&] { StreamWriter::resume_reopen(resume, {}); });
      check(bytes(resume.string() + ".partial") == bytes(fixture), "resume sync lost source");
    } else {
      rename_failure("resume_reopen", false, [&] { StreamWriter::resume_reopen(reopen, {}); });
      check(bytes(reopen) == bytes(fixture), "resume rename lost source");
      auto resumed = StreamWriter::resume_reopen(reopen, {});
      check(resumed->resumed().size() == index.size(), "journal could not resume");
    }
    const auto part = dir / "part";
    {
      StreamOptions options;
      options.part = true;
      StreamWriter writer(part, options);
      writer.chunk("PROV", 0, ByteBuffer{'{', '}'});
      rename_failure("part_index", after, [&] { writer.finish(); });
    }
    check(fs::exists(part.string() + ".idx") == after, "part index commit state differs");
    check(fs::exists(part.string() + ".partial") != after, "part data state differs");
    StreamOptions part_resume;
    part_resume.part = true;
    part_resume.resume = true;
    if (after) {
      const auto committed = bytes(part);
      expect(ErrorKind::io, [&] { StreamWriter writer(part, part_resume); });
      check(bytes(part) == committed, "part retry clobbered committed data");
    } else {
      StreamWriter resumed(part, part_resume);
      check(resumed.pending_replay() == 1, "part index failure lost accepted chunk");
      resumed.adopt(1);
      const auto accepted = resumed.index()[0];
      resumed.finish();
      check(StreamWriter::read_chunk_at(part, accepted) == ByteBuffer({'{', '}'}),
            "part index failure could not resume");
    }

    const auto text = dir / "text.jsonl", reconstructed = dir / "text.sicx";
    {
      std::ofstream out(text);
      StreamSink sink(out);
      dump_text_json(reader, sic::support::sha256::file_hex(fixture), sink, 1);
    }
    rename_failure("text_reconstruction", after, [&] { text_json_to_binary(text, reconstructed); });
    check(fs::exists(reconstructed) == after, "text publication state differs");
    if (after) check(bytes(reconstructed) == bytes(fixture), "text sync changed accepted data");
    for (const auto suffix : {".rebuild", ".rebuild.partial", ".rebuild.partial.journal"})
      check(!fs::exists(reconstructed.string() + suffix), "text scratch leaked");

    sic::transport::SplitOptions split;
    split.file = fixture;
    split.assets_dir = dir / "failed-assets";
    split.parts_path = dir / "failed-parts.json";
    split.prefix = "d4-m1";
    rename_failure("part_asset", after, [&] { sic::transport::split(split); });
    check(fs::exists(split.parts_path) == after, "manifest commit state differs");
    check(fs::exists(split.assets_dir / sic::transport::asset_name(split.prefix, 0)) == after,
          "asset commit state differs");
    const auto staged_asset =
        split.assets_dir / ("." + sic::transport::asset_name(split.prefix, 0) + ".tmp");
    check(fs::exists(staged_asset) != after, "asset scratch state differs");
    if (!after)
      check(bytes(staged_asset) == bytes(fixture), "asset rename failure lost staged bytes");
    else
      check(sic::transport::parse_parts(sic::transport::read_text(split.parts_path)).sha256 ==
                sic::support::sha256::file_hex(fixture),
            "manifest parent-sync failure damaged committed manifest");
    split.assets_dir = dir / "assets";
    split.parts_path = dir / "parts.json";
    const auto parts = sic::transport::split(split);
    auto manifest = sic::transport::parts_to_json(parts);
    manifest["schema"] =
        sic::support::Json::string_value("sicx-transport-parts/2.0");  // identity-negative
    expect(ErrorKind::unsupported,
           [&] { sic::transport::parse_parts(sic::support::dump_json(manifest)); });
    manifest = sic::transport::parts_to_json(parts);
    manifest["parts"].array.clear();
    expect(ErrorKind::invalid_format,
           [&] { sic::transport::parse_parts(sic::support::dump_json(manifest)); });
    auto wrong_dimension = parts;
    wrong_dimension.parts[0].asset = "d8-m1.sicx.part-0000";
    expect(ErrorKind::integrity, [&] { sic::transport::validate_layout(wrong_dimension, reader); });
    sic::transport::TransferJob job;
    job.parts = parts;
    job.out_dir = dir;
    job.final_path = dir / "download.sicx";
    job.source = split.assets_dir.string();
    job.jobs = 1;
    rename_failure("fetch", after, [&] { sic::transport::fetch({job, 0, false}); });
    check(fs::exists(job.final_path) == after, "fetch commit state differs");
    check(bytes(after ? job.final_path : fs::path(job.final_path.string() + ".part")) ==
              bytes(fixture),
          "fetch lost verified work file");
    if (!after) sic::transport::fetch({job, 0, false});
    rename_failure("repair_park", after, [&] { sic::transport::repair(job); });
    const auto parked = fs::path(job.final_path.string() + ".repairing");
    check(bytes(after ? parked : job.final_path) == bytes(fixture), "repair park lost data");
    rename_failure("repair_restore", after, [&] { sic::transport::repair(job); });
    check(bytes(after ? job.final_path : parked) == bytes(fixture), "repair restore lost data");
    if (!after) sic::transport::repair(job);

    sic::access::RemoteOptions remote;
    remote.source = split.assets_dir.string();
    remote.cache = dir / "cache";
    remote.jobs = 1;
    remote.prefetch_bytes = 0;
    remote.max_fraction = 0.01;
    const sic::support::ByteRange all{0, parts.size};
    auto source = sic::access::remote_byte_source(split.parts_path, remote);
    bool need_full = false;
    try {
      source->plan({&all, 1});
    } catch (const sic::access::NeedFull& error) {
      need_full = error.kind() == ErrorKind::unsupported;
    }
    check(need_full && source->stats().transferred == 0, "NeedFull permission changed");
    source.reset();
    remote.fetch_full = true;
    source = sic::access::remote_byte_source(split.parts_path, remote);
    rename_failure("full_download", after, [&] { source->plan({&all, 1}); });
    const auto full =
        remote.cache / "ranges" / parts.exact_root / parts.file_root /
        sic::access::checksum(sic::support::dump_json(sic::transport::parts_to_json(parts))) /
        "full.sicx";
    check(fs::exists(full) == after, "full cache publication state differs");
    check(bytes(after ? full : full.parent_path() / "full.download") == bytes(fixture),
          "full cache lost verified work file");
    check(!source->stats().full_download, "failed full download reported success");
    source.reset();
    auto reused = sic::access::remote_byte_source(split.parts_path, remote);
    reused->plan({&all, 1});
    check(reused->stats().full_download, "full cache could not resume");
    if (after) check(reused->stats().transferred == 0, "healthy committed cache was refetched");
    reused.reset();
    source.reset();
    for (const bool during_admission : {false, true}) {
      for (const auto kind : {ErrorKind::io, ErrorKind::resource, ErrorKind::cancelled}) {
        if (during_admission) {
          fs::remove(full);
          source = sic::access::remote_byte_source(split.parts_path, remote);
          copy_fixture(fixture, full);
        }
        const auto accepted = bytes(full);
        {
          FaultScope hook("none");
          full_kind = kind;
          expect(kind, [&] {
            if (during_admission)
              source->plan({&all, 1});
            else
              source = sic::access::remote_byte_source(split.parts_path, remote);
          });
        }
        check(bytes(full) == accepted, "noncorruption failure deleted healthy full cache");
        if (source) check(source->stats().transferred == 0, "noncorruption failure refetched");
        source.reset();
      }
    }
    for (const auto url : {"file:///tmp/%zz", "file:///tmp/%"}) {
      auto bad_url = remote;
      bad_url.source = url;
      expect(ErrorKind::invalid_argument,
             [&] { sic::access::remote_byte_source(split.parts_path, bad_url); });
    }
    for (const bool exact : {false, true}) {
      auto pinned = remote;
      (exact ? pinned.exact_root : pinned.root) = std::string(64, '0');
      expect(ErrorKind::integrity,
             [&] { sic::access::remote_byte_source(split.parts_path, pinned); });
    }
    auto finite_budget = remote;
    finite_budget.memory_budget = UINT64_MAX - 1;
    expect_native(NativeFault::usage,
                  [&] { sic::access::remote_byte_source(split.parts_path, finite_budget); });
    auto memory_limited = remote;
    auto measured = sic::access::remote_byte_source(split.parts_path, remote);
    memory_limited.memory_budget = measured->memory_reservation() + 1;
    measured.reset();
    expect(ErrorKind::resource,
           [&] { sic::access::remote_byte_source(split.parts_path, memory_limited); });
    for (const bool lock : {false, true}) {
      auto blocked = remote;
      blocked.cache = dir / (lock ? "blocked-lock" : "blocked-download");
      auto blocked_source = sic::access::remote_byte_source(split.parts_path, blocked);
      const auto cache_base =
          blocked.cache / "ranges" / parts.exact_root / parts.file_root /
          sic::access::checksum(sic::support::dump_json(sic::transport::parts_to_json(parts)));
      fs::create_directory(cache_base / (lock ? "full.lock" : "full.download"));
      expect(ErrorKind::io, [&] { blocked_source->plan({&all, 1}); });
    }
    const auto asset = split.assets_dir / parts.parts[0].asset;
    const auto accepted_asset = bytes(asset);
    write(asset, ByteBuffer{1});
    for (const bool full_download : {false, true}) {
      auto bad_part = remote;
      bad_part.cache = dir / (full_download ? "bad-full" : "bad-range");
      bad_part.fetch_full = full_download;
      bad_part.max_fraction = full_download ? 0.01 : 1;
      auto bad_source = sic::access::remote_byte_source(split.parts_path, bad_part);
      expect(ErrorKind::integrity, [&] {
        if (full_download)
          bad_source->plan({&all, 1});
        else {
          uint8_t first;
          bad_source->read(0, {&first, 1});
        }
      });
    }
    auto same_size = accepted_asset;
    same_size[0] ^= 1;
    write(asset, same_size);
    auto corrupt_download = remote;
    corrupt_download.cache = dir / "corrupt-download";
    auto corrupt_source = sic::access::remote_byte_source(split.parts_path, corrupt_download);
    expect(ErrorKind::integrity, [&] { corrupt_source->plan({&all, 1}); });
    write(asset, accepted_asset);
    // An actual stopped source still propagates cancellation before transfer.
    auto token = std::make_shared<sic::support::StopToken>();
    auto stopped = sic::access::remote_byte_source(split.parts_path, remote);
    stopped->bind_cancellation(token);
    token->request_stop();
    uint8_t byte;
    expect(ErrorKind::cancelled, [&] { stopped->read(0, {&byte, 1}); });
  }
}
#endif
}  // namespace

int main(int argc, char** argv) try {
  check(argc == 3, "expected fixture and work directory");
  const fs::path fixture = argv[1], work = argv[2];
  fs::remove_all(work);
  fs::create_directories(work);
  Reader reader(fixture);
  producer_failures(reader, work);
  primitives(reader, fixture, work);
  request_and_state_failures(reader, fixture, work);
  finalization_failures(reader, fixture, work);
  file_element_failures(reader, fixture, work);
  text_failures(reader, fixture, work);
  reader_failures(bytes(fixture));
#ifdef SIC_ENABLE_TEST_HOOKS
  range_failures(work);
  rename_failures(fixture, work);
#endif
  std::cout << "failure category and recovery controls passed\n";
  flint_cleanup_master();
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
