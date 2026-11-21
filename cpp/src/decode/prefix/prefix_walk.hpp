// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <vector>

namespace sic::decode::detail {
using PrefixCount = std::uint64_t;
using WordView = std::span<const PrefixCount>;
struct PrefixCaps {
  unsigned maximum_depth = 62, maximum_cached_states = 64;
};
struct PrefixStatistics {
  PrefixCount actual_calls = 0;
  unsigned maximum_depth = 0;
};
PrefixCount prefix_add(PrefixCount a, PrefixCount b);

// Borrows immutable word spans and factors. Caller admits this O(d) index
// before construction and keeps views/data alive throughout traversal.
class PrefixPlan {
  std::span<const WordView> words_;
  WordView factors_;
  std::vector<std::size_t> order_;
  PrefixCount count(unsigned axis, std::size_t first, std::size_t end) const;
  template <class State, class Generator, class Leaf, class AfterCall>
  void visit(const State& prefix, unsigned axis, std::size_t first, std::size_t end,
             PrefixStatistics& statistics, Generator& generator, Leaf& leaf,
             AfterCall& after) const;

 public:
  PrefixCount old_calls = 0, planned_calls = 0;
  unsigned rank = 0, required_cached_states = 0;
  PrefixPlan(std::span<const WordView> words, WordView factors, PrefixCaps caps = {});
  template <class State, class Generator, class Leaf, class AfterCall>
  void walk(const State& base, PrefixStatistics& statistics, Generator generator, Leaf leaf,
            AfterCall after) const;
  template <class State, class Generator, class Leaf>
  void walk(const State& base, PrefixStatistics& statistics, Generator generator, Leaf leaf) const;
};
}  // namespace sic::decode::detail

#include "decode/prefix/prefix_walk.tpp"
