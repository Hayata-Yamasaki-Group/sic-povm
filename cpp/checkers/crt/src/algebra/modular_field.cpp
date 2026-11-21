// SPDX-License-Identifier: AGPL-3.0-or-later
#include "modular_field.hpp"

#include "../format/canonical_json.hpp"
#include "../format/container.hpp"
#include "../format/integer_codec.hpp"
#include "../runtime/diagnostics.hpp"
#include "../runtime/parallel_for.hpp"
namespace sic_check::crt {
using namespace sic_check::crt::sicx;
Coords::Coords() { fmpz_init(u); }

Coords::~Coords() {
  for (auto& v : X) fmpz_clear(&v);
  for (auto& v : Y) fmpz_clear(&v);
  fmpz_clear(u);
}

auto Ctx::lin(const vector<Uint64>& w) const -> Uint64 {
  Uint64 c = 0;
  for (size_t l = 0; l < naxes; ++l) c += (w[l] % smith[l]) * Ms[l];
  return c;
}

auto Ctx::digits(Uint64 c) const -> vector<Uint64> {
  vector<Uint64> w(naxes);
  for (size_t l = 0; l < naxes; ++l) {
    w[l] = c % smith[l];
    c /= smith[l];
  }
  return w;
}

auto Ctx::add(const vector<Uint64>& a, const vector<Uint64>& b) const -> vector<Uint64> {
  vector<Uint64> r(naxes);
  for (size_t l = 0; l < naxes; ++l) r[l] = (a[l] + b[l]) % smith[l];
  return r;
}

auto Ctx::addw(Uint64 c, const vector<Uint64>& w) const -> Uint64 {
  Uint64 r = 0;
  for (size_t l = 0; l < naxes; ++l) {
    Uint64 dg = c % smith[l];
    c /= smith[l];
    dg += w[l];
    if (dg >= smith[l]) dg -= smith[l];
    r += dg * Ms[l];
  }
  return r;
}

auto Ctx::scale(const vector<Uint64>& a, Uint64 k) const -> vector<Uint64> {
  vector<Uint64> r(naxes);
  for (size_t l = 0; l < naxes; ++l) r[l] = (a[l] % smith[l]) * (k % smith[l]) % smith[l];
  return r;
}

auto Ctx::word_of_odd(Uint64 k) const -> vector<Uint64> { return in.entry_word(k % d); }

auto Ctx::load(const string& path, const Trust& trust) -> void {
  in.open(path, trust);
  d = in.d;
  D = in.D;
  D0 = in.D0;
  t = in.t();
  deg = in.deg;
  smith = in.smith;
  naxes = smith.size();
  if (d < 4 || (d & (d - 1)))
    refuse("CRT certificate requires d = 2^s >= 4; general-d container accepted");
  if (2 * D > (Uint64(1) << 32)) refuse("2[L:K] > 2^32: point indices are 32-bit");
  N.assign(t + 1, 1);
  for (int j = 0; j < t; ++j) N[j + 1] = N[j] * deg[j];
  Ms.assign(naxes, 1);
  for (size_t l = 1; l < naxes; ++l) Ms[l] = Ms[l - 1] * smith[l - 1];
  co.resize(in.el.size());
  for (size_t i = 0; i < in.el.size(); ++i) {
    co[i] = std::make_unique<Coords>();
    in.coordinates(i, co[i]->X, co[i]->Y);
    fmpz_set_str(co[i]->u, in.el[i].scale.c_str(), 10);
    co[i]->v2 = fmpz_val2(co[i]->u);
  }
  wsq.assign(t, SIZE_MAX);
  fcoef.assign(t, {});
  actdelta.assign(t, SIZE_MAX);
  act.assign(t, vector<size_t>(naxes, SIZE_MAX));
  actodd.assign(t, vector<vector<size_t>>(naxes));
  for (int j = 0; j < t; ++j) {
    if (deg[j] == 2) {
      wsq[j] = in.find("wsq", j);
      if (wsq[j] == SIZE_MAX) refuse("missing wsq");
    } else {
      for (Uint64 k = 0; k < deg[j]; ++k) {
        fcoef[j].push_back(in.find("fcoef", j, -1, (Int64)k));
        if (fcoef[j].back() == SIZE_MAX) refuse("missing fcoef");
      }
      actdelta[j] = in.find("actdelta", j);
    }
    for (size_t l = 0; l < naxes; ++l) {
      if (deg[j] == 2)
        act[j][l] = in.find("act", j, (Int64)l);
      else if (in.find("actodd", j, (Int64)l, 0) != SIZE_MAX) {
        for (Uint64 k = 0; k < deg[j]; ++k)
          actodd[j][l].push_back(in.find("actodd", j, (Int64)l, (Int64)k));
        if (actdelta[j] == SIZE_MAX) refuse("actodd without actdelta");
      }
    }
  }
  for (auto& [pos, id] : in.rep) {
    reppos.push_back(pos);
    repid.push_back(id);
  }
  std::map<Int64, int> ri;
  for (size_t i = 0; i < reppos.size(); ++i) ri[reppos[i]] = (int)i;
  rep_of_m.assign(d, 0);
  wordm.assign(d, vector<Uint64>(naxes, 0));
  wc.assign(d, 0);
  for (Uint64 m = 1; m < d; ++m) {
    Uint64 r = in.entry_position(m);
    rep_of_m[m] = ri.at((Int64)r);
    wordm[m] = in.entry_word(m);
    wc[m] = lin(wordm[m]);
  }
  rep_of_m[0] = ri.at(0);
}

ModP::ModP(const Ctx& c) : C(c) {}

auto ModP::mul(Uint64 a, Uint64 b) const -> Uint64 { return nmod_mul(a, b, mod); }

auto ModP::add(Uint64 a, Uint64 b) const -> Uint64 { return nmod_add(a, b, mod); }

auto ModP::sub(Uint64 a, Uint64 b) const -> Uint64 { return nmod_sub(a, b, mod); }

auto ModP::inv(Uint64 a) const -> Uint64 { return n_invmod(a, p); }

auto ModP::red(const fmpz_t x) const -> Uint64 { return fmpz_fdiv_ui(x, p); }

auto ModP::transform(size_t id) const -> vector<Uint64> {
  const Elem& e = C.in.el[id];
  const Coords& c = *C.co[id];
  const Uint64 u = red(c.u);
  Uint64 P = 2, rest = e.ncoord;
  vector<Uint64> cur(rest * 2);
  for (Uint64 s = 0; s < rest; ++s) {
    const Uint64 x = red(&c.X[s]), y = red(&c.Y[s]);
    for (int k = 0; k < 2; ++k) cur[2 * s + k] = mul(u, add(x, mul(y, om[k])));
  }
  size_t li = 0;
  for (int j = 0; j < e.field; ++j) {
    const Uint64 pj = C.deg[j];
    const bool inJ = li < e.levels.size() && e.levels[li] == j;
    const Uint64 pk = inJ ? pj : 1;
    if (inJ) ++li;
    const Uint64 nrest = rest / pk, nP = P * pj;
    vector<Uint64> out(nrest * nP);
    const auto& Bj = B[j];
    for (Uint64 r = 0; r < nrest; ++r)
      for (Uint64 rho = 0; rho < pj; ++rho) {
        Uint64* o = &out[r * nP + P * rho];
        for (Uint64 k = 0; k < pk; ++k) {
          const Uint64* in = &cur[(r * pk + k) * P];
          const Uint64* b = &Bj[k][P * rho];
          if (k == 0)
            for (Uint64 psi = 0; psi < P; ++psi) o[psi] = mul(in[psi], b[psi]);
          else
            for (Uint64 psi = 0; psi < P; ++psi) o[psi] = add(o[psi], mul(in[psi], b[psi]));
        }
      }
    cur.swap(out);
    P = nP;
    rest = nrest;
  }
  return cur;
}

auto ModP::init(Uint64 prime, bool one_place) -> bool {
  p = prime;
  nmod_init(&mod, p);
  // omega^2 = omega + (D0-1)/4
  const Uint64 m = (Uint64)((C.D0 - 1) / 4) % p;
  const Uint64 disc = (1 + 4 * m) % p;  // = D0 mod p
  if (disc == 0 || n_jacobi((slong)disc, p) != 1) return false;
  const Uint64 s = n_sqrtmod(disc, p);
  inv2 = inv(2);
  om[0] = mul(sub(1, s), inv2);
  om[1] = one_place ? om[0] : mul(add(1, s), inv2);
  R.assign(C.t, {});
  B.assign(C.t, {});
  built = 0;
  return true;
}

auto ModP::build_level(int j) -> int {
  const Uint64 P = 2 * C.N[j], pj = C.deg[j];
  R[j].assign(P * pj, 0);
  B[j].assign(pj, vector<Uint64>(P * pj, 0));
  if (pj == 2) {
    const vector<Uint64> c = transform(C.wsq[j]);
    bool witness = false, ok = true;
    for (Uint64 psi = 0; psi < P; ++psi) {
      if (c[psi] == 0) return 0;
      if (n_jacobi((slong)c[psi], p) != 1) {
        witness = true;
        ok = false;
        break;
      }
    }
    if (!ok) return witness ? 2 : 0;
    for (Uint64 psi = 0; psi < P; ++psi) {
      const Uint64 s = n_sqrtmod(c[psi], p);
      if (s == 0 || mul(s, s) != c[psi]) return 0;
      R[j][psi] = s;
      R[j][psi + P] = p - s;
      const Uint64 i2s = inv(mul(2, s));
      B[j][0][psi] = inv2;
      B[j][0][psi + P] = inv2;
      B[j][1][psi] = i2s;
      B[j][1][psi + P] = p - i2s;
    }
  } else {
    vector<vector<Uint64>> a(pj);
    for (Uint64 k = 0; k < pj; ++k) a[k] = transform(C.fcoef[j][k]);
    nmod_poly_t f;
    nmod_poly_init(f, p);
    nmod_poly_factor_t fac;
    nmod_poly_factor_init(fac);
    int res = 1;
    for (Uint64 psi = 0; psi < P && res == 1; ++psi) {
      nmod_poly_zero(f);
      for (Uint64 k = 0; k < pj; ++k) nmod_poly_set_coeff_ui(f, k, a[k][psi]);
      nmod_poly_set_coeff_ui(f, pj, 1);
      nmod_poly_roots(fac, f, 1);
      bool distinct = fac->num == (slong)pj;
      for (slong i = 0; i < fac->num && distinct; ++i)
        if (fac->exp[i] != 1) distinct = false;
      if (!distinct) {
        res = nmod_poly_is_irreducible(f) ? 2 : 0;
        break;
      }
      vector<Uint64> roots;
      for (slong i = 0; i < fac->num; ++i)
        roots.push_back(
            nmod_neg(nmod_poly_get_coeff_ui(fac->p + i, 0), mod));  // monic linear X - r
      std::sort(roots.begin(), roots.end());
      for (Uint64 rho = 0; rho < pj; ++rho) {
        const Uint64 r = roots[rho];
        // f'(r) and q_k: q_{p-1} = 1, q_{k-1} = a_k + r q_k
        Uint64 fp = 0;
        for (Uint64 k = pj; k >= 1; --k)
          fp = add(mul(fp, r), k == pj ? pj % p : mul(k % p, a[k][psi]));
        if (fp == 0) {
          res = 0;
          break;
        }
        const Uint64 ifp = inv(fp);
        R[j][psi + P * rho] = r;
        Uint64 q = 1;
        for (Uint64 k = pj; k-- > 0;) {
          B[j][k][psi + P * rho] = mul(q, ifp);
          if (k == 0) break;
          q = add(a[k][psi], mul(r, q));
        }
      }
    }
    nmod_poly_factor_clear(fac);
    nmod_poly_clear(f);
    if (res != 1) return res;
    // the odd action records divide by delta: use only primes where it is a unit at every point
    if (C.actdelta[j] != SIZE_MAX) {
      const vector<Uint64> dl = transform(C.actdelta[j]);
      for (Uint64 x : dl)
        if (x == 0) return 0;
    }
  }
  built = j + 1;
  return 1;
}

int try_prime(ModP& M, Uint64 p, Witnesses& W, bool one_place, int maxlevel) {
  if (!M.init(p, one_place)) return -1;
  const int top = maxlevel >= 0 ? maxlevel : M.C.t;
  for (int j = 0; j < top; ++j) {
    g_reach[j]++;
    const int r = M.build_level(j);
    if (r == 2) {
      std::lock_guard<std::mutex> g(W.mu);
      if (!W.prime[j]) W.prime[j] = p;
    }
    if (r != 1) return j;
  }
  return top;
}
}  // namespace sic_check::crt
