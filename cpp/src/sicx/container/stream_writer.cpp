// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sicx/container/stream_writer.hpp"

#include "sicx/codec/integer.hpp"
#include "sicx/container/directory.hpp"
#include "sicx/container/leaf_index.hpp"
#include "sicx/io/io.hpp"

namespace {
struct ChunkDescriptor {
  int value;
  explicit ChunkDescriptor(int fd) noexcept : value(fd) {}
  ~ChunkDescriptor() { close(); }
  ChunkDescriptor(const ChunkDescriptor&) = delete;
  ChunkDescriptor& operator=(const ChunkDescriptor&) = delete;
  void close() noexcept {
    if (value >= 0) ::close(std::exchange(value, -1));
  }
};
}  // namespace

namespace sic::sicx {
void StreamWriter::State::write_data(const uint8_t* data, size_t size) {
#ifdef SIC_ENABLE_TEST_HOOKS
  if (kill_at_byte_ && offset_ < kill_at_byte_ && offset_ + size >= kill_at_byte_) {
    pwrite_all(fd_, data, static_cast<size_t>(kill_at_byte_ - offset_), offset_);
    ::raise(SIGKILL);
  }
#endif
  pwrite_all(fd_, data, size, offset_);
  offset_ = checked_add(offset_, size);
}
}  // namespace sic::sicx

namespace sic::sicx {
ByteBuffer StreamWriter::State::record(uint8_t kind, const ByteBuffer& payload) {
  if (payload.size() > UINT32_MAX) fail(sic::ErrorKind::resource, "journal record too large");
  ByteBuffer r{kind};
  put_le(r, payload.size(), 4);
  r.insert(r.end(), payload.begin(), payload.end());
  const Digest h = sha256(r.data(), r.size());
  r.insert(r.end(), h.begin(), h.begin() + 8);
  return r;
}
}  // namespace sic::sicx

namespace sic::sicx {
void StreamWriter::State::journal(uint8_t kind, const ByteBuffer& payload) {
  if (!options_.durable) return;
  const ByteBuffer r = record(kind, payload);
  pwrite_all(jfd_, r.data(), r.size(), journal_size_);
  journal_size_ += r.size();
  sync_fd(jfd_, "journal");
}
}  // namespace sic::sicx

namespace sic::sicx {
ByteBuffer StreamWriter::State::note_payload(const std::string& key, const ByteBuffer& value) {
  if (key.size() > UINT32_MAX) fail(sic::ErrorKind::resource, "journal key too large");
  ByteBuffer p;
  put_le(p, key.size(), 4);
  p.insert(p.end(), key.begin(), key.end());
  p.insert(p.end(), value.begin(), value.end());
  return p;
}
}  // namespace sic::sicx

namespace sic::sicx {
void StreamWriter::State::write_header() {
  if (options_.part) return;
  ByteBuffer header(kMagic.begin(), kMagic.end());
  put_le(header, kMajor, 2);
  put_le(header, kMinor, 2);
  put_le(header, 0, 4);
  write_data(header.data(), header.size());
}
}  // namespace sic::sicx

