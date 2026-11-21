// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/group/orbit_table.hpp"

#include "sic/error.hpp"

namespace sic::exact {
OrbitTableLayout::OrbitTableLayout(long d, sic::support::Index h,
                                   const std::vector<sic::support::Index>& orbit)
    : grid(d * d),
      slots((grid - 1) / 3),
      classes(h),
      canonical(orbit),
      index((sic::support::admit_index_count(orbit.size()), orbit.size()), 0),
      derived(h, 0),
      conjugate_index(h) {
  if (orbit.size() != (size_t)h * grid || !slots)
    throw std::invalid_argument("epsilon table layout dimensions");
  for (sic::support::Index c = 0; c < h; ++c) {
    long rank = 0;
    for (long q = 1; q < grid; ++q) {
      const long point = c * grid + q;
      if (orbit[point] == point) index[point] = sic::support::checked_index(c * slots + rank++ + 1);
    }
    if (rank != slots)
      throw sic::Error(sic::ErrorKind::construction,
                       "nonzero epsilon orbits do not all have size three");
  }
  for (size_t point = 0; point < orbit.size(); ++point) {
    if (orbit[point] < 0 || (size_t)orbit[point] >= orbit.size())
      throw std::runtime_error("epsilon orbit index outside grid");
    index[point] = index[orbit[point]];
  }
}

void OrbitTableLayout::pair_classes(
    const std::vector<char>& mask, long label,
    const std::function<sic::support::Index(sic::support::Index)>& conjugate) {
  if (mask.size() != (size_t)classes) throw std::invalid_argument("table pairing mask");
  if (conjugation_label == label && derived == mask) return;
  derived = mask;
  conjugate_index.assign(classes, {});
  for (sic::support::Index c = 0; c < classes; ++c)
    if (mask[c]) {
      auto& mapping = conjugate_index[c];
      mapping.resize(grid);
      for (long q = 0; q < grid; ++q) {
        const sic::support::Index target = conjugate(canonical[c * grid + q]);
        if (target < 0 || (size_t)target >= index.size() || mask[target / grid])
          throw std::runtime_error("derived class does not map to a stored class");
        mapping[q] = index[target];
      }
    }
  conjugation_label = label;
}

sic::support::Index OrbitTableLayout::encoded(long point, bool paired) const {
  if (point < 0 || (size_t)point >= index.size())
    throw std::out_of_range("table point outside layout");
  const long c = point / grid;
  return paired && derived[c] ? -conjugate_index[c][point % grid] : index[point];
}

}  // namespace sic::exact
