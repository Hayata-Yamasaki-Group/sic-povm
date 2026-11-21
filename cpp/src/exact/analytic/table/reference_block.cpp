// SPDX-License-Identifier: AGPL-3.0-or-later
#include <flint/arb_hypgeom.h>
#include <flint/arb_mat.h>

#include <chrono>

#include "exact/analytic/table/faddeev.hpp"
#include "exact/analytic/table/table_view.hpp"
#include "exact/certification/bounds/quadrature_bounds.hpp"
#include "exact/runtime/parallel.hpp"
#include "sic/support/flint_raii.hpp"
namespace sic::exact {
void TableView::block_value(acb_t res, sic::support::Index c, long i, long j, slong p) {
  const QuadraticContext& kc = ar.kc;
  const Form& f = ar.cls[c].cycle[0];
  long t = ar.t;
  // epsilon = (t + index*sqrt(Delta))/2, gamma = gamma_1^3
  const long index = ar.order_index;
  long g1[2][2] = {{(t - index * f.B) / 2, -index * f.C}, {index * f.A, (t + index * f.B) / 2}};
  auto mm = [](long A[2][2], long B[2][2], long C[2][2]) {
    long T[2][2];
    for (int a = 0; a < 2; ++a)
      for (int bb = 0; bb < 2; ++bb) T[a][bb] = A[a][0] * B[0][bb] + A[a][1] * B[1][bb];
    for (sic::support::Index a = 0; a < 2; ++a)
      for (sic::support::Index bb = 0; bb < 2; ++bb) C[a][bb] = T[a][bb];
  };
  long g[2][2];
  mm(g1, g1, g);
  mm(g, g1, g);
  long a = g[0][0], cc = g[1][0], dd = g[1][1];
  // x = (i/d) tau + j/d ; u from (eps^{-3} - 1) x = u tau + v
  QuadraticNumber x =
      add(scal(Rational(i, d), ar.cls[c].tau[0]), QuadraticNumber(Rational(j, d), Rational(0)));
  QuadraticNumber e3 = mul(kc, mul(kc, ar.eps, ar.eps), ar.eps);
  QuadraticNumber w = mul(kc, sub(inv(kc, e3), QuadraticNumber(1)), x);
  auto [uq, vq] = ar.coords(c, w);
  if (!fmpz_is_one(fmpq_denref(uq.v))) throw std::runtime_error("block_value: u not integral");
  long m = fmpz_get_si(fmpq_numref(uq.v)), n = 0;
  slong wp = p + 30;
  sic::support::flint::Arb tau, z, ctd, arg;

  to_arb(tau, kc, ar.cls[c].tau[0], 1, wp);
  to_arb(z, kc, x, 1, wp);
  arb_mul_si(ctd, tau, cc, wp);
  arb_add_si(ctd, ctd, dd, wp);
  acb_one(res);
  sic::support::flint::Acb fct;
  for (long jj = 0; jj < cc; ++jj) {
    long num = dd * (m + jj) - n;
    long kj = (num >= 0) ? num / cc : -((-num + cc - 1) / cc);  // floor
    arb_mul_si(arg, tau, m + jj, wp);
    arb_add(arg, arg, z, wp);
    arb_add_si(arg, arg, kj, wp);
    faddeev(fct, arg, ctd, wp);
    acb_mul(res, res, fct, wp);
  }
  // mu_gamma = exp(pi i ((a+d)/(12c) - s(d,c) - 1/4))
  sic::support::flint::Fmpq sdq, ph, tq;
  sic::support::flint::Fmpz D_, C_;
  fmpz_set_si(D_, dd);
  fmpz_set_si(C_, cc);
  fmpq_dedekind_sum(sdq, D_, C_);
  fmpq_set_si(ph, a + dd, 12 * cc);
  fmpq_sub(ph, ph, sdq);
  fmpq_set_si(tq, 1, 4);
  fmpq_sub(ph, ph, tq);
  sic::support::flint::Arb th;
  arb_const_pi(th, wp);
  sic::support::flint::Arb q;
  arb_set_fmpq(q, ph, wp);
  arb_mul(th, th, q, wp);
  sic::support::flint::Acb mu;
  arb_sin_cos(acb_imagref(mu), acb_realref(mu), th, wp);
  acb_mul(res, res, mu, p);
}
}  // namespace sic::exact
