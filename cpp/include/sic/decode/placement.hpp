// SPDX-License-Identifier: AGPL-3.0-or-later
// Decoder CPU placement value and explicit affinity operations.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace sic::decode {
struct Placement {
  std::vector<int> cpus;
  std::vector<int> node_of;
  unsigned nodes = 1;
  bool pin = true;
  // Comma-separated nonnegative decimal CPU numbers or inclusive a-b ranges.
  // Empty items are ignored; order and duplicates are preserved. Values at or above
  // CPU_SETSIZE are omitted. Malformed numbers, overflow and reversed ranges
  // throw invalid_format.
  static std::vector<int> parse_list(const std::string& text);
  static Placement detect(bool pin);
  size_t slot(unsigned worker, unsigned workers) const;
  int node(unsigned worker, unsigned workers) const;
  bool bind(unsigned worker, unsigned workers) const;
};
}  // namespace sic::decode
