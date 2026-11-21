// SPDX-License-Identifier: AGPL-3.0-or-later
#include "decode/input/leaf_values.hpp"

namespace sic::decode {
void ElementLeaves::index() {
  std::sort(blocks.begin(), blocks.end(),
            [](const LeafBlock& a, const LeafBlock& b) { return a.base < b.base; });
  pos.clear();
  value.clear();
  for (const auto& b : blocks)
    for (size_t q = 0; q < b.pos.size(); ++q) {
      if (!pos.empty() && b.base + b.pos[q] <= pos.back())
        fail(sic::ErrorKind::invalid_format, "element leaves are not increasing");
      pos.push_back(b.base + b.pos[q]);
      value.push_back(b.values[q].get());
    }
  loaded = true;
}

void ElementLeaves::clear() {
  blocks.clear();
  pos.clear();
  value.clear();
  pos.shrink_to_fit();
  value.shrink_to_fit();
  loaded = false;
}

}  // namespace sic::decode
