// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/analytic/table/faddeev.hpp"

#include "sic/error.hpp"

namespace sic::exact {
int faddeev_integrand(acb_ptr out, const acb_t w, void* param, slong order, slong prec) {
  FaddeevParam* P = static_cast<FaddeevParam*>(param);
  if (order > 1) flint_abort();
  if (order == 1) {
    // Poles lie on the imaginary axis at 0, i pi k / b, i pi k b. Holomorphic unless Re(w) may be 0
    // and Im(w) may hit one of them.
    if (arb_contains_zero(acb_realref(w))) {
      sic::support::flint::Arb lim;
      arb_min(lim, P->b, P->binv, prec);
      arb_const_pi(acb_imagref(out), prec);  // temp
      arb_mul(lim, lim, acb_imagref(out), prec);
      int ok = arb_is_positive(acb_imagref(w));
      sic::support::flint::Arb d;
      arb_sub(d, lim, acb_imagref(w), prec);
      ok = ok && arb_is_positive(d);

      if (!ok) {
        acb_indeterminate(out);
        return 0;
      }
    }
  }
  sic::support::flint::Acb t, u, v;

  acb_mul_arb(t, w, P->nu2, prec);
  acb_exp(t, t, prec);  // exp(2 nu w)
  acb_mul_arb(u, w, P->b, prec);
  acb_sinh(u, u, prec);
  acb_mul_arb(v, w, P->binv, prec);
  acb_sinh(v, v, prec);
  acb_mul(u, u, v, prec);
  acb_mul(u, u, w, prec);
  acb_mul_2exp_si(u, u, 2);  // 4 sinh sinh w
  acb_div(out, t, u, prec);

  return 0;
}

void faddeev_strip(acb_t res, const arb_t z, const arb_t tau, slong prec) {
  slong wp = prec + 40;
  FaddeevParam P;

  arb_sqrt(P.b, tau, wp);
  arb_inv(P.binv, P.b, wp);
  // nu = (z + (1 - tau)/2) / b
  sic::support::flint::Arb nu, kappa, tmp;

  arb_sub_si(tmp, tau, 1, wp);
  arb_mul_2exp_si(tmp, tmp, -1);
  arb_sub(nu, z, tmp, wp);
  arb_div(nu, nu, P.b, wp);
  arb_mul_2exp_si(P.nu2, nu, 1);
  // decay rate kappa = b + 1/b - 2|nu| > 0
  arb_add(kappa, P.b, P.binv, wp);
  arb_abs(tmp, P.nu2);
  arb_sub(kappa, kappa, tmp, wp);
  if (!arb_is_positive(kappa)) throw std::runtime_error("faddeev_strip: z outside the strip");
  // eta = (pi/2) min(b, 1/b)
  sic::support::flint::Arb eta;
  arb_min(eta, P.b, P.binv, wp);
  arb_const_pi(tmp, wp);
  arb_mul(eta, eta, tmp, wp);
  arb_mul_2exp_si(eta, eta, -1);
  // Choose an initial radius heuristically, then accept it only when the full
  // outward-rounded tail bound is below the integration error budget.
  sic::support::flint::Arf kap_lower;
  arb_get_lbound_arf(kap_lower, kappa, wp);
  double kaplo = arf_get_d(kap_lower, ARF_RND_DOWN);

  if (!(kaplo > 0.0) || !std::isfinite(kaplo))
    throw sic::Error(sic::ErrorKind::precision,
                     "faddeev_strip: decay lower bound is not representable");
  double R = ((prec + 24) * 0.6931471805599453 + 8.0) / kaplo + 2.0;
  if (!(R > 0.0) || !std::isfinite(R))
    throw sic::Error(sic::ErrorKind::precision, "faddeev_strip: truncation radius is not finite");
  sic::support::flint::Arb Ra, tail;

  sic::support::flint::Mag tol;
  mag_set_ui_2exp_si(tol, 1, -(prec + 20));
  for (;;) {
    arb_set_d(Ra, R);
    faddeev_tail_bound(tail, kappa, P.b, P.binv, Ra, wp);
    sic::support::flint::Mag tail_mag;
    arb_get_mag(tail_mag, tail);
    int meets_budget = mag_cmp(tail_mag, tol) <= 0;

    if (meets_budget) break;
    R = 2.0 * R + 1.0;
    if (!(R > 0.0) || !std::isfinite(R)) {
      throw sic::Error(sic::ErrorKind::precision,
                       "faddeev_strip: cannot find a finite truncation radius");
    }
  }
  sic::support::flint::Acb a, bnd, I;

  arb_neg(acb_realref(a), Ra);
  arb_set(acb_imagref(a), eta);
  arb_set(acb_realref(bnd), Ra);
  arb_set(acb_imagref(bnd), eta);
  acb_calc_integrate_opt_t opt;
  acb_calc_integrate_opt_init(opt);
  opt->eval_limit = 1000000000;
  opt->depth_limit = 4 * wp;
  int st = acb_calc_integrate(I, faddeev_integrand, &P, a, bnd, prec + 20, tol, opt, wp);
  if (st != ARB_CALC_SUCCESS)
    throw sic::Error(sic::ErrorKind::precision, "faddeev_strip: integration failed");
  // The computed outward-rounded value is the explicit tail enclosure used
  // to certify the truncation error.
  {
    sic::support::flint::Mag m;
    arb_get_mag(m, tail);
    acb_add_error_mag(I, m);
  }
  acb_exp(res, I, prec);
}

void acb_e(acb_t r, const arb_t x, slong prec) {
  sic::support::flint::Arb t;
  arb_const_pi(t, prec);
  arb_mul(t, t, x, prec);
  arb_mul_2exp_si(t, t, 1);
  arb_sin_cos(acb_imagref(r), acb_realref(r), t, prec);
}

void faddeev(acb_t res, const arb_t z, const arb_t tau, slong prec) {
  slong wp = prec + 30;
  sic::support::flint::Arb c, z0, tmp;

  arb_sub_si(c, tau, 1, wp);
  arb_mul_2exp_si(c, c, -1);  // centre (tau - 1)/2
  arb_sub(tmp, z, c, wp);
  long k = (long)llround(arf_get_d(arb_midref(tmp), ARF_RND_NEAR));
  arb_sub_si(z0, z, k, wp);  // z = z0 + k
  faddeev_strip(res, z0, tau, wp);
  sic::support::flint::Acb f, e;

  if (k > 0) {
    // Phi(z0 + k) = Phi(z0) / prod_{j=1..k} (1 - e((z0 + j)/tau))
    for (long j = 1; j <= k; ++j) {
      arb_add_si(tmp, z0, j, wp);
      arb_div(tmp, tmp, tau, wp);
      acb_e(e, tmp, wp);
      acb_sub_ui(e, e, 1, wp);
      acb_neg(e, e);
      acb_div(res, res, e, wp);
    }
  } else if (k < 0) {
    // Phi(z) = Phi(z + m) prod_{j=1..m} (1 - e((z + j)/tau)), m = -k
    for (long j = 1; j <= -k; ++j) {
      arb_add_si(tmp, z, j, wp);
      arb_div(tmp, tmp, tau, wp);
      acb_e(e, tmp, wp);
      acb_sub_ui(e, e, 1, wp);
      acb_neg(e, e);
      acb_mul(res, res, e, wp);
    }
  }
  acb_set_round(res, res, prec);
}

}  // namespace sic::exact
