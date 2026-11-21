// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::norm {

bool interval_component(arb_t box, const arb_t value, uint64_t bits, slong prec);

bool verify_quadratic_branches(const Meta& m, const std::vector<std::vector<Pair>>& leaves);
}  // namespace sic_check::norm
