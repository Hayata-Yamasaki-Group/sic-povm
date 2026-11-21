// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/sicx/io.hpp"
#include "sicx/codec/block.hpp"
#include "sicx/codec/integer.hpp"
#include "sicx/io/io.hpp"
#include "support/sha256.hpp"
namespace sic::sicx {
void append_part(StreamWriter& writer, const std::filesystem::path& part) {
  const std::filesystem::path idx_path = part.string() + ".idx";
  std::ifstream in(idx_path, std::ios::binary);
  if (!in) fail(ErrorKind::io, "cannot open part index " + idx_path.string());
  ByteBuffer idx;
  try {
    idx.assign(std::istreambuf_iterator<char>(in), {});
  } catch (const std::ios_base::failure&) {
    fail(ErrorKind::io, "cannot read part index " + idx_path.string());
  }
  if (in.bad()) fail(ErrorKind::io, "cannot read part index " + idx_path.string());
  if (idx.size() < 16 + 32 || std::memcmp(idx.data(), "SICXPRT1", 8))
    fail(sic::ErrorKind::invalid_format, "invalid part index " + idx_path.string());
  const uint64_t n = get_le(idx.data() + 8, 8);
  if (n > kMaxChunks || idx.size() != checked_add(48, checked_mul(n, kIndexEntryBytes)))
    fail(sic::ErrorKind::invalid_format, "part index length");
  std::vector<IndexEntry> entries;
  for (uint64_t i = 0; i < n; ++i)
    entries.push_back(IndexEntry::get(idx.data() + 16 + i * kIndexEntryBytes));
  const Digest root = merkle_root(entries);
  if (!std::equal(root.begin(), root.end(), idx.end() - 32))
    fail(sic::ErrorKind::integrity, "part index root mismatch");
  const int fd = ::open(part.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) fail(sic::ErrorKind::io, "cannot open part " + part.string());
  ByteBuffer payload;
  uint64_t offset = 0;
  try {
    for (const auto& e : entries) {
      if (e.offset != offset)
        fail(sic::ErrorKind::invalid_format, "part chunks are not contiguous");
      uint8_t head[kChunkHeaderBytes];
      pread_exact(fd, head, kChunkHeaderBytes, e.offset);
      if (std::memcmp(head, e.type.data(), 4) || get_le(head + 4, 4) != e.flags ||
          get_le(head + 8, 8) != e.length)
        fail(sic::ErrorKind::invalid_format, "part chunk header disagrees with its index");
      payload.resize(e.length);
      if (e.length) pread_exact(fd, payload.data(), e.length, e.offset + kChunkHeaderBytes);
      if (sha256(payload.data(), payload.size()) != e.sha256)
        fail(sic::ErrorKind::integrity, "part chunk SHA-256 mismatch");
      writer.chunk(e.type_name(), e.flags, payload, e.object, e.first, e.count);
      offset = e.offset + kChunkHeaderBytes + e.length;
    }
    struct stat st {};
    if (::fstat(fd, &st)) fail(ErrorKind::io, "cannot stat part");
    if (static_cast<uint64_t>(st.st_size) != offset)
      fail(sic::ErrorKind::invalid_format, "part file has trailing bytes");
  } catch (...) {
    ::close(fd);
    throw;
  }
  ::close(fd);
}

}  // namespace sic::sicx
