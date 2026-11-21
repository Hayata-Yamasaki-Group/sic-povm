// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <flint/acb_dft.h>

#include "exact/runtime/parallel.hpp"
#include "group.hpp"
#include "sic/support/ball_vectors.hpp"
#include "support/periodic_grid.hpp"

namespace sic::exact {

void rectangular_fourier(const std::vector<long>& dimensions,
                         sic::support::ComplexBallVector& values, slong wp, int threads);

inline void group_fourier(const Group& group, sic::support::ComplexBallVector& values, slong wp,
                          int threads) {
  rectangular_fourier(group.invf, values, wp, threads);
}

}  // namespace sic::exact
