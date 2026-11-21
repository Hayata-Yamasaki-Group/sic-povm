// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/sicx/json.hpp"

#include "sic/support/flint_raii.hpp"
#include "sic/support/parse_number.hpp"
#include "sicx/codec/ball.hpp"
#include "sicx/codec/block.hpp"
#include "sicx/container/verify.hpp"
#include "sicx/container/verify.tpp"
#include "support/hex.hpp"
#include "support/sha256.hpp"

namespace sic::sicx {
double certified_round(const arb_t x, const std::string& what) {
  arf_t lo, hi;
  arf_init(lo);
  arf_init(hi);
  arb_get_lbound_arf(lo, x, ARF_PREC_EXACT);
  arb_get_ubound_arf(hi, x, ARF_PREC_EXACT);
  const double a = arf_get_d(lo, ARF_RND_NEAR), b = arf_get_d(hi, ARF_RND_NEAR);
  arf_clear(lo);
  arf_clear(hi);
  if (!std::isfinite(a) || a != b)
    fail(sic::ErrorKind::precision, "ball is too wide to certify the binary64 rounding of " + what);
  return a == 0 ? 0.0 : a;  // canonical positive zero
}
}  // namespace sic::sicx

namespace sic::sicx {
std::string double_hex(double value) {
  uint64_t bits;
  std::memcpy(&bits, &value, 8);
  return support::hex_unsigned(bits, 16);
}
}  // namespace sic::sicx

namespace sic::sicx {
std::string double_decimal17(double value) {
  char buffer[64];
  std::snprintf(buffer, sizeof buffer, "%.16e", value);
  if (support::parse_number<double>(buffer, "binary64 round trip", ErrorKind::invalid_format) !=
      value)
    throw std::logic_error(std::string("SICX: ") + "binary64 decimal round trip failed");
  return buffer;
}
}  // namespace sic::sicx

namespace sic::sicx {
std::string fixed_decimal(const fmpz_t n, slong places) {
  sic::support::flint::Fmpz a;
  fmpz_abs(a, n);
  sic::support::flint::String raw(fmpz_get_str(nullptr, 10, a));
  std::string digits(raw.get());
  const bool negative = fmpz_sgn(n) < 0;
  if (static_cast<slong>(digits.size()) <= places)
    digits.insert(0, static_cast<size_t>(places + 1 - static_cast<slong>(digits.size())), '0');
  std::string text = (negative ? "-" : "") + digits.substr(0, digits.size() - places) + "." +
                     digits.substr(digits.size() - places);
  return text;
}
}  // namespace sic::sicx

namespace sic::sicx {
DecimalBall decimal_ball(const arb_t x, slong places, arb_t radius_out) {
  sic::support::flint::Fmpz scale;
  sic::support::flint::Fmpz num;
  sic::support::flint::Fmpz den;
  sic::support::flint::Fmpz q;
  sic::support::flint::Fmpz r;
  fmpz_ui_pow_ui(scale, 10, static_cast<ulong>(places));
  // mid = M 2^E exactly.
  sic::support::flint::Fmpz M;
  sic::support::flint::Fmpz E;
  arf_get_fmpz_2exp(M, E, arb_midref(x));
  const slong e = fmpz_get_si(E);
  fmpz_mul(num, M, scale);
  if (e >= 0) {
    fmpz_mul_2exp(num, num, static_cast<ulong>(e));
    fmpz_set(q, num);
  } else {
    fmpz_one(den);
    fmpz_mul_2exp(den, den, static_cast<ulong>(-e));
    fmpz_fdiv_qr(q, r, num, den);  // q = floor(num/den)
    fmpz_mul_2exp(r, r, 1);
    if (fmpz_cmp(r, den) >= 0) fmpz_add_ui(q, q, 1);  // nearest (ties up); error <= 1/2 unit
  }
  const std::string mid = fixed_decimal(q, places);
  // rad = m 2^f exactly.
  sic::support::flint::Arf rad;
  arf_set_mag(rad, arb_radref(x));
  arf_get_fmpz_2exp(M, E, rad);
  const slong f = fmpz_get_si(E);
  fmpz_mul(num, M, scale);
  if (f >= 0)
    fmpz_mul_2exp(q, num, static_cast<ulong>(f));
  else {
    fmpz_one(den);
    fmpz_mul_2exp(den, den, static_cast<ulong>(-f));
    fmpz_cdiv_q(q, num, den);  // ceil
  }
  fmpz_add_ui(q, q, 1);
  const std::string radius = fixed_decimal(q, places);
  arb_set_fmpz(radius_out, q);
  arb_div_fmpz(radius_out, radius_out, scale, 64);
  return {mid, radius};
}
}  // namespace sic::sicx

