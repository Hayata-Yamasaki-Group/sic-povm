// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/sicx/branches.hpp"
namespace sic::sicx {
bool branch_grid(arb_t out, const arb_t x, slong p, slong prec) {
  sic::support::flint::Arb scaled;

  arb_mul_2exp_si(scaled, x, p);
  sic::support::flint::Arf lo, hi;

  sic::support::flint::Fmpz q, r;

  arb_get_interval_arf(lo, hi, scaled, prec);
  arf_get_fmpz(q, lo, ARF_RND_NEAR);
  arf_get_fmpz(r, hi, ARF_RND_NEAR);
  bool ok = fmpz_equal(q, r);
  const bool singleton = ok && arb_is_exact(scaled) && arf_is_int(arb_midref(scaled));
  if (ok && !singleton && arb_contains_fmpz(scaled, q)) ok = false;
  if (ok) {
    arb_set_fmpz(out, q);
    arb_mul_2exp_si(out, out, -p);
    if (!singleton) mag_set_ui_2exp_si(arb_radref(out), 1, -p);
  }

  return ok;
}

std::string branch_dump(const arb_t x) {
  std::unique_ptr<char, decltype(&flint_free)> text(arb_dump_str(x), flint_free);
  std::string out(text.get());
  return out;
}
}  // namespace sic::sicx

namespace sic::sicx {
BranchRefusal::BranchRefusal(const std::string& message) : Error(ErrorKind::precision, message) {}
}  // namespace sic::sicx
