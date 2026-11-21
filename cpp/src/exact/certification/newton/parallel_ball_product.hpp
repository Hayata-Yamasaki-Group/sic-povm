// SPDX-License-Identifier: AGPL-3.0-or-later
// Rigorous Krawczyk products with fixed output tiles; no midpoint substitution.
#pragma once
#include <flint/acb_mat.h>

#include <chrono>
#include <cstdint>

#include "exact/certification/newton/tiled_ball_product.hpp"
#include "exact/runtime/parallel.hpp"
#include "sic/support/kernel_settings.hpp"
#include "sic/support/stop.hpp"
#include "support/resources.hpp"

namespace sic::exact {

bool parallel_ball_product(acb_mat_t output, const acb_mat_t left, const acb_mat_t right,
                           slong precision, int workers,
                           sic::support::resources::Bytes workspace_limit, bool verbose = false,
                           const BallProductHooks* hooks = nullptr);

}  // namespace sic::exact
