// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/durable_file.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

#include "sic/error.hpp"
#include "sic/support/json.hpp"
#include "support/io/durable_output.hpp"

#ifdef SIC_TEST_WRAP_IO
#include "io_faults.hpp"
#endif

static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
template <class F>
static void refuses(F&& action) {
  bool threw = false;
  try {
    action();
  } catch (const sic::Error& error) {
    check(error.kind() == sic::ErrorKind::io, "I/O category lost");
    threw = true;
  }
  check(threw, "expected failure");
}
static std::string read(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  check(bool(in), "open test output");
  return {std::istreambuf_iterator<char>(in), {}};
}

int main() try {
  namespace support = sic::support;
  namespace filesystem = std::filesystem;
  char directory[] = "/tmp/sic-support-XXXXXX";
  check(::mkdtemp(directory) != nullptr, "mkdtemp");
  const filesystem::path dir(directory), file = dir / "data";
  struct Cleanup {
    filesystem::path path;
    ~Cleanup() {
      std::error_code ec;
      filesystem::remove_all(path, ec);
    }
  } cleanup{dir};
  const std::string binary("a\0b\n", 4);
  support::atomic_write(file, binary, false);
  check(read(file) == binary, "binary atomic write");
  refuses([&] { support::atomic_write(file, "do not overwrite", false); });
  check(read(file) == binary, "exclusive write preserved destination");
  support::atomic_write(file, "replaced");
  check(read(file) == "replaced", "atomic replacement");
  const std::string streamed_payload = binary + std::string(20000, 'x') + binary;
  const auto emit = [&](std::ostream& out) {
    out << binary;
    for (int i = 0; i < 20000; ++i) out.put('x');
    out.write(binary.data(), binary.size());
  };
  support::atomic_write_stream(file, emit);
  check(read(file) == streamed_payload, "streamed publication preserves all buffered bytes");
  refuses([&] { support::atomic_write_stream(file, emit, false); });
  check(read(file) == streamed_payload, "streamed collision preserved destination");
  const auto aborted = dir / "aborted";
  try {
    support::atomic_write_stream(
        aborted,
        [](std::ostream& out) {
          out << std::string(20000, 'x');
          throw sic::Error(sic::ErrorKind::precision, "callback refusal");
        },
        false);
    throw std::runtime_error("expected callback refusal");
  } catch (const sic::Error& error) {
    check(error.kind() == sic::ErrorKind::precision &&
              std::string(error.what()) == "callback refusal",
          "callback category/message changed");
  }
  check(!filesystem::exists(aborted), "callback failure published output");
  bool callback_failure = false;
  try {
    support::atomic_write_stream(
        aborted, [](std::ostream&) { throw std::ios_base::failure("callback stream exception"); });
  } catch (const std::ios_base::failure&) {
    callback_failure = true;
  }
  check(callback_failure && !filesystem::exists(aborted), "callback stream exception changed");
  for (const auto& entry : filesystem::directory_iterator(dir))
    check(entry.path().filename().string().find(".tmp-") == std::string::npos,
          "callback failure leaked temporary");
  const filesystem::path symlink = dir / "dangling";
  filesystem::create_symlink(dir / "missing", symlink);
  refuses([&] { support::atomic_write(symlink, "do not follow", false); });
  check(filesystem::is_symlink(symlink), "exclusive write preserved symlink");
  refuses([&] { support::atomic_write(dir, "do not replace a directory"); });
  refuses([&] { support::sync_parent(dir / "missing" / "file"); });
  refuses([] { support::sync_file(-1); });
  int channel[2];
  check(::pipe(channel) == 0, "pipe");
  refuses([&] { support::sync_file(channel[1], true); });
  ::close(channel[0]);
  ::close(channel[1]);
  support::sync_file(file);
  support::sync_file(dir);
  refuses([&] { support::sync_file(dir / "absent"); });
  const auto private_dir = dir / "private-dir", published_dir = dir / "published-dir";
  filesystem::create_directory(private_dir);
  support::atomic_write(private_dir / "data", "directory bytes");
  support::publish_replace(private_dir, published_dir);
  check(read(published_dir / "data") == "directory bytes", "directory publication");
  filesystem::create_directory(private_dir);
  support::atomic_write(private_dir / "data", "competing bytes");
  refuses([&] { support::publish_replace(private_dir, published_dir); });
  check(read(published_dir / "data") == "directory bytes", "nonempty directory preserved");
#ifdef SIC_TEST_WRAP_IO
  // Data-only applies to the regular file; the parent always gets full fsync.
  faults::reset();
  support::atomic_write(file, "data sync", true, 0600, true);
  check(faults::fdatasync.calls == 1 && faults::fsync.calls == 1, "data/full sync selection");
  faults::reset();
  faults::fdatasync = {1, 0, EINTR};
  support::atomic_write(file, "retried data sync", true, 0600, true);
  check(faults::fdatasync.calls == 2 && faults::fsync.calls == 1, "data sync EINTR retry");
  faults::reset();
  faults::fdatasync.at = 1;
  refuses([&] { support::atomic_write(file, "not published", true, 0600, true); });
  faults::reset();
  check(read(file) == "retried data sync", "data sync failure preserved previous bytes");
  const auto clean = [&] {
    for (const auto& entry : filesystem::directory_iterator(dir))
      check(entry.path().filename().string().find(".tmp-") == std::string::npos,
            "temporary file leaked after fault");
  };
  // Before publication, every failure preserves the previous destination and
  // removes the private temporary. After publication the new bytes stay visible.
  for (const bool streamed : {false, true}) {
    for (const bool replace : {false, true}) {
      const auto prepare = [&] {
        faults::reset();
        filesystem::remove(file);
        if (replace) support::atomic_write(file, "previous");
        faults::reset();
      };
      const auto state = [&](bool published) {
        faults::reset();
        check(filesystem::exists(file) == (replace || published), "publication state");
        if (replace || published)
          check(read(file) == (published ? "next payload" : "previous"), "destination bytes");
        clean();
      };
      for (int which = 0; which < 8; ++which) {
        prepare();
        if (which == 0) {
          faults::write.at = 1;
          faults::zero_write = true;
        }
        if (which == 1) {
          faults::write.at = 1;
          faults::write.error = ENOSPC;
        }
        if (which == 2) faults::fsync.at = 1;
        if (which == 3) faults::close.at = 1;
        if (which == 4) (replace ? faults::rename : faults::link).at = 1;
        if (which == 5) faults::fsync.at = 2;
        if (which == 6) faults::close.at = 2;
        if (which == 7) {
          faults::write.at = 1;
          faults::short_write = true;
          faults::write_error_after = 2;
        }
        try {
          if (streamed)
            support::atomic_write_stream(
                file, [](std::ostream& out) { out << "next payload"; }, replace);
          else
            support::atomic_write(file, "next payload", replace);
          throw std::runtime_error("expected failure");
        } catch (const sic::Error& error) {
          check(error.kind() == sic::ErrorKind::io, "I/O category lost");
          if (which == 0 || which == 1 || which == 7)
            check(std::string(error.what()).find(file.string() + ".tmp-") != std::string::npos,
                  "write failure lost temporary pathname");
        }
        if (which == 3) check(faults::close.calls == 1, "failed close retried");
        state(which == 5 || which == 6);
      }
      for (int which = 0; which < 3; ++which) {
        prepare();
        if (which == 0) {
          faults::write.at = 1;
          faults::short_write = true;
        }
        if (which == 1) {
          faults::write.at = 1;
          faults::write.error = EINTR;
        }
        if (which == 2) {
          faults::fsync.at = 1;
          faults::fsync.error = EINTR;
        }
        if (streamed)
          support::atomic_write_stream(
              file, [](std::ostream& out) { out << "next payload"; }, replace);
        else
          support::atomic_write(file, "next payload", replace);
        check(which == 2 ? faults::fsync.calls == 3 : faults::write.calls == 2,
              "unexpected retry count");
        state(true);
      }
      if (!replace) {
        prepare();
        faults::unlink.at = 1;
        refuses([&] {
          if (streamed)
            support::atomic_write_stream(
                file, [](std::ostream& out) { out << "next payload"; }, false);
          else
            support::atomic_write(file, "next payload", false);
        });
        check(faults::unlink.calls == 2, "cleanup must retry unlink after publication failure");
        state(true);
      }
    }
  }
  // Direct no-replace move: force both Linux fallback reasons and each edge.
  const auto from = dir / "private", to = dir / "published";
  for (const int fallback : {ENOSYS, EINVAL, EOPNOTSUPP}) {
    for (int which = 0; which < 5; ++which) {
      faults::reset();
      filesystem::remove(from);
      filesystem::remove(to);
      support::atomic_write(from, "private bytes");
      if (which == 1) support::atomic_write(to, "previous");
      faults::reset();
      faults::publish = {1, 0, fallback};
      if (which == 2) faults::link.at = 1;
      if (which == 3) faults::unlink.at = 1;
      if (which == 4) faults::publish.error = EACCES;
      if (which == 0)
        support::publish_no_replace(from, to);
      else
        refuses([&] { support::publish_no_replace(from, to); });
      check(faults::publish.calls == 1, "renameat2 fault not reached");
      check(faults::link.calls == (which == 4 ? 0 : 1), "fallback selection");
      faults::reset();
      check(filesystem::exists(from) == (which != 0), "private move state");
      check(filesystem::exists(to) == (which <= 1 || which == 3), "published move state");
      if (filesystem::exists(to))
        check(read(to) == (which == 1 ? "previous" : "private bytes"), "no-clobber bytes");
      clean();
    }
  }
  faults::reset();
  filesystem::remove(from);
  filesystem::remove(to);
  support::atomic_write(from, "native move");
  faults::reset();
  support::publish_no_replace(from, to);
  const bool native_no_replace = faults::link.calls == 0;
  check(!filesystem::exists(from) && read(to) == "native move", "native no-replace move");
  support::atomic_write(from, "competing native bytes");
  faults::reset();
  refuses([&] { support::publish_no_replace(from, to); });
#if defined(__linux__)
  // The VFS can refuse an existing target before querying filesystem support.
  check(faults::publish.calls == 1 && faults::link.calls <= 1 &&
            (!native_no_replace || faults::link.calls == 0),
        "detected no-replace collision path");
#endif
  check(read(from) == "competing native bytes" && read(to) == "native move",
        "native collision preserved both files");
#endif
  for (const auto& entry : filesystem::directory_iterator(dir))
    check(entry.path().filename().string().find(".tmp-") == std::string::npos,
          "temporary file leaked");

  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
