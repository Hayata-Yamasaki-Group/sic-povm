// SPDX-License-Identifier: AGPL-3.0-or-later
#include "integer_codec.hpp"

#include "../runtime/diagnostics.hpp"
#include "ball_codec.hpp"
#include "canonical_json.hpp"
#include "container.hpp"
#include "metadata.hpp"
#include "sha256.hpp"
namespace sic_check::verifier {
Uint64 le64(const Uint8* p) {
  Uint64 v = 0;
  for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
  return v;
}

uint32_t le32(const Uint8* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

uint32_t le16(const Uint8* p) { return p[0] | p[1] << 8; }

Uint64 rd_varint(Cur& c) {
  Uint64 v = 0;
  for (int i = 0; i < 10; i++) {
    if (c.p >= c.e) reject("varint: truncated");
    Uint8 b = *c.p++;
    if (i == 9 && (b & 0x7e)) reject("varint: value >= 2^64 or 10th byte invalid");
    v |= (Uint64)(b & 0x7f) << (7 * i);
    if (!(b & 0x80)) {
      if (i > 0 && b == 0) reject("varint: non-canonical (redundant trailing 0x00 group)");
      return v;
    }
    if (i == 9) reject("varint: longer than 10 bytes");
  }
  reject("varint");
}

void rd_int(Cur& c, fmpz_t out) {
  Uint64 code = rd_varint(c), n = code >> 1;
  int sign = code & 1;
  if (n == 0) {
    if (sign) reject("int: negative zero (code 1)");
    fmpz_zero(out);
    return;
  }
  if (n > kIntegerLimit) throw PrecErr("int: magnitude longer than limit");
  if ((Uint64)(c.e - c.p) < n) reject("int: truncated magnitude");
  if (c.p[n - 1] == 0) reject("int: most significant magnitude byte is zero");
  size_t nl = (n + 7) / 8;
  vector<ulong> limbs(nl, 0);
  for (Uint64 i = 0; i < n; i++) limbs[i / 8] |= (ulong)c.p[i] << (8 * (i % 8));
  c.p += n;
  fmpz_set_ui_array(out, limbs.data(), (slong)nl);
  if (sign) fmpz_neg(out, out);
}

void enc_varint(vector<Uint8>& o, Uint64 v) {
  while (v >= 0x80) {
    o.push_back((Uint8)(v | 0x80));
    v >>= 7;
  }
  o.push_back((Uint8)v);
}

void enc_int(vector<Uint8>& o, const fmpz_t x) {
  if (fmpz_is_zero(x)) {
    o.push_back(0);
    return;
  }
  Integer a;
  fmpz_abs(a, x);
  size_t nb = (fmpz_bits(a) + 7) / 8;
  enc_varint(o, ((Uint64)nb << 1) | (fmpz_sgn(x) < 0 ? 1 : 0));
  Integer t;
  for (size_t i = 0; i < nb; i++) {
    fmpz_fdiv_q_2exp(t, a, 8 * i);
    o.push_back((Uint8)fmpz_fdiv_ui(t, 256));
  }
}

int xmode(const fmpz_t D0) {
  if (fmpz_fdiv_ui(D0, 4) != 1) return 0;
  if (fmpz_cmp_ui(D0, 1) > 0) return 1;
  if (fmpz_sgn(D0) < 0) return 2;
  return 0;
}

void x_from_r(fmpz_t x, const fmpz_t r, const fmpz_t y, const fmpz_t D0) {
  int m = xmode(D0);
  fmpz_t k;
  fmpz_init(k);
  if (m == 1) {
    kfun(k, y, D0);
    fmpz_add(x, r, k);
  } else if (m == 2) {
    fmpz_fdiv_q_2exp(k, y, 1);
    fmpz_sub(x, r, k);
  } else
    fmpz_set(x, r);
  fmpz_clear(k);
}

void r_from_x(fmpz_t r, const fmpz_t x, const fmpz_t y, const fmpz_t D0) {
  int m = xmode(D0);
  fmpz_t k;
  fmpz_init(k);
  if (m == 1) {
    kfun(k, y, D0);
    fmpz_sub(r, x, k);
  } else if (m == 2) {
    fmpz_fdiv_q_2exp(k, y, 1);
    fmpz_add(r, x, k);
  } else
    fmpz_set(r, x);
  fmpz_clear(k);
}

void kfun(fmpz_t k, const fmpz_t y, const fmpz_t D0) {
  if (fmpz_is_zero(y)) {
    fmpz_zero(k);
    return;
  }
  Integer a, t;
  fmpz_mul(t, y, y);
  fmpz_mul(t, t, D0);
  fmpz_sqrt(a, t);
  if (fmpz_sgn(y) < 0) {
    fmpz_neg(a, a);
    fmpz_sub_ui(a, a, 1);
  }
  fmpz_sub(a, a, y);
  fmpz_fdiv_q_2exp(k, a, 1);
}

void parse_block(const Uint8* b, size_t n, Uint64 exp_elem, Uint64 exp_first, Uint64 exp_count,
                 const fmpz_t D0, bool xform,
                 const std::function<void(Uint64, const fmpz_t, const fmpz_t)>& sink,
                 const std::function<void(Uint64 /*s0*/, Uint64 /*len*/)>* zsink,
                 size_t* consumed) {
  Cur c{b, b + n};
  if (rd_varint(c) != exp_elem) reject("block: element id mismatch");
  if (rd_varint(c) != exp_first) reject("block: first mismatch");
  if (rd_varint(c) != exp_count) reject("block: count mismatch");
  bool use = xform && xmode(D0) != 0;
  Uint64 covered = 0;
  int last = -1;
  Integer r, y, x, k;
  while (covered < exp_count) {
    Uint64 tag = rd_varint(c), nn = tag >> 1;
    int kind = tag & 1;
    if (nn == 0) reject("block: empty run");
    if (kind == last) reject("block: runs do not alternate");
    if (nn > exp_count - covered) reject("block: runs overflow the coordinate count");
    last = kind;
    if (!kind) {
      if (zsink) (*zsink)(covered, nn);
      covered += nn;
      continue;
    }
    for (Uint64 i = 0; i < nn; i++) {
      rd_int(c, r);
      rd_int(c, y);
      if (fmpz_is_one(D0) && !fmpz_is_zero(y)) reject("rational base requires y = 0");
      if (use)
        x_from_r(x, r, y, D0);
      else
        fmpz_set(x, r);
      if (fmpz_is_zero(x) && fmpz_is_zero(y)) reject("block: (0,0) inside a data run");
      sink(exp_first + covered + i, x, y);
    }
    covered += nn;
  }
  if (consumed)
    *consumed = c.p - b;
  else if (c.p != c.e)
    reject("block: trailing bytes after runs");
}

auto Encoder::flush_data() -> void {
  if (ndata) {
    enc_varint(out, ndata * 2 + 1);
    out.insert(out.end(), data.begin(), data.end());
    data.clear();
    ndata = 0;
  }
}

auto Encoder::start(Uint64 el, Uint64 first, Uint64 count) -> void {
  out.clear();
  data.clear();
  ndata = 0;
  pos = 0;
  enc_varint(out, el);
  enc_varint(out, first);
  enc_varint(out, count);
}

auto Encoder::nonzero(Uint64 srel, const fmpz_t x, const fmpz_t y, const fmpz_t D0) -> void {
  if (srel > pos) {
    flush_data();
    enc_varint(out, (srel - pos) * 2);
    pos = srel;
  }
  Integer r;
  r_from_x(r, x, y, D0);
  enc_int(data, r);
  enc_int(data, y);
  ndata++;
  pos = srel + 1;
}

auto Encoder::finish(Uint64 count) -> void {
  flush_data();
  if (count > pos) enc_varint(out, (count - pos) * 2);
}

string hexz(const fmpz_t x) {
  std::unique_ptr<char, decltype(&flint_free)> p(fmpz_get_str(nullptr, 16, x), flint_free);
  string s = p.get();
  return s;
}

Uint64 checked_add(Uint64 a, Uint64 b) {
  if (b > UINT64_MAX - a) reject("integer addition overflow");
  return a + b;
}

Uint64 checked_mul(Uint64 a, Uint64 b) {
  if (b && a > UINT64_MAX / b) reject("integer product overflow");
  return a * b;
}
}  // namespace sic_check::verifier
