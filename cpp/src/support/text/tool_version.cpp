// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/support/tool_version.hpp"

#include <iostream>
namespace sic::support::cli {
std::optional<int> version_request(int argc, char** argv, std::string_view name) {
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) != "--version") continue;
    if (argc != 2) {
      std::cerr << name << ": --version requires no other arguments\n";
      return 2;
    }
    std::cout << "{\"name\":\"" << name
              << "\",\"schema\":\"sic-tool-version/1.0\",\"source_commit\":\"" << SIC_SOURCE_COMMIT
              << "\",\"version\":\"1.0.0\"}\n";
    return 0;
  }
  return std::nullopt;
}
}  // namespace sic::support::cli
