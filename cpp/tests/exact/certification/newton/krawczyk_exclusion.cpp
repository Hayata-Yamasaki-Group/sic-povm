#include "exact/certification/newton/krawczyk_exclusion.hpp"

#include <random>

#include "assertions.hpp"
#include "sic/support/flint_raii.hpp"

using Matrix = sic::support::flint::AcbMat;

bool full_test_fails(const acb_mat_t inverse, const acb_mat_t jacobian, mag_srcptr rho, long bits) {
  const long n = acb_mat_nrows(inverse);
  Matrix product(n, n);
  acb_mat_mul(product.get(), inverse, jacobian, bits);
  sic::exact::krawczyk_exclusion::Magnitudes scratch(3);
  for (long r = 0; r < n; ++r) {
    mag_zero(scratch[0]);
    for (long c = 0; c < n; ++c) {
      auto entry = acb_mat_entry(product.get(), r, c);
      if (r == c) acb_sub_ui(entry, entry, 1, bits);
      acb_get_mag(scratch[1], entry);
      mag_mul(scratch[2], scratch[1], rho + c);
      mag_add(scratch[0], scratch[0], scratch[2]);
    }
    mag_mul_2exp_si(scratch[0], scratch[0], 1);
    if (mag_cmp(scratch[0], rho + r) >= 0) return true;
  }
  return false;
}

