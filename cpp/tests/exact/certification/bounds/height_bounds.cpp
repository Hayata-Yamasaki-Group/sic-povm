// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/bounds/height_bounds.hpp"

#include "assertions.hpp"

using sic::exact::certified::HeightBound;

// Compare the actual mag value to an exact integer, rather than comparing
// nearest-double logarithms or rounding the magnitude to an integer first.
static bool encloses_integer(const HeightBound& b, const fmpz_t z) {
  arf_t upper, exact;
  arf_init(upper);
  arf_init(exact);
  arf_set_mag(upper, b.get());
  arf_set_fmpz(exact, z);
  const bool ok = arf_cmp(upper, exact) >= 0;
  arf_clear(upper);
  arf_clear(exact);
  return ok;
}

int main() {
  fmpz_t z;
  fmpz_init(z);
  for (ulong n : {1UL, 2UL, 3UL, 1024UL, 10485760UL}) {
    const HeightBound b(n);
    SIC_CHECK(mag_cmp_2exp_si(b.get(), b.upper_binary_exponent()) <= 0);
  }
  // FLINT may inflate even a power-of-two mag slightly. The exponent must
  // enclose it; minimality is not required for the recognition certificate.
  SIC_CHECK(HeightBound(1).upper_binary_exponent() >= 0);
  SIC_CHECK(HeightBound(1024).upper_binary_exponent() >= 10);
  const HeightBound huge = HeightBound(2).pow(100000);
  fmpz_one(z);
  fmpz_mul_2exp(z, z, 100000);
  SIC_CHECK(encloses_integer(huge, z));
  SIC_CHECK(mag_cmp_2exp_si(huge.get(), huge.upper_binary_exponent()) <= 0);

  for (ulong n : {5UL, 127UL, 1000UL}) {
    const ulong k = n / 2;
    const HeightBound b = HeightBound::binomial(n, k) * HeightBound(37).pow(n - k);
    fmpz_bin_uiui(z, n, k);
    fmpz_t power;
    fmpz_init_set_ui(power, 37);
    fmpz_pow_ui(power, power, n - k);
    fmpz_mul(z, z, power);
    SIC_CHECK(encloses_integer(b, z));
    SIC_CHECK(mag_cmp_2exp_si(b.get(), b.upper_binary_exponent()) <= 0);
    fmpz_clear(power);
  }

  // A generic candidate may include LONG_MIN; never negate it in machine words.
  fmpz_set_si(z, std::numeric_limits<slong>::min());
  fmpz_abs(z, z);
  SIC_CHECK(encloses_integer(HeightBound::absolute_integer(std::numeric_limits<slong>::min()), z));
  const HeightBound generic =
      HeightBound::absolute_integer(-17) + HeightBound::absolute_integer(29);
  fmpz_set_ui(z, 46);
  SIC_CHECK(encloses_integer(generic, z));

  arb_t x;
  arb_init(x);
  arb_sqrt_ui(x, 1025, 128);
  arb_add_ui(x, x, 1024, 128);
  const HeightBound eta = HeightBound::absolute_ball(x);
  // The exact d+sqrt(d+1) value lies above d+32 and below d+33.
  fmpz_set_ui(z, 1056);
  SIC_CHECK(encloses_integer(eta, z));
  SIC_CHECK(mag_cmp_2exp_si(eta.get(), eta.upper_binary_exponent()) <= 0);
  HeightBound copy = eta;
  copy = generic;
  SIC_CHECK(mag_cmp(copy.get(), generic.get()) == 0);
  SIC_CHECK(mag_cmp(eta.get(), generic.get()) > 0);

  bool rejected = false;
  try {
    (void)HeightBound(0).upper_binary_exponent();
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  SIC_CHECK(rejected);
  arb_indeterminate(x);
  rejected = false;
  try {
    (void)HeightBound::absolute_ball(x).upper_binary_exponent();
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  SIC_CHECK(rejected);
  arb_clear(x);
  fmpz_clear(z);
  return sic_test::report();
}
