// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "exact/certification/bounds/height_bounds.hpp"
#include "exact/storage/leaf_source.hpp"
namespace sic::construct {
using ExactSource = sic::exact::storage::Source;
using sic::exact::Leaf;
using sic::exact::certified::HeightBound;
struct ActionElement {
  int i;
  int l;
  long k;
  HeightBound height;
  std::vector<Leaf> lv;
  ExactSource source;
  bool constant = false;
};  // k = -1: gamma, -2: delta, >= 0: delta a_k
}  // namespace sic::construct
