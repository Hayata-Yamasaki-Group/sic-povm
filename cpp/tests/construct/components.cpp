// SPDX-License-Identifier: AGPL-3.0-or-later
#include "construct/components/components.hpp"

#include <stdexcept>

int main() {
  using namespace sic::construct;
  sic::support::resources::Budget budget;
  Context context{budget, 1, {}};
  PrecisionManager precision{144};
  CheckpointStore checkpoints;
  TowerShape shape;
  TowerContext tower;
  int calls = 0;
  tower.select_candidates = [&] {
    ++calls;
    Candidate candidate;
    candidate.index = 7;
    return std::vector<GeneratorChoice>{{candidate, {}}};
  };
  tower.certify = [](unsigned, const GeneratorChoice&, slong) { return true; };
  Components components;
  const auto choices = components.selector->select(shape, tower, context, precision, checkpoints);
  if (calls != 1 || std::get<Candidate>(choices.at(0).form).index != 7 ||
      components.selector->name() != "sic-step-selection/1.0" ||
      components.rescaling->name() != "sicx-2adic-greedy/1.0" || !components.transforms.empty())
    throw std::runtime_error("default component composition changed");
  sic::sicx::Meta meta;
  meta.degrees = {2};
  meta.width = {1, 2};
  meta.D = 2;
  sic::sicx::RescalingCollector collector(meta);
  NoRescalingRule none;
  ExactTowerOps ops;
  ops.degrees = {2};
  ops.threads = 1;
  if (none.choose(collector, ops).shifts != std::vector<uint64_t>{0})
    throw std::runtime_error("none rule changed");

  StepSelectionEvaluator evaluator(choices, [](const auto& frozen, slong, bool, bool) {
    EvaluatedTower evaluated;
    evaluated.sel = {std::get<Candidate>(frozen.at(0).form).index};
    return evaluated;
  });
  auto changed = choices;
  std::get<Candidate>(changed[0].form).index = 3;
  if (evaluator.evaluate_all(changed, 256, false).sel != std::vector<sic::support::Index>{3})
    throw std::runtime_error("evaluation ignored frozen generator choice");
  components.rescaling = std::make_unique<NoRescalingRule>();
  bool refused = false;
  try {
    components.require_native_supported();
  } catch (const std::runtime_error&) {
    refused = true;
  }
  if (!refused) throw std::runtime_error("noncanonical rescaling was silently accepted");
}
