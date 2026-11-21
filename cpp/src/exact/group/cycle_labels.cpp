// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/group/cycle_labels.hpp"

namespace sic::exact {
GridMatrix GridMatrix::multiply(const GridMatrix& x, const GridMatrix& y, long d) {
  return {residue(x.a * y.a + x.b * y.c, d), residue(x.a * y.b + x.b * y.e, d),
          residue(x.c * y.a + x.e * y.c, d), residue(x.c * y.b + x.e * y.e, d)};
}

GridMatrix cycle_matrix(const std::vector<long>& digits, long d) {
  GridMatrix result;
  for (long digit : digits) result = GridMatrix::multiply(GridMatrix::step(digit, d), result, d);
  return result;
}

}  // namespace sic::exact
