// SPDX-License-Identifier: AGPL-3.0-or-later
// Tests of the Weyl–Heisenberg group conventions.

#include "sic/core/weyl_heisenberg.hpp"

#include <cstdint>
#include <random>

#include "assertions.hpp"

using sic::core::Complex;
using sic::core::Vector;

static Vector random_vector(int d, std::mt19937_64& rng) {
  std::normal_distribution<double> g;
  Vector v(d);
  for (auto& z : v) z = Complex(g(rng), g(rng));
  return v;
}

static double dist(const Vector& a, const Vector& b) {
  double s = 0;
  for (std::size_t k = 0; k < a.size(); ++k) s += std::norm(a[k] - b[k]);
  return std::sqrt(s);
}

int main() {
  std::mt19937_64 rng(12345);
  const double tol = 1e-10;

  for (int d = 2; d <= 9; ++d) {
    const sic::core::WeylHeisenberg wh(d);
    SIC_CHECK_NEAR(wh.tau_pow(2), wh.omega_pow(1), tol);        // tau^2 = omega
    SIC_CHECK_NEAR(wh.tau_pow(wh.dbar()), Complex(1, 0), tol);  // tau has order dbar
    if (d % 2 == 0) SIC_CHECK_NEAR(wh.tau_pow(d), Complex(-1, 0), tol);

    const Vector v = random_vector(d, rng);
    const Vector u = random_vector(d, rng);
    const std::int64_t ps[] = {-7, -3, -1, 0, 1, 2, 5, 11};
    for (std::int64_t p1 : ps)
      for (std::int64_t p2 : ps) {
        // D_p depends only on p mod dbar
        SIC_CHECK(dist(wh.apply(p1, p2, v), wh.apply(p1 + wh.dbar(), p2, v)) < tol);
        SIC_CHECK(dist(wh.apply(p1, p2, v), wh.apply(p1, p2 - wh.dbar(), v)) < tol);
        // D_p^dagger = D_{-p}:  <u|D_p v> = <D_{-p} u|v>
        SIC_CHECK_NEAR(wh.matrix_element(u, p1, p2, v),
                       std::conj(wh.matrix_element(v, -p1, -p2, u)), tol * d);
        for (std::int64_t q1 : {-2, 0, 3})
          for (std::int64_t q2 : {-1, 1, 4}) {
            // D_p D_q = tau^<p,q> D_{p+q},  <p,q> = p2 q1 - p1 q2
            Vector lhs = wh.apply(p1, p2, wh.apply(q1, q2, v));
            Vector rhs = wh.apply(p1 + q1, p2 + q2, v);
            const Complex ph = wh.tau_pow(p2 * q1 - p1 * q2);
            for (auto& z : rhs) z *= ph;
            SIC_CHECK(dist(lhs, rhs) < tol * d);
          }
      }

    // tr(D_p^dagger D_q) = d delta_{p,q} for p, q in {0..d-1}^2
    for (int p1 = 0; p1 < d; ++p1)
      for (int p2 = 0; p2 < d; ++p2)
        for (int q1 = 0; q1 < d; ++q1)
          for (int q2 = 0; q2 < d; ++q2) {
            Complex tr = 0;
            for (int j = 0; j < d; ++j) {
              Vector e(d, 0);
              e[j] = 1;
              tr += wh.matrix_element(wh.apply(p1, p2, e), q1, q2, e);  // <D_p e_j | D_q e_j>
            }
            const double expected = (p1 == q1 && p2 == q2) ? d : 0;
            SIC_CHECK_NEAR(tr, Complex(expected, 0), tol * d);
          }

    // overlaps() agrees with matrix_element()
    const auto ov = wh.overlaps(v);
    for (int p1 = 0; p1 < d; ++p1)
      for (int p2 = 0; p2 < d; ++p2)
        SIC_CHECK_NEAR(ov[static_cast<std::size_t>(p1) * d + p2], wh.matrix_element(v, p1, p2, v),
                       1e-9 * d);
  }
  return sic_test::report();
}
