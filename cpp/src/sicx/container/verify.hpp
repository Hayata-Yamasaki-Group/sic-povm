// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/sicx/verify.hpp"
#include "sicx/codec/block.hpp"
#include "support/sha256.hpp"
namespace sic::sicx {
class LeafDigest {
  support::sha256::Hasher hasher_;
  IntCodec codec_;
  ByteBuffer buffer_;
  uint64_t nonzero_ = 0;

 public:
  void add(uint64_t position, const fmpz_t x, const fmpz_t y);
  void add_bytes(const ByteBuffer& bytes, uint64_t nonzero);
  uint64_t nonzero() const;
  Digest finish();
};
}  // namespace sic::sicx
