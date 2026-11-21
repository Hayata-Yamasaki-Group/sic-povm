// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdint>
namespace sic::decode {
// Checked depth advance for the bounded restriction graph.
uint64_t next_graph_depth(uint64_t depth);
}  // namespace sic::decode
