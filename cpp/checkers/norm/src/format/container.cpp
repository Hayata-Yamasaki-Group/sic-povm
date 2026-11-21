// SPDX-License-Identifier: AGPL-3.0-or-later
#include "container.hpp"

#include "../runtime/diagnostics.hpp"
#include "canonical_json.hpp"
#include "input.hpp"
#include "integer_codec.hpp"
#include "metadata.hpp"
namespace sic_check::norm {
auto HashState::compress() -> void {
  constexpr uint32_t kConstants[] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
      0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
      0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
      0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
      0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
      0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
      0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
      0xc67178f2};
  uint32_t w[64]{};
  for (size_t j = 0; j < 16; ++j)
    for (size_t b = 0; b < 4; ++b) w[j] = (w[j] << 8) | block[4 * j + b];
  for (size_t j = 16; j < 64; ++j) {
    auto u = w[j - 15], v = w[j - 2];
    w[j] = w[j - 16] + (std::rotr(u, 7) ^ std::rotr(u, 18) ^ (u >> 3)) + w[j - 7] +
           (std::rotr(v, 17) ^ std::rotr(v, 19) ^ (v >> 10));
  }
  auto [a, b, c, d, e, f, g, h] = state;
  for (size_t j = 0; j < 64; ++j) {
    uint32_t t = h + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) +
                 ((e & f) ^ (~e & g)) + kConstants[j] + w[j];
    uint32_t u =
        (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    h = g;
    g = f;
    f = e;
    e = d + t;
    d = c;
    c = b;
    b = a;
    a = t + u;
  }
  std::array<uint32_t, 8> work{a, b, c, d, e, f, g, h};
  for (size_t j = 0; j < 8; ++j) state[j] += work[j];
}

auto HashState::update(std::span<const uint8_t> bytes) -> void {
  input_require(bytes.size() <= UINT64_MAX / 8 - length, "SHA-256 length overflow");
  length += bytes.size();
  for (auto byte : bytes) {
    block[used++] = byte;
    if (used == 64) {
      compress();
      used = 0;
    }
  }
}

auto HashState::finish() -> Hash {
  uint64_t bits = length * 8;
  block[used++] = 128;
  if (used > 56) {
    std::fill(block.begin() + used, block.end(), 0);
    compress();
    used = 0;
  }
  std::fill(block.begin() + used, block.begin() + 56, 0);
  for (unsigned i = 0; i < 8; ++i) block[56 + i] = uint8_t(bits >> (56 - 8 * i));
  compress();
  Hash result{};
  for (size_t j = 0; j < 32; ++j) result[j] = uint8_t(state[j / 4] >> (24 - 8 * (j % 4)));
  return result;
}

Hash digest(std::span<const uint8_t> bytes) {
  HashState state;
  state.update(bytes);
  return state.finish();
}

Hash tree_hash(const std::vector<ByteBuffer>& leaves, size_t first, size_t count) {
  if (!count) return digest({});
  ByteBuffer message{uint8_t(count == 1 ? 0 : 1)};
  if (count == 1)
    message.insert(message.end(), leaves[first].begin(), leaves[first].end());
  else {
    size_t split = std::bit_floor(count - 1);
    for (const auto& h :
         {tree_hash(leaves, first, split), tree_hash(leaves, first + split, count - split)})
      message.insert(message.end(), h.begin(), h.end());
  }
  return digest(message);
}

Hash logical_digest(const Logical& l, std::span<const uint8_t> bytes) {
  ByteBuffer prefix{'S', 'I', 'C', 'X', '1', '-', 'L', 'E', 'A', 'F', 0};
  prefix.insert(prefix.end(), l.kind.begin(), l.kind.end());
  append_le(prefix, l.element);
  append_le(prefix, l.first);
  append_le(prefix, l.count);
  append_le(prefix, bytes.size());
  HashState h;
  h.update(prefix);
  h.update(bytes);
  return h.finish();
}

std::string hash_hex(const Hash& hash) {
  constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  for (auto b : hash) {
    out += kDigits[b >> 4];
    out += kDigits[b & 15];
  }
  return out;
}

