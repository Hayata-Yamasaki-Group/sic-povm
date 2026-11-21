// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/bounds/height_bounds.hpp"

#include "sic/error.hpp"

namespace sic::exact::certified {
HeightBound::HeightBound() {
  mag_init(value_);
  mag_one(value_);
}

HeightBound::HeightBound(ulong n) {
  mag_init(value_);
  mag_set_ui(value_, n);
}

HeightBound::HeightBound(const HeightBound& b) {
  mag_init(value_);
  mag_set(value_, b.value_);
}

HeightBound HeightBound::absolute_integer(slong n) {
  fmpz_t z;
  fmpz_init_set_si(z, n);
  fmpz_abs(z, z);
  HeightBound b;
  mag_set_fmpz(b.value_, z);
  fmpz_clear(z);
  return b;
}

HeightBound HeightBound::binomial(ulong n, ulong k) {
  fmpz_t z;
  fmpz_init(z);
  fmpz_bin_uiui(z, n, k);
  HeightBound b;
  mag_set_fmpz(b.value_, z);
  fmpz_clear(z);
  return b;
}

slong HeightBound::upper_binary_exponent() const {
  if (mag_is_inf(value_) || mag_is_zero(value_))
    throw std::runtime_error("recognition height must be positive and finite");
  const double proposal = std::ceil(mag_get_d_log2_approx(value_));
  // Recognition still uses doubles to store this integer, and computes 2B+8.
  // Leave ample room for its subsequent signed-word arithmetic as well.
  const double limit =
      std::min(std::ldexp(1.0, 50), static_cast<double>(std::numeric_limits<slong>::max() / 16));
  if (!std::isfinite(proposal) || std::fabs(proposal) >= limit)
    throw sic::Error(sic::ErrorKind::unsupported,
                     "recognition height exponent exceeds supported range");
  slong e = static_cast<slong>(proposal);
  while (mag_cmp_2exp_si(value_, e) > 0) {
    if (static_cast<double>(e + 1) >= limit)
      throw sic::Error(sic::ErrorKind::unsupported,
                       "recognition height exponent exceeds supported range");
    ++e;
  }
  return e;
}

}  // namespace sic::exact::certified
