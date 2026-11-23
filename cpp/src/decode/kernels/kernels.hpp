// SPDX-License-Identifier: AGPL-3.0-or-later
// Bounded private kernels for prime-degree transported trace towers.
#pragma once

#include <flint/acb_poly.h>

#include <utility>

#include "sic/sicx/format.hpp"
#include "sic/support/ball_vectors.hpp"
#include "support/resources.hpp"

namespace sic::decode::kernels {

using Count = sic::support::resources::Count;

struct PrecisionRefusal : sic::Error {
  explicit PrecisionRefusal(const std::string& message);
};

void precision(slong bits);

void bounded(acb_srcptr value, slong bits);

class Polynomial {
  acb_poly_t value_;

 public:
  Polynomial();
  ~Polynomial() { acb_poly_clear(value_); }
  Polynomial(const Polynomial&) = delete;
  Polynomial& operator=(const Polynomial&) = delete;
  acb_poly_struct* get() { return value_; }
  const acb_poly_struct* get() const { return value_; }
};

unsigned monic_degree(const Polynomial& polynomial, slong bits);

// PRECONDITION: roots came from successful isolate for THIS polynomial, and
// the target has an independent exact root-identity contract for the same
// polynomial. For transport this is the authenticated exact action relation;
// for a base path it is the authenticated authoritative branch identity.
// Unique interval overlap alone does not prove either precondition, and this
// numerical kernel cannot validate a caller's algebraic provenance contract.
void select(acb_ptr output, const sic::support::ComplexBallVector& roots, acb_srcptr target,
            bool authoritative_containment, slong bits);

}  // namespace sic::decode::kernels

namespace sic::decode {
using sic::support::ComplexBallVector;
using namespace sic::sicx;

using sic::decode::kernels::PrecisionRefusal;

sic::support::ComplexBallVector isolate_roots(const sic::decode::kernels::Polynomial& polynomial,
                                              slong bits, slong iterations);
// Euler trace duals q_k(r)/f'(r), f(X)/(X - r) = sum q_k X^k.
sic::support::ComplexBallVector euler_duals(const sic::decode::kernels::Polynomial& polynomial,
                                            acb_srcptr root, slong bits);
// tau(v) = (sum_k (delta a_k) v^k) / delta, all at the SAME old embedding.
void transport_odd_value(acb_ptr out, const sic::support::ComplexBallVector& delta_a, unsigned p,
                         acb_srcptr delta, acb_srcptr old_root, slong bits);

// Leaf value lambda = x + y * iota(omega), with the integral basis derived from d.
// For D0 = 1 use omega = 0; for D0 != 1 mod 4 use omega = -sqrt(D0).
// (docs/sicx.md section 2), rounded to `prec`.
//  * D0 > 1: real; integral-basis conversion preserves its arithmetic operation order.
//  * D0 < 0: sqrt(D0) = i sqrt(|D0|), iota(omega) = 1/2 - i sqrt(|D0|)/2, so
//    Re lambda = x + y/2 (exact) and Im lambda = -y sqrt(|D0|)/2 (d = 2: D0 = -3).
class LeafConverter {
  arb_t w_, a_;
  slong prec_, cp_;
  bool imaginary_, half_basis_;

 public:
  LeafConverter(int64_t D0, slong prec, uint64_t max_bits);
  ~LeafConverter() {
    arb_clear(w_);
    arb_clear(a_);
  }
  LeafConverter(const LeafConverter&) = delete;
  LeafConverter& operator=(const LeafConverter&) = delete;
  void operator()(acb_t out, const fmpz_t x, const fmpz_t y);
};

// Normalized, gauged vector with a given anchor.
sic::support::ComplexBallVector normalize(std::span<const acb_struct> v, uint64_t anchor,
                                          slong prec);
inline sic::support::ComplexBallVector normalize(const sic::support::ComplexBallVector& v,
                                                 uint64_t anchor, slong prec) {
  return normalize(v.span(), anchor, prec);
}

}  // namespace sic::decode

#include "decode/kernels/kernels.inl.hpp"
