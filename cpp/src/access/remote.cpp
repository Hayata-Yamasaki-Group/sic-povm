// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cctype>
#include <charconv>
#include <climits>

#include "access/remote_state.hpp"
#include "sic/support/parse_number.hpp"
#include "sic/support/thread_group.hpp"
#include "sicx/codec/integer.hpp"
#include "sicx/io/io.hpp"
#include "support/sha256.hpp"
#include "transport/parts.hpp"
namespace sic::access {
using namespace sic::sicx;

namespace filesystem = std::filesystem;
std::string local_base(const std::string& base) {
  if (base.rfind("file://", 0) != 0) return base;
  std::string s = base.substr(7);
  if (s.rfind("localhost/", 0) == 0) s.erase(0, 9);
  if (s.empty() || s[0] != '/')
    fail(sic::ErrorKind::invalid_argument,
         "remote: file:// mirror must have an absolute local path");
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%') {
      if (i + 2 >= s.size()) fail(sic::ErrorKind::invalid_argument, "remote: bad file URL escape");
      const unsigned n =
          support::parse_number<unsigned>(std::string_view(s).substr(i + 1, 2), "file URL escape",
                                          ErrorKind::invalid_argument, 1, 255, 16);
      out += static_cast<char>(n);
      i += 2;
    } else
      out += s[i];
  }
  return out;
}

Source::Source(const filesystem::path& manifest, RemoteOptions o)
    : parts_(sic::transport::parse_parts(sic::transport::read_text(manifest))),
      options_(std::move(o)),
      cache_(parts_, options_),
      base_(sic::transport::source_base(parts_, options_.source)) {
  cancellation_ = options_.cancellation;
  if (parts_.size > (uint64_t(1) << 62))
    fail(sic::ErrorKind::resource, "remote: logical file too large");
  if (!options_.cancellation || !options_.jobs || options_.jobs > options_.limits.maximum_jobs ||
      options_.retries > options_.limits.maximum_retries || !options_.range_bytes ||
      options_.range_bytes > options_.limits.maximum_extent_bytes || !options_.max_ranges ||
      options_.max_ranges > options_.limits.maximum_ranges || options_.timeout_ms < 1 ||
      options_.connect_timeout_ms < 1 ||
      options_.coalesce_gap > options_.limits.maximum_extent_bytes ||
      options_.prefetch_bytes > options_.limits.maximum_prefetch_bytes ||
      !std::isfinite(options_.max_fraction) || options_.max_fraction <= 0 ||
      options_.max_fraction > 1)
    fail(sic::ErrorKind::invalid_argument,
         "remote: invalid jobs (1..32), retries (0..10), range bytes (1..8388608), cache ranges "
         "(1..1048576), timeout or "
         "fraction (0..1]");
  if ((!options_.exact_root.empty() && !sic::transport::is_hex64(options_.exact_root)) ||
      (!options_.root.empty() && !sic::transport::is_hex64(options_.root)))
    fail(sic::ErrorKind::invalid_argument, "remote: root pins must be lowercase SHA-256 hex");
  http_ = base_.rfind("https://", 0) == 0 || base_.rfind("http://", 0) == 0;
  if (!http_) {
    if (base_.find("://") != std::string::npos && base_.rfind("file://", 0) != 0)
      fail(sic::ErrorKind::unsupported, "remote: unsupported source URL");
    base_ = local_base(base_);
  }
#ifndef SIC_HAVE_CURL
  if (http_)
    fail(sic::ErrorKind::unsupported,
         "remote: HTTP support was disabled at build time (SIC_ENABLE_REMOTE); use a local parts "
         "mirror or sic-container fetch");
#else
  // 7.81 has no thread-safe global initialization guarantee. Construction must
  // precede application workers; the CLIs open sources before creating pools.
  if (http_) (void)curl_api();
#endif
  cache_base_ = (options_.cache.empty() ? default_cache() : options_.cache) / "ranges" /
                parts_.exact_root / parts_.file_root /
                checksum(sic::support::dump_json(sic::transport::parts_to_json(parts_)));
  sic::sicx::filesystem_io([&] { return filesystem::create_directories(cache_base_); });
  // Identity is a checksummed hint, never a substitute for the Reader checks.
  const filesystem::path hint = cache_base_ / "identity";
  if (sic::sicx::filesystem_io([&] { return filesystem::exists(hint); }) &&
      sic::sicx::filesystem_io([&] { return filesystem::file_size(hint); }) == 195) {
    const std::string s = sic::transport::read_text(hint);
    if (s.size() == 195 && s[64] == '\n' && s[129] == '\n' && s[194] == '\n' &&
        sic::transport::is_hex64(s.substr(0, 64)) && sic::transport::is_hex64(s.substr(65, 64)) &&
        checksum(s.substr(0, 130)) == s.substr(130, 64)) {
      exact_ = s.substr(0, 64);
      root_ = s.substr(65, 64);
    }
  }
  cache_.directory_ = cache_base_ / parts_.file_root;
  sic::sicx::filesystem_io([&] { return filesystem::create_directories(cache_.directory_); });
  stats_.buffer_bound =
      checked_add(checked_mul(checked_mul(2, options_.jobs), checked_add(options_.range_bytes, 56)),
                  checked_mul(2, options_.limits.maximum_extent_bytes));
  stats_.prefetch_window = options_.prefetch_bytes;
  if (memory_reservation() >= options_.memory_budget)
    fail(sic::ErrorKind::resource,
         "remote: source staging/metadata headroom exceeds memory budget; reduce --range-jobs, "
         "--range-bytes or --cache-max-ranges");
  const filesystem::path full = cache_base_ / "full.sicx";
  if (sic::sicx::filesystem_io([&] { return filesystem::exists(full); })) {
    bool valid = false;
    try {
      check_full(full);
      valid = true;
    } catch (const Error& error) {
      if (error.kind() != ErrorKind::integrity && error.kind() != ErrorKind::invalid_format) throw;
      // Do not trust a damaged full cache. Normal admission controls refetches.
      sic::sicx::filesystem_io([&] { return filesystem::remove(full); });
    }
    if (valid) activate_full(full);
  }
  if (!full_) cache_.scan();
}

