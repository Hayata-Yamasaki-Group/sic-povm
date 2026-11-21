// SPDX-License-Identifier: AGPL-3.0-or-later
// SICX 1.0 container I/O: chunk writer, PDIR table, Merkle root, footer and the
// random-access reader (docs/sicx.md, sections "Container" and "Reader rules").
#pragma once

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <fstream>
#include <functional>
#include <future>
#include <mutex>
#include <thread>

#include "sic/sicx/format.hpp"
#include "sic/support/admission.hpp"
#include "sic/support/byte_source.hpp"
#include "sic/support/durable_file.hpp"
#include "sic/support/flint_raii.hpp"
#include "sic/support/stop.hpp"
#include "sic/support/thread_group.hpp"

namespace sic::sicx {
class StreamWriter;
class Reader;

struct LogicalEntry {
  std::array<char, 4> type{};
  uint64_t element = 0, first = 0, count = 0;
  Digest sha256{};
  std::string type_name() const;
  void put(ByteBuffer& out) const;
  static LogicalEntry get(const uint8_t* p);
};
struct IndexEntry {
  std::array<char, 4> type{};
  uint32_t flags = 0, codec = 0;
  uint64_t offset = 0, length = 0, decoded_length = 0;
  uint64_t first_leaf = 0, leaf_count = 0, object = 0, first = 0, count = 0;
  Digest sha256{};
  std::string type_name() const;
  void put(ByteBuffer& out) const;
  static IndexEntry get(const uint8_t* p);
};

Digest merkle_root(const std::vector<LogicalEntry>& entries);
Digest merkle_root(const std::vector<IndexEntry>& entries);

struct LeafSpan {
  BlockHeader header;
  size_t offset, length;
  uint64_t nonzero = 0;
};
struct ParsedLeaf {
  ByteBuffer values;  // subindex varint followed by full original-basis x and y
  size_t count = 0;
};
// Parse framing without expanding coordinates. Semantic checks follow before consumption.

// ----------------------------------------------------------- stream writer
// Streaming writer (docs/sicx.md#58 section 5.4).
//  * Deterministic: the bytes are a function of the chunk sequence only.
//  * Bounded memory: one chunk at a time.
//  * Durable (default): every chunk is fdatasync'ed, then its PDIR entry is appended to
//    NAME.partial.journal and fdatasync'ed. A killed run leaves NAME.partial + journal.
//  * Resume: StreamOptions::resume re-opens them, re-hashes every journaled chunk against
//    its recorded SHA-256, truncates after the last good chunk and continues. The caller
//    replays its deterministic chunk sequence: replayed chunks are compared with the
//    recorded entries (any difference fails), or adopted unseen with adopt().
//  * finish() writes PDIR and the footer (exact-part Merkle root and full root), syncs and
//    renames NAME.partial to NAME (the rename is the commit point).
//  * Part mode writes a bare chunk sequence plus NAME.idx (an index fragment) for
//    merge_parts(); the merged file equals a single-writer run byte for byte.
//  * Fault injection (tests): SIC_FAULT_KILL_AT_BYTE=N raises SIGKILL once the data file
//    reaches N bytes (mid-chunk), SIC_FAULT_KILL_AFTER_CHUNKS=K after K durable chunks.
struct StreamOptions {
  bool durable = true;
  bool resume = false;
  ResourceLimits limits{};
  Digest plan{};      // identifies the producer input and options; must match on resume
  bool part = false;  // private bare-chunk spool
  std::function<void(const std::filesystem::path&)> before_publish;
  StreamOptions(bool d = true, bool r = false, Digest p = {}, bool bare_part = false,
                std::function<void(const std::filesystem::path&)> validate = {});
};

// Merge part files (StreamOptions::part) in order into one .sicx through `writer`.
// Every chunk is re-hashed against its part index; the result equals a single-writer run.
void append_part(StreamWriter& writer, const std::filesystem::path& part);

// ------------------------------------------------------------------ reader
// Opening reads the header, footer and PDIR, checks the layout and the Merkle
// root, then reads and verifies EXMT. Every later chunk read checks its SHA-256.

class StreamWriter {
 public:
  StreamWriter(const std::filesystem::path& path, StreamOptions options = {});
  static std::unique_ptr<StreamWriter> reopen(const std::filesystem::path& path,
                                              StreamOptions options,
                                              const std::vector<IndexEntry>& exact_index,
                                              uint64_t ldir_offset, const Digest& exact_root);
  static std::unique_ptr<StreamWriter> resume_reopen(const std::filesystem::path& path,
                                                     StreamOptions options);
  ~StreamWriter();
  size_t pending_replay() const;
  const std::vector<IndexEntry>& resumed() const;
  const std::vector<IndexEntry>& index() const;
  uint64_t offset() const;
  Digest exact_root() const;
  const ByteBuffer* note(const std::string& key) const;
  void put_note(const std::string& key, const ByteBuffer& value);
  void adopt(size_t n);
  static std::vector<IndexEntry> peek_journal(const std::filesystem::path& journal,
                                              Digest* plan = nullptr);
  static ByteBuffer read_chunk_at(const std::filesystem::path& file, const IndexEntry& e,
                                  const ResourceLimits& limits = {});
  void abandon();
  void chunk(const std::string& type, uint32_t flags, std::span<const uint8_t> payload,
             uint64_t object = 0, uint64_t first = 0, uint64_t count = 0);
  Digest finish();
  StreamWriter(StreamWriter&&) noexcept;
  StreamWriter& operator=(StreamWriter&&) noexcept;
  StreamWriter(const StreamWriter&) = delete;
  StreamWriter& operator=(const StreamWriter&) = delete;

 private:
  class State;
  std::unique_ptr<State> state_;
  explicit StreamWriter(std::unique_ptr<State> state);
};

class Reader {
 public:
  struct ElementChunk {
    std::shared_ptr<const ByteBuffer> bytes;
    std::shared_ptr<const std::vector<LeafSpan>> spans;
    std::shared_ptr<const std::vector<ParsedLeaf>> parsed;
    uint64_t first_leaf = 0;
    std::span<const uint8_t> leaf(size_t index) const;
  };
  const ResourceLimits& resource_limits() const noexcept;
  explicit Reader(const std::filesystem::path& p, ResourceLimits limits = {});
  explicit Reader(std::shared_ptr<sic::support::ByteSource> source, ResourceLimits limits = {});
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
  ElementChunk element_chunk(size_t owner, bool retain_integers = false) const;
  ByteBuffer leaf(size_t li) const;
  sic::support::Json json_chunk(size_t i) const;
  std::vector<BallPart> ball_parts(size_t ray) const;
  ~Reader();
  Reader(Reader&&) noexcept;
  Reader& operator=(Reader&&) noexcept;
  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;

 private:
  class State;
  std::unique_ptr<State> state_;
};

}  // namespace sic::sicx
