// SPDX-License-Identifier: AGPL-3.0-or-later
#include "integer_codec.hpp"

#include "../runtime/diagnostics.hpp"
#include "canonical_json.hpp"
#include "container.hpp"
#include "input.hpp"
#include "metadata.hpp"
namespace sic_check::norm {
uint64_t little(std::span<const uint8_t> data, size_t pos, unsigned width) {
  input_require(width <= 8 && pos <= data.size() && width <= data.size() - pos,
                "truncated integer");
  uint64_t v = 0;
  for (unsigned j = 0; j < width; ++j) v |= uint64_t(data[pos + j]) << (8 * j);
  return v;
}

Cursor::Cursor(std::span<const uint8_t> data) : b(data) {}

auto Cursor::position() const -> size_t { return p; }

auto Cursor::varint() -> uint64_t {
  uint64_t v = 0;
  for (unsigned j = 0; j < 10; ++j) {
    input_require(p < b.size(), "truncated varint");
    unsigned c = b[p++];
    input_require(j < 9 || c <= 1, "varint overflow");
    v |= uint64_t(c & 127) << (7 * j);
    if (!(c & 128)) {
      input_require(j == 0 || c != 0, "overlong varint");
      return v;
    }
  }
  input_require(false, "varint width");
  return 0;
}

auto Cursor::integer() -> Integer {
  uint64_t code = varint(), n = code / 2;
  resource(n <= (1u << 20), "integer magnitude");
  input_require(n <= b.size() - p && code != 1, "integer length/sign");
  input_require(!n || b[p + n - 1] != 0, "integer leading zero");
  Integer z;
  for (size_t j = n; j > 0; --j) {
    fmpz_mul_2exp(z.v, z.v, 8);
    fmpz_add_ui(z.v, z.v, b[p + j - 1]);
  }
  if (code & 1) fmpz_neg(z.v, z.v);
  p += n;
  return z;
}

auto Cursor::end() const -> void { input_require(p == b.size(), "block trailing bytes"); }

void append_le(ByteBuffer& b, uint64_t v, unsigned n) {
  for (unsigned i = 0; i < n; ++i) b.push_back(uint8_t(v >> (8 * i)));
}

auto Reader::leaf_width(const Meta& meta, const Element& e) -> uint64_t {
  uint64_t width = 1;
  for (unsigned l : e.levels) {
    if (width > (1u << 14) / meta.degrees[l]) break;
    width *= meta.degrees[l];
  }
  return width;
}

auto Reader::undo_pair(Pair& v, int64_t D0) -> void {
  if ((D0 % 4 + 4) % 4 != 1 || D0 == 1) return;
  Integer shift, tmp;
  if (D0 < 0) {
    fmpz_fdiv_q_2exp(shift.v, v.y.v, 1);
    fmpz_sub(v.x.v, v.x.v, shift.v);
  } else if (!fmpz_is_zero(v.y.v)) {
    fmpz_mul(tmp.v, v.y.v, v.y.v);
    fmpz_mul_si(tmp.v, tmp.v, D0);
    fmpz_sqrt(shift.v, tmp.v);
    if (fmpz_sgn(v.y.v) < 0) {
      fmpz_neg(shift.v, shift.v);
      fmpz_sub_ui(shift.v, shift.v, 1);
    }
    fmpz_sub(shift.v, shift.v, v.y.v);
    fmpz_fdiv_q_2exp(shift.v, shift.v, 1);
    fmpz_add(v.x.v, v.x.v, shift.v);
  }
}

auto Reader::varput(ByteBuffer& out, uint64_t v) -> void {
  do {
    uint8_t c = uint8_t(v & 127);
    v >>= 7;
    out.push_back(uint8_t(c | (v ? 128 : 0)));
  } while (v);
}

auto Reader::intput(ByteBuffer& out, const Integer& x) -> void {
  uint64_t n = (fmpz_bits(x.v) + 7) / 8;
  varput(out, 2 * n + (fmpz_sgn(x.v) < 0));
  Integer a;
  fmpz_abs(a.v, x.v);
  for (uint64_t i = 0; i < n; ++i) {
    out.push_back(uint8_t(fmpz_get_ui(a.v) & 255));
    fmpz_fdiv_q_2exp(a.v, a.v, 8);
  }
}

auto Reader::reencode_leaf(const Logical& l, const Integer& scale) const -> ByteBuffer {
  const auto& e = m.elements[size_t(l.element)];
  ByteBuffer out;
  varput(out, l.element);
  varput(out, l.first);
  varput(out, l.count);
  std::vector<Pair> values(size_t(l.count));
  for (size_t i = 0; i < l.count; ++i) {
    size_t sub = size_t(l.first) + i, full = 0, stride = 1, sel = 0;
    for (unsigned level = 0; level < e.level; ++level) {
      if (sel < e.levels.size() && e.levels[sel] == level) {
        full += (sub % m.degrees[level]) * stride;
        sub /= m.degrees[level];
        ++sel;
      }
      stride *= m.degrees[level];
    }
    fmpz_divexact(values[i].x.v, leaves[size_t(l.element)][full].x.v, scale.v);
    fmpz_divexact(values[i].y.v, leaves[size_t(l.element)][full].y.v, scale.v);
  }
  auto nonzero = [&](size_t i) {
    return !fmpz_is_zero(values[i].x.v) || !fmpz_is_zero(values[i].y.v);
  };
  for (size_t i = 0; i < values.size();) {
    size_t end = i + 1;
    bool data = nonzero(i);
    while (end < values.size() && nonzero(end) == data) ++end;
    varput(out, 2 * (end - i) + data);
    for (; i < end; ++i)
      if (data) {
        Pair zero;
        zero.y = values[i].y;
        undo_pair(zero, m.D0);  // inverse adds k(y), or subtracts floor(y/2).
        fmpz_sub(values[i].x.v, values[i].x.v, zero.x.v);
        intput(out, values[i].x);
        intput(out, values[i].y);
      }
  }
  return out;
}
}  // namespace sic_check::norm
