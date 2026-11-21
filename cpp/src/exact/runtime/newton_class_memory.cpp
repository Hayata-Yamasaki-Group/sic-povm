// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/runtime/newton_class_memory.hpp"

#include "support/index.hpp"

namespace sic::exact::phase_memory {
sic::support::resources::Bytes callback_workspace(sic::support::resources::Count dimension,
                                                  sic::support::resources::Bytes private_slot,
                                                  sic::support::resources::Bytes packed_output,
                                                  int workers, bool replay) {
  using sic::support::resources::add;
  using sic::support::resources::mul;
  if (dimension < 4 || workers <= 0) throw std::invalid_argument("invalid callback workspace");
  const auto D = mul(dimension, dimension);
  const auto scalar = add(private_slot / mul(4, D), 128);
  auto publication = add(packed_output, add(128 * sic::support::resources::kMib,
                                            mul(mul(8, mul(2, dimension) - 2), scalar)));
  if (replay) {
    const auto active = std::min<sic::support::resources::Count>(workers, dimension);
    const auto entries = add(add(mul(3, dimension), 64), mul(active, add(mul(2, dimension), 64)));
    const auto scratch =
        add(128 * sic::support::resources::kMib,
            add(mul(entries, scalar), add(mul(active, sic::support::resources::kMib),
                                          add(mul(mul(128, dimension), scalar),
                                              mul((D - 1) / 3, sizeof(sic::support::Index))))));
    publication = add(publication, add(packed_output, scratch));
  }
  return std::max(add(private_slot, packed_output), publication);
}

}  // namespace sic::exact::phase_memory
