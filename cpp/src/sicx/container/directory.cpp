// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/container/directory.hpp"

#include "sic/sicx/io.hpp"
#include "sicx/codec/block.hpp"
#include "support/sha256.hpp"
namespace sic::sicx {

std::string LogicalEntry::type_name() const { return {type.data(), 4}; }

void LogicalEntry::put(ByteBuffer& out) const {
  out.insert(out.end(), type.begin(), type.end());
  put_le(out, 0, 4);
  put_le(out, element, 8);
  put_le(out, first, 8);
  put_le(out, count, 8);
  out.insert(out.end(), sha256.begin(), sha256.end());
}

LogicalEntry LogicalEntry::get(const uint8_t* p) {
  if (get_le(p + 4, 4)) fail(sic::ErrorKind::invalid_format, "LDIR reserved field");
  LogicalEntry e;
  std::memcpy(e.type.data(), p, 4);
  e.element = get_le(p + 8, 8);
  e.first = get_le(p + 16, 8);
  e.count = get_le(p + 24, 8);
  std::memcpy(e.sha256.data(), p + 32, 32);
  return e;
}

std::string IndexEntry::type_name() const { return {type.data(), 4}; }

void IndexEntry::put(ByteBuffer& out) const {
  out.insert(out.end(), type.begin(), type.end());
  put_le(out, flags, 4);
  put_le(out, offset, 8);
  put_le(out, length, 8);
  put_le(out, codec, 4);
  put_le(out, 0, 4);
  put_le(out, decoded_length, 8);
  put_le(out, first_leaf, 8);
  put_le(out, leaf_count, 8);
  put_le(out, object, 8);
  put_le(out, first, 8);
  put_le(out, count, 8);
  out.insert(out.end(), sha256.begin(), sha256.end());
}

IndexEntry IndexEntry::get(const uint8_t* p) {
  if (get_le(p + 28, 4)) fail(sic::ErrorKind::invalid_format, "PDIR reserved field");
  IndexEntry e;
  std::memcpy(e.type.data(), p, 4);
  e.flags = get_le(p + 4, 4);
  e.offset = get_le(p + 8, 8);
  e.length = get_le(p + 16, 8);
  e.codec = get_le(p + 24, 4);
  e.decoded_length = get_le(p + 32, 8);
  e.first_leaf = get_le(p + 40, 8);
  e.leaf_count = get_le(p + 48, 8);
  e.object = get_le(p + 56, 8);
  e.first = get_le(p + 64, 8);
  e.count = get_le(p + 72, 8);
  std::memcpy(e.sha256.data(), p + 80, 32);
  return e;
}

Digest logical_hash(const LogicalEntry& e, std::span<const uint8_t> payload) {
  ByteBuffer prefix{'S', 'I', 'C', 'X', '1', '-', 'L', 'E', 'A', 'F', 0};
  prefix.insert(prefix.end(), e.type.begin(), e.type.end());
  put_le(prefix, e.element, 8);
  put_le(prefix, e.first, 8);
  put_le(prefix, e.count, 8);
  put_le(prefix, payload.size(), 8);
  support::sha256::Hasher h;
  h.update(prefix.data(), prefix.size());
  h.update(payload.data(), payload.size());
  return h.finish();
}

Digest tree_hash(const std::vector<ByteBuffer>& entries) {
  std::vector<Digest> leaves;
  leaves.reserve(entries.size());
  for (const auto& e : entries) {
    support::sha256::Hasher h;
    const uint8_t tag = 0;
    h.update(&tag, 1);
    h.update(e.data(), e.size());
    leaves.push_back(h.finish());
  }
  if (leaves.empty()) return sha256(nullptr, 0);
  auto tree = [&](auto&& self, size_t b, size_t n) -> Digest {
    if (n == 1) return leaves[b];
    size_t k = 1;
    while (k <= (n - 1) / 2) k *= 2;
    const auto l = self(self, b, k), r = self(self, b + k, n - k);
    ByteBuffer node{1};
    node.insert(node.end(), l.begin(), l.end());
    node.insert(node.end(), r.begin(), r.end());
    return sha256(node.data(), node.size());
  };
  return tree(tree, 0, leaves.size());
}

ByteBuffer directory_chunk(std::string_view type, const ByteBuffer& payload) {
  ByteBuffer out(type.begin(), type.end());
  put_le(out, 1, 4);
  put_le(out, payload.size(), 8);
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

Digest file_tree_root(const std::vector<IndexEntry>& entries, const ByteBuffer& ldir) {
  std::vector<ByteBuffer> bytes(entries.size());
  for (size_t i = 0; i < entries.size(); ++i) entries[i].put(bytes[i]);
  ByteBuffer tag{'S', 'I', 'C', 'X', '1', '-', 'L', 'D', 'I', 'R', 0};
  const auto h = sha256(ldir.data(), ldir.size());
  tag.insert(tag.end(), h.begin(), h.end());
  bytes.push_back(std::move(tag));
  return tree_hash(bytes);
}

bool known_type(const std::string& t) {
  return t == "EXMT" || t == "TOWR" || t == "BODY" || t == "PROV" || t == "RAYS" || t == "BALL" ||
         t == "PREC";
}

int type_rank(const std::string& t) {
  if (t == "EXMT") return 0;
  if (t == "TOWR") return 1;
  if (t == "BODY") return 2;
  if (t == "PROV") return 3;
  if (t == "RAYS") return 4;
  if (t == "BALL") return 5;
  if (t == "PREC") return 6;
  fail(sic::ErrorKind::invalid_format, "undefined chunk type " + t);
}

bool finalization_type(const std::string& t) { return t == "RAYS" || t == "BALL" || t == "PREC"; }

size_t exact_count_of(const std::vector<IndexEntry>& entries) {
  for (size_t i = 0; i < entries.size(); ++i)
    if (finalization_type(entries[i].type_name())) return i;
  return entries.size();
}

}  // namespace sic::sicx

namespace sic::sicx {
template <class Entry>
Digest directory_merkle_root(const std::vector<Entry>& entries) {
  std::vector<ByteBuffer> bytes(entries.size());
  for (size_t i = 0; i < entries.size(); ++i) entries[i].put(bytes[i]);
  return tree_hash(bytes);
}
Digest merkle_root(const std::vector<LogicalEntry>& entries) {
  return directory_merkle_root(entries);
}
Digest merkle_root(const std::vector<IndexEntry>& entries) {
  return directory_merkle_root(entries);
}
}  // namespace sic::sicx
