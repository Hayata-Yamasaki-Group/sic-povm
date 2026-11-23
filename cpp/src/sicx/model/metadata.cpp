// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/model/metadata.hpp"

#include <tuple>

#include "sic/support/parse_number.hpp"
#include "sicx/model/unit_group.hpp"

namespace sic::sicx {
std::string ray_label(std::size_t index) { return "c" + std::to_string(index); }

std::vector<uint64_t> words_add(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b,
                                const std::vector<uint64_t>& smith) {
  std::vector<uint64_t> out(smith.size());
  for (size_t l = 0; l < smith.size(); ++l) out[l] = (a[l] + b[l]) % smith[l];
  return out;
}
}  // namespace sic::sicx

namespace sic::sicx {
void derive_blocks(ElementInfo& e, uint64_t block_coordinates) {
  size_t k = 0;
  while (k < e.layout.levels.size() && e.layout.sub_width[k + 1] <= block_coordinates) ++k;
  e.block = e.layout.sub_width[k];
  e.block_level = k ? e.layout.levels[k - 1] + 1 : 0;
}
}  // namespace sic::sicx

namespace sic::sicx {
bool decimal_positive(const std::string& text) {
  return !text.empty() && text.size() < 4096 && text[0] >= '1' && text[0] <= '9' &&
         text.find_first_not_of("0123456789") == std::string::npos;
}
}  // namespace sic::sicx

namespace sic::sicx {
int kind_rank(const std::string& kind) {
  static const std::map<std::string, int> ranks{{"wsq", 0}, {"fcoef", 1},  {"actdelta", 2},
                                                {"act", 3}, {"actodd", 3}, {"rep", 4}};
  const auto found = ranks.find(kind);
  if (found == ranks.end()) fail(sic::ErrorKind::invalid_format, "unknown element kind " + kind);
  return found->second;
}
}  // namespace sic::sicx

namespace sic::sicx {
ElementKey element_key(const ElementInfo& e) {
  const int rank = kind_rank(e.kind);
  if (rank == 4) return {4, 0, 0, 0, e.position};
  return {rank < 3 ? rank : 3, e.level, rank == 3 ? e.axis : -1, e.k, 0};
}
}  // namespace sic::sicx

namespace sic::sicx {
bool element_less(const ElementInfo& a, const ElementInfo& b) {
  const auto ka = element_key(a), kb = element_key(b);
  if ((ka.kind_rank == 4) != (kb.kind_rank == 4)) return kb.kind_rank == 4;
  if (ka.kind_rank == 4) return ka.position < kb.position;
  if (ka.level != kb.level) return ka.level < kb.level;
  return std::tie(ka.kind_rank, ka.axis, ka.k) < std::tie(kb.kind_rank, kb.axis, kb.k);
}
}  // namespace sic::sicx

