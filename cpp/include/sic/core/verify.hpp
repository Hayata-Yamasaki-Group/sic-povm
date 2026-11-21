// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Double-precision consistency check of the WH SIC fiducial condition
//   |<psi|D_p|psi>|^2 = 1/(d+1)  for all p != 0 (mod d).

#include <cmath>

#include "sic/core/types.hpp"
#include "sic/error.hpp"

namespace sic::core {

struct VerifyResult {
  int d = 0;
  Real input_norm = 0;         // ||psi|| before normalization
  Real max_overlap_error = 0;  // max_{p != 0} | |<psi|D_p|psi>|^2 - 1/(d+1) |
  int worst_p1 = 0, worst_p2 = 0;

  bool is_fiducial(Real tol) const {
    return d > 0 && std::isfinite(input_norm) && input_norm > 0 &&
           std::isfinite(max_overlap_error) && std::isfinite(tol) && tol >= 0 &&
           max_overlap_error <= tol;
  }
};

// Normalizes a copy of psi, then streams all d^2 - 1 overlaps using O(d)
// storage and, for power-of-two d, O(d^2 log d) work. Rejects empty, zero,
// nonfinite inputs, a dimension above INT_MAX and an unrepresentable norm with
// sic::Error(invalid_argument). Does not prove exact SIC identities.
VerifyResult verify_fiducial(const Vector& psi);

}  // namespace sic::core
