// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/fmpz_vec.h>
#include <flint/gr.h>
#include <flint/gr_poly.h>
#include <flint/gr_vec.h>
#include <flint/qqbar.h>

#include "../model/input_model.hpp"
namespace sic_check::norm {

struct Algebraic {
  qqbar_t v;

  Algebraic();

  Algebraic(const Algebraic& x);

  Algebraic(Algebraic&& x) noexcept;

  Algebraic& operator=(const Algebraic& x);

  ~Algebraic();
};

// Admission bounds are intentionally conservative: a possible expansion beyond
// the budget is a resource refusal even if its eventual minimal polynomial might
// be smaller. Never rely on gr_ctx limits to constrain direct qqbar operations.
class BoundedAlgebra {
  AlgebraicLimits limits;

 public:
  explicit BoundedAlgebra(AlgebraicLimits budget = {});

  void bound(uint64_t degree, uint64_t height, const char* operation) const;

  void check(const qqbar_t x) const;

  void binary_bound(const qqbar_t x, const qqbar_t y) const;

  void set(qqbar_t out, const fmpz_t n) const;

  void add(qqbar_t out, const qqbar_t x, const qqbar_t y) const;

  void mul(qqbar_t out, const qqbar_t x, const qqbar_t y) const;

  void div(qqbar_t out, const qqbar_t x, const qqbar_t y) const;

  void shift(qqbar_t out, const qqbar_t x, slong bits) const;

  void components(qqbar_t re, qqbar_t im, const qqbar_t z) const;

  Algebraic integer(const fmpz_t n) const;
};

// Comparisons, including singleton and half-integer ties, use exact algebraic
// numbers. Ball rounding cannot prove these equality cases.
void branch_component(arb_t out, const qqbar_t component, uint64_t bits,
                      const BoundedAlgebra& arithmetic = BoundedAlgebra{});

ComplexBall branch_rectangle(const Algebraic& z, uint64_t bits,
                             const BoundedAlgebra& arithmetic = BoundedAlgebra{});

std::vector<Algebraic> polynomial_roots(const std::vector<Algebraic>& coefficients,
                                        AlgebraicLimits limits = {});

void verify_canonical_branches(const Meta& m, const std::vector<std::vector<Pair>>& leaves,
                               AlgebraicLimits limits);
}  // namespace sic_check::norm
