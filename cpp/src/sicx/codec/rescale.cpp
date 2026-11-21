// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/rescale.hpp"

#include "sic/support/flint_raii.hpp"
#include "sicx/codec/rescaling_rule.hpp"

namespace sic::sicx {
LeafSource rescaled_source(const Meta& m, const ElementId& id, LeafSource source,
                           const GeneratorRescaling& r) {
  if (r.identity()) return source;
  if (r.shifts.size() != m.degrees.size())
    fail(sic::ErrorKind::invalid_format, "rescaling: shift vector length differs from the tower");
  const unsigned n = id.level;
  const uint64_t w = own_weight(id, m.degrees);
  const uint64_t own = w ? w * r.shifts.at(n) : 0;
  return [degrees = m.degrees, shifts = r.shifts, n, own,
          source = std::move(source)](const LeafVisitor& visit) {
    struct Tmp {
      fmpz_t x, y;
      Tmp() {
        fmpz_init(x);
        fmpz_init(y);
      }
      ~Tmp() {
        fmpz_clear(x);
        fmpz_clear(y);
      }
    } tmp;
    source([&](uint64_t pos, const fmpz* x, const fmpz* y) {
      uint64_t shift = own, rest = pos;
      for (unsigned i = 0; i < n; ++i) {
        shift += (rest % degrees[i]) * shifts[i];
        rest /= degrees[i];
      }
      if (!shift || (fmpz_is_zero(x) && fmpz_is_zero(y))) {
        visit(pos, x, y);
        return;
      }
      if (pair_val2(x, y) < shift)
        fail(sic::ErrorKind::invalid_format, "rescaling: leaf " + std::to_string(pos) +
                                                 " is not divisible by 2^" + std::to_string(shift));
      fmpz_tdiv_q_2exp(tmp.x, x, shift);
      fmpz_tdiv_q_2exp(tmp.y, y, shift);
      visit(pos, tmp.x, tmp.y);
    });
  };
}
}  // namespace sic::sicx

namespace sic::sicx {
void rescale_branches(Meta& m, const GeneratorRescaling& r) {
  if (r.shifts.size() != m.branches.size())
    fail(sic::ErrorKind::invalid_format,
         "rescaling: shift vector length differs from the branches");
  sic::support::flint::Arb x;
  for (size_t s = 0; s < m.branches.size(); ++s) {
    if (!r.shifts[s]) continue;
    for (auto& part : m.branches[s]) {
      if (arb_load_str(x, part.c_str())) {
        fail(sic::ErrorKind::invalid_format, "rescaling: branch rectangle does not load");
      }
      arb_mul_2exp_si(x, x, -static_cast<slong>(r.shifts[s]));
      sic::support::flint::String text(arb_dump_str(x));
      part = text.get();
    }
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
ElementId element_id_of(const Meta& meta, const ElementInfo& e) {
  return ElementId{e.kind, e.kind == "rep" ? meta.levels() : e.level, e.axis, e.k, e.position};
}
}  // namespace sic::sicx
