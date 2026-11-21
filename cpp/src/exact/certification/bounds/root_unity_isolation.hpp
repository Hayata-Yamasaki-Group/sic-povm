// SPDX-License-Identifier: AGPL-3.0-or-later
// Identify a known root of unity using a rigorous separation bound.
#pragma once
#include <flint/acb.h>

#include <stdexcept>

namespace sic::exact {

class RootUnityIsolation {
 public:
  RootUnityIsolation(ulong order, slong precision);
  ~RootUnityIsolation();
  RootUnityIsolation(const RootUnityIsolation&) = delete;
  RootUnityIsolation& operator=(const RootUnityIsolation&) = delete;

  // Hypotheses: value contains an exact order-th root, and candidate encloses
  // one specified exact order-th root. Distinct such roots are at distance
  // at least 2*sin(pi/order), so the strict comparison proves they coincide.
  bool matches(const acb_t value, const acb_t candidate);

 private:
  slong precision_;
  arb_t separation_;
  mag_t gap_, distance_;
  acb_t difference_;
};

}  // namespace sic::exact
