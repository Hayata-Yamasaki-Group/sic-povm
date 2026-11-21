// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/core/interchange.hpp"
namespace sic::core::interchange {
std::uint32_t crc32(const void* data, std::size_t n);

namespace zip {
void p16(ByteBuffer& b, std::uint32_t x);
void p32(ByteBuffer& b, std::uint32_t x);
inline constexpr std::uint32_t kDosTime = 0,
                               kDosDate = (0u << 9) | (1u << 5) | 1u;  // 1980-01-01 00:00:00
}  // namespace zip

struct ZipMember {
  std::string name;
  ByteBuffer data;
};

inline constexpr uint64_t kZipProfileMaximumBytes = 0x7fffffff;
inline constexpr uint64_t kZipProfileMaximumMembers = 65535;
// ZIP64 is deliberately refused; split large bundles or use HDF5. Preflight
// the complete archive before allocating output or encoding NPY members.
void admit_zip_profile(uint64_t members, uint64_t bytes);
// The deterministic JSON comment starts with its document format identity.
void require_npz_comment_identity(const std::string& comment, const std::string& what);
ByteBuffer zip_store_encode(const std::vector<ZipMember>& members, const std::string& comment = "");

// Strict reader for exactly the profile written above (store only, contiguous layout, no extras).
// Every member's CRC-32 is verified, and local and central headers must agree. Anything else
// throws.
std::vector<ZipMember> zip_store_decode(const ByteBuffer& in, std::string* comment = nullptr,
                                        const std::string& what = "zip");

// One .npz per dimension: member "<ray id>.npy" per ray, sorted by ray id (byte order), plus a JSON
// archive comment.

}  // namespace sic::core::interchange
