// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "exact/group/group_checkpoint.hpp"

namespace sic::exact {

using SmithWord = std::vector<long>;

class RaySelection {
 public:
  RaySelection(const GroupProjection& group, int threads);

  std::vector<SmithWord> derive() const;
  void check(const std::vector<SmithWord>& words) const;

 private:
  const GroupProjection& group_;
  std::vector<sic::support::Index> ordinary_;
  std::vector<std::vector<sic::support::Index>> translate_;
  std::vector<long> axis_orders_;
  size_t ordinary_count_ = 0;

  std::vector<SmithWord> words_from_rows(const std::vector<long>& rows) const;
};

}  // namespace sic::exact
