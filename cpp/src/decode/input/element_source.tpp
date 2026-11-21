// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::decode {
template <class Leaf>
uint64_t stream_leaf_payload(const Meta& meta, const IndexEntry& entry,
                             std::span<const uint8_t> payload, uint64_t& base, Leaf leaf) {
  const auto& e = meta.elements.at(entry.object);
  base = e.layout.position(entry.first);
  FmpzPair scale_storage;
  fmpz* scale = scale_storage.x;
  fmpz_set_str(scale, e.scale.c_str(), 10);
  IntCodec codec;
  PairTransform transform(meta.D0);
  FmpzPair value, full;
  decode_block(
      payload, meta,
      [&](uint64_t s, const fmpz_t x, const fmpz_t y) {
        fmpz_mul(full.x, x, scale);
        fmpz_mul(full.y, y, scale);
        if (std::max(fmpz_bits(full.x), fmpz_bits(full.y)) > e.max_bits)
          fail(sic::ErrorKind::invalid_format, "leaf exceeds max_bits");
        leaf(e.layout.position(s) - base, full.x, full.y);
      },
      [](uint64_t, uint64_t) {}, codec, transform, value);
  return payload.size();
}

template <class Leaf>
uint64_t stream_leaf_block(const Reader& reader, size_t chunk_index, uint64_t& base, Leaf leaf) {
  const auto payload = reader.leaf(chunk_index);
  return stream_leaf_payload(reader.meta(), reader.leaf_entry(chunk_index), payload, base, leaf);
}

template <class Leaf>
uint64_t stream_verified_leaf(const Meta& meta, const IndexEntry& entry,
                              const Reader::ElementChunk& unit, size_t index, uint64_t& base,
                              Leaf leaf) {
  const auto payload = unit.leaf(index);  // also checks the global leaf index
  if (unit.parsed->empty()) return stream_leaf_payload(meta, entry, payload, base, leaf);
  const auto& e = meta.elements.at(entry.object);
  base = e.layout.position(entry.first);
  const auto& parsed = unit.parsed->at(index - unit.first_leaf);
  if (!parsed.count) return payload.size();
  Cursor in(parsed.values.data(), parsed.values.size());
  IntCodec codec;
  FmpzPair value;
  for (size_t j = 0; j < parsed.count; ++j) {
    const auto subindex = in.varint();
    codec.get(in, value.x);
    codec.get(in, value.y);
    leaf(e.layout.position(subindex) - base, value.x, value.y);
  }
  if (in.remaining()) fail(sic::ErrorKind::invalid_format, "retained integer framing mismatch");
  return payload.size();
}

}  // namespace sic::decode
