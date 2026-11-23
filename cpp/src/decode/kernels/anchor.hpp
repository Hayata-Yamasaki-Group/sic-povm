// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "sic/error.hpp"
#include "sic/support/ball_vectors.hpp"
#include "sic/support/flint_raii.hpp"

namespace sic::decode {
// Scan raw coordinates in order. Replace the incumbent only when the next
// lower bound for the squared modulus exceeds the incumbent upper bound.
size_t select_anchor(std::span<const acb_struct> raw, slong bits);
inline size_t select_anchor(const sic::support::ComplexBallVector& raw, slong bits) {
  return select_anchor(raw.span(), bits);
}
}  // namespace sic::decode
