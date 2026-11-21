// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::exact {
template <class Producer>
LeafSpool::Ticket LeafSpool::append_generated(size_t count, Producer&& producer,
                                              size_t block_size) {
  require_enabled();
  if (!count || !block_size) throw std::invalid_argument("invalid generated leaf dimensions");
  if (fseeko(file_, 0, SEEK_END) != 0) throw io_error("seek generated spool end");
  const off_t start = ftello(file_);
  if (start < 0) throw io_error("tell generated spool end");
  Ticket ticket{start, count, 0, {}, {}};
  ticket.index = std::make_shared<std::vector<off_t>>();
  ticket.lattice_discriminant = lattice_discriminant_;
  if (compressed_) {
    block_size = std::min<size_t>(block_size, 65536);
    ticket.compressed = true;
    ticket.stride = block_size;
  }
  std::vector<Leaf> block(std::min(block_size, count));
  try {
    for (size_t first = 0; first < count;) {
      const size_t size = std::min(block.size(), count - first);
      producer(first, std::span<Leaf>(block.data(), size));
      // Decoder admission uses the original pair, never the residual code.
      for (size_t q = 0; q < size; ++q)
        ticket.max_bits = std::max(ticket.max_bits,
                                   (slong)std::max(fmpz_bits(block[q].x), fmpz_bits(block[q].y)));
      if (lattice_discriminant_) {
        sic::support::resources::Budget().require_additional(
            "lattice spool encoded block/square-root scratch",
            sic::support::resources::add(
                sic::support::resources::leaf_block_bytes(
                    size, sic::support::resources::add(ticket.max_bits, 66)),
                lattice_scratch(ticket.max_bits)));
        sic::support::LatticeCoordinates coordinates(lattice_discriminant_);
        for (size_t q = 0; q < size; ++q) coordinates.encode(block[q].x, block[q].y);
      }
      if (compressed_) {
        ticket.index->push_back(ftello(file_));
        write_compressed(std::span<const Leaf>(block.data(), size));
        first += size;
        continue;
      }
      sic::support::resources::Bytes bytes = 0;
      for (size_t q = 0; q < size; ++q) {
        const auto& leaf = block[q];
        bytes = sic::support::resources::add(
            bytes, 8 + (fmpz_bits(leaf.x) + 7) / 8 + (fmpz_bits(leaf.y) + 7) / 8);
      }
      require_leaf_disk(file_, bytes);
      for (size_t q = 0; q < size; ++q) {
        if ((first + q) % 65536 == 0) ticket.index->push_back(ftello(file_));
        if (fmpz_out_raw(file_, block[q].x) == 0 || fmpz_out_raw(file_, block[q].y) == 0)
          throw io_error("write generated leaf block");
      }
      first += size;
    }
    if (fflush(file_) != 0) throw io_error("flush generated leaves");
  } catch (...) {
    // This private append has no issued ticket. Discard only its incomplete
    // suffix so a numerical retry cannot retain dead disk allocations.
    const int flushed = fflush(file_);
    const int truncated = ftruncate(fileno(file_), start);
    clearerr(file_);
    const int positioned = fseeko(file_, start, SEEK_SET);
    if (flushed || truncated || positioned) throw io_error("rollback incomplete generated leaves");
    throw;
  }
  return ticket;
}

template <class Consumer>
void LeafSpool::read_blocks(Ticket ticket, Consumer&& consumer, size_t block_size) {
  if (block_size == 0) throw std::invalid_argument("zero leaf block size");
  std::function<void(Ticket, size_t)> visit = [&](Ticket current, size_t base) {
    if (current.parts) {
      size_t offset = 0;
      for (const auto& part : *current.parts) {
        visit(part, base + offset);
        offset += part.count;
      }
      return;
    }
    seek(current);
    if (current.compressed) {
      std::vector<Leaf> block(std::min(current.stride, current.count));
      for (size_t first = 0; first < current.count;) {
        const size_t size = std::min(block.size(), current.count - first);
        read_compressed(std::span<Leaf>(block.data(), size));
        if (current.lattice_discriminant) {
          admit_lattice_decode(size, current.max_bits);
          sic::support::LatticeCoordinates coordinates(current.lattice_discriminant);
          for (size_t q = 0; q < size; ++q) coordinates.decode(block[q].x, block[q].y);
        }
        const off_t next = ftello(file_);
        for (size_t offset = 0; offset < size; offset += block_size)
          consumer(base + first + offset,
                   std::span<Leaf>(block.data() + offset, std::min(block_size, size - offset)));
        first += size;
        if (first < current.count && fseeko(file_, next, SEEK_SET) != 0)
          throw io_error("restore compressed leaf position");
      }
      return;
    }
    std::vector<Leaf> block(std::min(block_size, current.count));
    for (size_t first = 0; first < current.count;) {
      const size_t count = std::min(block.size(), current.count - first);
      for (size_t q = 0; q < count; ++q)
        if (fmpz_inp_raw(block[q].x, file_) == 0 || fmpz_inp_raw(block[q].y, file_) == 0)
          throw read_error(file_, "read raw leaf block");
      if (current.lattice_discriminant) {
        admit_lattice_decode(count, current.max_bits);
        sic::support::LatticeCoordinates coordinates(current.lattice_discriminant);
        for (size_t q = 0; q < count; ++q) coordinates.decode(block[q].x, block[q].y);
      }
      consumer(base + first, std::span<Leaf>(block.data(), count));
      first += count;
    }
    if (ferror(file_)) throw io_error("read spool blocks");
  };
  visit(ticket, 0);
}

}  // namespace sic::exact