namespace sic::sicx {
sic::support::Json meta_to_json(const Meta& m) {
  sic::support::Json j = sic::support::Json::object_value();
  j["format"] = sic::support::Json::string_value("SICX");
  j["version"] = sic::support::Json::integer_array(std::vector<uint64_t>{kMajor, m.minor});
  j["required_features"] = sic::support::Json::array_value();
  for (const auto& f : m.required_features)
    j["required_features"].array.push_back(sic::support::Json::string_value(f));
  j["d"] = sic::support::Json::integer_value(static_cast<int64_t>(m.d));
  j["tower"] = sic::support::Json::object_value();
  j["tower"]["degrees"] = sic::support::Json::integer_array(m.degrees);
  sic::support::Json branches = sic::support::Json::array_value();
  for (const auto& b : m.branches) {
    sic::support::Json pair = sic::support::Json::array_value();
    pair.array.push_back(sic::support::Json::string_value(b[0]));
    pair.array.push_back(sic::support::Json::string_value(b[1]));
    branches.array.push_back(pair);
  }
  j["tower"]["branches"] = branches;
  j["tower"]["branch_bits"] = sic::support::Json::integer_value(m.branch_bits);
  j["generator_rescaling"] = sic::support::Json::object_value();
  j["generator_rescaling"]["rule"] = sic::support::Json::string_value(kRescalingRule);
  j["generator_rescaling"]["shifts"] = sic::support::Json::integer_array(m.rescaling_shifts);
  j["entry_rule"] = sic::support::Json::object_value();
  j["entry_rule"]["name"] = sic::support::Json::string_value(kEntryRule);
  const auto representatives = divisor_positions(m.d);
  j["entry_rule"]["representatives"] = sic::support::Json::integer_array(representatives);
  j["galois"] = sic::support::Json::object_value();
  j["galois"]["smith"] = sic::support::Json::integer_array(m.smith);
  j["galois"]["unit_generator_words"] = sic::support::Json::array_value();
  for (const auto& w : m.unit_generator_words)
    j["galois"]["unit_generator_words"].array.push_back(sic::support::Json::integer_array(w));
  sic::support::Json elements = sic::support::Json::array_value();
  for (const auto& e : m.elements) {
    sic::support::Json x = sic::support::Json::object_value();
    x["kind"] = sic::support::Json::string_value(e.kind);
    x["levels"] = sic::support::Json::integer_array(e.levels);
    x["max_bits"] = sic::support::Json::integer_value(static_cast<int64_t>(e.max_bits));
    x["scale"] = sic::support::Json::string_value(e.scale);
    if (e.kind == "rep")
      x["position"] = sic::support::Json::integer_value(e.position);
    else
      x["level"] = sic::support::Json::integer_value(e.level);
    if (e.axis >= 0) x["axis"] = sic::support::Json::integer_value(e.axis);
    if (e.k >= 0) x["k"] = sic::support::Json::integer_value(e.k);
    elements.array.push_back(x);
  }
  j["elements"] = elements;
  return j;
}
}  // namespace sic::sicx

namespace sic::sicx {
sic::support::Json rays_to_json(const Meta& m) {
  sic::support::Json j = sic::support::Json::object_value();
  j["balls"] = sic::support::Json::object_value();
  j["balls"]["limbs"] = sic::support::Json::integer_value(m.ball_limbs);
  j["balls"]["working_bits"] =
      sic::support::Json::integer_value(static_cast<int64_t>(m.ball_working_bits));
  sic::support::Json rays = sic::support::Json::array_value();
  for (const auto& r : m.rays) {
    sic::support::Json x = sic::support::Json::object_value();
    x["label"] = sic::support::Json::string_value(r.label);
    x["word"] = sic::support::Json::integer_array(r.word);
    x["anchor"] = sic::support::Json::integer_value(static_cast<int64_t>(r.anchor));
    x["accuracy_bits"] = sic::support::Json::integer_value(r.accuracy_bits);
    rays.array.push_back(x);
  }
  j["rays"] = rays;
  if (!m.rays_decoder.empty()) {
    j["decoder"] = sic::support::Json::object_value();
    j["decoder"]["name"] = sic::support::Json::string_value(m.rays_decoder);
    j["decoder"]["source_commit"] = sic::support::Json::string_value(m.rays_decoder_source_commit);
  }
  return j;
}
}  // namespace sic::sicx

