// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::decode::detail {
template <class State, class Generator, class Leaf, class AfterCall>
void PrefixPlan::visit(const State& prefix, unsigned axis, std::size_t first, std::size_t end,
                       PrefixStatistics& statistics, Generator& generator, Leaf& leaf,
                       AfterCall& after) const {
  statistics.maximum_depth = std::max(statistics.maximum_depth, axis);
  if (axis == factors_.size()) {
    for (auto q = first; q < end; ++q) leaf(order_[q], prefix);
    return;
  }
  State cursor = prefix;  // exact deep copy is the caller's State contract
  PrefixCount previous = 0;
  while (first < end) {
    const auto digit = words_[order_[first]][axis];
    auto next = first + 1;
    while (next < end && words_[order_[next]][axis] == digit) ++next;
    for (auto step = previous; step < digit; ++step) {
      if (statistics.actual_calls >= planned_calls)
        throw std::logic_error("prefix walk exceeds booked calls");
      ++statistics.actual_calls;
      cursor = generator(cursor, axis);
      after(statistics.actual_calls);
    }
    visit(cursor, axis + 1, first, next, statistics, generator, leaf, after);
    previous = digit;
    first = next;
  }
}

template <class State, class Generator, class Leaf, class AfterCall>
void PrefixPlan::walk(const State& base, PrefixStatistics& statistics, Generator generator,
                      Leaf leaf, AfterCall after) const {
  if (statistics.actual_calls || statistics.maximum_depth)
    throw std::logic_error("prefix walk statistics must be fresh");
  visit(base, 0, 0, order_.size(), statistics, generator, leaf, after);
  if (statistics.actual_calls != planned_calls) throw std::logic_error("incomplete prefix walk");
}

template <class State, class Generator, class Leaf>
void PrefixPlan::walk(const State& base, PrefixStatistics& statistics, Generator generator,
                      Leaf leaf) const {
  walk(base, statistics, generator, leaf, [](PrefixCount) {});
}

}  // namespace sic::decode::detail
