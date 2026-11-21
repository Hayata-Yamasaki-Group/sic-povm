// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <string>
namespace sic::support::resources {
struct NumaPolicy {
  int mode;
  unsigned long mask;
};
void apply_numa_policy(NumaPolicy policy, const std::string& description);
std::string numa_pages();
}  // namespace sic::support::resources
