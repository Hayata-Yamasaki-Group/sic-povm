// SPDX-License-Identifier: AGPL-3.0-or-later
// Scalar fixed fields for the stabilizer-adapted tower when [L:K] has an odd part
// (generalizes fixed_field_tower.hpp, which needs a
// power-of-two group). Used only by apps/construct/main.cpp.
//
// G = Gal(L/K) is abelian, G = G_2 x G_odd. Every scalar stabilizer H_m is a 2-group, so
// E_m = L^{H_m} contains L^{G_2}, the odd-degree subfield (degree = odd part of N). The adapted
// tower therefore adjoins the odd prime steps first (bottom of the tower) and then the quadratic
// chain constrained to annihilate H_m and cc; each real fixed field R_m = L^{<H_m, cc>} has degree
// N / (2 |H_m|) = odd * 2^k. The checks below are the same as in fixed_field_tower.hpp except
// that the degrees are odd * 2^k with one common odd factor.
#pragma once

#include "fixed_field_tower.hpp"
#include "relative_projection.hpp"

namespace sic::exact {

struct ScalarFixedFieldsGeneral {
  std::vector<ScalarFixedField> fields;  // increasing real degree
  std::vector<size_t> representative_field;
  long odd = 1;  // odd part of N = [L^{G_2} : K]

  ScalarFixedFieldsGeneral(const Group& group, const std::vector<long>& sig, long cc);
};

// Generators of the common kernel (inside Gal(L/K)) of a chain of characters, each of prime
// order p on the kernel of its predecessors (generalizes quadratic_annihilator_generators to odd
// p). For chi with exponents e_j = k_j * (modulus/p) on the current generators g_j and a pivot
// with k_pivot != 0 mod p: ker = < pivot^p, g_j * pivot^(-k_j / k_pivot mod p) >.
std::vector<long> prime_annihilator_generators(const Group& group, std::span<const long> characters,
                                               std::span<const long> primes);

}  // namespace sic::exact
