// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::verifier {

// ------------------------------------------------------------------------------------------------
// SHA-256
struct Sha256 {
  uint32_t h[8];
  Uint8 buf[64];
  Uint64 len = 0;
  size_t bl = 0;

  Sha256();

  void reset();

  static uint32_t ror(uint32_t x, int n);

  void block(const Uint8* p);

  void update(const void* data, size_t n);

  void final(Uint8 out[32]);
};

Hash sha(const void* p, size_t n);

string hex(const Uint8* p, size_t n);

string hex(const Hash& h);
}  // namespace sic_check::verifier
