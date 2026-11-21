// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Extended Clifford elements and Appleby unitary/antiunitary application.
// Checked caller preconditions throw sic::Error(invalid_argument): nonunit inverses,
// determinant mismatches, dimensions outside 1..min(maximum_dimension, 2^30), and vector sizes.
// Search and fingerprints live in sic/search/clifford.hpp.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "sic/core/fft.hpp"
#include "sic/core/sic_frame.hpp"
#include "sic/core/types.hpp"
#include "sic/limits.hpp"

namespace sic::core::clifford {

using Int = std::int64_t;

Int mod(Int x, Int m);
Int mulmod(Int x, Int y, Int m);
// Inverse of x modulo m; throws sic::Error(invalid_argument) if gcd(x, m) != 1.
Int inverse_mod(Int x, Int m);
Int dbar_of(Int d);

// [[a, b], [c, e]]; (F p) = (a p1 + b p2, c p1 + e p2).
struct Mat2 {
  Int a = 1, b = 0, c = 0, e = 1;
  friend bool operator==(const Mat2&, const Mat2&) = default;
};
Mat2 reduce(const Mat2& f, Int m);
Mat2 mul(const Mat2& x, const Mat2& y, Int m);
Int det(const Mat2& f, Int m);
// Inverse mod m of a matrix whose determinant is a unit.
Mat2 inverse(const Mat2& f, Int m);
Int trace(const Mat2& f, Int m);
std::array<Int, 2> apply(const Mat2& f, Int p1, Int p2, Int m);
inline constexpr Mat2 kJ{1, 0, 0, -1};
// Multiplicative order of f mod m (f invertible), at most limit; 0 if larger.
Int order(const Mat2& f, Int m, Int limit = 1 << 22);

// Lexicographically smallest (a, b, c, e) in [0, dbar) with F_lift = F mod d and
// det F_lift = sign mod dbar. Requires 1 <= d <= 2^30, sign = +1 or -1, and det F = sign mod d;
// invalid inputs throw sic::Error(invalid_argument).
Mat2 canonical_lift(const Mat2& f_mod_d, Int d, Int sign);

Int gcd(Int x, Int y);

// U_F as a product: complex conjugation K first when det F = -1 (then F -> F J), followed by
// det-1 factors with a unit upper-right entry, applied in list order (U_F = U_last ... U_first
// [K]).
struct Factorization {
  bool conjugate = false;
  std::vector<Mat2> factors;
};
Factorization factorize(const Mat2& f_in, Int d);

// Applies Appleby's U_F (det F = +1) or U_F = U_(FJ) K (det F = -1), F mod dbar.
// O(d log d) per factor for power-of-two d. Immutable after construction; calls use local scratch.
class CliffordAction {
 public:
  static Int checked_dimension(Int d, uint64_t maximum_dimension);
  explicit CliffordAction(Int d, uint64_t maximum_dimension = sic::kDefaultCoreDimension);
  Int dim() const { return d_; }
  Int dbar() const { return db_; }

  void apply(const Mat2& f_in, const Vector& v, Vector& out) const;

 private:
  Int d_, db_;
  Dft dft_;
  std::vector<Complex> roots_;  // exp(pi i m / d), m < 2d

  // tau^k = exp(pi i k (d + 1) / d)
  Complex tau_pow(Int k) const {
    return roots_[static_cast<std::size_t>(mulmod(mod(k, 2 * d_), d_ + 1, 2 * d_))];
  }
  // out_u = d^(-1/2) tau^(b' e u^2) sum_v omega^(-b' u v) [tau^(b' a v^2) v_v].
  void unit_b(const Mat2& f, const Vector& v, Vector& out) const;
};

struct CliffordElement {
  Mat2 F;        // in ESL(2, Z_dbar), canonical lift; det +1 unitary, -1 antiunitary
  Int sign = 1;  // det F mod dbar as +1 / -1
  Int chi1 = 0, chi2 = 0;
  Real z_deviation = 0;  // max_p |z_psi(p) - z_phi(F^(-1) p)|
  Real residual = 0;     // 1 - |<psi|D_chi U_F phi>| (normalized binary64 vectors)
  bool antiunitary() const;
};

// Applies U = D_chi U_F to phi (for checks and for generating Clifford images).
Vector apply_element(const CliffordElement& el, const Vector& phi);

std::string to_string(const Mat2& f);

}  // namespace sic::core::clifford