namespace sic::sicx {
float certified_round32(const arb_t x, const std::string& what) {
  arf_t lo, hi, a, b;
  arf_init(lo);
  arf_init(hi);
  arf_init(a);
  arf_init(b);
  arb_get_lbound_arf(lo, x, ARF_PREC_EXACT);
  arb_get_ubound_arf(hi, x, ARF_PREC_EXACT);
  arf_set_round(a, lo, 24, ARF_RND_NEAR);
  arf_set_round(b, hi, 24, ARF_RND_NEAR);
  const bool same = arf_equal(a, b);
  const double v = arf_get_d(a, ARF_RND_DOWN);  // exact: 24 significant bits
  arf_clear(lo);
  arf_clear(hi);
  arf_clear(a);
  arf_clear(b);
  if (!same)
    fail(sic::ErrorKind::precision, "ball is too wide to certify the binary32 rounding of " + what);
  if (v != 0 && (std::fabs(v) < 1.17549435e-38 || std::fabs(v) > 3.40282346e38))
    fail(sic::ErrorKind::invalid_format, "binary32 value outside the normal range for " + what);
  return v == 0 ? 0.0f : static_cast<float>(v);
}
}  // namespace sic::sicx

namespace sic::sicx {
std::string float_hex(float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, 4);
  return support::hex_unsigned(bits, 8);
}
}  // namespace sic::sicx

namespace sic::sicx {
std::string float_decimal9(float value) {
  char buffer[64];
  std::snprintf(buffer, sizeof buffer, "%.8e", static_cast<double>(value));
  if (support::parse_number<float>(buffer, "binary32 round trip", ErrorKind::invalid_format) !=
      value)
    throw std::logic_error(std::string("SICX: ") + "binary32 decimal round trip failed");
  return buffer;
}
}  // namespace sic::sicx

namespace sic::sicx {
std::string error_bound_text(double max_error) {
  if (max_error <= 0) return "0x0p+0";
  int exponent = 0;
  std::frexp(max_error, &exponent);  // max_error < 2^exponent
  return "0x1p" + std::string(exponent >= 0 ? "+" : "") + std::to_string(exponent);
}
}  // namespace sic::sicx