namespace sic::sicx {
void StreamWriter::State::create_journal(const std::vector<IndexEntry>& entries,
                                         const ByteBuffer* exact_note) {
  // A finalization journal becomes visible only after the complete immutable
  // prefix and exact-root note have reached stable storage.
  std::string pattern = journal_.string() + ".creating.XXXXXX";
  std::vector<char> name(pattern.begin(), pattern.end());
  name.push_back(0);
  jfd_ = ::mkstemp(name.data());
  struct Unpublished {
    const char* name;
    ~Unpublished() {
      if (name) ::unlink(name);
    }
  } cleanup{jfd_ >= 0 ? name.data() : nullptr};
  const std::filesystem::path unpublished(name.data());
  if (jfd_ >= 0 && ::fcntl(jfd_, F_SETFD, FD_CLOEXEC))
    fail(sic::ErrorKind::io, "journal close-on-exec failed");
  if (jfd_ < 0) fail(sic::ErrorKind::io, "cannot create " + journal_.string());
  ByteBuffer head(kJournalMagic.begin(), kJournalMagic.end());
  head.insert(head.end(), options_.plan.begin(), options_.plan.end());
  put_le(head, base_, 8);
  put_le(head, options_.part ? 1 : 0, 8);
  pwrite_all(jfd_, head.data(), head.size(), 0);
  journal_size_ = head.size();
  for (const auto& e : entries) {
    ByteBuffer p;
    e.put(p);
    const ByteBuffer r = record(1, p);
    pwrite_all(jfd_, r.data(), r.size(), journal_size_);
    journal_size_ += r.size();
  }
  if (exact_note) {
    // Written with the entries in one synced write: no crash window without the note.
    notes_["exact part"] = *exact_note;
    const ByteBuffer r = record(2, note_payload("exact part", *exact_note));
    pwrite_all(jfd_, r.data(), r.size(), journal_size_);
    journal_size_ += r.size();
  }
  sync_fd(jfd_, "journal");
  if (sic::sicx::filesystem_io([&] { return std::filesystem::exists(journal_); }))
    fail(sic::ErrorKind::io, "journal appeared during creation");
  sic::sicx::rename_file(unpublished, journal_, "journal");
  cleanup.name = nullptr;
  sync_dir(journal_);
}
}  // namespace sic::sicx

namespace sic::sicx {
void StreamWriter::State::resume_existing() {
  jfd_ = ::open(journal_.c_str(), O_RDWR | O_CLOEXEC);
  if (jfd_ < 0) fail(sic::ErrorKind::io, "resume: no journal " + journal_.string());
  fd_ = ::open(partial_.c_str(), O_RDWR | O_CLOEXEC);
  if (fd_ < 0) fail(sic::ErrorKind::io, "resume: cannot open " + partial_.string());
  struct stat js {
  }, fs{};
  if (::fstat(jfd_, &js) || ::fstat(fd_, &fs)) fail(sic::ErrorKind::io, "resume: stat failed");
  ByteBuffer j(static_cast<size_t>(js.st_size));
  if (!j.empty()) pread_exact(jfd_, j.data(), j.size(), 0);
  if (j.size() < 56 || !std::equal(kJournalMagic.begin(), kJournalMagic.end(), j.begin()))
    fail(sic::ErrorKind::invalid_format, "resume: invalid journal header");
  if (!std::equal(options_.plan.begin(), options_.plan.end(), j.begin() + 8))
    fail(sic::ErrorKind::integrity,
         "resume: the partial file belongs to a different input or option set (plan digest "
         "differs)");
  base_ = get_le(j.data() + 40, 8);
  const auto part_mode = get_le(j.data() + 48, 8);
  if (part_mode > 1) fail(ErrorKind::invalid_format, "resume: invalid part mode");
  if (part_mode != (options_.part ? 1u : 0u))
    fail(ErrorKind::integrity, "resume: part mode differs");
  if (!options_.part) {
    uint8_t header[kHeaderBytes];
    pread_exact(fd_, header, kHeaderBytes, 0);
    if (!std::equal(kMagic.begin(), kMagic.end(), header) || get_le(header + 8, 2) != kMajor ||
        get_le(header + 10, 2) != kMinor || get_le(header + 12, 4) || base_ != kHeaderBytes)
      fail(sic::ErrorKind::invalid_format, "resume: invalid partial header");
  } else if (base_ != 0)
    fail(sic::ErrorKind::invalid_format, "resume: invalid part base");
  size_t at = 56, good_journal = 56;
  uint64_t offset = base_;
  const uint64_t file_size = static_cast<uint64_t>(fs.st_size);
  ByteBuffer payload;
  while (at + 13 <= j.size()) {
    const uint8_t kind = j[at];
    const uint64_t len = get_le(j.data() + at + 1, 4);
    if (at + 5 + len + 8 > j.size()) break;  // torn record
    const Digest h = sha256(j.data() + at, 5 + len);
    if (!std::equal(h.begin(), h.begin() + 8,
                    j.begin() + static_cast<std::ptrdiff_t>(at + 5 + len)))
      break;
    const uint8_t* p = j.data() + at + 5;
    if (kind == 1) {
      if (len != kIndexEntryBytes)
        fail(sic::ErrorKind::invalid_format, "resume: invalid journal entry");
      const auto e = IndexEntry::get(p);
      if (!known_type(e.type_name()) || e.codec || e.decoded_length != e.length)
        fail(sic::ErrorKind::invalid_format, "resume: invalid retained chunk");
      admit_chunk(e.type_name(), e.length, options_.limits);
      if (e.offset != offset ||
          checked_add(checked_add(e.offset, kChunkHeaderBytes), e.length) > file_size)
        break;
      uint8_t head[kChunkHeaderBytes];
      pread_exact(fd_, head, kChunkHeaderBytes, e.offset);
      if (std::memcmp(head, e.type.data(), 4) || get_le(head + 4, 4) != e.flags ||
          get_le(head + 8, 8) != e.length)
        break;
      payload.resize(e.length);
      if (e.length) pread_exact(fd_, payload.data(), e.length, e.offset + kChunkHeaderBytes);
      if (sha256(payload.data(), payload.size()) != e.sha256) break;
      replay_.push_back(e);
      offset = e.offset + kChunkHeaderBytes + e.length;
    } else if (kind == 2) {
      if (len < 4) fail(sic::ErrorKind::invalid_format, "resume: invalid journal note");
      const uint64_t klen = get_le(p, 4);
      if (klen + 4 > len) fail(sic::ErrorKind::invalid_format, "resume: invalid journal note");
      notes_[std::string(reinterpret_cast<const char*>(p + 4), klen)] =
          ByteBuffer(p + 4 + klen, p + len);
    } else
      fail(sic::ErrorKind::invalid_format, "resume: unknown journal record");
    at += 5 + len + 8;
    good_journal = at;
  }
  if (::ftruncate(fd_, static_cast<off_t>(offset)) ||
      ::ftruncate(jfd_, static_cast<off_t>(good_journal)))
    fail(sic::ErrorKind::io, "resume: truncate failed");
  sync_fd(fd_, "partial");
  sync_fd(jfd_, "journal");
  offset_ = base_;
  journal_size_ = good_journal;
}
}  // namespace sic::sicx

