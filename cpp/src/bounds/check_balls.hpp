// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "sic/bounds/overlap_bounds.hpp"
#include "sic/support/cli.hpp"
namespace sic::bounds {
inline constexpr BallVerificationLimits kDefaultBallLimits{};
inline constexpr long kBallAccuracyHeadroom = 40;
inline constexpr long kDefaultBallAccuracyBits = 100;
inline constexpr long kDefaultBallWorkingBits = kDefaultBallLimits.working_bits;
int check_balls(const support::cli::Arguments& args);
}  // namespace sic::bounds