namespace sic::sicx {
void ray_levels(const Reader& reader, size_t ri, sic::support::Json& x) {
  const Meta& meta = reader.meta();
  const auto parts = reader.ball_parts(ri);
  sic::support::flint::Arb z[2], worst, t, dec_rad;
  sic::support::flint::Arf wr;
  int64_t worst_bound = INT64_MIN / 2;
  for (const auto& part : parts) worst_bound = std::max(worst_bound, radius_log2_bound(part));
  const int64_t acc = std::clamp<int64_t>(-worst_bound, 1, 100000);
  const slong places =
      static_cast<slong>(std::ceil(static_cast<double>(acc) * 0.30102999566398120)) + 3;
  const slong prec = 64 * static_cast<slong>(meta.ball_limbs) + 256;
  sic::support::Json v64 = sic::support::Json::array_value(),
                     d64 = sic::support::Json::array_value(),
                     v32 = sic::support::Json::array_value(),
                     d32 = sic::support::Json::array_value(),
                     hp_values = sic::support::Json::array_value();
  double e64 = 0, e32 = 0;
  for (uint64_t m = 0; m < meta.d; ++m) {
    ball_part_arb(z[0], parts[2 * m]);
    ball_part_arb(z[1], parts[2 * m + 1]);
    sic::support::Json p64 = sic::support::Json::array_value(),
                       q64 = sic::support::Json::array_value(),
                       p32 = sic::support::Json::array_value(),
                       q32 = sic::support::Json::array_value(),
                       ball = sic::support::Json::array_value();
    for (int c = 0; c < 2; ++c) {
      const std::string what = std::string(c ? "imaginary" : "real") + " part " + std::to_string(m);
      const double a = certified_round(z[c], what);
      const float f = certified_round32(z[c], what);
      p64.array.push_back(sic::support::Json::string_value(double_hex(a)));
      q64.array.push_back(sic::support::Json::string_value(double_decimal17(a)));
      p32.array.push_back(sic::support::Json::string_value(float_hex(f)));
      q32.array.push_back(sic::support::Json::string_value(float_decimal9(f)));
      for (int level = 0; level < 2; ++level) {
        arb_set_d(t, level ? static_cast<double>(f) : a);
        arb_sub(t, t, z[c], prec);
        arb_abs(t, t);
        arb_get_ubound_arf(wr, t, 64);
        (level ? e32 : e64) = std::max(level ? e32 : e64, arf_get_d(wr, ARF_RND_UP));
      }
      const auto [mid, radius] = decimal_ball(z[c], places, dec_rad);
      ball.array.push_back(sic::support::Json::string_value(mid));
      ball.array.push_back(sic::support::Json::string_value(radius));
      arb_max(worst, worst, dec_rad, 64);
    }
    v64.array.push_back(p64);
    d64.array.push_back(q64);
    v32.array.push_back(p32);
    d32.array.push_back(q32);
    hp_values.array.push_back(ball);
  }
  const std::string rounding = "certified round-to-nearest-even of the exact normalized coordinate";
  sic::support::Json fp64 = sic::support::Json::object_value();
  fp64["values"] = v64;
  fp64["decimal"] = d64;
  fp64["error_bound"] = sic::support::Json::string_value(error_bound_text(e64));
  fp64["rounding"] = sic::support::Json::string_value(rounding);
  sic::support::Json fp32 = sic::support::Json::object_value();
  fp32["values"] = v32;
  fp32["decimal"] = d32;
  fp32["error_bound"] = sic::support::Json::string_value(error_bound_text(e32));
  fp32["rounding"] = sic::support::Json::string_value(rounding);
  arf_t up;
  arf_init(up);
  arb_get_ubound_arf(up, worst, 64);
  const int64_t bits = -static_cast<int64_t>(arf_abs_bound_lt_2exp_si(up));
  arf_clear(up);
  sic::support::Json hp = sic::support::Json::object_value();
  hp["accuracy_bits"] = sic::support::Json::string_value(std::to_string(bits));
  hp["decimal_places"] = sic::support::Json::string_value(std::to_string(places));
  hp["values"] = hp_values;
  x["fp32"] = fp32;
  x["fp64"] = fp64;
  x["hp"] = hp;
}
}  // namespace sic::sicx

namespace sic::sicx {
std::string ray_class(const std::string& label) {
  if (label.size() < 2 || label.size() > 19 || label[0] != 'c' ||
      (label.size() > 2 && label[1] == '0') ||
      label.find_first_not_of("0123456789", 1) != std::string::npos)
    fail(sic::ErrorKind::invalid_argument, "invalid ray class label");
  return label;
}
}  // namespace sic::sicx

