// SPDX-License-Identifier: AGPL-3.0-or-later
#include "assertions.hpp"
#include "exact/certification/reconstruction/ghost.hpp"

static void compare_proposal(long d, int chi, long n, long k, slong q) {
  const long count = 2 * d - 2;
  sic::exact::GhostRefiner refiner(d, chi, n);
  refiner.k = k;
  refiner.S.resize(count);
  std::iota(refiner.S.begin(), refiner.S.end(), 1);
  sic::exact::GhostRefiner::Ctx high(d, chi, 1024), context(high, q);
  sic::support::ComplexBallVector point(count), F(count), original_F(count), original_point(count),
      expected, actual;
  for (long row = 0; row < count; ++row) {
    acb_set_si(point[row], row % 7 + 1);
    arb_set_si(acb_imagref(point[row]), row % 5 - 2);
    acb_mul_2exp_si(point[row], point[row], -8);
    acb_set_si(F[row], row % 11 - 5);
    arb_set_si(acb_imagref(F[row]), row % 3 - 1);
    acb_mul_2exp_si(F[row], F[row], -5);
    arb_add_error_2exp_si(acb_realref(F[row]), -900);
    acb_set(original_F[row], F[row]);
    acb_set(original_point[row], point[row]);
  }
  acb_mat_t inverse, jacobian;
  acb_mat_init(inverse, count, count);
  acb_mat_init(jacobian, count, count);
  for (long row = 0; row < count; ++row) {
    acb_one(acb_mat_entry(inverse, row, row));
    acb_mul_2exp_si(acb_mat_entry(inverse, row, row), acb_mat_entry(inverse, row, row), -16);
  }
  refiner.kernel_threads = 1;
  refiner.jacobian(jacobian, context, point.v, refiner.S);
  acb_mat_get_mid(jacobian, jacobian);
  const auto available = sic::support::resources::linear_correction_workspace(d, q, 4);
  for (int passes : {1, 2}) {
    SIC_CHECK(sic::exact::inner_refinement::propose(
        expected, inverse, jacobian, F, point, q, passes, 1,
        sic::exact::inner_refinement::scratch_bytes(count, q, 1)));
    refiner.kernel_threads = 4;
    SIC_CHECK(
        refiner.refined_correction(actual, inverse, F.v, point.v, high, q, passes, available));
    for (long row = 0; row < count; ++row) {
      SIC_CHECK(acb_equal(actual[row], expected[row]));
      SIC_CHECK(acb_equal(F[row], original_F[row]));
      SIC_CHECK(acb_equal(point[row], original_point[row]));
    }
  }
  sic::support::ComplexBallVector old_F(F), old_point(point), delta(count), once(point), initial;
  SIC_CHECK(sic::exact::inner_refinement::propose(
      initial, inverse, jacobian, F, point, q, 0, 4,
      sic::exact::inner_refinement::scratch_bytes(count, q, 4)));
  const double old_stat = refiner.correction(inverse, old_F.v, old_point.v, delta.v, 768, q);
  const double new_stat = refiner.commit_correction(initial, once.v, 768);
  SIC_CHECK(old_stat == new_stat);
  for (long row = 0; row < count; ++row) SIC_CHECK(acb_equal(once[row], old_point[row]));
  actual = sic::support::ComplexBallVector(1);
  acb_set_si(actual[0], 791);
  SIC_CHECK(!refiner.refined_correction(actual, inverse, F.v, point.v, high, q, 2, 1));
  SIC_CHECK(actual.size() == 1 && acb_equal_si(actual[0], 791));
  acb_mat_clear(inverse);
  acb_mat_clear(jacobian);
}

static void directional_derivative() {
  constexpr long kD = 8, count = 2 * kD - 2;
  sic::exact::GhostRefiner refiner(kD, -1, 3);
  refiner.k = kD - 1;
  refiner.S.resize(count);
  refiner.Kexp.assign(kD * kD, 0);
  std::iota(refiner.S.begin(), refiner.S.end(), 1);
  sic::exact::GhostRefiner::Ctx high(kD, -1, 1024), context(high, 768);
  sic::support::ComplexBallVector point(count), direction(count), shifted(count), F(count),
      next(count), work(3);
  acb_mat_t jacobian;
  acb_mat_init(jacobian, count, count);
  acb_one(work[0]);
  arb_one(acb_imagref(work[0]));
  acb_mul_2exp_si(work[0], work[0], -160);
  for (long row = 0; row < count; ++row) {
    acb_set_si(point[row], row % 5 + 1);
    acb_mul_2exp_si(point[row], point[row], -7);
    acb_set_si(direction[row], row % 7 - 3);
    arb_set_si(acb_imagref(direction[row]), row % 3 - 1);
    acb_mul(work[1], work[0], direction[row], 768);
    acb_add(shifted[row], point[row], work[1], 768);
  }
  refiner.residual(F.v, context, point.v);
  refiner.residual(next.v, context, shifted.v);
  refiner.jacobian(jacobian, context, point.v, refiner.S);
  mag_t error, tolerance;
  mag_init(error);
  mag_init(tolerance);
  mag_set_ui_2exp_si(tolerance, 1, -100);
  for (long row = 0; row < count; ++row) {
    acb_sub(work[1], next[row], F[row], 768);
    acb_div(work[1], work[1], work[0], 768);
    acb_dot(work[2], nullptr, 0, acb_mat_entry(jacobian, row, 0), 1, direction.v.data(), 1, count,
            768);
    acb_sub(work[1], work[1], work[2], 768);
    acb_get_mag(error, work[1]);
    SIC_CHECK(mag_cmp(error, tolerance) < 0);
  }
  // The normalization 1 + v_l*u_l can be exactly zero even for finite inputs.
  refiner.k = 0;
  for (long row = 0; row < count; ++row) acb_zero(point[row]);
  acb_one(point[0]);
  acb_set_si(point[kD - 1], -1);
  sic::support::ComplexBallVector output(1);
  acb_set_si(output[0], 31);
  SIC_CHECK(!refiner.refined_correction(
      output, jacobian, F.v, point.v, high, 512, 2,
      sic::support::resources::linear_correction_workspace(kD, 512, 1)));
  SIC_CHECK(output.size() == 1 && acb_equal_si(output[0], 31));
  mag_clear(error);
  mag_clear(tolerance);
  acb_mat_clear(jacobian);
}

int main() {
  flint_set_num_threads(4);
  for (long d : {8, 16})
    for (int chi : {-1, 1})
      for (long n : {1L, 2 * d - 1})
        for (long k : {0L, d - 1})
          for (slong q : {128, 512}) compare_proposal(d, chi, n, k, q);
  compare_proposal(64, -1, 3, 17, 256);
  directional_derivative();
  flint_cleanup_master();
  return sic_test::report();
}
