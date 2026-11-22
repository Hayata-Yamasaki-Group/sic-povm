// SPDX-License-Identifier: AGPL-3.0-or-later
#include <csignal>
#include <cstdio>

#include "src/cli/command_line.hpp"

int main(int argc, char** argv) {
  std::signal(SIGPIPE, SIG_IGN);
  const int status = sic_check::norm::run_command(argc, argv);
  if (std::fflush(stdout) != 0 || std::ferror(stdout)) {
    std::fputs("sic-certify-norm: [io] stdout write failed\n", stderr);
    return 1;
  }
  return status;
}
