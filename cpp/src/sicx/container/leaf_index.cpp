// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/container/leaf_index.hpp"

#include "sic/sicx/io.hpp"
#include "sicx/codec/block.hpp"
#include "sicx/container/directory.hpp"
#include "support/sha256.hpp"
namespace sic::sicx {
std::vector<LeafSpan> leaf_spans(std::span<const uint8_t> payload, const ResourceLimits& limits) {
  Cursor in(payload.data(), payload.size());
  std::vector<LeafSpan> spans;
  while (in.remaining()) {
    const auto* start = in.at;
    BlockHeader h{in.varint(), in.varint(), in.varint()};
    if (!h.count || h.first > UINT64_MAX - h.count)
      fail(sic::ErrorKind::invalid_format, "invalid logical leaf span");
    uint64_t left = h.count;
    uint64_t nonzero = 0;
    int last = -1;
    while (left) {
      const uint64_t tag = in.varint(), n = tag >> 1;
      const int kind = tag & 1;
      if (!n || n > left || kind == last)
        fail(sic::ErrorKind::invalid_format, "noncanonical leaf runs");
      last = kind;
      left -= n;
      if (kind) {
        nonzero += n;
        if (n > in.remaining() / 2) fail(sic::ErrorKind::invalid_format, "truncated data run");
        for (uint64_t q = 0; q < n; ++q)
          for (int j = 0; j < 2; ++j) {
            const auto code = in.varint(), size = code >> 1;
            if (code == 1) fail(sic::ErrorKind::invalid_format, "invalid leaf integer");
            if (size > kMaxIntegerBytes)
              fail(ErrorKind::resource, "leaf integer exceeds reader limit");
            const auto* p = in.take(size);
            if (size && !p[size - 1]) fail(sic::ErrorKind::invalid_format, "nonminimal integer");
          }
      }
    }
    const size_t length = in.at - start;
    if (length > limits.block_bytes || spans.size() >= limits.directory_entries)
      fail(sic::ErrorKind::resource, "logical leaf resource limit");
    spans.push_back({h, static_cast<size_t>(start - payload.data()), length, nonzero});
  }
  if (spans.empty()) fail(sic::ErrorKind::invalid_format, "empty element chunk");
  return spans;
}

std::vector<LogicalEntry> chunk_leaves(const IndexEntry& entry, std::span<const uint8_t> bytes,
                                       const ResourceLimits& limits) {
  std::vector<LogicalEntry> leaves;
  const auto type = entry.type_name();
  if (type == "EXMT") {
    LogicalEntry e;
    e.type = entry.type;
    e.count = 1;
    e.sha256 = logical_hash(e, bytes);
    leaves.push_back(e);
  } else if (type == "TOWR" || type == "BODY") {
    for (const auto& span : leaf_spans(bytes, limits)) {
      LogicalEntry e;
      e.type = entry.type;
      e.element = span.header.element;
      e.first = span.header.first;
      e.count = span.header.count;
      e.sha256 = logical_hash(e, bytes.subspan(span.offset, span.length));
      leaves.push_back(e);
    }
  }
  return leaves;
}

}  // namespace sic::sicx

namespace sic::sicx {
std::span<const uint8_t> Reader::ElementChunk::leaf(size_t index) const {
  if (index < first_leaf || index - first_leaf >= spans->size())
    fail(sic::ErrorKind::invalid_argument, "leaf outside physical unit");
  const auto& span = spans->at(index - first_leaf);
  return std::span<const uint8_t>(*bytes).subspan(span.offset, span.length);
}
}  // namespace sic::sicx
