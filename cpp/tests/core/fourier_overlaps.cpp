// SPDX-License-Identifier: AGPL-3.0-or-later
// Independent displacement sums and Fourier identities for the numerical API.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>

#include "assertions.hpp"
#include "sic/core/sic_frame.hpp"
#include "sic/core/verify.hpp"
#include "sic/core/weyl_heisenberg.hpp"

using sic::core::Complex;
using sic::core::Vector;

template <class Function>
static bool invalid_precondition(Function&& operation) {
  try {
    operation();
  } catch (const sic::Error& e) {
    return e.kind() == sic::ErrorKind::invalid_argument;
  }
  return false;
}

static Vector random_unit_vector(int d, std::mt19937_64& rng) {
  std::normal_distribution<double> normal;
  Vector v(static_cast<std::size_t>(d));
  double norm2 = 0;
  for (auto& z : v) {
    z = Complex(normal(rng), normal(rng));
    norm2 += std::norm(z);
  }
  for (auto& z : v) z /= std::sqrt(norm2);
  return v;
}

int main() {
  for (const auto& v : {Vector{}, Vector(4), Vector{std::numeric_limits<double>::quiet_NaN()},
                        Vector{std::numeric_limits<double>::infinity()}}) {
    SIC_CHECK(invalid_precondition([&] { sic::core::normalized_copy(v); }));
    SIC_CHECK(invalid_precondition([&] { sic::core::SicFrame frame(v); }));
  }
  std::mt19937_64 rng(71029);
  // Direct D_p application uses neither the row product formula nor the FFT.
  // Include odd/composite non-powers to exercise the direct fallback.
  for (int d : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 16, 32, 64, 128, 256}) {
    const sic::core::WeylHeisenberg wh(d);
    const auto psi = random_unit_vector(d, rng);
    const auto saved = psi;
    const auto all = wh.overlaps(psi);
    Vector row;
    SIC_CHECK(all.size() == static_cast<std::size_t>(d) * d);
    for (int a = 0; a < d; ++a) {
      wh.overlap_row(a, psi, row);
      SIC_CHECK(row.size() == psi.size());
      for (int b = 0; b < d; ++b) {
        const auto expected = wh.matrix_element(psi, a, b, psi);
        SIC_CHECK_NEAR(all[static_cast<std::size_t>(a) * d + b], expected, 2e-12);
        SIC_CHECK_NEAR(row[static_cast<std::size_t>(b)], expected, 2e-12);
      }
    }
    SIC_CHECK(psi == saved);
  }

  for (int d : {7, 8, 64}) {
    const sic::core::WeylHeisenberg wh(d);
    const auto psi = random_unit_vector(d, rng);
    Vector row;
    const std::int64_t shifts[] = {-17,
                                   -1,
                                   0,
                                   d - 1,
                                   d,
                                   d + 1,
                                   2 * d + 3,
                                   std::numeric_limits<std::int64_t>::min(),
                                   std::numeric_limits<std::int64_t>::max()};
    for (auto shift : shifts) {
      auto canonical = shift % (2 * static_cast<std::int64_t>(d));
      if (canonical < 0) canonical += 2 * d;
      wh.overlap_row(shift, psi, row);
      for (const auto second : shifts) {
        auto reduced_second = second % (2 * static_cast<std::int64_t>(d));
        if (reduced_second < 0) reduced_second += 2 * d;
        SIC_CHECK(wh.apply(shift, second, psi) == wh.apply(canonical, reduced_second, psi));
      }
      for (int b = 0; b < d; ++b)
        SIC_CHECK_NEAR(row[static_cast<std::size_t>(b)], wh.matrix_element(psi, canonical, b, psi),
                       2e-12);
    }
  }

  {
    constexpr int kD = 2048;
    const sic::core::WeylHeisenberg wh(kD);
    const auto psi = random_unit_vector(kD, rng);
    const auto all = wh.overlaps(psi);
    double total = 0;
    for (int a = 0; a < kD; ++a) {
      double spectrum_norm = 0, product_norm = 0;
      for (int b = 0; b < kD; ++b)
        spectrum_norm += std::norm(all[static_cast<std::size_t>(a) * kD + b]);
      for (int j = 0; j < kD; ++j)
        product_norm += std::norm(psi[static_cast<std::size_t>(j)]) *
                        std::norm(psi[static_cast<std::size_t>((j + a) % kD)]);
      SIC_CHECK_NEAR(spectrum_norm, kD * product_norm, 2e-12);
      total += spectrum_norm;
    }
    // Orthogonality of the displacement basis: sum_p |<psi|D_p|psi>|^2=d.
    SIC_CHECK_NEAR(total, static_cast<double>(kD), 2e-9);
    for (int a : {0, 1, 7, 129, 511, 1024, 2047})
      for (int b : {0, 1, 3, 73, 511, 1024, 2047})
        SIC_CHECK_NEAR(all[static_cast<std::size_t>(a) * kD + b], wh.matrix_element(psi, a, b, psi),
                       2e-12);
    // The streaming verifier must report the same normalized error maximum.
    double maximum = 0;
    for (std::size_t p = 1; p < all.size(); ++p)
      maximum = std::max(maximum, std::abs(std::norm(all[p]) - 1.0 / (kD + 1)));
    SIC_CHECK_NEAR(sic::core::verify_fiducial(psi).max_overlap_error, maximum, 2e-12);
  }

  {
    constexpr int kD = 2048, position = 713;
    const sic::core::WeylHeisenberg wh(kD);
    Vector basis(kD);
    basis[position] = Complex(0, 2);
    Vector row;
    // Complete analytic spectrum of a non-normalized basis ray, including
    // every label: zero off the zero-shift row, 4*omega^(b*position) on it.
    for (int a = 0; a < kD; ++a) {
      wh.overlap_row(a, basis, row);
      for (int b = 0; b < kD; ++b) {
        const Complex expected = a == 0 ? 4.0 * wh.omega_pow(b * position) : Complex(0);
        SIC_CHECK_NEAR(row[static_cast<std::size_t>(b)], expected, 2e-12);
      }
    }
  }

  {
    const sic::core::WeylHeisenberg wh(4);
    Vector psi(4), row;
    SIC_CHECK(invalid_precondition([&] { wh.overlap_row(0, Vector(3), row); }));
    SIC_CHECK(invalid_precondition([&] { wh.overlap_row(0, psi, psi); }));
    SIC_CHECK(invalid_precondition([&] { (void)wh.overlaps(Vector(3)); }));
  }
  {
    // Scaling must not underflow or overflow the norm-square accumulator.
    for (double scale : {1e-300, 1.0, 1e300}) {
      const Vector qutrit = {0.0, scale, -scale};
      const auto result = sic::core::verify_fiducial(qutrit);
      SIC_CHECK(result.is_fiducial(2e-12));
      SIC_CHECK_NEAR(result.input_norm / scale, std::sqrt(2.0), 1e-14);
      SIC_CHECK(!result.is_fiducial(std::numeric_limits<double>::infinity()));
      SIC_CHECK(!result.is_fiducial(-1));
    }
    SIC_CHECK(!sic::core::VerifyResult().is_fiducial(1e-12));
    SIC_CHECK(invalid_precondition([&] { (void)sic::core::verify_fiducial(Vector()); }));
    SIC_CHECK(invalid_precondition([&] { (void)sic::core::verify_fiducial(Vector(4)); }));
    for (double bad :
         {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
      SIC_CHECK(invalid_precondition([&] { (void)sic::core::verify_fiducial(Vector{bad, 1.0}); }));
      SIC_CHECK(invalid_precondition([&] {
        (void)sic::core::verify_fiducial(Vector{Complex(1.0, bad), 0.0});
      }));
    }
    SIC_CHECK(invalid_precondition(
        [&] { (void)sic::core::verify_fiducial(Vector(4, std::numeric_limits<double>::max())); }));
  }
  return sic_test::report();
}
