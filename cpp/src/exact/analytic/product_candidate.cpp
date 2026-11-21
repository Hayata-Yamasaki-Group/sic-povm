// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/analytic/product_candidate.hpp"

namespace sic::exact {
ProductCandidate::ProductCandidate(int generators, int count, unsigned seed) : certified_height(1) {
  if (count < 0 || count > 4 || generators < 1)
    throw std::invalid_argument("product candidate supports zero to four factors");
  std::mt19937 rng(seed + 104729u);
  std::uniform_int_distribution<long> coefficient(1, 4);
  for (int k = 0; k < count; ++k) {
    std::vector<long> factor(generators + 1);
    ulong sum = 0;
    for (long& value : factor) {
      value = coefficient(rng);
      sum += value;
    }
    factors.push_back(std::move(factor));
    certified_height = certified_height * certified::HeightBound(sum);
    height *= sum;
  }
}

}  // namespace sic::exact
