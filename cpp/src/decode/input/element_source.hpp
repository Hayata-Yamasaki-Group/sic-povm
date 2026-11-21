// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "decode/input/leaf_values.hpp"
#include "sic/sicx/io.hpp"
#include "sicx/codec/block.hpp"
namespace sic::decode {
using namespace sic::sicx;
// Streams the leaves of one block (in increasing position) to `leaf(pos_relative_to_base, x, y)`.
template <class Leaf>
uint64_t stream_leaf_payload(const Meta& meta, const IndexEntry& entry,
                             std::span<const uint8_t> payload, uint64_t& base, Leaf leaf);

template <class Leaf>
uint64_t stream_leaf_block(const Reader& reader, size_t chunk_index, uint64_t& base, Leaf leaf);

// The retained integers are the exact full lambda coordinates produced by the
// complete physical-unit verifier. Zero gaps and logical rounding boundaries
// remain identical to the ordinary stable-byte streaming path.
template <class Leaf>
uint64_t stream_verified_leaf(const Meta& meta, const IndexEntry& entry,
                              const Reader::ElementChunk& unit, size_t index, uint64_t& base,
                              Leaf leaf);

LeafBlock decode_leaf_block(const Reader& reader, size_t chunk_index);

}  // namespace sic::decode

#include "decode/input/element_source.tpp"
