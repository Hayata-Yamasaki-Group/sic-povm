// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

#include "sic/support/stop.hpp"

namespace sic::support {

struct ByteRange {
  uint64_t offset = 0, size = 0;
};

// Exact, thread-safe reads of a logical file. The caller owns the output buffer.
// plan() admits a set of future reads before any of them is fetched; it does not
// prefetch. Container readers retain responsibility for their integrity checks.
class ByteSource {
 public:
  virtual ~ByteSource() = default;
  virtual uint64_t size() const = 0;
  virtual void read(uint64_t offset, std::span<uint8_t> output) const = 0;
  virtual void plan(std::span<const ByteRange> ranges) const;
  // Ordered lookahead hint, after plan() has admitted the entire dependency set.
  // Implementations may prefetch to disk; reads still perform container checks.
  virtual void schedule(std::span<const ByteRange>) const {}
  virtual bool remote() const { return false; }
  virtual uint64_t memory_reservation() const { return 0; }
  virtual void quiesce() const {}
  // Owner operation, before scheduling; a Reader must not be shared between
  // concurrently mutating decoder instances. Workers retain the shared token.
  virtual void bind_cancellation(std::shared_ptr<sic::support::StopToken>) const {}
  // Called only after a container has validated its index and both identities.
  virtual void bind_identity(const std::string&, const std::string&) const {}
  virtual void bind_layout(std::span<const uint64_t>, uint64_t, uint64_t) const {}
  virtual void invalidate(ByteRange) const {}

 protected:
  void check(ByteRange r) const;
};

class LocalByteSource final : public ByteSource {
 public:
  explicit LocalByteSource(const std::filesystem::path& path);
  ~LocalByteSource() override;
  LocalByteSource(const LocalByteSource&) = delete;
  LocalByteSource& operator=(const LocalByteSource&) = delete;
  uint64_t size() const override { return size_; }
  void read(uint64_t offset, std::span<uint8_t> output) const override;

 private:
  int fd_ = -1;
  uint64_t size_ = 0;
};

std::shared_ptr<ByteSource> local_byte_source(const std::filesystem::path& path);

}  // namespace sic::support
