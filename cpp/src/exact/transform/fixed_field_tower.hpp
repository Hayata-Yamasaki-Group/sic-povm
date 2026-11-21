// SPDX-License-Identifier: AGPL-3.0-or-later
// Exact scalar stabilizers for an optional real tower adapted to eta_m.
#pragma once

#include <algorithm>
#include <bit>
#include <map>
#include <numeric>
#include <unordered_set>

#include "exact/group/group.hpp"
#include "exact/storage/analytic_generator.hpp"

namespace sic::exact {

template <class Mask, class Multiply, class Allowed>
long quadratic_support_witness(long count, const Mask& support, const Mask& subgroup,
                               const Mask& preferred, Multiply&& multiply, Allowed&& allowed);

struct ScalarFixedField {
  std::vector<long> generators, members;
  long degree = 0;
  int level = -1;
};

}  // namespace sic::exact

#include "exact/transform/fixed_field_tower.tpp"
