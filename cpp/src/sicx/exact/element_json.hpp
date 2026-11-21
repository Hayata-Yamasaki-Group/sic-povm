// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sicx/exact/tower.hpp"
namespace sic::sicx::exact {
std::string fmpz_dec(const fmpz_t z);
std::string fmpq_dec(const fmpq_t q);

sic::support::Json element_json(const Elt& x);

std::string ke_infix(const QuadraticElement& x);
std::string elt_infix(const Tower& T, const Elt& x, unsigned n);

}  // namespace sic::sicx::exact