namespace sic::sicx {
void require_multiplet(const std::string& label) {
  if (label.size() < 2 || label.size() > 19 || label[0] != 'm' || label[1] == '0' ||
      label.find_first_not_of("0123456789", 1) != std::string::npos)
    fail(sic::ErrorKind::invalid_argument, "multiplet must be m<N> with N positive");
}
}  // namespace sic::sicx

namespace sic::sicx {
std::string ray_full_id(const Meta& meta, size_t ri, const std::string& multiplet) {
  require_multiplet(multiplet);
  return "d" + std::to_string(meta.d) + "-" + multiplet + "-" + ray_class(meta.rays.at(ri).label);
}
}  // namespace sic::sicx

namespace sic::sicx {
std::string ray_file_name(const Meta& meta, size_t ri) {
  return ray_class(meta.rays.at(ri).label) + ".json";
}
}  // namespace sic::sicx

namespace sic::sicx {
sic::support::Json ray_json(const Reader& reader, size_t ri, const std::string& sicx_name,
                            const std::string& sicx_sha256, bool exact_present,
                            const std::string& multiplet) {
  const Meta& meta = reader.meta();
  const auto& ray = meta.rays.at(ri);
  sic::support::Json j = sic::support::Json::object_value();
  j["schema"] = sic::support::Json::string_value("sic-fiducial-json/1.0");
  j["required_features"] = sic::support::Json::array_value();
  j["d"] = sic::support::Json::string_value(std::to_string(meta.d));
  if (!multiplet.empty()) j["multiplet"] = sic::support::Json::string_value(multiplet);
  j["id"] = sic::support::Json::string_value(ray_full_id(meta, ri, multiplet));
  j["index"] = sic::support::Json::string_value(std::to_string(ri));
  sic::support::Json element = sic::support::Json::object_value();
  element["smith"] = sic::support::Json::array_value();
  element["exponents"] = sic::support::Json::array_value();
  for (size_t l = 0; l < meta.smith.size(); ++l) {
    element["smith"].array.push_back(
        sic::support::Json::string_value(std::to_string(meta.smith[l])));
    element["exponents"].array.push_back(
        sic::support::Json::string_value(std::to_string(ray.word[l])));
  }
  j["galois_element"] = element;
  sic::support::Json labels = sic::support::Json::object_value();
  for (const auto& [key, value] : meta.catalogue.object)
    if (key != "conductor" && key != "order_index" && key != "multiplet")
      labels[key] = value;  // no conductor in public data
  labels["anchor"] = sic::support::Json::string_value(std::to_string(ray.anchor));
  labels["sicx_ray_label"] = sic::support::Json::string_value(ray.label);
  j["labels"] = labels;
  j["sicx"] = sic::support::Json::object_value();
  j["sicx"]["name"] = sic::support::Json::string_value(sicx_name);
  j["sicx"]["sha256"] = sic::support::Json::string_value(sicx_sha256);
  j["sicx"]["merkle_root"] = sic::support::Json::string_value(hex(reader.root()));
  if (exact_present) {
    j["exact"] = sic::support::Json::object_value();
    j["exact"]["file"] = sic::support::Json::string_value("exact.json");
    j["exact"]["ray"] = sic::support::Json::string_value(ray_full_id(meta, ri, multiplet));
  }
  ray_levels(reader, ri, j);
  return j;
}
}  // namespace sic::sicx

namespace sic::sicx {
sic::support::Json all_in_one_json(const Reader& reader, const std::string& sicx_name,
                                   const std::string& sicx_sha256, const std::string& multiplet) {
  sic::support::Json j = sic::support::Json::object_value();
  j["schema"] = sic::support::Json::string_value("sic-fiducial-json-all/1.0");
  j["rays"] = sic::support::Json::array_value();
  for (size_t ri = 0; ri < reader.meta().rays.size(); ++ri)
    j["rays"].array.push_back(ray_json(reader, ri, sicx_name, sicx_sha256, false, multiplet));
  return j;
}
}  // namespace sic::sicx
