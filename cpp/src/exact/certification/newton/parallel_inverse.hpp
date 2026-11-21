// SPDX-License-Identifier: AGPL-3.0-or-later
// Approximate point inverse proposals; later Arb certificates remain mandatory.
#pragma once
#include <flint/acb_mat.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <system_error>

#include "exact/runtime/row_worker_team.hpp"
#include "sic/support/kernel_settings.hpp"
#include "sic/support/stop.hpp"
#include "support/resources.hpp"

namespace sic::exact {

using InverseTeamFactory = std::function<std::unique_ptr<RowWorkerTeam>(int)>;
enum class PointInverseArithmetic { arf, double_double };

int parallel_point_inverse(acb_mat_t output, const acb_mat_t input, slong precision, int workers,
                           sic::support::resources::Bytes workspace_limit, bool verbose = false,
                           const InverseTeamFactory& factory = {},
                           PointInverseArithmetic arithmetic = PointInverseArithmetic::arf);

}  // namespace sic::exact
