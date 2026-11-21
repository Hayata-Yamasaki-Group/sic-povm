// SPDX-License-Identifier: AGPL-3.0-or-later
#include <sys/stat.h>

#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

#include "access/range_cache.hpp"
#include "io_faults.hpp"
#include "sic/error.hpp"
#include "sic/support/durable_file.hpp"
#include "sicx/io/io.hpp"
#include "support/io/write_all.hpp"
#include "support/sha256.hpp"
#include "transport/fetch.hpp"
#include "transport/parts.hpp"

namespace fs = std::filesystem;
namespace sha = sic::support::sha256;
static void check(bool value, const char* why) {
  if (!value) throw std::runtime_error(why);
}
template <class F>
static void refuses(sic::ErrorKind kind, F action) {
  try {
    action();
  } catch (const sic::Error& error) {
    check(error.kind() == kind, "wrong error category");
    return;
  }
  throw std::runtime_error("expected categorized failure");
}
template <class Exception, class F>
static void refuses_native(F action) {
  try {
    action();
  } catch (const Exception&) {
    return;
  }
  throw std::runtime_error("expected native boundary failure");
}
static std::string read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  check(bool(in), "read test file");
  return {std::istreambuf_iterator<char>(in), {}};
}
int main() try {
  char pattern[] = "/tmp/sic-io-adapters-XXXXXX";
  check(::mkdtemp(pattern), "mkdtemp");
  const fs::path dir(pattern), file = dir / "data";
  struct Cleanup {
    fs::path path;
    ~Cleanup() {
      std::error_code ec;
      fs::remove_all(path, ec);
    }
  } cleanup{dir};
  const std::string payload = "0123456789abcdef";
  const auto* data = reinterpret_cast<const uint8_t*>(payload.data());
  const auto clean = [&] {
    for (const auto& entry : fs::directory_iterator(dir))
      check(entry.path().filename().string().find(".tmp-") == std::string::npos,
            "cache temporary leaked");
  };
  const int fd = ::open(file.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
  check(fd >= 0, "open test file");
  sha::Descriptor owner(fd);
  for (int adapter = 0; adapter < 3; ++adapter) {
    const auto write = [&] {
      if (adapter == 0) sic::support::write_all(fd, {data, payload.size()}, 3);
      if (adapter == 1) sic::transport::pwrite_all(fd, data, payload.size(), 3);
      if (adapter == 2) sic::sicx::pwrite_all(fd, data, payload.size(), 3);
    };
    for (int which = 0; which < 5; ++which) {
      faults::reset();
      check(::ftruncate(fd, 0) == 0 && ::lseek(fd, 2, SEEK_SET) == 2, "reset write file");
      faults::pwrite.at = 1;
      if (which == 0) faults::short_pwrite = true;
      if (which == 1) faults::pwrite.error = EINTR;
      if (which == 2) faults::zero_pwrite = true;
      if (which == 3) faults::pwrite.error = ENOSPC;
      if (which == 4) {
        faults::short_pwrite = true;
        faults::pwrite_error_after = 2;
      }
      if (which < 2)
        write();
      else
        refuses(sic::ErrorKind::io, write);
      check(::lseek(fd, 0, SEEK_CUR) == 2, "positioned write changed offset");
      const int calls = faults::pwrite.calls;
      faults::reset();
      const auto bytes = read(file);
      if (which < 2)
        check(bytes == std::string(3, '\0') + payload && calls == 2, "pwrite retry bytes");
      if (which == 2 || which == 3) check(bytes.empty() && calls == 1, "failed write changed file");
      if (which == 4)
        check(bytes == std::string(3, '\0') + payload.substr(0, 8), "partial ENOSPC bytes");
    }
  }
  faults::reset();
  const auto maximum = static_cast<uint64_t>(std::numeric_limits<off_t>::max());
  const auto bytes = std::span(data, payload.size());
  for (const auto offset : {maximum, maximum + 1, UINT64_MAX}) {
    refuses_native<std::overflow_error>([&] { sic::support::write_all(fd, bytes, offset); });
    refuses_native<std::overflow_error>([&] { sic::sicx::pwrite_all(fd, data, 1, offset); });
    refuses_native<std::overflow_error>([&] { sic::transport::pwrite_all(fd, data, 1, offset); });
  }
  sic::support::write_all(fd, {}, maximum);
  check(faults::pwrite.calls == 0 && faults::write.calls == 0, "write admission performed I/O");
  std::vector<unsigned char> scratch(3), empty_scratch;
  for (const auto offset : {maximum, maximum + 1, UINT64_MAX}) {
    refuses_native<std::overflow_error>([&] { sha::range_hash(fd, offset, 1, scratch); });
    refuses_native<std::overflow_error>(
        [&] { sic::transport::hash_range(fd, offset, 1, scratch); });
  }
  refuses_native<std::overflow_error>([&] { sha::range_hash(fd, 1, UINT64_MAX, scratch); });
  refuses_native<std::invalid_argument>([&] { sha::range_hash(fd, 0, 1, empty_scratch); });
  check(sha::range_hash(fd, maximum, 0, empty_scratch) == sha::digest_hex(""),
        "empty support range");
  check(sic::transport::hash_range(fd, UINT64_MAX, 0, empty_scratch) == sha::digest_hex(""),
        "empty transport scratch");
  check(faults::pread.calls == 0, "hash admission performed I/O");
  check(::ftruncate(fd, 0) == 0, "truncate");
  sic::support::write_all(fd, {data, payload.size()}, 0);
  check(::lseek(fd, 5, SEEK_SET) == 5, "hash offset");
  for (const size_t size : {size_t(1), size_t(3), size_t(65536)}) {
    std::vector<unsigned char> buffer(size);
    for (int which = 0; which < 4; ++which) {
      faults::reset();
      if (which == 1) {
        faults::pread.at = 1;
        faults::pread.error = EINTR;
      }
      if (which == 2) {
        faults::pread.at = 1;
        faults::short_read = true;
      }
      if (which == 3) faults::pread.at = 1;
      if (which < 3)
        check(
            sic::transport::hash_range(fd, 2, 11, buffer) == sha::digest_hex(payload.substr(2, 11)),
            "range digest");
      else
        check(sic::transport::hash_range(fd, 2, 11, buffer).empty(),
              "failed read must be repairable");
      faults::reset();
      check(buffer.size() == size, "caller buffer budget changed");
      check(sic::transport::hash_range(fd, 0, 0, buffer) == sha::digest_hex(""),
            "empty transport range");
      check(sic::transport::hash_range(fd, 0, payload.size() + 1, buffer).empty(),
            "short file repairable miss");
      check(sic::transport::hash_range(-1, 0, 1, buffer).empty(), "bad fd repairable miss");
      refuses(sic::ErrorKind::io, [&] { sha::range_hash(fd, 0, payload.size() + 1, buffer); });
      refuses(sic::ErrorKind::io, [&] { sha::range_hash(-1, 0, 1, buffer); });
      check(::lseek(fd, 0, SEEK_CUR) == 5, "range hash changed offset");
    }
  }
  std::array<uint8_t, 32> buffer{};
  refuses(sic::ErrorKind::invalid_format,
          [&] { sic::sicx::pread_exact(fd, buffer.data(), buffer.size(), 0); });
  refuses(sic::ErrorKind::io, [&] { sic::sicx::pread_exact(-1, buffer.data(), 1, 0); });
  refuses(sic::ErrorKind::io, [&] { sic::sicx::read_bounded_text(dir / "missing"); });
  refuses(sic::ErrorKind::resource, [&] { sic::sicx::read_bounded_text(file, 1); });
  for (const int error : {EIO, EINTR}) {
    faults::reset();
    faults::fdatasync = {1, 0, error};
    if (error == EIO)
      refuses(sic::ErrorKind::io, [&] { sic::sicx::sync_fd(fd, "test"); });
    else {
      sic::sicx::sync_fd(fd, "test");
      check(faults::fdatasync.calls == 2, "data sync retry");
    }
  }
  faults::reset();
  faults::fsync.at = 1;
  refuses(sic::ErrorKind::io, [&] { sic::sicx::sync_dir(file); });
  faults::reset();
  // Cache's data sync, replacement, private mode and unchecked final close stay fixed.
  for (int which = 0; which < 6; ++which) {
    faults::reset();
    sic::access::atomic_text(file, "previous");
    faults::reset();
    if (which == 0) {
      faults::pwrite.at = 1;
      faults::zero_pwrite = true;
    }
    if (which == 1) faults::fdatasync.at = 1;
    if (which == 2) faults::rename.at = 1;
    if (which == 3) faults::fsync.at = 1;
    if (which == 4) faults::close.at = 1;  // parent close is checked
    if (which == 5) faults::close.at = 2;  // final file close remains unchecked
    if (which < 5)
      refuses(sic::ErrorKind::io, [&] { sic::access::atomic_text(file, payload); });
    else
      sic::access::atomic_text(file, payload);
    if (which == 5)
      check(faults::fdatasync.calls == 1 && faults::fsync.calls == 1, "cache sync types");
    faults::reset();
    check(read(file) == (which < 3 ? "previous" : payload), "cache publication state");
    struct stat st {};
    check(::stat(file.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600, "cache mode");
    clean();
  }
  // A worker must preserve categorized write failures through join/rethrow.
  const auto assets = dir / "assets";
  fs::create_directory(assets);
  sic::support::atomic_write(assets / "part", payload);
  sic::transport::Parts parts;
  sic::transport::Part part;
  part.asset = "part";
  part.size = payload.size();
  part.sha256 = sha::digest_hex(payload);
  parts.parts.push_back(part);
  const auto work = dir / "work";
  sha::Descriptor work_fd(::open(work.c_str(), O_CREAT | O_RDWR, 0600));
  check(work_fd.fd >= 0, "open transport work file");
  faults::reset();
  faults::pwrite.at = 1;
  faults::zero_pwrite = true;
  refuses(sic::ErrorKind::io,
          [&] { sic::transport::sync_parts(work_fd.fd, parts, assets.string(), 1, false, dir); });
  faults::reset();
  check(!fs::exists(dir / ".part.tmp") && read(work).empty(), "worker failure cleanup");
  refuses(sic::ErrorKind::resource, [&] { sic::transport::check_disk(dir, UINT64_MAX, "test"); });
  refuses(sic::ErrorKind::io, [&] { sic::transport::free_bytes(dir / "missing"); });
  // The remaining direct sync/read paths preserve their original retry policy,
  // while retaining io across the worker boundary.
  const std::string large_payload(size_t(1) << 20, 'x');
  sic::support::atomic_write(assets / "part", large_payload);
  parts.parts[0].size = large_payload.size();
  parts.parts[0].sha256 = sha::digest_hex(large_payload);
  for (int which = 0; which < 3; ++which) {
    faults::reset();
    check(::ftruncate(work_fd.fd, 0) == 0, "reset transport work");
    if (which == 0)
      faults::fdatasync.at = 1;
    else
      faults::pread.at = which == 1 ? 2 : 1;  // hash read, then copy read
    refuses(sic::ErrorKind::io,
            [&] { sic::transport::sync_parts(work_fd.fd, parts, assets.string(), 1, false, dir); });
    faults::reset();
    check(!fs::exists(dir / ".part.tmp"), "direct I/O failure leaked transport temporary");
    check(fs::file_size(work) == (which == 0 ? large_payload.size() : 0), "direct I/O work state");
  }
  check(::ftruncate(work_fd.fd, 0) == 0, "reset mismatched work");
  parts.parts[0].sha256 = sha::digest_hex("different");
  refuses(sic::ErrorKind::integrity,
          [&] { sic::transport::sync_parts(work_fd.fd, parts, assets.string(), 1, false, dir); });
  check(!fs::exists(dir / ".part.tmp") && read(work).empty(), "digest failure state");
  refuses(sic::ErrorKind::io, [&] { sic::transport::open_rw(dir / "missing" / "file", true); });
  refuses(sic::ErrorKind::io, [&] { sic::transport::preallocate(-1, 1); });
  refuses(sic::ErrorKind::io, [&] { sha::file_hex(dir / "missing"); });
  refuses(sic::ErrorKind::io, [&] { sha::file_hex(dir); });
  refuses(sic::ErrorKind::io, [&] { sic::access::atomic_text(dir / "missing" / "file", payload); });
  sic::transport::TransferJob job;
  job.jobs = 2;
  job.parts.parts.resize(2);
  job.parts.part_size = job.parts.size = UINT64_MAX;
  refuses(sic::ErrorKind::resource, [&] { (void)sic::transport::inflight(job); });
  job.parts.part_size = 7;
  check(sic::transport::inflight(job) == 14, "inflight arithmetic");
  job.jobs = 0;
  check(sic::transport::inflight(job) == 0, "empty inflight allowance");
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
