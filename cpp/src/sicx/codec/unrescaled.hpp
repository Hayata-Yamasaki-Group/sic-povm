// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sicx/codec/rescale.hpp"

namespace sic::sicx {
// Exact inverse of the shared rescaled_source. The input stream yields stored full leaves
// (primitive pairs times the recorded element scale); the result yields pre-rescaling leaves.
LeafSource unrescaled_source(const Meta& meta, const ElementId& id, LeafSource source,
                             std::vector<uint64_t> shifts);
LeafSource unrescaled_source(const Meta& meta, const ElementId& id, LeafSource source);

}  // namespace sic::sicx
