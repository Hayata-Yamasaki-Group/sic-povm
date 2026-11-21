// SPDX-License-Identifier: AGPL-3.0-or-later
#include "assertions.hpp"
#include "exact/certification/reconstruction/ghost.hpp"

template <class Real>
Real reference_power_two(int exponent) {
  Real value = (Real)1;
  for (int k = 0; k < exponent; ++k) value *= (Real)2;
  for (int k = 0; k > exponent; --k) value = value / (Real)2;
  return value;
}
template <class Real>
void ordinary_range() {
  arf_t value;
  arf_init(value);
  for (int exponent : {-600, 600}) {
    arf_one(value);
    arf_mul_2exp_si(value, value, exponent);
    const Real native = sic::exact::selection_from_arf<Real>(value);
    SIC_CHECK(sic::exact::selection_finite(native));
    const Real expected = (Real)std::ldexp(1.0, exponent);
    SIC_CHECK((double)(native / expected) == 1.0);
  }
  arf_clear(value);
}
template <class Real>
void wide_range() {
  if constexpr (sic::exact::selection_max_exponent<Real>() > 2001) {
    arf_t value, extra;
    arf_init(value);
    arf_init(extra);
    for (int exponent : {-2000, -1200, 1200, 2000}) {
      arf_one(value);
      arf_mul_2exp_si(value, value, exponent);
      arf_one(extra);
      arf_mul_2exp_si(extra, extra, exponent - 100);
      arf_add(value, value, extra, ARF_PREC_EXACT, ARF_RND_NEAR);
      const Real native = sic::exact::selection_from_arf<Real>(value);
      const Real scale = reference_power_two<Real>(exponent);
      const Real expected = (Real)1 + (Real)std::ldexp(1.0, -100);
      SIC_CHECK(sic::exact::selection_finite(native));
      SIC_CHECK((double)((native / scale - expected) / expected) == 0.0);
    }
    for (int exponent : {-1200, 1200}) {
      const Real radicand = reference_power_two<Real>(exponent);
      const Real expected = reference_power_two<Real>(exponent / 2);
      const Real root = sic::exact::selection_sqrt(radicand);
      SIC_CHECK(sic::exact::selection_finite(root));
      SIC_CHECK((double)(root / expected) == 1.0);
    }
    arf_clear(value);
    arf_clear(extra);
  }
}
int main() {
  ordinary_range<double>();
  ordinary_range<long double>();
  ordinary_range<sic::exact::SelectionWide>();
  ordinary_range<sic::exact::SelectionFloat>();
  wide_range<long double>();
  wide_range<sic::exact::SelectionWide>();
  {
    arf_t value;
    arf_init(value);
    arf_one(value);
    arf_mul_2exp_si(value, value, 1200);
    SIC_CHECK(!sic::exact::selection_finite(sic::exact::selection_from_arf<double>(value)));
    arf_clear(value);
  }
  {
    // A known nonzero normalization with severe cancellation. High-precision
    // Jacobian arithmetic must retain it; low-precision behavior is logged.
    sic::exact::GhostRefiner refiner(8, 1, 1);
    sic::support::ComplexBallVector point(14), tiny(1);
    acb_set_ui(point[0], 3);
    acb_one(tiny[0]);
    acb_mul_2exp_si(tiny[0], tiny[0], -200);
    acb_sub_ui(point[7], tiny[0], 1, 512);
    acb_div_ui(point[7], point[7], 3, 512);
    refiner.selection_normalization_resolved(point.v, 64, true);
    SIC_CHECK(refiner.selection_normalization_resolved(point.v, 512, true));
    sic::exact::GhostRefiner::Ctx context(8, 1, 512);
    std::vector<long> equations{1, 2, 3, 4, 5, 6, 7};
    acb_mat_t matrix;
    acb_mat_init(matrix, equations.size(), 14);
    refiner.jacobian(matrix, context, point.v, equations);
    for (size_t r = 0; r < equations.size(); ++r)
      for (int c = 0; c < 14; ++c) SIC_CHECK(acb_is_finite(acb_mat_entry(matrix, r, c)));
    acb_mat_clear(matrix);
  }
  return sic_test::report();
}
