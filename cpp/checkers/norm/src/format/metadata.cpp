// SPDX-License-Identifier: AGPL-3.0-or-later
#include "metadata.hpp"

#include "../runtime/diagnostics.hpp"
#include "canonical_json.hpp"
#include "container.hpp"
#include "input.hpp"
#include "integer_codec.hpp"
namespace sic_check::norm {
namespace {
constexpr const char* kDecoderIdentity = "sic-decode/1.0";
constexpr const char* kEntryIdentity = "sicx-divisors/1.0";
constexpr const char* kRescalingIdentity = "sicx-2adic-greedy/1.0";
}  // namespace
void canonical_ball(arb_t out, const std::string& s) {
  resource(s.size() <= (1u << 20), "branch string bytes");
  std::array<Integer, 4> part;
  size_t first = 0;
  for (size_t i = 0; i < 4; ++i) {
    size_t last = s.find(' ', first);
    if (i == 3) {
      input_require(last == std::string::npos, "Arb extra field");
      last = s.size();
    } else
      input_require(last != std::string::npos, "Arb missing field");
    input_require(last > first, "Arb empty field");
    auto field = s.substr(first, last - first);
    size_t sign = field[0] == '-' ? 1 : 0;
    input_require(
        sign < field.size() && (field[sign] != '0' || field.size() == 1) && (i != 2 || !sign),
        "Arb sign/leading zero");
    for (size_t j = sign; j < field.size(); ++j)
      input_require((field[j] >= '0' && field[j] <= '9') || (field[j] >= 'a' && field[j] <= 'f'),
                    "Arb hexadecimal digit");
    input_require(fmpz_set_str(part[i].v, field.c_str(), 16) == 0, "Arb integer");
    first = last + 1;
  }
  input_require((fmpz_is_zero(part[0].v) ? fmpz_is_zero(part[1].v) : fmpz_is_odd(part[0].v)) &&
                    (fmpz_is_zero(part[2].v)
                         ? fmpz_is_zero(part[3].v)
                         : (fmpz_is_odd(part[2].v) && fmpz_cmp_ui(part[2].v, 1u << 30) < 0)),
                "Arb noncanonical midpoint/radius");
  resource(fmpz_bits(part[0].v) <= (1u << 20) && fmpz_fits_si(part[1].v) &&
               fmpz_fits_si(part[3].v) && fmpz_get_si(part[1].v) >= -(1 << 20) &&
               fmpz_get_si(part[1].v) <= (1 << 20) && fmpz_get_si(part[3].v) >= -(1 << 20) &&
               fmpz_get_si(part[3].v) <= (1 << 20),
           "branch mantissa/exponents");
  arf_set_fmpz_2exp(arb_midref(out), part[0].v, part[1].v);
  mag_set_fmpz_2exp_fmpz(arb_radref(out), part[2].v, part[3].v);
}

void ball_part_arb(arb_t out, const BallPart& b) {
  Integer ex;
  fmpz_set_si(ex.v, b.E);
  arf_set_fmpz_2exp(arb_midref(out), b.midpoint.v, ex.v);
  Integer rad;
  fmpz_set_ui(rad.v, b.radius);
  fmpz_set_si(ex.v, b.e);
  mag_set_fmpz_2exp_fmpz(arb_radref(out), rad.v, ex.v);
}

std::vector<uint64_t> words_add(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b,
                                const std::vector<uint64_t>& mod) {
  input_require(a.size() == mod.size() && b.size() == mod.size(), "word rank");
  auto result = a;
  for (size_t j = 0; j < mod.size(); ++j) result[j] = (a[j] + b[j]) % mod[j];
  return result;
}

int64_t base_radicand(uint64_t d) {
  if (d == 2) return -3;
  if (d == 3) return 1;  // Q-base sentinel, not the kernel of zero.
  uint64_t n = (d - 3) * (d + 1), result = 1;
  for (uint64_t p = 2; p <= n / p; ++p) {
    bool odd = false;
    while (n % p == 0) {
      n /= p;
      odd = !odd;
    }
    if (odd) result *= p;
  }
  return int64_t(result * n);
}

std::vector<uint64_t> divisor_positions(uint64_t d) {
  std::vector<uint64_t> positions{0};
  for (uint64_t g = 1; g < d; ++g)
    if (d % g == 0) positions.push_back(g);
  return positions;
}

uint64_t power_mod(uint64_t a, uint64_t n, uint64_t modulus) {
  uint64_t result = 1;
  for (; n; n >>= 1, a = a * a % modulus)
    if (n & 1) result = result * a % modulus;
  return result;
}

std::vector<UnitFactor> unit_factors(uint64_t d) {
  std::vector<UnitFactor> result;
  uint64_t remaining = d;
  for (uint64_t p = 2; p <= remaining; ++p) {
    if (remaining % p) continue;
    uint64_t modulus = 1;
    do {
      remaining /= p;
      modulus *= p;
    } while (remaining % p == 0);
    auto add = [&](uint64_t local, uint64_t order) -> UnitFactor& {
      uint64_t cofactor = d / modulus, lift = 1;
      while (lift % modulus != local) lift += cofactor;
      result.push_back({lift, order, modulus, std::vector<uint32_t>(modulus, UINT32_MAX)});
      return result.back();
    };
    if (p == 2) {
      if (modulus == 2) continue;
      add(modulus - 1, 2);
      if (modulus == 4) {
        result.back().logarithm[1] = 0;
        result.back().logarithm[3] = 1;
      } else {
        add(5, modulus / 4);
        auto& sign = result[result.size() - 2];
        auto& five = result.back();
        uint64_t u = 1;
        for (uint32_t a = 0; a < modulus / 4; ++a, u = u * 5 % modulus) {
          sign.logarithm[u] = 0;
          sign.logarithm[modulus - u] = 1;
          five.logarithm[u] = five.logarithm[modulus - u] = a;
        }
      }
    } else {
      uint64_t order = modulus / p * (p - 1), qn = order;
      std::vector<uint64_t> primes;
      for (uint64_t q = 2; q <= qn / q; ++q)
        if (qn % q == 0) {
          primes.push_back(q);
          do {
            qn /= q;
          } while (qn % q == 0);
        }
      if (qn > 1) primes.push_back(qn);
      uint64_t local = 1;
      for (;; ++local)
        if (std::gcd(local, modulus) == 1 && std::all_of(primes.begin(), primes.end(), [&](auto q) {
              return power_mod(local, order / q, modulus) != 1;
            }))
          break;
      auto& factor = add(local, order);
      uint64_t u = 1;
      for (uint32_t a = 0; a < order; ++a, u = u * local % modulus) factor.logarithm[u] = a;
    }
  }
  return result;
}

auto Meta::entry(uint64_t m) const -> Entry {
  input_require(m < d, "entry coordinate range");
  Entry v{0, std::vector<uint64_t>(smith.size(), 0)};
  uint64_t g = std::gcd(m, d), n = d / g;
  v.representative = g == d ? 0 : int64_t(g);
  uint64_t lift = n == 1 ? 1 : m / g;
  while (std::gcd(lift, d) != 1) lift += n;
  for (size_t j = 0; j < units.size(); ++j) {
    uint64_t a = units[j].logarithm[lift % units[j].modulus];
    for (size_t l = 0; l < smith.size(); ++l)
      v.word[l] = (v.word[l] + a * unit_words[j][l]) % smith[l];
  }
  return v;
}

auto Reader::word(const Json& j, const std::vector<uint64_t>& mod) -> std::vector<uint64_t> {
  input_require(j.items().size() == mod.size(), "word length");
  std::vector<uint64_t> v;
  for (size_t l = 0; l < mod.size(); ++l) v.push_back(number(j.list[l], 0, int64_t(mod[l] - 1)));
  return v;
}

auto Reader::metadata(const Json& j) -> void {
  j.keys({"d", "elements", "entry_rule", "format", "galois", "generator_rescaling",
          "required_features", "tower", "version"});
  input_require(j.at("format").str() == "SICX" && j.at("required_features").items().empty(),
                "format/features");
  auto ver = j.at("version").items();
  input_require(ver.size() == 2 && number(ver[0]) == 1 && number(ver[1]) == 0, "version");
  m.d = number(j.at("d"), 2, 1 << 20);
  m.D0 = base_radicand(m.d);
  const auto& tw = j.at("tower");
  tw.keys({"branch_bits", "branches", "degrees"});
  m.branch_bits = number(tw.at("branch_bits"), 128);
  input_require(std::has_single_bit(m.branch_bits), "branch_bits sequence");
  resource(m.branch_bits <= (1u << 20), "branch_bits");
  const auto& scaling = j.at("generator_rescaling");
  scaling.keys({"rule", "shifts"});
  input_require(scaling.at("rule").str() == kRescalingIdentity, "rescaling rule");
  for (const auto& x : scaling.at("shifts").items()) m.shifts.push_back(number(x));

  for (const auto& x : tw.at("degrees").items()) {
    unsigned p = unsigned(number(x, 2, 31));
    for (unsigned f = 2; f * f <= p; ++f) input_require(p % f != 0, "nonprime tower degree");
    input_require(m.D <= (uint64_t(1) << 40) / p, "tower dimension overflow");
    m.D *= p;
    m.degrees.push_back(p);
  }
  input_require(!m.degrees.empty() && m.degrees.size() <= 62, "tower length");
  // The norm method constructs dense power bases; refuse before any such allocation.
  resource(m.D <= options.max_field_dimension && m.d <= options.max_dimension,
           "dense checker dimension");
  input_require(m.shifts.size() == m.degrees.size(), "rescaling shift count");
  for (const auto& b : tw.at("branches").items()) {
    input_require(b.items().size() == 2, "branch pair");
    ComplexBall z;
    canonical_ball(acb_realref(z.v), b.list[0].str());
    canonical_ball(acb_imagref(z.v), b.list[1].str());
    m.branches.push_back(z);
  }
  input_require(m.branches.size() == m.degrees.size(), "branch count");
  auto& gal = j.at("galois");
  gal.keys({"smith", "unit_generator_words"});
  uint64_t product = 1;
  for (const auto& x : gal.at("smith").items()) {
    uint64_t n = number(x, 2, int64_t(m.D));
    input_require(m.D % n == 0 && product <= m.D / n, "Smith factor/product");
    product *= n;
    m.smith.push_back(n);
  }
  input_require(!m.smith.empty() && product == m.D, "Smith product");
  m.units = unit_factors(m.d);
  const auto& unit_words = gal.at("unit_generator_words").items();
  input_require(unit_words.size() == m.units.size(), "unit generator word count");
  for (size_t u = 0; u < unit_words.size(); ++u) {
    auto w = word(unit_words[u], m.smith);
    for (size_t l = 0; l < w.size(); ++l)
      input_require((m.units[u].order * w[l]) % m.smith[l] == 0, "unit generator order relation");
    m.unit_words.push_back(std::move(w));
  }
  const auto& entry = j.at("entry_rule");
  entry.keys({"name", "representatives"});
  input_require(entry.at("name").str() == kEntryIdentity, "entry rule");
  auto reps = divisor_positions(m.d);
  input_require(entry.at("representatives").items().size() == reps.size(),
                "entry representative count");
  for (size_t i = 0; i < reps.size(); ++i)
    input_require(uint64_t(number(entry.at("representatives").list[i])) == reps[i],
                  "entry representatives");
  for (const auto& x : j.at("elements").items()) {
    Element e;
    e.kind = x.at("kind").str();
    if (e.kind == "rep") {
      x.keys({"kind", "levels", "max_bits", "position", "scale"});
      e.level = unsigned(m.degrees.size());
      e.position = number(x.at("position"), 0, int64_t(m.d - 1));
    } else {
      e.level = unsigned(number(x.at("level"), 0, int64_t(m.degrees.size() - 1)));
      if (e.kind == "wsq" || e.kind == "actdelta")
        x.keys({"kind", "level", "levels", "max_bits", "scale"});
      else if (e.kind == "act")
        x.keys({"axis", "kind", "level", "levels", "max_bits", "scale"});
      else if (e.kind == "fcoef")
        x.keys({"k", "kind", "level", "levels", "max_bits", "scale"});
      else {
        input_require(e.kind == "actodd", "element kind");
        x.keys({"axis", "k", "kind", "level", "levels", "max_bits", "scale"});
      }
      if (x.fields.contains("axis")) e.axis = number(x.at("axis"), 0, int64_t(m.smith.size() - 1));
      if (x.fields.contains("k")) e.k = number(x.at("k"), 0, m.degrees[e.level] - 1);
    }
    for (const auto& lev : x.at("levels").items()) {
      unsigned l = unsigned(number(lev, 0, int64_t(e.level) - 1));
      input_require(e.level > 0 && (e.levels.empty() || e.levels.back() < l), "level set order");
      e.levels.push_back(l);
    }
    e.max_bits = number(x.at("max_bits"), 0, 1 << 23);
    e.scale = x.at("scale").str();
    input_require(
        !e.scale.empty() && e.scale.size() <= 4095 && e.scale[0] >= '1' && e.scale[0] <= '9',
        "scale");
    for (char c : e.scale) input_require(c >= '0' && c <= '9', "scale digit");
    m.elements.push_back(std::move(e));
  }
  size_t id = 0;
  auto consume = [&](const std::string& kind, unsigned level, int64_t axis = -1, int64_t k = -1,
                     int64_t rep = -1) {
    input_require(id < m.elements.size(), "missing element");
    const auto& e = m.elements[id];
    input_require(
        e.kind == kind && e.level == level && e.axis == axis && e.k == k && e.position == rep,
        "element order/set");
    if (rep >= 0) m.representative.emplace(rep, id);
    ++id;
  };
  for (unsigned l = 0; l < m.degrees.size(); ++l) {
    if (m.degrees[l] == 2)
      consume("wsq", l);
    else
      for (unsigned k = 0; k < m.degrees[l]; ++k) consume("fcoef", l, -1, k);
    bool delta = id < m.elements.size() && m.elements[id].kind == "actdelta";
    if (delta) {
      input_require(m.degrees[l] != 2, "quadratic delta");
      consume("actdelta", l);
    }
    bool odd = false;
    for (size_t a = 0; a < m.smith.size(); ++a) {
      if (id == m.elements.size() || m.elements[id].level != l || m.elements[id].axis != int64_t(a))
        continue;
      if (m.degrees[l] == 2)
        consume("act", l, int64_t(a));
      else {
        odd = true;
        for (unsigned k = 0; k < m.degrees[l]; ++k) consume("actodd", l, int64_t(a), k);
      }
    }
    input_require(delta == odd, "odd action denominator/set");
  }
  for (uint64_t r : reps) consume("rep", unsigned(m.degrees.size()), -1, -1, int64_t(r));
  input_require(id == m.elements.size(), "extra element");
}

auto Reader::raydata(const Json& j) -> void {
  if (j.fields.contains("decoder")) {
    j.keys({"balls", "decoder", "rays"});
    const auto& d = j.at("decoder");
    d.keys({"name", "source_commit"});
    input_require(
        d.at("name").str() == kDecoderIdentity &&
            (d.at("source_commit").str().size() == 40 || d.at("source_commit").str().size() == 64),
        "decoder record");
    for (char c : d.at("source_commit").str())
      input_require((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'), "decoder digest");
  } else
    j.keys({"balls", "rays"});
  j.at("balls").keys({"limbs", "working_bits"});
  m.limbs = number(j.at("balls").at("limbs"), 1, 1024);
  m.working_bits = number(j.at("balls").at("working_bits"), 1, 1 << 20);
  int64_t previous_label = -1;
  for (const auto& x : j.at("rays").items()) {
    x.keys({"accuracy_bits", "anchor", "label", "word"});
    Ray r;
    r.label = x.at("label").str();
    input_require(r.label.size() >= 2 && r.label[0] == 'c', "ray label spelling");
    Json suffix;
    suffix.text = r.label.substr(1);
    int64_t label = number(suffix);
    input_require(label > previous_label, "ray label order/duplicate");
    previous_label = label;

    r.word = word(x.at("word"), m.smith);
    input_require(m.rays.empty() || m.rays.back().word < r.word, "ray word order/duplicate");
    r.anchor = number(x.at("anchor"), 0, int64_t(m.d - 1));
    r.accuracy = number(x.at("accuracy_bits"), -(1 << 20), 1 << 20);
    m.rays.push_back(std::move(r));
  }
  input_require(!m.rays.empty(), "empty ray list");
}
}  // namespace sic_check::norm
