// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/group/group_checkpoint.hpp"

namespace sic::exact {
std::string GroupCheckpoint::source_id() {
#ifdef SIC_GROUP_SOURCE_ID
  return SIC_GROUP_SOURCE_ID;
#else
  throw std::runtime_error("group checkpoints require the CMake source fingerprint");
#endif
}

sic::support::resources::Bytes GroupCheckpoint::bytes(long npts, long elements, size_t bases,
                                                      size_t rank) {
  using sic::support::resources::add;
  using sic::support::resources::Bytes;
  using sic::support::resources::mul;
  return add(2048, add(mul(rank + bases, sizeof(long)),
                       add(mul(elements, add(sizeof(long) + sizeof(int),
                                             mul(bases, sizeof(sic::support::Index)))),
                           mul(npts, 2 * sizeof(sic::support::Index)))));
}

void GroupCheckpoint::save(const Group& group, const std::filesystem::path& path) try {
  if (group.N <= 0 || group.base.empty() || group.invf.empty() ||
      group.nn.size() != (size_t)group.N || group.chi.size() != (size_t)group.N ||
      group.img.size() != sic::support::resources::mul(group.N, group.nb()) ||
      group.rep_elem.size() != (size_t)group.npts || group.rep_base.size() != (size_t)group.npts)
    throw std::logic_error("only a completed exact group can be checkpointed");
  const auto digest_path = std::filesystem::path(path.string() + ".sha256");
  const auto partial = std::filesystem::path(path.string() + ".partial");
  if (std::filesystem::exists(path) || std::filesystem::exists(digest_path))
    throw sic::Error(sic::ErrorKind::io, "group checkpoint destination already exists");
  auto parent = path.parent_path();
  if (parent.empty()) parent = ".";
  const auto forecast = bytes(group.npts, group.N, group.nb(), group.invf.size());
  const auto reserve = std::min(sic::support::resources::kGib,
                                std::max(4 * sic::support::resources::kMib, forecast / 20));
  auto available = std::filesystem::space(parent).available;
#ifdef SIC_ENABLE_TEST_HOOKS
  if (disk_available) available = disk_available();
#endif
  if (available < sic::support::resources::add(forecast, reserve))
    throw sic::Error(sic::ErrorKind::resource, "group checkpoint disk headroom is insufficient");
  const int fd = open(partial.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0)
    throw sic::Error(sic::ErrorKind::io, "cannot create exclusive group checkpoint partial");
  FILE* file = fdopen(fd, "wb");
  if (!file) {
    close(fd);
    throw sic::Error(sic::ErrorKind::io, "checkpoint fdopen failed");
  }
  std::unique_ptr<FILE, decltype(&fclose)> owner(file, fclose);
  const std::uint64_t header[] = {0x53494347524f5550ULL,
                                  1,
                                  0x0102030405060708ULL,
                                  sizeof(long),
                                  sizeof(sic::support::Index),
                                  (std::uint64_t)group.ar.s,
                                  (std::uint64_t)group.ar.Delta,
                                  (std::uint64_t)group.ar.original_Delta,
                                  (std::uint64_t)group.ar.order_index,
                                  (std::uint64_t)group.ar.h(),
                                  (std::uint64_t)group.npts,
                                  (std::uint64_t)group.N,
                                  (std::uint64_t)group.nb(),
                                  (std::uint64_t)group.invf.size()};
  write(file, header, std::size(header));
  const std::string identity = source_id();
  const std::uint64_t length = identity.size();
  write(file, &length);
  write(file, identity.data(), identity.size());
  write(file, group.invf.data(), group.invf.size());
  write(file, group.base.data(), group.base.size());
  write(file, group.nn.data(), group.nn.size());
  write(file, group.chi.data(), group.chi.size());
  write(file, group.img.data(), group.img.size());
  write(file, group.rep_elem.data(), group.rep_elem.size());
  write(file, group.rep_base.data(), group.rep_base.size());
  if (fflush(file)) throw sic::Error(sic::ErrorKind::io, "checkpoint flush failed");
  sic::support::sync_file(fd);
  if (fclose(owner.release())) throw sic::Error(sic::ErrorKind::io, "checkpoint close failed");
#ifdef SIC_ENABLE_TEST_HOOKS
  if (rename_fault) rename_fault();
#endif
  std::filesystem::rename(partial, path);
  const std::string digest = checkpoint_digest(path);
  sic::support::atomic_write(digest_path, digest + "\n", false, 0600);
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

void GroupCheckpoint::load(Group& group, const std::filesystem::path& path, long expected_elements,
                           const std::function<void(size_t)>& admit) try {
  std::ifstream checksum(path.string() + ".sha256");
  if (!checksum) throw sic::Error(sic::ErrorKind::io, "cannot open group checkpoint checksum");
  checksum.exceptions(std::ios::badbit);
  std::string expected, extra;
  if (!(checksum >> expected) || (checksum >> extra) || expected.size() != 64 ||
      expected.find_first_not_of("0123456789abcdef") != std::string::npos)
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid group checkpoint checksum");
  if (checkpoint_digest(path) != expected)
    throw sic::Error(sic::ErrorKind::integrity, "group checkpoint SHA-256 verification failed");
  std::unique_ptr<FILE, decltype(&fclose)> file(fopen(path.c_str(), "rb"), fclose);
  if (!file) throw sic::Error(sic::ErrorKind::io, "cannot open group checkpoint");
  std::uint64_t header[14];
  read(file.get(), header, std::size(header));
  const auto& ar = group.ar;
  if (header[0] != 0x53494347524f5550ULL || header[1] != 1 || header[2] != 0x0102030405060708ULL ||
      header[3] != sizeof(long) || header[4] != sizeof(sic::support::Index))
    throw sic::Error(sic::ErrorKind::invalid_format, "group checkpoint schema mismatch");
  if (header[5] != (std::uint64_t)ar.s || header[6] != (std::uint64_t)ar.Delta ||
      header[7] != (std::uint64_t)ar.original_Delta || header[8] != (std::uint64_t)ar.order_index ||
      header[9] != (std::uint64_t)ar.h() || header[10] != (std::uint64_t)group.npts ||
      header[11] != (std::uint64_t)expected_elements)
    throw sic::Error(sic::ErrorKind::integrity, "group checkpoint arithmetic/dimension mismatch");
  if (!header[12] || header[12] > (std::uint64_t)group.npts || !header[13] || header[13] > 64)
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid group checkpoint base/rank");
  std::uint64_t length = 0;
  read(file.get(), &length);
  if (length > 1024)
    throw sic::Error(sic::ErrorKind::invalid_format, "group checkpoint source identity length");
  std::string identity(length, '\0');
  read(file.get(), identity.data(), identity.size());
  if (identity != source_id())
    throw sic::Error(sic::ErrorKind::integrity, "group checkpoint source mismatch");
  const auto expected_bytes = sic::support::resources::add(
      sic::support::resources::add(sizeof header + sizeof length, identity.size()),
      sic::support::resources::add(
          sic::support::resources::mul(header[13], sizeof(long)),
          sic::support::resources::add(
              sic::support::resources::mul(header[12], sizeof(sic::support::Index)),
              sic::support::resources::add(
                  sic::support::resources::mul(header[11], sizeof(long) + sizeof(int)),
                  sic::support::resources::add(
                      sic::support::resources::mul(
                          sic::support::resources::mul(header[11], header[12]),
                          sizeof(sic::support::Index)),
                      sic::support::resources::mul(header[10],
                                                   2 * sizeof(sic::support::Index)))))));
  if (std::filesystem::file_size(path) != expected_bytes)
    throw sic::Error(sic::ErrorKind::invalid_format, "group checkpoint length mismatch");
  admit(header[12]);
  group.N = expected_elements;
  group.invf.resize(header[13]);
  read(file.get(), group.invf.data(), group.invf.size());
  group.stride.resize(group.invf.size());
  sic::support::resources::Count product = 1;
  for (size_t i = 0; i < group.invf.size(); ++i) {
    if (group.invf[i] <= 1)
      throw sic::Error(sic::ErrorKind::invalid_format, "invalid cached Smith factor");
    group.stride[i] = product;
    product = sic::support::resources::mul(product, group.invf[i]);
  }
  if (product != (sic::support::resources::Count)group.N)
    throw sic::Error(sic::ErrorKind::invalid_format, "cached Smith order mismatch");
  group.base.resize(header[12]);
  group.nn.resize(group.N);
  group.chi.resize(group.N);
  group.img.resize(sic::support::resources::mul(group.N, header[12]));
  group.rep_elem.resize(group.npts);
  group.rep_base.resize(group.npts);
  read(file.get(), group.base.data(), group.base.size());
  read(file.get(), group.nn.data(), group.nn.size());
  read(file.get(), group.chi.data(), group.chi.size());
  read(file.get(), group.img.data(), group.img.size());
  read(file.get(), group.rep_elem.data(), group.rep_elem.size());
  read(file.get(), group.rep_base.data(), group.rep_base.size());
  const int trailing = fgetc(file.get());
  if (ferror(file.get())) throw sic::Error(sic::ErrorKind::io, "group checkpoint read failure");
  if (trailing != EOF)
    throw sic::Error(sic::ErrorKind::invalid_format, "group checkpoint trailing data");
  for (sic::support::Index point : group.base)
    if (point < 0 || point >= group.npts || group.orbit[point] != point)
      throw sic::Error(sic::ErrorKind::invalid_format, "invalid cached action base");
  for (long g = 0; g < group.N; ++g)
    if (group.nn[g] <= 0 || group.nn[g] >= 2 * group.d || !(group.nn[g] & 1) ||
        (group.chi[g] != 1 && group.chi[g] != -1))
      throw sic::Error(sic::ErrorKind::invalid_format, "invalid cached scalar action");
  for (sic::support::Index point : group.img)
    if (point < 0 || point >= group.npts || group.orbit[point] != point)
      throw sic::Error(sic::ErrorKind::invalid_format, "invalid cached image");
  for (long point = 0; point < group.npts; ++point)
    if (group.orbit[point] == point) {
      const sic::support::Index g = group.rep_elem[point], b = group.rep_base[point];
      if (g < 0 || g >= group.N || b < 0 || (size_t)b >= group.nb() ||
          group.img[(size_t)g * group.nb() + b] != point)
        throw sic::Error(sic::ErrorKind::invalid_format,
                         "invalid cached canonical point representative");
    }
  if (group.nn[0] != 1 || group.chi[0] != 1 || group.img[0] != group.base[0])
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid cached identity action");
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
} catch (const std::ios_base::failure& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

}  // namespace sic::exact
