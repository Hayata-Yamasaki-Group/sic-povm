// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "construct/model/session_state.hpp"
#include "exact/analytic/cyclotomic_trace.hpp"
#include "exact/arithmetic/arith.hpp"
namespace sic::construct {
inline acb_srcptr Construction::tval(const Table& table, long x) {
  return &table.val[x / (d * d)][x % (d * d)];
}

inline sic::support::Index Construction::coset(int i, long g) {
  if (i == t) return cos_top[g];
  if (cos_shift[i] >= 0)
    return static_cast<std::make_unsigned_t<sic::support::Index>>(cos_top[g]) >> cos_shift[i];
  return static_cast<sic::support::Index>(cos_top[g] / cos_scale[i]);
}

}  // namespace sic::construct
