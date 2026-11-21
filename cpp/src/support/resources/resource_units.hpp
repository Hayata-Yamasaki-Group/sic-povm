// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdint>

namespace sic::support::resources {
// Private unit vocabulary; representation and checked arithmetic stay unchanged.
using ByteCount = std::uint64_t;
using Count = std::uint64_t;
using BitCount = std::uint64_t;
using Microseconds = std::uint64_t;
}  // namespace sic::support::resources
