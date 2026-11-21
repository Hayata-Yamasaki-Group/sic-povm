// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/core/verify.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "core/numeric/verify.hpp"
#include "sic/core/weyl_heisenberg.hpp"

namespace sic::core {

namespace detail {
int checked_verify_dimension(std::size_t size) {
  if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw sic::Error(ErrorKind::invalid_argument, "verify_fiducial: unsupported dimension");
  return static_cast<int>(size);
}
}  // namespace detail

VerifyResult verify_fiducial(const Vector& psi_in) {
  VerifyResult r;
  r.d = detail::checked_verify_dimension(psi_in.size());
  if (r.d < 1) throw sic::Error(ErrorKind::invalid_argument, "verify_fiducial: empty vector");

  Real scale = 0;
  for (const Complex& z : psi_in) {
    if (!std::isfinite(z.real()) || !std::isfinite(z.imag()))
      throw sic::Error(ErrorKind::invalid_argument, "verify_fiducial: nonfinite vector");
    scale = std::max({scale, std::abs(z.real()), std::abs(z.imag())});
  }
  if (scale == 0) throw sic::Error(ErrorKind::invalid_argument, "verify_fiducial: zero vector");
  Real scaled_norm2 = 0;
  for (const Complex& z : psi_in) scaled_norm2 += std::norm(z / scale);
  const Real scaled_norm = std::sqrt(scaled_norm2);
  r.input_norm = scale * scaled_norm;
  if (!std::isfinite(r.input_norm))
    throw sic::Error(ErrorKind::invalid_argument,
                     "verify_fiducial: input norm is not representable");

  Vector psi(psi_in);
  for (Complex& z : psi) z = (z / scale) / scaled_norm;

  const WeylHeisenberg wh(r.d);
  Vector row;
  const Real target = Real(1) / (static_cast<Real>(r.d) + 1);
  for (int p1 = 0; p1 < r.d; ++p1) {
    wh.overlap_row(p1, psi, row);
    for (int p2 = 0; p2 < r.d; ++p2) {
      if (p1 == 0 && p2 == 0) continue;
      const Real err = std::abs(std::norm(row[static_cast<std::size_t>(p2)]) - target);
      if (!std::isfinite(err)) throw std::runtime_error("verify_fiducial: nonfinite overlap");
      if (err > r.max_overlap_error) {
        r.max_overlap_error = err;
        r.worst_p1 = p1;
        r.worst_p2 = p2;
      }
    }
  }
  return r;
}

}  // namespace sic::core
