// SPDX-License-Identifier: AGPL-3.0-or-later
// A rectangular sufficient stabilizer for functions on Smith coordinates.
#pragma once

#include <algorithm>
#include <vector>

#include "support/resources.hpp"

namespace sic::support {

struct PeriodicGrid {
  std::vector<long> full, periods, stride, reduced_stride;
  long count = 1, full_count = 1;

  PeriodicGrid(const std::vector<long>& dimensions, std::vector<long> lengths);

  long element(long index) const { return lift(index, false); }
  long character(long index) const { return lift(index, true); }

 private:
  long lift(long index, bool frequency) const;
};

template <class Fixes>
std::vector<long> exact_axis_periods(const std::vector<long>& dimensions, Fixes&& fixes);

}  // namespace sic::support

#include "support/periodic_grid.tpp"
