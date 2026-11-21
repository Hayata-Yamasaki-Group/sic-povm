// SPDX-License-Identifier: AGPL-3.0-or-later
#include "search/stabilizer.hpp"
#pragma once
#include "sic/support/cli.hpp"
namespace sic::apps::stabilizer {
const sic::support::cli::Parser& command_line_parser();
struct Options {
  std::string path, root, out_path, catalogue;
  int dim;
  sic::search::StabilizerOptions o;
};
Options parse_command_line(int argc, char** argv);
}  // namespace sic::apps::stabilizer
