// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/storage/exact_leaf_codec.hpp"

#include "sic/error.hpp"

namespace sic::exact::leaf_codec {
size_t varint_size(Count value) {
  size_t count = 1;
  while (value >= 128) {
    value >>= 7;
    ++count;
  }
  return count;
}

void write_unsigned(FILE* file, Count value) {
  do {
    const unsigned char byte = (value & 127) | (value >= 128 ? 128 : 0);
    if (fputc(byte, file) == EOF) throw sic::Error(sic::ErrorKind::io, "binary leaf write failed");
    value >>= 7;
  } while (value);
}

Count read_unsigned(FILE* file) {
  Count value = 0;
  for (unsigned i = 0; i < 10; ++i) {
    const int byte = fgetc(file);
    if (byte == EOF)
      throw sic::Error(ferror(file) ? sic::ErrorKind::io : sic::ErrorKind::invalid_format,
                       "truncated binary leaf varint");
    if ((i == 9 && (byte & 254)) || (i > 0 && byte == 0))
      throw sic::Error(sic::ErrorKind::invalid_format,
                       "noncanonical or overflowing binary leaf varint");
    value |= Count(byte & 127) << (7 * i);
    if (!(byte & 128)) return value;
  }
  throw sic::Error(sic::ErrorKind::invalid_format, "overflowing binary leaf varint");
}

Count integer_code(const fmpz_t value) {
  const Count bytes = (fmpz_bits(value) + 7) / 8;
  return sic::support::resources::add(sic::support::resources::mul(bytes, 2),
                                      fmpz_sgn(value) < 0 ? 1 : 0);
}

void write_integer(FILE* file, const fmpz_t value) {
  const Count code = integer_code(value), bytes = code / 2;
  write_unsigned(file, code);
  if (!bytes) return;
  if (bytes <= sizeof(ulong)) {
    std::array<unsigned char, sizeof(ulong)> data{};
    ulong magnitude = fmpz_get_ui(value);
    for (Count q = 0; q < bytes; ++q) {
      data[bytes - q - 1] = magnitude & 255;
      magnitude >>= 8;
    }
    if (fwrite(data.data(), 1, bytes, file) != bytes)
      throw sic::Error(sic::ErrorKind::io, "binary small-integer write failed");
    return;
  }
  std::vector<unsigned char> data(bytes);
  std::vector<ulong> limbs((bytes + sizeof(ulong) - 1) / sizeof(ulong));
  fmpz_t magnitude;
  fmpz_init(magnitude);
  fmpz_abs(magnitude, value);
  fmpz_get_ui_array(limbs.data(), limbs.size(), magnitude);
  fmpz_clear(magnitude);
  for (Count q = 0; q < bytes; ++q) {
    const Count index = bytes - q - 1;
    data[q] = (limbs[index / sizeof(ulong)] >> (8 * (index % sizeof(ulong)))) & 255;
  }
  if (fwrite(data.data(), 1, bytes, file) != bytes)
    throw sic::Error(sic::ErrorKind::io, "binary leaf magnitude write failed");
}

void read_integer(FILE* file, fmpz_t value, Count& payload_budget) {
  const Count code = read_unsigned(file), bytes = code / 2;
  if (code == 1) throw sic::Error(sic::ErrorKind::invalid_format, "negative zero in binary leaf");
  if (bytes > payload_budget)
    throw sic::Error(sic::ErrorKind::resource,
                     "binary leaf payload exceeds reader workspace budget");
  payload_budget -= bytes;
  if (!bytes) {
    fmpz_zero(value);
    return;
  }
  if (bytes <= sizeof(ulong)) {
    std::array<unsigned char, sizeof(ulong)> data{};
    if (fread(data.data(), 1, bytes, file) != bytes)
      throw sic::Error(ferror(file) ? sic::ErrorKind::io : sic::ErrorKind::invalid_format,
                       "truncated binary small integer");
    if (!data[0])
      throw sic::Error(sic::ErrorKind::invalid_format, "leading zero in binary leaf magnitude");
    ulong magnitude = 0;
    for (Count q = 0; q < bytes; ++q) magnitude = (magnitude << 8) | data[q];
    fmpz_set_ui(value, magnitude);
    if (code & 1) fmpz_neg(value, value);
    return;
  }
  std::vector<unsigned char> data(bytes);
  if (fread(data.data(), 1, bytes, file) != bytes)
    throw sic::Error(ferror(file) ? sic::ErrorKind::io : sic::ErrorKind::invalid_format,
                     "truncated binary leaf magnitude");
  if (!data[0])
    throw sic::Error(sic::ErrorKind::invalid_format, "leading zero in binary leaf magnitude");
  std::vector<ulong> limbs((bytes + sizeof(ulong) - 1) / sizeof(ulong), 0);
  for (Count q = 0; q < bytes; ++q) {
    const Count index = bytes - q - 1;
    limbs[index / sizeof(ulong)] |= ulong(data[q]) << (8 * (index % sizeof(ulong)));
  }
  fmpz_set_ui_array(value, limbs.data(), limbs.size());
  if (code & 1) fmpz_neg(value, value);
}

}  // namespace sic::exact::leaf_codec
