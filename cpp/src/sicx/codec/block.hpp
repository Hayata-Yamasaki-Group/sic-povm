// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/sicx/format.hpp"
#include "sicx/codec/integer.hpp"
#include "support/lattice_coordinates.hpp"
namespace sic::sicx {

class PairTransform {
  std::optional<sic::support::LatticeCoordinates> lattice_;
  bool balanced_ =
      false;  // D0 < 0, D0 = 1 (mod 4): r = x + floor(y/2) (real part of x + y*omega, rounded down)

 public:
  explicit PairTransform(int64_t D0);
  void encode(fmpz_t x, const fmpz_t y);
  void decode(fmpz_t r, const fmpz_t y);
};

struct FmpzPair {
  fmpz_t x, y;
  FmpzPair();
  ~FmpzPair();
  FmpzPair(const FmpzPair&) = delete;
  FmpzPair& operator=(const FmpzPair&) = delete;
};

// Encoder input: the nonzero coordinates of one block in increasing sub-index order.
struct BlockEncoder {
  IntCodec codec;
  fmpz_t r;
  PairTransform transform;
  explicit BlockEncoder(int64_t D0);
  ~BlockEncoder();
  BlockEncoder(const BlockEncoder&) = delete;
  BlockEncoder& operator=(const BlockEncoder&) = delete;

  // items: sub-index (absolute) and stored coordinate (x, y), all nonzero, increasing.
  template <class Get>
  ByteBuffer encode(uint64_t element, uint64_t first, uint64_t count, size_t items, Get get);
};

// Decode a block; calls pair(sub_index, x, y) for each data pair (original basis)
// and zeros(sub_index, n) for each zero run. Checks canonical form and bounds.
template <class Pair, class Zeros>
BlockHeader decode_block(std::span<const uint8_t> payload, const Meta& meta, Pair pair, Zeros zeros,
                         IntCodec& codec, PairTransform& transform, FmpzPair& value);

// ------------------------------------------------------------------ BALL
// One record per entry: real part then imaginary part; each part is
// u64 flags (bit 0: mid negative) | i64 mid exponent | u64 radius mantissa |
// i64 radius exponent | limbs x u64 |mid mantissa| (little-endian).
// Text form: the Arb dump "M E m e" (hex), exactly as in SIC_VECTOR_BALLS files.

}  // namespace sic::sicx
#include "sicx/codec/block.tpp"
