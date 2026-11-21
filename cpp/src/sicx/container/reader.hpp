// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/sicx/io.hpp"
#include "sicx/codec/block.hpp"
#include "sicx/container/directory.hpp"
#include "sicx/container/leaf_index.hpp"
namespace sic::sicx {
class Reader::State {
  std::shared_ptr<sic::support::ByteSource> source_;
  uint64_t size_ = 0, ldir_offset_ = 0;
  std::vector<IndexEntry> index_;
  std::vector<LogicalEntry> logical_;
  std::vector<size_t> owners_;
  Digest root_{}, exact_root_{};
  Meta meta_;
  sic::support::Json meta_json_;
  std::vector<std::vector<size_t>> element_blocks_;
  std::vector<size_t> ball_chunks_;
  std::optional<size_t> rays_chunk_, prec_, prov_;
  mutable std::mutex stats_mutex_, cache_mutex_;
  mutable uint64_t bytes_read_ = 0, cache_bytes_ = 0;
  struct Cached {
    std::shared_ptr<const ByteBuffer> bytes;
    std::vector<LeafSpan> spans;
    std::vector<ParsedLeaf> parsed;
    uint64_t retained = 0;
  };
  // Cache limits include each unit's payload and framing metadata. Active
  // caller-owned leaf copies remain part of the decoder admission window.
  mutable std::map<size_t, std::shared_ptr<const Cached>> cache_;
  mutable std::map<size_t, std::shared_future<std::shared_ptr<const Cached>>> loading_;
  // Verification workers may cover many physical units at once; their parent
  // decoder tasks separately admit the full payload/framing footprint.
  mutable uint64_t loading_bytes_ = 0;
  mutable std::condition_variable cache_space_;

  void read(uint8_t* p, size_t n, uint64_t offset) const;
  ResourceLimits limits_;
  ByteBuffer read_directory(std::string_view type, uint64_t offset, uint64_t length, size_t width);
  void validate_coverage();
  std::shared_ptr<const ByteBuffer> verified_chunk(size_t i,
                                                   std::vector<ParsedLeaf>* parsed = nullptr,
                                                   uint64_t* parsed_bytes = nullptr) const;

 public:
  // The State owns an immutable policy copy; this view lives with the State.
  const ResourceLimits& resource_limits() const noexcept;
  explicit State(const std::filesystem::path& p, ResourceLimits limits = {});
  explicit State(std::shared_ptr<sic::support::ByteSource> source, ResourceLimits limits = {});
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  const Meta& meta() const;
  const sic::support::Json& meta_json() const;
  const std::vector<IndexEntry>& index() const;
  const std::vector<LogicalEntry>& logical() const;
  const Digest& root() const;
  const Digest& exact_root() const;
  size_t exact_count() const;
  size_t meta_chunk() const;
  uint64_t ldir_offset() const;
  bool finalized() const;
  std::optional<size_t> rays_chunk() const;
  uint64_t size() const;
  uint16_t minor() const;
  const std::vector<size_t>& element_blocks(size_t id) const;
  size_t leaf_owner(size_t leaf) const;
  IndexEntry leaf_entry(size_t leaf) const;
  size_t ball_chunk(size_t ray) const;
  std::optional<size_t> prec() const;
  std::optional<size_t> prov() const;
  uint64_t bytes_read() const;
  void plan_chunks(std::span<const size_t> chunks) const;
  void bind_cancellation(std::shared_ptr<sic::support::StopToken> token) const;
  bool remote() const;
  uint64_t source_memory_reservation() const;
  void quiesce() const;
  void schedule_chunks(std::span<const size_t> chunks) const;
  ByteBuffer chunk(size_t i) const;

 private:
  std::shared_ptr<const Cached> load_element_chunk(size_t owner, bool retain_integers) const;

 public:
  // A stable, fully verified physical unit. The aliased spans share the same
  // immutable cache lifetime; no leaf is exposed before whole-unit checks pass.
  using ElementChunk = Reader::ElementChunk;

  ElementChunk element_chunk(size_t owner, bool retain_integers = false) const;
  ByteBuffer leaf(size_t li) const;
  sic::support::Json json_chunk(size_t i) const;
  std::vector<BallPart> ball_parts(size_t ray) const;
};
}  // namespace sic::sicx
