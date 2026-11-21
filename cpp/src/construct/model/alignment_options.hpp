// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace sic::construct {
inline constexpr uint64_t kAlignmentSeed = 20261104;
inline constexpr uint64_t kAlignmentTrain = 64;
inline constexpr uint64_t kAlignmentCandidates = 2048;
inline constexpr int kAlignmentSweeps = 4;
inline constexpr uint64_t kAlignmentBatchBytes = uint64_t{256} << 20;
inline constexpr uint64_t kAlignmentWorkerBytes = uint64_t{256} << 20;
inline constexpr const char* kAlignmentSelector = "conjugate-alignment";
struct AlignmentOptions {
  std::string selector = kAlignmentSelector;
  uint64_t seed = kAlignmentSeed;
  uint64_t train = kAlignmentTrain, candidates = kAlignmentCandidates;
  int sweeps = kAlignmentSweeps;
  uint64_t memory_bytes = uint64_t{16} << 30;
  std::optional<uint64_t> cache_bytes;
  uint64_t batch_bytes = kAlignmentBatchBytes;
  uint64_t worker_bytes = kAlignmentWorkerBytes;
  bool trace = false;
};
}  // namespace sic::construct
