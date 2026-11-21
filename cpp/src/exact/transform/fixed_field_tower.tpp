// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::exact {
template <class Mask, class Multiply, class Allowed>
long quadratic_support_witness(long count, const Mask& support, const Mask& subgroup,
                               const Mask& preferred, Multiply&& multiply, Allowed&& allowed) {
  for (int pass = 0; pass < 2; ++pass)
    for (long c = 0; c < count; ++c)
      if ((pass || preferred[c]) && support[c] && !subgroup[c] && allowed(c) &&
          subgroup[multiply(c, c)])
        return c;
  return -1;
}

}  // namespace sic::exact
