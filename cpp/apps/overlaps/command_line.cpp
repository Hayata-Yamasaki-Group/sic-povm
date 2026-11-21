// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cmath>
#include "command_line.hpp"
namespace sic::apps::overlaps {
namespace {
using sic::support::cli::default_text;
constexpr double kDefaultTolerance = 1e-10;
}  // namespace

const sic::support::cli::Parser& command_line_parser() {
  static const sic::support::cli::Parser parser{
      "sic-overlaps FILE [OPTIONS]",
      {
          {"--tol", "T",
           "Nonnegative finite tolerance (default " + default_text(kDefaultTolerance) + ").", 1, 1,
           false},
          {"--vectors", "", "Stream all frame vectors.", 0, 0, false},
          {"--table", "OUT.bin", "Write overlaps as complex128.", 1, 1, false},
      }};
  return parser;
}
Options parse_command_line(int argc, char** argv) {
  const auto args = command_line_parser().parse(argc, argv);

  args.positionals(1, 1);
  const std::string path = args.pos[0], table = args.get("--table");
  const double tol = args.numeric<double>("--tol", kDefaultTolerance);
  const bool vectors = args.has("--vectors");

  return {path, table, tol, vectors};
}
}  // namespace sic::apps::overlaps
