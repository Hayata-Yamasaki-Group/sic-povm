// SPDX-License-Identifier: AGPL-3.0-or-later
#include "integer_codec.hpp"

#include "../runtime/diagnostics.hpp"
#include "canonical_json.hpp"
#include "container.hpp"
#include "metadata.hpp"
#include "sicx_input.hpp"
namespace sic_check::crt::sicx {
Uint64 add(Uint64 a, Uint64 b) {
  if (b > UINT64_MAX - a) throw Reject("SICX input: addition overflow");
  return a + b;
}

Uint64 mul(Uint64 a, Uint64 b) {
  if (a && b > UINT64_MAX / a) throw Reject("SICX input: multiplication overflow");
  return a * b;
}

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
    if (i == 9 && (b & 0x7e)) reject("varint: value >= 2^64");
    v |= (Uint64)(b & 0x7f) << (7 * i);
    if (!(b & 0x80)) {
      if (i > 0 && b == 0) reject("varint: non-canonical");
      return v;
    }
  }
  reject("varint: longer than 10 bytes");
}

void rd_int(Cur& c, fmpz_t out) {
  Uint64 code = rd_varint(c), n = code >> 1;
  int sign = code & 1;
  if (n == 0) {
    if (sign) reject("int: negative zero");
    fmpz_zero(out);
    return;
  }
  if (n > (1u << 20)) resource("int: magnitude longer than 2^20 bytes");
  if ((Uint64)(c.e - c.p) < n) reject("int: truncated magnitude");
  if (c.p[n - 1] == 0) reject("int: most significant magnitude byte is zero");
  size_t nl = (n + 7) / 8;
  vector<ulong> limbs(nl, 0);
  for (Uint64 i = 0; i < n; i++) limbs[i / 8] |= (ulong)c.p[i] << (8 * (i % 8));
  c.p += n;
  fmpz_set_ui_array(out, limbs.data(), (slong)nl);
  if (sign) fmpz_neg(out, out);
}

void kfun(fmpz_t k, const fmpz_t y, const fmpz_t D0) {
  if (fmpz_is_zero(y)) {
    fmpz_zero(k);
    return;
  }
  fmpz_t a, t;
  fmpz_init(a);
  fmpz_init(t);
  fmpz_mul(t, y, y);
  fmpz_mul(t, t, D0);
  fmpz_sqrt(a, t);
  if (fmpz_sgn(y) < 0) {
    fmpz_neg(a, a);
    fmpz_sub_ui(a, a, 1);
  }
  fmpz_sub(a, a, y);
  fmpz_fdiv_q_2exp(k, a, 1);
  fmpz_clear(a);
  fmpz_clear(t);
}

void put64(vector<Uint8>& b, Uint64 v) {
  for (int i = 0; i < 8; ++i) b.push_back((Uint8)(v >> (8 * i)));
}

void wr_varint(vector<Uint8>& b, Uint64 v) {
  do {
    Uint8 x = v & 127;
    v >>= 7;
    b.push_back(x | (v ? 128 : 0));
  } while (v);
}

void wr_int(vector<Uint8>& b, const fmpz_t z) {
  Uint64 n = (fmpz_bits(z) + 7) / 8;
  wr_varint(b, 2 * n + (fmpz_sgn(z) < 0));
  Z a;
  fmpz_abs(a.v, z);
  for (Uint64 j = 0; j < n; ++j) {
    b.push_back((Uint8)fmpz_get_ui(a.v));
    fmpz_fdiv_q_2exp(a.v, a.v, 8);
  }
}

