// SPDX-License-Identifier: AGPL-3.0-or-later

// Optional disk-backed storage for exact algebraic-integer leaves.
#pragma once
#include <flint/fmpz.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "exact/runtime/disk_budget.hpp"
#include "sic/error.hpp"
#include "sic/support/kernel_settings.hpp"
#include "support/lattice_coordinates.hpp"

namespace sic::exact {

struct Leaf {
  fmpz_t x, y;
  Leaf();
  ~Leaf() {
    fmpz_clear(x);
    fmpz_clear(y);
  }
  Leaf(const Leaf&) = delete;
  Leaf& operator=(const Leaf&) = delete;
};

void write_leaf_text(FILE* output, std::span<const Leaf> leaves);

class LeafSpool {
 public:
  struct Ticket {
    off_t offset = -1;
    size_t count = 0;
    slong max_bits = 0;
    std::shared_ptr<std::vector<off_t>> index;
    std::shared_ptr<const std::vector<Ticket>> parts;
    bool compressed = false;
    size_t stride = 65536;
    long lattice_discriminant = 0;
    Ticket() = default;
    Ticket(off_t start, size_t length, slong bits, std::shared_ptr<std::vector<off_t>> positions,
           std::shared_ptr<const std::vector<Ticket>> segments = {})
        : offset(start),
          count(length),
          max_bits(bits),
          index(std::move(positions)),
          parts(std::move(segments)) {}
  };

  // Disabled by default so callers opt in before incurring disk I/O.
  explicit LeafSpool(bool enabled = false, bool compression = true, long discriminant = 0);

  LeafSpool(const LeafSpool&) = delete;
  LeafSpool& operator=(const LeafSpool&) = delete;

  ~LeafSpool();

  bool enabled() const { return enabled_; }

  sic::support::resources::Bytes bytes() const;

  // A logical concatenation retains the original private records and their
  // indexes; it does not duplicate integer payloads on disk.
  Ticket concatenate(std::vector<Ticket> parts);

  Ticket append(const std::vector<Leaf>& leaves);

  template <class Producer>
  Ticket append_generated(size_t count, Producer&& producer, size_t block_size = 262144);

  class Cursor {
   public:
    Cursor(LeafSpool& spool, Ticket ticket, size_t start = 0);
    const Leaf& next();

   private:
    LeafSpool& spool_;
    Ticket ticket_;
    std::vector<Leaf> buffer_;
    off_t byte_position_;
    size_t position_ = 0, size_ = 0, consumed_ = 0;
    size_t part_ = 0, child_consumed_ = 0;
    std::unique_ptr<Cursor> child_;
  };

  // Re-read exact integers in bounded blocks, including on precision retries.
  // The callback completes before the next block is read and may use workers.
  template <class Consumer>
  void read_blocks(Ticket ticket, Consumer&& consumer, size_t block_size = 65536);

  std::vector<Leaf> load(Ticket ticket);

  void write_text(FILE* output, Ticket ticket);

 private:
  bool enabled_;
  bool compressed_;
  long lattice_discriminant_ = 0;
  FILE* file_ = nullptr;

  static sic::support::resources::Bytes lattice_scratch(sic::support::resources::BitCount bits);
  void admit_lattice_decode(size_t count, slong original_bits) const;

  void write_compressed(std::span<const Leaf> block);

  void read_compressed(std::span<Leaf> block);

  static sic::Error io_error(const char* operation) {
    return sic::Error(sic::ErrorKind::io, std::string(operation) + ": " + std::strerror(errno));
  }

  static sic::Error read_error(FILE* file, const char* operation) {
    if (ferror(file)) return io_error(operation);
    return sic::Error(sic::ErrorKind::invalid_format, std::string(operation) + ": short record");
  }

  void require_enabled() const;

  void seek(Ticket ticket);
};

}  // namespace sic::exact

#include "exact/storage/leaf_store.tpp"
