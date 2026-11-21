// SPDX-License-Identifier: AGPL-3.0-or-later
#include "decode/input/element_source.hpp"

namespace sic::decode {
LeafBlock decode_leaf_block(const Reader& reader, size_t chunk_index) {
  LeafBlock block;
  block.bytes = stream_leaf_block(reader, chunk_index, block.base,
                                  [&](uint64_t pos, const fmpz_t x, const fmpz_t y) {
                                    auto pair = std::make_unique<FmpzPair>();
                                    fmpz_set(pair->x, x);
                                    fmpz_set(pair->y, y);
                                    block.pos.push_back(pos);
                                    block.values.push_back(std::move(pair));
                                  });
  return block;
}

}  // namespace sic::decode