namespace sic::sicx {
Meta meta_from_json(const sic::support::Json& j) {
  j.keys({"format", "version", "required_features", "d", "tower", "galois", "elements",
          "entry_rule", "generator_rescaling"});
  if (j.at("format").as_str() != "SICX")
    fail(sic::ErrorKind::invalid_format, "EXMT format is not SICX");
  const auto version = j.at("version").as_uints<uint64_t>();
  if (version.size() != 2) fail(ErrorKind::invalid_format, "invalid EXMT version");
  if (version[0] != kMajor || version[1] != kMinor)
    fail(sic::ErrorKind::unsupported, "unsupported EXMT version");
  Meta m;
  m.minor = version[1];
  for (const auto& f : j.at("required_features").as_array())
    m.required_features.push_back(f.as_str());
  if (!m.required_features.empty())
    fail(sic::ErrorKind::unsupported,
         "file requires unsupported feature " + m.required_features[0]);
  m.d = j.at("d").as_uint();
  if (m.d < 2 || m.d > kMaximumDimension)
    fail(sic::ErrorKind::invalid_format, "dimension must be in [2, 2^20]");
  m.units = std::make_shared<const UnitGroup>(m.d);
  m.D0 = field_radicand(m.d);
  const auto& tower = j.at("tower");
  tower.keys({"degrees", "branches", "branch_bits"});
  m.branch_bits = tower.at("branch_bits").as_uint();
  if (m.branch_bits < 128 || (m.branch_bits & (m.branch_bits - 1)) ||
      m.branch_bits > kMaximumBranchBits)
    fail(sic::ErrorKind::invalid_format, "branch_bits out of range");
  m.degrees = tower.at("degrees").as_uints<unsigned>();
  if (m.degrees.empty() || m.degrees.size() > kMaximumTowerDepth)
    fail(sic::ErrorKind::invalid_format, "tower depth out of range");
  for (auto p : m.degrees) {
    bool prime = p >= 2 && p <= kMaximumPrimeDegree;
    for (unsigned q = 2; q * q <= p && prime; ++q) prime = p % q;
    if (!prime) fail(sic::ErrorKind::invalid_format, "tower degree is not a prime <= 31");
    if (m.width.back() > kMaximumFieldDegree / p)
      fail(sic::ErrorKind::invalid_format, "tower degree product too large");
    m.width.push_back(m.width.back() * p);
  }
  m.D = m.width.back();
  for (const auto& b : tower.at("branches").as_array()) {
    const auto& pair = b.as_array();
    if (pair.size() != 2)
      fail(sic::ErrorKind::invalid_format, "branch rectangle must be a [real, imag] pair");
    validate_branch_string(pair[0].as_str());
    validate_branch_string(pair[1].as_str());
    m.branches.push_back({pair[0].as_str(), pair[1].as_str()});
  }
  if (m.branches.size() != m.degrees.size())
    fail(sic::ErrorKind::invalid_format, "one branch rectangle per tower level required");
  const auto& galois = j.at("galois");
  galois.keys({"smith", "unit_generator_words"});
  m.smith = galois.at("smith").as_uints<uint64_t>();
  uint64_t order = 1;
  for (auto n : m.smith) {
    if (n < 2 || n > m.D / order || m.D % n)
      fail(sic::ErrorKind::invalid_format, "invalid Smith factor");
    order *= n;
  }
  if (m.smith.empty() || order != m.D)
    fail(sic::ErrorKind::invalid_format, "Smith factors do not multiply to [L:K]");
  for (const auto& word : galois.at("unit_generator_words").as_array())
    m.unit_generator_words.push_back(word.as_uints<uint64_t>());
  if (m.unit_generator_words.size() != m.units->orders.size())
    fail(sic::ErrorKind::invalid_format, "unit generator word count");
  for (size_t word_index = 0; word_index < m.unit_generator_words.size(); ++word_index) {
    const auto& w = m.unit_generator_words[word_index];
    if (w.size() != m.smith.size())
      fail(sic::ErrorKind::invalid_format, "unit generator word rank");
    for (size_t l = 0; l < m.smith.size(); ++l) {
      if (w[l] >= m.smith[l]) fail(sic::ErrorKind::invalid_format, "unit generator word residue");
      // d <= 2^20 and Smith factors <= 2^40, so this product fits uint64_t.
      if (m.units->orders[word_index] * w[l] % m.smith[l])
        fail(sic::ErrorKind::invalid_format, "unit generator word order relation");
    }
  }
  const auto& rescaling = j.at("generator_rescaling");
  rescaling.keys({"rule", "shifts"});
  if (rescaling.at("rule").as_str() != kRescalingRule)
    fail(sic::ErrorKind::unsupported, "unknown rescaling rule");
  m.rescaling_shifts = rescaling.at("shifts").as_uints<uint64_t>();
  if (m.rescaling_shifts.size() != m.levels())
    fail(sic::ErrorKind::invalid_format, "rescaling shift count");
  for (auto n : m.rescaling_shifts)
    if (n > 999999999999999999ull)
      fail(sic::ErrorKind::invalid_format, "rescaling shift too large");
  const auto& entry = j.at("entry_rule");
  entry.keys({"name", "representatives"});
  if (entry.at("name").as_str() != kEntryRule)
    fail(sic::ErrorKind::unsupported, "unknown entry rule");
  const auto representatives = divisor_positions(m.d);
  if (entry.at("representatives").as_uints<uint64_t>() != representatives)
    fail(sic::ErrorKind::invalid_format, "entry representatives");
  // Elements.
  for (const auto& x : j.at("elements").as_array()) {
    ElementInfo e;
    e.kind = x.at("kind").as_str();
    const int rank = kind_rank(e.kind);
    if (e.kind == "rep") {
      x.keys({"kind", "levels", "max_bits", "scale", "position"});
      e.position = x.at("position").as_int();
      e.level = m.levels();
    } else {
      if (e.kind == "wsq" || e.kind == "actdelta")
        x.keys({"kind", "levels", "max_bits", "scale", "level"});
      else if (e.kind == "fcoef")
        x.keys({"kind", "levels", "max_bits", "scale", "level", "k"});
      else if (e.kind == "act")
        x.keys({"kind", "levels", "max_bits", "scale", "level", "axis"});
      else
        x.keys({"kind", "levels", "max_bits", "scale", "level", "axis", "k"});
      const auto level = x.at("level").as_uint();
      if (level >= m.levels()) fail(sic::ErrorKind::invalid_format, "element level out of range");
      e.level = static_cast<unsigned>(level);
      if (x.has("axis")) {
        e.axis = x.at("axis").as_int();
        if (e.axis < 0 || static_cast<uint64_t>(e.axis) >= m.smith.size())
          fail(sic::ErrorKind::invalid_format, "action axis out of range");
      }
      if (x.has("k")) {
        e.k = x.at("k").as_int();
        if (e.k < 0 || e.k >= m.degrees[e.level])
          fail(sic::ErrorKind::invalid_format, "coefficient index out of range");
      }
      const bool odd = m.degrees[e.level] != 2;
      if ((e.kind == "wsq" || e.kind == "act") == odd)
        fail(sic::ErrorKind::invalid_format, "element kind does not match level degree");
    }
    (void)rank;
    e.levels = x.at("levels").as_uints<unsigned>();
    for (auto l : e.levels)
      if (l >= e.level) fail(sic::ErrorKind::invalid_format, "element level set exceeds its field");
    e.layout = SubLayout(m.degrees, m.width, e.levels);
    e.scale = x.at("scale").as_str();
    if (!decimal_positive(e.scale))
      fail(sic::ErrorKind::invalid_format, "element scale must be a positive decimal integer");
    e.max_bits = x.at("max_bits").as_uint();
    if (e.max_bits > 8 * kMaxIntegerBytes)
      fail(sic::ErrorKind::resource, "element max_bits exceeds reader limit");
    derive_blocks(e, m.block_coordinates);
    m.elements.push_back(std::move(e));
  }
  // Required element set and canonical order.
  for (size_t i = 1; i < m.elements.size(); ++i)
    if (!element_less(m.elements[i - 1], m.elements[i]))
      fail(sic::ErrorKind::invalid_format, "elements not in canonical order or duplicated");
  std::map<std::tuple<std::string, unsigned, int64_t, int64_t>, bool> present;
  for (size_t i = 0; i < m.elements.size(); ++i) {
    const auto& e = m.elements[i];
    present[{e.kind, e.level, e.axis, e.k}] = true;
    if (e.kind == "rep") {
      if (e.position < 0 || static_cast<uint64_t>(e.position) >= m.d ||
          (e.position && m.d % static_cast<uint64_t>(e.position)))
        fail(sic::ErrorKind::invalid_format,
             "representative position must be 0 or a proper divisor of d");
      m.representative[e.position] = i;
    }
  }
  if (!m.representative.contains(0))
    fail(sic::ErrorKind::invalid_format, "missing representative 0");
  for (auto r : representatives)
    if (!m.representative.contains(static_cast<int64_t>(r)))
      fail(sic::ErrorKind::invalid_format, "missing representative " + std::to_string(r));
  for (unsigned i = 0; i < m.levels(); ++i) {
    const unsigned p = m.degrees[i];
    if (p == 2) {
      if (!present.contains({"wsq", i, -1, -1}))
        fail(sic::ErrorKind::invalid_format, "missing wsq element");
    } else {
      for (unsigned k = 0; k < p; ++k)
        if (!present.contains({"fcoef", i, -1, k}))
          fail(sic::ErrorKind::invalid_format, "missing fcoef element");
      bool any = false;
      for (size_t l = 0; l < m.smith.size(); ++l) {
        unsigned count = 0;
        for (unsigned k = 0; k < p; ++k)
          count += present.contains({"actodd", i, static_cast<int64_t>(l), k});
        if (count && count != p) fail(sic::ErrorKind::invalid_format, "incomplete odd action");
        any = any || count;
      }
      if (any != present.contains({"actdelta", i, -1, -1}))
        fail(sic::ErrorKind::invalid_format, "actdelta presence does not match odd actions");
    }
  }
  return m;
}
}  // namespace sic::sicx

