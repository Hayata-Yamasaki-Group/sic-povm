// SPDX-License-Identifier: AGPL-3.0-or-later
// Phase forecast shared by tracing and explicit retained-storage admission.
#pragma once

#include <array>

#include "support/resources.hpp"

namespace sic::exact::phase_memory {

using sic::support::resources::BitCount;
using sic::support::resources::Bytes;
using sic::support::resources::Count;

enum class Phase : size_t {
  roots,
  projection,
  selection,
  inverse,
  certification,
  high_newton,
  reconstruction,
  count
};
inline constexpr std::array<const char*, static_cast<size_t>(Phase::count)> kPhaseNames{
    "root identification", "seed projection", "row selection", "inverse",
    "B0/B1 certification", "high Newton",     "reconstruction"};

// Y, retained M0, current J, and current Mloc. J is destroyed before
// copying Mloc to M0; residual caches die before the product starts.
inline constexpr Count kCertificationMatrixOwners = 4;

// All bit fields are RETAINED STORAGE ceilings, including prior attempts.
// Neither matrix_hint nor the current arithmetic precision establishes them.
// Actual allocation inspection and finite Lagom checks must validate the
// contract before any future use in admission. Unknown ceilings require
// refusal/re-admission; they must never truncate certificate precision.
struct Ceilings {
  Count dimension = 0;
  BitCount source_bits = 0, selection_bits = 0;
  // high_bits includes B0 arithmetic and all retained high states/contexts,
  // not just the requested final output precision.
  BitCount inverse_bits = 0, stored_matrix_bits = 0, high_bits = 0;
  Count candidate_rows = 0;
  BitCount previous_output_bits = 0;  // zero means no retained output
  // Raw FLINT/vector counts from inspect_storage(), excluding allocator
  // bookkeeping. A nonzero count gets additional per-value allocator slack.
  Bytes source_storage_bytes = 0, previous_output_storage_bytes = 0;
  int workers = 0;
  bool modular = false, column_inverse = false, threaded_product = false;
  bool refined_correction = false, parallel_reconstruction = false;
  Bytes native_scalar_bytes = 0, native_complex_bytes = 0;
};

struct Accounting {
  std::array<Bytes, static_cast<size_t>(Phase::count)> totals{};
  Bytes small_objects = 0, source_grid = 0, previous_output = 0;
  Bytes matrix = 0, empty_matrix = 0, reshape = 0;

  Bytes at(Phase phase) const { return totals.at(static_cast<size_t>(phase)); }
  Bytes peak() const { return *std::max_element(totals.begin(), totals.end()); }
};

// Includes matrix headers/row pointers, even when entries are empty. The
// precision payload charge already includes each acb_struct and scalar
// allocator allowances, so add only matrix-level metadata here.
Bytes matrix_bytes(Count rows, Count columns, Bytes scalar_bytes);

Accounting forecast(const Ceilings& c);

}  // namespace sic::exact::phase_memory
