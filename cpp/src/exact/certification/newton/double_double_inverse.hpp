// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <flint/acb_mat.h>

#include "exact/runtime/row_worker_team.hpp"

namespace sic::exact {
// Private proposal kernel. Caller validates disjoint square matrices and admits
// two N*N complex double-double arrays plus three N-long index/scale arrays.
// False leaves output unpublished; no mathematical enclosure is asserted.
bool double_double_inverse(acb_mat_t output, const acb_mat_t input, RowWorkerTeam& team);
}  // namespace sic::exact
