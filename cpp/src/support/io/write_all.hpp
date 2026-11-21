// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace sic::support {
// Complete a bounded write, optionally positioned without changing the fd offset.
// Retries EINTR and short writes; rejects zero progress. I/O failures throw
// sic::Error(io); a positioned range outside off_t throws std::overflow_error.
void write_all(int fd, std::span<const uint8_t> bytes,
               std::optional<uint64_t> offset = std::nullopt);
}  // namespace sic::support
