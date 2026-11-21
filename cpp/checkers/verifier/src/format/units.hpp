// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::verifier {

Uint64 unit_power(Uint64 a, Uint64 n, Uint64 modulus);

vector<Uint64> divisor_positions(Uint64 d);

vector<UnitGenerator> unit_generators(Uint64 d);

vector<Uint64> unit_exponents(Uint64 u, const vector<UnitGenerator>& generators);
}  // namespace sic_check::verifier
