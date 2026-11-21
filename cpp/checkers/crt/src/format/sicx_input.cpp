// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx_input.hpp"

#include "../runtime/diagnostics.hpp"
#include "canonical_json.hpp"
#include "container.hpp"
#include "integer_codec.hpp"
#include "metadata.hpp"
namespace sic_check::crt::sicx {

auto Input::integral_shift_hints() const -> vector<Uint64> {
  for (Uint64 shift : shifts)
    if (shift > 64) return vector<Uint64>(deg.size(), 0);
  return shifts;
}

auto Input::algebraic_limit(const AlgebraicNumber& a) const -> void {
  if (!qqbar_within_limits(a.v, limits.algebraic_degree, limits.algebraic_bits))
    resource("exact branch algebraic degree/height limit");
}

auto Input::ceil_log2(Uint64 n) -> Uint64 {
  Uint64 bits = 0, power = 1;
  while (power < n) {
    power = mul(power, 2);
    ++bits;
  }
  return bits;
}

auto Input::factor_allowance(Uint64 degree) -> Uint64 {
  return add(add(degree, ceil_log2(add(degree, 1))), 2);
}

auto Input::sqrt_checked(const AlgebraicNumber& x) const -> AlgebraicNumber {
  algebraic_limit(x);
  Uint64 degree = mul(2, qqbar_degree(x.v));
  if (degree > (Uint64)limits.algebraic_degree ||
      add(qqbar_height_bits(x.v), factor_allowance(degree)) > (Uint64)limits.algebraic_bits)
    resource("quadratic branch degree/height admission");
  AlgebraicNumber result;
  qqbar_sqrt(result.v, x.v);
  algebraic_limit(result);
  return result;
}

auto Input::op(const AlgebraicNumber& a, const AlgebraicNumber& b, char operation) const
    -> AlgebraicNumber {
  algebraic_limit(a);
  algebraic_limit(b);
  if (!qqbar_binop_within_limits(a.v, b.v, limits.algebraic_degree, limits.algebraic_bits))
    resource("exact branch operation degree/height admission");
  Uint64 m = qqbar_degree(a.v), n = qqbar_degree(b.v);
  Uint64 ha = qqbar_height_bits(a.v), hb = qqbar_height_bits(b.v), degree = mul(m, n);
  // Cauchy bounds give |alpha| < 2^(ha+1), also for inverses of nonzero roots.
  // A generic sum/product/quotient annihilator has leading coefficient at most
  // 2^(n*ha+m*hb). Bounding its symmetric sums (and those of its primitive
  // minimal-polynomial factor) admits the following coarse height budget.
  Uint64 height = add(add(mul(n, ha), mul(m, hb)), add(mul(degree, add(add(ha, hb), 3)), 1));
  height = add(height, factor_allowance(degree));
  if (height > (Uint64)limits.algebraic_bits) resource("exact branch resultant height admission");
  AlgebraicNumber r;
  switch (operation) {
    case '+':
      qqbar_add(r.v, a.v, b.v);
      break;
    case '-':
      qqbar_sub(r.v, a.v, b.v);
      break;
    case '*':
      qqbar_mul(r.v, a.v, b.v);
      break;
    default:
      if (qqbar_is_zero(b.v)) reject("zero trace-dual denominator");
      qqbar_div(r.v, a.v, b.v);
  }
  algebraic_limit(r);
  return r;
}

auto Input::scaled(const AlgebraicNumber& a, Int64 exponent) const -> AlgebraicNumber {
  if (qqbar_is_zero(a.v)) return a;
  if (exponent > limits.algebraic_bits || exponent < -limits.algebraic_bits)
    resource("exact branch dyadic exponent limit");
  if (add(qqbar_height_bits(a.v),
          mul(qqbar_degree(a.v), (Uint64)(exponent < 0 ? -exponent : exponent))) >
      (Uint64)limits.algebraic_bits)
    resource("exact branch dyadic height admission");
  AlgebraicNumber r;
  qqbar_mul_2exp_si(r.v, a.v, exponent);
  algebraic_limit(r);
  return r;
}

auto Input::evaluate(size_t id, const vector<vector<AlgebraicNumber>>& dual,
                     const AlgebraicNumber& omega) const -> AlgebraicNumber {
  const auto& e = el[id];
  AlgebraicNumber sum;
  Z scale;
  fmpz_set_str(scale.v, e.scale.c_str(), 10);
  for (const auto& p : element(id)) {
    AlgebraicNumber x, y, u;
    qqbar_set_fmpz(x.v, p.x.v);
    qqbar_set_fmpz(y.v, p.y.v);
    qqbar_set_fmpz(u.v, scale.v);
    AlgebraicNumber term = op(op(x, op(y, omega, '*'), '+'), u, '*');
    Uint64 sub = p.sub;
    size_t active = 0;
    for (int j = 0; j < e.field; ++j) {
      Uint64 digit = 0;
      if (active < e.levels.size() && e.levels[active] == j) {
        digit = sub % deg[j];
        sub /= deg[j];
        ++active;
      }
      term = op(term, dual.at(j).at(digit), '*');
    }
    sum = op(sum, term, '+');
  }
  return sum;
}

auto Input::zhex(const fmpz_t z) -> string {
  std::unique_ptr<char, decltype(&flint_free)> p(fmpz_get_str(nullptr, 16, z), flint_free);
  string s(p.get());
  return s;
}

auto Input::real_enclosure(arb_t enclosure, const AlgebraicNumber& x, Uint64 work) const -> void {
  if (qqbar_is_rational(x.v)) {
    arb_set_fmpz(enclosure, QQBAR_COEFFS(x.v));
    arb_div_fmpz(enclosure, enclosure, QQBAR_COEFFS(x.v) + 1, (slong)work);
    arb_neg(enclosure, enclosure);
    return;
  }
  // All callers supply an exactly real algebraic number. Its Qbar rectangle already
  // contains that root. Real interval Newton preserves it by the mean-value theorem.
  // Use a fixed number of iterations at the admitted precision: Qbar's convenience
  // conversion can silently increase precision and construct dyadic candidates.
  arb_set(enclosure, acb_realref(QQBAR_ENCLOSURE(x.v)));
  A value, derivative, midpoint, at_midpoint, image, next;
  for (Uint64 iteration = 0; iteration < add(ceil_log2(add(work, 1)), 2); ++iteration) {
    arb_zero(value.v);
    arb_zero(derivative.v);
    arb_zero(at_midpoint.v);
    arb_get_mid_arb(midpoint.v, enclosure);
    for (slong j = QQBAR_POLY(x.v)->length; j-- > 0;) {
      arb_mul(derivative.v, derivative.v, enclosure, (slong)work);
      arb_add(derivative.v, derivative.v, value.v, (slong)work);
      arb_mul(value.v, value.v, enclosure, (slong)work);
      arb_add_fmpz(value.v, value.v, QQBAR_COEFFS(x.v) + j, (slong)work);
      arb_mul(at_midpoint.v, at_midpoint.v, midpoint.v, (slong)work);
      arb_add_fmpz(at_midpoint.v, at_midpoint.v, QQBAR_COEFFS(x.v) + j, (slong)work);
    }
    if (arb_contains_zero(derivative.v)) break;
    arb_div(image.v, at_midpoint.v, derivative.v, (slong)work);
    arb_sub(image.v, midpoint.v, image.v, (slong)work);
    if (!arb_is_finite(image.v)) break;
    if (!arb_intersection(next.v, enclosure, image.v, (slong)work))
      resource("algebraic interval refinement failed");
    if (arb_equal(next.v, enclosure)) break;
    arb_set(enclosure, next.v);
  }
  if (!arb_is_finite(enclosure)) resource("nonfinite algebraic comparison enclosure");
}

auto Input::real_sign(const AlgebraicNumber& x) const -> int {
  if (qqbar_is_zero(x.v)) return 0;
  if (qqbar_is_rational(x.v)) return -fmpz_sgn(QQBAR_COEFFS(x.v)) * fmpz_sgn(QQBAR_COEFFS(x.v) + 1);
  for (Uint64 work = std::min<Uint64>(128, limits.branch_bits);;) {
    A enclosure;
    real_enclosure(enclosure.v, x, work);
    if (arb_is_positive(enclosure.v)) return 1;
    if (arb_is_negative(enclosure.v)) return -1;
    if (work == limits.branch_bits) resource("exact branch comparison precision limit");
    work = std::min(limits.branch_bits, mul(work, 2));
  }
}

auto Input::component_box(const AlgebraicNumber& x, Uint64 p) const -> string {
  AlgebraicNumber y = scaled(x, (Int64)p);
  Z q;
  bool exact = false;
  if (qqbar_is_rational(y.v)) {
    qqbar_floor(q.v, y.v);  // rational Euclidean division; no numerical refinement
    AlgebraicNumber floor;
    qqbar_set_fmpz(floor.v, q.v);
    AlgebraicNumber fraction = scaled(op(y, floor, '-'), 1), one(1);
    int cmp = real_sign(op(fraction, one, '-'));
    if (cmp > 0 || (cmp == 0 && fmpz_is_odd(q.v))) fmpz_add_ui(q.v, q.v, 1);
    AlgebraicNumber nearest;
    qqbar_set_fmpz(nearest.v, q.v);
    exact = qqbar_equal(y.v, nearest.v);
  } else {
    // An irrational algebraic component cannot equal a dyadic grid point or a tie.
    // Prove its nearest integer by bounded certified refinement, never an unbounded floor call.
    AlgebraicNumber shifted = op(y, scaled(AlgebraicNumber(1), -1), '+');
    for (Uint64 work = std::min(limits.branch_bits, add(p, 64));;) {
      A enclosure;
      real_enclosure(enclosure.v, shifted, work);
      arf_t lo, hi;
      arf_init(lo);
      arf_init(hi);
      arb_get_lbound_arf(lo, enclosure.v, (slong)work);
      arb_get_ubound_arf(hi, enclosure.v, (slong)work);
      Z lower, upper;
      arf_get_fmpz(lower.v, lo, ARF_RND_FLOOR);
      arf_get_fmpz(upper.v, hi, ARF_RND_FLOOR);
      arf_clear(lo);
      arf_clear(hi);
      if (fmpz_equal(lower.v, upper.v)) {
        fmpz_set(q.v, lower.v);
        break;
      }
      if (work == limits.branch_bits) resource("canonical branch rounding precision limit");
      work = std::min(limits.branch_bits, mul(work, 2));
    }
  }
  Int64 E = -(Int64)p;
  if (fmpz_is_zero(q.v))
    E = 0;
  else {
    Uint64 v = fmpz_val2(q.v);
    fmpz_tdiv_q_2exp(q.v, q.v, v);
    E += (Int64)v;
  }
  Z exp(E), radexp(exact ? 0 : -(Int64)p);
  return zhex(q.v) + " " + zhex(exp.v) + (exact ? " 0 " : " 1 ") + zhex(radexp.v);
}

auto Input::parts(AlgebraicNumber& re, AlgebraicNumber& im, const AlgebraicNumber& z) const
    -> void {
  if (arb_is_zero(acb_imagref(QQBAR_ENCLOSURE(z.v)))) {
    re = z;
    im = AlgebraicNumber();
    return;
  }
  AlgebraicNumber conjugate, imaginary;
  qqbar_conj(conjugate.v, z.v);  // same integer polynomial and degree
  imaginary = sqrt_checked(AlgebraicNumber(-1));
  re = scaled(op(z, conjugate, '+'), -1);
  im = op(scaled(op(z, conjugate, '-'), -1), imaginary, '/');
}

auto Input::root_box(const AlgebraicNumber& z, Uint64 p) const -> std::array<string, 2> {
  AlgebraicNumber re, im;
  parts(re, im, z);
  return {component_box(re, p), component_box(im, p)};
}

auto Input::component_contains(const string& s, const AlgebraicNumber& x) const -> bool {
  BallString b(s);
  AlgebraicNumber m, r;
  qqbar_set_fmpz(m.v, b.M.v);
  qqbar_set_fmpz(r.v, b.m.v);
  m = scaled(m, fmpz_get_si(b.E.v));
  r = scaled(r, fmpz_get_si(b.e.v));
  AlgebraicNumber lo = op(m, r, '-'), hi = op(m, r, '+');
  return real_sign(op(x, lo, '-')) >= 0 && real_sign(op(hi, x, '-')) >= 0;
}

auto Input::box_contains(const std::array<string, 2>& b, const AlgebraicNumber& z) const -> bool {
  AlgebraicNumber re, im;
  parts(re, im, z);
  return component_contains(b[0], re) && component_contains(b[1], im);
}

auto Input::boxes_overlap(const std::array<string, 2>& a, const std::array<string, 2>& b) -> bool {
  for (int k = 0; k < 2; ++k) {
    A x, y;
    BallString(a[k]).ball(x.v);
    BallString(b[k]).ball(y.v);
    if (!arb_overlaps(x.v, y.v)) return false;
  }
  return true;
}

auto Input::algebraic_branch_level(int i, const vector<vector<AlgebraicNumber>>& dual,
                                   const AlgebraicNumber& omega) const -> BranchLevel {
  Uint64 p = deg[i];
  vector<AlgebraicNumber> coef(p + 1);
  coef[p] = AlgebraicNumber(1);
  if (p == 2) {
    coef[0] = evaluate(find("wsq", i), dual, omega);
    qqbar_neg(coef[0].v, coef[0].v);
  } else
    for (Uint64 k = 0; k < p; ++k) coef[k] = evaluate(find("fcoef", i, -1, k), dual, omega);
  vector<AlgebraicNumber> roots(p);
  if (p == 2) {
    if (qqbar_is_zero(coef[0].v)) reject("zero/non-simple quadratic root");
    AlgebraicNumber c;
    qqbar_neg(c.v, coef[0].v);
    roots[0] = sqrt_checked(c);
    qqbar_neg(roots[1].v, roots[0].v);
  } else {
    // A squarefree polynomial has no common zero with its derivative. Euclidean division over
    // Qbar proves this before calling FLINT's squarefree-root routine (which requires that
    // premise).
    vector<AlgebraicNumber> a = coef, b(p);
    for (Uint64 k = 1; k <= p; ++k) b[k - 1] = op(coef[k], AlgebraicNumber(k), '*');
    auto trim = [](vector<AlgebraicNumber>& v) {
      while (!v.empty() && qqbar_is_zero(v.back().v)) v.pop_back();
    };
    trim(b);
    while (!b.empty()) {
      vector<AlgebraicNumber> r = a;
      while (r.size() >= b.size()) {
        AlgebraicNumber factor = op(r.back(), b.back(), '/');
        size_t shift = r.size() - b.size();
        for (size_t k = 0; k < b.size(); ++k)
          r[k + shift] = op(r[k + shift], op(factor, b[k], '*'), '-');
        trim(r);
      }
      a = b;
      b = std::move(r);
    }
    if (a.size() != 1) reject("defining polynomial has multiple roots");
    Uint64 combinations = 1, height_sum = 0, height_max = 0;
    Uint64 combination_limit = (Uint64)limits.algebraic_degree / p;
    if (!combination_limit) resource("odd branch degree admission");
    for (const auto& coefficient : coef) {
      algebraic_limit(coefficient);
      if ((Uint64)qqbar_degree(coefficient.v) > combination_limit / combinations)
        resource("odd branch coefficient-conjugate degree admission");
      combinations = mul(combinations, qqbar_degree(coefficient.v));
      height_sum = add(height_sum, qqbar_height_bits(coefficient.v));
      height_max = std::max<Uint64>(height_max, qqbar_height_bits(coefficient.v));
    }
    Uint64 degree_bound = mul(p, combinations);
    // Multiply the coefficient-conjugate polynomials by the product of their
    // leading denominators. A polynomial 1-norm bound admits integer height
    // M*(sum H_i + max H_i + 1 + ceil(log2(p+1))) + 1, plus factor allowance.
    Uint64 height_bound =
        add(mul(combinations, add(add(height_sum, height_max), add(1, ceil_log2(p + 1)))), 1);
    height_bound = add(height_bound, factor_allowance(degree_bound));
    if (degree_bound > (Uint64)limits.algebraic_degree ||
        height_bound > (Uint64)limits.algebraic_bits)
      resource("odd branch resultant degree/height admission");
    qqbar_ptr coeff = _qqbar_vec_init(p + 1), rr = _qqbar_vec_init(p);
    for (Uint64 k = 0; k <= p; ++k) qqbar_set(coeff + k, coef[k].v);
    int ok = _qqbar_roots_poly_squarefree(rr, coeff, p + 1, limits.algebraic_degree,
                                          limits.algebraic_bits);
    if (ok)
      for (Uint64 k = 0; k < p; ++k) qqbar_set(roots[k].v, rr + k);
    _qqbar_vec_clear(coeff, p + 1);
    _qqbar_vec_clear(rr, p);
    if (!ok) resource("exact algebraic odd-root isolation limit");
  }
  size_t chosen = SIZE_MAX;
  for (size_t k = 0; k < roots.size(); ++k) {
    algebraic_limit(roots[k]);
    if (qqbar_is_zero(roots[k].v)) reject("zero defining root");
    for (size_t j = 0; j < k; ++j)
      if (qqbar_equal(roots[j].v, roots[k].v)) reject("multiple defining root");
    if (box_contains(branches[i], roots[k])) {
      if (chosen != SIZE_MAX) reject("ambiguous selected branch");
      chosen = k;
    }
  }
  if (chosen == SIZE_MAX) reject("branch contains no exact root");
  AlgebraicNumber r = roots[chosen];
  vector<AlgebraicNumber> basis(p);
  if (p == 2) {
    basis[0] = scaled(AlgebraicNumber(1), -1);
    basis[1] = op(basis[0], r, '/');
  } else {
    vector<AlgebraicNumber> quotient(p);
    quotient[p - 1] = AlgebraicNumber(1);
    for (Int64 k = (Int64)p - 2; k >= 0; --k)
      quotient[k] = op(coef[k + 1], op(r, quotient[k + 1], '*'), '+');
    AlgebraicNumber derivative;
    for (Int64 k = (Int64)p; k >= 1; --k)
      derivative = op(op(derivative, r, '*'), op(coef[k], AlgebraicNumber(k), '*'), '+');
    if (qqbar_is_zero(derivative.v)) reject("zero derivative");
    for (Uint64 k = 0; k < p; ++k) basis[k] = op(quotient[k], derivative, '/');
  }
  return {std::move(roots), chosen, std::move(basis)};
}

auto Input::quadratic_branches() const -> bool {
  const int prefix = !deg.empty() && deg[0] != 2 ? 1 : 0;
  if (D0 < 0 || std::any_of(deg.begin() + prefix, deg.end(), [](Uint64 p) { return p != 2; }))
    return false;
  for (const auto& branch : branches)
    if (branch[0] != "0 0 0 0" && branch[1] != "0 0 0 0") return false;
  BranchLevel first;
  if (prefix) {
    AlgebraicNumber root = sqrt_checked(AlgebraicNumber(D0));
    AlgebraicNumber omega = D0 % 4 == 1 ? scaled(op(AlgebraicNumber(1), root, '-'), -1)
                                        : op(AlgebraicNumber(0), root, '-');
    first = algebraic_branch_level(0, {}, omega);
    if (!qqbar_is_real(first.roots[first.chosen].v)) return false;
    for (const auto& b : first.basis)
      if (!qqbar_is_real(b.v)) return false;
  }
  for (Uint64 work = std::min(limits.branch_bits, add(branch_bits, 128));;) {
    A omega;
    arb_sqrt_ui(omega.v, (ulong)D0, (slong)work);
    arb_neg(omega.v, omega.v);
    if (D0 % 4 == 1) {
      arb_add_ui(omega.v, omega.v, 1, (slong)work);
      arb_mul_2exp_si(omega.v, omega.v, -1);
    }
    vector<B> roots;
    vector<vector<B>> dual;
    if (prefix) {
      vector<B> basis(first.basis.size());
      for (size_t k = 0; k < basis.size(); ++k)
        real_enclosure(acb_realref(basis[k].v), first.basis[k], work);
      dual.push_back(std::move(basis));
    }
    vector<A> radicands;
    vector<int> axes;
    bool retry = false;
    for (int i = prefix; i < t(); ++i) {
      size_t id = find("wsq", i);
      const auto& e = el[id];
      B c, term;
      Z scale;
      fmpz_set_str(scale.v, e.scale.c_str(), 10);
      for (const auto& p : element(id)) {
        acb_zero(term.v);
        arb_mul_fmpz(acb_realref(term.v), omega.v, p.y.v, (slong)work);
        arb_add_fmpz(acb_realref(term.v), acb_realref(term.v), p.x.v, (slong)work);
        acb_mul_fmpz(term.v, term.v, scale.v, (slong)work);
        Uint64 sub = p.sub;
        size_t active = 0;
        for (int j = 0; j < e.field; ++j) {
          Uint64 digit = 0;
          if (active < e.levels.size() && e.levels[active] == j) {
            digit = sub % deg[j];
            sub /= deg[j];
            ++active;
          }
          acb_mul(term.v, term.v, dual[j][digit].v, (slong)work);
        }
        // This is an exact structural equality (operations on zero components),
        // not a ball that merely happens to contain zero.
        if (!arb_is_zero(acb_imagref(term.v))) return false;
        acb_add(c.v, c.v, term.v, (slong)work);
      }
      if (arb_is_zero(acb_realref(c.v))) reject("zero/non-simple quadratic root");
      int axis;
      if (arb_is_positive(acb_realref(c.v)))
        axis = 0;
      else if (arb_is_negative(acb_realref(c.v)))
        axis = 1;
      else {
        retry = true;
        break;
      }
      if (branches[i][1 - axis] != "0 0 0 0") return false;
      A mag, stored;
      arb_abs(mag.v, acb_realref(c.v));
      radicands.push_back(mag);
      arb_sqrt(mag.v, mag.v, (slong)work);
      BallString(branches[i][axis]).ball(stored.v);
      if (arb_is_negative(stored.v))
        arb_neg(mag.v, mag.v);
      else if (!arb_is_positive(stored.v))
        return false;
      B r, b;
      arb_set(axis ? acb_imagref(r.v) : acb_realref(r.v), mag.v);
      if (acb_contains_zero(r.v)) {
        retry = true;
        break;
      }
      acb_inv(b.v, r.v, (slong)work);
      acb_mul_2exp_si(b.v, b.v, -1);
      roots.push_back(r);
      vector<B> basis(2);
      acb_one(basis[0].v);
      acb_mul_2exp_si(basis[0].v, basis[0].v, -1);
      basis[1] = b;
      dual.push_back(std::move(basis));
      axes.push_back(axis);
    }
    auto box = [&](size_t i, bool negative, Uint64 p, string& result) -> bool {
      A x;
      arb_set(x.v, axes[i] ? acb_imagref(roots[i].v) : acb_realref(roots[i].v));
      if (negative) arb_neg(x.v, x.v);
      if (arb_is_exact(x.v)) {
        fmpq_t rational;
        fmpq_init(rational);
        arf_get_fmpq(rational, arb_midref(x.v));
        AlgebraicNumber q;
        qqbar_set_fmpq(q.v, rational);
        fmpq_clear(rational);
        result = component_box(q, p);
        return true;
      }
      arb_mul_2exp_si(x.v, x.v, (slong)p);
      A half, shifted;
      arb_one(half.v);
      arb_mul_2exp_si(half.v, half.v, -1);
      arb_add(shifted.v, x.v, half.v, (slong)work);
      arf_t lo, hi;
      arf_init(lo);
      arf_init(hi);
      arb_get_lbound_arf(lo, shifted.v, (slong)work);
      arb_get_ubound_arf(hi, shifted.v, (slong)work);
      Z q, upper;
      arf_get_fmpz(q.v, lo, ARF_RND_FLOOR);
      arf_get_fmpz(upper.v, hi, ARF_RND_FLOOR);
      arf_clear(lo);
      arf_clear(hi);
      if (!fmpz_equal(q.v, upper.v)) return false;
      // Matching floors alone permit equality at the lower half-integer tie.
      // Require an open rounding cell; exact ties go to exact Qbar rounding.
      A lower_cell, upper_cell;
      arb_set_fmpz(lower_cell.v, q.v);
      fmpz_add_ui(upper.v, q.v, 1);
      arb_set_fmpz(upper_cell.v, upper.v);
      if (!arb_gt(shifted.v, lower_cell.v) || !arb_lt(shifted.v, upper_cell.v)) return false;
      // Prove that this component is not an exact dyadic singleton by testing
      // its square against the defining radicand, with certified arithmetic.
      A square, difference;
      fmpz_mul(upper.v, q.v, q.v);
      Z exponent(-2 * (Int64)p);
      arb_set_fmpz_2exp(square.v, upper.v, exponent.v);
      arb_sub(difference.v, radicands[i].v, square.v, (slong)work);
      if (arb_contains_zero(difference.v)) return false;
      Int64 E = -(Int64)p;
      if (fmpz_is_zero(q.v))
        E = 0;
      else {
        Uint64 v = fmpz_val2(q.v);
        fmpz_tdiv_q_2exp(q.v, q.v, v);
        E += (Int64)v;
      }
      Z exp(E), radexp(-(Int64)p);
      result = zhex(q.v) + " " + zhex(exp.v) + " 1 " + zhex(radexp.v);
      return true;
    };
    if (!retry) {
      for (Uint64 p = 128;; p = mul(p, 2)) {
        bool separated = true;
        vector<std::array<string, 2>> selected;
        if (prefix) {
          vector<std::array<string, 2>> boxes;
          for (const auto& r : first.roots) {
            boxes.push_back(root_box(r, p));
            if (box_contains(boxes.back(), AlgebraicNumber())) separated = false;
            for (size_t k = 0; k + 1 < boxes.size(); ++k)
              if (boxes_overlap(boxes.back(), boxes[k])) separated = false;
          }
          selected.push_back(boxes[first.chosen]);
        }
        for (size_t i = 0; i < roots.size(); ++i) {
          std::array<string, 2> a{"0 0 0 0", "0 0 0 0"}, b = a;
          if (!box(i, false, p, a[axes[i]]) || !box(i, true, p, b[axes[i]])) {
            retry = true;
            break;
          }
          if (boxes_overlap(a, b) || boxes_overlap(a, {"0 0 0 0", "0 0 0 0"})) separated = false;
          selected.push_back(a);
        }
        if (retry) break;
        if (separated) {
          if (p != branch_bits) reject("branch_bits is not the minimum canonical precision");
          if (selected != branches)
            reject("branch midpoint/radius is not canonical exact rounding");
          return true;
        }
        if (p >= branch_bits) reject("branch rectangles are not disjoint/nonzero");
      }
    }
    if (work == limits.branch_bits) return false;
    work = std::min(limits.branch_bits, mul(work, 2));
  }
}

auto Input::check_branches() const -> void {
  if (!quadratic_branches()) check_branches_algebraic();
}

auto Input::check_branches_algebraic() const -> void {
  AlgebraicNumber disc(D0), root = sqrt_checked(disc);
  AlgebraicNumber omega = ((D0 % 4 + 4) % 4 == 1) ? scaled(op(AlgebraicNumber(1), root, '-'), -1)
                                                  : op(AlgebraicNumber(0), root, '-');
  vector<vector<AlgebraicNumber>> dual;
  vector<vector<AlgebraicNumber>> families;
  vector<AlgebraicNumber> selected;
  for (int i = 0; i < t(); ++i) {
    auto level = algebraic_branch_level(i, dual, omega);
    selected.push_back(level.roots[level.chosen]);
    families.push_back(std::move(level.roots));
    dual.push_back(std::move(level.basis));
  }
  for (Uint64 p = 128;; p = mul(p, 2)) {
    bool separated = true;
    for (size_t i = 0; i < families.size(); ++i) {
      vector<std::array<string, 2>> boxes;
      for (const auto& z : families[i]) boxes.push_back(root_box(z, p));
      for (size_t k = 0; k < boxes.size(); ++k) {
        if (box_contains(boxes[k], AlgebraicNumber())) separated = false;
        for (size_t j = 0; j < k; ++j)
          if (boxes_overlap(boxes[k], boxes[j])) separated = false;
      }
    }
    if (separated) {
      if (p != branch_bits) reject("branch_bits is not the minimum canonical precision");
      break;
    }
    if (p >= branch_bits) reject("branch rectangles are not disjoint/nonzero");
  }
  for (int i = 0; i < t(); ++i)
    if (root_box(selected[i], branch_bits) != branches[i])
      reject("branch midpoint/radius is not canonical exact rounding");
}

auto Input::complete_exact_if_ready() const -> void {
  if (!layout_ready || checking_exact || scope.exact_checked) return;
  for (const auto& c : ch)
    if ((c.type == "TOWR" || c.type == "BODY") && !scope.chunks.count((size_t)(&c - ch.data())))
      return;
  checking_exact = true;
  struct Reset {
    bool& flag;
    ~Reset() { flag = false; }
  } reset{checking_exact};
  vector<Constraint> constraints;
  for (size_t id = 0; id < el.size(); ++id) {
    auto pairs = element(id);
    Z scale;
    fmpz_set_str(scale.v, el[id].scale.c_str(), 10);
    Uint64 sv = fmpz_val2(scale.v);
    for (const auto& p : pairs) {
      Uint64 v = UINT64_MAX;
      if (!fmpz_is_zero(p.x.v)) v = fmpz_val2(p.x.v);
      if (!fmpz_is_zero(p.y.v)) v = std::min<Uint64>(v, fmpz_val2(p.y.v));
      if (constraints.size() >= limits.rescaling_constraints)
        resource("rescaling constraint memory limit");
      constraints.push_back({id, p.sub, add(v, sv)});
    }
  }
  check_rescaling(constraints);
  check_branches();
  scope.exact_checked = true;
}

auto Input::verify_full() -> void {
  scope.full = false;
  for (size_t id = 0; id < el.size(); ++id) element(id);
  complete_exact_if_ready();
  size_t nb = 0;
  for (size_t ci = 1; ci < ch.size(); ++ci) {
    const auto& type = ch[ci].type;
    if (type == "PROV" || type == "PREC")
      parse_optional(ci);
    else if (type == "RAYS") {
      auto b = payload(ci);
      parse_rays(b);
      scope.chunks.insert(ci);
    } else if (type == "BALL") {
      if (nb >= rays.size()) reject("extra BALL");
      check_ball(ci, nb++);
    }
  }
  if (nb != rays.size()) reject("missing BALL");
  if (!scope.whole_checked) {
    file_sha = whole_file_sha();
    scope.whole_checked = true;
  } else if (whole_file_sha() != file_sha)
    reject("input changed during verification");
  scope.full = true;
}

auto Input::open(const string& p, const Trust& trust, bool full) -> void {
  path = p;
  f.open(p);
  if (f.size < 16 + 112) reject("file too short for final footer");
  Uint8 h[16];
  f.read(0, h, 16);
  if (memcmp(h, "SICX\r\n\x1a\n", 8)) reject("header magic");
  if (le16(h + 8) != 1 || le16(h + 10) != 0) reject("unsupported major/minor");
  if (le32(h + 12)) reject("unknown physical required_features");
  auto ft = f.read(f.size - 112, 112);
  if (memcmp(ft.data() + 104, "SICXEND\x1a", 8)) reject("final footer magic");
  Uint64 lo = le64(ft.data()), ll = le64(ft.data() + 8), po = le64(ft.data() + 16),
         pl = le64(ft.data() + 24);
  memcpy(exact_root.data(), ft.data() + 32, 32);
  memcpy(merkle.data(), ft.data() + 64, 32);
  if (lo < 16 || po != add(add(lo, 16), ll) || f.size != add(add(add(po, 16), pl), 112) ||
      le64(ft.data() + 96) != f.size)
    reject("directory/footer geometry");
  if (ll > limits.directory_bytes || pl > limits.directory_bytes ||
      add(ll, pl) > limits.directory_bytes)
    resource("directory byte limit");
  header_at(lo, "LDIR", 1, ll);
  header_at(po, "PDIR", 1, pl);
  auto lb = f.read(lo, (size_t)add(16, ll));
  auto pb = f.read(add(po, 16), (size_t)pl);
  if (ll < 8 || pl < 8) reject("directory count missing");
  Uint64 nl = le64(lb.data() + 16), np = le64(pb.data());
  if (!nl || !np || nl > (1ull << 24) || np > (1ull << 24))
    reject("directory count outside 1..2^24");
  if (nl > limits.directory_entries || np > limits.directory_entries)
    resource("directory entry limit");
  if (ll != add(8, mul(64, nl)) || pl != add(8, mul(112, np)))
    reject("directory count/length mismatch");
  vector<Hash> lh, ph;
  lh.reserve(nl);
  ph.reserve(np + 1);
  for (Uint64 i = 0; i < nl; ++i) {
    const Uint8* e = lb.data() + 24 + 64 * i;
    Logical l;
    l.kind.assign((const char*)e, 4);
    if (le32(e + 4)) reject("LDIR reserved field");
    l.element = le64(e + 8);
    l.first = le64(e + 16);
    l.count = le64(e + 24);
    memcpy(l.hash.data(), e + 32, 32);
    if ((i == 0 && (l.kind != "EXMT" || l.element || l.first || l.count != 1)) ||
        (i > 0 && l.kind != "TOWR" && l.kind != "BODY"))
      reject("LDIR kind or EXMT marker");
    logical.push_back(l);
    lh.push_back(tree_leaf(e, 64));
  }
  Uint64 off = 16, next_leaf = 0;
  int state = 0;
  size_t balls = 0;
  for (Uint64 i = 0; i < np; ++i) {
    const Uint8* e = pb.data() + 8 + 112 * i;
    Chunk c;
    c.type.assign((const char*)e, 4);
    c.flags = le32(e + 4);
    c.off = le64(e + 8);
    c.len = le64(e + 16);
    c.codec = le32(e + 24);
    if (le32(e + 28)) reject("PDIR reserved field");
    c.decoded = le64(e + 32);
    c.first_leaf = le64(e + 40);
    c.leaf_count = le64(e + 48);
    c.object = le64(e + 56);
    c.first = le64(e + 64);
    c.count = le64(e + 72);
    memcpy(c.sha.data(), e + 80, 32);
    for (Uint8 a : c.type)
      if (a < 0x21 || a > 0x7e) reject("chunk type is not printable ASCII");
    bool optional = c.type == "PROV" || c.type == "PREC";
    if (c.type != "EXMT" && c.type != "TOWR" && c.type != "BODY" && !optional && c.type != "RAYS" &&
        c.type != "BALL")
      reject("undefined or misplaced chunk type: " + c.type);
    if (c.flags != (optional ? 0u : 1u)) reject("chunk flags");
    if (c.codec) reject("unsupported codec");
    if (c.decoded != c.len) reject("raw encoded/decoded length");
    if (c.off != off) reject("noncontiguous physical chunks");
    off = add(add(off, 16), c.len);
    if (off > lo) reject("physical chunk overlaps directories");
    if (i == 0) {
      if (c.type != "EXMT") reject("EXMT must be first");
      state = 1;
    } else if (c.type == "TOWR") {
      if (state != 1) reject("TOWR order");
    } else if (c.type == "BODY") {
      if (state > 2) reject("BODY order");
      state = 2;
    } else if (c.type == "PROV") {
      if (state > 2) reject("PROV order/duplicate");
      state = 3;
    } else if (c.type == "RAYS") {
      if (state > 3) reject("RAYS order/duplicate");
      state = 4;
    } else if (c.type == "BALL") {
      if (state != 4 && state != 5) reject("BALL without RAYS or after PREC");
      state = 5;
      ++balls;
    } else if (c.type == "PREC") {
      if (state != 5) reject("PREC without BALL or duplicate");
      state = 6;
    } else
      reject("duplicate EXMT");
    if (c.type == "EXMT") {
      if (c.first_leaf || c.leaf_count != 1 || c.object || c.first || c.count)
        reject("EXMT physical marker");
      ++next_leaf;
    } else if (c.type == "TOWR" || c.type == "BODY") {
      if (!c.leaf_count || c.first_leaf != next_leaf || c.first_leaf >= nl ||
          c.leaf_count > nl - c.first_leaf)
        reject("logical coverage gap/overlap");
      Uint64 end = c.first, sum = 0;
      for (Uint64 j = c.first_leaf; j < c.first_leaf + c.leaf_count; ++j) {
        const auto& l = logical[j];
        if (l.kind != c.type || l.element != c.object || l.first != end || !l.count)
          reject("logical/physical mapping");
        end = add(end, l.count);
        sum = add(sum, l.count);
      }
      if (sum != c.count) reject("physical coordinate span");
      next_leaf = add(next_leaf, c.leaf_count);
    } else {
      if (c.first_leaf || c.leaf_count || c.first) reject("non-leaf physical coverage");
      if (c.type == "BALL") {
        if (c.object != balls - 1) reject("BALL ray index/order");
      } else if (c.object || c.count)
        reject("non-element physical fields");
    }
    ch.push_back(c);
    ph.push_back(tree_leaf(e, 112));
  }
  if (off != lo || next_leaf != nl) reject("missing/extra physical or logical coverage");
  if (state == 4) reject("RAYS without BALL");
  if (tree(lh, 0, lh.size()) != exact_root) reject("exact tree root mismatch");
  vector<Uint8> binding = {'S', 'I', 'C', 'X', '1', '-', 'L', 'D', 'I', 'R', 0};
  auto lhash = sha(lb.data(), lb.size());
  binding.insert(binding.end(), lhash.begin(), lhash.end());
  ph.push_back(tree_leaf(binding.data(), binding.size()));
  if (tree(ph, 0, ph.size()) != merkle) reject("file tree root mismatch");
  if (!trust.exact.empty()) {
    if (parse_hash(trust.exact) != exact_root) reject("trusted exact root mismatch");
    scope.exact_trusted = true;
    scope.exact_route = "direct exact-root match";
  }
  if (!trust.file.empty()) {
    if (parse_hash(trust.file) != merkle) reject("trusted file root mismatch");
    scope.file_trusted = true;
    scope.file_route = "direct file-root match";
    if (!scope.exact_trusted) {
      scope.exact_trusted = true;
      scope.exact_route = "transitive via file root and complete LDIR";
    }
  }
  if (!trust.whole.empty()) {
    file_sha = whole_file_sha();
    if (parse_hash(trust.whole) != file_sha) reject("trusted whole-file hash mismatch");
    scope.whole_checked = true;
    scope.file_trusted = true;
    if (scope.file_route.empty()) scope.file_route = "transitive via whole-file SHA-256";
    if (!scope.exact_trusted) {
      scope.exact_trusted = true;
      scope.exact_route = "transitive via whole-file SHA-256 and complete directories";
    }
  }
  if (full && !scope.whole_checked) {
    file_sha = whole_file_sha();
    scope.whole_checked = true;
  }
  auto meta = payload(0);
  if (logical_hash("EXMT", 0, 0, 1, meta.data(), meta.size()) != logical[0].hash)
    reject("EXMT logical digest");
  parse_meta(meta, 0);
  scope.chunks.insert(0);
  Uint64 li = 1;
  for (size_t id = 0; id < el.size(); ++id) {
    auto& e = el[id];
    for (Uint64 b = 0; b < e.nblocks; ++b) {
      if (li >= nl) reject("missing logical leaf");
      const auto& l = logical[li++];
      if (l.kind != (e.kind == "rep" ? "BODY" : "TOWR") || l.element != id ||
          l.first != mul(b, e.W) || l.count != e.W)
        reject("logical boundary/element order");
    }
  }
  if (li != nl) reject("extra logical leaves");
  for (size_t ci = 1; ci < ch.size(); ++ci) {
    const auto& c = ch[ci];
    if (c.type == "TOWR" || c.type == "BODY") {
      if (c.object >= el.size()) reject("element id");
      el[c.object].chunks.push_back(ci);
    }
    if (c.type == "BALL" && c.count != d) reject("BALL coordinate count");
  }
  layout_ready = true;
  if (full)
    verify_full();
  else if (scope.whole_checked && whole_file_sha() != file_sha)
    reject("input changed during verification");
}

auto Input::verify_chunk(size_t ci) -> void {
  if (ci >= ch.size()) reject("chunk index");
  const auto& c = ch[ci];
  if (c.type == "TOWR" || c.type == "BODY") {
    read_chunk(ci);
    return;
  }
  if (c.type == "PROV" || c.type == "PREC") {
    parse_optional(ci);
    return;
  }
  if (c.type == "EXMT") {
    auto b = payload(ci);
    if (logical_hash("EXMT", 0, 0, 1, b.data(), b.size()) != logical[0].hash)
      reject("EXMT logical digest");
    scope.chunks.insert(ci);
    return;
  }
  if (c.type == "RAYS") {
    auto b = payload(ci);
    parse_rays(b);
    scope.chunks.insert(ci);
    return;
  }
  if (c.type == "BALL") {
    if (rays.empty()) {
      for (size_t i = 0; i < ch.size(); ++i)
        if (ch[i].type == "RAYS") {
          verify_chunk(i);
          break;
        }
    }
    if (c.object >= rays.size()) reject("BALL ray index");
    check_ball(ci, (size_t)c.object);
    return;
  }
  reject("undefined requested chunk");
}

auto Input::parse_optional(size_t ci) -> void {
  auto b = payload(ci);
  auto root = parse_canonical(b.data(), b.size(), ch[ci].type.c_str());
  if (root.t != 'o') reject("optional JSON must be object");
  if (ch[ci].type == "PROV") {
    if (root.get("generator_rescaling")) reject("PROV duplicates EXMT generator_rescaling");
  } else
    for (const auto& kv : root.o) {
      if (kv.first != "branch_bits" && kv.first != "base_path_bits" && kv.first != "delta_bits" &&
          kv.first != "max_element_bits")
        reject("unknown PREC key");
      if (kv.second.t != 's') reject("PREC type");
      to_u64(kv.second.s, "PREC hint");
    }
  scope.chunks.insert(ci);
}

auto Input::read_chunk(size_t ci) const -> vector<LeafData> {
  if (ci >= ch.size()) reject("chunk index");
  const auto& c = ch[ci];
  if (c.type != "TOWR" && c.type != "BODY") reject("exact element chunk expected");
  auto b = payload(ci);
  Cur cur{b.data(), b.data() + b.size()};
  vector<LeafData> out;
  Uint64 pairs = 0;
  const auto& e = el.at((size_t)c.object);
  Z scale, Dz(D0);
  fmpz_set_str(scale.v, e.scale.c_str(), 10);
  for (Uint64 li = c.first_leaf; li < c.first_leaf + c.leaf_count; ++li) {
    const Uint8* begin = cur.p;
    const auto& l = logical.at(li);
    LeafData data;
    data.index = li;
    if (rd_varint(cur) != l.element || rd_varint(cur) != l.first || rd_varint(cur) != l.count)
      reject("leaf header disagrees with directories");
    Uint64 covered = 0;
    int last = -1;
    vector<Uint8> encoded;
    wr_varint(encoded, l.element);
    wr_varint(encoded, l.first);
    wr_varint(encoded, l.count);
    while (covered < l.count) {
      Uint64 tag = rd_varint(cur), n = tag >> 1;
      int kind = tag & 1;
      if (!n || kind == last || n > l.count - covered) reject("leaf run empty/nonmaximal/overrun");
      last = kind;
      wr_varint(encoded, tag);
      if (kind)
        for (Uint64 j = 0; j < n; ++j) {
          if (++pairs > limits.coordinate_pairs)
            resource("physical chunk nonzero coordinate limit");
          Z r, y, k;
          rd_int(cur, r.v);
          rd_int(cur, y.v);
          wr_int(encoded, r.v);
          wr_int(encoded, y.v);
          Pair p;
          p.sub = add(l.first, add(covered, j));
          fmpz_set(p.y.v, y.v);
          if (D0 == 1 && !fmpz_is_zero(y.v)) reject("rational base requires y = 0");
          if (D0 > 1 && D0 % 4 == 1) {
            kfun(k.v, y.v, Dz.v);
            fmpz_add(p.x.v, r.v, k.v);
          } else if (D0 < 0 && ((D0 % 4 + 4) % 4) == 1) {
            fmpz_fdiv_q_2exp(k.v, y.v, 1);
            fmpz_sub(p.x.v, r.v, k.v);
          } else
            fmpz_set(p.x.v, r.v);
          if (fmpz_is_zero(p.x.v) && fmpz_is_zero(p.y.v))
            reject("zero original coordinate in data run");
          Z full;
          fmpz_mul(full.v, p.x.v, scale.v);
          if (fmpz_bits(full.v) > e.max_bits) reject("leaf above max_bits");
          fmpz_mul(full.v, p.y.v, scale.v);
          if (fmpz_bits(full.v) > e.max_bits) reject("leaf above max_bits");
          data.pairs.push_back(std::move(p));
        }
      covered += n;
    }
    size_t bytes = (size_t)(cur.p - begin);
    if (bytes > limits.chunk_bytes) resource("logical leaf byte limit");
    if (encoded.size() != bytes || memcmp(encoded.data(), begin, bytes))
      reject("logical re-encoding identity");
    if (logical_hash(l.kind, l.element, l.first, l.count, begin, bytes) != l.hash)
      reject("logical leaf SHA-256 mismatch");
    out.push_back(std::move(data));
  }
  if (cur.p != cur.e) reject("trailing/unused physical payload bytes");
  scope.chunks.insert(ci);
  complete_exact_if_ready();
  return out;
}

auto Input::element(size_t id) const -> vector<Pair> {
  if (id >= el.size()) reject("element id out of range");
  const auto& e = el[id];
  vector<Pair> pairs;
  for (size_t ci : e.chunks) {
    auto chunk = read_chunk(ci);
    for (auto& l : chunk)
      for (auto& p : l.pairs) {
        if (pairs.size() >= limits.coordinate_pairs) resource("element nonzero coordinate limit");
        pairs.push_back(std::move(p));
      }
  }
  check_element(id, pairs);
  return pairs;
}

auto Input::coordinates(size_t id, vector<fmpz>& X, vector<fmpz>& Y) const -> void {
  if (id >= el.size()) reject("element id");
  const auto& e = el[id];
  if (e.ncoord > limits.dense_coordinates) resource("dense coordinate allocation limit");
  auto pairs = element(id);  // validate before publishing or modifying caller-owned coordinates
  for (auto& x : X) fmpz_clear(&x);
  for (auto& y : Y) fmpz_clear(&y);
  X.assign(e.ncoord, 0);
  Y.assign(e.ncoord, 0);
  for (auto& p : pairs) {
    fmpz_set(&X[p.sub], p.x.v);
    fmpz_set(&Y[p.sub], p.y.v);
  }
}

auto Input::find(const string& kind, Int64 level, Int64 axis, Int64 k) const -> size_t {
  for (size_t i = 0; i < el.size(); ++i)
    if (el[i].kind == kind && el[i].level == level && el[i].axis == axis && el[i].k == k) return i;
  return SIZE_MAX;
}
}  // namespace sic_check::crt::sicx