namespace sic::sicx {
void rays_from_json(const sic::support::Json& j, Meta& m) {
  // Optional "decoder": the certified decoder that checked the balls before finalization.
  if (j.has("decoder")) {
    j.keys({"balls", "decoder", "rays"});
    const auto& dec = j.at("decoder");
    dec.keys({"name", "source_commit"});
    m.rays_decoder = dec.at("name").as_str();
    m.rays_decoder_source_commit = dec.at("source_commit").as_str();
    if (m.rays_decoder != kDecoderVersion)
      fail(sic::ErrorKind::unsupported, "unsupported RAYS decoder identifier");
    if ((m.rays_decoder_source_commit.size() != 40 && m.rays_decoder_source_commit.size() != 64) ||
        m.rays_decoder_source_commit.find_first_not_of("0123456789abcdef") != std::string::npos)
      fail(sic::ErrorKind::invalid_format,
           "RAYS decoder source_commit must be 40 or 64 lowercase hex digits");
  } else {
    j.keys({"balls", "rays"});
    m.rays_decoder.clear();
    m.rays_decoder_source_commit.clear();
  }
  const auto& balls = j.at("balls");
  balls.keys({"limbs", "working_bits"});
  const uint64_t limbs = balls.at("limbs").as_uint();
  if (limbs < 1 || limbs > kMaximumBallLimbs)
    fail(sic::ErrorKind::invalid_format, "ball limbs out of range");
  m.ball_limbs = static_cast<unsigned>(limbs);
  m.ball_working_bits = balls.at("working_bits").as_uint();
  if (!m.ball_working_bits || m.ball_working_bits > kMaximumWorkingBits)
    fail(sic::ErrorKind::invalid_format, "ball working_bits out of range");
  if (j.at("rays").as_array().empty())
    fail(sic::ErrorKind::invalid_format, "RAYS must list at least one ray");
  for (const auto& x : j.at("rays").as_array()) {
    x.keys({"label", "word", "anchor", "accuracy_bits"});
    Ray r;
    r.label = x.at("label").as_str();
    r.word = x.at("word").as_uints<uint64_t>();
    r.anchor = x.at("anchor").as_uint();
    r.accuracy_bits = x.at("accuracy_bits").as_int();
    if (r.accuracy_bits < -(1 << 20) || r.accuracy_bits > (1 << 20))
      fail(sic::ErrorKind::invalid_format, "ray accuracy_bits out of range");
    validate_ray_identity(m.rays.size(), r.label, r.word, m.smith,
                          m.rays.empty() ? nullptr : &m.rays.back().word);
    if (r.anchor >= m.d) fail(sic::ErrorKind::invalid_format, "ray anchor out of range");
    m.rays.push_back(std::move(r));
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
void validate_branch_string(const std::string& text) {
  if (text.size() > kMaximumBranchTextBytes)
    fail(sic::ErrorKind::resource, "branch string resource refusal");
  // Branch exponents are not BALL's fixed-width i64 fields. Load the finite
  // arbitrary-exponent Arb form and require exact canonical round-tripping.
  arb_t value;
  arb_init(value);
  if (arb_load_str(value, text.c_str()) || !arb_is_finite(value)) {
    arb_clear(value);
    fail(sic::ErrorKind::invalid_format, "invalid branch string");
  }
  char* dump = arb_dump_str(value);
  const bool canonical = text == dump;
  flint_free(dump);
  arb_clear(value);
  if (!canonical) fail(sic::ErrorKind::invalid_format, "noncanonical branch string");
}
}  // namespace sic::sicx
