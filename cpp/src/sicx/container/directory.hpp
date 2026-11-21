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

}  // namespace sic::sicx
