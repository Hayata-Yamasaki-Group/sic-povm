// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::exact {
template <class Source>
BallFile::BallFile(size_t count, size_t components, Source&& source) try
    : temporary_(std::make_unique<Temporary>()), count_(count), components_(components) {
  path_ = temporary_->path;
  if (!count || (components != 1 && components != 2))
    throw std::invalid_argument("invalid ball backing dimensions");
  FILE* file = temporary_->file.get();
  const std::uint64_t header[] = {kMagic, 1, MAG_BITS, sizeof(ulong), count, components};
  if (fwrite(header, sizeof header, 1, file) != 1)
    throw sic::Error(sic::ErrorKind::io, "ball backing header write failed");
  Integers work;
  constexpr size_t kBlock = 4096;
  for (size_t first = 0; first < count; first += kBlock) {
    const size_t end = std::min(count, first + kBlock);
    sic::support::resources::Bytes bytes = 0;
    for (size_t q = first; q < end; ++q)
      for (size_t part = 0; part < components; ++part) {
        decompose(source(q, part), work);
        for (const auto& integer : work.value) {
          const auto code = leaf_codec::integer_code(&integer);
          bytes = sic::support::resources::add(
              bytes, sic::support::resources::add(code / 2, leaf_codec::varint_size(code)));
        }
      }
    require_leaf_disk(file, bytes);
    for (size_t q = first; q < end; ++q)
      for (size_t part = 0; part < components; ++part) {
        decompose(source(q, part), work);
        for (const auto& integer : work.value) leaf_codec::write_integer(file, &integer);
      }
  }
  if (fflush(file) != 0) throw sic::Error(sic::ErrorKind::io, "ball backing flush failed");
  sic::support::sync_file(fileno(file));
  if (fclose(temporary_->file.release()))
    throw sic::Error(sic::ErrorKind::io, "ball backing close failed");
  bytes_ = std::filesystem::file_size(path_);
  digest_ = checkpoint_digest(path_);
} catch (const std::filesystem::filesystem_error& error) {
  throw sic::Error(sic::ErrorKind::io, error.what());
}

template <class Destination>
void BallFile::load(FILE* file, Destination&& destination) const try {
  Integers work;
  leaf_codec::Count budget = 128 * sic::support::resources::kMib;
  for (size_t q = 0; q < count_; ++q) {
    if (q % 4096 == 0) budget = 128 * sic::support::resources::kMib;
    for (size_t part = 0; part < components_; ++part)
      read_ball(file, destination(q, part), work, budget);
  }
  const int trailing = fgetc(file);
  if (ferror(file)) throw sic::Error(sic::ErrorKind::io, "unreadable ball backing");
  if (trailing != EOF) throw sic::Error(sic::ErrorKind::invalid_format, "trailing ball backing");
} catch (const sic::Error&) {
  throw;
} catch (const std::runtime_error& error) {
  throw sic::Error(ferror(file) ? sic::ErrorKind::io : sic::ErrorKind::invalid_format,
                   error.what());
}

}  // namespace sic::exact
