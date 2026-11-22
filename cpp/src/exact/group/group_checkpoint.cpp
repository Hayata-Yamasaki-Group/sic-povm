// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/group/group_checkpoint.hpp"

#include <array>
#include <iomanip>

namespace sic::exact {
namespace {
using sic::support::Index;
using sic::support::resources::add;
using sic::support::resources::Count;
using sic::support::resources::mul;

enum HeaderField : size_t {
  kSignature,
  kVersion,
  kByteOrder,
  kLongBytes,
  kIndexBytes,
  kExponent,
  kDiscriminant,
  kOriginalDiscriminant,
  kOrderIndex,
  kClasses,
  kPoints,
  kElements,
  kBases,
  kRank,
  kFieldCount
};
using HeaderFields = std::array<std::uint64_t, kFieldCount>;
constexpr HeaderFields::value_type kMagic = 0x53494347524f5550ULL;
constexpr HeaderFields::value_type kEndian = 0x0102030405060708ULL;

struct Header {
  HeaderFields fields;
  std::string source;

  Count base_offset() const {
    return sizeof fields + sizeof(std::uint64_t) + source.size() + mul(fields[kRank], sizeof(long));
  }

  Count image_offset() const {
    return add(add(base_offset(), mul(fields[kBases], sizeof(Index))),
               mul(fields[kElements], sizeof(long) + sizeof(int)));
  }

  Count image_end() const {
    return add(image_offset(), mul(mul(fields[kElements], fields[kBases]), sizeof(Index)));
  }

