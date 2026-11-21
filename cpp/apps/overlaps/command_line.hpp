// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/support/cli.hpp"
namespace sic::apps::overlaps {
const sic::support::cli::Parser& command_line_parser();
struct Options {
  std::string path, table;
  double tol;
  bool vectors;
};
Options parse_command_line(int argc, char** argv);
}  // namespace sic::apps::overlaps
