// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/model/session_state.hpp"
#include "exact/transform/fixed_field_tower_general.hpp"
namespace sic::construct {
int Construction::rep_of(long mm, long& sig) {
  sig = entry_actions.at(mm);
  return entry_translations.index.at(mm);
}

long Construction::rep_entry(int r) { return entry_translations.representatives.at(r); }

slong Construction::log2_bp_of(const StepData& sd, int n, const HeightBound& Ca) {
  HeightBound b = HeightBound(2) * HeightBound(static_cast<ulong>(Ncos[n])) * Ca;
  for (int j = 0; j < n; ++j) b = b * sd.certified_Cg[j].pow(ps[j] - 1);
  return b.upper_binary_exponent();
}

slong Construction::representative_height(const StepData& sd, int r) {
  const auto& field = scalar_fields->fields[scalar_fields->representative_field[r]];
  HeightBound bound = HeightBound(2) * HeightBound((ulong)Ncos[t]) * Ceta * sd.certified_Cg.back();
  for (int i = 0; i < field.level; ++i) bound = bound * sd.certified_Cg[i];
  return bound.upper_binary_exponent();
}

bool Construction::constant_item(const StepData& sd, const Item& it) {
  return it.k < 0 && sd.g[it.i].character &&
         std::all_of(ps.begin(), ps.begin() + it.i, [](long p) { return p == 2; });
}

HeightBound Construction::item_height(const StepData& sd, const Item& it) {
  return it.k < 0 ? sd.certified_Cg[it.i].pow(2) : sd.certified_Cf[it.i][it.k];
}

long Construction::chiexp(long character, long g) {  // exponent k with chi_m(g) = e(k / Lc)
  long k = 0;
  for (size_t l = 0; l < (*group_).invf.size(); ++l)
    k += (*group_).digit(character, l) * (*group_).digit(g, l) * (Lc / (*group_).invf[l]);
  return ((k % Lc) + Lc) % Lc;
}

}  // namespace sic::construct

namespace sic::construct {
void Construction::conj_f_at(const Table& table, long i, long j, long g, acb_ptr out) {
  sic::support::Index x0 = (*group_).ghost_point(i, j);
  table.read(out, (*group_).act(g, x0));
  if ((*group_).chi[g] < 0) acb_neg(out, out);
}

void Construction::root_2d(long k, slong wp, acb_ptr out) {
  arb_t x;
  arb_init(x);
  arb_set_si(x, k);
  arb_div_si(x, x, 2 * d, wp);
  acb_e(out, x, wp);
  arb_clear(x);
}

void Construction::product_at(const Table& Tb, long g, slong wp,
                              const sic::support::ComplexBallVector& roots, const arb_t sqrt_eps,
                              acb_ptr out) {
  (*product_).evaluate(out, wp, [&](int j, acb_ptr value) {
    if (j < s)
      conj_f_at(Tb, 0, d >> (j + 1), g, value);
    else if (j == s)
      acb_set(value, roots[(*group_).nn[g]]);
    else {
      acb_set_arb(value, sqrt_eps);
      if ((*group_).chi[g] < 0) acb_neg(value, value);
    }
  });
}

void Construction::generator_at(const Table& Tb, int j, long g, slong wp, acb_ptr out) {
  if (j < s) {
    conj_f_at(Tb, 0, d >> (j + 1), g, out);
    return;
  }
  if (j == s) {
    root_2d((*group_).nn[g], wp, out);
    return;
  }
  arb_t e;
  arb_init(e);
  to_arb(e, (*ar_).kc, (*ar_).eps, 1, wp);
  arb_sqrt(e, e, wp);
  acb_set_arb(out, e);
  if ((*group_).chi[g] < 0) acb_neg(out, out);
  arb_clear(e);
}
}  // namespace sic::construct

namespace sic::construct {
long Construction::source_class(long g) {
  const sic::support::Index code =
      T->layout->encoded((*group_).act(g, analytic_anchor), T->paired_values);
  if (!code) throw std::logic_error("zero analytic class anchor");
  return ((code < 0 ? -(long)code : code) - 1) / T->layout->slots;
}
}  // namespace sic::construct
