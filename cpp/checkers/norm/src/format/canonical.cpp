// SPDX-License-Identifier: AGPL-3.0-or-later
#include "canonical.hpp"

#include "../runtime/diagnostics.hpp"
#include "canonical_json.hpp"
#include "container.hpp"
#include "integer_codec.hpp"
#include "metadata.hpp"
#include "quadratic.hpp"
namespace sic_check::norm {
Algebraic::Algebraic() { qqbar_init(v); }

Algebraic::Algebraic(const Algebraic& x) : Algebraic() { qqbar_set(v, x.v); }

Algebraic::Algebraic(Algebraic&& x) noexcept : Algebraic() { qqbar_swap(v, x.v); }

Algebraic& Algebraic::operator=(const Algebraic& x) {
  qqbar_set(v, x.v);
  return *this;
}

Algebraic::~Algebraic() { qqbar_clear(v); }

BoundedAlgebra::BoundedAlgebra(AlgebraicLimits budget) : limits(budget) {
  resource(limits.degree && limits.degree <= 256 && limits.height_bits &&
               limits.height_bits <= (1u << 20),
           "exact algebraic limits");
}

auto BoundedAlgebra::bound(uint64_t degree, uint64_t height, const char* operation) const -> void {
  resource(degree <= limits.degree && height <= limits.height_bits, operation);
}

auto BoundedAlgebra::check(const qqbar_t x) const -> void {
  bound(uint64_t(qqbar_degree(x)), uint64_t(qqbar_height_bits(x)), "exact algebraic value");
}

auto BoundedAlgebra::binary_bound(const qqbar_t x, const qqbar_t y) const -> void {
  check(x);
  check(y);
  uint64_t m = uint64_t(qqbar_degree(x)), n = uint64_t(qqbar_degree(y));
  uint64_t h = uint64_t(qqbar_height_bits(x)), k = uint64_t(qqbar_height_bits(y));
  // The resultant has degree <= mn and leading coefficient bounded by
  // 2^(nh+mk). Cauchy's bound on the roots of either polynomial (also reversed
  // for division) bounds sum/product/quotient conjugates by 2^(h+k+2).
  // A primitive factor's leading coefficient divides that of the resultant;
  // expanding its <= mn roots contributes at most mn*(h+k+3) bits.
  bound(m * n, n * h + m * k + m * n * (h + k + 3) + 1, "exact binary pre-expansion bound");
}

auto BoundedAlgebra::set(qqbar_t out, const fmpz_t n) const -> void {
  bound(1, std::max<uint64_t>(1, fmpz_bits(n)), "exact integer pre-expansion bound");
  qqbar_set_fmpz(out, n);
}

auto BoundedAlgebra::add(qqbar_t out, const qqbar_t x, const qqbar_t y) const -> void {
  binary_bound(x, y);
  qqbar_add(out, x, y);
  check(out);
}

auto BoundedAlgebra::mul(qqbar_t out, const qqbar_t x, const qqbar_t y) const -> void {
  binary_bound(x, y);
  qqbar_mul(out, x, y);
  check(out);
}

auto BoundedAlgebra::div(qqbar_t out, const qqbar_t x, const qqbar_t y) const -> void {
  input_require(!qqbar_is_zero(y), "exact division by zero");
  binary_bound(x, y);
  qqbar_div(out, x, y);
  check(out);
}

auto BoundedAlgebra::shift(qqbar_t out, const qqbar_t x, slong bits) const -> void {
  check(x);
  uint64_t magnitude = bits < 0 ? uint64_t(-(bits + 1)) + 1 : uint64_t(bits);
  resource(magnitude <= limits.height_bits, "exact dyadic shift bound");
  bound(uint64_t(qqbar_degree(x)),
        uint64_t(qqbar_height_bits(x)) + uint64_t(qqbar_degree(x)) * magnitude,
        "exact dyadic pre-expansion bound");
  qqbar_mul_2exp_si(out, x, bits);
  check(out);
}

auto BoundedAlgebra::components(qqbar_t re, qqbar_t im, const qqbar_t z) const -> void {
  check(z);
  if (qqbar_is_real(z)) {
    qqbar_set(re, z);
    qqbar_zero(im);
    return;
  }
  Algebraic conjugate, negative, imaginary_unit;
  qqbar_conj(conjugate.v, z);  // Conjugation/negation preserve degree and height.
  qqbar_neg(negative.v, conjugate.v);
  qqbar_i(imaginary_unit.v);
  // Form the components explicitly so every expansion is admitted first.
  // This also proves singleton equality, including zero and nonzero dyadics.
  add(re, z, conjugate.v);
  shift(re, re, -1);
  add(im, z, negative.v);
  mul(im, im, imaginary_unit.v);
  qqbar_neg(im, im);
  shift(im, im, -1);
}

auto BoundedAlgebra::integer(const fmpz_t n) const -> Algebraic {
  Algebraic out;
  set(out.v, n);
  return out;
}

void branch_component(arb_t out, const qqbar_t component, uint64_t bits,
                      const BoundedAlgebra& arithmetic) {
  Algebraic scaled, threshold, rounded;
  resource(bits <= (1u << 20), "exact branch rounding precision");
  arithmetic.shift(scaled.v, component, slong(bits));
  Integer n, twice;
  qqbar_floor(n.v, scaled.v);
  fmpz_mul_2exp(twice.v, n.v, 1);
  fmpz_add_ui(twice.v, twice.v, 1);
  arithmetic.set(threshold.v, twice.v);
  arithmetic.shift(threshold.v, threshold.v, -1);
  int cmp = qqbar_cmp_re(scaled.v, threshold.v);
  if (cmp > 0 || (cmp == 0 && fmpz_is_odd(n.v))) fmpz_add_ui(n.v, n.v, 1);
  arithmetic.set(rounded.v, n.v);
  Integer exp;
  fmpz_set_si(exp.v, -slong(bits));
  arf_set_fmpz_2exp(arb_midref(out), n.v, exp.v);
  if (qqbar_equal(scaled.v, rounded.v))
    mag_zero(arb_radref(out));
  else
    mag_set_ui_2exp_si(arb_radref(out), 1, -slong(bits));
}

ComplexBall branch_rectangle(const Algebraic& z, uint64_t bits, const BoundedAlgebra& arithmetic) {
  Algebraic re, im;
  arithmetic.components(re.v, im.v, z.v);
  ComplexBall out;
  branch_component(acb_realref(out.v), re.v, bits, arithmetic);
  branch_component(acb_imagref(out.v), im.v, bits, arithmetic);
  return out;
}

std::vector<Algebraic> polynomial_roots(const std::vector<Algebraic>& coefficients,
                                        AlgebraicLimits limits) {
  BoundedAlgebra arithmetic(limits);
  uint64_t conjugations = 1, height_sum = 0, height_max = 0;
  for (const auto& coefficient : coefficients) {
    arithmetic.check(coefficient.v);
    conjugations *= uint64_t(qqbar_degree(coefficient.v));
    arithmetic.bound(conjugations, 0, "exact polynomial conjugate degree bound");
    uint64_t h = uint64_t(qqbar_height_bits(coefficient.v));
    height_sum += h;
    height_max = std::max(height_max, h);
  }
  uint64_t degree = (coefficients.size() - 1) * conjugations;
  // Bound both the cleared conjugate-product polynomial and any primitive
  // factor, before gr's squarefree factorization/root isolation. The sum of
  // coefficient heights bounds the common denominator. A conjugate polynomial
  // has l1 norm <= (p+1)*2^(height_max+1). Cauchy's root bound then bounds factors.
  arithmetic.bound(
      degree,
      conjugations * (height_sum + height_max + 1 + std::bit_width(coefficients.size())) +
          degree * (height_max + 3) + 1,
      "exact polynomial pre-expansion bound");
  struct Generic {
    gr_ctx_t ctx;
    gr_poly_t poly;
    gr_vec_t roots;
    fmpz_vec_t multiplicities;
    explicit Generic(AlgebraicLimits budget) {
      gr_ctx_init_complex_qqbar(ctx);
      _gr_ctx_qqbar_set_limits(ctx, slong(budget.degree), slong(budget.height_bits));
      gr_poly_init(poly, ctx);
      gr_vec_init(roots, 0, ctx);
      fmpz_vec_init(multiplicities, 0);
    }
    ~Generic() {
      fmpz_vec_clear(multiplicities);
      gr_vec_clear(roots, ctx);
      gr_poly_clear(poly, ctx);
      gr_ctx_clear(ctx);
    }
  } g(limits);
  for (size_t k = 0; k < coefficients.size(); ++k)
    resource(gr_poly_set_coeff_scalar(g.poly, slong(k), coefficients[k].v, g.ctx) == GR_SUCCESS,
             "exact polynomial coefficients");
  int status = gr_poly_roots(g.roots, g.multiplicities, g.poly, 0, g.ctx);
  resource(status == GR_SUCCESS, "exact algebraic root isolation (degree <= 256, height <= 2^20)");
  input_require(size_t(g.roots->length) + 1 == coefficients.size(), "multiple defining roots");
  std::vector<Algebraic> roots(size_t(g.roots->length));
  for (size_t i = 0; i < roots.size(); ++i) {
    input_require(fmpz_is_one(g.multiplicities->entries + i), "multiple defining root");
    qqbar_set(roots[i].v, static_cast<qqbar_srcptr>(gr_vec_entry_srcptr(g.roots, slong(i), g.ctx)));
    arithmetic.check(roots[i].v);
    input_require(!qqbar_is_zero(roots[i].v), "zero defining root");
  }
  return roots;
}

void verify_canonical_branches(const Meta& m, const std::vector<std::vector<Pair>>& leaves,
                               AlgebraicLimits limits) {
  if (m.D > 64 && limits.degree == 256 && limits.height_bits == (1u << 20) &&
      verify_quadratic_branches(m, leaves))
    return;
  BoundedAlgebra arithmetic(limits);
  Algebraic omega, temp;
  Integer radicand;
  if (m.D0 != 1) {
    fmpz_set_si(radicand.v, m.D0);
    arithmetic.bound(2, std::max<uint64_t>(1, fmpz_bits(radicand.v)),
                     "exact base square root bound");
    arithmetic.set(omega.v, radicand.v);
    qqbar_sqrt(omega.v, omega.v);
    arithmetic.check(omega.v);
    qqbar_neg(omega.v, omega.v);
    if ((m.D0 % 4 + 4) % 4 == 1) {
      qqbar_one(temp.v);
      arithmetic.add(omega.v, omega.v, temp.v);
      arithmetic.shift(omega.v, omega.v, -1);
    }
  }
  std::vector<std::vector<Algebraic>> duals, all_roots;
  auto contract = [&](size_t id) {
    Algebraic sum, term;
    for (size_t index = 0; index < leaves[id].size(); ++index) {
      const auto& pair = leaves[id][index];
      if (fmpz_is_zero(pair.x.v) && fmpz_is_zero(pair.y.v)) continue;
      auto x = arithmetic.integer(pair.x.v), y = arithmetic.integer(pair.y.v);
      arithmetic.mul(term.v, omega.v, y.v);
      arithmetic.add(term.v, term.v, x.v);
      size_t digits = index;
      for (unsigned level = 0; level < m.elements[id].level; ++level) {
        arithmetic.mul(term.v, term.v, duals[level][digits % m.degrees[level]].v);
        digits /= m.degrees[level];
      }
      arithmetic.add(sum.v, sum.v, term.v);
    }
    return sum;
  };
  size_t next = 0;
  for (unsigned level = 0; level < m.degrees.size(); ++level) {
    unsigned p = m.degrees[level];
    std::vector<Algebraic> coefficients(p + 1);
    qqbar_one(coefficients[p].v);
    if (p == 2) {
      coefficients[0] = contract(next++);
      qqbar_neg(coefficients[0].v, coefficients[0].v);
    } else
      for (unsigned k = 0; k < p; ++k) coefficients[k] = contract(next++);
    while (next < m.elements.size() && m.elements[next].kind != "rep" &&
           m.elements[next].level == level)
      ++next;
    auto roots = polynomial_roots(coefficients, limits);
    int selected = -1;
    for (size_t i = 0; i < roots.size(); ++i) {
      auto box = branch_rectangle(roots[i], m.branch_bits, arithmetic);
      if (acb_equal(box.v, m.branches[level].v)) {
        input_require(selected < 0, "ambiguous canonical branch");
        selected = int(i);
      }
    }
    input_require(selected >= 0, "branch exact rounding/root choice");
    const auto& root = roots[size_t(selected)];
    std::vector<Algebraic> q(p), basis(p);
    qqbar_one(q[p - 1].v);
    for (unsigned k = p - 1; k > 0; --k) {
      arithmetic.mul(q[k - 1].v, root.v, q[k].v);
      arithmetic.add(q[k - 1].v, q[k - 1].v, coefficients[k].v);
    }
    Algebraic derivative;
    // Horner evaluation of f'(root).
    Integer integer_multiplier;
    fmpz_set_ui(integer_multiplier.v, p);
    arithmetic.set(derivative.v, integer_multiplier.v);
    for (unsigned k = p - 1; k > 0; --k) {
      arithmetic.mul(derivative.v, derivative.v, root.v);
      Algebraic multiplier;
      fmpz_set_ui(integer_multiplier.v, k);
      arithmetic.set(multiplier.v, integer_multiplier.v);
      arithmetic.mul(temp.v, coefficients[k].v, multiplier.v);
      arithmetic.add(derivative.v, derivative.v, temp.v);
    }
    input_require(!qqbar_is_zero(derivative.v), "zero defining derivative");
    for (unsigned k = 0; k < p; ++k) arithmetic.div(basis[k].v, q[k].v, derivative.v);
    duals.push_back(std::move(basis));
    all_roots.push_back(std::move(roots));
  }
  for (uint64_t bits = 128; bits <= m.branch_bits; bits *= 2) {
    bool isolated = true;
    for (const auto& roots : all_roots) {
      std::vector<ComplexBall> boxes;
      for (const auto& root : roots) {
        boxes.push_back(branch_rectangle(root, bits, arithmetic));
        if (acb_contains_zero(boxes.back().v)) isolated = false;
        for (size_t j = 0; j + 1 < boxes.size(); ++j)
          if (acb_overlaps(boxes[j].v, boxes.back().v)) isolated = false;
      }
    }
    if (isolated) {
      input_require(bits == m.branch_bits, "branch_bits not minimal");
      return;
    }
  }
  input_require(false, "canonical branch rectangles do not isolate all roots");
}
}  // namespace sic_check::norm
