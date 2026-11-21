// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/core/interchange.hpp"
namespace sic::core::interchange {
std::vector<std::uint16_t> utf8_to_utf16(const std::string& s);

void append_utf8(std::string& out, std::uint32_t cp);

std::string utf16_to_utf8(const std::vector<std::uint16_t>& u);

namespace mat5 {
inline constexpr uint64_t kMaximumElementBytes = UINT32_MAX;
inline constexpr uint64_t kMaximumDimension = INT32_MAX;
uint64_t padded_element(uint64_t n);
void admit_matrix(uint64_t rows, uint64_t cols, uint64_t name_bytes, uint64_t bytes, bool complex);
enum : std::uint32_t {
  mi_int8 = 1,
  mi_uint16 = 4,
  mi_int32 = 5,
  mi_uint32 = 6,
  mi_double = 9,
  mi_matrix = 14
};
enum : std::uint32_t { mx_char_class = 4, mx_double_class = 6 };

void put32(ByteBuffer& b, std::uint32_t x);
void pad8(ByteBuffer& b);
// Full (non-packed) data element: tag(type, nbytes) + data + padding to 8 bytes.
void element(ByteBuffer& b, std::uint32_t type, const void* data, std::size_t n);
// Matrix element with flags, dims, name and payload sub-elements.
void matrix(ByteBuffer& out, const std::string& name, std::uint32_t cls, bool complex,
            uint64_t rows, uint64_t cols, std::uint32_t dtype, const void* re, const void* im,
            std::size_t bytes);
}  // namespace mat5

}  // namespace sic::core::interchange
