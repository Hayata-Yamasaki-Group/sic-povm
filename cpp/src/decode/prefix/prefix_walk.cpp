// SPDX-License-Identifier: AGPL-3.0-or-later
#include "decode/prefix/prefix_walk.hpp"

namespace sic::decode::detail {
PrefixCount prefix_add(PrefixCount a, PrefixCount b) {
  if (b > std::numeric_limits<PrefixCount>::max() - a)
    throw std::overflow_error("prefix work counter overflow");
  return a + b;
}

PrefixCount PrefixPlan::count(unsigned axis, std::size_t first, std::size_t end) const {
  if (axis == factors_.size()) return 0;
  PrefixCount result = 0, previous = 0;
  while (first < end) {
    const auto digit = words_[order_[first]][axis];
    auto next = first + 1;
    while (next < end && words_[order_[next]][axis] == digit) ++next;
    result = prefix_add(result, digit - previous);
    result = prefix_add(result, count(axis + 1, first, next));
    previous = digit;
    first = next;
  }
  return result;
}

PrefixPlan::PrefixPlan(std::span<const WordView> words, WordView factors, PrefixCaps caps)
    : words_(words), factors_(factors) {
  if (factors.empty() || factors.size() > 62 || caps.maximum_depth > 62 ||
      caps.maximum_cached_states > 64 || factors.size() > caps.maximum_depth ||
      factors.size() + 2 > caps.maximum_cached_states)
    throw std::runtime_error("prefix depth/cached-state cap refused");
  rank = factors.size();
  required_cached_states = rank + 2;
  for (auto factor : factors)
    if (factor < 2) throw std::runtime_error("invalid word factor");
  for (const auto word : words) {
    if (word.size() != rank) throw std::runtime_error("word width mismatch");
    for (unsigned axis = 0; axis < rank; ++axis) {
      if (word[axis] >= factors[axis]) throw std::runtime_error("word digit out of range");
      old_calls = prefix_add(old_calls, word[axis]);
    }
  }
  order_.resize(words.size());
  std::iota(order_.begin(), order_.end(), 0);
  std::sort(order_.begin(), order_.end(), [&](auto a, auto b) {
    const auto x = words_[a], y = words_[b];
    const auto mismatch = std::mismatch(x.begin(), x.end(), y.begin());
    return mismatch.first == x.end() ? a < b : *mismatch.first < *mismatch.second;
  });
  planned_calls = count(0, 0, order_.size());
  if (planned_calls > old_calls) throw std::logic_error("prefix plan increases work");
}

}  // namespace sic::decode::detail
