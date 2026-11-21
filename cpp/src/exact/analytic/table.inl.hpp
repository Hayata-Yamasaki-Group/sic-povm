// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::exact {
inline void Table::state_factor(acb_t res, sic::support::Index sid, long x, long y, slong p) const {
  const long id = (x - 1) * d + y;
  acb_exp(res, &sd[sid].L[id], p);
  acb_mul(res, res, &sd[sid].S[id], p);
}

}  // namespace sic::exact
