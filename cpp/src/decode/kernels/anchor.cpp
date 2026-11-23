// SPDX-License-Identifier: AGPL-3.0-or-later
#include "decode/kernels/anchor.hpp"

#include <optional>

namespace sic::decode {
namespace {
using sic::support::flint::Arf;

void squared_bounds(Arf& lower, Arf& upper, arb_srcptr ball, slong bits) {
  arb_get_interval_arf(lower, upper, ball, bits);
  if (arf_sgn(upper) < 0) {
    arf_swap(lower, upper);
  } else if (arf_sgn(lower) <= 0) {
    if (arf_cmpabs(lower, upper) > 0) arf_set(upper, lower);
    arf_zero(lower);
  }
  arf_mul(lower, lower, lower, bits, ARF_RND_FLOOR);
  arf_mul(upper, upper, upper, bits, ARF_RND_CEIL);
}
}  // namespace

size_t select_anchor(std::span<const acb_struct> raw, slong bits) {
  std::optional<size_t> best;
  Arf best_upper, real_lower, real_upper, imag_lower, imag_upper, lower;
  for (size_t k = 0; k < raw.size(); ++k) {
    if (!acb_is_finite(&raw[k]))
      throw sic::Error(sic::ErrorKind::precision, "nonfinite raw coordinate in anchor selection");
    squared_bounds(real_lower, real_upper, acb_realref(&raw[k]), bits);
    squared_bounds(imag_lower, imag_upper, acb_imagref(&raw[k]), bits);
    arf_add(lower, real_lower, imag_lower, bits, ARF_RND_FLOOR);
    if (arf_sgn(lower) > 0 && (!best || arf_cmp(lower, best_upper) > 0)) {
      best = k;
      arf_add(best_upper, real_upper, imag_upper, bits, ARF_RND_CEIL);
    }
  }
  if (!best)
    throw sic::Error(sic::ErrorKind::precision,
                     "automatic anchor refused: no certified nonzero raw coordinate");
  return *best;
}
}  // namespace sic::decode
