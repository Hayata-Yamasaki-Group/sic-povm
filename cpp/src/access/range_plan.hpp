// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <map>
#include <vector>

#include "sic/support/byte_source.hpp"
namespace sic::access {

using Ranges = std::map<uint64_t, uint64_t>;  // offset -> exclusive end, disjoint and coalesced

void add(Ranges& rs, uint64_t begin, uint64_t end);
uint64_t total(const Ranges& rs);
bool covered(const Ranges& rs, uint64_t begin, uint64_t end);
std::vector<sic::support::ByteRange> gaps(const Ranges& rs, sic::support::ByteRange r);
}  // namespace sic::access
