// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "support/resources.hpp"

namespace sic::exact::product_tiles {
// Numerical policy: independent of workers, memory availability and scheduling.
inline constexpr long kSide = 64;
inline constexpr long kSmallMatrix = 128;
// Bounded simultaneous FLINT packing workspaces. This affects scheduling only.
inline constexpr int kMaximumActive = 7;

long count(long dimension);
sic::support::resources::Bytes scratch(long dimension, int workers,
                                       sic::support::resources::BitCount storage_bits);
}  // namespace sic::exact::product_tiles
