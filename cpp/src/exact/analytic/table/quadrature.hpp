// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "exact/analytic/table/table_view.hpp"
#include "sic/support/flint_raii.hpp"

namespace sic::exact {
namespace table_cleanup {
struct Magnitude {
  sic::support::flint::Mag value;
  Magnitude() {}
  ~Magnitude() {}
  Magnitude(const Magnitude&) = delete;
  Magnitude& operator=(const Magnitude&) = delete;
};

// Local quadrature nodes/rules must survive a recoverable admission throw.
// Cache insertion transfers ownership by swapping into an already-made node.
struct Nodes {
  std::vector<QuadratureNode> values;
  explicit Nodes(size_t count = 0);
  ~Nodes() { clear(); }
  Nodes(const Nodes&) = delete;
  Nodes& operator=(const Nodes&) = delete;
  void clear();
};
}  // namespace table_cleanup
}  // namespace sic::exact
