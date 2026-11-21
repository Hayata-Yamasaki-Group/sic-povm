// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/selection/selection_arithmetic.hpp"

namespace sic::exact {
template <class Real>
bool selection_finite(Real value) {
  const Real infinity = (Real)std::numeric_limits<double>::infinity();
  return value < infinity && -value < infinity;
}

template <class Real>
Real selection_scale(Real value, slong exponent) {
  const Real up = (Real)std::ldexp(1.0, 512), down = (Real)std::ldexp(1.0, -512);
  while (exponent > 512) {
    value *= up;
    exponent -= 512;
  }
  while (exponent < -512) {
    value *= down;
    exponent += 512;
  }
  return value * (Real)std::ldexp(1.0, (int)exponent);
}

template <class Real>
Real selection_from_arf(const arf_t value) {
  if (arf_is_zero(value)) return (Real)0;
  if (!arf_is_finite(value)) return (Real)std::numeric_limits<double>::quiet_NaN();
  const slong exponent = arf_abs_bound_lt_2exp_si(value);
  if (exponent > selection_max_exponent<Real>())
    return (Real)(arf_sgn(value) < 0 ? -std::numeric_limits<double>::infinity()
                                     : std::numeric_limits<double>::infinity());
  if (exponent < selection_min_exponent<Real>() - 2) return (Real)0;
  arf_t normalized, residual;
  arf_init(normalized);
  arf_init(residual);
  arf_mul_2exp_si(normalized, value, -exponent);
  const double hi = arf_get_d(normalized, ARF_RND_NEAR);
  arf_set_d(residual, hi);
  arf_sub(residual, normalized, residual, ARF_PREC_EXACT, ARF_RND_DOWN);
  const double lo = arf_get_d(residual, ARF_RND_NEAR);
  arf_clear(normalized);
  arf_clear(residual);
  return selection_scale((Real)hi + (Real)lo, exponent);
}

template <class Real>
Real selection_sqrt(Real value) {
  if (!selection_finite(value)) return value;
  if (value <= (Real)0) return (Real)0;
  const Real up = (Real)std::ldexp(1.0, 512), down = (Real)std::ldexp(1.0, -512);
  Real normalized = value;
  slong exponent = 0;
  while (normalized > up) {
    normalized *= down;
    exponent += 256;
  }
  while (normalized < down) {
    normalized *= up;
    exponent -= 256;
  }
  Real root = (Real)std::sqrt((double)normalized);
  for (int iteration = 0; iteration < 3; ++iteration) root = (root + normalized / root) / (Real)2;
  return selection_scale(root, exponent);
}

}  // namespace sic::exact

namespace sic::exact {
template bool selection_finite<double>(double);
template double selection_scale<double>(double, slong);
template double selection_from_arf<double>(const arf_t);
template double selection_sqrt<double>(double);
template bool selection_finite<long double>(long double);
template long double selection_scale<long double>(long double, slong);
template long double selection_from_arf<long double>(const arf_t);
template long double selection_sqrt<long double>(long double);
template bool selection_finite<SelectionFloat>(SelectionFloat);
template SelectionFloat selection_scale<SelectionFloat>(SelectionFloat, slong);
template SelectionFloat selection_from_arf<SelectionFloat>(const arf_t);
template SelectionFloat selection_sqrt<SelectionFloat>(SelectionFloat);
#if defined(__SIZEOF_FLOAT128__)
template bool selection_finite<__float128>(__float128);
template __float128 selection_scale<__float128>(__float128, slong);
template __float128 selection_from_arf<__float128>(const arf_t);
template __float128 selection_sqrt<__float128>(__float128);
#endif
}  // namespace sic::exact
