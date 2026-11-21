// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/integer.hpp"
// Sparse mixed-radix contraction and representative decoding.
#pragma once
#include "decode/kernels/kernels.hpp"
#include "decode/model.hpp"

namespace sic::decode {

// The value of one class at level i+1 from the children of its position at level i:
//   sum over the present digits d (increasing) of child_d[parent] * dual[d],
// computed by one acb_dot (exact accumulation, one outward rounding). child[d] ==
// nullptr is a structural zero and is omitted. The Carry and lift use this one
// function, so every class value is the same whatever the cut level or schedule.
void combine_class(acb_ptr out, const Class& cls, const acb_srcptr* child, unsigned p, slong prec);

// Mixed-radix carry over tower levels [low, high) for one aligned subtree.
// Positions are in units of width(low) relative to the subtree base. Feeding a
// vector at level `low` (or scalars when low = 0) in increasing position order,
// with aligned zero runs, computes sum_e lambda_e prod_i dual_i[e_i] for every
// class at level `high`. The children of a position are kept until its last
// digit arrives; then each class value is one combine_class (absent children,
// i.e. zero runs, are omitted). A position without any present child is absent.
class Carry {
  const Tree& t_;
  unsigned low_, high_;
  std::vector<std::vector<ComplexBallVector>>
      kids_;                                // per level: the p child vectors of the open position
  std::vector<std::vector<char>> present_;  // per level: which children are present
  std::vector<char> open_;
  std::vector<ComplexBallVector> sums_;  // per level i: the produced vector of level i + 1
  ComplexBallVector result_;
  uint64_t visited_ = 0, total_;
  bool complete_ = false;
  uint64_t rel(unsigned i) const;

  void feed(uint64_t position, unsigned first, acb_srcptr lower, bool zero);

 public:
  Carry(const Tree& tree, unsigned low, unsigned high);
  // value: count(low) balls (a scalar when low = 0).
  void value(uint64_t position, acb_srcptr lower);
  void zeros_until(uint64_t position);
  // Result vector (moved out); the carry must not be used afterwards.
  ComplexBallVector release() {
    finish();
    return std::move(result_);
  }
  const ComplexBallVector& finish();
};

}  // namespace sic::decode

#include "decode/contraction.inl.hpp"
