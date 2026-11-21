// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/integer.hpp"

namespace sic::sicx {
void put_le(ByteBuffer& out, uint64_t value, int bytes) {
  for (int i = 0; i < bytes; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t get_le(const uint8_t* data, int bytes) {
  uint64_t value = 0;
  for (int i = 0; i < bytes; ++i) value |= uint64_t(data[i]) << (8 * i);
  return value;
}
}  // namespace sic::sicx

namespace sic::sicx {
void put_varint(ByteBuffer& out, uint64_t value) {
  while (value >= 0x80) {
    out.push_back(static_cast<uint8_t>(value | 0x80));
    value >>= 7;
  }
  out.push_back(static_cast<uint8_t>(value));
}
}  // namespace sic::sicx

namespace sic::sicx {
const uint8_t* Cursor::take(size_t size) {
  if (size > remaining()) fail(sic::ErrorKind::invalid_format, "truncated payload");
  const uint8_t* result = at;
  at += size;
  return result;
}
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t Cursor::varint() {
  uint64_t value = 0;
  for (int i = 0;; ++i) {
    if (at == end) fail(sic::ErrorKind::invalid_format, "truncated varint");
    const uint8_t byte = *at++;
    if (i == 9 && byte > 1) fail(sic::ErrorKind::invalid_format, "varint overflow");
    value |= uint64_t(byte & 0x7f) << (7 * i);
    if (!(byte & 0x80)) {
      if (i > 0 && byte == 0) fail(sic::ErrorKind::invalid_format, "non-minimal varint");
      return value;
    }
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
IntCodec::IntCodec() { fmpz_init(magnitude_); }
}  // namespace sic::sicx

namespace sic::sicx {
IntCodec::~IntCodec() { fmpz_clear(magnitude_); }
}  // namespace sic::sicx

namespace sic::sicx {
void IntCodec::put(ByteBuffer& out, const fmpz_t value) {
  const int sign = fmpz_sgn(value);
  if (!sign) {
    out.push_back(0);
    return;
  }
  const uint64_t bits = fmpz_bits(value), size = (bits + 7) / 8, words = (bits + 63) / 64;
  put_varint(out, 2 * size + (sign < 0));
  fmpz_abs(magnitude_, value);
  limbs_.assign(words, 0);
  fmpz_get_ui_array(limbs_.data(), static_cast<slong>(words), magnitude_);
  if constexpr (std::endian::native == std::endian::little && sizeof(ulong) == 8) {
    const size_t offset = out.size();
    out.resize(offset + size);
    std::memcpy(out.data() + offset, limbs_.data(), size);
  } else
    for (uint64_t i = 0; i < size; ++i)
      out.push_back(static_cast<uint8_t>(limbs_[i / 8] >> (8 * (i % 8))));
}
}  // namespace sic::sicx

namespace sic::sicx {
void IntCodec::get(Cursor& in, fmpz_t value) {
  const uint64_t code = in.varint();
  if (code == 0) {
    fmpz_zero(value);
    return;
  }
  if (code == 1) fail(sic::ErrorKind::invalid_format, "negative zero integer");
  const uint64_t size = code >> 1;
  if (size > kMaxIntegerBytes) fail(sic::ErrorKind::resource, "integer exceeds reader limit");
  const uint8_t* bytes = in.take(size);
  if (bytes[size - 1] == 0) fail(sic::ErrorKind::invalid_format, "non-minimal integer magnitude");
  const uint64_t words = (size + 7) / 8;
  limbs_.assign(words, 0);
  if constexpr (std::endian::native == std::endian::little && sizeof(ulong) == 8)
    std::memcpy(limbs_.data(), bytes, size);
  else
    for (uint64_t i = 0; i < size; ++i) limbs_[i / 8] |= ulong(bytes[i]) << (8 * (i % 8));
  fmpz_set_ui_array(value, limbs_.data(), static_cast<slong>(words));
  if (code & 1) fmpz_neg(value, value);
}
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t checked_add(uint64_t a, uint64_t b) {
  if (b > UINT64_MAX - a) fail(sic::ErrorKind::invalid_format, "integer addition overflow");
  return a + b;
}
uint64_t checked_mul(uint64_t a, uint64_t b) {
  if (a && b > UINT64_MAX / a)
    fail(sic::ErrorKind::invalid_format, "integer multiplication overflow");
  return a * b;
}
}  // namespace sic::sicx
