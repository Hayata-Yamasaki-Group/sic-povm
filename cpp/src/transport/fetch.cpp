// SPDX-License-Identifier: AGPL-3.0-or-later
#include "transport/fetch.hpp"

#include "sic/error.hpp"
#include "sicx/io/io.hpp"
#include "support/sha256.hpp"
#include "transport/parts.hpp"
#include "transport/repair.hpp"
namespace sic::transport {

uint64_t inflight(const TransferJob& j) {
  const uint64_t count = std::min<uint64_t>(j.jobs, j.parts.parts.size());
  const uint64_t size = std::min(j.parts.part_size, j.parts.size);
  if (count != 0 && size > UINT64_MAX / count)
    throw sic::Error(sic::ErrorKind::resource, "parts in flight exceed the byte-count range");
  return count * size;
}

uint64_t allocated(const std::filesystem::path& p) {
  struct stat st {};
  return ::stat(p.c_str(), &st) ? 0 : static_cast<uint64_t>(st.st_blocks) * 512;
}

int open_rw(const std::filesystem::path& p, bool create) {
  const int fd = ::open(p.c_str(), O_RDWR | O_CLOEXEC | (create ? O_CREAT : 0), 0644);
  if (fd < 0)
    throw sic::Error(sic::ErrorKind::io, "cannot open " + p.string() + ": " + std::strerror(errno));
  return fd;
}

void preallocate(int fd, uint64_t size) {
  if (::ftruncate(fd, static_cast<off_t>(size)))
    throw sic::Error(sic::ErrorKind::io, "cannot size the work file");
  const int rc = ::posix_fallocate(fd, 0, static_cast<off_t>(size));
  if (rc == ENOSPC)
    throw sic::Error(sic::ErrorKind::io, "no space left while preallocating the work file");
  // Other errors (a file system without fallocate): the file stays sparse and is filled by pwrite.
}
int fetch(const FetchOptions& options) try {
  const auto& j = options.job;
  const uint64_t margin = 64u << 20;
  const uint64_t extra = options.extra;
  const std::filesystem::path work = std::filesystem::path(j.final_path.string() + ".part");
  if (sic::sicx::filesystem_io([&] { return std::filesystem::exists(j.final_path); })) {
    if (sic::sicx::filesystem_io([&] { return std::filesystem::file_size(j.final_path); }) ==
            j.parts.size &&
        sic::support::sha256::file_hex(j.final_path) == j.parts.sha256) {
      std::cout << "already present and identical: " << j.final_path.string() << "\n";
      return 0;
    }
    if (!options.force)
      throw sic::Error(
          sic::ErrorKind::integrity,
          j.final_path.string() +
              " exists and differs; use `sic-container repair` (or --force to start over)");
    sic::sicx::filesystem_io([&] { return std::filesystem::remove(j.final_path); });
  }
  const bool fresh =
      !sic::sicx::filesystem_io([&] { return std::filesystem::exists(work); }) ||
      sic::sicx::filesystem_io([&] { return std::filesystem::file_size(work); }) == 0;
  const uint64_t have_alloc = fresh ? 0 : std::min(allocated(work), j.parts.size);
  sic::transport::check_disk(j.out_dir, j.parts.size - have_alloc + inflight(j) + margin + extra,
                             "the work file and parts in flight");
  const int fd = open_rw(work, true);
  struct Closer {
    int fd;
    ~Closer() {
      if (fd >= 0) ::close(fd);
    }
  } closer{fd};
  preallocate(fd, j.parts.size);
  const auto done = sic::transport::sync_parts(fd, j.parts, j.source, j.jobs, !fresh, j.out_dir);
  if (::fsync(fd)) throw sic::Error(sic::ErrorKind::io, "fsync failed");
  ::close(fd);
  closer.fd = -1;
  sic::transport::final_checks(work, j.parts);
  sic::sicx::rename_file(work, j.final_path, "fetch");
  sic::sicx::sync_dir(j.final_path);  // atomic: the final name only ever holds a verified file
  std::cout << "wrote " << j.final_path.string() << " (" << j.parts.size << " bytes, sha256 "
            << j.parts.sha256 << "); downloaded " << done.size() << " of " << j.parts.parts.size()
            << " part(s)\n";
  return 0;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}
}  // namespace sic::transport
