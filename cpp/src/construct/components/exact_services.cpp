// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/components/exact_services.hpp"

#include "sic/support/flint_raii.hpp"

namespace sic::construct {
ExactServices::ExactServices(const ExactTower& input,
                             const sic::construct::ProductionPolicy& policy)
    : input_((admit(input.metadata, policy), input)), policy_(policy), tower_(input.metadata) {
  const auto& meta = input_.metadata;
  for (unsigned level = 0; level < meta.levels(); ++level) {
    std::vector<Value> coefficients(meta.degrees[level], tower_.zero(level));
    bool found = false;
    for (size_t id = 0; id < input_.elements.size(); ++id) {
      const auto& element = input_.elements[id];
      if (element.id.level != level) continue;
      if (element.id.kind == "wsq") {
        coefficients[0] = from_trace(level, element.leaves);
        for (auto& value : coefficients[0]) {
          fmpq_neg(value.a, value.a);
          fmpq_neg(value.b, value.b);
        }
        found = true;
      } else if (element.id.kind == "fcoef") {
        coefficients.at(element.id.k) = from_trace(level, element.leaves);
        found = true;
      }
    }
    if (!found) throw sic::Error(sic::ErrorKind::unsupported, "missing exact defining polynomial");
    tower_.set_level(level, std::move(coefficients));
  }
}

ExactServices::Value ExactServices::from_trace(unsigned level,
                                               const sic::sicx::LeafSource& source) const {
  if (level > tower_.t) throw sic::Error(sic::ErrorKind::invalid_argument, "exact service level");
  // Explicit admission before allocating dense rational values.
  sic::support::resources::Budget budget;
  budget.require_additional("extension exact arithmetic",
                            sic::support::resources::mul(tower_.N[level], 1024));
  (void)admit(input_.metadata, policy_);
  Value leaves = tower_.zero(level);
  uint64_t seen_bits = 0;
  source([&](uint64_t position, const fmpz* x, const fmpz* y) {
    const uint64_t bits = std::max(fmpz_bits(x), fmpz_bits(y));
    if (bits > seen_bits) {
      (void)admit(input_.metadata, policy_, bits);
      seen_bits = bits;
    }
    auto& value = leaves.at(position);
    fmpq_set_fmpz(value.a, x);
    fmpq_set_fmpz(value.b, y);
  });
  (void)admit(input_.metadata, policy_, value_bits(leaves));
  return tower_.from_leaves(leaves.data(), level);
}

ExactServices::Value ExactServices::multiply(const Value& a, const Value& b, unsigned level) const {
  if (level > tower_.t || a.size() != tower_.N[level] || b.size() != tower_.N[level])
    throw sic::Error(sic::ErrorKind::invalid_argument, "exact multiplication shape");
  (void)admit(input_.metadata, policy_, sic::support::resources::add(value_bits(a), value_bits(b)));
  return tower_.mul(a, b, level);
}

ExactServices::Value ExactServices::inverse(const Value& a, unsigned level) const {
  if (level > tower_.t || a.size() != tower_.N[level])
    throw sic::Error(sic::ErrorKind::invalid_argument, "exact inverse shape");
  (void)admit(input_.metadata, policy_, value_bits(a));
  return tower_.inverse(a, level);
}

ExactServices::Value ExactServices::galois(const Value& a, unsigned level,
                                           const std::vector<Value>& generator_images) const {
  if (level > tower_.t || a.size() != tower_.N[level] || generator_images.size() < level)
    throw sic::Error(sic::ErrorKind::invalid_argument, "exact action shape");
  for (unsigned i = 0; i < level; ++i)
    if (generator_images[i].size() != tower_.N[i + 1])
      throw sic::Error(sic::ErrorKind::invalid_argument, "exact action image shape");
  uint64_t bits = value_bits(a);
  for (unsigned i = 0; i < level; ++i)
    bits = sic::support::resources::add(bits, value_bits(generator_images[i]));
  (void)admit(input_.metadata, policy_, bits);
  return tower_.apply(a, level, generator_images);
}

std::vector<std::string> ExactServices::integrality_denominators() const {
  std::vector<std::string> result;
  sic::support::flint::Fmpz denominator;
  for (const auto& coefficients : tower_.f) {
    fmpz_one(denominator);
    for (const auto& coefficient : coefficients)
      for (const auto& value : coefficient) {
        fmpz_lcm(denominator, denominator, fmpq_denref(value.a));
        fmpz_lcm(denominator, denominator, fmpq_denref(value.b));
      }
    sic::support::flint::Fmpz lower, power;
    for (size_t i = 0; i < result.size(); ++i) {
      fmpz_set_str(lower, result[i].c_str(), 10);
      fmpz_pow_ui(power, lower, tower_.p[i] - 1);
      fmpz_mul(denominator, denominator, power);
    }
    sic::support::flint::String raw(fmpz_get_str(nullptr, 10, denominator));
    result.emplace_back(raw.get());
  }
  return result;
}

sic::sicx::CanonicalBranches ExactServices::isolate() const {
  return sic::sicx::canonical_branches(
      input_.metadata, [&](size_t id, const auto& visit) { input_.elements.at(id).leaves(visit); },
      policy_.precision.maximum_bits);
}

uint64_t ExactServices::value_bits(const Value& value) {
  uint64_t bits = 0;
  for (const auto& entry : value) {
    bits = std::max(bits, sic::support::resources::add(fmpz_bits(fmpq_numref(entry.a)),
                                                       fmpz_bits(fmpq_denref(entry.a))));
    bits = std::max(bits, sic::support::resources::add(fmpz_bits(fmpq_numref(entry.b)),
                                                       fmpz_bits(fmpq_denref(entry.b))));
  }
  return bits;
}

const sic::sicx::Meta& ExactServices::admit(const sic::sicx::Meta& meta,
                                            const sic::construct::ProductionPolicy& policy,
                                            uint64_t operand_bits) {
  if (meta.width.size() != meta.degrees.size() + 1 || meta.width.empty() || meta.width[0] != 1 ||
      meta.width.back() != meta.D)
    throw sic::Error(sic::ErrorKind::invalid_argument, "exact service tower shape");
  for (size_t i = 0; i < meta.degrees.size(); ++i)
    if (meta.degrees[i] < 2 ||
        meta.width[i + 1] != sic::support::resources::mul(meta.width[i], meta.degrees[i]))
      throw sic::Error(sic::ErrorKind::invalid_argument, "exact service tower widths");
  if (!meta.D || meta.D > policy.exact_service_degree)
    throw sic::Error(sic::ErrorKind::resource, "exact service degree allowance");
  uint64_t bits = std::max<uint64_t>(128, operand_bits);
  for (const auto& element : meta.elements)
    bits = std::max(
        bits, sic::support::resources::add(element.max_bits,
                                           sic::support::resources::mul(4, element.scale.size())));
  // Q payload growth allowance, all coefficient/dual buffers, and the
  // full 2D by 2D native inverse matrix, including set_level() dual construction.
  const auto payload = sic::support::resources::add(
      512, sic::support::resources::mul(
               2, sic::support::resources::mul(meta.D, sic::support::resources::add(bits, 128))));
  const auto cells = sic::support::resources::add(
      sic::support::resources::mul(16, sic::support::resources::mul(meta.D, meta.D)),
      sic::support::resources::mul(128, meta.D));
  const auto bytes = sic::support::resources::add(sic::support::resources::kMib,
                                                  sic::support::resources::mul(cells, payload));
  if (bytes > policy.exact_service_bytes)
    throw sic::Error(sic::ErrorKind::resource, "exact service workspace allowance");
  sic::support::resources::Budget budget;
  budget.require_additional("bounded exact extension service", bytes);
  return meta;
}

}  // namespace sic::construct

namespace sic::construct {
void ExactServices::verify_written(const std::filesystem::path& path) const {
  sic::sicx::Reader reader(path, policy_.resources);
  (void)sic::sicx::verify_all(reader, 1, {policy_.precision, policy_.valuation});
}
}  // namespace sic::construct
