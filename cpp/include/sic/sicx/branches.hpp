// SPDX-License-Identifier: AGPL-3.0-or-later
// Certified absolute-grid branch rectangles, SICX 1.0 section 5.1b.
#pragma once
#include <flint/acb_poly.h>

#include "sic/sicx/format.hpp"
#include "sic/support/ball_vectors.hpp"
#include "sic/support/flint_raii.hpp"
#include "sic/support/stop.hpp"

namespace sic::sicx {
class BranchRefusal : public Error {
 public:
  explicit BranchRefusal(const std::string& message);
};
struct CanonicalBranches {
  uint64_t bits;
  std::vector<std::array<std::string, 2>> rectangles;
  std::vector<sic::support::ComplexBallVector> selected_roots;
  slong precision = 0;
};

// An enclosure only decides strict inequalities. Equality is accepted solely
// when the arithmetic proves an exact dyadic component; unresolved equality is
// a precision/resource refusal, never a guessed singleton or positive radius.
bool branch_grid(arb_t out, const arb_t x, slong p, slong prec);
std::string branch_dump(const arb_t x);

// Source(id, visitor) yields the full, rescaled trace leaves in original basis.
// No whole exact element or whole file is allocated.

}  // namespace sic::sicx
