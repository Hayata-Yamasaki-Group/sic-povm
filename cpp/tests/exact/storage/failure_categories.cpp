// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <source_location>
#include <sstream>

#include "assertions.hpp"
#include "exact/storage/completed_class_cache.hpp"
#include "exact/storage/leaf_source.hpp"
#include "support/io_faults.hpp"

using namespace sic::exact;
using sic::Error;
using sic::ErrorKind;

namespace {
using faults::replace_on_seek;
using faults::seek_failure;
using faults::short_fwrite;
using faults::sync_countdown;
using faults::sync_observer;
using faults::trailing_byte;
using faults::trailing_read_error;

template <class F>
std::string expect(ErrorKind kind, F&& action,
                   std::source_location location = std::source_location::current()) {
  bool caught = false;
  std::string diagnostic;
  try {
    action();
  } catch (const Error& error) {
    caught = true;
    diagnostic = error.what();
    if (error.kind() != kind)
      std::fprintf(stderr, "wrong category at line %u: %s\n", location.line(), error.what());
    SIC_CHECK(error.kind() == kind);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "uncategorized failure: %s\n", error.what());
  }
  if (!caught) std::fprintf(stderr, "expected categorized failure at line %u\n", location.line());
  SIC_CHECK(caught);
  return diagnostic;
}

using File = std::unique_ptr<FILE, decltype(&fclose)>;

File bytes(const std::string& data) {
  File file(tmpfile(), fclose);
  if (!file) throw std::runtime_error("cannot create test input");
  SIC_CHECK(fwrite(data.data(), 1, data.size(), file.get()) == data.size());
  rewind(file.get());
  return file;
}

ssize_t fail_read(void*, char*, size_t) {
  errno = EIO;
  return -1;
}
struct WriteState {
  int calls = 0;
  int pass = 0;
};
ssize_t fail_write(void* cookie, const char*, size_t size) {
  if (cookie) {
    auto& state = *static_cast<WriteState*>(cookie);
    if (++state.calls <= state.pass) return size;
  }
  errno = EDQUOT;
  return 0;
}

File broken_stream(bool writing, WriteState* state = nullptr) {
  cookie_io_functions_t functions{};
  if (writing)
    functions.write = fail_write;
  else
    functions.read = fail_read;
  File file(fopencookie(state, writing ? "w" : "r", functions), fclose);
  if (!file) throw std::runtime_error("cannot create test stream");
  SIC_CHECK(setvbuf(file.get(), nullptr, _IONBF, 0) == 0);
  return file;
}

std::string read(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}
void write(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(text.data(), text.size());
  if (!output) throw std::runtime_error("cannot write test fixture");
}
void authenticate(const std::filesystem::path& path) {
  write(path.string() + ".sha256", checkpoint_digest(path) + "\n");
}
}  // namespace

