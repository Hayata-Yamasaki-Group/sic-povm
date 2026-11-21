// SPDX-License-Identifier: AGPL-3.0-or-later
#include "exact/runtime/memory/phase_accounting.hpp"

#include "exact/runtime/memory/product_tiles.hpp"

namespace sic::exact::phase_memory {
Bytes matrix_bytes(Count rows, Count columns, Bytes scalar_bytes) {
  return sic::support::resources::add(
      sic::support::resources::mul(sic::support::resources::mul(rows, columns), scalar_bytes),
      sic::support::resources::add(sic::support::resources::mul(rows, sizeof(acb_ptr)),
                                   sizeof(acb_mat_struct) + 64));
}

Accounting forecast(const Ceilings& c) {
  using sic::support::resources::add;
  using sic::support::resources::mul;
  if (c.dimension < 2 || (c.dimension & (c.dimension - 1)) || c.workers <= 0 || c.source_bits < 2 ||
      c.selection_bits < c.source_bits || c.inverse_bits < 2 || c.stored_matrix_bits < 2 ||
      c.high_bits < 2 || !c.candidate_rows ||
      (!c.modular && (!c.native_scalar_bytes || !c.native_complex_bytes)))
    throw std::invalid_argument("incomplete Newton storage ceilings");
  const Count d = c.dimension, D = mul(d, d), N = mul(2, d) - 2;
  if (c.candidate_rows > D - 1)
    throw std::invalid_argument("Newton candidate count exceeds nonzero labels");
  const auto scalar = [](BitCount p) {
    return sic::support::resources::complex_bytes(sic::support::resources::add(p, 128));
  };
  const auto grid = [&](BitCount p) { return mul(D, scalar(p)); };
  const BitCount matrix_precision = std::max(c.inverse_bits, c.stored_matrix_bits);
  const BitCount maximum_precision = std::max(
      {c.source_bits, c.selection_bits, matrix_precision, c.high_bits, c.previous_output_bits});
  Accounting result;
  const Bytes scalar_allocators = mul(64, D);
  result.source_grid =
      std::max(grid(c.source_bits),
               c.source_storage_bytes ? add(c.source_storage_bytes, scalar_allocators) : 0);
  result.previous_output = std::max(c.previous_output_bits ? grid(c.previous_output_bits) : 0,
                                    c.previous_output_storage_bytes
                                        ? add(c.previous_output_storage_bytes, scalar_allocators)
                                        : 0);
  result.empty_matrix = matrix_bytes(N, N, sizeof(acb_struct));
  result.matrix = matrix_bytes(N, N,
                               sic::support::resources::complex_bytes(
                                   sic::support::resources::newton_matrix_bits(matrix_precision)));
  result.reshape = add(mul(mul(N, N), sizeof(double)), mul(mul(3, N), sizeof(double)));

  // Named engineering allowances: contexts (including old/new Ctop), Ktop,
  // states/rounded boxes and v/u, mag vectors/scalar scratch, integer grids,
  // candidate/seen/subsystem/container metadata, worker stacks and allocator
  // slack. Native allocator/library internals are not a proved byte bound.
  // The per-worker allowance includes the retained seven scalar row slots, padding,
  // and row crew; exclusive matrix kernels release that crew before entry.
  result.small_objects = add(512 * sic::support::resources::kMib,
                             add(mul(mul(128, d), scalar(maximum_precision)),
                                 add(mul(mul(16, D), sizeof(long)),
                                     mul(c.workers, add(2 * sic::support::resources::kMib,
                                                        mul(128, scalar(maximum_precision)))))));
  const Bytes E = result.small_objects, seed = result.source_grid;
  const auto set = [&](Phase phase, Bytes bytes) {
    result.totals[static_cast<size_t>(phase)] = add(E, bytes);
  };

  // std::map node overhead is implementation-dependent; the extra256 bytes
  // per entry is a deliberately generous allowance for this pinned libstdc++.
  // This cache dies before projection's A0 and X are created.
  const Bytes root_map = mul(D, add(scalar(c.source_bits), 256));
  set(Phase::roots, add(mul(2, seed), root_map));
  set(Phase::projection, add(mul(4, seed), sic::support::resources::ghost_reconstruction_scratch(
                                               d, c.source_bits, 1)));

  const Bytes candidate = matrix_bytes(c.candidate_rows, N, scalar(c.selection_bits));
  const Bytes selector =
      c.modular
          ? sic::support::resources::modular_selection_bytes(c.candidate_rows, N, c.workers,
                                                             c.selection_bits)
          : add(mul(mul(c.candidate_rows, N), c.native_complex_bytes),
                add(mul(c.candidate_rows, add(3 * sizeof(long) + 2 * sizeof(slong) + sizeof(char),
                                              c.native_scalar_bytes)),
                    mul(N, c.native_complex_bytes)));
  set(Phase::selection,
      add(mul(2, seed), add(candidate, std::max(mul(2, grid(c.selection_bits)), selector))));

  // Preserve the eight-matrix native margin. Jacobian caches die before
  // inversion; a prior Y may retain its full capacity during the next attempt.
  const Bytes inverse_scratch = c.column_inverse ? sic::support::resources::inverse_column_scratch(
                                                       N, c.workers, matrix_precision)
                                                 : 0;
  const Bytes product_scratch =
      c.threaded_product
          ? sic::exact::product_tiles::scratch(
                N, c.workers, sic::support::resources::newton_matrix_bits(matrix_precision))
          : 0;
  set(Phase::inverse,
      add(mul(2, seed), std::max(add(mul(8, result.matrix), inverse_scratch),
                                 add(mul(2, result.matrix), mul(2, grid(matrix_precision))))));

  // Old M0 survives recomputation. Mloc exists even on reuse. Reshape's
  // N²-double matrix survives its nested Krawczyk call. J dies before copying
  // Mloc into M0, and residual caches die before J/product construction.
  const Bytes certification_work = std::max(
      {add(mul(kCertificationMatrixOwners, result.matrix), product_scratch),
       add(add(mul(3, result.matrix), result.empty_matrix), mul(2, grid(matrix_precision))),
       add(add(mul(2, result.matrix), result.empty_matrix), mul(2, grid(c.high_bits)))});
  set(Phase::certification,
      add(seed, add(result.previous_output, add(result.reshape, certification_work))));

  const BitCount correction_precision = std::min(c.high_bits, add(c.inverse_bits, 512));
  const Bytes correction =
      c.refined_correction
          ? sic::support::resources::linear_correction_workspace(d, correction_precision, c.workers)
          : 0;
  set(Phase::high_newton,
      add(seed, add(result.previous_output,
                    add(mul(2, result.matrix), std::max(mul(2, grid(c.high_bits)), correction)))));

  // Previous output is explicitly released before allocating its replacement.
  // Refiner-local matrices still coexist with the new output here. The caller
  // later packs Tn only after this function returns and matrices are destroyed.
  const Bytes reconstruction =
      c.parallel_reconstruction && c.workers > 1
          ? sic::support::resources::ghost_reconstruction_scratch(d, c.high_bits, c.workers)
          : sic::support::resources::ghost_reconstruction_scratch(d, c.high_bits, 1);
  set(Phase::reconstruction,
      add(seed, add(grid(c.high_bits), add(mul(2, result.matrix), reconstruction))));
  return result;
}

}  // namespace sic::exact::phase_memory
