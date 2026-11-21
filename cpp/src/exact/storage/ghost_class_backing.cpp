// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/storage/ghost_class_backing.hpp"

namespace sic::exact {
sic::support::resources::Bytes GhostClassBacking::workspace(int owned_workers) const {
  const auto bits = std::max(metadata.precision, metadata.constants_precision);
  const bool parallel = owned_workers > 1;
  return sic::support::resources::add(
      parallel
          ? sic::support::resources::ghost_reconstruction_scratch(metadata.d, bits, owned_workers)
          : 0,
      sic::support::resources::add(
          64 * sic::support::resources::kMib,
          sic::support::resources::add(
              sic::support::resources::mul(sic::support::resources::mul(128, metadata.d),
                                           sic::support::resources::complex_bytes(bits)),
              sic::support::resources::mul((metadata.d * metadata.d - 1) / 3,
                                           sizeof(sic::support::Index)))));
}

std::shared_ptr<GhostClassBacking> GhostClassBacking::save(const CertifiedGhostRoot& root,
                                                           long element, slong precision) {
  if (root.state.size() != (size_t)(2 * root.metadata.d - 2) ||
      root.metadata.constants_precision < root.metadata.precision ||
      precision > root.metadata.precision)
    throw std::logic_error("incomplete certified ghost backing");
  auto result = std::make_shared<GhostClassBacking>();
  result->metadata = root.metadata;
  result->group_element = element;
  result->packing_precision = precision;
  result->state = BallFile::save(root.state);
  return result;
}

sic::support::ComplexBallVector GhostClassBacking::load(
    const Group& group, const Table& table, sic::support::Index cls,
    const sic::support::resources::Budget& budget, int owned_workers) const {
  if (!state || metadata.d != group.d || metadata.d != table.d || !table.compact() ||
      group_element < 0 || group_element >= group.N || metadata.chi != group.chi[group_element] ||
      metadata.n != (group.ar.order_index * group.nn[group_element]) % (2 * group.d) ||
      packing_precision != table.prec)
    throw std::logic_error("inconsistent certified ghost replay");
  const auto slots = table.layout->slots;
  int workers = std::max(1, owned_workers);
  const auto output = sic::support::resources::mul(
      slots, sic::support::resources::complex_bytes(packing_precision));
  try {
    budget.require_additional("certified ghost class reconstruction",
                              sic::support::resources::add(workspace(workers), output));
  } catch (const sic::support::resources::AdmissionError&) {
    if (workers == 1) throw;
    workers = 1;
    budget.require_additional("serial certified ghost class reconstruction",
                              sic::support::resources::add(workspace(), output));
  }
  sic::support::ComplexBallVector values(slots), root = state->complex();
  std::vector<sic::support::Index> winner(slots, -1);
  auto slot_of = [&](long original) {
    const sic::support::Index point =
        group.act(group_element, group.ghost_point(original / group.d, original % group.d));
    const sic::support::Index encoded = table.layout->encoded(point, false);
    if (encoded <= 0 || (encoded - 1) / slots != cls)
      throw std::logic_error("certified ghost replay left its original class");
    return (encoded - 1) % slots;
  };
  // Original production packs V[y] in ascending original y. Preserve its
  // last writer even though reconstruction emits DFT rows in another order.
  for (long y = 1; y < group.d * group.d; ++y) winner[slot_of(y)] = y;
  for (sic::support::Index y : winner)
    if (y < 0) throw std::logic_error("incomplete ghost replay slot coverage");
  GhostRefiner::Ctx high(group.d, metadata.chi, metadata.constants_precision);
  GhostRefiner::Ctx context(high, metadata.precision);
  auto emit = [&](long original, acb_srcptr value) {
    const long slot = slot_of(original);
    if (winner[slot] != original) return;
    acb_set_round(values[slot], value, packing_precision);
    if (metadata.chi < 0) acb_neg(values[slot], values[slot]);
  };
  // The immutable winner filter selects one emitting original per slot.
  const bool reconstructed =
      workers > 1 && try_parallel_ghost_reconstruction(
                         metadata, root.v, context, emit, workers, workspace(workers),
                         sic::support::kernel_setting("SIC_PROGRESS") != nullptr);
  if (!reconstructed) reconstruct_ghost_state(metadata, root.v, context, emit);
  for (size_t q = 0; q < values.size(); ++q)
    if (!acb_is_finite(values[q])) throw std::runtime_error("nonfinite certified ghost replay");
  return values;
}

}  // namespace sic::exact
