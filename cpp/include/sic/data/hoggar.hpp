// SPDX-License-Identifier: AGPL-3.0-or-later
// The Hoggar lines: 64 equiangular lines in C^8, covariant under the 3-qubit Pauli group (NOT under
// the Z/8 Weyl-Heisenberg group; they are not part of the 174-ray Weyl-Heisenberg catalogue).
//
// Exact fiducial (Gaussian integers, unnormalized):  v = (-1+2i, 1, 1, 1, 1, 1, 1, 1),  <v|v> = 12,
// psi = v / sqrt(12).  Self-contained: exact arithmetic in Z[i] with int64, no FLINT.
//
// Convention (docs/data.md#hoggar, Hoggar section): C^8 = (C^2)^{(x)3}, |x> = |x_1> (x) |x_2> (x)
// |x_3> with x = 4 x_1 + 2 x_2 + x_3, so tensor factor 1 is the MOST significant bit (same rule as
// sicx.md#basis section 1).
//   P_{a,b} = (X^{a_1} Z^{b_1}) (x) (X^{a_2} Z^{b_2}) (x) (X^{a_3} Z^{b_3}),  X|x> = |x+1 mod 2>,
//   Z|x> = (-1)^x |x>, P_{a,b} |x> = (-1)^{b.x} |x xor a>      (no extra phase; every P_{a,b} is a
//   real +-1 signed permutation).
// Packed labels: a = 4 a_1 + 2 a_2 + a_3 (so a is the integer whose bits are a_1 a_2 a_3), likewise
// b.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace sic::data::hoggar {

// ------------------------------------------------------------------ exact arithmetic in Z[i]
// Definitions are required for constexpr Gaussian-integer evaluation.
struct Gauss {
  std::int64_t re = 0, im = 0;
  friend constexpr Gauss operator+(Gauss x, Gauss y) { return {x.re + y.re, x.im + y.im}; }
  friend constexpr Gauss operator*(Gauss x, Gauss y) {
    return {x.re * y.re - x.im * y.im, x.re * y.im + x.im * y.re};
  }
  friend constexpr bool operator==(Gauss x, Gauss y) { return x.re == y.re && x.im == y.im; }
};
constexpr Gauss conj(Gauss x) { return {x.re, -x.im}; }
constexpr std::int64_t norm2(Gauss x) { return x.re * x.re + x.im * x.im; }

using Vec8 = std::array<Gauss, 8>;

constexpr Vec8 fiducial_unnormalized() {
  return {Gauss{-1, 2}, Gauss{1, 0}, Gauss{1, 0}, Gauss{1, 0},
          Gauss{1, 0},  Gauss{1, 0}, Gauss{1, 0}, Gauss{1, 0}};
}
inline constexpr std::int64_t kNormSquared = 12;  // <v|v>; the normalization is 1/sqrt(12)

// P_{a,b} v, with a, b in 0..7 packed as in the header comment (bit 2 = tensor factor 1).
constexpr Vec8 pauli_apply(unsigned a, unsigned b, const Vec8& v) {
  Vec8 w{};
  for (unsigned x = 0; x < 8; ++x) {
    const bool neg = std::popcount(b & x) & 1u;  // (-1)^{b.x}
    const Gauss s = v[x];
    w[x ^ a] = neg ? Gauss{-s.re, -s.im} : s;
  }
  return w;
}

constexpr Gauss inner(const Vec8& u, const Vec8& v) {  // <u|v> = sum conj(u_j) v_j, unnormalized
  Gauss s{};
  for (unsigned j = 0; j < 8; ++j) s = s + conj(u[j]) * v[j];
  return s;
}

struct Report {
  bool ok = false;
  std::int64_t norm_squared = 0;  // <v|v> (expected 12)
  int overlaps_checked = 0;       // non-identity (a,b), expected 63
  int pairs_checked = 0;          // unordered pairs of the 64 vectors P_{a,b} v, expected 2016
  std::string message;            // first failure, empty when ok
};

// Exact check: for psi = v/sqrt(N), N = <v|v>:  <psi|psi> = 1 iff N is the norm; |<psi|P psi>|^2 =
// |g|^2/N^2 = 1/9 iff 9 |g|^2 = N^2 where g = <v|P v> in Z[i].  All integer arithmetic, no
// rounding.
Report verify_exact(const Vec8& v);

// ------------------------------------------------------------------ exact decimal containment
// (verification only) Minimal unsigned big integers, base 1e9 little endian; only what the
// digit-by-digit square root needs.

// Exact containment test for a released high-precision ball: is |sqrt(num/den) - mid| <= rad ?  mid
// and rad are non-negative decimal strings "0.ddd" with the same number of decimals P (a leading
// "-" on mid is handled by the caller, who passes the magnitude).  Decided exactly: (mid-rad)^2 den
// <= num 10^{2P} <= (mid+rad)^2 den.
bool hp_contains_sqrt_ratio(const std::string& mid, const std::string& rad, std::uint32_t num,
                            std::uint32_t den);

}  // namespace sic::data::hoggar
