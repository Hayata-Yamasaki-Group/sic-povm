// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/arb.h>

#include "sic/support/flint_raii.hpp"
namespace sic::exact {
void faddeev_tail_bound(arb_t bound, const arb_t kappa, const arb_t b, const arb_t binv,
                        const arb_t R, slong prec);
}
// Outward-rounded bounds for the Faddeev contour quadrature.
// Floating-point choices of panels and ellipse radii are proposals only.

#include <cmath>
#include <stdexcept>

namespace sic::exact::certified {

struct Ball {
  arb_t v;
  Ball();
  ~Ball() { arb_clear(v); }
  Ball(const Ball&) = delete;
  Ball& operator=(const Ball&) = delete;
};

struct ContourBounds {
  Ball b, binv, eta, U, kappa, pi;
  slong p;

  ContourBounds(const arb_t b_value, const arb_t eta_value, double umax, slong precision);

  // Supremum on the ellipse above the central panel [-eta, eta].
  bool central(arb_t M, double radius) const;

  // Supremum on either off-axis ellipse over [x0, 2*x0] or its reflection.
  bool off_axis(arb_t M, double x0, double radius) const;

  // A degree-(2n-1) Chebyshev truncation has sup error at most
  // 2 M rho^(-2n)/(1-rho^-1). Both integral and positive Gauss rule
  // have norm 2 on [-1,1]; scaling to length L gives this bound.
  void panel_error(mag_t error, const arb_t M, const arb_t length, double radius, ulong n) const;

  void tail_error(mag_t error, double endpoint) const;
};

}  // namespace sic::exact::certified
