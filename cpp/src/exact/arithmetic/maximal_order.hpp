// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "exact/arithmetic/arith.hpp"
#include "sic/error.hpp"

namespace sic::exact::reduced_order {

constexpr int kMaxPower = 64;  // search bound for epsilon = u^k

void contract(bool ok, const std::string& what, std::optional<sic::ErrorKind> kind = std::nullopt);

struct CycleUnit {
  QuadraticNumber u;  // positive cycle unit (> 1 in the embedding where tau > 1)
  int k = 0;          // epsilon = u^k
};

// The cycle unit of class c: the product of the steps (b - tau_l)^(-1) over the primitive cycle,
// continued from the class's scal recurrence (Arith::enumerate_classes).
QuadraticNumber class_cycle_unit(const Arithmetic& ar, sic::support::Index c);

// u and k with epsilon = u^k, from the primitive cycles of ar (all classes must agree).
CycleUnit cycle_unit(const Arithmetic& ar);

// ar must be Arith(s, false). Re-derive the maximal order exactly as Arith(s, true) does (without
// its list of verified dimensions), find epsilon = u^k and, for k > 1, store every cycle k times.
CycleUnit configure_arith(Arithmetic& ar);

// h d^2 / (2k): closure cap and expected |G'| (checked against the closure by the producer).
long degree(const Arithmetic& ar, int k);

// Recompute the Group's orbit labels modulo u (the library constructor used epsilon). Must run
// before generators, closure or checkpoint load. Returns the number of nonzero orbits.

}  // namespace sic::exact::reduced_order
