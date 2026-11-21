// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "sic/support/ball_vectors.hpp"
#include "support/resources/checked_bytes.hpp"

namespace sic::exact {

// A bounded, attempt-local cache of unscaled chirped DFT rows. The caller's
// domain authenticates the full root state, its normalization coordinate and
// constants generation. Dimension and arithmetic precision are also in the key.
// prepare() is serial; returned distinct slots may be filled concurrently, and
// no cache operation is allowed until those workers have joined.
class DirectEtaRows {
 public:
  using Bytes = sic::support::resources::Bytes;
  using Count = sic::support::resources::Count;
  struct Request {
    long k, a0, gamma, length;
  };
  struct Batch {
    std::vector<sic::support::ComplexBallVector*> slots;
    size_t hits = 0, evictions = 0;
  };

  explicit DirectEtaRows(Bytes capacity, size_t domains = 1);
  Batch prepare(const std::string& domain, long dimension, slong precision,
                const std::vector<Request>& requests);
  // Discard incomplete/nonfinite or unexpectedly large allocations before
  // publishing the next batch. Completed output balls are never changed.
  void settle();
  void clear();
  Bytes bytes() const { return bytes_; }
  Bytes capacity() const { return capacity_; }
  size_t size() const { return rows_.size(); }
  static Bytes row_bytes(long length, slong precision, size_t domain_size);
  static Bytes window_bytes(long dimension, Count classes, slong precision);

 private:
  struct Key {
    std::string domain;
    long dimension;
    slong precision;
    long k, a0, gamma;
    auto operator<=>(const Key&) const = default;
  };
  struct Row {
    sic::support::ComplexBallVector values;
    Bytes charge = 0;
    Count used = 0;
    bool pinned = false, validated = false;
  };
  std::map<Key, Row> rows_;
  Bytes capacity_, domain_capacity_, bytes_ = 0;
  Count clock_ = 0;
};
}  // namespace sic::exact
