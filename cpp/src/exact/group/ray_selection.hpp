// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "exact/group/group_checkpoint.hpp"
#include "sicx/codec/ray_list.hpp"

namespace sic::exact {

using sic::sicx::SmithWord;
void sort_ray_words(std::vector<SmithWord>& words);

GroupProjection dimension_two_projection();
sic::sicx::RayList select_rays(const GroupProjection& group, int threads,
                               const sic::support::resources::Budget& budget);
sic::sicx::RayList select_rays(const Group& group, int unit_power, int threads,
                               const sic::support::resources::Budget& budget);
int admit_ray_selection(const GroupProjection& group, int threads,
                        const sic::support::resources::Budget& budget);

class RaySelection {
 public:
  RaySelection(const GroupProjection& group, int threads);
  RaySelection(const GroupProjection& group, const Arithmetic& arithmetic, int unit_power,
               int threads);

  std::vector<SmithWord> derive() const;
  void check(const std::vector<SmithWord>& words) const;

 private:
  const GroupProjection& group_;
  std::vector<sic::support::Index> ordinary_;
  std::vector<std::vector<sic::support::Index>> translate_;
  std::vector<long> axis_orders_;
  size_t ordinary_count_ = 0;

  void prepare(const Arithmetic& arithmetic, int unit_power, int threads);
  std::vector<SmithWord> words_from_rows(const std::vector<long>& rows) const;
};

}  // namespace sic::exact
