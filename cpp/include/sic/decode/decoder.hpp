// SPDX-License-Identifier: AGPL-3.0-or-later
// Certified SICX decoding. Implementation state and kernels remain private.
#pragma once

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <vector>

#include "sic/decode/placement.hpp"
#include "sic/limits.hpp"
#include "sic/sicx/io.hpp"
#include "sic/support/ball_vectors.hpp"
#include "sic/support/stop.hpp"

namespace sic::decode {

struct DecoderAccess;

inline constexpr slong kMinimumTargetBits = 64;
inline constexpr slong kDefaultGuardBits = 64;
inline constexpr slong kDefaultInitialPrecisionCap = 2048;

struct Stats {
  double seconds_tower_load = 0, seconds_base = 0, seconds_tower_eval = 0, seconds_roots = 0,
         seconds_edges = 0, seconds_body = 0;
  double seconds_edges_targets = 0, seconds_edges_labels = 0, seconds_edges_duals = 0;
  double seconds_plan = 0, seconds_prune = 0, seconds_rebase = 0;
  uint64_t body_bytes_read = 0, tower_bytes_read = 0, contraction_leaves = 0, passes = 0,
           replays = 0;
  uint64_t edge_targets = 0, edge_targets_reused = 0, generations = 0;
  uint64_t budget_waits = 0, budget_rss_waits = 0, budget_peak = 0, budget_peak_per_worker = 0,
           budget_fallbacks = 0;
  unsigned bind_failures = 0;
  bool tower_resident = true;
  bool arenas_capped = false;
};

class Decoder {
 public:
  struct Options {
    slong target = 128;  // absolute accuracy bits of every raw output
    slong delta =
        -1;  // working precision = target + delta (default: PREC hint or kDefaultGuardBits)
    slong base_bits = -1;  // base path precision (default: PREC hint or capped library default)
    slong maximum_precision = sic::kDefaultMaximumPrecision;
    unsigned threads = 1;
    unsigned maximum_threads = 1024;
    uint64_t memory_budget = uint64_t(16) << 30;
    bool pin = true;
    unsigned attempts = 8;
    slong root_iterations = 256;
    int tower_resident = -1;  // -1: by budget; 0: stream per level; 1: keep resident
    // Per-instance token is retained by the decoder and inherited by workers.
    std::shared_ptr<sic::support::StopToken> cancellation =
        std::make_shared<sic::support::StopToken>();
    std::ostream* progress = nullptr;     // borrowed; must outlive Decoder; serialize shared sinks
    bool tune_process_allocator = false;  // explicit glibc process-wide opt-in
    uint64_t maximum_graph_nodes = uint64_t{1} << 32;
    bool profile = false;  // per-level timings on stderr
  };
  struct Request {
    int64_t representative;
    std::vector<uint64_t> word;
  };

  // Borrows sic::sicx::Reader; sic::sicx::Reader and progress sink must outlive this owner-thread
  // object. decode(), stats() and destruction must be serialized on its constructing thread.
  // Returned sic::support::ComplexBallVector owns its balls; views/references from
  // stats()/placement() live until destruction and stats mutate at the next call. Separate
  // instances may run in parallel with separate readers, underlying byte sources, tokens and sinks,
  // provided the application does not
  // change FLINT globals or call flint_cleanup() while any object is alive.
  // Native FLINT workers on the owner must already be one. pin=true temporarily
  // binds the owner; use pin=false in an embedding host. No environment is read.
  ~Decoder();
  Decoder(const Decoder&) = delete;
  Decoder& operator=(const Decoder&) = delete;
  // Moves must run on the constructing thread; views follow the transferred state.
  // Assignment between two live owners requires pin=false on both; otherwise it
  // refuses before mutation, preserving each pool's saved affinity restoration.
  Decoder(Decoder&& other);
  Decoder& operator=(Decoder&& other);
  static Options validate_options(Options options, const sic::sicx::Reader& reader);
  Decoder(const sic::sicx::Reader& reader, Options options);
  Decoder(sic::sicx::Reader&&, Options) = delete;
  Decoder(const sic::sicx::Reader&&, Options) = delete;
  Decoder(sic::sicx::Reader&&, Options, const std::vector<Request>&) = delete;
  Decoder(const sic::sicx::Reader&&, Options, const std::vector<Request>&) = delete;
  // Preflight the complete request dependency set before loading any towers.
  // The same sic::sicx::Reader accepts local files and authenticated remote byte sources.
  Decoder(const sic::sicx::Reader& reader, Options options, const std::vector<Request>& requests);
  static std::vector<size_t> input_chunks(const sic::sicx::Reader& reader,
                                          const std::vector<Request>& requests);
  const Stats& stats() const;
  const Placement& placement() const;
  slong base_precision() const;
  // Number of consecutive worker groups assigned to the same detected NUMA node.
  unsigned pool_groups() const;

  // Decode raw ball values iota(sigma_word(eta_rep)) for every request; each output has
  // absolute or relative accuracy >= target bits (else it is replayed).
  sic::support::ComplexBallVector decode(const std::vector<Request>& requests);

 private:
  friend struct DecoderAccess;
  class Impl;
  std::unique_ptr<Impl> impl_;
  Impl& state() const;
};
}  // namespace sic::decode
