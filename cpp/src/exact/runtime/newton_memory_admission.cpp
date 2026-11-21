// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/runtime/newton_memory_admission.hpp"

namespace sic::exact::phase_memory {
void CopySource::observe(const BallStorage& storage) {
  bits = std::max<BitCount>(bits, storage.largest_mantissa);
  maximum_heap = std::max(maximum_heap, storage.heap_bytes);
  finite_lagom = finite_lagom && storage.finite_lagom;
}

Bytes CopySource::grid_storage(sic::support::resources::Count dimension) const {
  if (!finite_lagom) throw StorageContractError("unsupported Newton source storage");
  return sic::support::resources::mul(
      sic::support::resources::mul(dimension, dimension),
      sic::support::resources::add(sizeof(acb_struct), maximum_heap));
}

Ceilings propose(sic::support::resources::Count dimension, sic::support::resources::BitCount target,
                 sic::support::resources::BitCount inverse_hint, const CopySource& source,
                 const Options& options) {
  using sic::support::resources::add;
  using sic::support::resources::mul;
  Ceilings c;
  c.dimension = dimension;
  c.source_bits = source.bits;
  c.source_storage_bytes = source.grid_storage(dimension);
  c.selection_bits = std::max<BitCount>(160, source.bits);
  c.inverse_bits = c.stored_matrix_bits = std::max<BitCount>({128, inverse_hint, source.bits});
  const auto growth = options.modular ? std::max<BitCount>(256, add(c.inverse_bits, 512)) : 256;
  c.high_bits = std::max({add(add(target, growth), 64), add(mul(2, c.inverse_bits), 128),
                          add(add(source.bits, c.inverse_bits), 128)});
  c.candidate_rows = options.reference
                         ? mul(dimension, dimension) - 1
                         : std::min(mul(8, mul(2, dimension) - 2), mul(dimension, dimension) - 1);
  c.workers = options.workers;
  c.modular = options.modular;
  c.column_inverse = options.column_inverse;
  c.threaded_product = options.threaded_product;
  c.refined_correction = options.refined_correction;
  c.parallel_reconstruction = options.parallel_reconstruction;
  c.native_scalar_bytes = options.native_scalar_bytes;
  c.native_complex_bytes = options.native_complex_bytes;
  return c;
}

void merge(Ceilings& retained, const Ceilings& update) {
  if (retained.dimension != update.dimension || retained.workers != update.workers ||
      retained.modular != update.modular || retained.column_inverse != update.column_inverse ||
      retained.threaded_product != update.threaded_product ||
      retained.refined_correction != update.refined_correction ||
      retained.parallel_reconstruction != update.parallel_reconstruction ||
      retained.native_scalar_bytes != update.native_scalar_bytes ||
      retained.native_complex_bytes != update.native_complex_bytes)
    throw std::invalid_argument("Newton retained-memory configuration changed");
  auto keep = [](auto& old, auto next) { old = std::max(old, next); };
  keep(retained.source_bits, update.source_bits);
  keep(retained.selection_bits, std::max(update.selection_bits, retained.source_bits));
  keep(retained.inverse_bits, update.inverse_bits);
  keep(retained.stored_matrix_bits, update.stored_matrix_bits);
  keep(retained.high_bits, update.high_bits);
  keep(retained.candidate_rows, update.candidate_rows);
  keep(retained.previous_output_bits, update.previous_output_bits);
  keep(retained.source_storage_bytes, update.source_storage_bytes);
  keep(retained.previous_output_storage_bytes, update.previous_output_storage_bytes);
}

RequiredMemory requirement(const Ceilings& c, const Publication& publication) {
  using sic::support::resources::add;
  using sic::support::resources::mul;
  RequiredMemory result{c, forecast(c)};
  result.private_peak = result.accounting.peak();
  // Retained root/state/context ceilings, rather than a four-grid slot
  // implication. Eight copies cover bounded root dumping/serialization.
  const auto scalar = sic::support::resources::complex_bytes(
      add(std::max(c.high_bits, std::max(c.inverse_bits, c.source_bits)), 128));
  const auto roots = mul(mul(2, c.dimension) - 2, scalar);
  auto parked =
      add(publication.packed_output, add(128 * sic::support::resources::kMib, mul(8, roots)));
  if (publication.replay) {
    if (publication.replay_workers <= 0) throw std::invalid_argument("invalid replay share");
    const auto scratch =
        add(sic::support::resources::ghost_reconstruction_scratch(c.dimension, c.high_bits,
                                                                  publication.replay_workers),
            add(128 * sic::support::resources::kMib,
                add(mul(mul(128, c.dimension), scalar),
                    mul((mul(c.dimension, c.dimension) - 1) / 3, sizeof(sic::support::Index)))));
    parked = add(parked, add(publication.packed_output, scratch));
  }
  result.callback_peak = std::max(add(result.private_peak, publication.packed_output), parked);
  return result;
}

Bytes correction_baseline(const Accounting& accounting) {
  return sic::support::resources::add(
      accounting.small_objects,
      sic::support::resources::add(
          accounting.source_grid,
          sic::support::resources::add(accounting.previous_output,
                                       sic::support::resources::mul(2, accounting.matrix))));
}

Bytes shortcut_workspace(const Ceilings& ceilings, Bytes packed_output) {
  const auto D = sic::support::resources::mul(ceilings.dimension, ceilings.dimension);
  const auto scalar = sic::support::resources::add(
      64, std::max(ceilings.source_storage_bytes / D,
                   sic::support::resources::complex_bytes(
                       sic::support::resources::add(ceilings.source_bits, 128))));
  return sic::support::resources::add(
      packed_output, sic::support::resources::add(128 * sic::support::resources::kMib,
                                                  sic::support::resources::mul(32, scalar)));
}

}  // namespace sic::exact::phase_memory
