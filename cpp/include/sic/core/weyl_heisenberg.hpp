// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Weyl–Heisenberg group in dimension d, in Appleby's convention:
//
//   omega = exp(2 pi i / d),  tau = -exp(pi i / d)  (tau^2 = omega),
//   X|j> = |j+1>,  Z|j> = omega^j |j>,
//   D_p = tau^(p1 p2) X^(p1) Z^(p2),   p = (p1, p2) in Z^2.
//
// D_p depends only on p mod dbar, where dbar = d (d odd) or 2d (d even).

#include <cstdint>
#include <vector>

#include "sic/core/fft.hpp"
#include "sic/core/types.hpp"

namespace sic::core {

// Invalid dimension, vector sizes and checked output aliases throw sic::Error(invalid_argument).
class WeylHeisenberg {
 public:
  explicit WeylHeisenberg(int d);

  int dim() const { return d_; }
  int dbar() const { return (d_ % 2 == 0) ? 2 * d_ : d_; }

  // tau^k and omega^k for any integer k (exact index reduction, table lookup).
  Complex tau_pow(std::int64_t k) const;
  Complex omega_pow(std::int64_t k) const;
  Complex root_2d(std::int64_t m) const;  // exp(pi i m / d)

  // Returns D_p v.
  Vector apply(std::int64_t p1, std::int64_t p2, const Vector& v) const;

  // Raw displacement; input and output must not overlap. O(d).
  void displace(std::int64_t p1, std::int64_t p2, const Complex* v, Complex* out) const;

  // Returns <u| D_p |v>.
  Complex matrix_element(const Vector& u, std::int64_t p1, std::int64_t p2, const Vector& v) const;

  // One row <psi|D_(p1,p2)|psi>, p2=0..d-1. Resizes and reuses out;
  // out must be a different Vector object from psi. Integer p1 is not reduced
  // modulo d in the phase, so the even-dimensional displacement signs remain.
  // O(d log d) for power-of-two d; direct O(d^2) otherwise. O(d) storage.
  void overlap_row(std::int64_t p1, const Vector& psi, Vector& out) const;

  // out[p2] = <D_(p1,p2) psi|phi>, for p2 = 0..d-1.
  void cross_overlap_row(std::int64_t p1, const Vector& psi, const Vector& phi, Vector& out) const;

  // All overlaps <psi| D_p |psi> for p in {0..d-1}^2, stored at index p1*d + p2.
  // O(d^2 log d) for power-of-two d; direct O(d^3) otherwise. The returned
  // array takes O(d^2) storage; use overlap_row for streaming consumers.
  // These double-precision values are numerical diagnostics, not certificates.
  std::vector<Complex> overlaps(const Vector& psi) const;

 private:
  int d_;
  std::vector<Complex> roots_2d_;  // roots_2d_[m] = exp(pi i m / d), m = 0..2d-1
  Dft dft_;

  std::int64_t mod_d(std::int64_t a) const;
  void apply_tau_ramp(std::int64_t p1, Complex* out) const;
  void overlap_row_impl(std::int64_t p1, const Vector& psi, Complex* out) const;
};

}  // namespace sic::core
