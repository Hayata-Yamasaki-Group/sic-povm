// SPDX-License-Identifier: AGPL-3.0-or-later
#include "branches.hpp"

#include "../format/ball_codec.hpp"
#include "../format/canonical_json.hpp"
#include "../format/container.hpp"
#include "../format/integer_codec.hpp"
#include "../format/metadata.hpp"
#include "../format/sha256.hpp"
#include "../runtime/diagnostics.hpp"
#include "branches_numeric.hpp"
#ifdef SIC_CHECKER_FAULT_SEAMS
#include "../runtime/fault_seams.hpp"
#define _qqbar_roots_poly_squarefree sic_check::verifier::test::test_roots
#endif
namespace sic_check::verifier {
Algebraic::Algebraic() { qqbar_init(v); }

Algebraic::Algebraic(const Algebraic& x) {
  qqbar_init(v);
  qqbar_set(v, x.v);
}

Algebraic& Algebraic::operator=(const Algebraic& x) {
  qqbar_set(v, x.v);
  return *this;
}

Algebraic::~Algebraic() { qqbar_clear(v); }

AlgebraicArray::AlgebraicArray(slong count) : v(_qqbar_vec_init(count)), n(count) {}

AlgebraicArray::~AlgebraicArray() { _qqbar_vec_clear(v, n); }

string exact_component(const qqbar_t x, slong bits, arb_t interval) {
  Algebraic z, fraction, one;
  qqbar_one(one.v);
  qqbar_mul_2exp_si(z.v, x, bits);
  Integer q, m, E, r, e;
  qqbar_floor(q, z.v);
  qqbar_sub_fmpz(fraction.v, z.v, q);
  qqbar_mul_ui(fraction.v, fraction.v, 2);
  int cmp = qqbar_cmp_re(fraction.v, one.v);
  if (cmp > 0 || (cmp == 0 && fmpz_is_odd(q))) fmpz_add_ui(q, q, 1);
  qqbar_sub_fmpz(fraction.v, z.v, q);
  bool singleton = qqbar_is_zero(fraction.v);
  fmpz_set(m, q);
  fmpz_set_si(E, -bits);
  if (fmpz_is_zero(m))
    fmpz_zero(E);
  else {
    ulong v = fmpz_val2(m);
    fmpz_tdiv_q_2exp(m, m, v);
    fmpz_add_ui(E, E, v);
  }
  if (!singleton) {
    fmpz_one(r);
    fmpz_set_si(e, -bits);
  }
  arb_set_fmpz_2exp(interval, m, E);
  mag_set_fmpz_2exp_fmpz(arb_radref(interval), r, e);
  string result = hexz(m) + " " + hexz(E) + " " + hexz(r) + " " + hexz(e);
  return result;
}

void branch_check_exact(Container& C, const Opts& O) {
  Meta& M = C.M;
  if (M.D > O.branch_degree_limit)
    throw PrecErr("exact branch verification exceeds --branch-degree-limit (default 256)");
  for (const auto& ep : M.el)
    if (ep->kind != "rep" && (ep->ncoord > (1u << 18) || ep->max_bits > 16384))
      throw PrecErr("exact branch coefficient resource limit");
  Algebraic omega, sqrtD;
  qqbar_set_fmpz(sqrtD.v, M.D0);
  qqbar_sqrt(sqrtD.v, sqrtD.v);
  if (fmpz_is_one(M.D0))
    qqbar_zero(omega.v);
  else if (fmpz_fdiv_ui(M.D0, 4) == 1) {
    qqbar_ui_sub(omega.v, 1, sqrtD.v);
    qqbar_div_ui(omega.v, omega.v, 2);
  } else
    qqbar_neg(omega.v, sqrtD.v);
  C.base_roots.resize(M.t());
  vector<vector<Algebraic>> dual(M.t()), roots(M.t());
  vector<int> selected(M.t(), -1);
  auto eval = [&](int ei, qqbar_t out) {
    const Elem& el = *M.el[ei];
    qqbar_zero(out);
    vector<Uint8> buf;
    for (size_t ci : el.chunks) {
      const Chunk& c = C.leaf_chunks[ci];
      read_payload(C.f, c, buf);
      hash_check(sha(buf.data(), buf.size()), c.sha, "branch leaf reread");
      auto sink = [&](Uint64 sub, const fmpz_t x, const fmpz_t y) {
        Algebraic a, b;
        qqbar_mul_fmpz(a.v, omega.v, y);
        qqbar_add_fmpz(a.v, a.v, x);
        size_t j = 0;
        for (int i = 0; i < el.nfield; ++i) {
          Uint64 digit = 0;
          if (j < el.levels.size() && el.levels[j] == i) {
            digit = sub % M.deg[i];
            sub /= M.deg[i];
            ++j;
          }
          qqbar_mul(a.v, a.v, dual[i][digit].v);
        }
        qqbar_add(out, out, a.v);
      };
      parse_block(buf.data(), buf.size(), c.object, c.first, c.count, M.D0, true, sink);
    }
    qqbar_mul_fmpz(out, out, el.scale);
  };
  for (int i = 0; i < M.t(); ++i) {
    Uint64 p = M.deg[i];
    AlgebraicArray f(p + 1), rr(p);
    qqbar_one(f.v + p);
    if (p == 2) {
      eval(M.idx.at(ekey("wsq", i)), f.v);
      qqbar_neg(f.v, f.v);
    } else
      for (Uint64 k = 0; k < p; ++k) eval(M.idx.at(ekey("fcoef", i, -1, k)), f.v + k);
    {
      vector<Algebraic> a(p + 1), b(p);
      for (Uint64 k = 0; k <= p; ++k) qqbar_set(a[k].v, f.v + k);
      for (Uint64 k = 1; k <= p; ++k) qqbar_mul_ui(b[k - 1].v, f.v + k, k);
      auto trim = [](auto& v) {
        while (!v.empty() && qqbar_is_zero(v.back().v)) v.pop_back();
      };
      while (!b.empty()) {
        trim(a);
        Algebraic factor, term;
        while (a.size() >= b.size()) {
          size_t shift = a.size() - b.size();
          qqbar_div(factor.v, a.back().v, b.back().v);
          for (size_t k = 0; k < b.size(); ++k) {
            qqbar_mul(term.v, factor.v, b[k].v);
            qqbar_sub(a[shift + k].v, a[shift + k].v, term.v);
          }
          trim(a);
        }
        a.swap(b);
      }
      if (a.size() != 1) reject("tower polynomial has repeated roots");
    }
    if (!_qqbar_roots_poly_squarefree(rr.v, f.v, p + 1, O.branch_degree_limit * 2, 16384))
      throw PrecErr(
          "exact algebraic root isolation refused (degree/height or nonsimple polynomial)");
    roots[i].resize(p);
    int matches = 0;
    for (Uint64 j = 0; j < p; ++j) {
      if (qqbar_is_zero(rr.v + j)) reject("tower polynomial has a zero root");
      qqbar_set(roots[i][j].v, rr.v + j);
      for (Uint64 k = 0; k < j; ++k)
        if (qqbar_equal(rr.v + j, rr.v + k)) reject("tower polynomial has repeated roots");
      // Exact comparisons against the closed dyadic rectangle, including singleton components.
      bool inside = true;
      for (int part = 0; part < 2; ++part) {
        Algebraic component, lower, upper;
        if (part)
          qqbar_im(component.v, rr.v + j);
        else
          qqbar_re(component.v, rr.v + j);
        const string& dump = part ? M.branch_re[i].second : M.branch_re[i].first;
        Integer mant, exp, rad, rexp;
        parse_arbdump(dump, mant, exp, rad, rexp);
        Algebraic mid, radius;
        qqbar_set_fmpz(mid.v, mant);
        qqbar_mul_2exp_si(mid.v, mid.v, fmpz_get_si(exp));
        qqbar_set_fmpz(radius.v, rad);
        qqbar_mul_2exp_si(radius.v, radius.v, fmpz_get_si(rexp));
        qqbar_sub(lower.v, mid.v, radius.v);
        qqbar_add(upper.v, mid.v, radius.v);
        inside &=
            qqbar_cmp_re(component.v, lower.v) >= 0 && qqbar_cmp_re(component.v, upper.v) <= 0;
      }
      if (inside) {
        selected[i] = j;
        ++matches;
      }
    }
    if (matches != 1) reject("branch does not select exactly one exact root");
    // Euler relative duals, evaluated exactly at the selected root.
    const qqbar_struct* r = rr.v + selected[i];
    qqbar_set(C.base_roots[i].v, r);
    vector<Algebraic> q(p);
    qqbar_one(q[p - 1].v);
    for (int k = p - 1; k >= 1; --k) {
      qqbar_mul(q[k - 1].v, r, q[k].v);
      qqbar_add(q[k - 1].v, q[k - 1].v, f.v + k);
    }
    Algebraic derivative, term;
    qqbar_zero(derivative.v);
    for (int k = p; k >= 1; --k) {
      qqbar_mul(derivative.v, derivative.v, r);
      qqbar_mul_ui(term.v, f.v + k, k);
      qqbar_add(derivative.v, derivative.v, term.v);
    }
    if (qqbar_is_zero(derivative.v)) reject("selected root is not simple");
    dual[i].resize(p);
    for (Uint64 k = 0; k < p; ++k) qqbar_div(dual[i][k].v, q[k].v, derivative.v);
  }
  for (Uint64 bits = 128; bits <= M.branch_bits; bits *= 2) {
    bool admissible = true;
    vector<std::pair<string, string>> stored(M.t());
    for (int i = 0; i < M.t(); ++i) {
      vector<ACB> boxes(roots[i].size());
      for (size_t j = 0; j < roots[i].size(); ++j) {
        Algebraic re, im;
        qqbar_re(re.v, roots[i][j].v);
        qqbar_im(im.v, roots[i][j].v);
        string a = exact_component(re.v, bits, acb_realref(boxes[j].v));
        string b = exact_component(im.v, bits, acb_imagref(boxes[j].v));
        if (int(j) == selected[i]) stored[i] = {a, b};
        if (acb_contains_zero(boxes[j].v)) admissible = false;
        for (size_t k = 0; k < j; ++k)
          if (acb_overlaps(boxes[j].v, boxes[k].v)) admissible = false;
      }
    }
    if (admissible) {
      if (bits != M.branch_bits) reject("branch_bits is not the minimum canonical precision");
      if (stored != M.branch_re)
        reject("branches differ from exact canonical midpoint/radius rule");
      say("  canonical branches verified by exact algebraic comparisons at %llu bits\n",
          (unsigned long long)bits);
      return;
    }
  }
  reject("branch_bits does not separate all nonzero roots");
}

void branch_check(Container& C, const Opts& O) {
  if (!numeric_branch_check(C)) branch_check_exact(C, O);
}
}  // namespace sic_check::verifier
