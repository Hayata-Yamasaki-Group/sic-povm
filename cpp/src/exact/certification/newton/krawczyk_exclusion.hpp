// SPDX-License-Identifier: AGPL-3.0-or-later
// Quadratic-cost obstruction for a rectangular ball-matrix Krawczyk test.
#pragma once

#include <flint/acb.h>
#include <flint/acb_mat.h>

#include <stdexcept>
#include <vector>

namespace sic::exact::krawczyk_exclusion {

struct Magnitudes {
  std::vector<mag_struct> values;
  explicit Magnitudes(long n);
  ~Magnitudes();
  Magnitudes(const Magnitudes&) = delete;
  Magnitudes& operator=(const Magnitudes&) = delete;
  mag_ptr operator[](long i) { return &values[i]; }
};

// Return a row whose radius contribution alone precludes the existing
// strict 2*sum_c |(I-YJ)_rc|*rho_c < rho_r test. A negative result means
// only that this cheap check did not prove an obstruction.
long obstructed_row(const acb_mat_t inverse, const acb_mat_t jacobian, mag_srcptr rho);

}  // namespace sic::exact::krawczyk_exclusion
