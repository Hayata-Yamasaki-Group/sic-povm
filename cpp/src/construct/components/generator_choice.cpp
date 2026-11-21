// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/components/generator_choice.hpp"

namespace sic::construct {
EvaluatedGenerator StepSelectionEvaluator::evaluate(unsigned level, const GeneratorChoice& choice,
                                                    slong bits) {
  auto choices = base_;
  choices.at(level) = choice;
  auto all = evaluate_all(choices, bits, false);
  if (!all.ok) throw sic::Error(sic::ErrorKind::construction, "generator proposal did not certify");
  return {std::move(all.g.at(level)), all.certified_Cg.at(level), all.certified_Cf.at(level)};
}

}  // namespace sic::construct

namespace sic::construct {
EvaluatedTower StepSelectionEvaluator::evaluate_all(const std::vector<GeneratorChoice>& choices,
                                                    slong precision, bool backed, bool verbose) {
  return evaluate_(choices, precision, backed, verbose);
}
}  // namespace sic::construct
