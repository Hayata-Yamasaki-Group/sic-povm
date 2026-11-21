// SPDX-License-Identifier: AGPL-3.0-or-later
// Range-safe native arithmetic for heuristic subsystem selection only.
// Mathematical acceptance still requires the subsequent Arb certificates.
#pragma once

#include <flint/arf.h>

#include <cfloat>
#include <cmath>
#include <limits>
#include <type_traits>

#include "selection_float.hpp"

namespace sic::exact {

template <class Real>
constexpr int selection_max_exponent() {
  if constexpr (std::is_same_v<Real, long double>) return LDBL_MAX_EXP;
#if defined(__SIZEOF_FLOAT128__)
  else if constexpr (std::is_same_v<Real, __float128>)
    return __FLT128_MAX_EXP__;
#endif
  else
    return DBL_MAX_EXP;
}
template <class Real>
constexpr int selection_min_exponent() {
  if constexpr (std::is_same_v<Real, long double>) return LDBL_MIN_EXP - LDBL_MANT_DIG;
#if defined(__SIZEOF_FLOAT128__)
  else if constexpr (std::is_same_v<Real, __float128>)
    return __FLT128_MIN_EXP__ - __FLT128_MANT_DIG__;
#endif
  else
    return DBL_MIN_EXP - DBL_MANT_DIG;
}
template <class Real>
bool selection_finite(Real value);
template <class Real>
Real selection_scale(Real value, slong exponent);
template <class Real>
Real selection_from_arf(const arf_t value);
template <class Real>
Real selection_sqrt(Real value);

}  // namespace sic::exact
