// SPDX-License-Identifier: AGPL-3.0-or-later
// Shared rank-one reconstruction from a certified normalized ghost state.
#pragma once
#include <flint/acb_dft.h>
#include <flint/ulong_extras.h>

#include <functional>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <system_error>

#include "exact/runtime/row_worker_team.hpp"
#include "sic/support/ball_vectors.hpp"
#include "sic/support/kernel_settings.hpp"
#include "sic/support/stop.hpp"
#include "support/resources.hpp"

namespace sic::exact {

struct GhostRootMetadata {
  long d = 0, n = 0, k = 0;
  int chi = 0;
  slong precision = 0, constants_precision = 0;
};

struct CertifiedGhostRoot {
  GhostRootMetadata metadata;
  sic::support::ComplexBallVector state;
};

template <class Context>
void ghost_coefficient(acb_ptr out, const Context& context, long d, long n, long original);

template <class Context, class Emit>
void reconstruct_ghost_state(const GhostRootMetadata& metadata,
                             const std::vector<acb_struct>& state, const Context& context,
                             const Emit& emit);

using GhostReconstructionTeamFactory = std::function<std::unique_ptr<RowWorkerTeam>(int)>;

// The caller must provide a disjoint-write Emit and keep its destination
// unpublished until this returns. False means no Emit occurred: use the
// unchanged serial implementation. Exceptions after dispatch propagate.
template <class Context, class Emit>
bool try_parallel_ghost_reconstruction(const GhostRootMetadata& metadata,
                                       const std::vector<acb_struct>& state, const Context& context,
                                       const Emit& emit, int owned_workers,
                                       sic::support::resources::Bytes scratch_limit,
                                       bool verbose = false,
                                       const GhostReconstructionTeamFactory& factory = {});

}  // namespace sic::exact

#include "exact/certification/reconstruction/ghost_reconstruction.tpp"
