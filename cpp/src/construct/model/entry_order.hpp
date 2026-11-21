// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <set>

#include "sic/sicx/format.hpp"
#include "support/index.hpp"
namespace sic::construct {
// Serialization/resume coordinate order comes solely from the general scalar
// rule, independent of the concrete dyadic construction used to obtain a tower.
struct EntryOrder {
  std::vector<long> representatives;
  std::vector<sic::support::Index> index;
  void derive(const sic::sicx::Meta& metadata);
};
}  // namespace sic::construct
