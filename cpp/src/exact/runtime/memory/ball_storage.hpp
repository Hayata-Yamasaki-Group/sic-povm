// SPDX-License-Identifier: AGPL-3.0-or-later
// Inspect retained FLINT allocations; precision is not an allocation capacity.
#pragma once

#include <flint/acb_mat.h>

#include <span>

#include "support/resources.hpp"

namespace sic::exact::phase_memory {

struct BallStorage {
  sic::support::resources::Count values = 0;
  sic::support::resources::Bytes header_bytes = 0, heap_bytes = 0;
  slong largest_mantissa = 0;
  bool finite_lagom = true;

  sic::support::resources::Bytes bytes() const {
    return sic::support::resources::add(header_bytes, heap_bytes);
  }
  void append(const BallStorage& other);
  void observe(acb_srcptr value);
};

// Includes unused vector capacity and FLINT-reported mantissa/radius/exponent
// allocations. It excludes allocator bookkeeping, library caches and stacks;
// those require independent allowances in a phase forecast. No ball mutates.
BallStorage inspect_storage(std::span<const acb_struct> values);

BallStorage inspect_storage(const std::vector<acb_struct>& values);

// This is uniquely owned storage only for full owning matrices. Windows
// report borrowed entries and must not be added as another owner.
BallStorage inspect_storage(const acb_mat_struct* matrix);

}  // namespace sic::exact::phase_memory
