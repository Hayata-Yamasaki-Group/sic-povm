// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace sic::support {
// Lowercase hexadecimal in byte order, retaining leading zero bytes.
std::string hex_encode(std::span<const uint8_t> bytes);
// Lowercase hexadecimal integer, padded to at least minimum_digits (zero has one digit).
std::string hex_unsigned(uint64_t value, unsigned minimum_digits = 1);
}  // namespace sic::support