int main() {
  char pattern[] = "/tmp/sic-storage-categories-XXXXXX";
  const char* name = mkdtemp(pattern);
  SIC_CHECK(name);
  if (!name) return sic_test::report();
  const std::filesystem::path root(name);
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
  } cleanup{root};

  // Separate short EOF from an actual stream error, through both codec entry points.
  Leaf integer;
  for (const auto& input : {std::string(), std::string("\x80", 1), std::string("\x02", 1),
                            std::string("\x12\x01", 2)}) {
    auto file = bytes(input);
    leaf_codec::Count budget = 1024;
    expect(ErrorKind::invalid_format,
           [&] { leaf_codec::read_integer(file.get(), integer.x, budget); });
    SIC_CHECK(!ferror(file.get()));
  }
  for (const auto& input :
       {std::string("\1", 1), std::string("\2\0", 2), std::string("\x80\0", 2),
        std::string(10, '\xff'), std::string("\x12", 1) + std::string(9, '\0')}) {
    auto file = bytes(input);
    leaf_codec::Count budget = 1024;
    expect(ErrorKind::invalid_format,
           [&] { leaf_codec::read_integer(file.get(), integer.x, budget); });
  }
  {
    auto file = broken_stream(false);
    expect(ErrorKind::io, [&] { leaf_codec::read_unsigned(file.get()); });
    SIC_CHECK(ferror(file.get()));
  }
  for (unsigned size : {1U, 9U}) {
    auto file = broken_stream(false);
    // Supply the size varint via ungetc; the magnitude then encounters EIO.
    SIC_CHECK(ungetc(2 * size, file.get()) != EOF);
    leaf_codec::Count budget = 1024;
    expect(ErrorKind::io, [&] { leaf_codec::read_integer(file.get(), integer.x, budget); });
    SIC_CHECK(ferror(file.get()));
  }
  for (unsigned bits : {1U, 100U}) {
    WriteState state{0, 1};
    auto file = broken_stream(true, &state);
    fmpz_one(integer.x);
    fmpz_mul_2exp(integer.x, integer.x, bits);
    expect(ErrorKind::io, [&] { leaf_codec::write_integer(file.get(), integer.x); });
    SIC_CHECK(state.calls > state.pass);
    SIC_CHECK(ferror(file.get()));
    file = bytes("");
    short_fwrite = true;
    expect(ErrorKind::io, [&] { leaf_codec::write_integer(file.get(), integer.x); });
    SIC_CHECK(!short_fwrite && !ferror(file.get()));
  }
  {
    auto file = bytes("\2");
    leaf_codec::Count budget = 0;
    expect(ErrorKind::resource, [&] { leaf_codec::read_integer(file.get(), integer.x, budget); });
  }
  {
    auto file = bytes("");
    long value = 0;
    expect(ErrorKind::invalid_format, [&] { GroupCheckpoint::read(file.get(), &value); });
    file = broken_stream(false);
    expect(ErrorKind::io, [&] { GroupCheckpoint::read(file.get(), &value); });
    file = broken_stream(true);
    expect(ErrorKind::io, [&] { GroupCheckpoint::write(file.get(), &value); });
  }

  // The ordered BallFile handlers must preserve the codec's resource category.
  sic::support::RealBallVector real(1);
  arb_one(real[0]);
  auto backing = BallFile::save(real);
  const auto persistent = root / "budget.balls";
  auto header = read(backing->path()).substr(0, 6 * sizeof(std::uint64_t));
  {
    auto file = bytes("");
    leaf_codec::write_unsigned(file.get(), 2 * (128 * sic::support::resources::kMib + 1));
    SIC_CHECK(fflush(file.get()) == 0);
    rewind(file.get());
    for (int byte; (byte = fgetc(file.get())) != EOF;) header += char(byte);
  }
  write(persistent, header);
  auto oversized = BallFile::open(persistent, 1, 1, header.size(), checkpoint_digest(persistent));
  expect(ErrorKind::resource, [&] { oversized->real(); });
  header.resize(6 * sizeof(std::uint64_t));
  header += '\1';
  write(persistent, header);
  auto malformed = BallFile::open(persistent, 1, 1, header.size(), checkpoint_digest(persistent));
  expect(ErrorKind::invalid_format, [&] { malformed->real(); });
  expect(ErrorKind::io, [&] { BallFile::open(root / "absent", 1, 1, 1, std::string(64, '0')); });

  // Newly typed spool errors still roll back an unissued generated suffix.
  LeafSpool spool(true, false);
  std::vector<Leaf> leaves(1);
  fmpz_one(leaves[0].x);
  const auto ticket = spool.append(leaves);
  seek_failure = true;
  expect(ErrorKind::io, [&] { spool.load(ticket); });
  const auto size_before = spool.bytes();
  expect(ErrorKind::resource, [&] {
    spool.append_generated(
        2,
        [](size_t offset, std::span<Leaf>) {
          if (offset) throw Error(ErrorKind::resource, "test producer refusal");
        },
        1);
  });
  SIC_CHECK(spool.bytes() == size_before);
  SIC_CHECK(fmpz_is_one(spool.load(ticket)[0].x));
  SIC_CHECK(spool.append(leaves).offset == (off_t)size_before);
  auto truncated_ticket = ticket;
  truncated_ticket.offset = spool.bytes();
  expect(ErrorKind::invalid_format, [&] { spool.load(truncated_ticket); });
  expect(ErrorKind::invalid_format,
         [&] { spool.read_blocks(truncated_ticket, [](size_t, std::span<Leaf>) {}); });
  truncated_ticket.index.reset();
  LeafSpool::Cursor short_cursor(spool, truncated_ticket);
  expect(ErrorKind::invalid_format, [&] { short_cursor.next(); });
  LeafSpool compressed(true, true);
  const auto compressed_ticket = compressed.append(leaves);
  auto compressed_record = [](std::string suffix) {
    const std::uint32_t length = 1;
    return std::string(reinterpret_cast<const char*>(&length), sizeof length) + suffix;
  };
  // Each malformed record reaches a separate compressed-reader rejection.
  for (const auto& record :
       {std::string(), compressed_record(""), compressed_record(std::string(1, '\x80')),
        compressed_record(std::string(1, '\1'))}) {
    replace_on_seek = record;
    expect(ErrorKind::invalid_format, [&] { compressed.load(compressed_ticket); });
    SIC_CHECK(!replace_on_seek);
  }
  storage::Private source_owner(false);
  const auto source = source_owner.append_generated(1, [](size_t, std::span<Leaf>) {});
  storage::Source::Cursor cursor(source);
  seek_failure = true;
  expect(ErrorKind::io, [&] { cursor.next(); });
  bool failed_cursor = false;
  try {
    cursor.next();
  } catch (const Error&) {
    SIC_CHECK(false);
  } catch (const std::runtime_error&) {
    failed_cursor = true;
  }
  SIC_CHECK(failed_cursor);
  {
    File file(fopen("/dev/full", "wb"), fclose);
    SIC_CHECK(file);
    SIC_CHECK(setvbuf(file.get(), nullptr, _IONBF, 0) == 0);
    expect(ErrorKind::io, [&] { write_leaf_text(file.get(), leaves); });
  }

  Arithmetic ar(3);
  Group group(ar);
  group.generate_standard();
  group.close((long)ar.h() * ar.d * ar.d / 2);
  const auto group_path = root / "group.bin";
  GroupCheckpoint::save(group, group_path);
  const auto valid_group = read(group_path);
  const auto group_checksum = group_path.string() + ".sha256";
  auto load_group = [&] {
    Group restored(ar);
    GroupCheckpoint::load(restored, group_path, group.N, [](size_t) {});
    SIC_CHECK(restored.img == group.img);
  };
  expect(ErrorKind::io, [&] { GroupCheckpoint::save(group, group_path); });
  SIC_CHECK(read(group_path) == valid_group);
  write(group_checksum, "z" + std::string(63, '0'));
  expect(ErrorKind::invalid_format, load_group);
  write(group_checksum, std::string(64, '0'));
  expect(ErrorKind::integrity, load_group);
  auto changed_group = valid_group;
  SIC_CHECK(changed_group.size() > 15 * sizeof(std::uint64_t));
  changed_group[15 * sizeof(std::uint64_t)] ^= 1;
  write(group_path, changed_group);
  authenticate(group_path);
  expect(ErrorKind::integrity, load_group);
  auto group_field = [&](size_t index, std::uint64_t value, ErrorKind kind) {
    auto record = valid_group;
    std::memcpy(record.data() + index * sizeof(value), &value, sizeof(value));
    write(group_path, record);
    authenticate(group_path);
    expect(kind, load_group);
  };
  for (size_t index : {5, 6, 7, 8, 9, 10, 11}) group_field(index, 0, ErrorKind::integrity);
  group_field(0, 0, ErrorKind::invalid_format);
  group_field(12, 0, ErrorKind::invalid_format);
  group_field(13, 0, ErrorKind::invalid_format);
  group_field(14, 1025, ErrorKind::invalid_format);
  write(group_path, valid_group + "extra");
  authenticate(group_path);
  expect(ErrorKind::invalid_format, load_group);
  std::uint64_t identity_length;
  std::memcpy(&identity_length, valid_group.data() + 14 * sizeof(identity_length),
              sizeof(identity_length));
  for (long factor : {1L, group.N + 1}) {
    auto record = valid_group;
    std::memcpy(record.data() + 15 * sizeof(identity_length) + identity_length, &factor,
                sizeof(factor));
    write(group_path, record);
    authenticate(group_path);
    expect(ErrorKind::invalid_format, load_group);
  }
  // Supply a byte only at the final fgetc, after the independent file-size check.
  write(group_path, valid_group);
  authenticate(group_path);
  trailing_byte = true;
  expect(ErrorKind::invalid_format, load_group);
  SIC_CHECK(!trailing_byte);
  trailing_read_error = true;
  expect(ErrorKind::io, load_group);
  SIC_CHECK(!trailing_read_error);
  write(group_path, valid_group.substr(0, 10));
  authenticate(group_path);
  expect(ErrorKind::invalid_format, load_group);
  write(group_path, valid_group);
  authenticate(group_path);
  std::filesystem::remove(group_checksum);
  expect(ErrorKind::io, load_group);
  authenticate(group_path);
  load_group();
  {
    const auto target = root / "sync.bin";
    sync_countdown = 1;
    expect(ErrorKind::io, [&] { GroupCheckpoint::save(group, target); });
    SIC_CHECK(sync_countdown == 0);
    SIC_CHECK(!std::filesystem::exists(target));
    SIC_CHECK(std::filesystem::exists(target.string() + ".partial"));
    SIC_CHECK(!std::filesystem::exists(target.string() + ".sha256"));
  }
  {
    const auto target = root / "parent-sync.bin";
    sync_countdown = 3;
    expect(ErrorKind::io, [&] { GroupCheckpoint::save(group, target); });
    SIC_CHECK(sync_countdown == 0);
    SIC_CHECK(read(target) == valid_group);
    SIC_CHECK(read(target.string() + ".sha256") == checkpoint_digest(target) + "\n");
    Group restored(ar);
    GroupCheckpoint::load(restored, target, group.N, [](size_t) {});
    expect(ErrorKind::io, [&] { GroupCheckpoint::save(group, target); });
    SIC_CHECK(read(target) == valid_group);
  }

  auto layout = std::make_shared<OrbitTableLayout>(ar.d, ar.h(), group.orbit);
  Table table(ar, 80, 1);
  table.enable_compact(layout);
  table.build_batched();
  table.release_states();
  const std::vector<char> mask(ar.h(), 0);
  const auto table_path = root / "table";
  TableCheckpoint::save(table, group, -1, mask, table_path);
  long cc = -2;
  std::vector<char> derived;
  bool admitted = false;
  auto load_table = [&] {
    admitted = false;
    return TableCheckpoint::load(ar, group, layout, 80, 1, table_path, cc, derived,
                                 [&](size_t) { admitted = true; });
  };
  const auto manifest_path = table_path / "manifest";
  const auto valid_manifest = read(manifest_path);
  write(table_path / "manifest.sha256", "bad");
  expect(ErrorKind::invalid_format, load_table);
  SIC_CHECK(!admitted);
  write(table_path / "manifest.sha256", std::string(64, '0'));
  expect(ErrorKind::integrity, load_table);
  SIC_CHECK(!admitted);
  auto changed = valid_manifest;
  const auto source_start = changed.find('\n') + 1;
  changed[source_start] ^= 1;
  write(manifest_path, changed);
  authenticate(manifest_path);
  expect(ErrorKind::integrity, load_table);
  SIC_CHECK(!admitted);
  changed = valid_manifest;
  const auto version_start = changed.find('\n', changed.find('\n', source_start) + 1) + 1;
  changed[version_start] = 'X';
  write(manifest_path, changed);
  authenticate(manifest_path);
  expect(ErrorKind::unsupported, load_table);
  SIC_CHECK(!admitted);
  auto table_fields = [&](size_t index, long value, bool other_group = false) {
    std::istringstream input(valid_manifest);
    std::string line, record;
    for (int i = 0; i < 4; ++i) {
      std::getline(input, line);
      record += line + '\n';
    }
    std::getline(input, line);
    std::istringstream metadata(line);
    std::vector<long> fields(10);
    for (auto& field : fields) SIC_CHECK(bool(metadata >> field));
    fields[index] = value;
    if (other_group) fields[7] = group.N;  // Valid in the saved group, outside the caller's group.
    for (long field : fields) record += std::to_string(field) + ' ';
    record += '\n';
    record.append(std::istreambuf_iterator<char>(input), {});
    write(manifest_path, record);
    authenticate(manifest_path);
  };
  for (size_t index : {0, 1, 2, 3, 4, 5, 6}) {
    table_fields(index, 0);
    expect(ErrorKind::integrity, load_table);
    SIC_CHECK(!admitted);
  }
  table_fields(3, group.N + 1, true);
  expect(ErrorKind::integrity, load_table);
  SIC_CHECK(!admitted);
  table_fields(7, group.N);
  expect(ErrorKind::invalid_format, load_table);
  SIC_CHECK(!admitted);
  write(manifest_path, valid_manifest);
  authenticate(manifest_path);
  SIC_CHECK(load_table()->cached_enclosures);

  CompletedClassIdentity identity;
  identity.s = ar.s;
  identity.d = ar.d;
  identity.delta = ar.Delta;
  identity.order = ar.order_index;
  identity.degree = group.N;
  identity.slots = layout->slots;
  identity.precision = 80;
  identity.producer = std::string(64, 'a');
  identity.group_digest = std::string(64, 'b');
  identity.coarse_digest = std::string(64, 'c');
  identity.derived.assign(ar.h(), 0);
  identity.elements.assign(ar.h(), -1);
  const auto anchor = group.orbit[group.pidx(0, 0, 1)];
  for (long g = 0; g < group.N; ++g) {
    const auto cls = group.act(g, anchor) / (ar.d * ar.d);
    if (identity.elements[cls] < 0) identity.elements[cls] = g;
  }
  CompletedClassCache cache(root / "completed", identity, group);
  auto saved = cache.publish(0, TableClassBacking(BallFile::save(table.packed[0])), "quadrature",
                             "synthetic storage fixture");
  const auto class_path = std::filesystem::path(saved.values->path()).parent_path();
  const auto class_manifest = class_path / "manifest";
  const auto valid_class = read(class_manifest);
  const auto class_digest = read(class_path / "manifest.sha256");
  write(class_path / "manifest.sha256", "g" + std::string(63, '0'));
  expect(ErrorKind::invalid_format, [&] { cache.inventory(); });
  write(class_path / "manifest.sha256", std::string(64, '0'));
  expect(ErrorKind::integrity, [&] { cache.inventory(); });
  auto altered = valid_class;
  const auto producer = altered.find(identity.producer);
  SIC_CHECK(producer != std::string::npos);
  altered[producer] = 'd';
  write(class_manifest, altered);
  authenticate(class_manifest);
  expect(ErrorKind::integrity, [&] { cache.inventory(); });
  altered = valid_class;
  const auto payload_digest = saved.values->digest();
  const auto payload_digest_start = altered.find(payload_digest);
  SIC_CHECK(payload_digest_start != std::string::npos);
  altered[payload_digest_start] = 'g';
  write(class_manifest, altered);
  authenticate(class_manifest);
  // Assert rejection by the manifest parser itself; BallFile also checks syntax.
  // This local diagnostic assertion does not make messages a public protocol.
  SIC_CHECK(expect(ErrorKind::invalid_format, [&] { cache.inventory(); }) ==
            "invalid completed-class payload record");
  write(class_manifest, valid_class);
  write(class_path / "manifest.sha256", class_digest);
  const auto partial = class_path.parent_path() / ".class-0.partial-test";
  std::filesystem::rename(class_path, partial);
  SIC_CHECK(cache.inventory().hits == 0);
  std::filesystem::rename(partial, class_path);
  SIC_CHECK(cache.inventory().hits == 1);
  expect(ErrorKind::resource, [&] {
    cache.publish(1, TableClassBacking(BallFile::save(table.packed[1])), "quadrature",
                  std::string(65537, 'x'));
  });
  SIC_CHECK(!cache.load(1));
  // Publish a valid competitor while the first publisher is syncing its private
  // payload, so its replace attempt reaches the existing io-only winner reload.
  CompletedClassCache competitor(root / "completed", identity, group);
  TableClassBacking winner;
  sync_observer = [&](int descriptor) {
    const auto path = std::filesystem::read_symlink("/proc/self/fd/" + std::to_string(descriptor));
    if (path.string().find(".class-1.partial-") == std::string::npos) return;
    sync_observer = {};
    winner = competitor.publish(1, TableClassBacking(BallFile::save(table.packed[1])), "quadrature",
                                "competing accepted fixture");
  };
  auto raced = cache.publish(1, TableClassBacking(BallFile::save(table.packed[1])), "quadrature",
                             "losing private fixture");
  SIC_CHECK(!sync_observer && winner && raced);
  SIC_CHECK(raced.values->path() == winner.values->path());
  SIC_CHECK(cache.inventory().hits == 2);
  for (const auto& entry : std::filesystem::directory_iterator(class_path.parent_path()))
    SIC_CHECK(entry.path().filename().string().find(".partial-") == std::string::npos);
  auto wrong_source = identity;
  wrong_source.coarse_source = "other";
  expect(ErrorKind::integrity,
         [&] { CompletedClassCache other(root / "other", wrong_source, group); });
  expect(ErrorKind::invalid_argument, [] { CompletedCacheRequirement::parse("bad"); });
  expect(ErrorKind::invalid_argument, [] {
    CompletedCacheRequirement{true}.require_context(false, false, nullptr, nullptr, nullptr);
  });
  expect(ErrorKind::io, [] { CompletedCacheRequirement{true}.enforce(80, 0, 1, 1); });

