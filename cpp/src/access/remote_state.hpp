// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <sys/file.h>
#include <sys/resource.h>

#include <cmath>
#include <condition_variable>
#include <iostream>
#include <set>

#include "access/http_transfer.hpp"
#include "access/range_cache.hpp"
namespace sic::access {
class Source final : public RemoteByteSource {
 public:
  Source(const std::filesystem::path& manifest, RemoteOptions o);
  ~Source() override;
  uint64_t size() const override;
  bool remote() const override;
  uint64_t memory_reservation() const override;
  void bind_cancellation(std::shared_ptr<sic::support::StopToken> token) const override;
  void quiesce() const override;
  RemoteStats stats() const override;
  void bind_identity(const std::string& exact, const std::string& root) const override;
  void bind_layout(std::span<const uint64_t> boundaries, uint64_t suffix,
                   uint64_t dimension) const override;
  void plan(std::span<const sic::support::ByteRange> ranges) const override;
  void schedule(std::span<const sic::support::ByteRange> ranges) const override;
  void invalidate(sic::support::ByteRange r) const override;
  void read(uint64_t offset, std::span<uint8_t> output) const override;

 private:
  // mutex_ serializes demand and prefetch staging. No second byte window is
  // allocated for prefetch: fill() writes directly to the fragment cache.
  void fill(sic::support::ByteRange requested, std::span<uint8_t> output = {}) const;

  void prefetch_loop() const;

  sic::transport::Parts parts_;
  RemoteOptions options_;
  RangeCache cache_;
  std::string base_;
  bool http_ = false;
  std::filesystem::path cache_base_;
  mutable std::string exact_, root_;
  mutable Ranges planned_;
  mutable std::mutex mutex_, transfer_mutex_;
  mutable RemoteStats stats_;
  mutable uint64_t active_ = 0;
  mutable std::shared_ptr<sic::support::ByteSource> full_;
  mutable std::vector<sic::support::ByteRange> schedule_;
  mutable std::vector<uint64_t> prefix_;
  mutable size_t next_prefetch_ = 0;
  mutable uint64_t consumed_ = 0;
  mutable bool decoder_bound_ = false;
  mutable std::shared_ptr<sic::support::StopToken> cancellation_;
  mutable std::thread prefetcher_;
  mutable std::condition_variable changed_;
  mutable std::exception_ptr prefetch_error_;
  mutable std::atomic<bool> stopping_{false};

  void admit(std::span<const sic::support::ByteRange> ranges) const;
  void count_request() const;
  void count_bytes(uint64_t n) const;
  void backoff(unsigned attempt) const;
  void fetch(sic::support::ByteRange r, ByteBuffer& bytes) const;
  // Once a complete part is cached, also check its manifest SHA-256. Sparse
  // subsets have only the container hashes and local range-integrity records.
  void download_full_part(int fd, const sic::transport::Part& part) const;
  void activate_full(const std::filesystem::path& file) const;
  void check_full(const std::filesystem::path& file) const;
  void use_full(const std::string& why) const;
};
}  // namespace sic::access
