// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/ball.hpp"

#include "sic/support/flint_raii.hpp"
#include "support/hex.hpp"

namespace sic::sicx {
size_t ball_record_bytes(unsigned limbs) { return 2 * (32 + 8 * size_t(limbs)); }
}  // namespace sic::sicx

namespace sic::sicx {
std::string fmpz_hex(const fmpz_t v) {
  sic::support::flint::String text(fmpz_get_str(nullptr, 16, v));
  std::string out(text.get());
  return out;
}
}  // namespace sic::sicx

namespace sic::sicx {
void parse_hex(fmpz_t out, const std::string& token) {
  const size_t first = !token.empty() && token[0] == '-' ? 1 : 0;
  if (first == token.size() ||
      token.find_first_not_of("0123456789abcdef", first) != std::string::npos ||
      fmpz_set_str(out, token.c_str(), 16) || fmpz_hex(out) != token)
    fail(sic::ErrorKind::invalid_format, "non-canonical hex integer in ball: " + token);
}
}  // namespace sic::sicx

namespace sic::sicx {
BallPart parse_ball_part(const std::string& text, unsigned limbs) {
  std::vector<std::string> fields;
  size_t start = 0;
  for (;;) {
    const size_t space = text.find(' ', start);
    fields.push_back(text.substr(start, space - start));
    if (space == std::string::npos) break;
    start = space + 1;
  }
  if (fields.size() != 4) fail(sic::ErrorKind::invalid_format, "ball part must have four fields");
  fmpz_t v[4];
  for (auto& x : v) fmpz_init(x);
  BallPart part;
  try {
    for (int i = 0; i < 4; ++i) parse_hex(v[i], fields[i]);
    if (!fmpz_fits_si(v[1]) || !fmpz_fits_si(v[3]))
      fail(sic::ErrorKind::invalid_format, "ball exponent out of range");
    if (fmpz_sgn(v[2]) < 0 || fmpz_bits(v[2]) > 30 || (!fmpz_is_zero(v[2]) && fmpz_is_even(v[2])))
      fail(sic::ErrorKind::invalid_format, "ball radius mantissa out of range");
    if (fmpz_bits(v[0]) > 64 * limbs)
      fail(sic::ErrorKind::invalid_format, "ball midpoint exceeds the record width");
    if (fmpz_cmp_si(v[1], kMaxBallExponent) > 0 || fmpz_cmp_si(v[1], -kMaxBallExponent) < 0 ||
        fmpz_cmp_si(v[3], kMaxBallExponent) > 0 || fmpz_cmp_si(v[3], -kMaxBallExponent) < 0)
      fail(sic::ErrorKind::invalid_format, "ball exponent out of range");
    if ((fmpz_is_zero(v[0]) && !fmpz_is_zero(v[1])) || (fmpz_is_zero(v[2]) && !fmpz_is_zero(v[3])))
      fail(sic::ErrorKind::invalid_format, "zero ball field with nonzero exponent");
    if (!fmpz_is_zero(v[0]) && fmpz_is_even(v[0]))
      fail(sic::ErrorKind::invalid_format, "ball midpoint mantissa is not odd");
    part.negative = fmpz_sgn(v[0]) < 0;
    part.mid_exp = fmpz_get_si(v[1]);
    part.rad_man = fmpz_get_ui(v[2]);
    part.rad_exp = fmpz_get_si(v[3]);
    part.mantissa.assign(limbs, 0);
    fmpz_abs(v[0], v[0]);
    fmpz_get_ui_array(reinterpret_cast<ulong*>(part.mantissa.data()), limbs, v[0]);
  } catch (...) {
    for (auto& x : v) fmpz_clear(x);
    throw;
  }
  for (auto& x : v) fmpz_clear(x);
  return part;
}
}  // namespace sic::sicx

namespace sic::sicx {
std::string hex_i64(int64_t value) {
  const uint64_t magnitude =
      value < 0 ? uint64_t(0) - static_cast<uint64_t>(value) : static_cast<uint64_t>(value);
  return (value < 0 ? "-" : "") + support::hex_unsigned(magnitude);
}
}  // namespace sic::sicx

namespace sic::sicx {
std::string ball_part_text(const BallPart& part) {
  sic::support::flint::Fmpz v;
  fmpz_set_ui_array(v, reinterpret_cast<const ulong*>(part.mantissa.data()),
                    static_cast<slong>(part.mantissa.size()));
  if (part.negative) fmpz_neg(v, v);
  std::string text = fmpz_hex(v);
  text += ' ';
  text += hex_i64(part.mid_exp);
  text += ' ';
  text += hex_i64(static_cast<int64_t>(part.rad_man));
  text += ' ';
  text += hex_i64(part.rad_exp);
  return text;
}
}  // namespace sic::sicx

namespace sic::sicx {
void put_ball_part(ByteBuffer& out, const BallPart& part) {
  put_le(out, part.negative ? 1 : 0, 8);
  put_le(out, static_cast<uint64_t>(part.mid_exp), 8);
  put_le(out, part.rad_man, 8);
  put_le(out, static_cast<uint64_t>(part.rad_exp), 8);
  for (auto limb : part.mantissa) put_le(out, limb, 8);
}
}  // namespace sic::sicx

namespace sic::sicx {
BallPart get_ball_part(const uint8_t* data, unsigned limbs) {
  BallPart part;
  const uint64_t flags = get_le(data, 8);
  if (flags > 1) fail(sic::ErrorKind::invalid_format, "invalid ball flags");
  part.negative = flags == 1;
  part.mid_exp = static_cast<int64_t>(get_le(data + 8, 8));
  part.rad_man = get_le(data + 16, 8);
  part.rad_exp = static_cast<int64_t>(get_le(data + 24, 8));
  if (part.mid_exp > kMaxBallExponent || part.mid_exp < -kMaxBallExponent ||
      part.rad_exp > kMaxBallExponent || part.rad_exp < -kMaxBallExponent)
    fail(sic::ErrorKind::invalid_format, "ball exponent out of range");
  part.mantissa.resize(limbs);
  bool zero = true;
  for (unsigned i = 0; i < limbs; ++i) {
    part.mantissa[i] = get_le(data + 32 + 8 * i, 8);
    zero = zero && !part.mantissa[i];
  }
  if ((zero && (part.negative || part.mid_exp)) || (!part.rad_man && part.rad_exp) ||
      part.rad_man >= (uint64_t(1) << 30) || (part.rad_man && !(part.rad_man & 1)) ||
      (!zero && !(part.mantissa[0] & 1)))
    fail(sic::ErrorKind::invalid_format, "non-canonical ball record");
  return part;
}
}  // namespace sic::sicx

namespace sic::sicx {
void ball_part_arb(arb_t out, const BallPart& part) {
  const std::string text = ball_part_text(part);
  if (arb_load_str(out, text.c_str()))
    fail(sic::ErrorKind::invalid_format, "ball part does not load");
}
}  // namespace sic::sicx

namespace sic::sicx {
int64_t radius_log2_bound(const BallPart& part) {
  if (!part.rad_man) return INT64_MIN / 2;
  return part.rad_exp + static_cast<int64_t>(64 - __builtin_clzll(part.rad_man));
}
}  // namespace sic::sicx
