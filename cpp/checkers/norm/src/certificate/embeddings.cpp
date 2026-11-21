// SPDX-License-Identifier: AGPL-3.0-or-later
#include "embeddings.hpp"

#include "../runtime/diagnostics.hpp"
namespace norm = sic_check::norm;
namespace sic_check::norm {
slong log2_upper(const acb_t z) {
  mag_t m;
  mag_init(m);
  acb_get_mag(m, z);
  slong e = LONG_MIN;
  if (!mag_is_zero(m)) {
    if (mag_is_inf(m))
      e = LONG_MAX;
    else if (!fmpz_fits_si(MAG_EXPREF(m)))
      e = fmpz_sgn(MAG_EXPREF(m)) > 0 ? LONG_MAX : LONG_MIN + 1;
    else
      e = fmpz_get_si(MAG_EXPREF(m));  // m = man * 2^(exp - 30), man < 2^30
  }
  mag_clear(m);
  return e;
}

double log2_approx(const acb_t z) {
  mag_t m;
  mag_init(m);
  acb_get_mag(m, z);
  const double r = mag_is_zero(m) ? -1e300 : mag_get_d_log2_approx(m);
  mag_clear(m);
  return r;
}

slong ceil_log2_u(uint64_t x) {  // smallest e with x <= 2^e
  slong e = 0;
  while ((uint64_t(1) << e) < x) ++e;
  return e;
}

void omega_at(acb_t out, const norm::Tower& T, int64_t D0, bool other, slong prec) {
  // iota(omega) = (1 - sqrt(D0))/2 (s = 1) or sqrt(D0) (s = 0); `other` flips the sign of sqrt(D0).
  arb_t r;
  arb_init(r);
  arb_set_si(r, D0 < 0 ? -D0 : D0);
  arb_sqrt(r, r, prec);
  acb_zero(out);
  if (D0 > 0)
    arb_set(acb_realref(out), r);
  else
    arb_set(acb_imagref(out), r);
  if (other) acb_neg(out, out);
  if (T.s) {
    acb_neg(out, out);
    arb_add_si(acb_realref(out), acb_realref(out), 1, prec);
    acb_mul_2exp_si(out, out, -1);
  }
  arb_clear(r);
}

void horner(acb_t out, const norm::Tower& T, const QuadraticElement* x, unsigned n,
            const std::vector<ComplexBall>& r, const acb_t omega, slong prec) {
  if (n == 0) {
    norm::acb_from_ke(out, x[0], omega, prec);
    return;
  }
  const unsigned p = T.p[n - 1];
  const size_t M = T.N[n - 1];
  acb_t part;
  acb_init(part);
  acb_zero(out);
  for (unsigned k = p; k-- > 0;) {
    acb_mul(out, out, r[n - 1].v, prec);
    if (!norm::block_zero(x + k * M, M)) {
      horner(part, T, x + k * M, n - 1, r, omega, prec);
      acb_add(out, out, part, prec);
    }
  }
  acb_clear(part);
}

std::vector<ComplexBall> isolate(const acb_poly_t P, unsigned p, slong prec) {
  std::vector<ComplexBall> out(p);
  if (p == 2 && acb_is_zero(P->coeffs + 1)) {
    acb_t c;
    acb_init(c);
    acb_neg(c, P->coeffs + 0);  // X^2 = c
    if (arb_is_negative(acb_realref(c))) {
      acb_neg(c, c);
      acb_sqrt(out[0].v, c, prec);
      acb_mul_onei(out[0].v, out[0].v);
    } else {
      acb_sqrt(out[0].v, c, prec);
    }
    acb_neg(out[1].v, out[0].v);
    const bool ok = acb_is_finite(out[0].v) && !acb_contains_zero(out[0].v) &&
                    !acb_overlaps(out[0].v, out[1].v);
    acb_clear(c);
    if (!ok) out.clear();
    return out;
  }
  acb_ptr roots = _acb_vec_init(p);
  const slong found = acb_poly_find_roots(roots, P, nullptr, 0, prec);
  bool ok = found == static_cast<slong>(p);
  for (unsigned j = 0; j < p && ok; ++j) {
    acb_set(out[j].v, roots + j);
    if (!acb_is_finite(out[j].v)) ok = false;
    for (unsigned k = 0; k < j && ok; ++k)
      if (acb_overlaps(out[j].v, out[k].v)) ok = false;
  }
  _acb_vec_clear(roots, p);
  if (!ok) out.clear();
  return out;
}

Ctx::Ctx(const norm::Reader& r, norm::Builder& b) : reader(r), meta(r.meta()), B(b), T(b.T) {}

auto Ctx::level_poly(acb_poly_t P, unsigned i, const std::vector<ComplexBall>& r, const acb_t omega,
                     slong prec) const -> void {
  acb_poly_zero(P);
  acb_t c;
  acb_init(c);
  for (unsigned k = 0; k < T.p[i]; ++k) {
    horner(c, T, T.f[i][k].data(), i, r, omega, prec);
    acb_poly_set_coeff_acb(P, k, c);
  }
  acb_one(c);
  acb_poly_set_coeff_acb(P, T.p[i], c);
  acb_clear(c);
}

AllEmbeddings::AllEmbeddings(const norm::Tower& t, int64_t D0, slong pr) : T(t), prec(pr) {
  omega_at(om[0].v, T, D0, false, prec);
  omega_at(om[1].v, T, D0, true, prec);
}

auto AllEmbeddings::count(unsigned n) const -> size_t { return 2 * T.N[n]; }

auto AllEmbeddings::values(const QuadraticElement* x, unsigned n) const
    -> std::vector<ComplexBall> {
  if (n == 0) {
    std::vector<ComplexBall> v(2);
    for (int e = 0; e < 2; ++e) norm::acb_from_ke(v[e].v, x[0], om[e].v, prec);
    return v;
  }
  const unsigned p = T.p[n - 1];
  const size_t M = T.N[n - 1];
  std::vector<std::vector<ComplexBall>> sub(p);
  for (unsigned k = 0; k < p; ++k)
    if (!norm::block_zero(x + k * M, M)) sub[k] = values(x + k * M, n - 1);
  const size_t E = count(n - 1);
  std::vector<ComplexBall> out(E * p);
  for (size_t psi = 0; psi < E; ++psi)
    for (unsigned j = 0; j < p; ++j) {
      acb_ptr o = out[psi * p + j].v;
      const acb_srcptr r = roots[n - 1][psi * p + j].v;
      acb_zero(o);
      for (unsigned k = p; k-- > 0;) {
        acb_mul(o, o, r, prec);
        if (!sub[k].empty()) acb_add(o, o, sub[k][psi].v, prec);
      }
    }
  return out;
}

auto AllEmbeddings::build() -> bool {
  roots.assign(T.t, {});
  ComplexPolynomial P;
  bool ok = true;
  for (unsigned i = 0; i < T.t && ok; ++i) {
    std::vector<std::vector<ComplexBall>> coef(T.p[i]);
    for (unsigned k = 0; k < T.p[i]; ++k) coef[k] = values(T.f[i][k].data(), i);
    const size_t E = count(i);
    roots[i].resize(E * T.p[i]);
    for (size_t psi = 0; psi < E && ok; ++psi) {
      acb_poly_zero(P);
      for (unsigned k = 0; k < T.p[i]; ++k) acb_poly_set_coeff_acb(P, k, coef[k][psi].v);
      ComplexBall one;
      acb_one(one);
      acb_poly_set_coeff_acb(P, T.p[i], one);
      auto iso = isolate(P, T.p[i], prec);
      if (iso.empty()) {
        ok = false;
        break;
      }
      for (unsigned j = 0; j < T.p[i]; ++j) acb_set(roots[i][psi * T.p[i] + j].v, iso[j].v);
    }
  }
  return ok;
}

LeafEmbeddings::LeafEmbeddings(const norm::Reader& r, const norm::Tower& t, slong pr)
    : reader(r), T(t), prec(pr) {
  omega_at(om[0].v, T, r.meta().D0, false, prec);
  omega_at(om[1].v, T, r.meta().D0, true, prec);
}

auto LeafEmbeddings::count(unsigned n) const -> size_t { return 2 * T.N[n]; }

auto LeafEmbeddings::values(const QuadraticElement* lam, unsigned n) const
    -> std::vector<ComplexBall> {
  if (n == 0) {
    std::vector<ComplexBall> v(2);
    for (int e = 0; e < 2; ++e) norm::acb_from_ke(v[e].v, lam[0], om[e].v, prec);
    return v;
  }
  const unsigned p = T.p[n - 1];
  const size_t M = T.N[n - 1];
  std::vector<std::vector<ComplexBall>> sub(p);
  for (unsigned k = 0; k < p; ++k)
    if (!norm::block_zero(lam + k * M, M)) sub[k] = values(lam + k * M, n - 1);
  const size_t E = count(n - 1);
  std::vector<ComplexBall> out(E * p);
  for (size_t psi = 0; psi < E; ++psi)
    for (unsigned j = 0; j < p; ++j) {
      acb_ptr o = out[psi * p + j].v;
      acb_zero(o);
      for (unsigned k = 0; k < p; ++k)
        if (!sub[k].empty())
          acb_addmul(o, sub[k][psi].v, duals[n - 1][(psi * p + j) * p + k].v, prec);
    }
  return out;
}

auto LeafEmbeddings::find(const std::string& kind, unsigned level, int64_t k) const -> size_t {
  const auto& el = reader.meta().elements;
  for (size_t id = 0; id < el.size(); ++id)
    if (el[id].kind == kind && el[id].level == level && el[id].k == k && el[id].axis == -1)
      return id;
  return SIZE_MAX;
}

auto LeafEmbeddings::build() -> bool {
  roots.assign(T.t, {});
  duals.assign(T.t, {});
  lvl_max.assign(T.t, -1e300);
  lvl_min.assign(T.t, 1e300);
  lvl_logabs_sum.assign(T.t, 0);
  ComplexPolynomial P;
  for (unsigned i = 0; i < T.t; ++i) {
    const unsigned p = T.p[i];
    std::vector<std::vector<ComplexBall>> coef(p);
    if (p == 2) {
      const size_t id = find("wsq", i, -1);
      if (id == SIZE_MAX) {
        return false;
      }
      const auto lam = norm::read_leaves(reader, id, T);
      auto c = values(lam.data(), i);
      for (auto& x : c) {
        lvl_max[i] = std::max(lvl_max[i], log2_approx(x.v));
        lvl_min[i] = std::min(lvl_min[i], log2_approx(x.v));
        acb_neg(x.v, x.v);
      }
      coef[0] = std::move(c);
      coef[1].assign(count(i), ComplexBall());
    } else {
      for (unsigned k = 0; k < p; ++k) {
        const size_t id = find("fcoef", i, k);
        if (id == SIZE_MAX) {
          return false;
        }
        const auto lam = norm::read_leaves(reader, id, T);
        coef[k] = values(lam.data(), i);
      }
    }
    const size_t E = count(i);
    roots[i].resize(E * p);
    duals[i].resize(E * p * p);
    ComplexBall fp, qk, one;
    acb_one(one);
    for (size_t psi = 0; psi < E; ++psi) {
      acb_poly_zero(P);
      for (unsigned k = 0; k < p; ++k) acb_poly_set_coeff_acb(P, k, coef[k][psi].v);
      acb_poly_set_coeff_acb(P, p, one);
      auto iso = isolate(P, p, prec);
      if (iso.empty()) {
        return false;
      }
      for (unsigned j = 0; j < p; ++j) {
        const acb_srcptr r = iso[j].v;
        acb_set(roots[i][psi * p + j].v, r);
        // f'(r)
        acb_zero(fp);
        for (unsigned k = p; k >= 1; --k) {
          acb_mul(fp, fp, r, prec);
          acb_t c;
          acb_init(c);
          if (k == p)
            acb_set_ui(c, p);
          else
            acb_mul_ui(c, coef[k][psi].v, k, prec);
          acb_add(fp, fp, c, prec);
          acb_clear(c);
        }
        lvl_logabs_sum[i] += log2_approx(fp);
        // q_{p-1} = 1, q_j = a_{j+1} + r q_{j+1}
        acb_one(qk);
        for (unsigned k = p; k-- > 0;) {
          acb_div(duals[i][(psi * p + j) * p + k].v, qk, fp, prec);
          if (k == 0) break;
          acb_mul(qk, qk, r, prec);
          acb_add(qk, qk, coef[k][psi].v, prec);
        }
      }
    }
  }
  return true;
}
}  // namespace sic_check::norm
