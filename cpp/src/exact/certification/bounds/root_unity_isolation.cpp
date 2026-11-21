// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/bounds/root_unity_isolation.hpp"

namespace sic::exact {
RootUnityIsolation::RootUnityIsolation(ulong order, slong precision) : precision_(precision) {
  if (order < 2) throw std::invalid_argument("root isolation: order must be at least two");
  arb_init(separation_);
  mag_init(gap_);
  mag_init(distance_);
  acb_init(difference_);
  arb_const_pi(separation_, precision_);
  arb_div_ui(separation_, separation_, order, precision_);
  arb_sin(separation_, separation_, precision_);
  arb_mul_2exp_si(separation_, separation_, 1);
  arb_get_mag_lower(gap_, separation_);
}

RootUnityIsolation::~RootUnityIsolation() {
  arb_clear(separation_);
  mag_clear(gap_);
  mag_clear(distance_);
  acb_clear(difference_);
}

bool RootUnityIsolation::matches(const acb_t value, const acb_t candidate) {
  if (mag_is_zero(gap_) || !acb_is_finite(value) || !acb_is_finite(candidate) ||
      !acb_overlaps(value, candidate))
    return false;
  acb_sub(difference_, value, candidate, precision_);
  acb_get_mag(distance_, difference_);
  return mag_cmp(distance_, gap_) < 0;
}

}  // namespace sic::exact
