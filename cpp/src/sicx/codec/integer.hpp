// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/sicx/format.hpp"
namespace sic::sicx {
uint64_t checked_add(uint64_t a, uint64_t b);
uint64_t checked_mul(uint64_t a, uint64_t b);
void put_le(ByteBuffer& out, uint64_t value, int bytes);
uint64_t get_le(const uint8_t* data, int bytes);
void put_varint(ByteBuffer& out, uint64_t value);

struct Cursor {
  const uint8_t* at;
  const uint8_t* end;
  Cursor(const uint8_t* data, size_t size) : at(data), end(data + size) {}
  size_t remaining() const { return static_cast<size_t>(end - at); }
  const uint8_t* take(size_t size);
  // Canonical unsigned LEB128: at most 10 bytes, no redundant trailing group.
  uint64_t varint();
};

// ------------------------------------------------------- integer code
// code = 2*n + sign, then n little-endian magnitude bytes (most significant
// byte nonzero). Zero is the single byte 0; code 1 (negative zero) is invalid.
class IntCodec {
  fmpz_t magnitude_;
  std::vector<ulong> limbs_;

 public:
  IntCodec();
  ~IntCodec();
  IntCodec(const IntCodec&) = delete;
  IntCodec& operator=(const IntCodec&) = delete;

  void put(ByteBuffer& out, const fmpz_t value);
  void get(Cursor& in, fmpz_t value);
};

// The shared JSON API keeps SICX's existing canonical bytes and error contract.

}  // namespace sic::sicx
