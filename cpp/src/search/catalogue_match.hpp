// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/search/clifford.hpp"
namespace sic::search {
struct CatalogueComparison {
  double moment_difference;
  MatchStats stats;
  std::vector<sic::core::clifford::CliffordElement> matches;
};
CatalogueComparison catalogue_match(const sic::core::Vector& phi, const sic::core::Vector& psi,
                                    const MatchOptions& opt, int moments,
                                    const std::vector<double>& mpsi);
}  // namespace sic::search
