// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::verifier {

// SPDX-License-Identifier: AGPL-3.0-or-later
// Exact, resource-bounded verification of spec 5.1b, independent of numerical decoding.
struct Algebraic {
  qqbar_t v;

  Algebraic();

  Algebraic(const Algebraic& x);

  Algebraic& operator=(const Algebraic& x);

  ~Algebraic();
};

struct AlgebraicArray {
  qqbar_ptr v;
  slong n;

  explicit AlgebraicArray(slong count);

  ~AlgebraicArray();
};

string exact_component(const qqbar_t x, slong bits, arb_t interval);

void branch_check_exact(Container& C, const Opts& O);

void branch_check(Container& C, const Opts& O);
}  // namespace sic_check::verifier