#ifdef SIC_TEST_WRAP_STREAMS
  // Real badbit failures at all six stream boundaries, in both build modes.
  auto stream_error = [&](int selected, auto action) {
    faults::stream_read = {selected};
    expect(ErrorKind::io, action);
    SIC_CHECK(faults::stream_read.calls == selected);
    faults::reset();
  };
  stream_error(1, [&] { completed_loaded_object_digest(dlsym(RTLD_DEFAULT, "acb_mat_mul")); });
  stream_error(1, [&] { cache_checksum_file(class_path / "manifest.sha256"); });
  stream_error(1, load_group);
  for (int selected : {1, 2}) {
    stream_error(selected, load_table);
    SIC_CHECK(!admitted);
    stream_error(selected, [&] { cache.load(0); });
  }
  SIC_CHECK(cache.inventory().hits == 2);
  SIC_CHECK(load_table()->cached_enclosures);
#else
  std::puts("SKIPPED stream badbit fault controls: libstdc++ ABI unavailable");
#endif

#ifdef SIC_ENABLE_TEST_HOOKS
  const auto target = root / "rename.bin";
  GroupCheckpoint::rename_fault = [] {
    throw std::filesystem::filesystem_error("injected group rename",
                                            std::make_error_code(std::errc::io_error));
  };
  expect(ErrorKind::io, [&] { GroupCheckpoint::save(group, target); });
  GroupCheckpoint::rename_fault = {};
  SIC_CHECK(!std::filesystem::exists(target));
  SIC_CHECK(!std::filesystem::exists(target.string() + ".sha256"));
  SIC_CHECK(read(target.string() + ".partial") == valid_group);
  SIC_CHECK(read(group_path) == valid_group);
  Group partial_group(ar);
  expect(ErrorKind::io,
         [&] { GroupCheckpoint::load(partial_group, target, group.N, [](size_t) {}); });
  // The exclusive partial is retained; retry cannot overwrite it.
  expect(ErrorKind::io, [&] { GroupCheckpoint::save(group, target); });
  SIC_CHECK(read(target.string() + ".partial") == valid_group);
  std::filesystem::remove(target.string() + ".partial");
  GroupCheckpoint::save(group, target);
  SIC_CHECK(read(target) == valid_group);
  GroupCheckpoint::disk_available = [] { return 0; };
  expect(ErrorKind::resource, [&] { GroupCheckpoint::save(group, root / "disk.bin"); });
  GroupCheckpoint::disk_available = {};
  SIC_CHECK(!std::filesystem::exists(root / "disk.bin.partial"));
#endif
  return sic_test::report();
}
