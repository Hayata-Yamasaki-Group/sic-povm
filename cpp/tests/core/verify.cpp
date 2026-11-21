// SPDX-License-Identifier: AGPL-3.0-or-later
// Tests of the SIC verifier on known small fiducials, and of vector I/O.

#include "sic/core/verify.hpp"

#include <cmath>
#include <numbers>
#include <sstream>

#include "assertions.hpp"
#include "sic/core/fiducial_io.hpp"

using sic::core::Complex;
using sic::core::Vector;

int main() {
  const double tol = 1e-12;

  // d = 2: tetrahedral fiducial, Bloch vector (1,1,1)/sqrt(3).
  {
    const double theta = std::acos(1.0 / std::sqrt(3.0));
    const Vector psi = {std::cos(theta / 2), std::polar(std::sin(theta / 2), std::numbers::pi / 4)};
    const auto r = sic::core::verify_fiducial(psi);
    SIC_CHECK(r.is_fiducial(tol));
  }
  // d = 3: (0, 1, -1)/sqrt(2); normalization is done by the verifier.
  {
    const Vector psi = {0.0, 1.0, -1.0};
    const auto r = sic::core::verify_fiducial(psi);
    SIC_CHECK(r.is_fiducial(tol));
    SIC_CHECK_NEAR(r.input_norm, std::sqrt(2.0), tol);
  }
  // Negative tests: basis vectors are never fiducials.
  for (int d = 2; d <= 8; ++d) {
    Vector e(d, 0);
    e[0] = 1;
    SIC_CHECK(!sic::core::verify_fiducial(e).is_fiducial(1e-3));
  }
  // I/O round trip.
  {
    const Vector v = {Complex(0.25, -1.5), Complex(1e-17, 3), Complex(-2, 0)};
    std::stringstream ss;
    sic::core::write_vector(ss, v);
    std::stringstream with_comments("# header\n" + ss.str() + "# trailer\n");
    const Vector w = sic::core::read_vector(with_comments);
    SIC_CHECK(w.size() == v.size());
    for (std::size_t k = 0; k < v.size(); ++k) SIC_CHECK(w[k] == v[k]);
  }
  return sic_test::report();
}
