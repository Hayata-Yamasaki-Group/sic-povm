// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/exact/tower.hpp"
#include "sicx/model/branches.hpp"
#pragma once
#include "construct/components/components.hpp"
#include "sic/sicx/exact.hpp"
namespace sic::construct {
// Opt-in exact services for bounded extension experiments. Values own their FLINT
// rationals; the service owns its tower. No rounded approximation is an exact proof.
class ExactServices {
 public:
  using Value = sic::sicx::exact::Elt;
  explicit ExactServices(const ExactTower& input, const sic::construct::ProductionPolicy& policy);
  Value from_trace(unsigned level, const sic::sicx::LeafSource& source) const;
  Value multiply(const Value& a, const Value& b, unsigned level) const;
  Value inverse(const Value& a, unsigned level) const;
  Value galois(const Value& a, unsigned level, const std::vector<Value>& generator_images) const;
  // M*g is integral when M clears every defining coefficient denominator:
  // X^p + sum M^(p-k)c_k X^k is monic over O_K. M=1 certifies g integral.
  std::vector<std::string> integrality_denominators() const;
  sic::sicx::CanonicalBranches isolate() const;
  void verify_written(const std::filesystem::path& path) const;

 private:
  static uint64_t value_bits(const Value& value);
  static const sic::sicx::Meta& admit(const sic::sicx::Meta& meta,
                                      const sic::construct::ProductionPolicy& policy,
                                      uint64_t operand_bits = 0);
  ExactTower input_;
  sic::construct::ProductionPolicy policy_;
  sic::sicx::exact::Tower tower_;
};
}  // namespace sic::construct
