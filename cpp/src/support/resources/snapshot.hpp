// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <string>
#include <vector>

#include "support/resources/checked_bytes.hpp"
namespace sic::support::resources {
struct Snapshot {
  Bytes available = 0;  // free headroom, never includes swap
  Bytes rss = 0;
  Bytes virtual_size = 0;
  Count cpus = 1;  // logical admission allowance, capped by process limits
  bool memory_known = false;
  std::vector<std::string> constraints;
};
}  // namespace sic::support::resources
