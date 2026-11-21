// SPDX-License-Identifier: AGPL-3.0-or-later
#include "command.hpp"
#include "command_line.hpp"
#include "sic/support/tool_version.hpp"

int main(int argc, char** argv) {
  if (const auto status = sic::support::cli::version_request(argc, argv, "sic-verify"))
    return *status;
  try {
    return sic::apps::verify::command(sic::apps::verify::parse_command_line(argc, argv));
  } catch (const std::exception& error) {
    return sic::support::cli::report(sic::apps::verify::command_line_parser(), error,
                                     "sic-verify: FAIL");
  }
}
