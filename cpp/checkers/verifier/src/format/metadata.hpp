// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::verifier {

string ekey(const string& kind, Int64 a, Int64 b = -1, Int64 c = -1);

bool is_prime(Uint64 p);

Uint64 squarefree_part_signed_pos(Uint64 n);

void parse_rays(Meta& M, const Jv& balls, const Jv& rays);

void parse_exmt(Meta& M, const Uint8* b, size_t n);
}  // namespace sic_check::verifier
