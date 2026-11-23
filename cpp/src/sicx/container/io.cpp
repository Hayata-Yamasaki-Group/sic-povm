// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/sicx/io.hpp"

#include "sicx/codec/block.hpp"
#include "sicx/container/reader.hpp"
#include "sicx/container/stream_writer.hpp"
#include "support/sha256.hpp"
namespace sic::sicx {
static void validate_writer_output(const std::filesystem::path& path, ResourceLimits limits) {
  Reader checked(path, limits);
}
const ResourceLimits& Reader::resource_limits() const noexcept { return state_->resource_limits(); }

Reader::Reader(const std::filesystem::path& p, ResourceLimits limits)
    : state_(std::make_unique<State>(p, limits)) {}

Reader::Reader(std::shared_ptr<sic::support::ByteSource> source, ResourceLimits limits)
    : state_(std::make_unique<State>(std::move(source), limits)) {}

const Meta& Reader::meta() const { return state_->meta(); }

const sic::support::Json& Reader::meta_json() const { return state_->meta_json(); }

const std::vector<IndexEntry>& Reader::index() const { return state_->index(); }

const std::vector<LogicalEntry>& Reader::logical() const { return state_->logical(); }

const Digest& Reader::root() const { return state_->root(); }

const Digest& Reader::exact_root() const { return state_->exact_root(); }

size_t Reader::exact_count() const { return state_->exact_count(); }

size_t Reader::meta_chunk() const { return state_->meta_chunk(); }

uint64_t Reader::ldir_offset() const { return state_->ldir_offset(); }

bool Reader::finalized() const { return state_->finalized(); }

std::optional<size_t> Reader::rays_chunk() const { return state_->rays_chunk(); }

uint64_t Reader::size() const { return state_->size(); }

uint16_t Reader::minor() const { return state_->minor(); }

const std::vector<size_t>& Reader::element_blocks(size_t id) const {
  return state_->element_blocks(id);
}

size_t Reader::leaf_owner(size_t leaf) const { return state_->leaf_owner(leaf); }

IndexEntry Reader::leaf_entry(size_t leaf) const { return state_->leaf_entry(leaf); }

size_t Reader::ball_chunk(size_t ray) const { return state_->ball_chunk(ray); }

std::optional<size_t> Reader::prec() const { return state_->prec(); }

std::optional<size_t> Reader::prov() const { return state_->prov(); }

uint64_t Reader::bytes_read() const { return state_->bytes_read(); }

void Reader::plan_chunks(std::span<const size_t> chunks) const { state_->plan_chunks(chunks); }

void Reader::bind_cancellation(std::shared_ptr<sic::support::StopToken> token) const {
  state_->bind_cancellation(std::move(token));
}

bool Reader::remote() const { return state_->remote(); }

uint64_t Reader::source_memory_reservation() const { return state_->source_memory_reservation(); }

void Reader::quiesce() const { state_->quiesce(); }

void Reader::schedule_chunks(std::span<const size_t> chunks) const {
  state_->schedule_chunks(chunks);
}

ByteBuffer Reader::chunk(size_t i) const { return state_->chunk(i); }

Reader::ElementChunk Reader::element_chunk(size_t owner, bool retain_integers) const {
  return state_->element_chunk(owner, retain_integers);
}

ByteBuffer Reader::leaf(size_t li) const { return state_->leaf(li); }

sic::support::Json Reader::json_chunk(size_t i) const { return state_->json_chunk(i); }

std::vector<BallPart> Reader::ball_parts(size_t ray) const { return state_->ball_parts(ray); }

StreamWriter::StreamWriter(const std::filesystem::path& path, StreamOptions options)
    : state_(std::make_unique<State>(path, std::move(options), &validate_writer_output)) {}

std::unique_ptr<StreamWriter> StreamWriter::reopen(const std::filesystem::path& path,
                                                   StreamOptions options,
                                                   const std::vector<IndexEntry>& exact_index,
                                                   uint64_t ldir_offset, const Digest& exact_root) {
  return std::unique_ptr<StreamWriter>(new StreamWriter(State::reopen(
      path, std::move(options), exact_index, ldir_offset, exact_root, &validate_writer_output)));
}

std::unique_ptr<StreamWriter> StreamWriter::resume_reopen(const std::filesystem::path& path,
                                                          StreamOptions options) {
  return std::unique_ptr<StreamWriter>(
      new StreamWriter(State::resume_reopen(path, std::move(options), &validate_writer_output)));
}

size_t StreamWriter::pending_replay() const { return state_->pending_replay(); }

const std::vector<IndexEntry>& StreamWriter::resumed() const { return state_->resumed(); }

const std::vector<IndexEntry>& StreamWriter::index() const { return state_->index(); }

uint64_t StreamWriter::offset() const { return state_->offset(); }

Digest StreamWriter::exact_root() const { return state_->exact_root(); }

const ByteBuffer* StreamWriter::note(const std::string& key) const { return state_->note(key); }

void StreamWriter::put_note(const std::string& key, const ByteBuffer& value) {
  state_->put_note(key, value);
}

void StreamWriter::adopt(size_t n) { state_->adopt(n); }

std::vector<IndexEntry> StreamWriter::peek_journal(const std::filesystem::path& journal,
                                                   Digest* plan, const WorkspaceAdmission& admit) {
  return State::peek_journal(journal, plan, admit);
}

ByteBuffer StreamWriter::read_chunk_at(const std::filesystem::path& file, const IndexEntry& e,
                                       const ResourceLimits& limits) {
  return State::read_chunk_at(file, e, limits);
}

void StreamWriter::abandon() { state_->abandon(); }

void StreamWriter::chunk(const std::string& type, uint32_t flags, std::span<const uint8_t> payload,
                         uint64_t object, uint64_t first, uint64_t count) {
  state_->chunk(type, flags, payload, object, first, count);
}

Digest StreamWriter::finish() { return state_->finish(); }

Reader::~Reader() = default;

Reader::Reader(Reader&&) noexcept = default;

Reader& Reader::operator=(Reader&&) noexcept = default;

StreamWriter::~StreamWriter() = default;

StreamWriter::StreamWriter(StreamWriter&&) noexcept = default;

StreamWriter& StreamWriter::operator=(StreamWriter&&) noexcept = default;

StreamWriter::StreamWriter(std::unique_ptr<State> state) : state_(std::move(state)) {}

}  // namespace sic::sicx
