// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace sic_test {
// Atomic allocation also distinguishes processes in different PID namespaces.
class TemporaryDirectory {
 public:
  std::filesystem::path path;
  explicit TemporaryDirectory(const std::string& name) {
    auto pattern = (std::filesystem::temp_directory_path() / (name + "-XXXXXX")).string();
    if (!::mkdtemp(pattern.data())) throw std::runtime_error("cannot create test directory");
    path = pattern;
  }
  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};
}  // namespace sic_test
