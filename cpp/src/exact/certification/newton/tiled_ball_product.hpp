// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/acb_mat.h>

namespace sic::exact {
// Private fault/observation seam. team_start runs once before leasing workers;
// before_tile/after_tile run once per tile, never per entry. Their state must
// support simultaneous calls from different owned workers.
struct BallProductHooks {
  void* context = nullptr;
  void (*team_start)(void*, int) = nullptr;
  void (*before_tile)(void*, long) = nullptr;
  void (*after_tile)(void*, long) = nullptr;
  void (*before_admission)(void*, int) = nullptr;
};

// Inputs/output have already passed the enclosing product validation/admission.
// Every job has a fixed output window, the full inner dimension, and one FLINT
// worker. Failure propagates only after all owned callbacks have joined.
void tiled_ball_product(acb_mat_t output, const acb_mat_t left, const acb_mat_t right,
                        slong precision, int workers, const BallProductHooks* hooks);
}  // namespace sic::exact
