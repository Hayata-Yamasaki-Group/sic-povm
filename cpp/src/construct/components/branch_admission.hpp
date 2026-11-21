// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <vector>

#include "support/resources.hpp"
namespace sic::construct {
// compute_path calls inverse_leaf_tree with labels={0}. Each need/parent vector therefore
// has one label. Above the1024-leaf cutoff, recursion retains one output cell per level;
// at/below it only values and next coexist. Current candidate roots/duals and one lookahead
// candidate coexist. Polynomial/native allocator workspace has an engineering allowance.
// Existing tables, integer leaves and any previous Path are already in the measured RSS.
sic::support::resources::Bytes branch_scratch_bytes(const std::vector<long>& degrees,
                                                    sic::support::resources::BitCount precision,
                                                    sic::support::resources::BitCount leaf_bits,
                                                    sic::support::resources::Bytes source_reader);
}  // namespace sic::construct
