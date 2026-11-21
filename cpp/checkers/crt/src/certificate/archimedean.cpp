// SPDX-License-Identifier: AGPL-3.0-or-later
#include "archimedean.hpp"

#include "../format/canonical_json.hpp"
#include "../format/container.hpp"
#include "../format/integer_codec.hpp"
#include "../runtime/diagnostics.hpp"
#include "../runtime/parallel_for.hpp"
namespace sic_check::crt {
using namespace sic_check::crt::sicx;
AcbVec::AcbVec(size_t k) { resize(k); }

auto AcbVec::resize(size_t k) -> void {
  if (v) _acb_vec_clear(v, n);
  n = k;
  v = n ? _acb_vec_init(n) : nullptr;
}

AcbVec::~AcbVec() {
  if (v) _acb_vec_clear(v, n);
}

AcbVec::AcbVec(AcbVec&& o) noexcept : v(o.v), n(o.n) {
  o.v = nullptr;
  o.n = 0;
}

AcbVec& AcbVec::operator=(AcbVec&& o) noexcept {
  if (v) _acb_vec_clear(v, n);
  v = o.v;
  n = o.n;
  o.v = nullptr;
  o.n = 0;
  return *this;
}

acb_ptr AcbVec::operator[](size_t i) const { return v + i; }

slong mag_exp_upper(const mag_t m) {  // m <= 2^e
  if (mag_is_zero(m))
    return kNegLog;  // exact zero: any bound works; small sentinel without overflow risk
  if (mag_is_inf(m)) refuse("infinite ball (precision)");
  return fmpz_get_si(MAG_EXPREF(m));
}

void parse_arbdump(arb_t out, const string& s) {
  vector<string> f;
  size_t p0 = 0;
  for (;;) {
    size_t q = s.find(' ', p0);
    if (q == string::npos) {
      f.push_back(s.substr(p0));
      break;
    }
    f.push_back(s.substr(p0, q - p0));
    p0 = q + 1;
  }
  if (f.size() != 4) refuse("branch rectangle: not an Arb dump string");
  Z M, E, m, e;
  auto hx = [](fmpz_t z, const string& t) {
    bool neg = !t.empty() && t[0] == '-';
    string b = neg ? t.substr(1) : t;
    if (b.empty() || fmpz_set_str(z, b.c_str(), 16) != 0) refuse("branch rectangle: bad hex");
    if (neg) fmpz_neg(z, z);
  };
  hx(M, f[0]);
  hx(E, f[1]);
  hx(m, f[2]);
  hx(e, f[3]);
  arb_set_fmpz_2exp(out, M, E);
  arf_t r;
  arf_init(r);
  arf_set_fmpz_2exp(r, m, e);
  mag_t rr;
  mag_init(rr);
  arf_get_mag(rr, r);
  mag_set(arb_radref(out), rr);
  mag_clear(rr);
  arf_clear(r);
}

Arch::Arch(const Ctx& c, slong pr) : C(c), prec(pr), om(2) {
  arb_t s;
  arb_init(s);
  arb_set_si(s, C.D0);
  arb_sqrt(s, s, prec);
  acb_zero(om[0]);
  arb_sub_si(acb_realref(om[0]), s, 1, prec);
  arb_neg(acb_realref(om[0]), acb_realref(om[0]));
  acb_mul_2exp_si(om[0], om[0], -1);
  acb_zero(om[1]);
  arb_add_si(acb_realref(om[1]), s, 1, prec);
  acb_mul_2exp_si(om[1], om[1], -1);
  arb_clear(s);
}

auto Arch::transform(size_t id) const -> AcbVec {
  const Elem& e = C.in.el[id];
  const Coords& c = *C.co[id];
  Uint64 P = 2, rest = e.ncoord;
  AcbVec cur(rest * 2);
  parallel_for(rest, [&](size_t lo, size_t hi) {
    acb_t z;
    acb_init(z);
    for (size_t s = lo; s < hi; ++s)
      for (int k = 0; k < 2; ++k) {
        acb_mul_fmpz(z, om[k], &c.Y[s], prec);
        acb_add_fmpz(z, z, &c.X[s], prec);
        acb_mul_fmpz(cur[2 * s + k], z, c.u, prec);
      }
    acb_clear(z);
  });
  size_t li = 0;
  for (int j = 0; j < e.field; ++j) {
    const Uint64 pj = C.deg[j];
    const bool inJ = li < e.levels.size() && e.levels[li] == j;
    const Uint64 pk = inJ ? pj : 1;
    if (inJ) ++li;
    const Uint64 nrest = rest / pk, nP = P * pj;
    AcbVec out(nrest * nP);
    parallel_for(nrest * pj, [&](size_t lo, size_t hi) {
      B tmp;
      for (size_t q = lo; q < hi; ++q) {
        const Uint64 r = q / pj, rho = q % pj;
        for (Uint64 psi = 0; psi < P; ++psi) {
          acb_ptr o = out[r * nP + P * rho + psi];
          for (Uint64 k = 0; k < pk; ++k) {
            acb_mul(tmp, cur[(r * pk + k) * P + psi], Bd[j][k][P * rho + psi], prec);
            if (k == 0)
              acb_swap(o, tmp);
            else
              acb_add(o, o, tmp, prec);
          }
        }
      }
    });
    cur = std::move(out);
    P = nP;
    rest = nrest;
  }
  return cur;
}

auto Arch::isolate(acb_srcptr coef, Uint64 pj, acb_ptr roots, slong prec) -> bool {
  if (pj == 2 && acb_is_zero(coef + 1)) {
    acb_t c;
    acb_init(c);
    acb_neg(c, coef + 0);
    if (arb_is_negative(acb_realref(c))) {
      acb_neg(c, c);
      acb_sqrt(roots, c, prec);
      acb_mul_onei(roots, roots);
    } else
      acb_sqrt(roots, c, prec);
    acb_neg(roots + 1, roots);
    const bool ok =
        acb_is_finite(roots) && !acb_contains_zero(roots) && !acb_overlaps(roots, roots + 1);
    acb_clear(c);
    return ok;
  }
  acb_poly_t P;
  acb_poly_init(P);
  for (Uint64 k = 0; k < pj; ++k) acb_poly_set_coeff_acb(P, k, coef + k);
  acb_t one;
  acb_init(one);
  acb_one(one);
  acb_poly_set_coeff_acb(P, pj, one);
  acb_clear(one);
  const slong found = acb_poly_find_roots(roots, P, nullptr, 0, prec);
  acb_poly_clear(P);
  if (found != (slong)pj) return false;
  for (Uint64 a = 0; a < pj; ++a) {
    if (!acb_is_finite(roots + a)) return false;
    for (Uint64 b = 0; b < a; ++b)
      if (acb_overlaps(roots + a, roots + b)) return false;
  }
  return true;
}

auto Arch::build() -> bool {
  R.clear();
  R.resize(C.t);
  Bd.clear();
  Bd.resize(C.t);
  Tc.assign(C.t, 0);
  Tfp.assign(C.t, 0);
  Bg.assign(C.t, kNegLog);
  for (int j = 0; j < C.t; ++j) {
    const Uint64 P = 2 * C.N[j], pj = C.deg[j];
    vector<AcbVec> a(pj);
    if (pj == 2) {
      a[0] = transform(C.wsq[j]);
      a[1].resize(P);
      Magnitude prod;
      mag_one(prod);
      for (Uint64 psi = 0; psi < P; ++psi) {
        Magnitude m;
        acb_get_mag(m, a[0][psi]);
        mag_mul(prod, prod, m);
        acb_neg(a[0][psi], a[0][psi]);
      }
      Tc[j] = mag_exp_upper(prod);
    } else {
      for (Uint64 k = 0; k < pj; ++k) a[k] = transform(C.fcoef[j][k]);
    }
    R[j].resize(P * pj);
    Bd[j].clear();
    for (Uint64 k = 0; k < pj; ++k) Bd[j].emplace_back(P * pj);
    std::atomic<bool> ok{true};
    std::mutex mu;
    Magnitude fprod;
    mag_one(fprod);
    slong bg = kNegLog;
    parallel_for(P, [&](size_t lo, size_t hi) {
      AcbVec coef(pj), roots(pj);
      B fp, q, tmp;
      Magnitude loc, m;
      mag_one(loc);
      slong lbg = kNegLog;
      for (size_t psi = lo; psi < hi && ok; ++psi) {
        for (Uint64 k = 0; k < pj; ++k) acb_set(coef[k], a[k][psi]);
        if (!isolate(coef.v, pj, roots.v, prec)) {
          ok = false;
          break;
        }
        for (Uint64 rho = 0; rho < pj; ++rho) {
          acb_srcptr r = roots[rho];
          acb_set(R[j][psi + P * rho], r);
          acb_get_mag(m, r);
          lbg = std::max(lbg, mag_exp_upper(m));
          acb_zero(fp);
          for (Uint64 k = pj; k >= 1; --k) {
            acb_mul(fp, fp, r, prec);
            if (k == pj)
              acb_add_ui(fp, fp, pj, prec);
            else {
              acb_mul_ui(tmp, coef[k], k, prec);
              acb_add(fp, fp, tmp, prec);
            }
          }
          if (acb_contains_zero(fp)) {
            ok = false;
            break;
          }
          acb_get_mag(m, fp);
          mag_mul(loc, loc, m);
          acb_one(q);
          for (Uint64 k = pj; k-- > 0;) {
            acb_div(Bd[j][k][psi + P * rho], q, fp, prec);
            if (k == 0) break;
            acb_mul(q, q, r, prec);
            acb_add(q, q, coef[k], prec);
          }
        }
      }
      std::lock_guard<std::mutex> g(mu);
      mag_mul(fprod, fprod, loc);
      bg = std::max(bg, lbg);
    });
    Tfp[j] = mag_exp_upper(fprod);
    Bg[j] = bg;
    if (!ok) return false;
  }
  return true;
}

auto Arch::stats(const AcbVec& v, slong& Bmax, bool& finite) -> void {
  Bmax = kNegLog;
  finite = true;
  Magnitude m;
  for (size_t i = 0; i < v.n; ++i) {
    if (!acb_is_finite(v[i])) finite = false;
    acb_get_mag(m, v[i]);
    Bmax = std::max(Bmax, mag_exp_upper(m));
  }
}

auto Bounds::elem(const Ctx& C, size_t id) const -> Int64 {
  const Elem& e = C.in.el[id];
  Int64 T = 0;
  size_t li = 0;
  for (int j = 0; j < e.field; ++j) {
    const bool inJ = li < e.levels.size() && e.levels[li] == j;
    if (inJ) ++li;
    T += inJ ? TBmax[j] : TB0[j];
  }
  const Int64 a2 = quad_before[e.field];
  T -= std::min<Int64>(a2, C.co[id]->v2) * n;
  return std::max<Int64>(T, 0);
}

Int64 cdiv(Int64 a, Int64 b) { return a >= 0 ? (a + b - 1) / b : -((-a) / b); }

Int64 log2sum(Int64 a, Int64 b) { return std::max(a, b) + 1; }
}  // namespace sic_check::crt
