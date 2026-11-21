// SPDX-License-Identifier: AGPL-3.0-or-later
// Balanced products preserve rigorous rectangles while reducing wrapping.
#pragma once

#include <bit>
#include <stdexcept>

#include "sic/support/ball_vectors.hpp"

namespace sic::support {

class BalancedComplexProduct {
  sic::support::ComplexBallVector partial_, work_{1};
  size_t capacity_, count_ = 0;

 public:
  explicit BalancedComplexProduct(size_t capacity);

  void reset() { count_ = 0; }

  void append(acb_srcptr factor, slong precision);

  void get(acb_ptr result, slong precision) const;
};

}  // namespace sic::support
