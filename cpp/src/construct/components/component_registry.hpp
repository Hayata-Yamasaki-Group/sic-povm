// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <map>
#include <set>

#include "construct/components/generator_choice.hpp"
namespace sic::construct {
struct StorageCapabilities {
  bool resident = true;
  bool bounded = false;
  bool replayable = false;
};
std::string choice_kind(const GeneratorChoice& choice);
// Registration is an owner-thread operation before selection. Functions may borrow
// the construction and must not escape it. Evaluator families explicitly admit
// every choice kind they interpret; unsupported mixtures fail before evaluation.
class ComponentRegistry {
 public:
  ComponentRegistry();
  struct Kind {
    StorageCapabilities storage;
    std::function<void(unsigned, const GeneratorChoice&)> validate;
    std::function<bool(unsigned, const GeneratorChoice&, slong)> certify;
  };
  using Evaluate = StepSelectionEvaluator::Evaluate;
  struct Family {
    std::set<std::string> kinds;
    Evaluate evaluate;
  };
  bool empty() const;
  bool has_kind(const std::string& name) const;
  bool has_family(const std::string& name) const;
  void add_kind(std::string name, Kind kind);
  void add_family(std::string name, Family family);
  const Kind& kind(const GeneratorChoice& choice) const;
  bool certify(unsigned level, const GeneratorChoice& choice, slong bits) const;
  StorageCapabilities capabilities(const std::vector<GeneratorChoice>& choices) const;
  EvaluatedTower evaluate(const std::string& family, const std::vector<GeneratorChoice>& choices,
                          slong bits, bool bounded, bool verbose) const;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
class RegistryEvaluator final : public GeneratorEvaluator {
 public:
  RegistryEvaluator(const ComponentRegistry& registry, std::string family,
                    std::vector<GeneratorChoice> base)
      : registry_(registry), family_(std::move(family)), base_(std::move(base)) {}
  EvaluatedTower evaluate_all(const std::vector<GeneratorChoice>& choices, slong bits, bool bounded,
                              bool verbose = false) override;
  EvaluatedGenerator evaluate(unsigned level, const GeneratorChoice& choice, slong bits) override;

 private:
  const ComponentRegistry& registry_;
  std::string family_;
  std::vector<GeneratorChoice> base_;
};
}  // namespace sic::construct
