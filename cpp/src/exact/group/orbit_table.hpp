// SPDX-License-Identifier: AGPL-3.0-or-later
// Shared exact point-to-storage labels for epsilon-orbit table compression.
#pragma once
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

#include "support/index.hpp"

namespace sic::exact {

struct OrbitTableLayout {
  long grid, slots;
  sic::support::Index classes;
  const std::vector<sic::support::Index>& canonical;
  std::vector<sic::support::Index> index;
  std::vector<char> derived;
  std::vector<std::vector<sic::support::Index>> conjugate_index;
  long conjugation_label = -1;

  OrbitTableLayout(long d, sic::support::Index h, const std::vector<sic::support::Index>& orbit);

  void pair_classes(const std::vector<char>& mask, long label,
                    const std::function<sic::support::Index(sic::support::Index)>& conjugate);

  sic::support::Index encoded(long point, bool paired) const;
};

}  // namespace sic::exact