int main() {
  using namespace sic::exact::krawczyk_exclusion;
  Matrix inverse(2, 2), jacobian(2, 2);
  Magnitudes rho(2);
  mag_one(rho[0]);
  mag_one(rho[1]);
  acb_mat_one(inverse.get());
  acb_mat_one(jacobian.get());
  SIC_CHECK(obstructed_row(inverse.get(), jacobian.get(), rho.values.data()) == -1);
  SIC_CHECK(!full_test_fails(inverse.get(), jacobian.get(), rho.values.data(), 256));
  arb_add_error_2exp_si(acb_realref(acb_mat_entry(jacobian.get(), 0, 0)), -1);
  SIC_CHECK(obstructed_row(inverse.get(), jacobian.get(), rho.values.data()) == 0);
  SIC_CHECK(full_test_fails(inverse.get(), jacobian.get(), rho.values.data(), 256));
  // Exercise imaginary weights and both rectangular radius channels.
  for (int channel = 0; channel < 2; ++channel) {
    acb_mat_zero(inverse.get());
    acb_mat_zero(jacobian.get());
    for (long r = 0; r < 2; ++r) {
      arb_one(acb_imagref(acb_mat_entry(inverse.get(), r, r)));
      arb_set_si(acb_imagref(acb_mat_entry(jacobian.get(), r, r)), -1);
      auto value = acb_mat_entry(jacobian.get(), r, r);
      arb_add_error_2exp_si(channel ? acb_imagref(value) : acb_realref(value), -1);
    }
    SIC_CHECK(obstructed_row(inverse.get(), jacobian.get(), rho.values.data()) == 0);
    SIC_CHECK(full_test_fails(inverse.get(), jacobian.get(), rho.values.data(), 256));
  }
  // Exact high-precision inverse pair: rounding the Jacobian alone can
  // make the low-precision product's radius incapable of passing.
  acb_mat_one(inverse.get());
  acb_mat_one(jacobian.get());
  auto large = acb_mat_entry(inverse.get(), 0, 1);
  acb_one(large);
  acb_mul_2exp_si(large, large, 600);
  acb_t tiny;
  acb_init(tiny);
  acb_one(tiny);
  acb_mul_2exp_si(tiny, tiny, -100);
  acb_add(large, large, tiny, 774);
  acb_clear(tiny);
  acb_neg(acb_mat_entry(jacobian.get(), 0, 1), large);
  Matrix low(2, 2);
  for (long r = 0; r < 2; ++r)
    for (long c = 0; c < 2; ++c)
      acb_set_round(acb_mat_entry(low.get(), r, c), acb_mat_entry(jacobian.get(), r, c), 423);
  SIC_CHECK(obstructed_row(inverse.get(), low.get(), rho.values.data()) == 0);
  SIC_CHECK(full_test_fails(inverse.get(), low.get(), rho.values.data(), 423));
  SIC_CHECK(obstructed_row(inverse.get(), jacobian.get(), rho.values.data()) == -1);
  SIC_CHECK(!full_test_fails(inverse.get(), jacobian.get(), rho.values.data(), 774));
  // Strongly anisotropic boxes and exponents outside floating-point range.
  acb_mat_one(inverse.get());
  acb_mat_one(jacobian.get());
  mag_set_ui_2exp_si(rho[1], 1, 100000);
  arb_add_error_2exp_si(acb_realref(acb_mat_entry(jacobian.get(), 0, 1)), -99999);
  SIC_CHECK(obstructed_row(inverse.get(), jacobian.get(), rho.values.data()) == 0);
  SIC_CHECK(full_test_fails(inverse.get(), jacobian.get(), rho.values.data(), 256));
  mag_one(rho[1]);
  // No obstruction is not an acceptance certificate.
  acb_mat_zero(inverse.get());
  acb_mat_zero(jacobian.get());
  SIC_CHECK(obstructed_row(inverse.get(), jacobian.get(), rho.values.data()) == -1);
  SIC_CHECK(full_test_fails(inverse.get(), jacobian.get(), rho.values.data(), 256));
  // Refuse unsupported/nonfinite inputs without inventing a rejection.
  acb_mat_one(inverse.get());
  acb_mat_one(jacobian.get());
  arb_add_error_2exp_si(acb_realref(acb_mat_entry(inverse.get(), 0, 0)), -10);
  SIC_CHECK(obstructed_row(inverse.get(), jacobian.get(), rho.values.data()) == -1);
  acb_indeterminate(acb_mat_entry(inverse.get(), 0, 0));
  SIC_CHECK(obstructed_row(inverse.get(), jacobian.get(), rho.values.data()) == -1);
  bool invalid = false;
  try {
    obstructed_row(inverse.get(), jacobian.get(), nullptr);
  } catch (const std::invalid_argument&) {
    invalid = true;
  }
  SIC_CHECK(invalid);
  acb_mat_one(inverse.get());
  mag_set_ui_2exp_si(rho[0], 1, 1L << 40);
  SIC_CHECK(obstructed_row(inverse.get(), jacobian.get(), rho.values.data()) == -1);
  mag_one(rho[0]);
  acb_mul_2exp_si(acb_mat_entry(inverse.get(), 0, 0), acb_mat_entry(inverse.get(), 0, 0), 1L << 40);
  SIC_CHECK(obstructed_row(inverse.get(), jacobian.get(), rho.values.data()) == -1);
  acb_mat_one(inverse.get());
  acb_indeterminate(acb_mat_entry(jacobian.get(), 0, 0));
  SIC_CHECK(obstructed_row(inverse.get(), jacobian.get(), rho.values.data()) == -1);
  acb_mat_one(jacobian.get());
  mag_zero(rho[0]);
  SIC_CHECK(obstructed_row(inverse.get(), jacobian.get(), rho.values.data()) == 0);
  SIC_CHECK(full_test_fails(inverse.get(), jacobian.get(), rho.values.data(), 256));
  mag_one(rho[0]);
  // Compare the obstruction with actual full ball multiplication over
  // randomized complex point matrices, radii, scales and box shapes.
  Matrix random_inverse(3, 3), random_jacobian(3, 3);
  Magnitudes random_rho(3);
  std::mt19937 random(7);
  int obstructions = 0;
  for (int trial = 0; trial < 128; ++trial) {
    for (long r = 0; r < 3; ++r) {
      mag_set_ui_2exp_si(random_rho[r], 1, (int)(random() % 41) - 20);
      for (long c = 0; c < 3; ++c) {
        auto y = acb_mat_entry(random_inverse.get(), r, c);
        auto j = acb_mat_entry(random_jacobian.get(), r, c);
        acb_set_si(y, (int)(random() % 17) - 8);
        arb_set_si(acb_imagref(y), (int)(random() % 17) - 8);
        acb_mul_2exp_si(y, y, (int)(random() % 61) - 30);
        acb_set_si(j, (int)(random() % 17) - 8);
        arb_set_si(acb_imagref(j), (int)(random() % 17) - 8);
        arb_add_error_2exp_si(acb_realref(j), (int)(random() % 101) - 70);
        arb_add_error_2exp_si(acb_imagref(j), (int)(random() % 101) - 70);
      }
    }
    if (obstructed_row(random_inverse.get(), random_jacobian.get(), random_rho.values.data()) >=
        0) {
      ++obstructions;
      SIC_CHECK(full_test_fails(random_inverse.get(), random_jacobian.get(),
                                random_rho.values.data(), 256));
    }
  }
  SIC_CHECK(obstructions > 100);
  return sic_test::report();
}
