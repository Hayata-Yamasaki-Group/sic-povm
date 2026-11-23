// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>

#include "support/resources.hpp"
namespace sic::construct::resources {
using sic::support::admit_index_count;
using sic::support::Index;
using sic::support::resources::add;
using sic::support::resources::Budget;
using sic::support::resources::Bytes;
using sic::support::resources::complex_bytes;
using sic::support::resources::ghost_reconstruction_scratch;
using sic::support::resources::inverse_column_scratch;
using sic::support::resources::kMib;
using sic::support::resources::leaf_block_bytes;
using sic::support::resources::linear_correction_vectors;
using sic::support::resources::linear_correction_workspace;
using sic::support::resources::modular_selection_bytes;
using sic::support::resources::mul;
using sic::support::resources::newton_matrix_bits;
using sic::support::resources::real_bytes;
sic::support::resources::Count construction_points(sic::support::resources::Count dimension,
                                                   sic::support::resources::Count classes);
struct Model {
  sic::support::resources::Count d, classes, degree, points, max_cycle, states, representatives;
  sic::support::resources::Count bases =
      1;  // placeholder until measured; never claimed to be the actual base count
  bool streamed_states = true;
  bool compact_tables = true;
  sic::support::resources::Count computed_classes = 0;
  bool phased_final = false;  // enabled only after actual tower/layout checks
  sic::support::resources::Count kernel_workers = 1, smith_sum = 0;
  sic::support::resources::BitCount decoder_hint = 0;
  sic::support::resources::Count generator_window = 262144;
  // Populated only from the actually selected adapted generators/prefixes.
  bool generator_metadata = false, phased_streamed_table = false;
  sic::support::resources::Count phased_representative_rows = 0, generator_rows = 0;
  sic::support::resources::Count action_generator_rows = 0, descriptor_rows = 0;
  Bytes table_reload_workspace = 0;
  Bytes extra_resident = 0;
  static constexpr sic::support::resources::Count kGenerators =
      14;  // ten principal candidates and at most four split primes

  Model(sic::support::resources::Count dimension, sic::support::resources::Count h,
        sic::support::resources::Count cycle_max, sic::support::resources::Count state_count,
        sic::support::resources::Count reps);

  Bytes group_initial() const;
  Bytes group_resident() const;
  Bytes group_closure(sic::support::resources::Count base_count,
                      sic::support::resources::Count generator_count) const;
  Bytes table(sic::support::resources::BitCount precision) const;
  Bytes quadrature_workspace(sic::support::resources::BitCount precision, int workers) const;
  Bytes quadrature(sic::support::resources::BitCount precision, int workers,
                   sic::support::resources::Count table_copies = 2) const;
  Bytes support(sic::support::resources::BitCount precision) const;
  // Populated from the actual composition tower. Folded real traces and symbolic top are
  // standard kernels. Odd physical duals remain dense until all action inverses finish.
  std::vector<sic::support::resources::Count> step_degrees, coset_counts;
  sic::support::resources::Count smith_rank = 0, band_generator_rows = 0;
  bool symbolic_top = false;
  Bytes rescaling_workspace() const;
  Bytes native_workspace(sic::support::resources::BitCount precision,
                         sic::support::resources::BitCount decoding_precision) const;
  std::array<Bytes, 5> tower_phase_forecasts(
      sic::support::resources::BitCount precision,
      sic::support::resources::BitCount decoding_precision) const;
  Bytes tower_final_buffers(sic::support::resources::BitCount precision,
                            sic::support::resources::BitCount decoding_precision) const;
  Bytes final_buffers(sic::support::resources::BitCount precision,
                      sic::support::resources::BitCount decoding_precision) const;
  Bytes newton_worker(sic::support::resources::BitCount precision, bool reference = false,
                      sic::support::resources::BitCount matrix_hint = 0, bool modular = false,
                      int phase_workers = 1, bool column_inverse = false,
                      bool threaded_product = false, bool parallel_reconstruction = false,
                      bool refined_correction = false) const;
  sic::support::resources::Count representative_rows() const;
};
}  // namespace sic::construct::resources
