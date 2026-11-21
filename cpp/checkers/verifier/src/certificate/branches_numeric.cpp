// SPDX-License-Identifier: AGPL-3.0-or-later
#include "branches_numeric.hpp"

#include "../decode/decoder.hpp"
#include "../format/ball_codec.hpp"
#include "../format/canonical_json.hpp"
#include "../format/container.hpp"
#include "../format/integer_codec.hpp"
#include "../format/metadata.hpp"
#include "../format/sha256.hpp"
#include "../runtime/diagnostics.hpp"
namespace sic_check::verifier {
string interval_component(const arb_t x, slong bits, arb_t box, slong prec) {
  Real scaled, shifted, half, rounded, delta;
  arb_mul_2exp_si(scaled.v, x, bits);
  arb_set_d(half.v, 0.5);
  arb_add(shifted.v, scaled.v, half.v, prec);
  arb_floor(rounded.v, shifted.v, prec);
  Integer q, m, E, r, e;
  if (!arb_get_unique_fmpz(q, rounded.v)) throw PrecErr("branch grid rounding unresolved");
  arb_sub_fmpz(delta.v, scaled.v, q, prec);
  // floor(x+1/2) chooses the upper integer at a tie; select its even neighbour.
  arb_neg(half.v, half.v);
  if (arb_equal(delta.v, half.v) && fmpz_is_odd(q)) {
    fmpz_sub_ui(q, q, 1);
    arb_sub_fmpz(delta.v, scaled.v, q, prec);
  }
  Real positive_half;
  arb_set_d(positive_half.v, 0.5);
  bool exact_tie = arb_equal(delta.v, half.v) || arb_equal(delta.v, positive_half.v);
  if (!exact_tie && !(arb_gt(delta.v, half.v) && arb_lt(delta.v, positive_half.v)))
    throw PrecErr("branch nearest-even tie comparison unresolved");
  bool singleton = arb_is_zero(delta.v);
  if (!singleton && arb_contains_zero(delta.v))
    throw PrecErr("branch singleton equality unresolved");
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
  arb_set_fmpz_2exp(box, m, E);
  mag_set_fmpz_2exp_fmpz(arb_radref(box), r, e);
  return hexz(m) + " " + hexz(E) + " " + hexz(r) + " " + hexz(e);
}

bool numeric_branch_check(Container& C) {
  Uint64 maxbits = C.M.branch_bits + 256;
  for (const auto& e : C.M.el)
    if (e->kind != "rep") maxbits = std::max(maxbits, e->max_bits + 512);
  for (Uint64 prec = maxbits; prec <= std::min<Uint64>(maxbits * 4, 1u << 20); prec *= 2) {
    try {
      vector<vector<ACB>> roots;
      numeric_base(C, prec, &roots);
      for (Uint64 bits = 128; bits <= C.M.branch_bits; bits *= 2) {
        bool admissible = true;
        Decoder D(C);
        vector<std::pair<string, string>> stored(C.M.t());
        for (int i = 0; i < C.M.t(); ++i) {
          vector<ACB> boxes(roots[i].size());
          for (size_t j = 0; j < roots[i].size(); ++j) {
            string re =
                interval_component(acb_realref(roots[i][j].v), bits, acb_realref(boxes[j].v), prec);
            string im =
                interval_component(acb_imagref(roots[i][j].v), bits, acb_imagref(boxes[j].v), prec);
            if (acb_contains(D.branch[i].v, roots[i][j].v)) stored[i] = {re, im};
            if (acb_contains_zero(boxes[j].v)) admissible = false;
            for (size_t k = 0; k < j; ++k)
              if (acb_overlaps(boxes[j].v, boxes[k].v)) admissible = false;
          }
        }
        if (admissible) {
          if (bits != C.M.branch_bits) reject("branch_bits is not the minimum canonical precision");
          if (stored != C.M.branch_re)
            reject("branches differ from certified canonical midpoint/radius rule");
          say("  canonical branches verified by certified interval comparisons at %llu bits\n",
              (unsigned long long)bits);
          return true;
        }
      }
      reject("branch_bits does not separate all nonzero roots");
    } catch (const PrecErr&) { /* Exact algebraic fallback after bounded refinement. */
    }
  }
  return false;
}
}  // namespace sic_check::verifier
