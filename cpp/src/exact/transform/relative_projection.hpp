// SPDX-License-Identifier: AGPL-3.0-or-later
// Certified local tests for an index-two relative trace.
#pragma once

#include <span>
#include <unordered_set>

#include "exact/group/group.hpp"
#include "sic/support/ball_vectors.hpp"

namespace sic::exact {

long smith_character_exponent(const Group& group, long character, long element, long modulus);

std::vector<long> quadratic_annihilator_generators(const Group& group,
                                                   std::span<const long> characters);

std::vector<long> enumerate_exact_subgroup(const Group& group, std::span<const long> generators,
                                           size_t limit);

template <class Value>
void relative_projection_at(acb_ptr out, const Group& group, std::span<const long> subgroup,
                            long tau, long anchor, slong precision, const Value& value);

}  // namespace sic::exact

#include "exact/transform/relative_projection.tpp"
