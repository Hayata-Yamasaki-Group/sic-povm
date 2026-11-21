// SPDX-License-Identifier: AGPL-3.0-or-later
// Producer-internal (sic-construct): lattice coordinates of kept representative spools.
//
// Every representative spool is kept until the canonical generator rescaling is known.
// A spool block stores (x, y) of beta = x + y omega; in lattice coordinates (r, y) = (x - shift(y),
// y) (sic::exact::LatticeCoordinates of D0, the same transform as the SICX pair transform) r is of
// the size of the conjugate embedding, so the raw spool shrinks to about the SICX size. The
// transform is exact and invertible; it is applied to whole blocks by all workers (each with its
// own square-root scratch). Nothing of it reaches the output: every reader decodes before use.
#pragma once

#include <algorithm>
#include <span>

#include "exact/runtime/parallel.hpp"
#include "exact/storage/leaf_store.hpp"
#include "support/lattice_coordinates.hpp"

namespace sic::exact::representative_lattice {

// Encodes (decode = false) or decodes (decode = true) every leaf of `block` in place.
void transform(std::span<sic::exact::Leaf> block, long D0, int threads, bool decode);

// Scratch of the workers (squares of y times D0 and the shift), for memory admissions.
inline unsigned long long scratch_bytes(unsigned long long bits, int threads) {
  return static_cast<unsigned long long>(std::max(1, threads)) * 16ull *
         (32ull + (2ull * bits + 71ull) / 8ull);
}

}  // namespace sic::exact::representative_lattice
