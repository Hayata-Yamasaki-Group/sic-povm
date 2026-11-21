// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/support/cli.hpp"
namespace sic::apps::identify {
const sic::support::cli::Parser& command_line_parser();
struct Options {
  std::string path, root, catalogue;
  double z_tol, entry_error, sic_tol;
  int moments;
  bool anti;
};
Options parse_command_line(int argc, char** argv);
}  // namespace sic::apps::identify
