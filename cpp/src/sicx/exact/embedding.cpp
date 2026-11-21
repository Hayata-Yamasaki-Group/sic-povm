// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/sicx/verify.hpp"
#include "sic/support/flint_raii.hpp"
#include "sicx/codec/block.hpp"
#include "sicx/exact/exact.hpp"
namespace sic::sicx::exact {
ComplexBall::ComplexBall() { acb_init(v); }

ComplexBall::~ComplexBall() { acb_clear(v); }

ComplexBall::ComplexBall(const ComplexBall& o) {
  acb_init(v);
  acb_set(v, o.v);
}

ComplexBall::ComplexBall(ComplexBall&& o) noexcept {
  acb_init(v);
  acb_swap(v, o.v);
}

ComplexBall& ComplexBall::operator=(const ComplexBall& o) {
  if (this != &o) acb_set(v, o.v);
  return *this;
}

ComplexBall& ComplexBall::operator=(ComplexBall&& o) noexcept {
  acb_swap(v, o.v);
  return *this;
}

void acb_from_ke(acb_t out, const QuadraticElement& x, const acb_t omega, slong prec) {
  acb_t t0;
  acb_init(t0);
  arb_set_fmpq(acb_realref(out), x.a, prec);
  arb_zero(acb_imagref(out));
  arb_set_fmpq(acb_realref(t0), x.b, prec);
  arb_zero(acb_imagref(t0));
  acb_addmul(out, t0, omega, prec);
  acb_clear(t0);
}

std::vector<ComplexBall> monomials(const Tower& T, unsigned n,
                                   const std::vector<ComplexBall>& roots, slong prec) {
  std::vector<ComplexBall> mono(T.N[n]);
  acb_one(mono[0].v);
  for (unsigned i = 0; i < n; ++i)
    for (unsigned k = 1; k < T.p[i]; ++k)
      for (size_t j = 0; j < T.N[i]; ++j)
        acb_mul(mono[k * T.N[i] + j].v, mono[(k - 1) * T.N[i] + j].v, roots[i].v, prec);
  return mono;
}

void evaluate(acb_t out, const Elt& x, const std::vector<ComplexBall>& mono, const acb_t omega,
              slong prec) {
  acb_t c, t0;
  acb_init(c);
  acb_init(t0);
  acb_zero(out);
  for (size_t e = 0; e < x.size(); ++e) {
    if (x[e].zero()) continue;
    acb_from_ke(c, x[e], omega, prec);
    acb_addmul(out, c, mono[e].v, prec);
  }
  acb_clear(c);
  acb_clear(t0);
}

std::string unit_decimal(slong places) {
  sic::support::flint::Fmpz one;
  fmpz_set_ui(one, 1);
  const std::string out = fixed_decimal(one, places);
  return out;
}

void arb_from_decimal(arb_t out, const std::string& text, slong prec) {
  if (arb_set_str(out, text.c_str(), prec))
    fail(sic::ErrorKind::invalid_format, "decimal does not parse: " + text);
}

void Builder::load_branch(acb_t out, const std::array<std::string, 2>& b) {
  if (arb_load_str(acb_realref(out), b[0].c_str()) || arb_load_str(acb_imagref(out), b[1].c_str()))
    fail(sic::ErrorKind::invalid_format, "branch rectangle does not load");
}

void Builder::set_omega() {
  const slong prec = opt.precision;
  arb_t r;
  arb_init(r);
  arb_set_si(r, meta.D0 < 0 ? -meta.D0 : meta.D0);
  arb_sqrt(r, r, prec);
  // sqrt(D0): positive real, or i*sqrt(-D0) for D0 < 0.
  ComplexBall sq;
  if (meta.D0 > 0)
    arb_set(acb_realref(sq.v), r);
  else
    arb_set(acb_imagref(sq.v), r);
  if (T.s) {  // omega = (1 - sqrt(D0))/2
    acb_neg(omega.v, sq.v);
    arb_add_si(acb_realref(omega.v), acb_realref(omega.v), 1, prec);
    acb_mul_2exp_si(omega.v, omega.v, -1);
  } else {
    acb_neg(omega.v, sq.v);
  }
  if (meta.D0 == 1) acb_zero(omega.v);
  arb_clear(r);
}

void Builder::level_polynomial(acb_poly_t P, unsigned i, const std::vector<ComplexBall>& rts,
                               slong prec) {
  const auto mono = monomials(T, i, rts, prec);
  acb_poly_zero(P);
  acb_t c;
  acb_init(c);
  for (unsigned k = 0; k < T.p[i]; ++k) {
    evaluate(c, T.f[i][k], mono, omega.v, prec);
    acb_poly_set_coeff_acb(P, k, c);
  }
  acb_one(c);
  acb_poly_set_coeff_acb(P, T.p[i], c);
  acb_clear(c);
}

void Builder::certify_roots() {
  const slong prec = opt.precision;
  set_omega();
  roots.assign(T.t, ComplexBall());
  root_text.clear();
  std::vector<ComplexBall> mids(T.t);
  sic::support::flint::AcbPoly P;
  sic::support::flint::AcbPoly dP;
  ComplexBall x, x0, B, branch, N, fx, dB;
  sic::support::flint::Arb rho;
  for (unsigned i = 0; i < T.t; ++i) {
    load_branch(branch.v, meta.branches[i]);
    // approximate root: Newton from the branch midpoint on the midpoints of the lower roots
    level_polynomial(P, i, mids, prec);
    acb_poly_derivative(dP, P, prec);
    acb_get_mid(x.v, branch.v);
    for (int it = 0; it < 80; ++it) {
      acb_poly_evaluate(fx.v, P, x.v, prec);
      acb_poly_evaluate(dB.v, dP, x.v, prec);
      acb_div(fx.v, fx.v, dB.v, prec);
      acb_sub(x.v, x.v, fx.v, prec);
      acb_get_mid(x.v, x.v);
    }
    // decimal midpoint
    sic::support::flint::Arb unused;
    const auto re = decimal_ball(acb_realref(x.v), opt.places, unused);
    const auto im = decimal_ball(acb_imagref(x.v), opt.places, unused);
    arb_from_decimal(acb_realref(x0.v), re.midpoint, prec);
    arb_from_decimal(acb_imagref(x0.v), im.midpoint, prec);
    // interval Newton on the published ball, with the lower roots at their published balls
    level_polynomial(P, i, roots, prec);
    acb_poly_derivative(dP, P, prec);
    bool ok = false;
    for (slong e = opt.places - 4; e >= 12 && !ok; e -= 3) {
      arb_from_decimal(rho, unit_decimal(e), prec);
      acb_set(B.v, x0.v);
      arb_add_error(acb_realref(B.v), rho);
      arb_add_error(acb_imagref(B.v), rho);
      acb_poly_evaluate(fx.v, P, x0.v, prec);
      acb_poly_evaluate(dB.v, dP, B.v, prec);
      acb_div(fx.v, fx.v, dB.v, prec);
      acb_sub(N.v, x0.v, fx.v, prec);
      if (acb_contains(B.v, N.v)) {
        if (!acb_overlaps(B.v, branch.v))
          fail(sic::ErrorKind::construction,
               "level " + std::to_string(i) + ": certified root lies outside the branch rectangle");
        roots[i] = B;
        mids[i] = x0;
        root_text.push_back(re.midpoint);
        root_text.push_back(unit_decimal(e));
        root_text.push_back(im.midpoint);
        root_text.push_back(unit_decimal(e));
        ok = true;
      }
    }
    if (!ok)
      fail(sic::ErrorKind::precision,
           "level " + std::to_string(i) +
               ": root isolation (interval Newton) failed at this precision");
  }
}
}  // namespace sic::sicx::exact
