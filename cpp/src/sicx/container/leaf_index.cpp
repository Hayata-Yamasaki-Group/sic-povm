// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/container/leaf_index.hpp"

#include "sic/sicx/io.hpp"
#include "sicx/codec/block.hpp"
#include "sicx/container/directory.hpp"
#include "support/sha256.hpp"
namespace sic::sicx {
namespace {
template <class Visit>
void scan_leaves(std::span<const uint8_t> payload, const ResourceLimits& limits, Visit visit) {
  Cursor in(payload.data(), payload.size());
  if (!in.remaining()) fail(ErrorKind::invalid_format, "empty element chunk");
  uint64_t leaves = 0;
  while (in.remaining()) {
    if (leaves == limits.directory_entries)
      fail(ErrorKind::resource, "logical leaf resource limit");
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
    if (length > limits.block_bytes) fail(sic::ErrorKind::resource, "logical leaf resource limit");
    visit(LeafSpan{h, static_cast<size_t>(start - payload.data()), length, nonzero});
    ++leaves;
  }
}
}  // namespace

std::vector<LeafSpan> leaf_spans(std::span<const uint8_t> payload, const ResourceLimits& limits) {
  std::vector<LeafSpan> spans;
  scan_leaves(payload, limits, [&](LeafSpan span) {
    if (spans.size() == spans.capacity())
      spans.reserve(directory_append_capacity(spans, 1, limits, "logical leaf spans"));
    spans.push_back(span);
  });
  return spans;
}

std::vector<LogicalEntry> chunk_leaves(const IndexEntry& entry, std::span<const uint8_t> bytes,
                                       const ResourceLimits& limits,
                                       const WorkspaceAdmission& admit) {
  std::vector<LogicalEntry> leaves;
  const auto append = [&](LogicalEntry leaf, std::span<const uint8_t> payload) {
    if (leaves.size() == leaves.capacity()) {
      const auto capacity = directory_append_capacity(leaves, 1, limits, "logical leaf rows");
      if (admit) admit("logical leaf rows", directory_append_bytes(leaves, capacity, 1));
      leaves.reserve(capacity);
    }
    leaf.sha256 = logical_hash(leaf, payload);
    leaves.push_back(leaf);
  };
  const auto type = entry.type_name();
  if (type == "EXMT") {
    LogicalEntry e;
    e.type = entry.type;
    e.count = 1;
    append(e, bytes);
  } else if (type == "TOWR" || type == "BODY") {
    scan_leaves(bytes, limits, [&](const LeafSpan& span) {
      LogicalEntry e;
      e.type = entry.type;
      e.element = span.header.element;
      e.first = span.header.first;
      e.count = span.header.count;
      append(e, bytes.subspan(span.offset, span.length));
    });
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
