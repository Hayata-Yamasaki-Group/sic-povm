// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::norm {

void canonical_ball(arb_t out, const std::string& s);

void ball_part_arb(arb_t out, const BallPart& b);

std::vector<uint64_t> words_add(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b,
                                const std::vector<uint64_t>& mod);

int64_t base_radicand(uint64_t d);

std::vector<uint64_t> divisor_positions(uint64_t d);

uint64_t power_mod(uint64_t a, uint64_t n, uint64_t modulus);

std::vector<UnitFactor> unit_factors(uint64_t d);
}  // namespace sic_check::norm
