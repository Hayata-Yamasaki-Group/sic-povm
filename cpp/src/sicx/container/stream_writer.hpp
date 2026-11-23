// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "sic/sicx/io.hpp"
#include "sicx/codec/block.hpp"
#include "sicx/container/directory.hpp"
#include "sicx/container/leaf_index.hpp"
namespace sic::sicx {
class StreamWriter::State {
  static constexpr std::array<uint8_t, 8> kJournalMagic = {'S', 'I', 'C', 'X', 'J', 'R', 'N', '1'};
  static constexpr std::array<uint8_t, 8> kPartMagic = {'S', 'I', 'C', 'X', 'P', 'R', 'T', '1'};
  std::filesystem::path path_, partial_, journal_;
  StreamOptions options_;
  void (*validate_output_)(const std::filesystem::path&, ResourceLimits) = nullptr;
  int fd_ = -1, jfd_ = -1;
  uint64_t offset_ = 0, base_ = 0, journal_size_ = 0;
  std::vector<IndexEntry> index_, replay_;
  std::vector<LogicalEntry> logical_;
  std::map<std::string, ByteBuffer> notes_;
  bool finished_ = false, abandoned_ = false;
#ifdef SIC_ENABLE_TEST_HOOKS
  uint64_t kill_at_byte_ = 0, kill_after_chunks_ = 0, durable_chunks_ = 0;
#endif

  void write_data(const uint8_t* data, size_t size);
  void reserve_directories(size_t new_leaves);
  ResourceLimits remaining_logical_limits() const;
  static ByteBuffer record(uint8_t kind, const ByteBuffer& payload);
  void journal(uint8_t kind, const ByteBuffer& payload);
  static ByteBuffer note_payload(const std::string& key, const ByteBuffer& value);
  void write_header();
  void create_journal(const std::vector<IndexEntry>& entries,
                      const ByteBuffer* exact_note = nullptr);
  // Re-open NAME.partial + journal: verify every journaled chunk, cut after the last good one.
  enum class ResumeName { partial, original, both };
  void resume_existing(ResumeName name = ResumeName::partial);

 public:
  State(const std::filesystem::path& path, StreamOptions options,
        void (*validate_output)(const std::filesystem::path&, ResourceLimits));

  // Reopen a finished, not yet finalized file to append finalization chunks (two-stage
  // completion). The exact part (every chunk, PDIR entry and the exact Merkle root) is kept;
  // only PDIR and the footer are rewritten. Crash-safe: the journal is created first, then
  // the file is renamed to NAME.partial and cut at its PDIR; rerunning with resume continues.
  static std::unique_ptr<State> reopen(const std::filesystem::path& path, StreamOptions options,
                                       const std::vector<IndexEntry>& exact_index,
                                       uint64_t ldir_offset, const Digest& exact_root,
                                       void (*validate_output)(const std::filesystem::path&,
                                                               ResourceLimits));
  // Resume an interrupted finalization (NAME.partial + journal, or a journal whose rename did not
  // happen).
  static std::unique_ptr<State> resume_reopen(const std::filesystem::path& path,
                                              StreamOptions options,
                                              void (*validate_output)(const std::filesystem::path&,
                                                                      ResourceLimits));

  ~State();
  State(const State&) = delete;
  State& operator=(const State&) = delete;

  // Chunks recovered on resume that the caller has not yet replayed or adopted.
  size_t pending_replay() const;
  const std::vector<IndexEntry>& resumed() const;
  const std::vector<IndexEntry>& index() const;
  uint64_t offset() const;
  Digest exact_root() const;
  // Durable key/value notes (e.g. per-element metadata for EXMT), recovered on resume.
  const ByteBuffer* note(const std::string& key) const;
  void put_note(const std::string& key, const ByteBuffer& value);
  // Accept the next n recovered chunks without regenerating them (they were re-hashed at open).
  void adopt(size_t n);
  // Read-only view of a journal: the chunk entries it records (stops at a torn record).
  static std::vector<IndexEntry> peek_journal(const std::filesystem::path& journal, Digest* plan,
                                              const WorkspaceAdmission& admit);
  // Reads one chunk of a file at its PDIR entry, checking the header and SHA-256.
  static ByteBuffer read_chunk_at(const std::filesystem::path& file, const IndexEntry& e,
                                  const ResourceLimits& limits = {});
  // Abandon: an unfinished durable output is deleted instead of kept for resume.
  void abandon();

  void chunk(const std::string& type, uint32_t flags, std::span<const uint8_t> payload,
             uint64_t object = 0, uint64_t first = 0, uint64_t count = 0);

  // Writes PDIR and the footer (or, in part mode, NAME.idx), syncs and renames.
  // Returns the full Merkle root.
  Digest finish();

 private:
  State() = default;
};
}  // namespace sic::sicx
