// SPDX-License-Identifier: AGPL-3.0-or-later
// Optional SICX transport-part random access. No curl types enter public headers.
#pragma once

#include <vector>

#include "sic/support/byte_source.hpp"

namespace sic::access {

struct RemoteLimits {
  unsigned maximum_jobs = 32, maximum_retries = 10;
  uint64_t maximum_extent_bytes = uint64_t{8} << 20;
  uint64_t maximum_ranges = uint64_t{1} << 20;
  uint64_t maximum_prefetch_bytes = uint64_t{256} << 20;
};
struct RemoteOptions {
  RemoteLimits limits;
  std::shared_ptr<sic::support::StopToken> cancellation =
      std::make_shared<sic::support::StopToken>();
  std::string source;                   // empty: GitHub release from the manifest
  std::filesystem::path cache;          // empty: XDG_CACHE_HOME/sicx or ~/.cache/sicx
  unsigned jobs = 4, retries = 3;       // retries after the initial attempt
  uint64_t range_bytes = 1u << 20;      // maximum extent and HTTP request size
  uint64_t coalesce_gap = 4096;         // maximum admitted gap between scheduled ranges
  uint64_t prefetch_bytes = 8u << 20;   // disk lookahead; zero disables scheduling
  uint64_t memory_budget = UINT64_MAX;  // aggregate caller budget; decoder reserves source headroom
  long timeout_ms = 30000, connect_timeout_ms = 10000;
  uint64_t max_ranges = 262144;  // hard cap on persistent extent and plan metadata
  double max_fraction = 0.5;     // union of missing planned ranges / logical size
  bool fetch_full = false;       // explicit opt-in to verified whole-file fallback
  std::string exact_root, root;  // optional trusted identities supplied out of band
};

struct RemoteStats {
  // requests counts transfer attempts, excluding redirect hops. buffer_bound
  // covers owned byte staging, excluding metadata and caller/decoder buffers.
  uint64_t requests = 0, transferred = 0, cache_bytes = 0;
  uint64_t peak_active = 0, buffer_bound = 0;
  uint64_t planned_bytes = 0, scheduled_bytes = 0, coalesced_ranges = 0;
  uint64_t prefetched_bytes = 0, prefetch_window = 0;
  bool full_download = false;
};

class RemoteByteSource : public sic::support::ByteSource {
 public:
  virtual RemoteStats stats() const = 0;
};

// With libcurl < 7.84, construct HTTP sources before launching application threads.
// Reads and planning on a constructed source are thread-safe. A source binds
// to one decoder cancellation context for its lifetime; independent decoder
// jobs require distinct sources (and distinct cache directories while active).
// A conflicting bind throws ErrorKind::invalid_argument before scheduling.
std::shared_ptr<RemoteByteSource> remote_byte_source(const std::filesystem::path& manifest,
                                                     RemoteOptions options = {});

}  // namespace sic::access
