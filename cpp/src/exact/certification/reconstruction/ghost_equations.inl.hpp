// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::exact {
inline long GhostRefiner::neg(long y) const {
  long i = y / d, j = y % d;
  return ((d - i) % d) * d + ((d - j) % d);
}

inline void GhostRefiner::coef(acb_t r, const Ctx& C, long yp) const {
  ghost_coefficient(r, C, d, n, ipos[yp]);
}

inline long GhostRefiner::zidx(long a) const { return a < k ? a : a - 1; }

inline void GhostRefiner::complex_ball_vector_of(acb_t A, acb_t V, const Ctx& C, long y,
                                                 const std::vector<acb_struct>& v,
                                                 const std::vector<acb_struct>& u,
                                                 const acb_t sinv) const {
  long i = y / d, j = y % d;
  sic::support::flint::Acb t;
  acb_zero(A);
  for (long a = 0; a < d; ++a) {  // sum_a w^{-ia} v_a u_{a+j}
    acb_mul(t, &v[a], &u[(a + j) % d], C.p);
    acb_addmul(A, t, &C.wd[(d - (i * a) % d) % d], C.p);
  }
  acb_mul(A, A, sinv, C.p);
  coef(t, C, y);
  acb_mul(V, A, t, C.p);
}

inline void GhostRefiner::check_vector_stop() { sic::support::check_stop(); }

inline void GhostRefiner::addgrad(acb_ptr row, const Ctx& C, long y, const acb_t fac, const acb_t A,
                                  const std::vector<acb_struct>& v,
                                  const std::vector<acb_struct>& u, const acb_t sinv) const {
  sic::support::flint::Acb c, t;
  addgrad_with_scratch(row, C, y, fac, A, v, u, sinv, c, t);
}

inline void GhostRefiner::addgrad_with_scratch(acb_ptr row, const Ctx& C, long y, const acb_t fac,
                                               const acb_t A, const std::vector<acb_struct>& v,
                                               const std::vector<acb_struct>& u, const acb_t sinv,
                                               acb_ptr c, acb_ptr t) const {
  long i = y / d, j = y % d;
  coef(c, C, y);
  acb_mul(c, c, fac, C.p);  // dE contribution: fac * coef * dA
  for (long l = 0; l < d; ++l) {
    if (l == k) continue;
    // dA/dv_l = (w^{-il} u_{l+j} - A u_l) / s
    acb_mul(t, &C.wd[(d - (i * l) % d) % d], &u[(l + j) % d], C.p);
    acb_submul(t, A, &u[l], C.p);
    acb_mul(t, t, sinv, C.p);
    acb_mul(t, t, c, C.p);
    acb_add(row + zidx(l), row + zidx(l), t, C.p);
    // dA/du_l = (w^{-i(l-j)} v_{l-j} - A v_l) / s
    long a = ((l - j) % d + d) % d;
    acb_mul(t, &C.wd[(d - (i * a) % d) % d], &v[a], C.p);
    acb_submul(t, A, &v[l], C.p);
    acb_mul(t, t, sinv, C.p);
    acb_mul(t, t, c, C.p);
    acb_add(row + d - 1 + zidx(l), row + d - 1 + zidx(l), t, C.p);
  }
}

}  // namespace sic::exact