Source::~Source() {
  stopping_ = true;
  changed_.notify_all();
  if (prefetcher_.joinable()) prefetcher_.join();
}

uint64_t Source::size() const { return parts_.size; }

bool Source::remote() const { return true; }

uint64_t Source::memory_reservation() const {
  // Includes worst-case extent maps, plan/schedule construction and worker
  // metadata. Curl/allocator overhead is also covered by measured decoder RSS.
  return checked_add(stats_.buffer_bound,
                     checked_add(checked_mul(options_.max_ranges, 512), 8ull << 20));
}

void Source::bind_cancellation(std::shared_ptr<sic::support::StopToken> token) const {
  std::lock_guard lock(mutex_);
  if (!token || (decoder_bound_ && cancellation_ != token))
    throw Error(
        ErrorKind::invalid_argument,
        "remote source is already bound to a different decoder context; use a separate source");
  cancellation_ = std::move(token);
  decoder_bound_ = true;
}

void Source::quiesce() const {
  std::lock_guard lock(mutex_);
  schedule_.clear();
  prefix_.clear();
  next_prefetch_ = 0;
  if (prefetch_error_) std::rethrow_exception(prefetch_error_);
}

RemoteStats Source::stats() const {
  std::lock_guard lock(mutex_);
  return stats_;
}

