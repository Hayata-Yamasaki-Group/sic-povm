// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::norm {

// FIPS 180-4 SHA-256. Arithmetic is unsigned, including the length and padding.
class HashState {
  std::array<uint32_t, 8> state{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::array<uint8_t, 64> block{};
  size_t used = 0;
  uint64_t length = 0;

  void compress();

 public:
  void update(std::span<const uint8_t> bytes);

  Hash finish();
};

Hash digest(std::span<const uint8_t> bytes);

Hash tree_hash(const std::vector<ByteBuffer>& leaves, size_t first, size_t count);

Hash logical_digest(const Logical& l, std::span<const uint8_t> bytes);

std::string hash_hex(const Hash& hash);

Hash parse_hash(const std::string& text);
}  // namespace sic_check::norm
