// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/limits.hpp"
namespace sic {
long checked_precision(long target, long guard, long ceiling) {
  if (target < 1 || target > kNativeMaximumPrecision || guard < 0 || ceiling < 64 ||
      ceiling > kNativeMaximumPrecision)
    throw Error(ErrorKind::invalid_argument, "invalid precision policy");
  if (target > ceiling || guard > ceiling - target)
    throw Error(ErrorKind::precision, "target plus guard exceeds precision policy");
  return target + guard;
}
}  // namespace sic
