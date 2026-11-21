// SPDX-License-Identifier: AGPL-3.0-or-later
// Exact modular labels for the three visits to each reduced-cycle state.
#pragma once

#include <array>
#include <utility>
#include <vector>

namespace sic::exact {

struct GridPoint {
  long row, column;
  auto operator<=>(const GridPoint&) const = default;
};
struct GridMatrix {
  long a = 1, b = 0, c = 0, e = 1;

  static long residue(long x, long d) { return (x % d + d) % d; }

  static GridMatrix multiply(const GridMatrix& x, const GridMatrix& y, long d);

  static GridMatrix step(long digit, long d) { return {residue(digit, d), 1, d - 1, 0}; }

  GridPoint apply(long i, long j, long d) const {
    return {residue(a * i + b * j, d), residue(c * i + e * j, d)};
  }
};

GridMatrix cycle_matrix(const std::vector<long>& digits, long d);

inline std::array<GridMatrix, 3> cycle_visits(const GridMatrix& prefix, const GridMatrix& period,
                                              long d) {
  const auto second = GridMatrix::multiply(prefix, period, d);
  return {prefix, second, GridMatrix::multiply(second, period, d)};
}

}  // namespace sic::exact
