// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::norm {

uint64_t add_checked(uint64_t a, uint64_t b);

uint64_t mul_checked(uint64_t a, uint64_t b);

uint64_t little(std::span<const uint8_t> data, size_t pos, unsigned width = 8);

class Cursor {
  std::span<const uint8_t> b;
  size_t p = 0;

 public:
  explicit Cursor(std::span<const uint8_t> data);

  size_t position() const;

  uint64_t varint();

  Integer integer();

  void end() const;
};

void append_le(ByteBuffer& b, uint64_t v, unsigned n = 8);
}  // namespace sic_check::norm

#include "integer_codec.inl.hpp"
