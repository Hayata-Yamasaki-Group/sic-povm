// SPDX-License-Identifier: AGPL-3.0-or-later
// Portable double-double arithmetic for the heuristic Jacobian row selector.
// Certification always uses Arb; this type only chooses a candidate subsystem.
#pragma once
#include "exact/arithmetic/double_double.hpp"

namespace sic::exact {
using SelectionFloat = DoubleDouble;

#if defined(__SIZEOF_FLOAT128__)
using SelectionWide = __float128;
#else
using SelectionWide = SelectionFloat;
#endif
}  // namespace sic::exact
