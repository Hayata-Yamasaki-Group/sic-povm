// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/certification/bounds/quadrature_bounds.hpp"

#include "sic/error.hpp"

namespace sic::exact {
void faddeev_tail_bound(arb_t bound, const arb_t kappa, const arb_t b, const arb_t binv,
                        const arb_t R, slong prec) {
  if (!arb_is_positive(kappa) || !arb_is_positive(b) || !arb_is_positive(binv) ||
      !arb_is_positive(R) || !arb_is_finite(kappa) || !arb_is_finite(b) || !arb_is_finite(binv) ||
      !arb_is_finite(R))
    throw std::runtime_error("faddeev_strip: invalid tail-bound parameters");

  sic::support::flint::Arb x, e1, c1, c2, cfac, denom;

  arb_mul(x, kappa, R, prec);
  arb_neg(e1, x);
  arb_exp(e1, e1, prec);
  arb_mul(denom, b, R, prec);
  arb_mul_2exp_si(denom, denom, 1);
  arb_neg(denom, denom);
  arb_exp(c1, denom, prec);
  arb_mul(denom, binv, R, prec);
  arb_mul_2exp_si(denom, denom, 1);
  arb_neg(denom, denom);
  arb_exp(c2, denom, prec);
  arb_neg(c1, c1);
  arb_add_ui(c1, c1, 1, prec);
  arb_neg(c2, c2);
  arb_add_ui(c2, c2, 1, prec);
  arb_mul(cfac, c1, c2, prec);
  if (!arb_is_positive(x) || !arb_is_positive(cfac) || !arb_is_finite(cfac)) {
    throw sic::Error(sic::ErrorKind::precision,
                     "faddeev_strip: tail bound is not finite and positive");
  }
  arb_mul(denom, x, cfac, prec);
  arb_div(bound, e1, denom, prec);
  arb_mul_2exp_si(bound, bound, 1);

  if (!arb_is_finite(bound) || !arb_is_positive(bound))
    throw sic::Error(sic::ErrorKind::precision,
                     "faddeev_strip: tail bound is not finite and positive");
}

}  // namespace sic::exact
namespace sic::exact::certified {
Ball::Ball() { arb_init(v); }

ContourBounds::ContourBounds(const arb_t b_value, const arb_t eta_value, double umax,
                             slong precision)
    : p(precision) {
  if (!std::isfinite(umax) || umax < 0 || !arb_is_finite(b_value) || !arb_is_positive(b_value) ||
      !arb_is_finite(eta_value) || !arb_is_positive(eta_value))
    throw std::runtime_error("quadrature: invalid contour parameters");
  arb_set(b.v, b_value);
  arb_inv(binv.v, b.v, p);
  arb_set(eta.v, eta_value);
  arb_set_d(U.v, umax);
  arb_const_pi(pi.v, p);
  Ball x, y;
  arb_mul(x.v, b.v, eta.v, p);
  arb_mul(y.v, binv.v, eta.v, p);
  if (!arb_lt(x.v, pi.v) || !arb_lt(y.v, pi.v))
    throw sic::Error(sic::ErrorKind::precision, "quadrature: contour may cross a pole");
  arb_mul_2exp_si(x.v, U.v, 1);
  arb_neg(x.v, x.v);
  arb_add_ui(x.v, x.v, 1, p);
  arb_mul(x.v, x.v, binv.v, p);
  arb_add(kappa.v, b.v, x.v, p);
  if (!arb_is_positive(kappa.v))
    throw sic::Error(sic::ErrorKind::precision, "quadrature: positive tail decay not certified");
}

bool ContourBounds::central(arb_t M, double radius) const {
  if (!std::isfinite(radius) || radius <= 1) return false;
  Ball rho, invrho, a, beta, ylo, yhi, t, s1, s2, denom;
  arb_set_d(rho.v, radius);
  arb_inv(invrho.v, rho.v, p);
  arb_add(a.v, rho.v, invrho.v, p);
  arb_mul(a.v, a.v, eta.v, p);
  arb_mul_2exp_si(a.v, a.v, -1);
  arb_sub(beta.v, rho.v, invrho.v, p);
  arb_mul(beta.v, beta.v, eta.v, p);
  arb_mul_2exp_si(beta.v, beta.v, -1);
  arb_sub(ylo.v, eta.v, beta.v, p);
  arb_add(yhi.v, eta.v, beta.v, p);
  if (!arb_is_positive(ylo.v)) return false;
  auto sine_min = [&](arb_t out, const arb_t scale) -> bool {
    Ball lo, hi, slo, shi;
    arb_mul(lo.v, ylo.v, scale, p);
    arb_mul(hi.v, yhi.v, scale, p);
    if (!arb_is_positive(lo.v) || !arb_lt(hi.v, pi.v)) return false;
    arb_sin(slo.v, lo.v, p);
    arb_sin(shi.v, hi.v, p);
    arb_min(out, slo.v, shi.v, p);
    return arb_is_positive(out);
  };
  if (!sine_min(s1.v, b.v) || !sine_min(s2.v, binv.v)) return false;
  arb_mul(denom.v, s1.v, s2.v, p);
  arb_mul(denom.v, denom.v, ylo.v, p);
  arb_mul_2exp_si(denom.v, denom.v, 2);
  arb_mul(t.v, U.v, a.v, p);
  arb_mul(t.v, t.v, binv.v, p);
  arb_mul_2exp_si(t.v, t.v, 1);
  arb_exp(t.v, t.v, p);
  arb_div(M, t.v, denom.v, p);
  return arb_is_positive(M) && arb_is_finite(M);
}

bool ContourBounds::off_axis(arb_t M, double x0, double radius) const {
  if (!std::isfinite(x0) || x0 <= 0 || !std::isfinite(radius) || radius <= 1) return false;
  Ball rho, invrho, r, x, y, denom;
  arb_set_d(rho.v, radius);
  arb_inv(invrho.v, rho.v, p);
  arb_add(r.v, rho.v, invrho.v, p);
  arb_mul_2exp_si(r.v, r.v, -2);
  arb_neg(r.v, r.v);
  arb_set_ui(x.v, 3);
  arb_mul_2exp_si(x.v, x.v, -1);
  arb_add(r.v, r.v, x.v, p);
  arb_set_d(x.v, x0);
  arb_mul(r.v, r.v, x.v, p);
  if (!arb_is_positive(r.v)) return false;
  arb_mul(x.v, b.v, r.v, p);
  arb_sinh(x.v, x.v, p);
  arb_mul(y.v, binv.v, r.v, p);
  arb_sinh(y.v, y.v, p);
  arb_mul(denom.v, x.v, y.v, p);
  arb_mul(denom.v, denom.v, r.v, p);
  arb_mul_2exp_si(denom.v, denom.v, 2);
  arb_mul(x.v, U.v, r.v, p);
  arb_mul(x.v, x.v, binv.v, p);
  arb_mul_2exp_si(x.v, x.v, 1);
  arb_exp(x.v, x.v, p);
  arb_div(M, x.v, denom.v, p);
  return arb_is_positive(M) && arb_is_finite(M);
}

void ContourBounds::panel_error(mag_t error, const arb_t M, const arb_t length, double radius,
                                ulong n) const {
  if (n == 0 || !std::isfinite(radius) || radius <= 1 || !arb_is_positive(M) || !arb_is_finite(M) ||
      !arb_is_positive(length))
    throw std::runtime_error("quadrature: invalid panel-error parameters");
  Ball rho, invrho, decay, denom, result;
  arb_set_d(rho.v, radius);
  arb_inv(invrho.v, rho.v, p);
  arb_neg(denom.v, invrho.v);
  arb_add_ui(denom.v, denom.v, 1, p);
  arb_pow_ui(decay.v, rho.v, n, p);
  arb_sqr(decay.v, decay.v, p);
  arb_mul(denom.v, denom.v, decay.v, p);
  arb_mul(result.v, M, length, p);
  arb_mul_2exp_si(result.v, result.v, 2);
  arb_div(result.v, result.v, denom.v, p);
  if (!arb_is_positive(result.v) || !arb_is_finite(result.v))
    throw sic::Error(sic::ErrorKind::precision, "quadrature: panel bound not finite and positive");
  arb_get_mag(error, result.v);
}

void ContourBounds::tail_error(mag_t error, double endpoint) const {
  Ball R, result;
  arb_set_d(R.v, endpoint);
  faddeev_tail_bound(result.v, kappa.v, b.v, binv.v, R.v, p);
  arb_get_mag(error, result.v);
}

}  // namespace sic::exact::certified
