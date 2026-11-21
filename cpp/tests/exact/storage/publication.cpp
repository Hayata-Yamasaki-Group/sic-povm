// SPDX-License-Identifier: AGPL-3.0-or-later
// Publication fault matrix: production remains free of fault injection seams.
#include <sys/stat.h>

#include <iostream>

#include "assertions.hpp"
#include "exact/storage/completed_class_cache.hpp"
#include "sic/error.hpp"
#include "support/io_faults.hpp"
#include "support/resources.hpp"

namespace fs = std::filesystem;
using namespace sic::exact;
using sic::ErrorKind;

namespace {
struct Cleanup {
  fs::path path;
  ~Cleanup() {
    faults::reset();
    std::error_code ignored;
    for (const auto& file : faults::private_files) fs::remove(file, ignored);
    fs::remove_all(path, ignored);
  }
};

struct Site {
  const char* name;
  faults::Fault* fault;
};
const Site kSites[] = {
    {"open", &faults::open},     {"mkstemp", &faults::mkstemp}, {"mkdtemp", &faults::mkdtemp},
    {"fopen", &faults::fopen},   {"fclose", &faults::fclose},   {"fflush", &faults::fflush},
    {"fwrite", &faults::fwrite}, {"fputc", &faults::fputc},     {"write", &faults::write},
    {"fsync", &faults::fsync},   {"close", &faults::close},     {"link", &faults::link},
    {"unlink", &faults::unlink}, {"rename", &faults::rename},   {"fdopen", &faults::fdopen},
    {"read", &faults::read},     {"fread", &faults::fread},     {"fstat", &faults::fstat}};

template <class F>
bool io_failure(F&& action) {
  try {
    action();
  } catch (const sic::Error& error) {
    SIC_CHECK(error.kind() == ErrorKind::io);
    return true;
  }
  return false;
}

template <class F>
void category(ErrorKind kind, F&& action) {
  bool refused = false;
  try {
    action();
  } catch (const sic::Error& error) {
    refused = error.kind() == kind;
  }
  SIC_CHECK(refused);
}

void equal(const sic::support::ComplexBallVector& expected,
           const sic::support::ComplexBallVector& actual) {
  SIC_CHECK(expected.size() == actual.size());
  for (size_t q = 0; q < expected.size(); ++q) SIC_CHECK(acb_equal(expected[q], actual[q]));
}

mode_t mode(const fs::path& path) {
  struct stat state {};
  SIC_CHECK(::stat(path.c_str(), &state) == 0);
  return state.st_mode & 0777;
}

// Each successful-path call index is injected separately, including calls whose
// cleanup-close result is intentionally ignored. Failure may be reported after
// commit; the committed generation must then be complete and retry acceptable.
template <class Prepare, class Action, class Verify>
void matrix(const char* label, Prepare&& prepare, Action&& action, Verify&& verify) {
  prepare();
  faults::reset();
  action();
  std::vector<int> calls;
  for (const auto& site : kSites) calls.push_back(site.fault->calls);
  faults::reset();
  verify();
  for (size_t s = 0; s < std::size(kSites); ++s) {
    std::cout << label << ' ' << kSites[s].name << ' ' << calls[s] << std::endl;
    for (int at = 1; at <= calls[s]; ++at) {
      prepare();
      faults::reset();
      kSites[s].fault->at = at;
      kSites[s].fault->error = ENOSPC;
      const bool reported = io_failure(action);
      SIC_CHECK(reported || kSites[s].fault == &faults::close ||
                kSites[s].fault == &faults::fclose || kSites[s].fault == &faults::unlink);
      std::cout << "  call " << at << (reported ? " reported io" : " accepted cleanup failure")
                << std::endl;
      SIC_CHECK(kSites[s].fault->calls >= at);
      faults::reset();
      verify();
    }
  }
}

template <class Prepare, class Action, class Verify>
void retries(Prepare&& prepare, Action&& action, Verify&& verify) {
  prepare();
  faults::reset();
  action();
  const int sync_count = faults::fsync.calls;
  faults::reset();
  for (int at = 1; at <= sync_count; ++at) {
    prepare();
    faults::reset();
    faults::fsync = {at, 0, EINTR};
    action();
    SIC_CHECK(faults::fsync.calls == sync_count + 1);
    faults::reset();
    verify();
  }
  for (int control = 0; control < 4; ++control) {
    prepare();
    faults::reset();
    faults::write.at = 1;
    faults::short_write = control == 0 || control == 3;
    faults::zero_write = control == 1;
    faults::write.error = EINTR;
    faults::write_error_after = control == 3 ? 2 : 0;
    const bool failed = io_failure(action);
    SIC_CHECK(failed == (control == 1 || control == 3));
    SIC_CHECK(faults::write.calls >= (control == 1 ? 1 : 2));
    faults::reset();
    verify();
  }
}

void private_backing() {
  sic::support::ComplexBallVector values(3);
  for (size_t q = 0; q < values.size(); ++q) acb_set_si(values[q], q + 2);
  auto previous = BallFile::save(values);
  const auto old_digest = previous->digest();
  std::shared_ptr<BallFile> result;
  const auto prepare = [&] { result.reset(); };
  const auto action = [&] { result = BallFile::save(values); };
  const auto verify = [&] {
    SIC_CHECK(checkpoint_digest(previous->path()) == old_digest);
    equal(values, previous->complex());
    if (result) equal(values, result->complex());
    result.reset();
    for (const auto& file : faults::private_files)
      if (file != previous->path()) SIC_CHECK(!fs::exists(file));
  };
  matrix("private backing", prepare, action, verify);
  // fdopen failure takes the descriptor-close/unlink cleanup path, which is
  // absent from successful serialization. Both cleanup failures keep io.
  for (bool fail_unlink : {false, true}) {
    prepare();
    faults::reset();
    faults::fdopen.at = 1;
    if (fail_unlink)
      faults::unlink.at = 1;
    else
      faults::close.at = 1;
    SIC_CHECK(io_failure(action));
    SIC_CHECK(faults::fdopen.calls == 1);
    SIC_CHECK((fail_unlink ? faults::unlink.calls : faults::close.calls) >= 1);
    faults::reset();
    // An unlink failure may leave an unpublished private file; remove it here.
    for (const auto& file : faults::private_files)
      if (file != previous->path()) fs::remove(file);
    verify();
  }
  const auto address = dlsym(RTLD_DEFAULT, "getpid");
  SIC_CHECK(address != nullptr);
  SIC_CHECK(completed_loaded_object_digest(address).size() == 64);
  for (int at : {1, 2}) {
    faults::reset();
    faults::fstat.at = at;
    category(ErrorKind::io, [&] { completed_loaded_object_digest(address); });
    SIC_CHECK(faults::fstat.calls == at);
  }
  faults::reset();
  // Both BallFile header and integer stream reads keep the io category.
  for (int at : {1, 2}) {
    faults::fread.at = at;
    category(ErrorKind::io, [&] { previous->complex(); });
    SIC_CHECK(faults::fread.calls == at);
    faults::reset();
  }
}

void checkpoint(const fs::path& root, const Arithmetic& ar, const Group& group,
                const std::shared_ptr<OrbitTableLayout>& layout) {
  Table table(ar, 80, 1);
  table.enable_compact(layout);
  table.build_batched();
  table.release_states();
  const std::vector<char> mask(ar.h(), 0);
  const auto previous = root / "previous", next = root / "next";
  const auto partial = fs::path(next.string() + ".partial");
  TableCheckpoint::save(table, group, -1, mask, previous);
  const auto old_digest = checkpoint_digest(previous / "manifest");
  const auto load = [&](const fs::path& path) {
    long cc = -2;
    std::vector<char> derived;
    auto restored =
        TableCheckpoint::load(ar, group, layout, 80, 1, path, cc, derived, [](size_t) {});
    SIC_CHECK(cc == -1 && derived == mask);
    for (size_t c = 0; c < table.packed.size(); ++c) equal(table.packed[c], restored->packed[c]);
  };
  const auto prepare = [&] {
    fs::remove_all(next);
    fs::remove_all(partial);
  };
  const auto action = [&] { TableCheckpoint::save(table, group, -1, mask, next); };
  const auto verify = [&] {
    SIC_CHECK(checkpoint_digest(previous / "manifest") == old_digest);
    load(previous);
    if (fs::exists(next)) {
      load(next);
      SIC_CHECK(!fs::exists(partial));
      SIC_CHECK(mode(next) == 0750);
      SIC_CHECK(mode(next / "manifest") == 0640);
      SIC_CHECK(mode(next / "manifest.sha256") == 0640);
      SIC_CHECK(mode(next / "class-0.balls") == 0640);
      category(ErrorKind::io, action);  // Established completed-checkpoint refusal.
    } else {
      // Fixed checkpoint partials support inspection on failure. The caller
      // removes them explicitly when preparing a retry.
      fs::remove_all(partial);
      action();
      load(next);
    }
  };
  matrix("checkpoint", prepare, action, verify);
  retries(prepare, action, verify);
  // Digest failures and authenticated schema failures have distinct categories.
  sic::support::atomic_write(next / "manifest", "corrupt\n");
  category(ErrorKind::integrity, [&] { load(next); });
  sic::support::atomic_write(next / "manifest.sha256", checkpoint_digest(next / "manifest") + '\n');
  category(ErrorKind::invalid_format, [&] { load(next); });
  category(ErrorKind::io, [&] { load(root / "absent"); });
}

void completed(const fs::path& root, const Arithmetic& ar, const Group& group,
               const std::shared_ptr<OrbitTableLayout>& layout) {
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
  sic::support::ComplexBallVector values(layout->slots);
  for (size_t q = 0; q < values.size(); ++q) acb_set_si(values[q], q + 5);
  const auto source = BallFile::save(values);
  CompletedClassCache previous(root, identity, group);
  auto old = previous.publish(0, TableClassBacking(source), "quadrature", "previous");
  const fs::path old_path = fs::path(old.values->path()).parent_path();
  const auto old_digest = checkpoint_digest(old_path / "manifest");
  identity.precision = 81;
  CompletedClassCache next(root, identity, group);
  fs::path new_generation;
  const auto action = [&] {
    auto result = next.publish(0, TableClassBacking(source), "quadrature", "next");
    new_generation = fs::path(result.values->path()).parent_path().parent_path();
  };
  action();
  const auto prepare = [&] { fs::remove_all(new_generation); };
  const auto verify = [&] {
    SIC_CHECK(checkpoint_digest(old_path / "manifest") == old_digest);
    equal(values, previous.load(0).values->complex());
    if (auto result = next.load(0)) {
      equal(values, result.values->complex());
      const fs::path path = fs::path(result.values->path()).parent_path();
      SIC_CHECK(mode(path) == 0700);
      SIC_CHECK(mode(path / "manifest") == 0640);
      SIC_CHECK(mode(path / "payload.balls") == 0640);
    }
    if (fs::exists(new_generation))
      for (const auto& entry : fs::directory_iterator(new_generation))
        SIC_CHECK(entry.path().filename().string().find(".partial-") == std::string::npos);
    // A failed or post-commit publication is always retryable without cleanup.
    action();
    equal(values, next.load(0).values->complex());
  };
  matrix("completed", prepare, action, verify);
  retries(prepare, action, verify);
  // A producer winning the final rename race remains acceptable. Inject the
  // competing complete directory at the actual rename, after our initial miss.
  prepare();
  faults::reset();
  faults::rename.at = 1;
  faults::rename.error = EEXIST;
  faults::rename_race = [](const char* from, const char* to) {
    fs::copy(from, to, fs::copy_options::recursive);
  };
  action();
  SIC_CHECK(faults::rename.calls == 1);
  faults::reset();
  verify();
  const auto manifest = new_generation / "class-0" / "manifest";
  sic::support::atomic_write(manifest, "corrupt\n");
  category(ErrorKind::integrity, [&] { next.load(0); });
  sic::support::atomic_write(manifest.parent_path() / "manifest.sha256",
                             checkpoint_digest(manifest) + '\n');
  category(ErrorKind::invalid_format, [&] { next.load(0); });
}
}  // namespace

int main() try {
  char pattern[] = "/tmp/sic-storage-publication-XXXXXX";
  if (!::mkdtemp(pattern)) throw std::runtime_error("test mkdtemp failed");
  Cleanup cleanup{pattern};
  const mode_t previous_mask = ::umask(0027);
  struct Mask {
    mode_t value;
    ~Mask() { ::umask(value); }
  } mask{previous_mask};
  SIC_CHECK(::syscall(SYS_getpid) == ::getpid());
  private_backing();
  Arithmetic ar(2);
  Group group(ar);
  group.generate_standard();
  group.close(
      sic::support::resources::requested_threads(sic::support::kernel_setting("SIC_THREADS")),
      (long)ar.h() * ar.d * ar.d / 2);
  auto layout = std::make_shared<OrbitTableLayout>(ar.d, ar.h(), group.orbit);
  checkpoint(cleanup.path / "checkpoints", ar, group, layout);
  completed(cleanup.path / "completed", ar, group, layout);
  return sic_test::report();
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
