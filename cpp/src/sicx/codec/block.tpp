// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Arbitrary synchronous block callbacks; borrowed FLINT values cannot escape the call.
namespace sic::sicx {
template <class Get>
ByteBuffer BlockEncoder::encode(uint64_t element, uint64_t first, uint64_t count, size_t items,
                                Get get) {
  ByteBuffer out;
  put_varint(out, element);
  put_varint(out, first);
  put_varint(out, count);
  uint64_t at = first;
  size_t i = 0;
  while (i < items) {
    const auto [s, x, y] = get(i);
    if (s < at || s >= first + count)
      fail(sic::ErrorKind::invalid_format, "encoder input out of order");
    if (s > at) put_varint(out, 2 * (s - at));
    size_t j = i + 1;
    uint64_t next = s + 1;
    while (j < items && std::get<0>(get(j)) == next) {
      ++j;
      ++next;
    }
    put_varint(out, 2 * (j - i) + 1);
    for (size_t q = i; q < j; ++q) {
      const auto [s2, xq, yq] = get(q);
      (void)s2;
      if (fmpz_is_zero(xq) && fmpz_is_zero(yq))
        fail(sic::ErrorKind::invalid_format, "encoder received a zero pair");
      fmpz_set(r, xq);
      transform.encode(r, yq);
      codec.put(out, r);
      codec.put(out, yq);
    }
    at = next;
    i = j;
  }
  if (at < first + count) put_varint(out, 2 * (first + count - at));
  return out;
}

template <class Pair, class Zeros>
BlockHeader decode_block(std::span<const uint8_t> payload, const Meta& meta, Pair pair, Zeros zeros,
                         IntCodec& codec, PairTransform& transform, FmpzPair& value) {
  Cursor in(payload.data(), payload.size());
  BlockHeader h{in.varint(), in.varint(), in.varint()};
  if (h.element >= meta.elements.size())
    fail(sic::ErrorKind::invalid_format, "block element out of range");
  const auto& e = meta.elements[h.element];
  if (h.count != e.block || h.first % e.block || h.first >= e.layout.coordinates())
    fail(sic::ErrorKind::invalid_format, "block does not follow the block rule");
  uint64_t at = h.first;
  int last = -1;
  const uint64_t end = h.first + h.count;
  while (at < end) {
    const uint64_t tag = in.varint();
    const int kind = static_cast<int>(tag & 1);
    const uint64_t n = tag >> 1;
    if (!n || n > end - at) fail(sic::ErrorKind::invalid_format, "run length out of range");
    if (kind == last) fail(sic::ErrorKind::invalid_format, "adjacent runs of the same kind");
    last = kind;
    if (!kind) {
      zeros(at, n);
      at += n;
      continue;
    }
    for (uint64_t q = 0; q < n; ++q, ++at) {
      codec.get(in, value.x);
      codec.get(in, value.y);
      transform.decode(value.x, value.y);
      if (meta.D0 == 1 && !fmpz_is_zero(value.y))
        fail(sic::ErrorKind::invalid_format, "nonzero rational-field second component");
      if (fmpz_is_zero(value.x) && fmpz_is_zero(value.y))
        fail(sic::ErrorKind::invalid_format, "zero pair inside a data run");
      pair(at, value.x, value.y);
    }
  }
  if (in.remaining()) fail(sic::ErrorKind::invalid_format, "trailing bytes in block");
  return h;
}
}  // namespace sic::sicx
