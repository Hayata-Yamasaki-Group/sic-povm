// SPDX-License-Identifier: AGPL-3.0-or-later
#include "support/periodic_grid.hpp"

#include "sic/error.hpp"

namespace sic::support {
PeriodicGrid::PeriodicGrid(const std::vector<long>& dimensions, std::vector<long> lengths)
    : full(dimensions), periods(std::move(lengths)) {
  if (full.size() != periods.size()) throw std::invalid_argument("periodic grid rank mismatch");
  for (size_t l = 0; l < full.size(); ++l) {
    if (periods[l] <= 0 || full[l] <= 0 || full[l] % periods[l])
      throw std::invalid_argument("invalid periodic grid axis");
    stride.push_back(full_count);
    reduced_stride.push_back(count);
    const auto next = sic::support::resources::mul(count, periods[l]);
    const auto next_full = sic::support::resources::mul(full_count, full[l]);
    if (next > (sic::support::resources::Count)WORD_MAX ||
        next_full > (sic::support::resources::Count)WORD_MAX)
      throw sic::Error(sic::ErrorKind::resource, "periodic grid index capacity exceeded");
    count = next;
    full_count = next_full;
  }
}

long PeriodicGrid::lift(long index, bool frequency) const {
  if (index < 0 || index >= count) throw std::out_of_range("periodic grid index");
  long result = 0;
  for (size_t l = 0; l < full.size(); ++l) {
    const long digit = (index / reduced_stride[l]) % periods[l];
    result += digit * stride[l] * (frequency ? full[l] / periods[l] : 1);
  }
  return result;
}

}  // namespace sic::support
