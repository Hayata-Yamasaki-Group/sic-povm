// SPDX-License-Identifier: AGPL-3.0-or-later
#include "access/range_plan.hpp"

#include <algorithm>
#include <iterator>
namespace sic::access {

void add(Ranges& rs, uint64_t begin, uint64_t end) {
  if (begin == end) return;
  auto i = rs.lower_bound(begin);
  if (i != rs.begin() && std::prev(i)->second >= begin) --i;
  while (i != rs.end() && i->first <= end) {
    begin = std::min(begin, i->first);
    end = std::max(end, i->second);
    i = rs.erase(i);
  }
  rs.emplace(begin, end);
}

uint64_t total(const Ranges& rs) {
  uint64_t n = 0;
  for (auto [b, e] : rs) n += e - b;
  return n;
}

bool covered(const Ranges& rs, uint64_t begin, uint64_t end) {
  if (begin == end) return true;
  auto i = rs.upper_bound(begin);
  return i != rs.begin() && std::prev(i)->second >= end;
}

std::vector<sic::support::ByteRange> gaps(const Ranges& rs, sic::support::ByteRange r) {
  std::vector<sic::support::ByteRange> out;
  const uint64_t end = r.offset + r.size;
  uint64_t at = r.offset;
  auto i = rs.upper_bound(at);
  if (i != rs.begin()) --i;
  for (; i != rs.end() && i->first < end; ++i) {
    if (i->second <= at) continue;
    if (i->first > at) out.push_back({at, i->first - at});
    at = std::min(end, i->second);
  }
  if (at < end) out.push_back({at, end - at});
  return out;
}

}  // namespace sic::access
