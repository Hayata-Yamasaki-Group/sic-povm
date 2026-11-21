// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/acb_poly.h>

#include "sicx/exact/tower.hpp"
namespace sic::sicx::exact {
struct ComplexBall {
  acb_t v;
  ComplexBall();
  ~ComplexBall();
  ComplexBall(const ComplexBall& o);
  ComplexBall(ComplexBall&& o) noexcept;
  ComplexBall& operator=(const ComplexBall& o);
  ComplexBall& operator=(ComplexBall&& o) noexcept;
};

void acb_from_ke(acb_t out, const QuadraticElement& x, const acb_t omega, slong prec);

// Monomial values g^e (e in [0, N[n])) at the roots r[0..n-1].
std::vector<ComplexBall> monomials(const Tower& T, unsigned n,
                                   const std::vector<ComplexBall>& roots, slong prec);
void evaluate(acb_t out, const Elt& x, const std::vector<ComplexBall>& mono, const acb_t omega,
              slong prec);

}  // namespace sic::sicx::exact
