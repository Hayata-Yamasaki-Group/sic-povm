// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "support/resources.hpp"

namespace sic::sicx {
using SmithWord = std::vector<long>;

struct RayList {
  uint64_t dimension = 0;
  std::string source, exact_root;
  std::vector<long> smith;
  std::vector<SmithWord> words;
};

std::string bound_ray_list_json(const RayList& list);

// Selector entries may omit the binding fields. Finalization requires them.
RayList read_ray_list(const std::filesystem::path& path,
                      const sic::support::resources::Budget& budget, bool allow_entry = false,
                      std::string* digest = nullptr);
std::string ray_entry_json(const RayList& list);
struct WrittenRayList {
  uint64_t bytes;
  std::string sha256;
};
WrittenRayList write_ray_list(const std::filesystem::path& path, const RayList& list,
                              const sic::support::resources::Budget& budget, bool resume = false);
}  // namespace sic::sicx
