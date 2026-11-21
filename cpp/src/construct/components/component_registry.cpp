// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/components/component_registry.hpp"

namespace sic::construct {
struct ComponentRegistry::State {
  std::map<std::string, Kind> kinds;
  std::map<std::string, Family> families;
};
ComponentRegistry::ComponentRegistry() : state_(std::make_shared<State>()) {}

std::string choice_kind(const GeneratorChoice& choice) {
  if (const auto* candidate = std::get_if<Candidate>(&choice.form)) return candidate->kind;
  return std::get<GaloisCombination>(choice.form).base.kind + ":" + kGaloisCombination;
}

void ComponentRegistry::add_kind(std::string name, Kind kind) {
  if (name.empty() || !kind.validate || !kind.certify || !kind.storage.resident ||
      !state_->kinds.emplace(std::move(name), std::move(kind)).second)
    throw sic::Error(sic::ErrorKind::invalid_argument, "invalid or duplicate generator kind");
}

void ComponentRegistry::add_family(std::string name, Family family) {
  if (name.empty() || family.kinds.empty() || !family.evaluate ||
      !state_->families.emplace(std::move(name), std::move(family)).second)
    throw sic::Error(sic::ErrorKind::invalid_argument, "invalid or duplicate evaluator family");
}

const ComponentRegistry::Kind& ComponentRegistry::kind(const GeneratorChoice& choice) const {
  const auto found = state_->kinds.find(choice_kind(choice));
  if (found == state_->kinds.end())
    throw sic::Error(sic::ErrorKind::unsupported,
                     "unknown generator choice kind: " + choice_kind(choice));
  return found->second;
}

StorageCapabilities ComponentRegistry::capabilities(
    const std::vector<GeneratorChoice>& choices) const {
  StorageCapabilities result{true, true, true};
  for (const auto& choice : choices) {
    const auto caps = kind(choice).storage;
    result.resident &= caps.resident;
    result.bounded &= caps.bounded;
    result.replayable &= caps.replayable;
  }
  return result;
}

EvaluatedTower ComponentRegistry::evaluate(const std::string& family,
                                           const std::vector<GeneratorChoice>& choices, slong bits,
                                           bool bounded, bool verbose) const {
  const auto found = state_->families.find(family);
  if (found == state_->families.end())
    throw sic::Error(sic::ErrorKind::unsupported, "unknown evaluator family");
  for (unsigned level = 0; level < choices.size(); ++level) {
    const auto& service = kind(choices[level]);
    service.validate(level, choices[level]);
    if (!found->second.kinds.contains(choice_kind(choices[level])) ||
        (bounded && (!service.storage.bounded || !service.storage.replayable)))
      throw sic::Error(sic::ErrorKind::unsupported, "unsupported evaluator/storage composition");
  }
  auto result = found->second.evaluate(choices, bits, bounded, verbose);
  const auto size = choices.size();
  if (result.ok && (result.g.size() != size || result.fc.size() != size ||
                    result.certified_Cg.size() != size || result.certified_Cf.size() != size ||
                    result.Cg.size() != size || result.log2U.size() != size))
    throw sic::Error(sic::ErrorKind::invalid_argument, "evaluator result shape");
  return result;
}

EvaluatedTower RegistryEvaluator::evaluate_all(const std::vector<GeneratorChoice>& choices,
                                               slong bits, bool bounded, bool verbose) {
  if (choices.size() != base_.size())
    throw sic::Error(sic::ErrorKind::invalid_argument, "generator choice count");
  return registry_.evaluate(family_, choices, bits, bounded, verbose);
}

EvaluatedGenerator RegistryEvaluator::evaluate(unsigned level, const GeneratorChoice& choice,
                                               slong bits) {
  auto choices = base_;
  choices.at(level) = choice;
  auto all = evaluate_all(choices, bits, false);
  if (!all.ok) throw sic::Error(sic::ErrorKind::construction, "generator proposal did not certify");
  return {std::move(all.g.at(level)), all.certified_Cg.at(level), all.certified_Cf.at(level)};
}

}  // namespace sic::construct

namespace sic::construct {
bool ComponentRegistry::certify(unsigned level, const GeneratorChoice& choice, slong bits) const {
  const auto& service = kind(choice);
  service.validate(level, choice);
  return service.certify(level, choice, bits);
}
}  // namespace sic::construct

namespace sic::construct {
bool ComponentRegistry::empty() const { return state_->kinds.empty() && state_->families.empty(); }
bool ComponentRegistry::has_kind(const std::string& name) const {
  return state_->kinds.contains(name);
}
bool ComponentRegistry::has_family(const std::string& name) const {
  return state_->families.contains(name);
}
}  // namespace sic::construct
