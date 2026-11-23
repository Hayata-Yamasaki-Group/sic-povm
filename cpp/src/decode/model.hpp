// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "sic/support/admission.hpp"
#include "sic/support/ball_vectors.hpp"
namespace sic::decode {
using sic::support::ComplexBallVector;
struct Class {
  uint64_t parent;
  unsigned index;
  // Roots belong to the level family until dual construction finishes.
  ComplexBallVector dual;
  Class(uint64_t p, unsigned i, unsigned degree);
  Class();  // filled later (parallel construction)
};
struct Tree {
  std::vector<unsigned> degrees;
  std::vector<uint64_t> width;
  slong prec;
  std::vector<std::vector<Class>> levels;  // levels[i] = classes of F_{i+1}
  uint64_t serial = 0;                     // identity for the class-group cache
  uint64_t count(unsigned level) const { return level ? levels.at(level - 1).size() : 1; }
};

inline constexpr unsigned kMaxDegree = 31;

// ------------------------------------------------------------ memory estimates
// Estimate of the bytes one acb occupies at precision `prec`: the struct plus two
// heap mantissas of at most floor(prec/64) + 1 limbs with 16 bytes of allocator overhead.
inline uint64_t acb_bytes(slong prec) {
  return sizeof(acb_struct) +
         2 * (16 + 8 * (static_cast<uint64_t>(std::max<slong>(prec, 64)) / 64 + 1));
}
inline uint64_t av_bytes(uint64_t n, slong prec) { return 48 + n * acb_bytes(prec); }

inline uint64_t request_metadata_bytes(uint64_t count, size_t rank) {
  using namespace sic::support::admission;
  return mul(count, add(256, mul(rank, 32)));
}

inline uint64_t request_numeric_bytes(uint64_t count, slong bits) {
  using sic::support::admission::mul;
  return mul(count, mul(3, acb_bytes(bits)));
}

}  // namespace sic::decode