  Count size() const { return add(image_end(), mul(fields[kPoints], 2 * sizeof(Index))); }
};

template <class Read, class Validate>
Header read_header(const Read& read, const Validate& validate) {
  Header header;
  auto& fields = header.fields;
  read(fields.data(), sizeof fields);
  if (fields[kSignature] != kMagic || fields[kVersion] != 1 || fields[kByteOrder] != kEndian ||
      fields[kLongBytes] != sizeof(long) || fields[kIndexBytes] != sizeof(Index))
    throw sic::Error(sic::ErrorKind::invalid_format, "group checkpoint schema mismatch");
  validate(fields);
  if (!fields[kBases] || fields[kBases] > fields[kPoints] || !fields[kRank] || fields[kRank] > 64)
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid group checkpoint base/rank");
  std::uint64_t length = 0;
  read(&length, sizeof length);
  if (length > 1024)
    throw sic::Error(sic::ErrorKind::invalid_format, "group checkpoint source identity length");
  header.source.resize(length);
  read(header.source.data(), header.source.size());
  return header;
}

template <class Read>
void read_smith(const Header& header, const Read& read, std::vector<long>& factors,
                std::vector<long>& strides) {
  factors.resize(header.fields[kRank]);
  read(factors.data(), factors.size() * sizeof(long));
  strides.resize(factors.size());
  Count product = 1;
  for (size_t i = 0; i < factors.size(); ++i) {
    if (factors[i] <= 1)
      throw sic::Error(sic::ErrorKind::invalid_format, "invalid cached Smith factor");
    strides[i] = product;
    product = mul(product, factors[i]);
  }
  if (product != header.fields[kElements])
    throw sic::Error(sic::ErrorKind::invalid_format, "cached Smith order mismatch");
}

std::string read_digest(const std::filesystem::path& path, std::string pin = {}) {
  if (pin.empty()) {
    std::ifstream checksum(path.string() + ".sha256");
    if (!checksum) throw sic::Error(sic::ErrorKind::io, "cannot open group checkpoint checksum");
    checksum.exceptions(std::ios::badbit);
    std::string extra;
    if (!(checksum >> std::setw(65) >> pin) || (checksum >> std::setw(1) >> extra))
      throw sic::Error(sic::ErrorKind::invalid_format, "invalid group checkpoint checksum");
  }
  if (pin.size() != 64 || pin.find_first_not_of("0123456789abcdef") != std::string::npos)
    throw sic::Error(sic::ErrorKind::invalid_format, "invalid group checkpoint checksum");
  return pin;
}
}  // namespace

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
  const std::uint64_t header[] = {kMagic,
                                  1,
                                  kEndian,
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
  const auto expected = read_digest(path);
  if (checkpoint_digest(path) != expected)
    throw sic::Error(sic::ErrorKind::integrity, "group checkpoint SHA-256 verification failed");
  std::unique_ptr<FILE, decltype(&fclose)> file(fopen(path.c_str(), "rb"), fclose);
  if (!file) throw sic::Error(sic::ErrorKind::io, "cannot open group checkpoint");
  const auto read_bytes = [&](void* destination, size_t size) {
    read(file.get(), static_cast<unsigned char*>(destination), size);
  };
  const auto header = read_header(read_bytes, [&](const auto& fields) {
    const auto& ar = group.ar;
    if (fields[kExponent] != (std::uint64_t)ar.s ||
        fields[kDiscriminant] != (std::uint64_t)ar.Delta ||
        fields[kOriginalDiscriminant] != (std::uint64_t)ar.original_Delta ||
        fields[kOrderIndex] != (std::uint64_t)ar.order_index ||
        fields[kClasses] != (std::uint64_t)ar.h() || fields[kPoints] != (std::uint64_t)group.npts ||
        fields[kElements] != (std::uint64_t)expected_elements)
      throw sic::Error(sic::ErrorKind::integrity, "group checkpoint arithmetic/dimension mismatch");
  });
  if (header.source != source_id())
    throw sic::Error(sic::ErrorKind::integrity, "group checkpoint source mismatch");
  if (std::filesystem::file_size(path) != header.size())
    throw sic::Error(sic::ErrorKind::invalid_format, "group checkpoint length mismatch");
  const auto bases = header.fields[kBases];
  admit(bases);
  group.N = expected_elements;
  read_smith(header, read_bytes, group.invf, group.stride);
  group.base.resize(bases);
  group.nn.resize(group.N);
  group.chi.resize(group.N);
  group.img.resize(sic::support::resources::mul(group.N, bases));
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

namespace {
struct ColumnEntry {
  Index value = 0;
  size_t filled = 0;

  bool read(Count offset, Count chunk_start, std::span<const unsigned char> chunk) {
    const Count start = offset + filled;
    const Count end = chunk_start + chunk.size();
    if (start >= end) return false;
    const size_t count = std::min<Count>(sizeof value - filled, end - start);
    std::memcpy(reinterpret_cast<unsigned char*>(&value) + filled,
                chunk.data() + (start - chunk_start), count);
    filled += count;
    if (filled != sizeof value) return false;
    filled = 0;
    return true;
  }
};
}  // namespace

GroupProjection GroupCheckpoint::project(const std::filesystem::path& path, std::string digest,
                                         bool read_ahead) try {
  if (!std::filesystem::is_regular_file(path))
    throw sic::Error(sic::ErrorKind::io, "group checkpoint is missing or not a regular file");
  const auto expected = read_digest(path, std::move(digest));
  const auto file_size = std::filesystem::file_size(path);
  GroupProjection result;
  Count length = 0, grid = 0, column = 0, row_bytes = 0;
  size_t samples = 0;
  ColumnEntry sample;
  const auto narrow_class = [&](Index image) -> Index {
    if (image <= 0 || image >= result.points || image % grid == 0)
      throw sic::Error(sic::ErrorKind::invalid_format, "invalid principal group image");
    return image / grid;
  };
  const auto visit = [&](std::span<const unsigned char> chunk) {
    if (!result.bytes) {
      auto prefix = chunk;
      const auto read_prefix = [&](void* destination, size_t size) {
        if (size > prefix.size())
          throw sic::Error(sic::ErrorKind::invalid_format, "truncated group checkpoint");
        std::memcpy(destination, prefix.data(), size);
        prefix = prefix.subspan(size);
      };
      const auto header = read_header(read_prefix, [&](const HeaderFields& fields) {
        if (fields[kExponent] < 2 || fields[kExponent] > DimensionPolicy{}.maximum_exponent)
          throw sic::Error(sic::ErrorKind::unsupported,
                           "group checkpoint dimension is unsupported");
        grid = Count{1} << (2 * fields[kExponent]);
        constexpr auto kPointLimit = std::numeric_limits<Index>::max();
        if (!fields[kClasses] || fields[kClasses] > kPointLimit / grid ||
            fields[kPoints] != fields[kClasses] * grid || !fields[kElements] ||
            fields[kElements] > fields[kPoints] / 2 || !fields[kDiscriminant] ||
            fields[kDiscriminant] > fields[kOriginalDiscriminant] ||
            fields[kOriginalDiscriminant] >= grid || !fields[kOrderIndex] ||
            fields[kOrderIndex] >= grid)
          throw sic::Error(sic::ErrorKind::invalid_format, "invalid group arithmetic or degree");
      });
      if (header.source != source_id())
        throw sic::Error(sic::ErrorKind::integrity, "group checkpoint source mismatch");
      const auto& fields = header.fields;
      result.exponent = fields[kExponent];
      result.discriminant = fields[kDiscriminant];
      result.original_discriminant = fields[kOriginalDiscriminant];
      result.order_index = fields[kOrderIndex];
      result.classes = fields[kClasses];
      result.points = fields[kPoints];
      result.elements = fields[kElements];
      result.source = header.source;
      length = header.size();
      if (length != file_size)
        throw sic::Error(sic::ErrorKind::invalid_format, "group checkpoint length mismatch");
      if (fields[kBases] < 2)
        throw sic::Error(sic::ErrorKind::invalid_format, "missing principal group base");
      read_smith(header, read_prefix, result.smith, result.stride);
      for (size_t i = 1; i < result.smith.size(); ++i)
        if (result.smith[i] % result.smith[i - 1])
          throw sic::Error(sic::ErrorKind::invalid_format, "invalid Smith divisibility");
      std::array<Index, 2> bases;
      read_prefix(bases.data(), sizeof bases);
      if (bases != std::array<Index, 2>{0, 1})
        throw sic::Error(sic::ErrorKind::invalid_format, "missing principal group base");
      column = header.image_offset() + sizeof(Index);
      row_bytes = fields[kBases] * sizeof(Index);
    }
    if (chunk.size() > length - result.bytes)
      throw sic::Error(sic::ErrorKind::invalid_format, "group checkpoint length mismatch");
    while (samples <= result.stride.size()) {
      const Count row = samples == 0 ? 0 : result.stride[samples - 1];
      if (!sample.read(column + row * row_bytes, result.bytes, chunk)) break;
      if (samples) {
        result.axis_classes.push_back(narrow_class(sample.value));
      } else if (sample.value != 1) {
        throw sic::Error(sic::ErrorKind::invalid_format, "invalid principal identity image");
      }
      ++samples;
    }
    result.bytes += chunk.size();
  };
  result.digest = sic::support::sha256::file_hex_visit(path, read_ahead, visit);
  if (result.bytes != length || samples != result.stride.size() + 1)
    throw sic::Error(sic::ErrorKind::invalid_format, "group checkpoint length mismatch");
  if (result.digest != expected)
    throw sic::Error(sic::ErrorKind::integrity, "group checkpoint SHA-256 verification failed");
  return result;
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
} catch (const std::ios_base::failure& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

}  // namespace sic::exact
