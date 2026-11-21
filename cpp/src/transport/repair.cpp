// SPDX-License-Identifier: AGPL-3.0-or-later
#include "transport/repair.hpp"

#include "sic/support/cli.hpp"
#include "sicx/io/io.hpp"
#include "support/sha256.hpp"
#include "transport/fetch.hpp"
#include "transport/parts.hpp"
namespace sic::transport {

int repair(const TransferJob& job) {
  const auto& j = job;
  const std::filesystem::path rep = std::filesystem::path(j.final_path.string() + ".repairing");
  const bool have_final =
                 sic::sicx::filesystem_io([&] { return std::filesystem::exists(j.final_path); }),
             have_rep = sic::sicx::filesystem_io([&] { return std::filesystem::exists(rep); });
  if (have_final && have_rep)
    throw sic::Error(sic::ErrorKind::io, "both " + j.final_path.string() + " and " + rep.string() +
                                             " exist; remove one");
  if (!have_final && !have_rep)
    throw sic::Error(sic::ErrorKind::io, "nothing to repair at " + j.final_path.string() +
                                             "; use `sic-container fetch`");
  if (have_final) {
    sic::sicx::rename_file(j.final_path, rep, "repair_park");
    sic::sicx::sync_dir(j.final_path);
  }  // the canonical name never refers to a file being repaired
  const uint64_t cur = sic::sicx::filesystem_io([&] { return std::filesystem::file_size(rep); });
  sic::transport::check_disk(
      j.out_dir, (j.parts.size > cur ? j.parts.size - cur : 0) + inflight(j) + (64u << 20),
      "the repair");
  const int fd = open_rw(rep, false);
  struct Closer {
    int fd;
    ~Closer() {
      if (fd >= 0) ::close(fd);
    }
  } closer{fd};
  if (cur != j.parts.size || allocated(rep) < j.parts.size)
    preallocate(fd, j.parts.size);  // truncate, extend, or finish an interrupted preallocation
  const auto fixed = sic::transport::sync_parts(fd, j.parts, j.source, j.jobs, true, j.out_dir);
  if (::fsync(fd)) throw sic::Error(sic::ErrorKind::io, "fsync failed");
  ::close(fd);
  closer.fd = -1;
  sic::transport::final_checks(rep, j.parts);
  sic::sicx::rename_file(rep, j.final_path, "repair_restore");
  sic::sicx::sync_dir(j.final_path);
  std::cout << "repaired " << fixed.size() << " part(s):";
  for (auto i : fixed) std::cout << ' ' << i;
  std::cout << "\nverified " << j.final_path.string() << "\n";
  return 0;
}
}  // namespace sic::transport
