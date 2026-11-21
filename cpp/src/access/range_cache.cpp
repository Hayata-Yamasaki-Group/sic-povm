// SPDX-License-Identifier: AGPL-3.0-or-later
#include "access/range_cache.hpp"

#include <cctype>
#include <charconv>
#include <climits>
#include <set>

#include "sicx/codec/integer.hpp"
#include "sicx/container/directory.hpp"
#include "sicx/io/io.hpp"
#include "support/sha256.hpp"
#include "transport/parts.hpp"
namespace sic::access {
using namespace sic::sicx;

namespace filesystem = std::filesystem;
std::string checksum(std::string_view text) {
  sic::support::sha256::Hasher h;
  h.update(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  return h.finish_hex();
}

filesystem::path default_cache() {
  const char* xdg = std::getenv("XDG_CACHE_HOME");
  if (xdg && *xdg) return filesystem::path(xdg) / "sicx";
  const char* home = std::getenv("HOME");
  if (home && *home) return filesystem::path(home) / ".cache/sicx";
  fail(sic::ErrorKind::invalid_argument, "remote: no user cache directory; pass --cache DIR");
}

// Keep this composition: cache entries use mkstemp permissions and data-only
// sync, and retain the fd through rename/parent sync with an unchecked close.
// support::atomic_write uses full sync and a checked close before publication.
void atomic_file(const filesystem::path& path, std::span<const uint8_t> bytes) {
  std::string pattern = path.string() + ".tmp-XXXXXX";
  const int raw = ::mkstemp(pattern.data());
  if (raw < 0) throw sic::Error(ErrorKind::io, "cache: cannot create " + path.string());
  Fd fd(raw);
  struct Cleanup {
    filesystem::path p;
    ~Cleanup() {
      std::error_code ec;
      filesystem::remove(p, ec);
    }
  } cleanup{pattern};
  pwrite_all(fd.value, bytes.data(), bytes.size(), 0);
  sync_fd(fd.value, "cache entry");
  if (::rename(pattern.c_str(), path.c_str()))
    throw sic::Error(ErrorKind::io, "cache: rename failed: " + std::string(std::strerror(errno)));
  sync_dir(path);
}

void atomic_text(const filesystem::path& path, const std::string& s) {
  atomic_file(path, {reinterpret_cast<const uint8_t*>(s.data()), s.size()});
}

std::string RangeCache::entry_name(sic::support::ByteRange r) {
  return std::to_string(r.offset) + "-" + std::to_string(r.size) + ".range";
}

void RangeCache::scan() const try {
  for (const auto& x :
       sic::sicx::filesystem_io([&] { return filesystem::directory_iterator(directory_); })) {
    const std::string n = x.path().filename().string();
    const size_t dash = n.find('-'), dot = n.find('.');
    if (dash == std::string::npos || dot == std::string::npos || n.substr(dot) != ".range")
      continue;
    try {
      const uint64_t b = sic::transport::parse_u64(n.substr(0, dash), ErrorKind::invalid_format),
                     length = sic::transport::parse_u64(n.substr(dash + 1, dot - dash - 1),
                                                        ErrorKind::invalid_format);
      if (!length || length > options_.limits.maximum_extent_bytes || b > parts_.size ||
          length > parts_.size - b || n != entry_name({b, length}))
        continue;
      // Entries starting at the same byte may differ after a changed read plan.
      // Prefer the longer one; every used record is verified before reuse.
      if (!entries_.contains(b) && entries_.size() >= options_.max_ranges)
        throw NeedFull(ErrorKind::resource,
                       "remote: cache extent limit exceeded; increase --cache-max-ranges or use "
                       "sic-container fetch");
      if (!entries_.contains(b) || entries_.at(b).size < length) entries_[b] = {length, n};
    } catch (const NeedFull&) {
      throw;
    } catch (const Error& error) {
      if (error.kind() != ErrorKind::invalid_format) throw;
      continue;
    }
  }
} catch (const filesystem::filesystem_error& error) {
  throw Error(ErrorKind::io, error.what());
}

Digest RangeCache::range_hash(const ByteBuffer& record) const {
  sic::support::sha256::Hasher h;
  h.update(reinterpret_cast<const uint8_t*>(parts_.sha256.data()), parts_.sha256.size());
  h.update(record.data(), 24);
  h.update(record.data() + 56, record.size() - 56);
  return h.finish();
}

ByteBuffer RangeCache::load(uint64_t offset, const Entry& e) const {
  const filesystem::path path = directory_ / e.name;
  Fd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  struct stat st {};
  if (fd.value < 0 || ::fstat(fd.value, &st) || !S_ISREG(st.st_mode) ||
      st.st_size != static_cast<off_t>(e.size + 56)) {
    bad_.insert(offset);
    return {};
  }
  ByteBuffer record(e.size + 56);
  try {
    pread_exact(fd.value, record.data(), record.size(), 0);
  } catch (const Error& error) {
    if (error.kind() != ErrorKind::io && error.kind() != ErrorKind::invalid_format &&
        error.kind() != ErrorKind::integrity)
      throw;
    bad_.insert(offset);
    return {};
  }
  const Digest h = range_hash(record);
  if (std::memcmp(record.data(), "SICXRNG1", 8) || get_le(record.data() + 8, 8) != offset ||
      get_le(record.data() + 16, 8) != e.size ||
      !std::equal(h.begin(), h.end(), record.begin() + 24)) {
    bad_.insert(offset);
    return {};
  }
  return ByteBuffer(record.begin() + 56, record.end());
}

void RangeCache::store(sic::support::ByteRange r, const ByteBuffer& bytes) const {
  ByteBuffer record{'S', 'I', 'C', 'X', 'R', 'N', 'G', '1'};
  put_le(record, r.offset, 8);
  put_le(record, r.size, 8);
  record.resize(56);
  record.insert(record.end(), bytes.begin(), bytes.end());
  const Digest h = range_hash(record);
  std::copy(h.begin(), h.end(), record.begin() + 24);
  atomic_file(directory_ / entry_name(r), record);
}

void RangeCache::remove_bad() const {
  for (auto b : bad_) {
    const auto i = entries_.find(b);
    if (i != entries_.end()) {
      std::error_code ec;
      filesystem::remove(directory_ / i->second.name, ec);
      entries_.erase(i);
    }
  }
  if (!bad_.empty()) {
    checked_parts_.clear();
    checked_whole_ = false;
  }
  bad_.clear();
}

void RangeCache::erase(sic::support::ByteRange r) const {
  for (const auto& [b, e] : entries_)
    if (b < r.offset + r.size && b + e.size > r.offset) bad_.insert(b);
  remove_bad();
}

void RangeCache::verify_complete_parts() const {
  Ranges rs;
  for (const auto& [b, e] : entries_) add(rs, b, b + e.size);
  for (const auto& part : parts_.parts) {
    if (checked_parts_.contains(part.index) || !covered(rs, part.offset, part.offset + part.size))
      continue;
    sic::support::sha256::Hasher h;
    uint64_t at = part.offset;
    for (const auto& [b, e] : entries_) {
      if (b >= part.offset + part.size) break;
      if (b > at || b + e.size <= at) continue;
      const ByteBuffer bytes = load(b, e);
      if (bytes.empty()) break;
      const uint64_t end = std::min(b + e.size, part.offset + part.size);
      h.update(bytes.data() + at - b, end - at);
      at = end;
    }
    if (at != part.offset + part.size) {
      remove_bad();
      continue;
    }
    if (h.finish_hex() != part.sha256) {
      erase({part.offset, part.size});
      fail(sic::ErrorKind::integrity,
           "remote: cached part SHA-256 disagrees with the manifest: " + part.asset);
    }
    checked_parts_.insert(part.index);
  }
  if (!checked_whole_ && checked_parts_.size() == parts_.parts.size()) {
    sic::support::sha256::Hasher whole;
    uint64_t at = 0;
    for (const auto& [b, e] : entries_) {
      if (b > at || b + e.size <= at) continue;
      const ByteBuffer bytes = load(b, e);
      if (bytes.empty()) break;
      whole.update(bytes.data() + at - b, bytes.size() - (at - b));
      at = b + e.size;
    }
    if (at == parts_.size) {
      if (whole.finish_hex() != parts_.sha256) {
        erase({0, parts_.size});
        fail(sic::ErrorKind::integrity, "remote: whole-file SHA-256 disagrees with the manifest");
      }
      checked_whole_ = true;
    }
    remove_bad();
  }
}

Ranges RangeCache::available(std::span<const sic::support::ByteRange> ranges) const {
  Ranges rs;
  for (const auto& [b, e] : entries_) {
    bool needed = false;
    for (auto r : ranges)
      if (b < r.offset + r.size && b + e.size > r.offset) {
        needed = true;
        break;
      }
    if (needed && !load(b, e).empty()) add(rs, b, b + e.size);
  }
  remove_bad();
  return rs;
}

}  // namespace sic::access
