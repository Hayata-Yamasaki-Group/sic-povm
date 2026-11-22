// SPDX-License-Identifier: AGPL-3.0-or-later
#include <csignal>
#include <cstdio>
#include <iostream>

#include "command_line.hpp"

int main(int argc, char** argv) {
  std::signal(SIGPIPE, SIG_IGN);
  const int status = sic_check::stabilizer::run_command(argc, argv);
  std::cout.flush();
  if (std::fflush(stdout) != 0 || std::ferror(stdout) || !std::cout) {
    std::fputs("sic-certify-stabilizer: [io] stdout write failed\n", stderr);
    return 1;
  }
  return status;
}
