// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/storage/leaf_store.hpp"

namespace sic::exact {
Leaf::Leaf() {
  fmpz_init(x);
  fmpz_init(y);
}

void write_leaf_text(FILE* output, std::span<const Leaf> leaves) {
  sic::support::resources::Bytes bytes = 0;
  for (const auto& leaf : leaves) {
    bytes = sic::support::resources::add(
        bytes,
        sic::support::resources::add(
            2, sic::support::resources::add(fmpz_sizeinbase(leaf.x, 10) + (fmpz_sgn(leaf.x) < 0),
                                            fmpz_sizeinbase(leaf.y, 10) + (fmpz_sgn(leaf.y) < 0))));
  }
  require_leaf_disk(output, bytes);
  for (const auto& leaf : leaves)
    if (fmpz_fprint(output, leaf.x) < 0 || fputc(' ', output) == EOF ||
        fmpz_fprint(output, leaf.y) < 0 || fputc('\n', output) == EOF)
      throw sic::Error(sic::ErrorKind::io, "write text leaf failed");
}

LeafSpool::LeafSpool(bool enabled, bool compression, long discriminant)
    : enabled_(enabled), compressed_(compression) {
  if (!enabled_) return;
  const char* lattice = sic::support::kernel_setting("SIC_SPOOL_LATTICE");
  if (lattice && std::strcmp(lattice, "0") != 0) {
    sic::support::LatticeCoordinates validate(discriminant);
    lattice_discriminant_ = discriminant;
  }

  char name[] = "/tmp/sic-actions-XXXXXX";
  const int fd = mkstemp(name);
  if (fd < 0) throw io_error("mkstemp");
  if (unlink(name) != 0) {
    const int saved_errno = errno;
    close(fd);
    unlink(name);
    errno = saved_errno;
    throw io_error("unlink temporary spool");
  }
  file_ = fdopen(fd, "w+b");
  if (file_ == nullptr) {
    const int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    throw io_error("fdopen temporary spool");
  }
}

LeafSpool::~LeafSpool() {
  if (file_ != nullptr) fclose(file_);
}

sic::support::resources::Bytes LeafSpool::bytes() const {
  require_enabled();
  struct stat status {};
  if (fstat(fileno(file_), &status) != 0 || status.st_size < 0) throw io_error("stat leaf spool");
  return status.st_size;
}

LeafSpool::Ticket LeafSpool::concatenate(std::vector<Ticket> parts) {
  require_enabled();
  Ticket result;
  for (const auto& part : parts) {
    if (part.offset < 0 || !part.count)
      throw std::invalid_argument("invalid leaf concatenation part");
    if (result.offset < 0) result.offset = part.offset;
    result.count = sic::support::resources::add(result.count, part.count);
    result.max_bits = std::max(result.max_bits, part.max_bits);
  }
  if (!result.count) throw std::invalid_argument("empty leaf concatenation");
  result.parts = std::make_shared<const std::vector<Ticket>>(std::move(parts));
  return result;
}

LeafSpool::Ticket LeafSpool::append(const std::vector<Leaf>& leaves) {
  require_enabled();
  if (lattice_discriminant_ && !leaves.empty()) {
    slong bits = 0;
    for (const auto& leaf : leaves)
      bits = std::max(bits, (slong)std::max(fmpz_bits(leaf.x), fmpz_bits(leaf.y)));
    sic::support::resources::Budget().require_additional(
        "lattice spool immutable append block",
        sic::support::resources::add(
            sic::support::resources::leaf_block_bytes(std::min<size_t>(4096, leaves.size()),
                                                      sic::support::resources::add(bits, 66)),
            sic::support::resources::mul(
                16,
                sic::support::resources::add(
                    32,
                    sic::support::resources::add(sic::support::resources::mul(2, bits), 71) / 8))));
    return append_generated(
        leaves.size(),
        [&](size_t first, std::span<Leaf> block) {
          for (size_t q = 0; q < block.size(); ++q) {
            fmpz_set(block[q].x, leaves[first + q].x);
            fmpz_set(block[q].y, leaves[first + q].y);
          }
        },
        4096);
  }
  if (fseeko(file_, 0, SEEK_END) != 0) throw io_error("seek spool end");
  const off_t start = ftello(file_);
  if (start < 0) throw io_error("tell spool end");
  slong max_bits = 0;
  auto index = std::make_shared<std::vector<off_t>>();
  if (compressed_) {
    for (size_t first = 0; first < leaves.size(); first += 65536) {
      index->push_back(ftello(file_));
      const auto block = std::span<const Leaf>(leaves).subspan(
          first, std::min<size_t>(65536, leaves.size() - first));
      for (const auto& leaf : block)
        max_bits = std::max(max_bits, (slong)std::max(fmpz_bits(leaf.x), fmpz_bits(leaf.y)));
      write_compressed(block);
    }
    if (fflush(file_) != 0 || ferror(file_)) throw io_error("flush compressed spool");
    Ticket ticket{start, leaves.size(), max_bits, std::move(index)};
    ticket.compressed = true;
    return ticket;
  }
  size_t ordinal = 0;
  sic::support::resources::Bytes bytes = 0;
  for (const auto& leaf : leaves)
    bytes = sic::support::resources::add(
        bytes, 8 + (fmpz_bits(leaf.x) + 7) / 8 + (fmpz_bits(leaf.y) + 7) / 8);
  require_leaf_disk(file_, bytes);
  for (const auto& leaf : leaves) {
    if (ordinal++ % 65536 == 0) index->push_back(ftello(file_));
    max_bits = std::max(max_bits, (slong)std::max(fmpz_bits(leaf.x), fmpz_bits(leaf.y)));
    if (fmpz_out_raw(file_, leaf.x) == 0 || fmpz_out_raw(file_, leaf.y) == 0)
      throw io_error("write raw leaf");
  }
  if (fflush(file_) != 0 || ferror(file_)) throw io_error("flush spool");
  return Ticket{start, leaves.size(), max_bits, std::move(index), {}};
}

LeafSpool::Cursor::Cursor(LeafSpool& spool, Ticket ticket, size_t start)
    : spool_(spool),
      ticket_(ticket),
      buffer_(ticket.parts ? 0 : std::min(ticket.stride, ticket.count)),
      byte_position_(ticket.offset) {
  spool_.require_enabled();
  if (start > ticket.count || ticket.offset < 0 || !ticket.stride)
    throw std::invalid_argument("invalid leaf cursor");
  if (start == ticket.count) {
    consumed_ = start;
    return;
  }
  if (ticket.parts) {
    consumed_ = start;
    size_t local = start;
    while (part_ < ticket.parts->size() && local >= ticket.parts->at(part_).count)
      local -= ticket.parts->at(part_++).count;
    if (part_ < ticket.parts->size()) {
      child_ = std::make_unique<Cursor>(spool_, ticket.parts->at(part_), local);
      child_consumed_ = local;
    }
    return;
  }
  if (ticket.index && start < ticket.count) {
    consumed_ = (start / ticket.stride) * ticket.stride;
    byte_position_ = ticket.index->at(start / ticket.stride);
  }
  const size_t skip = start - consumed_;
  for (size_t q = 0; q < skip; ++q) next();
}

const Leaf& LeafSpool::Cursor::next() {
  if (consumed_ == ticket_.count) throw std::runtime_error("leaf cursor past logical end");
  if (ticket_.parts) {
    if (child_consumed_ == ticket_.parts->at(part_).count) {
      ++part_;
      child_consumed_ = 0;
      child_.reset();
      child_ = std::make_unique<Cursor>(spool_, ticket_.parts->at(part_));
    }
    const Leaf& value = child_->next();
    ++consumed_;
    ++child_consumed_;
    return value;
  }
  if (position_ == size_) {
    if (fseeko(spool_.file_, byte_position_, SEEK_SET) != 0) throw io_error("seek leaf cursor");
    size_ = std::min(buffer_.size(), ticket_.count - consumed_);
    position_ = 0;
    if (ticket_.compressed)
      spool_.read_compressed(std::span<Leaf>(buffer_.data(), size_));
    else
      for (size_t q = 0; q < size_; ++q)
        if (fmpz_inp_raw(buffer_[q].x, spool_.file_) == 0 ||
            fmpz_inp_raw(buffer_[q].y, spool_.file_) == 0)
          throw read_error(spool_.file_, "read leaf cursor block");
    if (ticket_.lattice_discriminant) {
      spool_.admit_lattice_decode(size_, ticket_.max_bits);
      sic::support::LatticeCoordinates coordinates(ticket_.lattice_discriminant);
      for (size_t q = 0; q < size_; ++q) coordinates.decode(buffer_[q].x, buffer_[q].y);
    }
    byte_position_ = ftello(spool_.file_);
    if (byte_position_ < 0) throw io_error("tell leaf cursor position");
  }
  ++consumed_;
  return buffer_[position_++];
}

std::vector<Leaf> LeafSpool::load(Ticket ticket) {
  if (ticket.parts || ticket.compressed || ticket.lattice_discriminant) {
    std::vector<Leaf> leaves(ticket.count);
    read_blocks(ticket, [&](size_t first, std::span<Leaf> block) {
      for (size_t q = 0; q < block.size(); ++q) {
        fmpz_swap(leaves[first + q].x, block[q].x);
        fmpz_swap(leaves[first + q].y, block[q].y);
      }
    });
    return leaves;
  }
  seek(ticket);
  std::vector<Leaf> leaves(ticket.count);
  for (auto& leaf : leaves) {
    if (fmpz_inp_raw(leaf.x, file_) == 0 || fmpz_inp_raw(leaf.y, file_) == 0)
      throw read_error(file_, "read raw leaf");
  }
  if (ferror(file_)) throw io_error("read spool");
  return leaves;
}

void LeafSpool::write_text(FILE* output, Ticket ticket) {
  if (output == nullptr) throw std::invalid_argument("null leaf output stream");
  read_blocks(ticket, [&](size_t, std::span<Leaf> block) { write_leaf_text(output, block); });
  if (fflush(output) != 0 || ferror(output))
    throw sic::Error(sic::ErrorKind::io, "flush text leaves failed");
  if (ferror(file_)) throw io_error("read spool for text output");
}

sic::support::resources::Bytes LeafSpool::lattice_scratch(sic::support::resources::BitCount bits) {
  return sic::support::resources::mul(
      16, sic::support::resources::add(
              32, sic::support::resources::add(sic::support::resources::mul(2, bits), 71) / 8));
}

void LeafSpool::admit_lattice_decode(size_t count, slong original_bits) const {
  sic::support::resources::Budget().require_additional(
      "lattice spool decoded block/square-root scratch",
      sic::support::resources::add(sic::support::resources::leaf_block_bytes(count, original_bits),
                                   lattice_scratch(original_bits)));
}

void LeafSpool::write_compressed(std::span<const Leaf> block) {
  const auto length = static_cast<std::uint32_t>(block.size());
  if (!length || length > 65536) throw std::invalid_argument("invalid compressed leaf block");
  std::vector<unsigned char> bitmap((length + 7) / 8, 0);
  sic::support::resources::Bytes bytes = sizeof(length) + bitmap.size();
  for (size_t q = 0; q < block.size(); ++q)
    if (!fmpz_is_zero(block[q].x) || !fmpz_is_zero(block[q].y)) {
      bitmap[q / 8] |= 1U << (q % 8);
      bytes = sic::support::resources::add(
          bytes, 8 + (fmpz_bits(block[q].x) + 7) / 8 + (fmpz_bits(block[q].y) + 7) / 8);
    }
  require_leaf_disk(file_, bytes);
  if (fwrite(&length, sizeof(length), 1, file_) != 1 ||
      fwrite(bitmap.data(), 1, bitmap.size(), file_) != bitmap.size())
    throw io_error("write compressed leaf bitmap");
  for (size_t q = 0; q < block.size(); ++q)
    if (bitmap[q / 8] & (1U << (q % 8)))
      if (!fmpz_out_raw(file_, block[q].x) || !fmpz_out_raw(file_, block[q].y))
        throw io_error("write compressed leaf integers");
}

void LeafSpool::read_compressed(std::span<Leaf> block) {
  std::uint32_t length = 0;
  if (fread(&length, sizeof(length), 1, file_) != 1 || length != block.size() || !length ||
      length > 65536)
    throw sic::Error(ferror(file_) ? sic::ErrorKind::io : sic::ErrorKind::invalid_format,
                     "invalid or truncated compressed leaf block");
  std::vector<unsigned char> bitmap((length + 7) / 8);
  if (fread(bitmap.data(), 1, bitmap.size(), file_) != bitmap.size())
    throw read_error(file_, "read compressed leaf bitmap");
  if (length % 8 && (bitmap.back() >> (length % 8)))
    throw sic::Error(sic::ErrorKind::invalid_format, "nonzero compressed leaf padding");
  for (size_t q = 0; q < block.size(); ++q) {
    if (bitmap[q / 8] & (1U << (q % 8))) {
      if (!fmpz_inp_raw(block[q].x, file_) || !fmpz_inp_raw(block[q].y, file_))
        throw read_error(file_, "read compressed leaf integers");
    } else {
      fmpz_zero(block[q].x);
      fmpz_zero(block[q].y);
    }
  }
}

void LeafSpool::require_enabled() const {
  if (!enabled_ || file_ == nullptr) throw std::logic_error("leaf spool is disabled");
}

void LeafSpool::seek(Ticket ticket) {
  require_enabled();
  if (ticket.offset < 0) throw std::invalid_argument("invalid leaf spool ticket");
  if (fseeko(file_, ticket.offset, SEEK_SET) != 0) throw io_error("seek spool ticket");
}

}  // namespace sic::exact
