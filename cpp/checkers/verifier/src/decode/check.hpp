// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../model/input_model.hpp"
namespace sic_check::verifier {

// Independent decode consistency checks do not reconstruct the finalizer's source
// enclosures and therefore do not certify containment in an arbitrary stored BALL.
bool same_binary64(double a, double b);

double rounded_binary64(const arb_t x, slong prec);

string acbstr(const acb_t x);

bool rad_small(const acb_t x, slong bits);

void decode_check(Container& C, const Opts& O);
}  // namespace sic_check::verifier
