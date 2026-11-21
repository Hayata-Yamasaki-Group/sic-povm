// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <memory>

#include "exact/analytic/direct_eta_rows.hpp"
#include "support/resources.hpp"

namespace sic::construct::resources {
// Recheck fresh headroom before each extraction. Optional rows are disabled
// when their possible replacement/refill would displace mandatory workspace.
void admit_direct_rows(std::unique_ptr<sic::exact::DirectEtaRows>& rows,
                       const sic::support::resources::Budget& budget,
                       sic::support::resources::Bytes workspace);
}  // namespace sic::construct::resources
