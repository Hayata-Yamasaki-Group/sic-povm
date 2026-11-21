// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Types for double-precision numerical diagnostics. The exact construction
// uses separate FLINT/Arb types; these values and tolerances are not certificates.

#include <complex>
#include <vector>

namespace sic::core {

using Real = double;
using Complex = std::complex<Real>;
using Vector = std::vector<Complex>;

}  // namespace sic::core
