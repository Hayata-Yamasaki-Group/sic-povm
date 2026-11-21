// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/newton/krawczyk_exclusion.hpp"

namespace sic::exact::krawczyk_exclusion {
Magnitudes::Magnitudes(long n) : values(n) {
  for (auto& value : values) mag_init(&value);
}

Magnitudes::~Magnitudes() {
  for (auto& value : values) mag_clear(&value);
}

long obstructed_row(const acb_mat_t inverse, const acb_mat_t jacobian, mag_srcptr rho) {
  const long n = acb_mat_nrows(inverse);
  if (n < 1 || acb_mat_ncols(inverse) != n || acb_mat_nrows(jacobian) != n ||
      acb_mat_ncols(jacobian) != n || !rho)
    throw std::invalid_argument("invalid radius-gate dimensions or radii");
  for (long r = 0; r < n; ++r) {
    if (!mag_is_finite(rho + r) || fmpz_bits(MAG_EXPREF(rho + r)) > FLINT_BITS / 2) return -1;
    for (long c = 0; c < n; ++c)
      if (!acb_is_finite(acb_mat_entry(inverse, r, c)) ||
          !acb_is_exact(acb_mat_entry(inverse, r, c)) ||
          !acb_is_finite(acb_mat_entry(jacobian, r, c)) ||
          !ARB_IS_LAGOM(acb_realref(acb_mat_entry(inverse, r, c))) ||
          !ARB_IS_LAGOM(acb_imagref(acb_mat_entry(inverse, r, c))) ||
          !ARB_IS_LAGOM(acb_realref(acb_mat_entry(jacobian, r, c))) ||
          !ARB_IS_LAGOM(acb_imagref(acb_mat_entry(jacobian, r, c))))
        return -1;
  }
  Magnitudes real(n), imaginary(n), temporary(5);
  auto product = temporary[0];
  for (long k = 0; k < n; ++k)
    for (long c = 0; c < n; ++c) {
      const auto value = acb_mat_entry(jacobian, k, c);
      mag_mul_lower(product, arb_radref(acb_realref(value)), rho + c);
      mag_add_lower(real[k], real[k], product);
      mag_mul_lower(product, arb_radref(acb_imagref(value)), rho + c);
      mag_add_lower(imaginary[k], imaginary[k], product);
    }
  auto real_sum = temporary[1], imaginary_sum = temporary[2];
  auto real_weight = temporary[3], imaginary_weight = temporary[4];
  for (long r = 0; r < n; ++r) {
    mag_zero(real_sum);
    mag_zero(imaginary_sum);
    for (long k = 0; k < n; ++k) {
      const auto value = acb_mat_entry(inverse, r, k);
      arf_get_mag_lower(real_weight, arb_midref(acb_realref(value)));
      arf_get_mag_lower(imaginary_weight, arb_midref(acb_imagref(value)));
      mag_mul_lower(product, real_weight, real[k]);
      mag_add_lower(real_sum, real_sum, product);
      mag_mul_lower(product, imaginary_weight, imaginary[k]);
      mag_add_lower(real_sum, real_sum, product);
      mag_mul_lower(product, real_weight, imaginary[k]);
      mag_add_lower(imaginary_sum, imaginary_sum, product);
      mag_mul_lower(product, imaginary_weight, real[k]);
      mag_add_lower(imaginary_sum, imaginary_sum, product);
    }
    mag_mul_2exp_si(real_sum, real_sum, 1);
    mag_mul_2exp_si(imaginary_sum, imaginary_sum, 1);
    if (mag_cmp(real_sum, rho + r) >= 0 || mag_cmp(imaginary_sum, rho + r) >= 0) return r;
  }
  return -1;
}

}  // namespace sic::exact::krawczyk_exclusion
