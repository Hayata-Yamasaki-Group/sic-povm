// SPDX-License-Identifier: AGPL-3.0-or-later
// Header ODR check, translation unit A (with main). See all_exact_headers.hpp.
#include "all_exact_headers.hpp"
#include "construct/stages/dimension_two.hpp"
#include "construct/model/session_state.hpp"
#include "construct/stages/table/class_refinement.hpp"
#include "exact/analytic/direct_eta.hpp"
#include "sicx/container/stream_writer.hpp"

#include <cstdio>

int odr_unit_b();
size_t hook_layout_b();

int main() {
  const int b = odr_unit_b();
  std::printf("exact headers linked from two translation units (%d)\n", b);
  const size_t layout = sizeof(sic::construct::ProducerOptions) +
                        sizeof(sic::construct::Construction) +
                        sizeof(sic::construct::ClassRefinement) + sizeof(sic::exact::DirectEtaRoot);
  return b == 2 && hook_layout_b() == layout ? 0 : 1;
}
