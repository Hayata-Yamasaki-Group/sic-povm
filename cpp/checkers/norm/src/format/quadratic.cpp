// SPDX-License-Identifier: AGPL-3.0-or-later
#include "quadratic.hpp"

#include "../runtime/diagnostics.hpp"
#include "canonical_json.hpp"
#include "container.hpp"
#include "integer_codec.hpp"
#include "metadata.hpp"
namespace sic_check::norm {
bool interval_component(arb_t box, const arb_t value, uint64_t bits, slong prec) {
  if (!arb_is_finite(value)) return false;
  ComplexBall scaled, integer, half, distance;
  arb_mul_2exp_si(acb_realref(scaled.v), value, slong(bits));
  resource(arf_cmpabs_2exp_si(arb_midref(acb_realref(scaled.v)), 3 * (1 << 20)) < 0,
           "quadratic rounding integer admission");
  Integer q;
  arf_get_fmpz(q.v, arb_midref(acb_realref(scaled.v)), ARF_RND_NEAR);
  arb_set_fmpz(acb_realref(integer.v), q.v);
  arb_sub(acb_realref(distance.v), acb_realref(scaled.v), acb_realref(integer.v), prec);
  arb_one(acb_realref(half.v));
  arb_mul_2exp_si(acb_realref(half.v), acb_realref(half.v), -1);
  ComplexBall absolute;
  arb_abs(acb_realref(absolute.v), acb_realref(distance.v));
  // Strict separation from half-integers proves nearest rounding; an exact
  // singleton also permits a tie, with arf's nearest-even rule.
  bool rounded = arb_lt(acb_realref(absolute.v), acb_realref(half.v));
  if (!rounded && arb_is_exact(acb_realref(scaled.v))) {
    rounded = arf_cmpabs_2exp_si(arb_midref(acb_realref(distance.v)), -1) <= 0;
  }
  if (!rounded) return false;
  bool exact = arb_is_zero(acb_realref(distance.v));
  if (!exact && arb_contains_zero(acb_realref(distance.v))) return false;
  arb_mul_2exp_si(box, acb_realref(integer.v), -slong(bits));
  if (!exact) mag_set_ui_2exp_si(arb_radref(box), 1, -slong(bits));
  return true;
}

bool verify_quadratic_branches(const Meta& m, const std::vector<std::vector<Pair>>& leaves) {
  if (m.D0 <= 1 ||
      !std::all_of(m.degrees.begin(), m.degrees.end(), [](unsigned p) { return p == 2; }))
    return false;
  uint64_t height = 0;
  for (const auto& e : m.elements)
    if (e.kind == "wsq") height = std::max(height, e.max_bits);
  // Only bounded ball operations are used. Three passes then exact fallback.
  for (slong prec = slong(m.branch_bits + height + 256), pass = 0; pass < 3; ++pass, prec *= 2) {
    const uint64_t slots = m.D / 2 + 4 * m.degrees.size() + 16;
    const uint64_t bytes_per_slot = 256 + 32 * ((uint64_t(prec) + 63) / 64);
    resource(prec <= (1 << 20) && slots <= (512u << 20) / bytes_per_slot,
             "quadratic interval workspace admission");
    ComplexBall omega, square, term, coefficient, root;
    arb_set_si(acb_realref(omega.v), m.D0);
    arb_sqrt(acb_realref(omega.v), acb_realref(omega.v), prec);
    arb_neg(acb_realref(omega.v), acb_realref(omega.v));
    if (m.D0 % 4 == 1) {
      arb_add_ui(acb_realref(omega.v), acb_realref(omega.v), 1, prec);
      arb_mul_2exp_si(acb_realref(omega.v), acb_realref(omega.v), -1);
    }
    std::vector<std::array<ComplexBall, 2>> duals;
    std::vector<ComplexBall> selected;
    bool certified = true;
    size_t id = 0;
    for (unsigned level = 0; level < m.degrees.size(); ++level) {
      while (id < m.elements.size() &&
             !(m.elements[id].kind == "wsq" && m.elements[id].level == level))
        ++id;
      input_require(id < m.elements.size(), "quadratic defining element");
      // The tensor trace-dual contraction is evaluated from full scalar pairs.
      std::vector<ComplexBall> products(size_t(1) << level);
      acb_one(products[0].v);
      for (unsigned l = 0; l < level; ++l)
        for (size_t j = 0; j < (size_t(1) << l); ++j) {
          acb_mul(products[(size_t(1) << l) + j].v, products[j].v, duals[l][1].v, prec);
          acb_mul(products[j].v, products[j].v, duals[l][0].v, prec);
        }
      acb_zero(square.v);
      for (size_t j = 0; j < leaves[id].size(); ++j) {
        const auto& pair = leaves[id][j];
        if (fmpz_is_zero(pair.x.v) && fmpz_is_zero(pair.y.v)) continue;
        acb_mul_fmpz(coefficient.v, omega.v, pair.y.v, prec);
        acb_add_fmpz(coefficient.v, coefficient.v, pair.x.v, prec);
        acb_mul(term.v, coefficient.v, products[j].v, prec);
        acb_add(square.v, square.v, term.v, prec);
      }
      // Equality here is proved by an exact zero-radius enclosure, never by
      // replacing a small imaginary ball with zero.
      if (!arb_is_zero(acb_imagref(square.v))) {
        certified = false;
        break;
      }
      acb_zero(root.v);
      if (arb_is_positive(acb_realref(square.v)))
        arb_sqrt(acb_realref(root.v), acb_realref(square.v), prec);
      else if (arb_is_negative(acb_realref(square.v))) {
        arb_neg(acb_realref(square.v), acb_realref(square.v));
        arb_sqrt(acb_imagref(root.v), acb_realref(square.v), prec);
      } else {
        certified = false;
        break;
      }
      ComplexBall box, opposite;
      if (!interval_component(acb_realref(box.v), acb_realref(root.v), m.branch_bits, prec) ||
          !interval_component(acb_imagref(box.v), acb_imagref(root.v), m.branch_bits, prec)) {
        certified = false;
        break;
      }
      acb_neg(opposite.v, box.v);
      input_require(
          acb_equal(box.v, m.branches[level].v) || acb_equal(opposite.v, m.branches[level].v),
          "quadratic branch exact rounding/root choice");
      if (acb_equal(opposite.v, m.branches[level].v)) acb_neg(root.v, root.v);
      selected.push_back(root);
      std::array<ComplexBall, 2> dual;
      acb_one(dual[0].v);
      acb_mul_2exp_si(dual[0].v, dual[0].v, -1);
      acb_inv(dual[1].v, root.v, prec);
      acb_mul_2exp_si(dual[1].v, dual[1].v, -1);
      duals.push_back(std::move(dual));
    }
    if (!certified) continue;
    for (uint64_t bits = 128; bits <= m.branch_bits; bits *= 2) {
      bool isolated = true;
      for (const auto& r : selected) {
        ComplexBall box, opposite;
        if (!interval_component(acb_realref(box.v), acb_realref(r.v), bits, prec) ||
            !interval_component(acb_imagref(box.v), acb_imagref(r.v), bits, prec)) {
          certified = false;
          break;
        }
        acb_neg(opposite.v, box.v);
        if (acb_contains_zero(box.v) || acb_overlaps(box.v, opposite.v)) isolated = false;
      }
      if (!certified) break;
      if (isolated) {
        input_require(bits == m.branch_bits, "quadratic branch_bits not minimal");
        return true;
      }
    }
    if (certified) input_require(false, "quadratic canonical rectangles do not isolate");
  }
  return false;
}
}  // namespace sic_check::norm
