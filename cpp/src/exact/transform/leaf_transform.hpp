// SPDX-License-Identifier: AGPL-3.0-or-later
// Depth-first inverse trace transform. Natural leaf blocks are consumed in
// order; only the requested conjugate labels and their prefixes are retained.
#pragma once

#include <algorithm>
#include <functional>
#include <stdexcept>

#include "sic/support/ball_vectors.hpp"

namespace sic::exact {

template <class Read, class Term>
sic::support::ComplexBallVector inverse_leaf_tree(const std::vector<long>& degrees, int n,
                                                  const std::vector<long>& labels, Read&& read,
                                                  Term&& term);

}  // namespace sic::exact

#include "exact/transform/leaf_transform.tpp"
