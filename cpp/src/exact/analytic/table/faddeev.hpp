// SPDX-License-Identifier: AGPL-3.0-or-later
// Faddeev's noncompact quantum dilogarithm in the normalization of [RW]:
//   Phi_S(z; tau) = exp( int_{R + i eta} exp(2 nu w) / (4 sinh(b w) sinh(w / b) w) dw ),
//   b = sqrt(tau), nu = (z + (1 - tau)/2) / b, valid for -1 < z < tau, 0 < eta < pi min(b, 1/b).
// Other real z are reached through Phi_S(z + 1) = Phi_S(z) / (1 - e((z + 1)/tau)).
// Certified: acb_calc_integrate plus an explicit tail bound.
#pragma once
#include <flint/acb.h>
#include <flint/acb_calc.h>
#include <flint/arb.h>
#include <flint/arb_calc.h>

#include <cmath>
#include <stdexcept>

#include "exact/certification/bounds/quadrature_bounds.hpp"
#include "sic/support/flint_raii.hpp"

namespace sic::exact {

struct FaddeevParam {
  sic::support::flint::Arb b, binv, nu2;  // b, 1/b, 2 nu
};

int faddeev_integrand(acb_ptr out, const acb_t w, void* param, slong order, slong prec);

// Certified bound for both omitted tails. For r >= R > 0,
//   |f(±r + i eta)| <= exp(-kappa r) / (r c(R)),
// where c(R) = (1-exp(-2 b R))(1-exp(-2 R/b)). Since c(r) is increasing,
// integrating this and using 1/r <= 1/R gives
//   2 exp(-kappa R) / (kappa R c(R)).

// Phi_S(z; tau) for -1 < z < tau, computed by the integral (no shifts).
void faddeev_strip(acb_t res, const arb_t z, const arb_t tau, slong prec);

// e(x) = exp(2 pi i x) for real x
void acb_e(acb_t r, const arb_t x, slong prec);

// Phi_S(z; tau) for any real z (not a pole/zero), via shifts to the centre of the strip.
void faddeev(acb_t res, const arb_t z, const arb_t tau, slong prec);

}  // namespace sic::exact
