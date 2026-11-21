// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ball_codec.hpp"

#include "../runtime/diagnostics.hpp"
#include "canonical_json.hpp"
#include "container.hpp"
#include "integer_codec.hpp"
#include "metadata.hpp"
#include "sha256.hpp"
namespace sic_check::verifier {
bool hex_nat(const string& t, fmpz_t out, bool allow_neg) {
  size_t i = 0;
  bool neg = false;
  if (t.empty()) return false;
  if (t[0] == '-') {
    if (!allow_neg) return false;
    neg = true;
    i = 1;
  }
  if (i >= t.size()) return false;
  if (t.size() - i > 1 && t[i] == '0') return false;  // leading zero
  if (t[i] == '0' && neg) return false;               // -0
  for (size_t j = i; j < t.size(); j++) {
    char c = t[j];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  string body = t.substr(i);
  fmpz_set_str(out, body.c_str(), 16);
  if (neg) fmpz_neg(out, out);
  return true;
}

void parse_arbdump(const string& s, fmpz_t M, fmpz_t E, fmpz_t m, fmpz_t e) {
  if (s.size() > (1u << 20)) throw PrecErr("Arb string exceeds 1 MiB reader limit");
  vector<string> f;
  size_t p = 0;
  while (true) {
    size_t q = s.find(' ', p);
    if (q == string::npos) {
      f.push_back(s.substr(p));
      break;
    }
    f.push_back(s.substr(p, q - p));
    p = q + 1;
  }
  if (f.size() != 4)
    reject("Arb dump: not exactly four space-separated fields: " + s.substr(0, 60));
  if (!hex_nat(f[0], M, true) || !hex_nat(f[1], E, true) || !hex_nat(f[2], m, false) ||
      !hex_nat(f[3], e, true))
    reject("Arb dump: field not canonical lowercase hex: " + s.substr(0, 60));
  if (fmpz_cmp_si(E, 1u << 20) > 0 || fmpz_cmp_si(E, -(1l << 20)) < 0 ||
      fmpz_cmp_si(e, 1u << 20) > 0 || fmpz_cmp_si(e, -(1l << 20)) < 0)
    throw PrecErr("branch exponent exceeds reader resource limit 2^20");
  if (!fmpz_is_zero(M) && !fmpz_is_odd(M)) reject("Arb dump: M even");
  if (fmpz_is_zero(M) && !fmpz_is_zero(E)) reject("Arb dump: M = 0 with E != 0 (non-finite)");
  if (!fmpz_is_zero(m) && (!fmpz_is_odd(m) || fmpz_bits(m) > 30))
    reject("Arb dump: m not odd or >= 2^30");
  if (fmpz_is_zero(m) && !fmpz_is_zero(e)) reject("Arb dump: m = 0 with e != 0");
}

BallPart::BallPart() { fmpz_init(M); }

BallPart::~BallPart() { fmpz_clear(M); }

void parse_ball_part(const Uint8* p, Uint64 limbs, BallPart& o) {
  Uint64 fl = le64(p);
  Int64 E = (Int64)le64(p + 8);
  Uint64 m = le64(p + 16);
  Int64 e = (Int64)le64(p + 24);
  if (fl & ~1ull) reject("BALL: flag bits other than bit 0 set");
  if (E > (1ll << 40) || E < -(1ll << 40) || e > (1ll << 40) || e < -(1ll << 40))
    reject("BALL: exponent out of range");
  vector<ulong> L(limbs);
  for (Uint64 i = 0; i < limbs; i++) L[i] = le64(p + 32 + 8 * i);
  fmpz_set_ui_array(o.M, L.data(), (slong)limbs);
  if (!fmpz_is_zero(o.M)) {
    if (!fmpz_is_odd(o.M))
      reject("BALL: midpoint mantissa M is even (non-canonical; limb order assumed little-endian)");
  } else {
    if (E != 0) reject("BALL: zero M with nonzero exponent");
    if (fl & 1) reject("BALL: negative flag on zero midpoint (negative zero)");
  }
  if (m == 0 && e != 0) reject("BALL: zero m with nonzero exponent");
  if (m >= (1ull << 63)) reject("BALL: m >= 2^63");
  if (m != 0 && ((m & 1) == 0 || m >= (1ull << 30)))
    reject(
        "BALL: radius mantissa m must be odd and < 2^30 (Arb dump canonical form, spec 5.2 (g))");
  o.neg = fl & 1;
  o.E = E;
  o.m = m;
  o.e = e;
  if (o.neg) fmpz_neg(o.M, o.M);
}

void ball_to_arb(arb_t x, const BallPart& b) {
  fmpz_t E;
  fmpz_init_set_si(E, b.E);
  arb_set_fmpz_2exp(x, b.M, E);
  mag_t r;
  mag_init(r);
  mag_set_ui_2exp_si(r, b.m, b.e);
  mag_set(arb_radref(x), r);
  mag_clear(r);
  fmpz_clear(E);
}

void admit_ball_memory(Uint64 rays, Uint64 coordinates, Uint64 limbs) {
  const Uint64 per_component = checked_add(64, checked_mul(16, limbs));
  const Uint64 per_coordinate = checked_add(sizeof(ACB), checked_mul(2, per_component));
  const Uint64 per_ray = checked_add(128, checked_mul(coordinates, per_coordinate));
  if (checked_mul(rays, per_ray) > (1ull << 30))
    throw PrecErr("retained BALL admission including mantissas exceeds 1 GiB reader limit");
}
}  // namespace sic_check::verifier
