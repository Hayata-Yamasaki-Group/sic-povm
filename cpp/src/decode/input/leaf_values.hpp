// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>

#include "sic/sicx/format.hpp"
#include "sicx/codec/block.hpp"
namespace sic::decode {
using sic::sicx::fail;
using sic::sicx::FmpzPair;
// ------------------------------------------------------------ element sources
// Leaves of a tower/action element: global positions (increasing) and exact
// full leaves lambda = scale * c, parsed from hash-checked blocks.
struct LeafBlock {
  uint64_t base = 0;
  std::vector<uint64_t> pos;  // relative to base
  std::vector<std::unique_ptr<FmpzPair>> values;
  uint64_t bytes = 0;
};
struct ElementLeaves {
  std::vector<LeafBlock> blocks;
  std::vector<uint64_t> pos;  // global, increasing
  std::vector<const FmpzPair*> value;
  uint64_t payload_bytes = 0;
  bool loaded = false;
  void index();
  void clear();
};

}  // namespace sic::decode
