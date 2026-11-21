// SPDX-License-Identifier: AGPL-3.0-or-later
#include "search/catalogue_match.hpp"
namespace sic::search {
CatalogueComparison catalogue_match(const sic::core::Vector& phi, const sic::core::Vector& psi,
                                    const MatchOptions& opt, int moments,
                                    const std::vector<double>& mpsi) {
  const auto mphi =
      sic::search::clifford_moments(sic::search::phase_table(sic::core::SicFrame(phi)), moments);
  double dm = 0;
  for (int k = 0; k < moments; ++k) dm = std::max(dm, std::abs(mphi[k] - mpsi[k]));
  sic::search::MatchStats st;
  const auto found = sic::search::clifford_maps(phi, psi, opt, &st);

  return {dm, std::move(st), std::move(found)};
}
}  // namespace sic::search
