// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <atomic>
#include <compare>
#include <functional>
#include <thread>

#include "sic/sicx/format.hpp"
#include "sic/support/flint_raii.hpp"
#include "sic/support/thread_group.hpp"
namespace sic::sicx {
// Source/visitor calls are synchronous. Pairs are borrowed only for the visitor
// invocation; sources are re-iterable and must yield identical ordered leaves.
// The source owner outlives every pass; neither side may retain visitor/pair views.
using LeafVisitor = std::function<void(uint64_t position, const fmpz* x, const fmpz* y)>;
using LeafSource = std::function<void(const LeafVisitor&)>;

struct ElementId {
  std::string kind;
  unsigned level = 0;  // field level F_level of the element (representatives: t)
  int64_t axis = -1, k = -1, position = -1;
};

struct GeneratorRescaling {
  std::vector<uint64_t> shifts;  // J_s per tower level
  std::vector<uint64_t> gain;    // diagnostics: sum of weights a_s over all nonzero leaves
  bool identity() const;
  uint64_t total() const;
  std::string text() const;
};

// Weight of the element's own level (see the table above); 0 for representatives.

// PROV record: {"rule": ..., "shifts": [J_0, ...]}. `rule` is kRescalingRule for canonical files
// The production tools write the canonical rule.
sic::support::Json rescaling_json(const GeneratorRescaling& r,
                                  const std::string& rule = kRescalingRule);

// 2-adic valuation of a nonzero pair (x, y) of O_K = Z[omega] coordinates ({1, omega} is a Z-basis,
// so 2^k | x + y omega in O_K iff 2^k | x and 2^k | y).

// Collects the minimal valuation per weight vector and the gains; compute() applies the rule.
// Weight vectors are indexed densely: elements without own weight (representatives) by their full
// position (< D; an element of F_n has zero digits from level n on), elements of level n with own
// weight w by (n, w) and their position in F_n (< N_n).
class RescalingCollector {
 public:
  explicit RescalingCollector(const Meta& m, ValuationPolicy policy = {});

  uint64_t resident_valuation_bytes() const;

  void add(const ElementId& id, const LeafSource& source);

  // Read-only borrowed valuation tables; the visitor may not retain their spans.
  void valuations(
      const std::function<void(int64_t, uint64_t, std::span<const uint32_t>)>& visit) const;
  const std::vector<uint64_t>& gains() const;

  // Applies constraints during greedy selection, before consuming slack. A post-hoc
  // clamp is incorrect because later coordinates must see the remaining valuation.
  // Consumes the tables; the collector must be discarded on any exception.
  GeneratorRescaling compute(unsigned threads, std::span<const uint64_t> ceilings = {});

 private:
  ValuationPolicy policy_;
  uint64_t allocated_bytes_ = 0;
  void admit_table(uint64_t count);

  static constexpr uint32_t kNone = std::numeric_limits<uint32_t>::max();
  std::vector<unsigned> degrees_;
  std::vector<uint64_t> width_;
  std::vector<uint64_t> gain_;
  std::vector<uint32_t> all_;
  struct WeightKey {
    unsigned level;
    uint64_t weight;
    auto operator<=>(const WeightKey&) const = default;
  };
  std::map<WeightKey, std::vector<uint32_t>> own_;
};

}  // namespace sic::sicx
