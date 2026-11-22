// SPDX-License-Identifier: AGPL-3.0-or-later
#include "command.hpp"
#include "command_line.hpp"
#include "sic/support/tool_version.hpp"

int main(int argc, char** argv) {
  sic::support::cli::prepare_stdout();
  if (const auto status = sic::support::cli::version_request(argc, argv, "sic-inspect"))
    return *status;
  try {
    const int status =
        sic::apps::inspect::command(sic::apps::inspect::parse_command_line(argc, argv));
    return sic::support::cli::finish_stdout(status, "sic-inspect");
  } catch (const std::exception& error) {
    return sic::support::cli::report(sic::apps::inspect::command_line_parser(), error,
                                     "sic-inspect");
  }
}
