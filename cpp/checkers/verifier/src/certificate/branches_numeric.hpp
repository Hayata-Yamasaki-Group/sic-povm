// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::verifier {

// SPDX-License-Identifier: AGPL-3.0-or-later
// Certified fast path: interval separation proves strict algebraic comparisons.
// Exact zero-radius results prove singleton equality; unresolved comparisons use qqbar.
string interval_component(const arb_t x, slong bits, arb_t box, slong prec);

bool numeric_branch_check(Container& C);
}  // namespace sic_check::verifier
