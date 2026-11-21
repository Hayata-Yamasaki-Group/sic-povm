// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/stages/table/class_schedule.hpp"
namespace sic::construct {
std::optional<ClassAllocation> class_allocation(int total, int maximum, int live, int available,
                                                int selection_limit) {
  if (total < 1 || maximum < 1 || live < 0 || available < 0 || selection_limit < 1)
    throw std::invalid_argument("invalid live class allocation");
  for (int count = maximum; count > live; --count) {
    const int share = std::min(std::max(1, total / count), selection_limit);
    if (share <= available && share * (count - live) <= available)
      return ClassAllocation{count, share, 0};
  }
  return {};
}
}  // namespace sic::construct
