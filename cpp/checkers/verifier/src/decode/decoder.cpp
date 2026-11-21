// SPDX-License-Identifier: AGPL-3.0-or-later
#include "decoder.hpp"

#include "../format/ball_codec.hpp"
#include "../format/canonical_json.hpp"
#include "../format/container.hpp"
#include "../format/integer_codec.hpp"
#include "../format/metadata.hpp"
#include "../format/sha256.hpp"
#include "../format/units.hpp"
#include "../runtime/diagnostics.hpp"
#ifdef SIC_CHECKER_FAULT_SEAMS
#include "../runtime/fault_seams.hpp"
#define acb_is_finite sic_check::verifier::test::test_finite
#define acb_poly_find_roots sic_check::verifier::test::test_find_roots
#endif
namespace sic_check::verifier {
Decoder::Decoder(Container& c) : C(c), M(c.M), t(c.M.t()) {
  arb_init(omega);
  acb_init(omegac);
  cplx = fmpz_sgn(M.D0) < 0;
  branch.resize(t);
  for (int i = 0; i < t; i++) {
    for (int part = 0; part < 2; part++) {
      arb_ptr x = part ? acb_imagref(branch[i].v) : acb_realref(branch[i].v);
      const string& s = part ? M.branch_re[i].second : M.branch_re[i].first;
      Integer m1, e1, m2, e2;
      parse_arbdump(s, m1, e1, m2, e2);  // own parser, spec 7a
      arb_set_fmpz_2exp(x, m1, e1);
      mag_set_fmpz_2exp_fmpz(arb_radref(x), m2, e2);
    }
  }
}

Decoder::~Decoder() {
  arb_clear(omega);
  acb_clear(omegac);
}

auto Decoder::set_prec(slong p) -> void {
  prec = p;
  arb_t s;
  arb_init(s);
  if (fmpz_is_one(M.D0)) {
    arb_zero(omega);
  } else if (!cplx) {
    arb_sqrt_fmpz(s, M.D0, p + 32);
    arb_neg(omega, s);
    if (fmpz_fdiv_ui(M.D0, 4) == 1) {
      arb_add_ui(omega, omega, 1, p + 32);
      arb_mul_2exp_si(omega, omega, -1);
    }
  } else {
    Integer a;
    fmpz_neg(a, M.D0);
    arb_sqrt_fmpz(s, a, p + 32);
    acb_zero(omegac);
    arb_neg(acb_imagref(omegac), s);
    if (fmpz_fdiv_ui(M.D0, 4) == 1) {
      arb_set_d(acb_realref(omegac), 0.5);
      arb_mul_2exp_si(acb_imagref(omegac), acb_imagref(omegac), -1);
    }
  }
  arb_clear(s);
}

auto Decoder::id(const char* kind, Int64 a, Int64 b, Int64 c) -> int {
  auto it = M.idx.find(ekey(kind, a, b, c));
  return it == M.idx.end() ? -1 : it->second;
}

auto Decoder::stage(vector<ACB>& cur, const vector<ACB>& bd) -> void {
  size_t p = bd.size(), m = cur.size() / p;
  vector<ACB> nx(m);
  for (size_t s = 0; s < m; s++)
    for (size_t d = 0; d < p; d++) acb_addmul(nx[s].v, bd[d].v, cur[s * p + d].v, prec);
  cur.swap(nx);
}

auto Decoder::eval_elem(int e, const RootSet& rs, acb_t out) -> void {
  Elem& el = *M.el[e];
  int n = el.nfield;
  auto& J = el.levels;
  ACB pre;
  acb_one(pre);
  {
    size_t j = 0;
    for (int i = 0; i < n; i++) {
      if (j < J.size() && J[j] == i) {
        j++;
        continue;
      }
      acb_mul(pre, pre, rs.b[i][0].v, prec);
    }
  }
  size_t k = 0;
  {
    Uint64 W = 1;
    while (k < J.size() && W * M.deg[J[k]] <= el.W) {
      W *= M.deg[J[k]];
      k++;
    }
  }
  vector<ACB> B(el.nblocks);
  vector<Uint8> buf;
  Real val;
  for (size_t bi = 0; bi < el.chunks.size(); bi++) {
    const Chunk& c = C.leaf_chunks[el.chunks[bi]];
    read_payload(C.f, c, buf);
    hash_check(sha(buf.data(), buf.size()), c.sha,
               "decode read of chunk " + std::to_string(el.chunks[bi]));
    vector<ACB> cur;
    if (k > 0) cur.resize(el.W / M.deg[J[0]]);
    Uint64 p0 = k > 0 ? M.deg[J[0]] : 1;
    ACB valc, tmpc;
    auto sink = [&](Uint64 s, const fmpz_t x, const fmpz_t y) {
      Uint64 sr = s - c.first;
      if (!cplx) {
        arb_set_fmpz(val, x);
        arb_addmul_fmpz(val, omega, y, prec);
        if (k == 0) {
          arb_set(acb_realref(B[bi].v), val);
        } else
          acb_addmul_arb(cur[sr / p0].v, rs.b[J[0]][sr % p0].v, val, prec);
      } else {
        acb_set_fmpz(valc, x);
        acb_mul_fmpz(tmpc, omegac, y, prec);
        acb_add(valc, valc, tmpc, prec);
        if (k == 0)
          acb_set(B[bi].v, valc);
        else
          acb_addmul(cur[sr / p0].v, rs.b[J[0]][sr % p0].v, valc, prec);
      }
    };
    parse_block(buf.data(), buf.size(), c.object, c.first, c.count, M.D0, true, sink);
    if (k > 0) {
      for (size_t j = 1; j < k; j++) stage(cur, rs.b[J[j]]);
      acb_swap(B[bi].v, cur[0].v);
    }
  }
  for (size_t j = k; j < J.size(); j++) stage(B, rs.b[J[j]]);
  acb_mul(out, pre, B[0].v, prec);
  acb_mul_fmpz(out, out, el.scale, prec);
}

auto Decoder::horner(acb_t y, acb_t dy, const vector<ACB>& f, const acb_t x) -> void {
  acb_zero(y);
  acb_zero(dy);
  for (int k = (int)f.size() - 1; k >= 0; k--) {
    acb_mul(dy, dy, x, prec);
    acb_add(dy, dy, y, prec);
    acb_mul(y, y, x, prec);
    acb_add(y, y, f[k].v, prec);
  }
}

auto Decoder::newton(const vector<ACB>& f, const acb_t z, acb_t out) -> void {
  ACB best;
  acb_set(best.v, z);
  if (acb_rel_accuracy_bits(best.v) >
      prec / 2) {  // inflate: Newton cannot return a ball tighter than the evaluation error
    mag_t mg, one;
    mag_init(mg);
    mag_init(one);
    acb_get_mag(mg, best.v);
    mag_one(one);
    mag_max(mg, mg, one);
    mag_mul_2exp_si(mg, mg, -(Int64)(prec / 2));
    acb_add_error_mag(best.v, mg);
    mag_clear(mg);
    mag_clear(one);
  }
  ACB m, y, dy, y2, N;
  slong acc_old = -1;
  bool ok = false;
  for (int it = 0; it < 40; it++) {
    acb_get_mid(m.v, best.v);
    horner(y.v, dy.v, f, m.v);
    horner(y2.v, dy.v, f, best.v);
    if (acb_contains_zero(dy.v)) throw PrecErr("Newton: f'(Z) contains zero");
    acb_div(N.v, y.v, dy.v, prec);
    acb_sub(N.v, m.v, N.v, prec);
    if (!(ok ? acb_contains(best.v, N.v) : acb_contains_interior(best.v, N.v))) {
      if (!ok)
        throw PrecErr("Newton: step not contained (candidate ball too wide or precision low)");
      break;
    }
    ok = true;
    acb_set(best.v, N.v);
    slong acc = acb_rel_accuracy_bits(best.v);
    if (acc_old == std::numeric_limits<slong>::max() || acc <= acc_old + 1) break;
    acc_old = acc;
  }
  acb_set(out, best.v);
}

auto Decoder::make_duals(RootSet& rs, int i) -> void {
  Uint64 p = M.deg[i];
  auto& b = rs.b[i];
  b.assign(p, ACB());
  const acb_t& r = rs.r[i].v;
  if (p == 2) {
    acb_set_d(b[0].v, 0.5);
    acb_inv(b[1].v, r, prec);
    acb_mul_2exp_si(b[1].v, b[1].v, -1);
    return;
  }
  const vector<ACB>& f = rs.a[i];
  vector<ACB> q(p);
  acb_one(q[p - 1].v);
  for (int k = (int)p - 1; k >= 1; k--) {
    acb_mul(q[k - 1].v, r, q[k].v, prec);
    acb_add(q[k - 1].v, q[k - 1].v, f[k].v, prec);
  }
  ACB fp, y;
  acb_t dy;
  acb_init(dy);
  horner(y.v, fp.v, f, r);
  acb_clear(dy);
  for (Uint64 k = 0; k < p; k++) acb_div(b[k].v, q[k].v, fp.v, prec);
}

auto Decoder::build_level(RootSet& rs, int i, const ACB& z) -> void {
  Uint64 p = M.deg[i];
  vector<ACB> f(p + 1);
  if (p == 2) {
    int e = id("wsq", i);
    ACB c;
    eval_elem(e, rs, c.v);
    acb_neg(f[0].v, c.v);
    acb_one(f[2].v);
  } else {
    for (Uint64 k = 0; k < p; k++) eval_elem(id("fcoef", i, -1, k), rs, f[k].v);
    acb_one(f[p].v);
  }
  ACB root;
  try {
    newton(f, z.v, root.v);
  } catch (PrecErr& e) {
    std::unique_ptr<char, decltype(&flint_free)> zs(arb_get_str(acb_realref(z.v), 12, 0),
                                                    flint_free);
    string m = string(e.what()) + " [level " + std::to_string(i) + ", candidate re " + zs.get() +
               ", rel.acc " + std::to_string(acb_rel_accuracy_bits(z.v)) + "]";
    throw PrecErr(m);
  }
  rs.r[i] = root;
  rs.a[i] = f;
  make_duals(rs, i);
}

auto Decoder::base() -> RootSet {
  RootSet rs;
  rs.r.resize(t);
  rs.a.resize(t);
  rs.b.resize(t);
  if (C.base_roots.size() != size_t(t)) return numeric_base(C, prec, nullptr);
  for (int i = 0; i < t; i++) {
    ACB z;
    qqbar_get_acb(z.v, C.base_roots[i].v, prec);
    build_level(rs, i, z);
    // Exact singleton equality was proved by branch_check. Numerical refinement alone
    // cannot establish it; reinsert those exact components and require full containment.
    for (int part = 0; part < 2; ++part) {
      arb_srcptr bound = part ? acb_imagref(branch[i].v) : acb_realref(branch[i].v);
      arb_ptr value = part ? acb_imagref(rs.r[i].v) : acb_realref(rs.r[i].v);
      if (arb_is_exact(bound)) arb_set(value, bound);
      if (!arb_contains(bound, value)) throw PrecErr("base root enclosure not contained in branch");
    }
    make_duals(rs, i);
  }
  return rs;
}

auto Decoder::apply_generator(const RootSet& old, int l) -> RootSet {
  RootSet rs;
  rs.r.resize(t);
  rs.a.resize(t);
  rs.b.resize(t);
  for (int i = 0; i < t; i++) {
    Uint64 p = M.deg[i];
    ACB z = old.r[i];
    if (p == 2) {
      int e = id("act", i, l);
      if (e >= 0) {
        ACB A;
        eval_elem(e, old, A.v);
        if (acb_contains_zero(old.r[i].v))
          throw PrecErr("quadratic action denominator contains zero");
        acb_div(z.v, A.v, old.r[i].v, prec);
      }
    } else if (id("actodd", i, l, 0) >= 0) {
      ACB dl;
      eval_elem(id("actdelta", i), old, dl.v);
      if (acb_contains_zero(dl.v)) throw PrecErr("odd action delta contains zero");
      acb_zero(z.v);
      ACB pw;
      acb_one(pw.v);
      for (Uint64 k = 0; k < p; k++) {
        ACB ak;
        eval_elem(id("actodd", i, l, k), old, ak.v);
        acb_div(ak.v, ak.v, dl.v, prec);
        acb_addmul(z.v, ak.v, pw.v, prec);
        acb_mul(pw.v, pw.v, old.r[i].v, prec);
      }
    }
    build_level(rs, i, z);
  }
  return rs;
}

auto Decoder::roots_for_word(const RootSet& base_rs, const vector<Uint64>& w) -> RootSet {
  RootSet cur = base_rs;
  bool first = true;
  for (size_t l = 0; l < w.size(); l++)
    for (Uint64 s = 0; s < w[l]; s++) {
      cur = apply_generator(cur, (int)l);
      first = false;
    }
  (void)first;
  return cur;
}

auto Decoder::entry_word(Uint64 m, Uint64& rep, vector<Uint64>& word) -> void {
  word.assign(M.smith.size(), 0);
  if (m >= M.d) throw UsageErr("entry coordinate outside dimension");
  Uint64 g = std::gcd(m, M.d), n = M.d / g;
  rep = g == M.d ? 0 : g;
  Uint64 lift = n == 1 ? 1 : m / g;
  while (std::gcd(lift, M.d) != 1) lift += n;
  auto exponents = unit_exponents(lift, M.units);
  for (size_t j = 0; j < exponents.size(); ++j)
    for (size_t l = 0; l < word.size(); ++l)
      word[l] = (word[l] + exponents[j] * M.unit_words[j][l]) % M.smith[l];
}

void certify_real_roots(const vector<ACB>& f, vector<ACB>& roots) {
  for (const auto& coefficient : f)
    if (!arb_is_zero(acb_imagref(coefficient.v))) return;
  vector<bool> real(roots.size(), true);
  for (size_t j = 0; j < roots.size(); ++j) {
    ACB conjugate;
    acb_conj(conjugate.v, roots[j].v);
    for (size_t k = 0; k < roots.size(); ++k)
      if (k != j && acb_overlaps(conjugate.v, roots[k].v)) real[j] = false;
  }
  for (size_t j = 0; j < roots.size(); ++j)
    if (real[j]) arb_zero(acb_imagref(roots[j].v));
}

RootSet numeric_base(Container& C, slong prec, vector<vector<ACB>>* all) {
  Decoder D(C);
  D.set_prec(prec);
  RootSet rs;
  rs.r.resize(D.t);
  rs.a.resize(D.t);
  rs.b.resize(D.t);
  if (all) all->resize(D.t);
  for (int i = 0; i < D.t; ++i) {
    Uint64 p = D.M.deg[i];
    auto& f = rs.a[i];
    f.resize(p + 1);
    acb_one(f[p].v);
    vector<ACB> rr(p);
    if (p == 2) {
      ACB c;
      D.eval_elem(D.id("wsq", i), rs, c.v);
      acb_neg(f[0].v, c.v);
      if (acb_contains_zero(c.v)) throw PrecErr("quadratic coefficient contains zero");
      if (arb_is_negative(acb_realref(c.v))) {
        acb_neg(c.v, c.v);
        acb_sqrt(rr[0].v, c.v, prec);
        acb_mul_onei(rr[0].v, rr[0].v);
      } else
        acb_sqrt(rr[0].v, c.v, prec);
      acb_neg(rr[1].v, rr[0].v);
    } else {
      for (Uint64 k = 0; k < p; ++k) D.eval_elem(D.id("fcoef", i, -1, k), rs, f[k].v);
      acb_poly_t poly;
      acb_poly_init(poly);
      for (Uint64 k = 0; k <= p; ++k) acb_poly_set_coeff_acb(poly, k, f[k].v);
      acb_ptr result = _acb_vec_init(p);
      slong found = acb_poly_find_roots(result, poly, nullptr, 0, prec);
      if (found == slong(p))
        for (Uint64 k = 0; k < p; ++k) acb_set(rr[k].v, result + k);
      _acb_vec_clear(result, p);
      acb_poly_clear(poly);
      if (found != slong(p)) throw PrecErr("odd roots not all certified simple");
    }
    for (Uint64 j = 0; j < p; ++j) {
      if (!acb_is_finite(rr[j].v) || acb_contains_zero(rr[j].v))
        throw PrecErr("root isolation finite/nonzero unresolved");
      for (Uint64 k = 0; k < j; ++k)
        if (acb_overlaps(rr[j].v, rr[k].v)) throw PrecErr("root isolation separation unresolved");
    }
    certify_real_roots(f, rr);
    int selected = -1, matches = 0;
    for (Uint64 j = 0; j < p; ++j) {
      if (acb_contains(D.branch[i].v, rr[j].v)) {
        selected = j;
        ++matches;
      } else if (acb_overlaps(D.branch[i].v, rr[j].v))
        throw PrecErr("base-root rectangle containment/equality unresolved");
    }
    if (matches != 1) reject("branch does not select exactly one certified root");
    rs.r[i] = rr[selected];
    D.make_duals(rs, i);
    if (all) (*all)[i] = rr;
  }
  return rs;
}
}  // namespace sic_check::verifier
