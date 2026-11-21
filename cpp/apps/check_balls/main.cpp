// SPDX-License-Identifier: AGPL-3.0-or-later
#include <flint/flint.h>

#include "bounds/check_balls.hpp"
#include "sic/support/tool_version.hpp"

namespace {
using sic::support::cli::default_text;
const sic::support::cli::Parser kParser{
    "sic-check-balls BALLS [OPTIONS]",
    {{"--maximum-dimension", "D",
      "Dimension policy (default " +
          default_text(sic::bounds::kDefaultBallLimits.maximum_dimension) + "; " +
          default_text(sic::bounds::kMinimumBallDimension) + ".." +
          default_text(sic::bounds::kMaximumBallDimension) + ").",
      1, 1},
     {"--maximum-work", "N",
      "Work score policy (default " +
          default_text(sic::bounds::kDefaultBallLimits.maximum_work_score) + ").",
      1, 1},
     {"--maximum-precision", "W",
      "Precision policy (default " +
          default_text(sic::bounds::kDefaultBallLimits.maximum_working_bits) + "; " +
          default_text(sic::bounds::kMinimumBallWorkingBits) + ".." +
          default_text(sic::kNativeMaximumPrecision) + ").",
      1, 1},
     {"--memory-mib", "M",
      "Workspace limit in MiB (default " +
          default_text(sic::bounds::kDefaultBallLimits.maximum_workspace_bytes >> 20) + ").",
      1, 1},
     {"--bits", "B",
      "Accuracy bits (default " + default_text(sic::bounds::kDefaultBallAccuracyBits) +
          "; 1..maximum-precision minus " + default_text(sic::bounds::kBallAccuracyHeadroom) +
          "; default maximum " +
          default_text(sic::bounds::kDefaultBallLimits.maximum_working_bits -
                       sic::bounds::kBallAccuracyHeadroom) +
          ").",
      1, 1},
     {"--working-bits", "W",
      "Working bits (default " + default_text(sic::bounds::kDefaultBallWorkingBits) + "; minimum " +
          default_text(sic::bounds::kMinimumBallWorkingBits) +
          ", at most maximum-precision; default ceiling " +
          default_text(sic::bounds::kDefaultBallLimits.maximum_working_bits) + ").",
      1, 1}}};
}  // namespace
int main(int argc, char** argv) {
  if (const auto status = sic::support::cli::version_request(argc, argv, "sic-check-balls"))
    return *status;
  try {
    flint_set_num_threads(1);
    return sic::bounds::check_balls(kParser.parse(argc, argv));
  } catch (const std::exception& e) {
    return sic::support::cli::report(kParser, e, "sic-check-balls");
  }
}