auto Input::check_element(size_t id, const vector<Pair>& pairs) const -> void {
  const auto& e = el[id];
  Z gcd, scale, tmp;
  fmpz_set_str(scale.v, e.scale.c_str(), 10);
  Uint64 max = 0;
  vector<bool> active(e.levels.size(), false);
  for (const auto& p : pairs) {
    fmpz_gcd(gcd.v, gcd.v, p.x.v);
    fmpz_gcd(gcd.v, gcd.v, p.y.v);
    fmpz_mul(tmp.v, p.x.v, scale.v);
    max = std::max<Uint64>(max, fmpz_bits(tmp.v));
    fmpz_mul(tmp.v, p.y.v, scale.v);
    max = std::max<Uint64>(max, fmpz_bits(tmp.v));
    Uint64 sub = p.sub;
    for (size_t j = 0; j < e.levels.size(); ++j) {
      Uint64 degree = deg[e.levels[j]];
      if (sub % degree) active[j] = true;
      sub /= degree;
    }
  }
  if (pairs.empty()) {
    if (!e.levels.empty() || e.scale != "1" || e.max_bits) reject("zero element convention");
  } else if (!fmpz_is_one(gcd.v))
    reject("element coordinates not primitive");
  if (max != e.max_bits) reject("max_bits is not attained");
  for (bool a : active)
    if (!a) reject("nonminimal element level set");
  scope.elements.insert(id);
}

auto Input::weights(const Elem& e, Uint64 sub) const -> vector<Uint64> {
  vector<Uint64> w(deg.size(), 0);
  for (int i : e.levels) {
    w[i] = sub % deg[i];
    sub /= deg[i];
  }
  if (e.kind != "rep") {
    Uint64 p = deg[e.level], C = p * (p - 1) / 2;
    Uint64 own = (e.kind == "wsq" || e.kind == "act") ? 2
                 : e.kind == "fcoef"                  ? p - e.k
                 : e.kind == "actdelta"               ? C
                                                      : C + 1 - e.k;
    w[e.level] = add(w[e.level], own);
  }
  return w;
}

auto Input::check_rescaling(const vector<Constraint>& constraints) const -> void {
  vector<Uint64> gain(t(), 0), order(t());
  for (const auto& c : constraints) {
    auto w = weights(el[c.id], c.sub);
    for (int j = 0; j < t(); ++j) gain[j] = add(gain[j], w[j]);
  }
  for (int j = 0; j < t(); ++j) order[j] = j;
  std::stable_sort(order.begin(), order.end(),
                   [&](Uint64 a, Uint64 b) { return gain[a] > gain[b]; });
  // Valuations suffice to undo/reapply exactly; no enormous shifted integer is ever materialized.
  // In the reconstructed presentation v_original = v_stored + weight dot recorded_shifts.
  vector<Z> selected(t());
  for (Uint64 j : order) {
    if (!gain[j]) {
      if (shifts[j]) reject("nonzero shift at zero-gain level");
      continue;
    }
    Z bound;
    bool set = false;
    for (const auto& c : constraints) {
      auto w = weights(el[c.id], c.sub);
      if (!w[j]) continue;
      Z rem((Int64)c.v), tmp;
      for (int k = 0; k < t(); ++k) {
        fmpz_set_ui(tmp.v, shifts[k]);
        fmpz_sub(tmp.v, tmp.v, selected[k].v);
        fmpz_mul_ui(tmp.v, tmp.v, w[k]);
        fmpz_add(rem.v, rem.v, tmp.v);
      }
      fmpz_fdiv_q_ui(rem.v, rem.v, w[j]);
      if (!set || fmpz_cmp(rem.v, bound.v) < 0) {
        fmpz_set(bound.v, rem.v);
        set = true;
      }
    }
    if (fmpz_cmp_ui(bound.v, shifts[j]))
      reject("recorded rescaling shifts fail undo/reapply greedy rule");
    fmpz_set(selected[j].v, bound.v);
  }
  for (int j = 0; j < t(); ++j)
    if (gain[j]) {
      bool blocked = false;
      for (const auto& c : constraints) {
        auto w = weights(el[c.id], c.sub);
        if (w[j] && c.v < w[j]) {
          blocked = true;
          break;
        }
      }
      if (!blocked) reject("stored data have nonzero residual greedy shift");
    }
}
}  // namespace sic_check::crt::sicx
