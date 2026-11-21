// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/acb.h>
namespace sic::construct {
void print_arb(const arb_t value, slong digits, ulong flags);
void print_acb(const acb_t value, slong digits, ulong flags);
}  // namespace sic::construct