namespace sic::sicx {
StreamWriter::State::State(const std::filesystem::path& path, StreamOptions options,
                           void (*validate_output)(const std::filesystem::path&, ResourceLimits))
    : path_(path),
      partial_(path.string() + ".partial"),
      journal_(path.string() + ".partial.journal"),
      options_(options),
      validate_output_(validate_output) {
  try {
#ifdef SIC_ENABLE_TEST_HOOKS
    kill_at_byte_ = env_u64("SIC_FAULT_KILL_AT_BYTE");
    kill_after_chunks_ = env_u64("SIC_FAULT_KILL_AFTER_CHUNKS");
#endif
    if (sic::sicx::filesystem_io([&] { return std::filesystem::exists(path_); }))
      fail(sic::ErrorKind::io, "output exists: " + path_.string());
    const bool have_partial =
        sic::sicx::filesystem_io([&] { return std::filesystem::exists(partial_); });
    if (have_partial && options_.resume && options_.durable) {
      resume_existing();
      return;
    }
    if (have_partial)
      fail(sic::ErrorKind::io,
           "partial output exists (rerun with resume to continue it): " + partial_.string());
    if (sic::sicx::filesystem_io([&] { return std::filesystem::exists(journal_); }))
      fail(sic::ErrorKind::integrity, "stale journal without a partial file: " + journal_.string());
    fd_ = ::open(partial_.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd_ < 0) fail(sic::ErrorKind::io, "cannot create " + partial_.string());
    write_header();
    base_ = offset_;
    if (options_.durable) {
      sync_fd(fd_, "partial");
      create_journal({});
    }
  } catch (...) {
    if (jfd_ >= 0) ::close(std::exchange(jfd_, -1));
    if (fd_ >= 0) ::close(std::exchange(fd_, -1));
    throw;
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
std::unique_ptr<StreamWriter::State> StreamWriter::State::reopen(
    const std::filesystem::path& path, StreamOptions options,
    const std::vector<IndexEntry>& exact_index, uint64_t ldir_offset, const Digest& exact_root,
    void (*validate_output)(const std::filesystem::path&, ResourceLimits)) {
  options.durable = true;
  options.part = false;
  auto w = std::unique_ptr<State>(new State());
  w->path_ = path;
  w->partial_ = path.string() + ".partial";
  w->journal_ = path.string() + ".partial.journal";
  w->options_ = options;
  w->validate_output_ = validate_output;
#ifdef SIC_ENABLE_TEST_HOOKS
  w->kill_at_byte_ = env_u64("SIC_FAULT_KILL_AT_BYTE");
  w->kill_after_chunks_ = env_u64("SIC_FAULT_KILL_AFTER_CHUNKS");
#endif
  if (sic::sicx::filesystem_io([&] { return std::filesystem::exists(w->partial_); }) ||
      sic::sicx::filesystem_io([&] { return std::filesystem::exists(w->journal_); }))
    fail(sic::ErrorKind::io,
         "a finalization is already in progress (rerun with resume): " + w->partial_.string());
  w->base_ = kHeaderBytes;
  {
    // Binds the exact part being finalized: a resumed finalization must recover exactly it.
    ByteBuffer note;
    put_le(note, exact_index.size(), 8);
    const Digest root = exact_root;
    note.insert(note.end(), root.begin(), root.end());
    w->create_journal(exact_index, &note);
  }
  sic::sicx::rename_file(path, w->partial_, "reopen");
  sync_dir(path);
  w->fd_ = ::open(w->partial_.c_str(), O_RDWR | O_CLOEXEC);
  if (w->fd_ < 0) fail(sic::ErrorKind::io, "cannot open " + w->partial_.string());
  if (::ftruncate(w->fd_, static_cast<off_t>(ldir_offset)))
    fail(sic::ErrorKind::io, "truncate failed");
  sync_fd(w->fd_, "partial");
  w->replay_ = exact_index;
  w->offset_ = w->base_;
  return w;
}
}  // namespace sic::sicx

namespace sic::sicx {
std::unique_ptr<StreamWriter::State> StreamWriter::State::resume_reopen(
    const std::filesystem::path& path, StreamOptions options,
    void (*validate_output)(const std::filesystem::path&, ResourceLimits)) {
  const std::filesystem::path partial = path.string() + ".partial",
                              journal = path.string() + ".partial.journal";
  if (sic::sicx::filesystem_io([&] { return std::filesystem::exists(journal); }) &&
      !sic::sicx::filesystem_io([&] { return std::filesystem::exists(partial); }) &&
      sic::sicx::filesystem_io([&] { return std::filesystem::exists(path); })) {
    sic::sicx::rename_file(path, partial,
                           "resume_reopen");  // crashed between journal creation and rename
    sync_dir(path);
  }
  options.durable = true;
  options.resume = true;
  options.part = false;
  return std::make_unique<State>(path, options, validate_output);
}
}  // namespace sic::sicx

namespace sic::sicx {
StreamWriter::State::~State() {
  if (fd_ >= 0) ::close(fd_);
  if (jfd_ >= 0) ::close(jfd_);
  if (!finished_ && (abandoned_ || !options_.durable)) {
    std::error_code ec;
    std::filesystem::remove(partial_, ec);
    std::filesystem::remove(journal_, ec);
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
size_t StreamWriter::State::pending_replay() const {
  return replay_.size() > index_.size() ? replay_.size() - index_.size() : 0;
}
}  // namespace sic::sicx

namespace sic::sicx {
const std::vector<IndexEntry>& StreamWriter::State::resumed() const { return replay_; }
}  // namespace sic::sicx

namespace sic::sicx {
const std::vector<IndexEntry>& StreamWriter::State::index() const { return index_; }
}  // namespace sic::sicx

namespace sic::sicx {
uint64_t StreamWriter::State::offset() const { return offset_; }
}  // namespace sic::sicx

namespace sic::sicx {
Digest StreamWriter::State::exact_root() const { return merkle_root(logical_); }
}  // namespace sic::sicx

namespace sic::sicx {
const ByteBuffer* StreamWriter::State::note(const std::string& key) const {
  const auto it = notes_.find(key);
  return it == notes_.end() ? nullptr : &it->second;
}
}  // namespace sic::sicx

namespace sic::sicx {
void StreamWriter::State::put_note(const std::string& key, const ByteBuffer& value) {
  const auto it = notes_.find(key);
  if (it != notes_.end()) {
    if (it->second != value)
      fail(sic::ErrorKind::integrity, "resume: note " + key + " differs from the interrupted run");
    return;
  }
  notes_[key] = value;
  journal(2, note_payload(key, value));
}
}  // namespace sic::sicx

namespace sic::sicx {
void StreamWriter::State::adopt(size_t n) {
  if (n > pending_replay())
    fail(sic::ErrorKind::invalid_argument, "adopt beyond the recovered chunks");
  for (size_t q = 0; q < n; ++q) {
    const auto& e = replay_[index_.size()];
    const auto payload = read_chunk_at(partial_, e, options_.limits);
    const auto leaves = chunk_leaves(e, payload, options_.limits);
    if (!leaves.empty() && (e.first_leaf != logical_.size() || e.leaf_count != leaves.size()))
      fail(sic::ErrorKind::invalid_format, "resume logical mapping");
    if (e.type_name() == "TOWR" || e.type_name() == "BODY") {
      uint64_t next = e.first;
      for (const auto& leaf : leaves) {
        if (leaf.element != e.object || leaf.first != next)
          fail(sic::ErrorKind::invalid_format, "resume leaf/header mapping");
        next = checked_add(next, leaf.count);
      }
      if (next - e.first != e.count)
        fail(sic::ErrorKind::invalid_format, "resume logical coordinate count");
    }
    if (leaves.size() > options_.limits.directory_entries ||
        logical_.size() > options_.limits.directory_entries - leaves.size())
      throw Error(ErrorKind::resource, "SICX: logical directory resource limit");
    logical_.insert(logical_.end(), leaves.begin(), leaves.end());
    index_.push_back(e);
    offset_ = e.offset + kChunkHeaderBytes + e.length;
  }
}
}  // namespace sic::sicx

namespace sic::sicx {
std::vector<IndexEntry> StreamWriter::State::peek_journal(const std::filesystem::path& journal,
                                                          Digest* plan) {
  std::ifstream in(journal, std::ios::binary);
  if (!in) fail(ErrorKind::io, "cannot open journal " + journal.string());
  ByteBuffer j;
  try {
    j.assign(std::istreambuf_iterator<char>(in), {});
  } catch (const std::ios_base::failure&) {
    fail(ErrorKind::io, "cannot read journal " + journal.string());
  }
  if (in.bad()) fail(ErrorKind::io, "cannot read journal " + journal.string());
  if (j.size() < 56 || !std::equal(kJournalMagic.begin(), kJournalMagic.end(), j.begin()))
    fail(sic::ErrorKind::invalid_format, "invalid journal " + journal.string());
  if (plan) std::copy(j.begin() + 8, j.begin() + 40, plan->begin());
  std::vector<IndexEntry> out;
  for (size_t at = 56; at + 13 <= j.size();) {
    const uint64_t len = get_le(j.data() + at + 1, 4);
    if (at + 5 + len + 8 > j.size()) break;
    const Digest h = sha256(j.data() + at, 5 + len);
    if (!std::equal(h.begin(), h.begin() + 8,
                    j.begin() + static_cast<std::ptrdiff_t>(at + 5 + len)))
      break;
    if (j[at] == 1 && len == kIndexEntryBytes) out.push_back(IndexEntry::get(j.data() + at + 5));
    at += 5 + len + 8;
  }
  return out;
}
}  // namespace sic::sicx

namespace sic::sicx {
ByteBuffer StreamWriter::State::read_chunk_at(const std::filesystem::path& file,
                                              const IndexEntry& e, const ResourceLimits& limits) {
  admit_chunk(e.type_name(), e.length, limits);
  ChunkDescriptor fd(::open(file.c_str(), O_RDONLY | O_CLOEXEC));
  if (fd.value < 0) fail(sic::ErrorKind::io, "cannot open " + file.string());
  if (!known_type(e.type_name()) || e.codec || e.length != e.decoded_length) {
    fd.close();
    fail(sic::ErrorKind::invalid_format, "invalid journal chunk");
  }
  ByteBuffer payload(e.length);
  uint8_t head[kChunkHeaderBytes];
  try {
    pread_exact(fd.value, head, kChunkHeaderBytes, e.offset);
    if (e.length) pread_exact(fd.value, payload.data(), e.length, e.offset + kChunkHeaderBytes);
  } catch (...) {
    fd.close();
    throw;
  }
  fd.close();
  if (std::memcmp(head, e.type.data(), 4) || get_le(head + 4, 4) != e.flags ||
      get_le(head + 8, 8) != e.length)
    fail(sic::ErrorKind::invalid_format, "chunk does not match its journal entry");
  if (sha256(payload.data(), payload.size()) != e.sha256)
    fail(ErrorKind::integrity, "chunk digest does not match its journal entry");
  return payload;
}
}  // namespace sic::sicx

namespace sic::sicx {
void StreamWriter::State::abandon() { abandoned_ = true; }
}  // namespace sic::sicx

namespace sic::sicx {
void StreamWriter::State::chunk(const std::string& type, uint32_t flags,
                                std::span<const uint8_t> payload, uint64_t object, uint64_t first,
                                uint64_t count) {
  sic::support::check_stop();
  if (finished_) fail(ErrorKind::invalid_argument, "chunk after finish");
  if (!known_type(type)) fail(sic::ErrorKind::invalid_format, "invalid chunk type " + type);
  admit_chunk(type, payload.size(), options_.limits);
  if (index_.size() >= options_.limits.directory_entries)
    throw Error(ErrorKind::resource, "SICX: physical directory resource limit");
  IndexEntry e;
  std::memcpy(e.type.data(), type.data(), 4);
  e.flags = flags;
  e.offset = offset_;
  e.length = e.decoded_length = payload.size();
  e.object = object;
  e.first = first;
  e.count = count;
  e.sha256 = sha256(payload.data(), payload.size());
  const auto leaves = chunk_leaves(e, payload, options_.limits);
  if (!leaves.empty()) {
    e.first_leaf = logical_.size();
    e.leaf_count = leaves.size();
  }
  if (e.type_name() == "TOWR" || e.type_name() == "BODY") {
    uint64_t next = first;
    for (const auto& leaf : leaves) {
      if (leaf.element != object || leaf.first != next)
        fail(sic::ErrorKind::invalid_format, "writer leaf mapping mismatch");
      next = checked_add(next, leaf.count);
    }
    if (next - first != count)
      fail(sic::ErrorKind::invalid_format, "writer coordinate count mismatch");
  }
  if (leaves.size() > options_.limits.directory_entries ||
      logical_.size() > options_.limits.directory_entries - leaves.size())
    throw Error(ErrorKind::resource, "SICX: logical directory resource limit");
  logical_.insert(logical_.end(), leaves.begin(), leaves.end());
  if (index_.size() < replay_.size()) {
    ByteBuffer a, b;
    e.put(a);
    replay_[index_.size()].put(b);
    if (a != b)
      fail(sic::ErrorKind::integrity,
           "resume: chunk " + std::to_string(index_.size()) + " (" + type +
               ") differs from the interrupted run (non-deterministic input?)");
    index_.push_back(e);
    offset_ += kChunkHeaderBytes + payload.size();
    return;
  }
  ByteBuffer head(type.begin(), type.end());
  put_le(head, flags, 4);
  put_le(head, payload.size(), 8);
  write_data(head.data(), head.size());
  write_data(payload.data(), payload.size());
  if (options_.durable) {
    sync_fd(fd_, "partial");
    ByteBuffer p;
    e.put(p);
    journal(1, p);
  }
  index_.push_back(e);
#ifdef SIC_ENABLE_TEST_HOOKS
  ++durable_chunks_;
  if (kill_after_chunks_ && durable_chunks_ >= kill_after_chunks_) ::raise(SIGKILL);
#endif
}
}  // namespace sic::sicx

namespace sic::sicx {
Digest StreamWriter::State::finish() {
  sic::support::check_stop();
  if (pending_replay())
    fail(sic::ErrorKind::invalid_format,
         "resume: " + std::to_string(pending_replay()) + " recovered chunks were not replayed");
  Digest root;
  if (options_.part) {
    ByteBuffer idx(kPartMagic.begin(), kPartMagic.end());
    put_le(idx, index_.size(), 8);
    for (const auto& e : index_) e.put(idx);
    root = merkle_root(index_);
    idx.insert(idx.end(), root.begin(), root.end());
    sync_fd(fd_, "part");
    const std::filesystem::path idx_path = path_.string() + ".idx",
                                idx_partial = path_.string() + ".idx.partial";
    {
      ChunkDescriptor f(
          ::open(idx_partial.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));
      if (f.value < 0) fail(sic::ErrorKind::io, "cannot create " + idx_partial.string());
      pwrite_all(f.value, idx.data(), idx.size(), 0);
      sync_fd(f.value, "idx");
      f.close();
    }
    if (sic::sicx::filesystem_io([&] { return std::filesystem::exists(idx_path); }))
      fail(sic::ErrorKind::io, "output exists: " + idx_path.string());
    sic::sicx::rename_file(idx_partial, idx_path, "part_index");
  } else {
    ByteBuffer lt;
    put_le(lt, logical_.size(), 8);
    for (const auto& e : logical_) e.put(lt);
    const ByteBuffer ldir = directory_chunk("LDIR", lt);
    const uint64_t ldir_offset = offset_;
    write_data(ldir.data(), ldir.size());
    ByteBuffer pt;
    put_le(pt, index_.size(), 8);
    for (const auto& e : index_) e.put(pt);
    const uint64_t pdir_offset = offset_;
    const ByteBuffer pdir = directory_chunk("PDIR", pt);
    write_data(pdir.data(), pdir.size());
    root = file_tree_root(index_, ldir);
    const auto exact_root = merkle_root(logical_);
    ByteBuffer footer;
    put_le(footer, ldir_offset, 8);
    put_le(footer, lt.size(), 8);
    put_le(footer, pdir_offset, 8);
    put_le(footer, pt.size(), 8);
    footer.insert(footer.end(), exact_root.begin(), exact_root.end());
    footer.insert(footer.end(), root.begin(), root.end());
    put_le(footer, checked_add(offset_, kFooterBytes), 8);
    footer.insert(footer.end(), kEndMagic.begin(), kEndMagic.end());
    write_data(footer.data(), footer.size());
    if (::ftruncate(fd_, static_cast<off_t>(offset_))) fail(sic::ErrorKind::io, "truncate failed");
    if (::fsync(fd_)) fail(sic::ErrorKind::io, "fsync failed");
  }
  ::close(fd_);
  fd_ = -1;
  if (!options_.part) validate_output_(partial_, options_.limits);
  if (options_.before_publish) options_.before_publish(partial_);
  sic::support::publish_no_replace(partial_, path_);
  sync_dir(path_);
  finished_ = true;
  if (jfd_ >= 0) {
    ::close(jfd_);
    jfd_ = -1;
  }
  std::error_code ec;
  std::filesystem::remove(journal_, ec);
  return root;
}
}  // namespace sic::sicx

namespace sic::sicx {
StreamOptions::StreamOptions(bool d, bool r, Digest p, bool bare_part,
                             std::function<void(const std::filesystem::path&)> validate)
    : durable(d), resume(r), plan(p), part(bare_part), before_publish(std::move(validate)) {}

}  // namespace sic::sicx
