// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/model/entry_order.hpp"

namespace sic::construct {
void EntryOrder::derive(const sic::sicx::Meta& metadata) {
  sic::support::admit_index_count(metadata.d);
  std::set<long> positions;
  for (uint64_t m = 0; m < metadata.d; ++m) positions.insert(metadata.entry(m).representative);
  representatives.assign(positions.begin(), positions.end());
  index.resize(metadata.d);
  for (uint64_t m = 0; m < metadata.d; ++m) {
    const auto entry = metadata.entry(m);
    index[m] = sic::support::checked_index(
        std::lower_bound(representatives.begin(), representatives.end(), entry.representative) -
        representatives.begin());
  }
}

}  // namespace sic::construct
