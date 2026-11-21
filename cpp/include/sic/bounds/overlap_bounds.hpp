// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <flint/acb.h>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "sic/limits.hpp"

namespace sic::bounds {

// Native dimension and precision limits enforced by admit_wh_bounds.
inline constexpr std::size_t kMinimumBallDimension = 2;
inline constexpr std::size_t kMaximumBallDimension = LONG_MAX / 2;
inline constexpr long kMinimumBallWorkingBits = 64;

struct BallVerificationLimits {
  long working_bits = 256;
  // Required maximum-deviation interval radius <=2^(-this value).
  long minimum_absolute_accuracy_bits = 64;
  long maximum_working_bits = 4096;
  std::size_t maximum_dimension = 2048;
  std::uint64_t maximum_input_mantissa_bits = 64ULL * 1024 * 1024;
  std::uint64_t maximum_workspace_bytes = 512ULL * 1024 * 1024;
  // Declared admission score, not a bound on native instructions/bit work.
  // Default admits dimension2048 at the default precision/workspace limits.
  std::uint64_t maximum_work_score = 10000000000ULL;
};

// Validate dimension/precision and book the declared O(d) engineering workspace
// and minimum work score without touching input or allocating native vectors.
std::uint64_t admit_wh_bounds(std::size_t dimension, const BallVerificationLimits& limits);

struct BallVerificationInfo {
  std::size_t dimension;
  long working_bits;
  long minimum_absolute_accuracy_bits;
  std::uint64_t checked_nonidentity_labels;
  std::uint64_t dft_rows;
  std::uint64_t dft_points;
  std::uint64_t input_mantissa_bits;
  std::uint64_t admitted_workspace_bytes;
  std::uint64_t booked_work_score;
};

// Rigorous numerical enclosures, not an exact-zero SIC certificate. For every
// vector represented by the original input balls, the squared norm and maximum
// nonidentity WH squared-overlap deviation lie in the corresponding intervals.
class WhOverlapDeviationBounds {
 public:
  ~WhOverlapDeviationBounds();
  WhOverlapDeviationBounds(WhOverlapDeviationBounds&&) noexcept;
  WhOverlapDeviationBounds& operator=(WhOverlapDeviationBounds&&) noexcept;
  WhOverlapDeviationBounds(const WhOverlapDeviationBounds&) = delete;
  WhOverlapDeviationBounds& operator=(const WhOverlapDeviationBounds&) = delete;
  const BallVerificationInfo& info() const;
  arb_srcptr squared_norm() const;
  arb_srcptr maximum_squared_overlap_deviation() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  explicit WhOverlapDeviationBounds(std::unique_ptr<Impl> impl);
  friend WhOverlapDeviationBounds bound_wh_overlap_deviations(acb_srcptr, std::size_t,
                                                              const BallVerificationLimits&);
};

// Input is a caller-owned contiguous array of dimension initialized balls.
// No input mutation/copy is performed before admission. One native FLINT worker
// is required in the caller's FLINT thread-local budget; this is not an
// exclusive process lease. This function changes no global runtime settings.
// Dimension >= 2 within caller policy/native index bounds, finite Lagom inputs,
// a positive norm lower bound and the
// requested absolute deviation-radius accuracy are mandatory. Refusals throw;
// there is no automatic precision clamp/retry or inferred SIC pass/fail.
// Explicit retained working vectors use O(d) storage; native DFT workspace is
// covered by an engineering allowance, not a universal allocator theorem.
// Concurrent calls/admission are not guaranteed. The caller must not mutate
// the input or FLINT runtime while this function executes.
// Destroy results before calling application-level flint_cleanup().
WhOverlapDeviationBounds bound_wh_overlap_deviations(acb_srcptr components, std::size_t dimension,
                                                     const BallVerificationLimits& limits = {});

}  // namespace sic::bounds
