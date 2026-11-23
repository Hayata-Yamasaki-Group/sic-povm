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
struct QuadraticOrder {
  long discriminant;
  long conductor;
};

// The standard dyadic discriminant is odd and congruent to 5 modulo 8.
inline QuadraticOrder maximal_quadratic_order(long discriminant) {
  long conductor = 1;
  for (long p = 3; p <= discriminant / p; p += 2) {
    while (discriminant % (p * p) == 0) {
      discriminant /= p * p;
      conductor *= p;
    }
  }
  return {discriminant, conductor};
}
}  // namespace sic::exact