Hash parse_hash(const std::string& text) {
  input_require(text.size() == 64, "expected digest width");
  Hash hash{};
  for (size_t i = 0; i < 64; ++i) {
    char c = text[i];
    input_require((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'),
                  "expected digest lowercase hex");
    hash[i / 2] = uint8_t(16 * hash[i / 2] + (c <= '9' ? c - '0' : c - 'a' + 10));
  }
  return hash;
}

auto Reader::layout() -> void {
  input_require(!logical.empty() && logical[0].kind == "EXMT" && !logical[0].element &&
                    !logical[0].first && logical[0].count == 1,
                "EXMT logical mapping");
  size_t index = 1;
  for (size_t id = 0; id < m.elements.size(); ++id) {
    const auto& e = m.elements[id];
    uint64_t total = 1, width = leaf_width(m, e);
    for (unsigned l : e.levels) total = mul_checked(total, m.degrees[l]);
    for (uint64_t first = 0; first < total; first += width) {
      input_require(index < logical.size(), "missing logical leaf");
      const auto& l = logical[index++];
      input_require(l.kind == (e.kind == "rep" ? "BODY" : "TOWR") && l.element == id &&
                        l.first == first && l.count == width,
                    "logical layout/order/boundary");
    }
  }
  input_require(index == logical.size(), "extra logical leaf");
}

auto Reader::decode_ball(const Chunk& c) -> void {
  input_require(c.object == balls.size() && c.first == 0 && c.count == m.d &&
                    c.length == m.d * 2 * (32 + 8 * m.limbs),
                "BALL geometry/order");
  std::vector<BallPart> parts;
  int64_t worst = INT64_MIN;
  for (size_t off = 0; off < c.payload.size(); off += 32 + 8 * m.limbs) {
    BallPart b;
    uint64_t sign = little(c.payload, off);
    b.E = std::bit_cast<int64_t>(little(c.payload, off + 8));
    b.radius = little(c.payload, off + 16);
    b.e = std::bit_cast<int64_t>(little(c.payload, off + 24));
    input_require(sign <= 1 && b.E >= -(INT64_C(1) << 40) && b.E <= (INT64_C(1) << 40) &&
                      b.e >= -(INT64_C(1) << 40) && b.e <= (INT64_C(1) << 40),
                  "BALL flags/exponents");
    for (size_t j = m.limbs; j > 0; --j) {
      fmpz_mul_2exp(b.midpoint.v, b.midpoint.v, 64);
      fmpz_add_ui(b.midpoint.v, b.midpoint.v, little(c.payload, off + 32 + 8 * (j - 1)));
    }
    input_require((fmpz_is_zero(b.midpoint.v) ? (!sign && !b.E) : fmpz_is_odd(b.midpoint.v)) &&
                      (b.radius ? ((b.radius & 1) && b.radius < (1u << 30)) : b.e == 0),
                  "BALL canonical record");
    if (sign) fmpz_neg(b.midpoint.v, b.midpoint.v);
    if (b.radius) worst = std::max(worst, b.e + int64_t(std::bit_width(b.radius)));
    parts.push_back(std::move(b));
  }
  const Ray& r = m.rays.at(balls.size());
  input_require(worst != INT64_MIN && r.accuracy == -worst, "tight accuracy_bits");
  ComplexBall z;
  arb_t norm, term;
  arb_init(norm);
  arb_init(term);
  for (size_t j = 0; j < m.d; ++j) {
    ball_part_arb(acb_realref(z.v), parts[2 * j]);
    ball_part_arb(acb_imagref(z.v), parts[2 * j + 1]);
    acb_abs(term, z.v, slong(m.working_bits + 64));
    arb_sqr(term, term, slong(m.working_bits + 64));
    arb_add(norm, norm, term, slong(m.working_bits + 64));
  }
  bool normalized = arb_contains_si(norm, 1);
  arb_clear(norm);
  arb_clear(term);
  input_require(normalized, "BALL normalization");
  ball_part_arb(acb_realref(z.v), parts[2 * r.anchor]);
  ball_part_arb(acb_imagref(z.v), parts[2 * r.anchor + 1]);
  input_require(arb_is_positive(acb_realref(z.v)) && arb_is_zero(acb_imagref(z.v)), "anchor gauge");
  balls.push_back(std::move(parts));
}

auto Reader::type_at(std::span<const uint8_t> b, size_t at) -> std::string {
  input_require(at <= b.size() && b.size() - at >= 4, "chunk type bounds");
  return std::string(reinterpret_cast<const char*>(b.data() + at), 4);
}

auto Reader::element_type(const std::string& t) -> bool { return t == "TOWR" || t == "BODY"; }

auto Reader::json_type(const std::string& t) -> bool {
  return t == "EXMT" || t == "PROV" || t == "RAYS" || t == "PREC";
}

auto Reader::chunk_rules(const Chunk& c) -> void {
  input_require(element_type(c.type) || json_type(c.type) || c.type == "BALL",
                "undefined chunk type");
  input_require(c.flags == uint64_t(c.type != "PROV" && c.type != "PREC"), "chunk flags");
  input_require(c.decoded_length == c.length, "raw decoded length");
  resource(c.length <= (json_type(c.type) ? 64u << 20 : 256u << 20), "physical payload bytes");
}

auto Reader::coverage() -> void {
  uint64_t next_leaf = 0, ball_index = 0;
  // States: EXMT, elements, PROV, RAYS/BALL, PREC. The grammar is closed.
  unsigned state = 0;
  bool body = false;
  for (const auto& c : chunks) {
    if (c.type == "EXMT") {
      input_require(
          state == 0 && c.first_leaf == 0 && c.leaf_count == 1 && !c.object && !c.first && !c.count,
          "EXMT position/mapping");
      state = 1;
      next_leaf = 1;
    } else if (element_type(c.type)) {
      input_require(state == 1 && (!body || c.type == "BODY"), "element chunk order");
      body = body || c.type == "BODY";
      input_require(c.leaf_count && c.first_leaf == next_leaf && c.first_leaf < logical.size() &&
                        c.leaf_count <= logical.size() - c.first_leaf,
                    "logical ownership/coverage");
      uint64_t first = c.first, sum = 0;
      for (uint64_t li = c.first_leaf; li < c.first_leaf + c.leaf_count; ++li) {
        const auto& l = logical[size_t(li)];
        input_require(l.kind == c.type && l.element == c.object && l.first == first,
                      "physical kind/element/coordinate span");
        first = add_checked(first, l.count);
        sum = add_checked(sum, l.count);
      }
      input_require(sum == c.count && c.object < m.elements.size(), "physical coordinate count");
      next_leaf += c.leaf_count;
    } else {
      input_require(state && next_leaf == logical.size(), "non-element before complete exact data");
      input_require(!c.first_leaf && !c.leaf_count && !c.first, "non-leaf mapping");
      if (c.type == "BALL") {
        input_require(state == 3 && c.object == ball_index++ && c.count == m.d,
                      "BALL placement/index/count");
      } else {
        input_require(!c.object && !c.count, "JSON physical mapping");
        if (c.type == "PROV") {
          input_require(state == 1, "PROV placement/duplicate");
          state = 2;
        }
        if (c.type == "RAYS") {
          input_require(state <= 2, "RAYS placement/duplicate");
          state = 3;
        }
        if (c.type == "PREC") {
          input_require(state == 3 && ball_index, "PREC placement/duplicate");
          state = 4;
        }
      }
    }
  }
  input_require(next_leaf == logical.size() && (state < 3 || ball_index),
                "incomplete coverage/finalization");
}

auto Reader::json(const Chunk& c) -> Json {
  return JsonParser(std::string(c.payload.begin(), c.payload.end())).parse();
}

auto Reader::decode_element_chunk(const Chunk& c) -> void {
  size_t id = size_t(c.object);
  const auto& e = m.elements.at(id);
  if (leaves[id].empty()) {
    uint64_t field = 1;
    for (unsigned l = 0; l < e.level; ++l) field = mul_checked(field, m.degrees[l]);
    dense_coordinates = add_checked(dense_coordinates, field);
    resource(dense_coordinates <= options.max_dense_coordinates, "aggregate dense coordinates");
    leaves[id].resize(size_t(field));
  }
  Integer scale;
  input_require(fmpz_set_str(scale.v, e.scale.c_str(), 10) == 0, "scale parse");
  Cursor in(c.payload);
  for (uint64_t li = c.first_leaf; li < c.first_leaf + c.leaf_count; ++li) {
    const auto& l = logical.at(size_t(li));
    size_t begin = in.position();
    input_require(in.varint() == id && in.varint() == l.first && in.varint() == l.count,
                  "logical prefix/directory disagreement");
    uint64_t covered = 0;
    int previous = -1;
    while (covered < l.count) {
      uint64_t run = in.varint(), length = run / 2;
      int type = int(run & 1);
      input_require(length && length <= l.count - covered && type != previous,
                    "run length/alternation/coverage");
      previous = type;
      for (uint64_t u = 0; u < length; ++u, ++covered) {
        if (!type) continue;
        Pair v;
        v.x = in.integer();
        v.y = in.integer();
        input_require(m.D0 != 1 || fmpz_is_zero(v.y.v), "rational second component");
        undo_pair(v, m.D0);
        input_require(!fmpz_is_zero(v.x.v) || !fmpz_is_zero(v.y.v), "zero in data run");
        fmpz_mul(v.x.v, v.x.v, scale.v);
        fmpz_mul(v.y.v, v.y.v, scale.v);
        input_require(std::max(fmpz_bits(v.x.v), fmpz_bits(v.y.v)) <= e.max_bits,
                      "leaf exceeds max_bits");
        size_t full = 0, stride = 1, sub = size_t(l.first + covered), sel = 0;
        for (unsigned level = 0; level < e.level; ++level) {
          if (sel < e.levels.size() && e.levels[sel] == level) {
            full += (sub % m.degrees[level]) * stride;
            sub /= m.degrees[level];
            ++sel;
          }
          stride *= m.degrees[level];
        }
        leaves[id][full] = std::move(v);
      }
    }
    size_t length = in.position() - begin;
    resource(length <= (256u << 20), "logical leaf bytes");
    // Every accepted varint, integer, run and pair has a unique inverse encoding;
    // explicit re-encoding additionally guards the implementation of that inverse.
    auto bytes = std::span(c.payload).subspan(begin, length);
    input_require(logical_digest(l, bytes) == l.hash, "logical SHA-256");
    input_require(reencode_leaf(l, scale) == ByteBuffer(bytes.begin(), bytes.end()),
                  "logical re-encoding");
    loaded_coordinates[id] += l.count;
  }
  in.end();
}
}  // namespace sic_check::norm
