// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <functional>
#include <variant>

#include "exact/certification/bounds/height_bounds.hpp"
#include "exact/storage/analytic_generator.hpp"
#include "sic/error.hpp"
namespace sic::construct {
inline const std::string kStepSelection = "sic-step-selection/1.0";
inline const std::string kConjugateAlignment = "sic-conjugate-alignment/1.0";
inline const std::string kGaloisCombination = "sic-galois-combination/1.0";
inline const std::string kDyadicEvaluator = "sic-dyadic-evaluator/1.0";
inline const std::string kGaloisChoice = kStepSelection + ":" + kGaloisCombination;
struct ResourceForecast {
  uint64_t resident_bytes = 0, scratch_bytes = 0, spool_bytes = 0;
};
struct Candidate {
  std::string kind = kStepSelection;
  sic::support::Index index = sic::support::kNoIndex;
  std::vector<long> factors;
  long twist = 0;
  std::string name;
};
struct GaloisTerm {
  std::vector<long> word;
  int64_t a0 = 1, a1 = 0;
};
struct GaloisCombination {
  Candidate base;
  std::vector<GaloisTerm> terms;
};
struct GeneratorChoice {
  std::variant<Candidate, GaloisCombination> form;
  ResourceForecast forecast;
  std::function<ResourceForecast(slong)> forecast_at;
};
// The existing batched value type preserves allocation and arithmetic order across the interface.
struct EvaluatedTower {
  std::vector<sic::exact::AnalyticGenerator> g;
  std::vector<std::vector<sic::support::ComplexBallVector>> fc;
  std::vector<double> Cg, log2U;
  std::vector<sic::exact::certified::HeightBound> certified_Cg;
  std::vector<std::vector<sic::exact::certified::HeightBound>> certified_Cf;
  std::vector<sic::support::Index> sel;
  bool ok = true;
};
struct EvaluatedGenerator {
  sic::exact::AnalyticGenerator generator;
  sic::exact::certified::HeightBound certified_Cg;
  std::vector<sic::exact::certified::HeightBound> certified_Cf;
};
// Evaluator calls may mutate backing storage; no simultaneous calls or retained
// Arb pointers. Returned evaluated towers own their buffers until moved/replaced.
struct GeneratorEvaluator {
  virtual ~GeneratorEvaluator() = default;
  virtual EvaluatedTower evaluate_all(const std::vector<GeneratorChoice>&, slong, bool backed,
                                      bool verbose = false) = 0;
  virtual EvaluatedGenerator evaluate(unsigned level, const GeneratorChoice&, slong precision) = 0;
};
class StepSelectionEvaluator final : public GeneratorEvaluator {
 public:
  using Evaluate =
      std::function<EvaluatedTower(const std::vector<GeneratorChoice>&, slong, bool, bool)>;
  StepSelectionEvaluator(std::vector<GeneratorChoice> base, Evaluate evaluate)
      : base_(std::move(base)), evaluate_(std::move(evaluate)) {}
  EvaluatedTower evaluate_all(const std::vector<GeneratorChoice>& choices, slong precision,
                              bool backed, bool verbose = false) override;
  EvaluatedGenerator evaluate(unsigned level, const GeneratorChoice& choice, slong bits) override;

 private:
  std::vector<GeneratorChoice> base_;
  Evaluate evaluate_;
};
}  // namespace sic::construct
