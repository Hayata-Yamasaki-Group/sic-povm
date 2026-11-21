// SPDX-License-Identifier: AGPL-3.0-or-later
// Unnormalized cyclotomic traces depend only on the exact norm residue.
#pragma once

#include <algorithm>
#include <numeric>
#include <span>

#include "exact/group/group.hpp"
#include "sic/support/ball_vectors.hpp"

namespace sic::exact {

std::vector<long> norm_image(const Group& group, std::span<const long> generators, long modulus);

// kernel_generators generate H_{i+1}, and tau represents H_i/H_{i+1}.
// roots[k] is the certified zeta_{2d}^k. Realness is checked using cc in H.
sic::support::RealBallVector cyclotomic_generator_values(
    const Group& group, long power, std::span<const long> kernel_generators, long subgroup_size,
    long tau, long cc, const sic::support::ComplexBallVector& roots, slong precision);

}  // namespace sic::exact
