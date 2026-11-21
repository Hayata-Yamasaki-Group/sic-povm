// SPDX-License-Identifier: AGPL-3.0-or-later
// Experimental point proposal only. Never replaces a Krawczyk certificate.
#pragma once
#include <flint/acb_mat.h>

#include <functional>
#include <memory>
#include <system_error>

#include "exact/runtime/row_worker_team.hpp"
#include "sic/support/ball_vectors.hpp"
#include "sic/support/stop.hpp"
#include "support/resources.hpp"

namespace sic::exact::inner_refinement {

inline sic::support::resources::Bytes scratch_bytes(sic::support::resources::Count count,
                                                    sic::support::resources::BitCount precision,
                                                    int workers) {
  return sic::support::resources::linear_correction_vectors(count, precision, workers);
}

using TeamFactory = std::function<std::unique_ptr<RowWorkerTeam>(int)>;

// Freeze J, Y, F and scale throughout the proposal. Output is replaced only
// after success; F and scale are never rounded or mutated. Caller owns and
// separately admits J, and commits z -= output only after this returns true.
// Scale uses |scale_i|, or one at a zero coordinate, solely as a heuristic.
// True promises a finite proposal, not improvement or certified convergence.
// The scratch limit excludes other class obligations. A supplied factory and
// row observer are trusted test seams; the factory must honor its worker count.
bool propose_vectors(sic::support::ComplexBallVector& output, const acb_mat_t inverse,
                     const acb_mat_t jacobian, const std::vector<acb_struct>& residual,
                     const std::vector<acb_struct>& scale, slong precision, int extra_passes,
                     int owned_workers, sic::support::resources::Bytes scratch_limit,
                     const TeamFactory& factory = {},
                     const std::function<void(long)>& initial_row_observer = {});

bool propose(sic::support::ComplexBallVector& output, const acb_mat_t inverse,
             const acb_mat_t jacobian, const sic::support::ComplexBallVector& residual,
             const sic::support::ComplexBallVector& scale, slong precision, int extra_passes,
             int owned_workers, sic::support::resources::Bytes scratch_limit,
             const TeamFactory& factory = {}, const std::function<void(long)>& observer = {});

}  // namespace sic::exact::inner_refinement
