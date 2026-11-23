// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/codec/integer.hpp"
#pragma once
#include "sic/sicx/io.hpp"
namespace sic::sicx {
size_t exact_count_of(const std::vector<IndexEntry>& entries);
bool finalization_type(const std::string& t);
int type_rank(const std::string& t);
bool known_type(const std::string& t);
Digest file_tree_root(const std::vector<IndexEntry>& entries, const ByteBuffer& ldir);
ByteBuffer directory_chunk(std::string_view type, const ByteBuffer& payload);
Digest tree_hash(const std::vector<ByteBuffer>& entries);
Digest logical_hash(const LogicalEntry& e, std::span<const uint8_t> payload);

uint64_t checked_add(uint64_t a, uint64_t b);
uint64_t checked_mul(uint64_t a, uint64_t b);

// An append can use committed slack or allocate a larger array while the
// old one still lives. Bound the new capacity before either can be used.
template <class T>
size_t directory_append_capacity(const std::vector<T>& rows, size_t added,
                                 const ResourceLimits& limits, const char* stage) {
  const auto count = checked_add(rows.size(), added);
  const auto limit = std::min<uint64_t>(
      {limits.directory_entries, limits.reader_memory_bytes / sizeof(T), rows.max_size()});
  if (count > limit) fail(ErrorKind::resource, std::string(stage) + " resource limit");
  if (count <= rows.capacity()) return rows.capacity();
  return std::max(count, std::min(limit, checked_mul(2, rows.capacity())));
}

template <class T>
uint64_t directory_append_bytes(const std::vector<T>& rows, size_t capacity, size_t added) {
  return checked_mul(capacity > rows.capacity() ? capacity : added, sizeof(T));
}

}  // namespace sic::sicx
