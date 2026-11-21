// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <unistd.h>

#include <set>

#include "access/range_plan.hpp"
#include "sic/access/remote.hpp"
#include "sic/transport/parts.hpp"
namespace sic::access {
using sic::Error;
using sic::sicx::ByteBuffer;
using sic::sicx::Digest;
struct NeedFull : Error {
  using Error::Error;
};
struct Fd {
  int value = -1;
  explicit Fd(int n) : value(n) {}
  ~Fd() {
    if (value >= 0) ::close(value);
  }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
};
std::string checksum(std::string_view text);
std::filesystem::path default_cache();
void atomic_file(const std::filesystem::path& path, std::span<const uint8_t> bytes);
void atomic_text(const std::filesystem::path& path, const std::string& s);
// Borrows immutable Source values; Source declares both owners before the cache.
// Only Source holds its admission mutex. Cache operations never reenter Source.
class RangeCache {
 public:
  RangeCache(const sic::transport::Parts& parts, const RemoteOptions& options)
      : parts_(parts), options_(options) {}
  struct Entry {
    uint64_t size;
    std::string name;
  };
  mutable std::filesystem::path directory_;
  mutable std::map<uint64_t, Entry> entries_;
  mutable std::set<uint64_t> bad_, checked_parts_;
  mutable bool checked_whole_ = false;
  static std::string entry_name(sic::support::ByteRange r);
  void scan() const;
  Digest range_hash(const ByteBuffer& record) const;
  ByteBuffer load(uint64_t offset, const Entry& e) const;
  void store(sic::support::ByteRange r, const ByteBuffer& bytes) const;
  void remove_bad() const;
  void erase(sic::support::ByteRange r) const;
  Ranges available(std::span<const sic::support::ByteRange> ranges) const;
  void verify_complete_parts() const;

 private:
  const sic::transport::Parts& parts_;
  const RemoteOptions& options_;
};
}  // namespace sic::access
