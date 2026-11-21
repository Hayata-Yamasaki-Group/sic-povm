// SPDX-License-Identifier: AGPL-3.0-or-later
#include "input_model.hpp"

namespace sic_check::norm {
Integer::Integer() { fmpz_init(v); }

Integer::Integer(const Integer& x) : Integer() { fmpz_set(v, x.v); }

Integer::Integer(Integer&& x) noexcept : Integer() { fmpz_swap(v, x.v); }

Integer& Integer::operator=(const Integer& x) {
  fmpz_set(v, x.v);
  return *this;
}

Integer::~Integer() { fmpz_clear(v); }

ComplexBall::ComplexBall() { acb_init(v); }

ComplexBall::ComplexBall(const ComplexBall& z) : ComplexBall() { acb_set(v, z.v); }

ComplexBall::ComplexBall(ComplexBall&& z) noexcept : ComplexBall() { acb_swap(v, z.v); }

ComplexBall& ComplexBall::operator=(const ComplexBall& z) {
  acb_set(v, z.v);
  return *this;
}

ComplexBall::~ComplexBall() { acb_clear(v); }
}  // namespace sic_check::norm

namespace sic_check::norm {
Integer::operator fmpz*() { return v; }
Integer::operator const fmpz*() const { return v; }
fmpz* Integer::operator->() { return v; }
const fmpz* Integer::operator->() const { return v; }
}  // namespace sic_check::norm

namespace sic_check::norm {
ComplexBall::operator acb_struct*() { return v; }
ComplexBall::operator const acb_struct*() const { return v; }
acb_struct* ComplexBall::operator->() { return v; }
const acb_struct* ComplexBall::operator->() const { return v; }
}  // namespace sic_check::norm

namespace sic_check::norm {
RealBall::RealBall() { arb_init(v); }
RealBall::~RealBall() { arb_clear(v); }
}  // namespace sic_check::norm

namespace sic_check::norm {
RealBall::operator arb_struct*() { return v; }
RealBall::operator const arb_struct*() const { return v; }
arb_struct* RealBall::operator->() { return v; }
const arb_struct* RealBall::operator->() const { return v; }
}  // namespace sic_check::norm

namespace sic_check::norm {
ComplexPolynomial::ComplexPolynomial() { acb_poly_init(v); }
ComplexPolynomial::~ComplexPolynomial() { acb_poly_clear(v); }
}  // namespace sic_check::norm

namespace sic_check::norm {
ComplexPolynomial::operator acb_poly_struct*() { return v; }
ComplexPolynomial::operator const acb_poly_struct*() const { return v; }
acb_poly_struct* ComplexPolynomial::operator->() { return v; }
const acb_poly_struct* ComplexPolynomial::operator->() const { return v; }
}  // namespace sic_check::norm
