// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/acb_poly.h>
namespace sic::sicx {
template <class Source>
CanonicalBranches canonical_branches(const Meta& m, Source source,
                                     slong ceiling = sic::kDefaultMaximumPrecision,
                                     slong minimum_precision = 0) {
  using sic::support::ComplexBallVector;
  (void)sic::checked_precision(128, 128, ceiling);
  if (minimum_precision < 0 || minimum_precision > ceiling)
    throw sic::Error(ErrorKind::invalid_argument, "invalid minimum branch precision");
  for (slong p = 128; p <= ceiling; p *= 2) {
    sic::support::check_stop();
    bool decided = false;
    uint64_t height = 0;
    for (const auto& e : m.elements)
      if (e.kind != "rep") height = std::max(height, e.max_bits);
    for (slong prec = std::max({p + 128, static_cast<slong>(height) + p + 128, minimum_precision});
         prec <= ceiling; prec *= 2) {
      sic::support::check_stop();
      sic::support::ComplexBallVector omega(1), t(6);
      std::vector<sic::support::ComplexBallVector> duals;
      arb_set_si(acb_realref(omega[0]), m.D0 < 0 ? -m.D0 : m.D0);
      arb_sqrt(acb_realref(omega[0]), acb_realref(omega[0]), prec);
      if (m.D0 < 0) acb_mul_onei(omega[0], omega[0]);
      acb_neg(omega[0], omega[0]);
      if (m.D0 == 1)
        acb_zero(omega[0]);
      else if (((m.D0 % 4) + 4) % 4 == 1) {
        acb_add_ui(omega[0], omega[0], 1, prec);
        acb_mul_2exp_si(omega[0], omega[0], -1);
      }
      auto element = [&](size_t id, acb_t out) {
        sic::support::check_stop();
        acb_zero(out);
        const auto& e = m.elements[id];
        source(id, [&](uint64_t position, const fmpz* x, const fmpz* y) {
          acb_mul_fmpz(t[0], omega[0], y, prec);
          acb_add_fmpz(t[0], t[0], x, prec);
          for (unsigned i = 0; i < e.level; ++i) {
            const auto digit = position % m.degrees[i];
            position /= m.degrees[i];
            acb_mul(t[0], t[0], duals[i][digit], prec);
          }
          acb_add(out, out, t[0], prec);
        });
      };
      bool resolved = true, separated = true;
      CanonicalBranches result{static_cast<uint64_t>(p), {}, {}, prec};
      for (unsigned i = 0; i < m.levels() && resolved; ++i) {
        const unsigned degree = m.degrees[i];
        sic::support::ComplexBallVector coefficients(degree + 1), roots(degree), boxes(degree),
            dual(degree);
        acb_one(coefficients[degree]);
        for (size_t id = 0; id < m.elements.size(); ++id) {
          const auto& e = m.elements[id];
          if (e.level == i && e.kind == "wsq") {
            element(id, coefficients[0]);
            acb_neg(coefficients[0], coefficients[0]);
          }
          if (e.level == i && e.kind == "fcoef") element(id, coefficients[e.k]);
        }
        sic::support::flint::AcbPoly polynomial;
        for (unsigned k = 0; k <= degree; ++k)
          acb_poly_set_coeff_acb(polynomial, k, coefficients[k]);
        if (degree == 2) {
          acb_neg(t[1], coefficients[0]);
          if (acb_contains_zero(t[1]))
            resolved = false;
          else if (arb_is_negative(acb_realref(t[1]))) {
            acb_neg(t[1], t[1]);
            acb_sqrt(roots[0], t[1], prec);
            acb_mul_onei(roots[0], roots[0]);
          } else
            acb_sqrt(roots[0], t[1], prec);
          acb_neg(roots[1], roots[0]);
        } else if (acb_poly_find_roots(roots.v.data(), polynomial, nullptr, 256, prec) != degree)
          resolved = false;
        // Real and imaginary axis symmetries prove exact zero components when
        // all coefficients establish the corresponding polynomial symmetry.
        bool real_poly = true, imaginary_axis = true;
        for (unsigned k = 0; k <= degree; ++k) {
          real_poly = real_poly && arb_is_zero(acb_imagref(coefficients[k]));
          imaginary_axis =
              imaginary_axis && arb_is_zero((degree - k) % 2 ? acb_realref(coefficients[k])
                                                             : acb_imagref(coefficients[k]));
        }
        for (unsigned j = 0; j < degree && resolved; ++j) {
          if (!acb_is_finite(roots[j]) || acb_contains_zero(roots[j])) {
            resolved = false;
            break;
          }
          for (unsigned k = 0; k < j; ++k)
            if (acb_overlaps(roots[j], roots[k])) resolved = false;
          if (real_poly || imaginary_axis) {
            for (int axis = 0; axis < 2; ++axis) {
              if (axis ? !imaginary_axis : !real_poly) continue;
              acb_conj(t[1], roots[j]);
              if (axis) acb_neg(t[1], t[1]);
              unsigned matches = 0, selected = 0;
              for (unsigned k = 0; k < degree; ++k)
                if (acb_overlaps(t[1], roots[k])) {
                  ++matches;
                  selected = k;
                }
              if (matches == 1 && selected == j)
                arb_zero(axis ? acb_realref(roots[j]) : acb_imagref(roots[j]));
            }
          }
          if (!branch_grid(acb_realref(boxes[j]), acb_realref(roots[j]), p, prec) ||
              !branch_grid(acb_imagref(boxes[j]), acb_imagref(roots[j]), p, prec))
            resolved = false;
        }
        unsigned matches = 0, selected = 0;
        if (arb_load_str(acb_realref(t[2]), m.branches[i][0].c_str()) ||
            arb_load_str(acb_imagref(t[2]), m.branches[i][1].c_str())) {
          fail(sic::ErrorKind::invalid_format, "invalid input branch rectangle");
        }
        for (unsigned j = 0; j < degree; ++j)
          if (acb_overlaps(roots[j], t[2])) {
            ++matches;
            selected = j;
          }
        if (matches != 1) resolved = false;
        if (resolved) {
          for (unsigned j = 0; j < degree; ++j) {
            if (acb_contains_zero(boxes[j])) separated = false;
            for (unsigned k = 0; k < j; ++k)
              if (acb_overlaps(boxes[j], boxes[k])) separated = false;
          }
          if (!acb_contains(boxes[selected], roots[selected])) resolved = false;
          sic::support::ComplexBallVector selected_root(1);
          acb_set(selected_root[0], roots[selected]);
          result.selected_roots.push_back(std::move(selected_root));
          result.rectangles.push_back({branch_dump(acb_realref(boxes[selected])),
                                       branch_dump(acb_imagref(boxes[selected]))});
          // Euler duals: synthetic division followed by f'(root).
          acb_one(dual[degree - 1]);
          for (unsigned k = degree - 1; k; --k) {
            acb_mul(dual[k - 1], roots[selected], dual[k], prec);
            acb_add(dual[k - 1], dual[k - 1], coefficients[k], prec);
          }
          acb_zero(t[3]);
          for (unsigned k = degree; k-- > 0;) {
            acb_mul(t[3], t[3], roots[selected], prec);
            acb_add(t[3], t[3], dual[k], prec);
          }
          if (acb_contains_zero(t[3]))
            resolved = false;
          else
            for (unsigned k = 0; k < degree; ++k) acb_div(dual[k], dual[k], t[3], prec);
          duals.push_back(std::move(dual));
        }
      }
      if (!resolved) continue;
      if (separated) return result;
      decided = true;
      break;  // this p provably fails isolation; test the next grid
    }
    if (!decided) throw BranchRefusal("SICX: smaller branch grid remains unresolved");
  }
  throw BranchRefusal("SICX: canonical branch comparison/isolation resource refusal");
}
}  // namespace sic::sicx
