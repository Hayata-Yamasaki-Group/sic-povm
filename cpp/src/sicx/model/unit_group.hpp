// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/sicx/format.hpp"
namespace sic::sicx {
int64_t squarefree_part(int64_t n);
int64_t field_radicand(uint64_t d);
std::vector<uint64_t> divisor_positions(uint64_t d);
}  // namespace sic::sicx
