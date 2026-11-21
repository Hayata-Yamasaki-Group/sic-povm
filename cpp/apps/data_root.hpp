// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <filesystem>
#include "sic/error.hpp"
#include <string>

namespace sic::apps {
inline constexpr const char* kDefaultCatalogue = "data/catalogue.json";
inline constexpr const char* kDefaultFileRepository = ".";
inline std::string data_root(const std::string& explicit_root) {
  namespace fs = std::filesystem;
  if (!explicit_root.empty()) return explicit_root;
  std::error_code ec;
  const auto exe = fs::read_symlink("/proc/self/exe", ec);
  if (!ec) {
    const auto root = (exe.parent_path() / SIC_DATA_ROOT_FROM_BINDIR).lexically_normal();
    if (fs::is_regular_file(root / kDefaultCatalogue, ec)) return root.string();
  }
  ec.clear();
  if (fs::is_regular_file(kDefaultCatalogue, ec)) return ".";
  throw sic::Error(sic::ErrorKind::io, "cannot find data catalogue; specify --repo DIR");
}
}  // namespace sic::apps