void Source::bind_identity(const std::string& exact, const std::string& root) const {
  std::lock_guard lock(mutex_);
  if ((!options_.exact_root.empty() && options_.exact_root != exact) ||
      (!options_.root.empty() && options_.root != root))
    fail(sic::ErrorKind::integrity, "remote: footer roots disagree with the trusted root pin");
  if ((!exact_.empty() && exact_ != exact) || (!root_.empty() && root_ != root))
    fail(sic::ErrorKind::integrity,
         "remote: footer roots disagree with the cached identity for the manifest SHA-256");
  if (!sic::transport::is_hex64(exact) || !sic::transport::is_hex64(root))
    fail(sic::ErrorKind::invalid_format, "remote: invalid container identity");
  if (parts_.exact_root != exact || parts_.file_root != root)
    fail(sic::ErrorKind::integrity, "remote: manifest/footer roots differ");
  const filesystem::path target = cache_base_ / root;
  sic::sicx::filesystem_io([&] { return filesystem::create_directories(target); });
  if (cache_.directory_ != target) {
    for (const auto& [offset, entry] : cache_.entries_) {
      (void)offset;
      // Keep bootstrap extents: another process may still be opening the same
      // manifest. Atomic copies avoid a rename race and preserve offline open.
      const filesystem::path from = cache_.directory_ / entry.name, to = target / entry.name;
      Fd fd(::open(from.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
      if (fd.value >= 0) {
        struct stat st {};
        if (!::fstat(fd.value, &st) && st.st_size == static_cast<off_t>(entry.size + 56)) {
          ByteBuffer record(entry.size + 56);
          pread_exact(fd.value, record.data(), record.size(), 0);
          atomic_file(to, record);
        }
      }
    }
    cache_.directory_ = target;
    sync_dir(cache_.directory_ / "entry");
  }
  exact_ = exact;
  root_ = root;
  const std::string s = exact + "\n" + root + "\n";
  atomic_text(cache_base_ / "identity", s + checksum(s) + "\n");
}

void Source::invalidate(sic::support::ByteRange r) const {
  std::lock_guard lock(mutex_);
  if (full_) {
    full_.reset();
    sic::sicx::filesystem_io([&] { return filesystem::remove(cache_base_ / "full.sicx"); });
    sync_dir(cache_base_ / "full.sicx");
    cache_.checked_whole_ = false;
    cache_.checked_parts_.clear();
    stats_.full_download = false;
  }
  cache_.erase(r);
}

void Source::read(uint64_t offset, std::span<uint8_t> output) const {
  check({offset, output.size()});
  if (output.empty()) return;
  // Concurrent Reader calls share one admission/cache lock. Each call uses at
  // most jobs workers internally, so no unbounded HTTP or staging allocation.
  std::lock_guard lock(mutex_);
  if (prefetch_error_) std::rethrow_exception(prefetch_error_);
  sic::support::StopScope stop(cancellation_.get());
  sic::support::check_stop();
  const sic::support::ByteRange requested{offset, output.size()};
  admit({&requested, 1});
  if (full_) {
    full_->read(offset, output);
    return;
  }
  fill(requested, output);
  // Reading the last bytes of a scheduled unit releases disk-lookahead credit.
  // Workers may request units out of order; the high-water mark is sufficient
  // because only transport fragments, not decoded chunks, are retained here.
  const auto end = offset + output.size();
  auto it = std::lower_bound(
      schedule_.begin(), schedule_.end(), offset,
      [](sic::support::ByteRange r, uint64_t at) { return r.offset + r.size <= at; });
  for (; it != schedule_.end() && it->offset < end; ++it)
    if (it->offset + it->size <= end)
      consumed_ = std::max(consumed_, prefix_[it - schedule_.begin()] + it->size);
  changed_.notify_all();
}

void Source::fill(sic::support::ByteRange requested, std::span<uint8_t> output) const {
  const uint64_t offset = requested.offset, length = requested.size;
  Ranges available;
  for (const auto& [b, entry] : cache_.entries_) {
    if (b >= offset + length) break;
    if (b + entry.size <= offset) continue;
    const ByteBuffer bytes = cache_.load(b, entry);
    if (bytes.empty()) continue;
    const uint64_t first = std::max(b, offset), end = std::min(b + entry.size, offset + length);
    if (!output.empty()) {
      std::memcpy(output.data() + first - offset, bytes.data() + first - b, end - first);
      stats_.cache_bytes += end - first;
    }
    add(available, first, end);
  }
  cache_.remove_bad();
  const auto missing = gaps(available, requested);
  size_t cursor = 0;
  uint64_t at = missing.empty() ? 0 : missing.front().offset;
  std::atomic<bool> stop{false}, fallback{false};
  std::exception_ptr error;
  std::mutex result_mutex;
  auto worker = [&] {
    while (!stop) {
      sic::support::ByteRange r;
      {
        std::lock_guard result_lock(result_mutex);
        if (cursor == missing.size()) return;
        const auto& part = sic::transport::part_at(parts_, at);
        const uint64_t end = missing[cursor].offset + missing[cursor].size;
        r = {at, std::min({end - at, part.offset + part.size - at, options_.range_bytes})};
        at += r.size;
        if (at == end) {
          ++cursor;
          if (cursor < missing.size()) at = missing[cursor].offset;
        }
      }
      try {
        ByteBuffer bytes(r.size);
        fetch(r, bytes);
        cache_.store(r, bytes);
        if (!output.empty())
          std::memcpy(output.data() + r.offset - offset, bytes.data(), bytes.size());
        std::lock_guard result_lock(result_mutex);
        cache_.entries_[r.offset] = {r.size, cache_.entry_name(r)};
        if (output.empty()) stats_.prefetched_bytes += r.size;
      } catch (const NeedFull&) {
        fallback = true;
        stop = true;
      } catch (...) {
        std::lock_guard result_lock(result_mutex);
        if (!error) error = std::current_exception();
        stop = true;
      }
    }
  };
  // Shared joining ownership covers partial worker startup too.
  sic::support::ThreadGroup joining;
  for (unsigned i = 0; i < (missing.empty() ? 0u : options_.jobs); ++i)
    joining.start([&] {
      sic::support::flint::WorkerCleanup cleanup;
      worker();
    });
  joining.join();
  if (error) std::rethrow_exception(error);
  if (fallback) {
    use_full("server lacks HTTP Range support (200 response aborted at its headers)");
    if (!output.empty()) full_->read(offset, output);
    return;
  }
  cache_.verify_complete_parts();
}

void Source::prefetch_loop() const {
  sic::support::flint::WorkerCleanup cleanup;
  std::unique_lock lock(mutex_);
  for (;;) {
    changed_.wait(lock, [&] {
      return stopping_ || (!prefetch_error_ && !full_ && next_prefetch_ < schedule_.size() &&
                           prefix_[next_prefetch_] + schedule_[next_prefetch_].size <=
                               consumed_ + options_.prefetch_bytes);
    });
    if (stopping_) return;
    const auto r = schedule_[next_prefetch_++];
    try {
      sic::support::StopScope stop(cancellation_.get());
      sic::support::check_stop();
      fill(r);
    } catch (...) {
      prefetch_error_ = std::current_exception();
    }
    // Give demand reads a chance between bounded batches.
    lock.unlock();
    std::this_thread::yield();
    lock.lock();
  }
}

void Source::count_request() const {
  std::lock_guard lock(transfer_mutex_);
  ++stats_.requests;
}

void Source::count_bytes(uint64_t n) const {
  std::lock_guard lock(transfer_mutex_);
  stats_.transferred += n;
}

void Source::backoff(unsigned attempt) const {
  constexpr unsigned kInitialDelayMilliseconds = 100u;
  constexpr unsigned kMaxBackoffShift = 6u;
  constexpr unsigned kTickMilliseconds = 10u;
  const unsigned ticks =
      (kInitialDelayMilliseconds << std::min(attempt, kMaxBackoffShift)) / kTickMilliseconds;
  for (unsigned i = 0; i < ticks; ++i) {
    if (stopping_) fail(sic::ErrorKind::cancelled, "remote: transfer cancelled");
    sic::support::check_stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(kTickMilliseconds));
  }
}

void Source::fetch(sic::support::ByteRange r, ByteBuffer& bytes) const {
  const auto& part = sic::transport::part_at(parts_, r.offset);
  if (!http_) {
    sic::support::LocalByteSource local(filesystem::path(base_) / part.asset);
    if (local.size() != part.size)
      fail(sic::ErrorKind::integrity, "remote: part size differs from the manifest: " + part.asset);
    local.read(r.offset - part.offset, bytes);
    count_bytes(bytes.size());
    return;
  }
#ifdef SIC_HAVE_CURL
  {
    std::lock_guard lock(transfer_mutex_);
    ++active_;
    stats_.peak_active = std::max(stats_.peak_active, active_);
  }
  struct Active {
    const Source* s;
    ~Active() {
      std::lock_guard lock(s->transfer_mutex_);
      --s->active_;
    }
  } active{this};
  uint64_t received = 0;
  for (unsigned attempt = 0;; ++attempt) {
    if (stopping_) fail(sic::ErrorKind::cancelled, "remote: transfer cancelled");
    Curl curl;
    Transfer t;
    t.stopping = &stopping_;
    t.first = r.offset - part.offset + received;
    t.last = r.offset - part.offset + r.size - 1;
    t.part_size = part.size;
    t.output = std::span<uint8_t>(bytes).subspan(received);
    const std::string url = base_ + "/" + part.asset;
    configure(curl.handle, url, options_, t);
    const std::string range = std::to_string(t.first) + "-" + std::to_string(t.last);
    curl_api().setopt(curl.handle, CURLOPT_RANGE, range.c_str());
    count_request();
    const CURLcode result = curl_api().perform(curl.handle);
    count_bytes(t.wire_bytes);
    if (stopping_) fail(sic::ErrorKind::cancelled, "remote: transfer cancelled");
    if (t.exception) std::rethrow_exception(t.exception);
    if (t.no_range) throw NeedFull(ErrorKind::unsupported, t.error);
    if (!t.error.empty() && !t.transient)
      fail(sic::ErrorKind::io, "remote: " + part.asset + ": " + t.error);
    received += t.got;
    if (result == CURLE_OK && received == r.size) return;
    // A missing transfer terminator is harmless for an exact byte range whose
    // bytes all arrived. Its chunk hash still has to pass in the Reader.
    if (received == r.size && t.headers_ok) return;
    if (attempt >= options_.retries)
      fail(sic::ErrorKind::io, "remote: " + part.asset + ": range transfer failed after retries: " +
                                   (t.error.empty() ? curl_api().strerror(result) : t.error));
    backoff(attempt);
  }
#else
  (void)r;
  (void)bytes;
  fail(sic::ErrorKind::unsupported, "remote: HTTP support unavailable");
#endif
}

void Source::download_full_part(int fd, const sic::transport::Part& part) const {
  if (!http_) {
    sic::support::LocalByteSource local(filesystem::path(base_) / part.asset);
    if (local.size() != part.size)
      fail(sic::ErrorKind::integrity, "remote full download: part size differs from the manifest");
    ByteBuffer buffer(std::min(part.size, options_.range_bytes));
    for (uint64_t at = 0; at < part.size;) {
      const size_t n = std::min<uint64_t>(buffer.size(), part.size - at);
      local.read(at, std::span<uint8_t>(buffer).first(n));
      pwrite_all(fd, buffer.data(), n, part.offset + at);
      at += n;
      count_bytes(n);
    }
    return;
  }
#ifdef SIC_HAVE_CURL
  for (unsigned attempt = 0;; ++attempt) {
    Curl curl;
    Transfer t;
    sic::support::sha256::Hasher h;
    t.stopping = &stopping_;
    t.full = true;
    t.fd = fd;
    t.file_offset = part.offset;
    t.part_size = part.size;
    t.hash = &h;
    const std::string url = base_ + "/" + part.asset;
    configure(curl.handle, url, options_, t);
    count_request();
    const auto rc = curl_api().perform(curl.handle);
    count_bytes(t.wire_bytes);
    if (t.exception) std::rethrow_exception(t.exception);
    if (rc == CURLE_OK && t.got == part.size && h.finish_hex() == part.sha256) return;
    if (!t.error.empty() && !t.transient)
      fail(sic::ErrorKind::io, "remote full download: " + t.error);
    if (attempt >= options_.retries)
      fail(rc == CURLE_OK && t.got == part.size ? ErrorKind::integrity : ErrorKind::io,
           "remote full download: transfer or part SHA-256 failed: " + part.asset);
    backoff(attempt);
  }
#else
  (void)fd;
  (void)part;
#endif
}

void Source::use_full(const std::string& why) const {
  if (!options_.fetch_full)
    throw NeedFull(
        ErrorKind::unsupported,
        "remote: " + why +
            "; use sic-container fetch for a full download, or pass --fetch-full to allow "
            "it explicitly");
  std::cerr << "SICX remote: " << why << "; --fetch-full permits a verified full download\n";
  const filesystem::path final = cache_base_ / "full.sicx", work = cache_base_ / "full.download";
  Fd lock(::open((cache_base_ / "full.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600));
  if (lock.value < 0 || ::flock(lock.value, LOCK_EX))
    fail(sic::ErrorKind::io, "remote: cannot lock full download");
  if (sic::sicx::filesystem_io([&] { return filesystem::exists(final); })) {
    bool valid = false;
    try {
      check_full(final);
      valid = true;
    } catch (const Error& error) {
      if (error.kind() != ErrorKind::integrity && error.kind() != ErrorKind::invalid_format) throw;
      sic::sicx::filesystem_io([&] { return filesystem::remove(final); });
    }
    if (valid) {
      activate_full(final);
      return;
    }
  }
  sic::transport::check_disk(cache_base_, size() + (16u << 20), "explicit full download");
  Fd fd(::open(work.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600));
  if (fd.value < 0 || ::ftruncate(fd.value, static_cast<off_t>(size())))
    fail(sic::ErrorKind::io, "remote: cannot create full-download work file");
  ByteBuffer buffer(options_.range_bytes);
  for (const auto& part : parts_.parts) {
    if (sic::transport::hash_range(fd.value, part.offset, part.size, buffer) == part.sha256)
      continue;
    download_full_part(fd.value, part);
    if (sic::transport::hash_range(fd.value, part.offset, part.size, buffer) != part.sha256)
      fail(sic::ErrorKind::integrity,
           "remote full download: part SHA-256 disagrees with manifest: " + part.asset);
    sync_fd(fd.value, "full download");
  }
  check_full(work);
  sic::sicx::rename_file(work, final, "full_download");
  sync_dir(final);
  activate_full(final);
}

std::shared_ptr<RemoteByteSource> remote_byte_source(const filesystem::path& manifest,
                                                     RemoteOptions options) {
  return std::make_shared<Source>(manifest, std::move(options));
}
void Source::bind_layout(std::span<const uint64_t> boundaries, uint64_t suffix,
                         uint64_t dimension) const {
  if (!parts_.parts.front().asset.starts_with("d" + std::to_string(dimension) + "-m"))
    fail(sic::ErrorKind::integrity, "remote: asset dimension differs from EXMT");
  for (size_t i = 1; i < parts_.parts.size(); ++i)
    if (parts_.parts[i].offset > suffix ||
        std::find(boundaries.begin(), boundaries.end(), parts_.parts[i].offset) == boundaries.end())
      fail(sic::ErrorKind::invalid_format, "remote: transport cut splits physical chunk or suffix");
  if (parts_.parts.back().offset > suffix)
    fail(sic::ErrorKind::invalid_format, "remote: suffix is split");
}

void Source::plan(std::span<const sic::support::ByteRange> ranges) const {
  std::lock_guard lock(mutex_);
  admit(ranges);
}

void Source::schedule(std::span<const sic::support::ByteRange> ranges) const {
  std::lock_guard lock(mutex_);
  if (prefetch_error_) std::rethrow_exception(prefetch_error_);
  if (!options_.prefetch_bytes || full_) return;
  // Decoder order is physical order within the tower and representative
  // groups. Sort/deduplicate hints, then merge nearby ranges without crossing
  // a transport part. Gaps are admitted before any speculative transfer.
  Ranges wanted;
  for (auto r : ranges) {
    check(r);
    add(wanted, r.offset, r.offset + r.size);
    if (wanted.size() > options_.max_ranges)
      fail(sic::ErrorKind::resource, "remote: schedule metadata limit");
  }
  std::vector<sic::support::ByteRange> merged;
  uint64_t merges = ranges.size() > wanted.size() ? ranges.size() - wanted.size() : 0;
  const uint64_t batch =
      std::min(options_.prefetch_bytes, checked_mul(options_.range_bytes, options_.jobs));
  for (auto [first, end] : wanted) {
    uint64_t begin = first;
    while (begin < end) {
      const auto& part = sic::transport::part_at(parts_, begin);
      const uint64_t n = std::min({end - begin, part.offset + part.size - begin, batch});
      if (!merged.empty()) {
        auto& last = merged.back();
        const uint64_t gap = begin - (last.offset + last.size);
        if (gap <= options_.coalesce_gap && begin + n - last.offset <= batch &&
            sic::transport::part_at(parts_, last.offset).index == part.index) {
          last.size = begin + n - last.offset;
          ++merges;
          begin += n;
          continue;
        }
      }
      merged.push_back({begin, n});
      if (merged.size() > options_.max_ranges)
        fail(sic::ErrorKind::resource, "remote: schedule metadata limit");
      begin += n;
    }
  }
  admit(merged);
  if (full_) return;
  schedule_ = std::move(merged);
  prefix_.clear();
  uint64_t bytes = 0;
  for (auto r : schedule_) {
    prefix_.push_back(bytes);
    bytes = checked_add(bytes, r.size);
  }
  stats_.scheduled_bytes = bytes;
  stats_.coalesced_ranges += merges;
  next_prefetch_ = 0;
  consumed_ = 0;
  if (!prefetcher_.joinable()) prefetcher_ = std::thread([this] { prefetch_loop(); });
  changed_.notify_all();
}

void Source::admit(std::span<const sic::support::ByteRange> ranges) const {
  for (auto r : ranges) check(r);
  if (full_) return;
  const Ranges have = cache_.available(ranges);
  Ranges proposed = planned_, missing_ranges;
  for (auto r : ranges) {
    for (auto missing : gaps(have, r)) {
      add(proposed, missing.offset, missing.offset + missing.size);
      add(missing_ranges, missing.offset, missing.offset + missing.size);
      if (proposed.size() > options_.max_ranges || missing_ranges.size() > options_.max_ranges) {
        use_full("read plan exceeds --cache-max-ranges");
        return;
      }
    }
  }
  uint64_t extent_count = cache_.entries_.size();
  for (auto [first, end] : missing_ranges) {
    uint64_t begin = first;
    while (begin < end) {
      const auto& part = sic::transport::part_at(parts_, begin);
      const uint64_t n = std::min(end - begin, part.offset + part.size - begin);
      extent_count += (n + options_.range_bytes - 1) / options_.range_bytes;
      if (extent_count > options_.max_ranges) {
        use_full("read plan exceeds --cache-max-ranges");
        return;
      }
      begin += n;
    }
  }
  if (static_cast<long double>(total(proposed)) >
      static_cast<long double>(size()) * options_.max_fraction) {
    use_full("read plan exceeds --remote-max-fraction=" + std::to_string(options_.max_fraction));
    return;
  }
  planned_ = std::move(proposed);
  stats_.planned_bytes = total(planned_);
}
void Source::activate_full(const filesystem::path& file) const {
  Reader reader(file);
  const std::string exact = hex(reader.exact_root()), root = hex(reader.root());
  if ((!exact_.empty() && exact_ != exact) || (!root_.empty() && root_ != root) ||
      (!options_.exact_root.empty() && options_.exact_root != exact) ||
      (!options_.root.empty() && options_.root != root))
    fail(sic::ErrorKind::integrity,
         "remote: full-download roots disagree with the sparse source identity or trusted root "
         "pin");
  full_ = sic::support::local_byte_source(file);
  stats_.full_download = true;
}

void Source::check_full(const filesystem::path& file) const {
#ifdef SIC_ENABLE_TEST_HOOKS
  filesystem_io([&] {
    if (io_fault_hook) io_fault_hook("full_validation", file);
  });
#endif
  if (options_.memory_budget == UINT64_MAX) {
    sic::transport::final_checks(file, parts_);
    return;
  }
  if (sic::sicx::filesystem_io([&] { return filesystem::file_size(file); }) != parts_.size ||
      sic::support::sha256::file_hex(file) != parts_.sha256)
    fail(sic::ErrorKind::integrity, "remote: full cache size or whole-file SHA-256 mismatch");
  Reader reader(file);
  sic::transport::validate_layout(parts_, reader);
  struct rusage usage {};
  if (::getrusage(RUSAGE_SELF, &usage))
    fail(sic::ErrorKind::io, "remote: cannot measure verification baseline");
  const auto used = checked_add(checked_mul(usage.ru_maxrss, 1024), memory_reservation());
  const auto scratch = bounds_memory(reader, true);
  if (used >= options_.memory_budget || scratch > options_.memory_budget - used)
    throw sic::Error(sic::ErrorKind::resource,
                     "remote: full verification exceeds aggregate memory budget");
  (void)verify_all(reader, 1);
}
}  // namespace sic::access
