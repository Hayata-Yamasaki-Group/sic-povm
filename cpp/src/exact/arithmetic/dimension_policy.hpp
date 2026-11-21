// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <limits>

#include "sic/error.hpp"
namespace sic::exact {
// Default construction allowance. SICX permits d <= 2^20; native quadratic
// arithmetic must also have room for the discriminant and its signed guards.
struct DimensionPolicy {
  static constexpr unsigned kWireMaximumExponent = 20;
  unsigned maximum_exponent = 12;
  long checked(unsigned exponent) const {
    if (!maximum_exponent || maximum_exponent > kWireMaximumExponent || !exponent ||
        exponent > maximum_exponent || exponent >= (std::numeric_limits<long>::digits - 2) / 2)
      throw sic::Error(sic::ErrorKind::invalid_argument,
                       "dyadic exponent exceeds dimension policy");
    return long{1} << exponent;
  }
};
struct CanonicalProfile {
  static constexpr const char* kName = "sicx-dyadic/1.0";
  static bool maximal_order(unsigned exponent) { return exponent == 7; }
};
}  // namespace sic::exact
