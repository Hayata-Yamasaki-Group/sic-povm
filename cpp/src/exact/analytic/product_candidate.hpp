// SPDX-License-Identifier: AGPL-3.0-or-later
// Bounded empirical nonlinear candidate. Every use still needs certified
// Fourier support and step nonvanishing; no generic-success bound is claimed.
#pragma once

#include <random>

#include "exact/certification/bounds/height_bounds.hpp"
#include "sic/support/ball_vectors.hpp"

namespace sic::exact {

struct ProductCandidate {
  std::vector<std::vector<long>> factors;  // constant, then base coefficients
  certified::HeightBound certified_height;
  double height = 1;

  ProductCandidate(int generators, int count, unsigned seed);

  template <class BaseValue>
  void evaluate(acb_ptr out, slong wp, BaseValue&& base_value) const;
};

}  // namespace sic::exact

#include "exact/analytic/product_candidate.tpp"
