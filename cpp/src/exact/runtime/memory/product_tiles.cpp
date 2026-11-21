// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/runtime/memory/product_tiles.hpp"

#include <limits>

namespace sic::exact::product_tiles {
long count(long dimension) {
  if (dimension <= 0) throw std::invalid_argument("invalid product tile dimension");
  const auto axis = 1 + (dimension - 1) / kSide;
  const auto jobs = sic::support::resources::mul(axis, axis);
  if (jobs > static_cast<unsigned long long>(std::numeric_limits<long>::max()))
    throw sic::support::resources::AdmissionError("product tile count exceeds task range");
  return static_cast<long>(jobs);
}

sic::support::resources::Bytes scratch(long dimension, int workers,
                                       sic::support::resources::BitCount storage_bits) {
  using namespace sic::support::resources;
  if (dimension <= 0 || workers <= 0) throw std::invalid_argument("invalid product tile workspace");
  if (dimension <= kSmallMatrix) return ball_product_scratch(dimension, workers, storage_bits);
  const auto active = std::min<long>(std::min(workers, kMaximumActive), count(dimension));
  const Count edge = std::min<long>(kSide, dimension);
  const Count panels = add(mul(2, mul(edge, dimension)), mul(2, mul(edge, edge)));
  // FLINT 3.6 acb_mat_mul_reorder holds two shallow real panels and two
  // result tiles. arb_mat_mul_block additionally packs bounded-height integer
  // panels/results, exponent/bit tables, and magnitude error matrices. Charge
  // four complex (eight real) precision-sized equivalents of all four shapes,
  // rather than only
  // scalar-dot scratch. newton_matrix_bits exceeds the inspected
  // block max_height (1.25*precision+192); the fallback uses scalar dots.
  // Include windows, native worker stacks, allocator and multiplication slack.
  const Bytes packed = mul(4, mul(panels, complex_bytes(newton_matrix_bits(storage_bits))));
  return mul(active, add(4 * kMib, packed));
}
}  // namespace sic::exact::product_tiles
