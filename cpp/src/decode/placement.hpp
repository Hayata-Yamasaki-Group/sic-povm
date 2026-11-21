// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <filesystem>

#include "sic/decode/placement.hpp"

namespace sic::decode {
// One selected worker per assigned physical core, including owner zero. Without
// pinning, return a topology-derived concurrency bound over the allowed mask.
// Core identity and conservative topology fallback share the resource owner.
// Process allowance limits defaults at their callers, never this explicit team.
std::vector<unsigned> physical_workers(
    const Placement& placement, unsigned workers,
    const std::filesystem::path& sysfs_root = "/sys/devices/system/cpu");
}  // namespace sic::decode
