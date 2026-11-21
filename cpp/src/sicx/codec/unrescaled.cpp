// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/unrescaled.hpp"

#include "sicx/codec/integer.hpp"
#include "sicx/codec/rescaling_rule.hpp"

namespace sic::sicx {
LeafSource unrescaled_source(const Meta& meta, const ElementId& id, LeafSource source,
                             std::vector<uint64_t> shifts) {
  auto degrees = meta.degrees;
  const auto n = id.level;
  if (shifts.size() != degrees.size() || n > degrees.size())
    fail(sic::ErrorKind::invalid_format, "unrescale: tower shape");
  const auto w = own_weight(id, degrees);
  const auto own = w ? checked_mul(w, shifts.at(n)) : 0;
  return [source = std::move(source), shifts = std::move(shifts), degrees = std::move(degrees), n,
          own](const LeafVisitor& visit) {
    FmpzPair value;
    source([&](uint64_t pos, const fmpz* x, const fmpz* y) {
      uint64_t shift = own, rest = pos;
      for (unsigned i = 0; i < n; ++i) {
        shift = checked_add(shift, checked_mul(rest % degrees[i], shifts[i]));
        rest /= degrees[i];
      }
      if (shift > 8 * kMaxIntegerBytes)
        fail(sic::ErrorKind::resource, "unrescale: reconstruction resource refusal");
      fmpz_mul_2exp(value.x, x, shift);
      fmpz_mul_2exp(value.y, y, shift);
      visit(pos, value.x, value.y);
    });
  };
}
}  // namespace sic::sicx

namespace sic::sicx {
LeafSource unrescaled_source(const Meta& meta, const ElementId& id, LeafSource source) {
  return unrescaled_source(meta, id, std::move(source), meta.rescaling_shifts);
}
}  // namespace sic::sicx
