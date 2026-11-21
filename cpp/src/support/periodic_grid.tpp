// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::support {
template <class Fixes>
std::vector<long> exact_axis_periods(const std::vector<long>& dimensions, Fixes&& fixes) {
  std::vector<long> periods;
  for (size_t l = 0; l < dimensions.size(); ++l) {
    const long n = dimensions[l];
    std::vector<long> divisors;
    for (long a = 1; a <= n / a; ++a)
      if (n % a == 0) {
        divisors.push_back(a);
        if (a != n / a) divisors.push_back(n / a);
      }
    std::sort(divisors.begin(), divisors.end());
    const auto found =
        std::find_if(divisors.begin(), divisors.end(), [&](long p) { return fixes(l, p); });
    if (found == divisors.end())
      throw std::runtime_error("group identity does not fix support generator");
    periods.push_back(*found);
  }
  return periods;
}

}  // namespace sic::support
