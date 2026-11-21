// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "construct/components/components.hpp"
#include "construct/model/alignment_options.hpp"
namespace sic::construct {
class ConjugateAlignmentSelector final : public GeneratorSelector {
  std::vector<GeneratorChoice> base_;
  AlignmentOptions options_;
  sic::support::Json provenance_ = sic::support::Json::object_value();

 public:
  ConjugateAlignmentSelector(std::vector<GeneratorChoice> base, AlignmentOptions options);
  std::string name() const override;
  std::vector<GeneratorChoice> select(const TowerShape&, TowerContext&, Context&, PrecisionManager&,
                                      CheckpointStore&) override;
  sic::support::Json provenance() const override;
};
}  // namespace sic::construct
