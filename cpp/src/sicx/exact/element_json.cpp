// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/exact/element_json.hpp"

#include "sic/sicx/verify.hpp"
#include "sic/support/flint_raii.hpp"
#include "sicx/codec/block.hpp"
namespace sic::sicx::exact {
std::string fmpz_dec(const fmpz_t z) {
  sic::support::flint::String text(fmpz_get_str(nullptr, 10, z));
  std::string out(text.get());
  return out;
}

std::string fmpq_dec(const fmpq_t q) {
  sic::support::flint::String text(fmpq_get_str(nullptr, 10, q));
  std::string out(text.get());
  return out;
}

sic::support::Json element_json(const Elt& x) {
  sic::support::flint::Fmpz den;
  fmpz_set_ui(den, 1);
  for (const auto& c : x) {
    fmpz_lcm(den, den, fmpq_denref(c.a));
    fmpz_lcm(den, den, fmpq_denref(c.b));
  }
  sic::support::Json j = sic::support::Json::object_value();
  j["denominator"] = sic::support::Json::string_value(fmpz_dec(den));
  j["coefficients"] = sic::support::Json::array_value();
  sic::support::flint::Fmpz v;
  for (const auto& c : x) {
    sic::support::Json pair = sic::support::Json::array_value();
    for (const fmpq* q : {&c.a[0], &c.b[0]}) {
      fmpz_mul(v, fmpq_numref(q), den);
      fmpz_divexact(v, v, fmpq_denref(q));
      pair.array.push_back(sic::support::Json::string_value(fmpz_dec(v)));
    }
    j["coefficients"].array.push_back(pair);
  }
  return j;
}

std::string ke_infix(const QuadraticElement& x) {
  const std::string a = fmpq_dec(x.a), b = fmpq_dec(x.b);
  if (fmpq_is_zero(x.b)) return "(" + a + ")";
  if (fmpq_is_zero(x.a)) return "(" + b + "*omega)";
  return "(" + a + " + " + b + "*omega)";
}

std::string elt_infix(const Tower& T, const Elt& x, unsigned n) {
  std::string out;
  for (size_t e = 0; e < x.size(); ++e) {
    if (x[e].zero()) continue;
    std::string term = ke_infix(x[e]);
    size_t rest = e;
    for (unsigned i = 0; i < n; ++i) {
      const unsigned digit = static_cast<unsigned>(rest % T.p[i]);
      rest /= T.p[i];
      if (digit) term += "*g" + std::to_string(i) + (digit > 1 ? "^" + std::to_string(digit) : "");
    }
    out += (out.empty() ? "" : " + ") + term;
  }
  return out.empty() ? "0" : out;
}

sic::support::Json conjugation_word_json(std::span<const uint64_t> word) {
  sic::support::Json j = sic::support::Json::array_value();
  for (const uint64_t residue : word)
    j.array.push_back(sic::support::Json::string_value(std::to_string(residue)));
  return j;
}
}  // namespace sic::sicx::exact
