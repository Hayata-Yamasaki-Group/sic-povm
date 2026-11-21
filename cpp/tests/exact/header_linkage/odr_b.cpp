// SPDX-License-Identifier: AGPL-3.0-or-later
// Header ODR check, translation unit B. See all_exact_headers.hpp.
#include "all_exact_headers.hpp"
#include "construct/stages/dimension_two.hpp"
#include "construct/model/session_state.hpp"
#include "construct/stages/table/class_refinement.hpp"
#include "exact/analytic/direct_eta.hpp"
#include "sicx/container/stream_writer.hpp"

int odr_unit_b() { return 2; }

size_t hook_layout_b() {
  return sizeof(sic::construct::ProducerOptions) + sizeof(sic::construct::Construction) +
         sizeof(sic::construct::ClassRefinement) + sizeof(sic::exact::DirectEtaRoot);
}
